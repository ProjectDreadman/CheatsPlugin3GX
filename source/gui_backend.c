// ═══════════════════════════════════════════════════════════════════════════
//  CustomCheats — GUI Backend Selector
//
//  Decides whether the framebuffer or Citro2D renderer is active this
//  session, and permanently demotes to the framebuffer renderer the
//  instant the Citro2D backend reports a problem.
// ═══════════════════════════════════════════════════════════════════════════

#include <3ds.h>
#include <string.h>

#include "gui_backend.h"
#include "cheat_menu.h"
#include "cheat_engine.h"
#include "ini_parser.h"

// gui_citro2d.h is only included when the Citro2D backend is compiled in.
// See the Makefile's GUI_CITRO2D switch — building without it (the
// default for anyone who hasn't installed citro2d) simply compiles this
// selector down to "always use the framebuffer backend", with zero
// citro2d/citro3d link-time dependency.
#ifdef CC_ENABLE_CITRO2D
#include "gui_citro2d.h"
#endif

static GuiBackendKind s_active        = GUI_BACKEND_FRAMEBUFFER;
static bool           s_citro2dFailed = false;   // sticky — never retry once it's failed this session

// ─────────────────────────────────────────────
//  Init
// ─────────────────────────────────────────────
void GB_Init(void) {
    s_active        = GUI_BACKEND_FRAMEBUFFER;
    s_citro2dFailed = false;

    IniFile cfg;
    bool wantCitro2D = false;
    if (INI_Parse(CC_CONFIG_PATH, &cfg) == 0) {
        wantCitro2D = INI_GetBool(&cfg, "CustomCheats", "gui_backend_citro2d", false);
    }

    if (!wantCitro2D) {
        CC_Log("[GB] Using framebuffer backend (citro2d backend disabled in config)");
        return;
    }

#ifdef CC_ENABLE_CITRO2D
    if (GC2D_TryInit()) {
        s_active = GUI_BACKEND_CITRO2D;
        CC_Log("[GB] Using Citro2D backend");
    } else {
        s_citro2dFailed = true;
        CC_Log("[GB] Citro2D backend unavailable this session; using framebuffer backend");
    }
#else
    CC_Log("[GB] Citro2D backend was not compiled in (build without ENABLE_CITRO2D=1); "
           "using framebuffer backend");
#endif
}

void GB_Exit(void) {
#ifdef CC_ENABLE_CITRO2D
    if (s_active == GUI_BACKEND_CITRO2D) {
        GC2D_Shutdown();
    }
#endif
    s_active = GUI_BACKEND_FRAMEBUFFER;
}

GuiBackendKind GB_GetActiveBackend(void) {
    return s_active;
}

// ─────────────────────────────────────────────
//  Per-frame routing
//
//  Input is always handled by cheat_menu.c's logic regardless of which
//  backend is drawing — that's the single source of truth for
//  navigation, by design (see cheat_menu.h's CM_GetState rationale).
// ─────────────────────────────────────────────
void GB_HandleInput(u32 kDown, u32 kHeld) {
    CM_HandleInput(kDown, kHeld);
}

void GB_Draw(void) {
#ifdef CC_ENABLE_CITRO2D
    if (s_active == GUI_BACKEND_CITRO2D) {
        if (GC2D_RenderFrame()) {
            return;
        }

        // The Citro2D backend just failed mid-session. Demote permanently
        // rather than retrying — a backend that fails once while sharing
        // someone else's GPU context is not a backend to keep gambling
        // on for the rest of this game session.
        CC_Log("[GB] Citro2D backend failed during rendering; "
               "falling back to framebuffer backend for the remainder of this session");
        GC2D_Shutdown();
        s_active        = GUI_BACKEND_FRAMEBUFFER;
        s_citro2dFailed = true;
        // Fall through and draw this frame with the framebuffer backend
        // immediately, so the player doesn't see a blank/stale screen.
    }
#endif

    CM_Draw();
}
