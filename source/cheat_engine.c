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
        strncpy(g_cheats.db.sourcePath, path, CC_MAX_PATH - 1);
        return -1;
    }

    g_cheats.db.titleId = titleId;
    strncpy(g_cheats.db.sourcePath, path, CC_MAX_PATH - 1);

    u32 errorCount = 0;
    for (u32 i = 0; i < g_cheats.db.cheatCount; i++) {
        if (g_cheats.db.cheats[i].hasError) errorCount++;
    }

    CC_Log("[CC] Loaded %lu cheats (%lu flagged unsupported) for %016llX",
           (unsigned long)g_cheats.db.cheatCount,
           (unsigned long)errorCount,
           (unsigned long long)titleId);

    CC_RebuildCategoryIndex();

    // Restore any previously saved enable/disable state
    CC_LoadState(titleId);
    CC_RebuildCategoryIndex(); // enabled counts changed after restoring state

    return 0;
}

void CC_UnloadDatabase(void) {
    memset(&g_cheats.db, 0, sizeof(g_cheats.db));
}

// ─────────────────────────────────────────────
//  Reload — re-parse the same file on disk without restarting the game.
//  This is what makes cheat editing "dynamic": a user can edit
//  sdmc:/cheats/<titleid>.txt on a PC, swap the SD card back in (or use
//  a USB SD adapter / FTP tool that doesn't require ejecting), and pull
//  the changes in live via the overlay's "Reload" action.
//
//  Currently-enabled cheats are preserved across reload by name where
//  possible: we snapshot which names were enabled, re-parse, then
//  re-apply that enabled set to whichever entries still exist under the
//  same name. Cheats removed from the file are simply gone; cheats
//  added are loaded disabled by default (consistent with first load).
// ─────────────────────────────────────────────
Result CC_ReloadDatabase(void) {
    if (g_cheats.db.sourcePath[0] == '\0') {
        CC_Log("[CC] Reload requested but no source path is known yet");
        return -1;
    }

    // Snapshot currently-enabled names
    char enabledNames[CC_MAX_CHEATS][CC_MAX_NAME];
    u32  enabledCount = 0;
    for (u32 i = 0; i < g_cheats.db.cheatCount && enabledCount < CC_MAX_CHEATS; i++) {
        if (g_cheats.db.cheats[i].enabled) {
            strncpy(enabledNames[enabledCount], g_cheats.db.cheats[i].name, CC_MAX_NAME - 1);
            enabledNames[enabledCount][CC_MAX_NAME - 1] = '\0';
            enabledCount++;
        }
    }

    u64  titleId = g_cheats.db.titleId;
    char savedPath[CC_MAX_PATH];
    strncpy(savedPath, g_cheats.db.sourcePath, CC_MAX_PATH - 1);
    savedPath[CC_MAX_PATH - 1] = '\0';

    CheatDatabase fresh;
    if (CP_ParseFile(savedPath, &fresh) != 0) {
        CC_Log("[CC] Reload failed: could not re-read %s", savedPath);
        return -1;
    }

    fresh.titleId = titleId;
    strncpy(fresh.sourcePath, savedPath, CC_MAX_PATH - 1);

    // Re-apply the enabled snapshot by name
    u32 restored = 0;
    for (u32 i = 0; i < fresh.cheatCount; i++) {
        for (u32 j = 0; j < enabledCount; j++) {
            if (strcmp(fresh.cheats[i].name, enabledNames[j]) == 0) {
                if (!fresh.cheats[i].hasError) {
                    fresh.cheats[i].enabled = true;
                    restored++;
                }
                break;
            }
        }
    }

    g_cheats.db = fresh;
    CC_RebuildCategoryIndex();

    CC_Log("[CC] Reloaded %lu cheats from %s (%lu re-enabled from previous session)",
           (unsigned long)g_cheats.db.cheatCount, savedPath, (unsigned long)restored);

    CC_SaveState(titleId);
    return 0;
}

// ─────────────────────────────────────────────
//  Category index
//
//  Builds the deduplicated category list from the current cheats[]
//  array and assigns each cheat's categoryIndex. Cheats with an empty
//  category string are left at categoryIndex = -1 and shown under the
//  menu's built-in "Uncategorized" bucket rather than a real entry,
//  so a cheat file with no categories at all doesn't show a stray
//  empty-named category.
// ─────────────────────────────────────────────
void CC_RebuildCategoryIndex(void) {
    CheatDatabase *db = &g_cheats.db;
    db->categoryCount = 0;
    memset(db->categories, 0, sizeof(db->categories));

    for (u32 i = 0; i < db->cheatCount; i++) {
        Cheat *c = &db->cheats[i];

        if (c->category[0] == '\0') {
            c->categoryIndex = -1;
            continue;
        }

        // Find existing category (case-insensitive match)
        s32 foundIdx = -1;
        for (u32 k = 0; k < db->categoryCount; k++) {
            if (strcasecmp(db->categories[k].name, c->category) == 0) {
                foundIdx = (s32)k;
                break;
            }
        }

        if (foundIdx < 0) {
            if (db->categoryCount >= CC_MAX_CATEGORIES) {
                // Category table full — fall back to uncategorized rather
                // than overflow; still searchable via the full list.
                c->categoryIndex = -1;
                continue;
            }
            foundIdx = (s32)db->categoryCount;
            strncpy(db->categories[foundIdx].name, c->category, CC_MAX_CATEGORY - 1);
            db->categoryCount++;
        }

        c->categoryIndex = foundIdx;
        db->categories[foundIdx].cheatCount++;
        if (c->enabled) db->categories[foundIdx].enabledCount++;
    }

    CC_Log("[CC] Category index rebuilt: %lu categories", (unsigned long)db->categoryCount);
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

    // Keep the cached per-category enabled count in sync rather than
    // re-scanning every cheat on every toggle.
    if (c->categoryIndex >= 0 && (u32)c->categoryIndex < g_cheats.db.categoryCount) {
        CheatCategory *cat = &g_cheats.db.categories[c->categoryIndex];
        if (c->enabled) cat->enabledCount++;
        else if (cat->enabledCount > 0) cat->enabledCount--;
    }

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
            if (c->categoryIndex >= 0 && (u32)c->categoryIndex < g_cheats.db.categoryCount) {
                CheatCategory *cat = &g_cheats.db.categories[c->categoryIndex];
                if (cat->enabledCount > 0) cat->enabledCount--;
            }
            CC_Log("[CC] \"%s\" auto-disabled: target address out of bounds", c->name);
        }
    }
}
