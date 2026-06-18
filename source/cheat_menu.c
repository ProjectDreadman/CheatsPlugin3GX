// ═══════════════════════════════════════════════════════════════════════════
//  CustomCheats — In-game Cheat Menu (framebuffer renderer)
//
//  Bottom-screen overlay drawn via direct framebuffer writes. This is the
//  default, always-safe rendering backend — it never touches the GPU
//  command queue, so it can't conflict with whatever the game itself is
//  doing with Citro3D/Citro2D this frame.
//
//  Page flow:
//    Categories  →  List (filtered)  →  Detail
//                         ↕
//                      About
//
//  A second, optional backend (gui_citro2d.c) renders the exact same
//  CheatDatabase state using Citro2D for a more polished look; which
//  backend is active is selected by gui_backend.c based on config.ini
//  and a runtime safety probe. This file only implements the logic and
//  the framebuffer drawing — it has no GPU dependency.
// ═══════════════════════════════════════════════════════════════════════════

#include <3ds.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include "cheat_menu.h"
#include "cheat_engine.h"
#include "font5x7.h"

// ─────────────────────────────────────────────
//  State
// ─────────────────────────────────────────────
static bool          s_visible     = false;
static CheatMenuPage s_page        = CM_PAGE_CATEGORIES;

static int  s_catScroll    = 0;
static int  s_catCursor    = 0;     // index into a virtual list: 0 = "All Cheats", 1..N = categories

static int  s_listScroll   = 0;
static int  s_listCursor   = 0;     // index into the FILTERED list, not g_cheats.db.cheats directly
static s32  s_activeFilter = CM_CATEGORY_ALL;

static int  s_detailLine   = 0;

static u8 *s_fb  = NULL;
static u32 s_fbW = CM_SCREEN_W;
static u32 s_fbH = CM_SCREEN_H;

#define ROWS_PER_PAGE 9

// ─────────────────────────────────────────────
//  Filtered-list helpers
//
//  Rather than maintaining a separate filtered array, we keep a small
//  index map rebuilt whenever the filter or database changes. This
//  keeps CC_ToggleCheat() working against real database indices while
//  letting the UI iterate only the cheats that match the current
//  category filter.
// ─────────────────────────────────────────────
static u32 s_filteredIndices[CC_MAX_CHEATS];
static u32 s_filteredCount = 0;

static void rebuildFilteredList(void) {
    s_filteredCount = 0;
    for (u32 i = 0; i < g_cheats.db.cheatCount; i++) {
        Cheat *c = &g_cheats.db.cheats[i];
        if (s_activeFilter == CM_CATEGORY_ALL || c->categoryIndex == s_activeFilter) {
            s_filteredIndices[s_filteredCount++] = i;
        }
    }
    if (s_listCursor >= (int)s_filteredCount) s_listCursor = (s_filteredCount > 0) ? (int)s_filteredCount - 1 : 0;
    if (s_listScroll > s_listCursor) s_listScroll = s_listCursor;
}

// ─────────────────────────────────────────────
//  Init
// ─────────────────────────────────────────────
void CM_Init(void) {
    s_visible      = false;
    s_page         = CM_PAGE_CATEGORIES;
    s_catScroll    = 0;
    s_catCursor    = 0;
    s_listScroll   = 0;
    s_listCursor   = 0;
    s_activeFilter = CM_CATEGORY_ALL;
    s_detailLine   = 0;
}

void CM_Toggle(void) {
    s_visible = !s_visible;
    CC_Log("[CM] Menu %s", s_visible ? "shown" : "hidden");
}

bool CM_IsVisible(void) {
    return s_visible;
}

// ─────────────────────────────────────────────
//  State snapshot for alternate renderers
// ─────────────────────────────────────────────
void CM_GetState(CheatMenuState *out) {
    if (!out) return;

    // Make sure the filtered-list cache reflects the current filter
    // before handing out a pointer to it (covers the case where the
    // Citro2D backend renders a frame before the framebuffer path's
    // own drawPageList() would have refreshed it).
    if (s_activeFilter != CM_CATEGORY_ALL &&
        (s_activeFilter < 0 || (u32)s_activeFilter >= g_cheats.db.categoryCount)) {
        s_activeFilter = CM_CATEGORY_ALL;
    }
    rebuildFilteredList();

    out->page           = s_page;
    out->catScroll       = s_catScroll;
    out->catCursor       = s_catCursor;
    out->listScroll      = s_listScroll;
    out->listCursor      = s_listCursor;
    out->activeFilter    = s_activeFilter;
    out->detailLine       = s_detailLine;
    out->filteredIndices = s_filteredIndices;
    out->filteredCount   = s_filteredCount;
}

// ─────────────────────────────────────────────
//  Pixel helpers — bottom screen, 240×320 column-major RGB888
// ─────────────────────────────────────────────
static inline void putPixel(int x, int y, u32 rgb) {
    if (!s_fb) return;
    if (x < 0 || x >= (int)s_fbW || y < 0 || y >= (int)s_fbH) return;

    int idx = (x * 240 + (239 - y)) * 3;
    s_fb[idx + 0] = (rgb      ) & 0xFF;
    s_fb[idx + 1] = (rgb >>  8) & 0xFF;
    s_fb[idx + 2] = (rgb >> 16) & 0xFF;
}

static void drawRect(int x, int y, int w, int h, u32 color) {
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
            putPixel(x + dx, y + dy, color);
}

static void drawText(int x, int y, u32 color, const char *text) {
    if (!text) return;
    int cx = x;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') { cx = x; y += 9; continue; }
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c > 0x7E) { cx += 6; continue; }

        const u8 *glyph = g_font5x7[c - 0x20];
        for (int col = 0; col < 5; col++) {
            u8 bits = glyph[col];
            for (int row = 0; row < 7; row++) {
                if (bits & (1 << row))
                    putPixel(cx + col, y + row, color);
            }
        }
        cx += 6;
    }
}

static void drawTextF(int x, int y, u32 color, const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    drawText(x, y, color, buf);
}

// ─────────────────────────────────────────────
//  Chrome
// ─────────────────────────────────────────────
static void drawHeader(const char *title) {
    drawRect(0, 0, CM_SCREEN_W, 14, CM_COL_HEADER);
    drawText(4, 4, CM_COL_ACCENT, "Cheats");
    int titleX = CM_SCREEN_W / 2 - (int)(strlen(title) * 3);
    drawText(titleX, 4, CM_COL_WHITE, title);
    drawRect(0, 14, CM_SCREEN_W, 1, CM_COL_ACCENT);
}

static void drawFooter(const char *hint) {
    drawRect(0, CM_SCREEN_H - 12, CM_SCREEN_W, 12, CM_COL_HEADER);
    drawRect(0, CM_SCREEN_H - 13, CM_SCREEN_W, 1, CM_COL_ACCENT);
    drawText(4, CM_SCREEN_H - 10, CM_COL_TEXT, hint);
}

// ─────────────────────────────────────────────
//  Categories page — entry point
//
//  Row 0 is always "All Cheats (N)". Rows 1..categoryCount are the
//  parsed categories, each showing "Name (enabled/total)". Cheats with
//  no category never appear here as their own row — they're reachable
//  only through "All Cheats" — so a flat cheat file with zero [Category]
//  headers shows a single, uncluttered entry point.
// ─────────────────────────────────────────────
static int categoryRowCount(void) {
    return 1 + (int)g_cheats.db.categoryCount; // +1 for "All Cheats"
}

static void drawPageCategories(void) {
    drawHeader("Categories");

    if (!g_cheats.db.loaded || g_cheats.db.cheatCount == 0) {
        drawText(8, 60, CM_COL_TEXT, "No cheat file found for this game.");
        drawText(8, 72, CM_COL_TEXT, "Place a file at:");
        drawTextF(8, 84, CM_COL_YELLOW, "/cheats/%016llX.txt",
                  (unsigned long long)g_cheats.db.titleId);
        drawFooter("X: reload from SD");
        return;
    }

    int total = categoryRowCount();
    int y = 18;
    int end = s_catScroll + ROWS_PER_PAGE;
    if (end > total) end = total;

    for (int row = s_catScroll; row < end; row++) {
        bool selected = (row == s_catCursor);
        if (selected) drawRect(0, y - 1, CM_SCREEN_W, 10, CM_COL_ACCENT);

        if (row == 0) {
            drawTextF(8, y, CM_COL_WHITE, "All Cheats (%lu)",
                       (unsigned long)g_cheats.db.cheatCount);
        } else {
            CheatCategory *cat = &g_cheats.db.categories[row - 1];
            drawTextF(8, y, CM_COL_WHITE, "%s (%lu/%lu)",
                       cat->name,
                       (unsigned long)cat->enabledCount,
                       (unsigned long)cat->cheatCount);
        }
        y += 11;
    }

    drawFooter("A: open  X: reload  Up/Down");
}

// ─────────────────────────────────────────────
//  List page — filtered by s_activeFilter
// ─────────────────────────────────────────────
static void drawPageList(void) {
    // A reload can shrink the category table out from under an active
    // filter; fall back to "All Cheats" rather than reading past the end.
    if (s_activeFilter != CM_CATEGORY_ALL &&
        (s_activeFilter < 0 || (u32)s_activeFilter >= g_cheats.db.categoryCount)) {
        s_activeFilter = CM_CATEGORY_ALL;
    }

    rebuildFilteredList(); // cheap (≤256 entries); keeps the view correct after a reload

    const char *headerText = (s_activeFilter == CM_CATEGORY_ALL)
        ? "All Cheats"
        : g_cheats.db.categories[s_activeFilter].name;
    drawHeader(headerText);

    if (s_filteredCount == 0) {
        drawText(8, 60, CM_COL_TEXT, "No cheats in this category.");
        drawFooter("B: back to categories");
        return;
    }

    int y = 18;
    int end = s_listScroll + ROWS_PER_PAGE;
    if (end > (int)s_filteredCount) end = (int)s_filteredCount;

    for (int row = s_listScroll; row < end; row++) {
        u32 realIdx = s_filteredIndices[row];
        Cheat *c = &g_cheats.db.cheats[realIdx];
        bool selected = (row == s_listCursor);

        if (selected) drawRect(0, y - 1, CM_SCREEN_W, 10, CM_COL_ACCENT);

        u32 stateCol;
        const char *stateGlyph;
        if (c->hasError)      { stateCol = CM_COL_GREY;  stateGlyph = "!"; }
        else if (c->enabled)  { stateCol = CM_COL_GREEN; stateGlyph = "+"; }
        else                  { stateCol = CM_COL_RED;   stateGlyph = "-"; }

        drawText(4, y, stateCol, stateGlyph);

        u32 nameCol = c->hasError ? CM_COL_GREY : CM_COL_WHITE;
        drawText(14, y, nameCol, c->name);
        y += 11;
    }

    if (s_filteredCount > ROWS_PER_PAGE) {
        drawTextF(CM_SCREEN_W - 34, CM_SCREEN_H - 24, CM_COL_TEXT,
                  "%d/%lu", s_listCursor + 1, (unsigned long)s_filteredCount);
    }

    drawFooter("A: toggle  X: details  B: back");
}

// ─────────────────────────────────────────────
//  Detail page
// ─────────────────────────────────────────────
static void drawPageDetail(void) {
    if (s_filteredCount == 0) { s_page = CM_PAGE_LIST; return; }

    u32 realIdx = s_filteredIndices[s_listCursor];
    Cheat *c = &g_cheats.db.cheats[realIdx];
    drawHeader("Detail");

    int y = 18;
    drawText(8, y, CM_COL_WHITE, c->name); y += 11;

    if (c->category[0]) {
        drawTextF(8, y, CM_COL_GREY, "Category: %s", c->category); y += 10;
    }
    drawTextF(8, y, c->hasError ? CM_COL_RED : (c->enabled ? CM_COL_GREEN : CM_COL_TEXT),
              "Status: %s", c->hasError ? "Unsupported" : (c->enabled ? "Enabled" : "Disabled"));
    y += 10;
    drawTextF(8, y, CM_COL_TEXT, "Type: %s", c->isOneShot ? "One-shot" : "Continuous");
    y += 10;

    if (c->note[0]) {
        drawTextF(8, y, CM_COL_YELLOW, "Note: %s", c->note);
        y += 12;
    }

    drawRect(8, y, CM_SCREEN_W - 16, 1, CM_COL_ACCENT);
    y += 6;

    int visibleRows = (CM_SCREEN_H - 12 - y) / 9;
    int start = s_detailLine;
    int end = start + visibleRows;
    if (end > (int)c->lineCount) end = (int)c->lineCount;

    for (int i = start; i < end; i++) {
        drawTextF(8, y, CM_COL_TEXT, "%08lX %08lX",
                  (unsigned long)c->lines[i].addr,
                  (unsigned long)c->lines[i].val);
        y += 9;
    }

    if ((int)c->lineCount > visibleRows) {
        drawTextF(CM_SCREEN_W - 40, CM_SCREEN_H - 24, CM_COL_GREY,
                  "%d/%lu", s_detailLine + 1, (unsigned long)c->lineCount);
    }

    drawFooter("B: back  Up/Down: scroll codes");
}

// ─────────────────────────────────────────────
//  About page
// ─────────────────────────────────────────────
static void drawPageAbout(void) {
    drawHeader("About");
    int y = 22;
    drawText(8, y, CM_COL_WHITE, "CustomCheats Plugin"); y += 14;
    drawText(8, y, CM_COL_TEXT, "Luma3DS 3GX Plugin");   y += 14;
    drawText(8, y, CM_COL_TEXT, "Cheat files:");          y += 10;
    drawText(16, y, CM_COL_YELLOW, "/cheats/<titleid>.txt"); y += 14;
    drawText(8, y, CM_COL_TEXT, "Toggle state saved to:");  y += 10;
    drawText(16, y, CM_COL_YELLOW, "/luma/plugins/CustomCheats/"); y += 9;
    drawText(16, y, CM_COL_YELLOW, "  state/<titleid>.ini"); y += 14;
    drawText(8, y, CM_COL_TEXT, "Hotkey: R + SELECT");  y += 10;
    drawText(8, y, CM_COL_TEXT, "X on Categories: reload cheats.txt");
    drawFooter("B: back");
}

// ─────────────────────────────────────────────
//  Main draw entry
// ─────────────────────────────────────────────
void CM_Draw(void) {
    if (!s_visible) return;

    s_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (!s_fb) return;

    drawRect(0, 0, CM_SCREEN_W, CM_SCREEN_H, CM_COL_BG);

    switch (s_page) {
        case CM_PAGE_CATEGORIES: drawPageCategories(); break;
        case CM_PAGE_LIST:       drawPageList();       break;
        case CM_PAGE_DETAIL:     drawPageDetail();      break;
        case CM_PAGE_ABOUT:      drawPageAbout();        break;
    }
}

// ─────────────────────────────────────────────
//  Input
// ─────────────────────────────────────────────
void CM_HandleInput(u32 kDown, u32 kHeld) {
    (void)kHeld;

    switch (s_page) {

        case CM_PAGE_CATEGORIES: {
            int total = categoryRowCount();

            if (kDown & KEY_DOWN) {
                if (s_catCursor < total - 1) {
                    s_catCursor++;
                    if (s_catCursor >= s_catScroll + ROWS_PER_PAGE) s_catScroll++;
                }
            }
            if (kDown & KEY_UP) {
                if (s_catCursor > 0) {
                    s_catCursor--;
                    if (s_catCursor < s_catScroll) s_catScroll--;
                }
            }
            if (kDown & KEY_A) {
                s_activeFilter = (s_catCursor == 0) ? CM_CATEGORY_ALL : (s32)(s_catCursor - 1);
                s_listScroll = 0;
                s_listCursor = 0;
                rebuildFilteredList();
                s_page = CM_PAGE_LIST;
            }
            if (kDown & KEY_X) {
                // Hot-reload the cheat file from SD without restarting the game
                CC_ReloadDatabase();
                s_catCursor = 0;
                s_catScroll = 0;
            }
            if (kDown & KEY_Y) {
                s_page = CM_PAGE_ABOUT;
            }
            break;
        }

        case CM_PAGE_LIST: {
            if (kDown & KEY_B) {
                s_page = CM_PAGE_CATEGORIES;
                break;
            }
            if (s_filteredCount > 0) {
                if (kDown & KEY_DOWN) {
                    if (s_listCursor < (int)s_filteredCount - 1) {
                        s_listCursor++;
                        if (s_listCursor >= s_listScroll + ROWS_PER_PAGE) s_listScroll++;
                    }
                }
                if (kDown & KEY_UP) {
                    if (s_listCursor > 0) {
                        s_listCursor--;
                        if (s_listCursor < s_listScroll) s_listScroll--;
                    }
                }
                if (kDown & KEY_A) {
                    CC_ToggleCheat(s_filteredIndices[s_listCursor]);
                }
                if (kDown & KEY_X) {
                    s_detailLine = 0;
                    s_page = CM_PAGE_DETAIL;
                }
            }
            break;
        }

        case CM_PAGE_DETAIL: {
            if (kDown & KEY_B) { s_page = CM_PAGE_LIST; break; }
            if (s_filteredCount > 0) {
                Cheat *c = &g_cheats.db.cheats[s_filteredIndices[s_listCursor]];
                if (kDown & KEY_DOWN) {
                    if (s_detailLine < (int)c->lineCount - 1) s_detailLine++;
                }
                if (kDown & KEY_UP) {
                    if (s_detailLine > 0) s_detailLine--;
                }
            }
            break;
        }

        case CM_PAGE_ABOUT: {
            if (kDown & (KEY_B | KEY_Y)) s_page = CM_PAGE_CATEGORIES;
            break;
        }
    }
}
