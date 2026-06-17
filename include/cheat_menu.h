#pragma once
#ifndef CHEAT_MENU_H
#define CHEAT_MENU_H

#include <3ds.h>
#include <stdint.h>
#include <stdbool.h>

// ═══════════════════════════════════════════════════════════════════════════
//  In-game cheat menu — bottom-screen overlay.
//  Lets the player browse the loaded cheat database and toggle cheats
//  on/off live, without leaving the game.
// ═══════════════════════════════════════════════════════════════════════════

#define CM_SCREEN_W   320
#define CM_SCREEN_H   240

#define CM_COL_BG       0x14141F
#define CM_COL_HEADER   0x1F1F33
#define CM_COL_ACCENT   0x3D2B6E
#define CM_COL_TEXT     0xE0E0E0
#define CM_COL_GREEN    0x4CD964
#define CM_COL_RED      0xFF453A
#define CM_COL_YELLOW   0xFFD60A
#define CM_COL_WHITE    0xFFFFFF
#define CM_COL_GREY     0x808080

typedef enum {
    CM_PAGE_LIST   = 0,    // browsable, toggleable cheat list
    CM_PAGE_DETAIL = 1,    // selected cheat's raw code + note
    CM_PAGE_ABOUT  = 2,
} CheatMenuPage;

// ─────────────────────────────────────────────
//  API
// ─────────────────────────────────────────────
void CM_Init(void);
void CM_Toggle(void);
bool CM_IsVisible(void);
void CM_Draw(void);
void CM_HandleInput(u32 kDown, u32 kHeld);

#endif // CHEAT_MENU_H
