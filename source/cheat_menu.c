// ═══════════════════════════════════════════════════════════════════════════
//  CustomCheats — In-game Cheat Menu
//  Bottom-screen overlay drawn via direct framebuffer writes.
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
static bool          s_visible    = false;
static CheatMenuPage s_page       = CM_PAGE_LIST;
static int           s_scroll     = 0;
static int           s_cursor     = 0;
static int           s_detailLine = 0;   // scroll offset for the detail view

static u8 *s_fb = NULL;
static u32 s_fbW = CM_SCREEN_W;
static u32 s_fbH = CM_SCREEN_H;

#define ROWS_PER_PAGE 9

// ─────────────────────────────────────────────
//  Init
// ─────────────────────────────────────────────
void CM_Init(void) {
    s_visible    = false;
    s_page       = CM_PAGE_LIST;
    s_scroll     = 0;
    s_cursor     = 0;
    s_detailLine = 0;
}

void CM_Toggle(void) {
    s_visible = !s_visible;
    CC_Log("[CM] Menu %s", s_visible ? "shown" : "hidden");
}

bool CM_IsVisible(void) {
    return s_visible;
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
//  List page
// ─────────────────────────────────────────────
static void drawPageList(void) {
    char header[32];
    snprintf(header, sizeof(header), "Cheats (%lu)", (unsigned long)g_cheats.db.cheatCount);
    drawHeader(header);

    if (!g_cheats.db.loaded || g_cheats.db.cheatCount == 0) {
        drawText(8, 60, CM_COL_TEXT, "No cheat file found for this game.");
        drawText(8, 72, CM_COL_TEXT, "Place a file at:");
        drawTextF(8, 84, CM_COL_YELLOW, "/cheats/%016llX.txt",
                  (unsigned long long)g_cheats.db.titleId);
        drawFooter("L/R: page");
        return;
    }

    int y = 18;
    int end = s_scroll + ROWS_PER_PAGE;
    if (end > (int)g_cheats.db.cheatCount) end = (int)g_cheats.db.cheatCount;

    for (int i = s_scroll; i < end; i++) {
        Cheat *c = &g_cheats.db.cheats[i];
        bool selected = (i == s_cursor);

        if (selected)
            drawRect(0, y - 1, CM_SCREEN_W, 10, CM_COL_ACCENT);

        u32 stateCol;
        const char *stateGlyph;
        if (c->hasError)      { stateCol = CM_COL_GREY;   stateGlyph = "!"; }
        else if (c->enabled)  { stateCol = CM_COL_GREEN;  stateGlyph = "+"; }
        else                  { stateCol = CM_COL_RED;    stateGlyph = "-"; }

        drawText(4, y, stateCol, stateGlyph);

        u32 nameCol = c->hasError ? CM_COL_GREY : CM_COL_WHITE;
        drawText(14, y, nameCol, c->name);

        if (c->category[0]) {
            int catX = CM_SCREEN_W - (int)(strlen(c->category) * 6) - 30;
            if (catX > 120) drawText(catX, y, CM_COL_GREY, c->category);
        }
        y += 11;
    }

    if (g_cheats.db.cheatCount > ROWS_PER_PAGE) {
        drawTextF(CM_SCREEN_W - 34, CM_SCREEN_H - 24, CM_COL_TEXT,
                  "%d/%lu", s_cursor + 1, (unsigned long)g_cheats.db.cheatCount);
    }

    drawFooter("A: toggle  X: details  Up/Down/L/R");
}

// ─────────────────────────────────────────────
//  Detail page — raw codes + note for the selected cheat
// ─────────────────────────────────────────────
static void drawPageDetail(void) {
    if (g_cheats.db.cheatCount == 0) { s_page = CM_PAGE_LIST; return; }

    Cheat *c = &g_cheats.db.cheats[s_cursor];
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

    // Scrollable code listing
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
    drawText(8, y, CM_COL_TEXT, "Hotkey: R + SELECT");
    drawFooter("L/R: page");
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
        case CM_PAGE_LIST:   drawPageList();   break;
        case CM_PAGE_DETAIL: drawPageDetail(); break;
        case CM_PAGE_ABOUT:  drawPageAbout();  break;
    }
}

// ─────────────────────────────────────────────
//  Input
// ─────────────────────────────────────────────
void CM_HandleInput(u32 kDown, u32 kHeld) {
    (void)kHeld;

    if (s_page == CM_PAGE_LIST) {
        if (kDown & KEY_R) s_page = CM_PAGE_ABOUT;
        if (kDown & KEY_L) s_page = CM_PAGE_ABOUT;

        if (g_cheats.db.cheatCount > 0) {
            if (kDown & KEY_DOWN) {
                if (s_cursor < (int)g_cheats.db.cheatCount - 1) {
                    s_cursor++;
                    if (s_cursor >= s_scroll + ROWS_PER_PAGE) s_scroll++;
                }
            }
            if (kDown & KEY_UP) {
                if (s_cursor > 0) {
                    s_cursor--;
                    if (s_cursor < s_scroll) s_scroll--;
                }
            }
            if (kDown & KEY_A) {
                CC_ToggleCheat((u32)s_cursor);
            }
            if (kDown & KEY_X) {
                s_detailLine = 0;
                s_page = CM_PAGE_DETAIL;
            }
        }
    }
    else if (s_page == CM_PAGE_DETAIL) {
        Cheat *c = (g_cheats.db.cheatCount > 0) ? &g_cheats.db.cheats[s_cursor] : NULL;
        if (kDown & KEY_B) s_page = CM_PAGE_LIST;
        if (c) {
            if (kDown & KEY_DOWN) {
                if (s_detailLine < (int)c->lineCount - 1) s_detailLine++;
            }
            if (kDown & KEY_UP) {
                if (s_detailLine > 0) s_detailLine--;
            }
        }
    }
    else if (s_page == CM_PAGE_ABOUT) {
        if (kDown & KEY_L) s_page = CM_PAGE_LIST;
        if (kDown & KEY_R) s_page = CM_PAGE_LIST;
        if (kDown & KEY_B) s_page = CM_PAGE_LIST;
    }
}
