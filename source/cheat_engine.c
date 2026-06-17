// ═══════════════════════════════════════════════════════════════════════════
//  CustomCheats — Core Engine
//  Loads the per-game cheat database, persists toggle state, and drives
//  the apply loop that the plugin thread calls every poll tick.
// ═══════════════════════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>

#include "cheat_engine.h"
#include "cheat_parser.h"
#include "cheat_vm.h"
#include "ini_parser.h"

// ─────────────────────────────────────────────
//  Global state
// ─────────────────────────────────────────────
CheatPluginState g_cheats;

static FILE  *s_logFile  = NULL;
static Handle s_logMutex = 0;

// ─────────────────────────────────────────────
//  Logging
// ─────────────────────────────────────────────
void CC_Log(const char *fmt, ...) {
    if (!g_cheats.logEnabled || !s_logFile) return;

    svcWaitSynchronization(s_logMutex, U64_MAX);

    va_list args;
    va_start(args, fmt);
    vfprintf(s_logFile, fmt, args);
    fprintf(s_logFile, "\n");
    va_end(args);

    fflush(s_logFile);
    svcReleaseMutex(s_logMutex);
}

// ─────────────────────────────────────────────
//  Init / Exit
// ─────────────────────────────────────────────
Result CC_Init(void) {
    memset(&g_cheats, 0, sizeof(g_cheats));
    g_cheats.enabled        = true;
    g_cheats.logEnabled      = true;
    g_cheats.pollIntervalMs = 33; // ~30 Hz re-apply, smooth enough for HP/ammo-style codes

    svcCreateMutex(&s_logMutex, false);

    mkdir("sdmc:/luma",                          0777);
    mkdir("sdmc:/luma/plugins",                  0777);
    mkdir("sdmc:/luma/plugins/CustomCheats",     0777);
    mkdir("sdmc:/luma/plugins/CustomCheats/state", 0777);
    mkdir("sdmc:/cheats",                        0777);

    s_logFile = fopen(CC_LOG_PATH, "a");
    if (!s_logFile) g_cheats.logEnabled = false;

    CC_Log("=== CustomCheats Init ===");
    CC_ReadConfig();

    return 0;
}

void CC_Exit(void) {
    if (s_logFile) {
        CC_Log("=== CustomCheats Exit ===");
        fclose(s_logFile);
        s_logFile = NULL;
    }
    svcCloseHandle(s_logMutex);
}

// ─────────────────────────────────────────────
//  Config
// ─────────────────────────────────────────────
Result CC_ReadConfig(void) {
    IniFile cfg;
    if (INI_Parse(CC_CONFIG_PATH, &cfg) != 0) {
        CC_Log("[CC] No config found, using defaults");
        return 0;
    }

    g_cheats.enabled        = INI_GetBool(&cfg, "CustomCheats", "enabled", true);
    g_cheats.debugMode      = INI_GetBool(&cfg, "CustomCheats", "debug",   false);
    g_cheats.logEnabled     = INI_GetBool(&cfg, "CustomCheats", "logging", true);
    g_cheats.pollIntervalMs = (u32)INI_GetInt(&cfg, "CustomCheats", "poll_interval_ms", 33);

    if (g_cheats.pollIntervalMs < 16)  g_cheats.pollIntervalMs = 16;   // don't peg the CPU
    if (g_cheats.pollIntervalMs > 500) g_cheats.pollIntervalMs = 500;  // don't feel unresponsive

    CC_Log("[CC] Config loaded (enabled=%d, poll=%lums)",
           g_cheats.enabled, (unsigned long)g_cheats.pollIntervalMs);
    return 0;
}

// ─────────────────────────────────────────────
//  Load database for a title
// ─────────────────────────────────────────────
Result CC_LoadDatabase(u64 titleId) {
    char path[CC_MAX_PATH];
    snprintf(path, sizeof(path), CC_CHEATS_FMT, (unsigned long long)titleId);

    if (CP_ParseFile(path, &g_cheats.db) != 0) {
        CC_Log("[CC] No cheat file found at %s", path);
        g_cheats.db.titleId = titleId;
        g_cheats.db.loaded  = false;
        return -1;
    }

    g_cheats.db.titleId = titleId;

    u32 errorCount = 0;
    for (u32 i = 0; i < g_cheats.db.cheatCount; i++) {
        if (g_cheats.db.cheats[i].hasError) errorCount++;
    }

    CC_Log("[CC] Loaded %lu cheats (%lu flagged unsupported) for %016llX",
           (unsigned long)g_cheats.db.cheatCount,
           (unsigned long)errorCount,
           (unsigned long long)titleId);

    // Restore any previously saved enable/disable state
    CC_LoadState(titleId);

    return 0;
}

void CC_UnloadDatabase(void) {
    memset(&g_cheats.db, 0, sizeof(g_cheats.db));
}

// ─────────────────────────────────────────────
//  Persisted toggle state
//
//  Cheats are matched by name (not index) when restoring, since the
//  underlying file could be edited between sessions and indices would
//  silently apply the wrong saved state to the wrong cheat.
// ─────────────────────────────────────────────
Result CC_LoadState(u64 titleId) {
    char path[CC_MAX_PATH];
    snprintf(path, sizeof(path), CC_STATE_FMT, (unsigned long long)titleId);

    IniFile state;
    if (INI_Parse(path, &state) != 0) {
        CC_Log("[CC] No saved state for %016llX (first run)", (unsigned long long)titleId);
        return 0;
    }

    u32 restored = 0;
    for (u32 i = 0; i < g_cheats.db.cheatCount; i++) {
        Cheat *c = &g_cheats.db.cheats[i];
        const char *v = INI_Get(&state, "Cheats", c->name);
        if (v) {
            c->enabled = (strcasecmp(v, "true") == 0 || strcmp(v, "1") == 0);
            restored++;
        }
    }

    CC_Log("[CC] Restored toggle state for %lu cheat(s)", (unsigned long)restored);
    return 0;
}

Result CC_SaveState(u64 titleId) {
    char path[CC_MAX_PATH];
    snprintf(path, sizeof(path), CC_STATE_FMT, (unsigned long long)titleId);

    if (INI_BeginWrite(path) != 0) {
        CC_Log("[CC] Failed to open state file for writing: %s", path);
        return -1;
    }

    INI_WriteSection("Cheats");
    for (u32 i = 0; i < g_cheats.db.cheatCount; i++) {
        Cheat *c = &g_cheats.db.cheats[i];
        INI_WriteBool(c->name, c->enabled);
    }
    INI_EndWrite();

    CC_Log("[CC] Saved toggle state (%lu cheats) to %s",
           (unsigned long)g_cheats.db.cheatCount, path);
    return 0;
}

// ─────────────────────────────────────────────
//  Toggle a cheat by index (called from the overlay on button press)
// ─────────────────────────────────────────────
void CC_ToggleCheat(u32 index) {
    if (index >= g_cheats.db.cheatCount) return;

    Cheat *c = &g_cheats.db.cheats[index];
    if (c->hasError) {
        CC_Log("[CC] Refusing to enable \"%s\": unsupported opcode in this cheat", c->name);
        return;
    }

    c->enabled = !c->enabled;
    CC_Log("[CC] Cheat \"%s\" %s", c->name, c->enabled ? "ENABLED" : "disabled");

    // Persist immediately so a crash/reset doesn't lose the change
    CC_SaveState(g_cheats.db.titleId);
}

// ─────────────────────────────────────────────
//  Apply pass — called once per poll tick from the plugin thread
// ─────────────────────────────────────────────
void CC_ApplyEnabledCheats(void) {
    if (!g_cheats.enabled || !g_cheats.db.loaded) return;

    for (u32 i = 0; i < g_cheats.db.cheatCount; i++) {
        Cheat *c = &g_cheats.db.cheats[i];
        if (!c->enabled || c->hasError) continue;

        VMResult res = CVM_Execute(c);
        if (res != VM_OK && g_cheats.debugMode) {
            CC_Log("[CC] \"%s\" execute result=%d (auto-disabling)", c->name, res);
        }

        // A cheat that hits an out-of-bounds write is almost always
        // targeting memory that doesn't exist in the game's current
        // state (e.g. applied before the save data loaded). Disable it
        // automatically rather than spamming bounds errors every tick.
        if (res == VM_ERR_BOUNDS) {
            c->enabled = false;
            CC_Log("[CC] \"%s\" auto-disabled: target address out of bounds", c->name);
        }
    }
}
