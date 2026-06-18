#pragma once
#ifndef GUI_BACKEND_H
#define GUI_BACKEND_H

#include <3ds.h>
#include <stdbool.h>

// ═══════════════════════════════════════════════════════════════════════════
//  GUI backend selector
//
//  CustomCheats ships two rendering backends for the in-game menu:
//
//   • Framebuffer (cheat_menu.c) — direct pixel writes to the bottom
//     screen's framebuffer. Zero GPU interaction. Always safe to use
//     inside another process's game loop, which is what a 3GX plugin
//     actually is. This is the default and the fallback.
//
//   • Citro2D (gui_citro2d.c) — proper GPU-accelerated 2D rendering
//     (anti-aliased shapes, scalable text, smooth scrolling) using
//     citro2d/citro3d.
//
//  IMPORTANT — read before enabling the Citro2D backend:
//
//   A 3GX plugin's code runs *inside the game's own process*, sharing
//   its address space and, more importantly, its GPU command queue.
//   By the time our plugin thread runs, the game has almost certainly
//   already called C3D_Init() and owns the active render targets.
//
//   The Citro2D backend in this plugin does NOT call C3D_Init() itself.
//   Instead it:
//     1. Detects whether citro3d is already initialized in this process
//        (GB_ProbeC3DContext) before touching any Citro3D/Citro2D call.
//     2. Only ever draws to the BOTTOM screen target, attempting to
//        reuse the game's existing context rather than creating a
//        competing one.
//     3. Falls back to the framebuffer backend automatically — and
//        permanently, for the rest of that session — the moment
//        anything about the GPU draw call looks wrong (failed target
//        creation, failed frame begin, etc).
//
//   Even with those guards, drawing into another process's live GPU
//   command stream is inherently more fragile than the framebuffer
//   path: behavior can vary between games depending on exactly when
//   in their frame loop our hotkey fires. Treat the Citro2D backend as
//   an experimental, opt-in visual upgrade — not a guaranteed-safe
//   default — which is why it is OFF by default in config.ini.
// ═══════════════════════════════════════════════════════════════════════════

typedef enum {
    GUI_BACKEND_FRAMEBUFFER = 0,   // always available, always safe
    GUI_BACKEND_CITRO2D     = 1,   // opt-in, GPU-accelerated, auto-falls-back
} GuiBackendKind;

// ─────────────────────────────────────────────
//  API
// ─────────────────────────────────────────────

// Called once at startup, after config is loaded. Decides which backend
// to actually use this session based on config + a runtime safety probe.
void GB_Init(void);

// Called on plugin unload to release any GPU resources the Citro2D
// backend allocated (no-op if the framebuffer backend is active).
void GB_Exit(void);

// Replaces the direct CM_Draw()/CM_HandleInput() calls in main.c — routes
// to whichever backend is active, and transparently demotes to the
// framebuffer backend if the Citro2D backend reports a failure.
void GB_Draw(void);
void GB_HandleInput(u32 kDown, u32 kHeld);

// Which backend ended up active this session (useful for the About page
// and for diagnostics in the log).
GuiBackendKind GB_GetActiveBackend(void);

#endif // GUI_BACKEND_H
