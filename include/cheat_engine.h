#pragma once
#ifndef CHEAT_ENGINE_H
#define CHEAT_ENGINE_H

#include <3ds.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ═══════════════════════════════════════════════════════════════════════════
//  CustomCheats — Luma3DS 3GX Plugin
//  Core cheat database types and engine API.
//
//  Cheat code format supported: Gateway/AR-style hex code lines, e.g.
//
//    [Infinite HP]
//    94000130 FFFB0000
//    1216A800 000003E7
//    D2000000 00000000
//
//  Each cheat is a named group of one or more 8-hex/8-hex code lines.
//  Code types implement the common Gateway opcode set (see cheat_vm.h).
// ═══════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────
//  Limits
// ─────────────────────────────────────────────
#define CC_MAX_CHEATS        256     // cheats per game
#define CC_MAX_CODELINES     64      // code lines per cheat
#define CC_MAX_NAME          64
#define CC_MAX_NOTE          128
#define CC_MAX_PATH          256
#define CC_MAX_CATEGORY      32
#define CC_MAX_CATEGORIES    32      // distinct categories per game

// ─────────────────────────────────────────────
//  Paths
// ─────────────────────────────────────────────
#define CC_CHEATS_ROOT       "sdmc:/cheats/"
#define CC_CHEATS_FMT        "sdmc:/cheats/%016llX.txt"   // RetroArch/Citra-style flat file
#define CC_CONFIG_PATH       "sdmc:/luma/plugins/CustomCheats/config.ini"
#define CC_STATE_FMT         "sdmc:/luma/plugins/CustomCheats/state/%016llX.ini"
#define CC_LOG_PATH          "sdmc:/luma/plugins/CustomCheats/cheats.log"

// ─────────────────────────────────────────────
//  A single code line: two 32-bit hex words
//  (the universal Gateway/AR/CodeBreaker line shape)
// ─────────────────────────────────────────────
typedef struct {
    u32 addr;   // first word — opcode + address/operand
    u32 val;    // second word — value/operand
} CheatCodeLine;

// ─────────────────────────────────────────────
//  A single cheat (a named, togglable group of code lines)
// ─────────────────────────────────────────────
typedef struct {
    char           name[CC_MAX_NAME];
    char           category[CC_MAX_CATEGORY];   // optional "[Category] Name" parsing
    char           note[CC_MAX_NOTE];           // optional comment line
    CheatCodeLine  lines[CC_MAX_CODELINES];
    u32            lineCount;
    bool           enabled;          // current toggle state
    bool           isOneShot;        // codes with only D-type one-time writes
    bool           hasError;         // failed to parse / unsupported opcode
    s32            categoryIndex;    // index into CheatDatabase.categories, -1 = "Uncategorized"
} Cheat;

// ─────────────────────────────────────────────
//  A distinct category, with a running count of how many of its
//  cheats are currently enabled (kept in sync by the engine so the
//  menu can show "Combat (2/5)" without re-scanning every cheat).
// ─────────────────────────────────────────────
typedef struct {
    char name[CC_MAX_CATEGORY];
    u32  cheatCount;
    u32  enabledCount;
} CheatCategory;

// ─────────────────────────────────────────────
//  Per-game cheat database
// ─────────────────────────────────────────────
typedef struct {
    u64           titleId;
    char          gameName[CC_MAX_NAME];   // optional, from file header comment
    Cheat         cheats[CC_MAX_CHEATS];
    u32           cheatCount;
    CheatCategory categories[CC_MAX_CATEGORIES];
    u32           categoryCount;
    bool          loaded;
    char          sourcePath[CC_MAX_PATH]; // remembered for CC_ReloadDatabase()
} CheatDatabase;

// ─────────────────────────────────────────────
//  Global state
// ─────────────────────────────────────────────
typedef struct {
    CheatDatabase db;
    bool          enabled;       // master switch
    bool          logEnabled;
    bool          debugMode;
    u32           pollIntervalMs; // how often the VM re-applies active cheats
} CheatPluginState;

extern CheatPluginState g_cheats;

// ─────────────────────────────────────────────
//  API
// ─────────────────────────────────────────────
Result CC_Init(void);
void   CC_Exit(void);
Result CC_ReadConfig(void);

Result CC_LoadDatabase(u64 titleId);
void   CC_UnloadDatabase(void);
Result CC_ReloadDatabase(void);       // re-parse sourcePath without restarting the game

void   CC_RebuildCategoryIndex(void); // recompute g_cheats.db.categories[] from current cheats[]

Result CC_LoadState(u64 titleId);     // restore enabled/disabled per cheat
Result CC_SaveState(u64 titleId);     // persist toggle state across sessions

void   CC_ToggleCheat(u32 index);
void   CC_ApplyEnabledCheats(void);    // called every poll tick

void   CC_Log(const char *fmt, ...);

#endif // CHEAT_ENGINE_H
