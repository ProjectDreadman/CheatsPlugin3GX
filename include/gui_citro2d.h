#pragma once
#ifndef GUI_CITRO2D_H
#define GUI_CITRO2D_H

#include <3ds.h>
#include <stdbool.h>

// ═══════════════════════════════════════════════════════════════════════════
//  Citro2D rendering backend for the cheat menu.
//
//  Draws the exact same logical menu state (categories → list → detail →
//  about) as cheat_menu.c, but using citro2d primitives: anti-aliased
//  rounded rectangles, a real scalable font via C2D_TextBuf, and smooth
//  highlight transitions instead of hard pixel blocks.
//
//  This file does NOT call C3D_Init(). See gui_backend.h for the full
//  rationale. GC2D_TryInit() probes whether it's safe to proceed and
//  returns false immediately if anything looks off, so the caller
//  (gui_backend.c) can fall back to the framebuffer renderer.
// ═══════════════════════════════════════════════════════════════════════════

// Attempt to set up the Citro2D backend for this session.
// Returns true only if every step succeeded; on false, no Citro2D/Citro3D
// resources were left allocated (full internal cleanup on partial failure).
bool GC2D_TryInit(void);

// Tear down whatever GC2D_TryInit() allocated. Safe to call even if
// TryInit failed or was never called.
void GC2D_Shutdown(void);

// Render one frame of the current menu state to the bottom screen.
// Returns false if a draw call failed this frame (caller should treat
// this as fatal for the rest of the session and fall back permanently).
bool GC2D_RenderFrame(void);

// Input handling is identical in shape to the framebuffer menu's, and is
// implemented by delegating to the same logical state in cheat_menu.c —
// see gui_backend.c for how the two are kept in sync. This backend only
// owns drawing.

#endif // GUI_CITRO2D_H
