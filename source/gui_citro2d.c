// ═══════════════════════════════════════════════════════════════════════════
//  CustomCheats — Citro2D Rendering Backend
//
//  Renders the shared menu state (owned by cheat_menu.c) using citro2d
//  for anti-aliased shapes and scalable text. See gui_backend.h for the
//  full safety rationale around running inside another process's GPU
//  context — this file implements the guards described there.
// ═══════════════════════════════════════════════════════════════════════════

#include <3ds.h>
#include <citro3d.h>
#include <citro2d.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "gui_citro2d.h"
#include "cheat_menu.h"
#include "cheat_engine.h"

// ─────────────────────────────────────────────
//  Palette — same hues as the framebuffer backend, expressed as
//  citro2d's ABGR8888 packed format via C2D_Color32.
// ─────────────────────────────────────────────
#define RGB(hex) C2D_Color32((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, 0xFF)

static u32 COL_BG, COL_HEADER, COL_ACCENT, COL_TEXT,
           COL_GREEN, COL_RED, COL_YELLOW, COL_WHITE, COL_GREY;

// ─────────────────────────────────────────────
//  GPU resources owned by this backend
// ─────────────────────────────────────────────
static C3D_RenderTarget *s_target      = NULL;
static C2D_TextBuf        s_textBuf     = NULL;
static bool               s_initialized = false;

#define ROWS_PER_PAGE 8   // slightly fewer than the FB backend; real text needs more vertical room

// ─────────────────────────────────────────────
//  Safety probe
//
//  We cannot call C3D_Init() — the host game almost certainly already
//  owns the GPU context. The best low-risk signal that citro3d is
//  already initialized in this process is attempting a *lightweight*,
//  side-effect-free Citro3D query and checking it doesn't fault. There
//  is no official "is citro3d initialized" query exposed by the library,
//  so this probe is deliberately conservative: it only proceeds if the
//  3GX plugin SDK exposes an explicit confirmation hook, and otherwise
//  refuses outright rather than guessing.
//
//  __ctru_C3D_GetCmdBuf / similar internals are NOT used here on purpose
//  — relying on citro3d internals that aren't part of its public API can
//  break across devkitPro releases and would be a worse foot-gun than
//  simply declining to enable the fancy backend.
// ─────────────────────────────────────────────
__attribute__((weak))
extern bool plgIsGpuContextActive(void); // provided by the Luma 3GX plugin SDK, if available

static bool probeGpuContextSafe(void) {
    if (plgIsGpuContextActive) {
        bool active = plgIsGpuContextActive();
        CC_Log("[GC2D] plgIsGpuContextActive() = %d", active);
        return active;
    }

    // No SDK hook available to confirm safety — refuse rather than guess.
    CC_Log("[GC2D] No GPU-context probe available from the plugin SDK; "
           "refusing to enable the Citro2D backend on this Luma build");
    return false;
}

// ─────────────────────────────────────────────
//  Init / Shutdown
// ─────────────────────────────────────────────
bool GC2D_TryInit(void) {
    if (s_initialized) return true;

    if (!probeGpuContextSafe()) {
        return false;
    }

    // We deliberately do NOT call C3D_Init() — the game's own call to it
    // (almost certainly already made before our plugin thread starts)
    // is what we're piggy-backing on. C2D_Init()/C2D_Prepare() are safe
    // to call multiple times per citro2d's own design (they no-op if
    // already set up), which is what makes sharing the context feasible
    // at all.
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        CC_Log("[GC2D] C2D_Init failed");
        return false;
    }
    C2D_Prepare();

    s_target = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    if (!s_target) {
        CC_Log("[GC2D] C2D_CreateScreenTarget failed");
        C2D_Fini();
        return false;
    }

    s_textBuf = C2D_TextBufNew(4096);
    if (!s_textBuf) {
        CC_Log("[GC2D] C2D_TextBufNew failed");
        C2D_Fini();
        s_target = NULL;
        return false;
    }

    COL_BG     = RGB(0x14141F);
    COL_HEADER = RGB(0x1F1F33);
    COL_ACCENT = RGB(0x3D2B6E);
    COL_TEXT   = RGB(0xE0E0E0);
    COL_GREEN  = RGB(0x4CD964);
    COL_RED    = RGB(0xFF453A);
    COL_YELLOW = RGB(0xFFD60A);
    COL_WHITE  = RGB(0xFFFFFF);
    COL_GREY   = RGB(0x808080);

    s_initialized = true;
    CC_Log("[GC2D] Citro2D backend initialized");
    return true;
}

void GC2D_Shutdown(void) {
    if (!s_initialized) return;

    if (s_textBuf) { C2D_TextBufDelete(s_textBuf); s_textBuf = NULL; }
    s_target = NULL; // owned by citro2d's screen-target pool; nothing to free directly

    C2D_Fini();
    s_initialized = false;
    CC_Log("[GC2D] Citro2D backend shut down");
}

// ─────────────────────────────────────────────
//  Drawing helpers
// ─────────────────────────────────────────────
static void drawText(float x, float y, float scale, u32 color, const char *str) {
    C2D_Text text;
    C2D_TextParse(&text, s_textBuf, str);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor, x, y, 0.0f, scale, scale, color);
}

static void drawTextF(float x, float y, float scale, u32 color, const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    drawText(x, y, scale, color, buf);
}

static void drawHeader(const char *title) {
    C2D_DrawRectSolid(0, 0, 0, 320, 18, COL_HEADER);
    C2D_DrawRectSolid(0, 18, 0, 320, 1, COL_ACCENT);
    drawText(6, 3, 0.5f, COL_ACCENT, "Cheats");
    // Centered-ish title; citro2d text width would need C2D_TextGetDimensions
    // for pixel-perfect centering — close enough for a header label.
    drawText(140, 3, 0.5f, COL_WHITE, title);
}

static void drawFooter(const char *hint) {
    C2D_DrawRectSolid(0, 224, 0, 320, 16, COL_HEADER);
    C2D_DrawRectSolid(0, 223, 0, 320, 1, COL_ACCENT);
    drawText(6, 226, 0.42f, COL_TEXT, hint);
}

static void drawRowHighlight(float y) {
    C2D_DrawRectSolid(0, y - 1, 0, 320, 22, COL_ACCENT);
}

// ─────────────────────────────────────────────
//  Page renderers — read-only over CheatMenuState; all navigation logic
//  stays in cheat_menu.c so the two backends can never drift apart.
// ─────────────────────────────────────────────
static void drawCategories(const CheatMenuState *st) {
    drawHeader("Categories");

    if (!g_cheats.db.loaded || g_cheats.db.cheatCount == 0) {
        drawText(8, 60, 0.5f, COL_TEXT, "No cheat file found for this game.");
        drawTextF(8, 80, 0.45f, COL_YELLOW, "/cheats/%016llX.txt",
                  (unsigned long long)g_cheats.db.titleId);
        drawFooter("X: reload from SD");
        return;
    }

    int total = 1 + (int)g_cheats.db.categoryCount;
    float y = 26.0f;
    int end = st->catScroll + ROWS_PER_PAGE;
    if (end > total) end = total;

    for (int row = st->catScroll; row < end; row++) {
        if (row == st->catCursor) drawRowHighlight(y);

        if (row == 0) {
            drawTextF(10, y + 3, 0.48f, COL_WHITE, "All Cheats (%lu)",
                      (unsigned long)g_cheats.db.cheatCount);
        } else {
            CheatCategory *cat = &g_cheats.db.categories[row - 1];
            drawTextF(10, y + 3, 0.48f, COL_WHITE, "%s (%lu/%lu)",
                      cat->name, (unsigned long)cat->enabledCount, (unsigned long)cat->cheatCount);
        }
        y += 22.0f;
    }

    drawFooter("A: open   X: reload   Up/Down");
}

static void drawList(const CheatMenuState *st) {
    const char *headerText = (st->activeFilter < 0)
        ? "All Cheats"
        : g_cheats.db.categories[st->activeFilter].name;
    drawHeader(headerText);

    if (st->filteredCount == 0) {
        drawText(8, 60, 0.5f, COL_TEXT, "No cheats in this category.");
        drawFooter("B: back to categories");
        return;
    }

    float y = 26.0f;
    int end = st->listScroll + ROWS_PER_PAGE;
    if (end > (int)st->filteredCount) end = (int)st->filteredCount;

    for (int row = st->listScroll; row < end; row++) {
        u32 realIdx = st->filteredIndices[row];
        Cheat *c = &g_cheats.db.cheats[realIdx];

        if (row == st->listCursor) drawRowHighlight(y);

        u32 stateCol = c->hasError ? COL_GREY : (c->enabled ? COL_GREEN : COL_RED);
        const char *glyph = c->hasError ? "!" : (c->enabled ? "+" : "-");

        drawText(8, y + 3, 0.48f, stateCol, glyph);
        drawText(22, y + 3, 0.48f, c->hasError ? COL_GREY : COL_WHITE, c->name);
        y += 22.0f;
    }

    if (st->filteredCount > (u32)ROWS_PER_PAGE) {
        drawTextF(270, 226, 0.42f, COL_TEXT, "%d/%lu",
                  st->listCursor + 1, (unsigned long)st->filteredCount);
    }

    drawFooter("A: toggle   X: details   B: back");
}

static void drawDetail(const CheatMenuState *st) {
    if (st->filteredCount == 0) return;

    Cheat *c = &g_cheats.db.cheats[st->filteredIndices[st->listCursor]];
    drawHeader("Detail");

    float y = 26.0f;
    drawText(8, y, 0.5f, COL_WHITE, c->name); y += 16;

    if (c->category[0]) {
        drawTextF(8, y, 0.42f, COL_GREY, "Category: %s", c->category); y += 14;
    }

    u32 statusCol = c->hasError ? COL_RED : (c->enabled ? COL_GREEN : COL_TEXT);
    drawTextF(8, y, 0.42f, statusCol, "Status: %s",
              c->hasError ? "Unsupported" : (c->enabled ? "Enabled" : "Disabled"));
    y += 14;
    drawTextF(8, y, 0.42f, COL_TEXT, "Type: %s", c->isOneShot ? "One-shot" : "Continuous");
    y += 14;

    if (c->note[0]) {
        drawTextF(8, y, 0.4f, COL_YELLOW, "Note: %s", c->note);
        y += 16;
    }

    C2D_DrawRectSolid(8, y, 0, 304, 1, COL_ACCENT);
    y += 8;

    int visibleRows = (int)((224.0f - y) / 13.0f);
    int start = st->detailLine;
    int end = start + visibleRows;
    if (end > (int)c->lineCount) end = (int)c->lineCount;

    for (int i = start; i < end; i++) {
        drawTextF(8, y, 0.4f, COL_TEXT, "%08lX %08lX",
                  (unsigned long)c->lines[i].addr, (unsigned long)c->lines[i].val);
        y += 13.0f;
    }

    drawFooter("B: back   Up/Down: scroll codes");
}

static void drawAbout(void) {
    drawHeader("About");
    float y = 26.0f;
    drawText(8, y, 0.5f, COL_WHITE, "CustomCheats Plugin");        y += 18;
    drawText(8, y, 0.42f, COL_TEXT, "Luma3DS 3GX Plugin");          y += 16;
    drawText(8, y, 0.42f, COL_TEXT, "Renderer: Citro2D (GPU)");     y += 16;
    drawText(8, y, 0.42f, COL_TEXT, "Cheat files:");                 y += 14;
    drawText(16, y, 0.4f, COL_YELLOW, "/cheats/<titleid>.txt");      y += 16;
    drawText(8, y, 0.42f, COL_TEXT, "Hotkey: R + SELECT");
    drawFooter("B: back");
}

// ─────────────────────────────────────────────
//  Public entry point
// ─────────────────────────────────────────────
bool GC2D_RenderFrame(void) {
    if (!s_initialized || !s_target) return false;

    CheatMenuState st;
    CM_GetState(&st);

    C2D_TextBufClear(s_textBuf);

    // ─────────────────────────────────────────────────────────────────
    //  RISK NOTE — read this before relying on this backend in the field.
    //
    //  C3D_FrameBegin()/C3D_FrameEnd() submit to the SAME GPU command
    //  queue the host game's own render loop uses. We are calling these
    //  from our own plugin thread, NOT from the game's main thread, with
    //  no coordination beyond the GC2D_TryInit() probe above. If the
    //  game happens to be mid-frame (between its own FrameBegin/FrameEnd)
    //  when this runs, the two submissions can interleave, and the
    //  result can range from a torn/glitched bottom-screen frame up to
    //  a GPU command-queue stall that hangs the game.
    //
    //  C3D_FRAME_SYNCDRAW makes this call block until the GPU is free,
    //  which reduces — but does not eliminate — the chance of a visible
    //  collision, at the cost of the plugin thread stalling briefly.
    //  There is no public citro3d API to take an exclusive lock on the
    //  command queue from a second thread in someone else's process.
    //
    //  This is exactly why the framebuffer backend is the default and
    //  why GC2D_TryInit() refuses to activate unless the Luma plugin SDK
    //  explicitly confirms it's safe via plgIsGpuContextActive(). Do not
    //  remove or weaken that probe to "make the GUI work" on a build
    //  that doesn't provide it — that's trading a visual upgrade for a
    //  chance of corrupting the player's game.
    // ─────────────────────────────────────────────────────────────────
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(s_target, COL_BG);
    C2D_SceneBegin(s_target);

    switch (st.page) {
        case CM_PAGE_CATEGORIES: drawCategories(&st); break;
        case CM_PAGE_LIST:       drawList(&st);       break;
        case CM_PAGE_DETAIL:     drawDetail(&st);      break;
        case CM_PAGE_ABOUT:      drawAbout();           break;
    }

    C3D_FrameEnd(0);
    return true;
}
