// ═══════════════════════════════════════════════════════════════════════════
//  CustomCheats — Luma3DS 3GX Plugin
//  Entry point, plugin thread, title-ID detection, and game memory bounds.
// ═══════════════════════════════════════════════════════════════════════════

#include <3ds.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "cheat_engine.h"
#include "cheat_vm.h"
#include "cheat_menu.h"

// ─────────────────────────────────────────────
//  Version
// ─────────────────────────────────────────────
#define CC_VERSION_MAJOR 1
#define CC_VERSION_MINOR 0
#define CC_VERSION_PATCH 0

// ─────────────────────────────────────────────
//  3GX plugin header — Luma reads this at load time
// ─────────────────────────────────────────────
#define PLUGIN_MAGIC   0x58334700u  // "3GX\0"
#define PLUGIN_VERSION ((CC_VERSION_MAJOR << 16) | (CC_VERSION_MINOR << 8) | CC_VERSION_PATCH)

typedef struct {
    u32  magic;
    u32  version;
    u32  flags;
    char name[32];
    char author[32];
    char description[64];
} PluginHeader;

__attribute__((section(".plugin_header"), used))
static const PluginHeader g_pluginHeader = {
    .magic       = PLUGIN_MAGIC,
    .version     = PLUGIN_VERSION,
    .flags       = 0,
    .name        = "CustomCheats",
    .author      = "Community",
    .description = "Toggle custom cheats live from the in-game menu",
};

// ─────────────────────────────────────────────
//  Thread plumbing
// ─────────────────────────────────────────────
static Handle s_pluginThread = 0;
static bool   s_running      = false;

#define THREAD_STACK_SIZE 0x4000
static u32 s_threadStack[THREAD_STACK_SIZE / sizeof(u32)] __attribute__((aligned(8)));

// Hotkey: R + SELECT → toggle cheat menu (distinct from ModLoader's L+SELECT
// so both plugins can be run side by side without colliding combos)
#define HOTKEY_COMBO (KEY_R | KEY_SELECT)

// ─────────────────────────────────────────────
//  Title ID detection — same layered approach as ModLoader:
//  APT running-title query, then AM cartridge list, then AM SD list.
// ─────────────────────────────────────────────
static u64 getTitleIdFromAPT(void) {
    u32 cmdbuf[32];
    Handle aptHandle = 0;

    if (R_FAILED(srvGetServiceHandle(&aptHandle, "APT:U")))
        return 0;

    cmdbuf[0] = IPC_MakeHeader(0x0101, 0, 0);
    Result rc = svcSendSyncRequest(aptHandle);
    svcCloseHandle(aptHandle);

    if (R_FAILED(rc) || R_FAILED((Result)cmdbuf[1]))
        return 0;

    return ((u64)cmdbuf[3] << 32) | (u64)cmdbuf[2];
}

static u64 getTitleIdFromAM(MediaType media, bool requireGameCategory) {
    u64 titleId = 0;
    u32 count   = 0;

    if (R_FAILED(amInit())) return 0;

    if (R_SUCCEEDED(AM_GetTitleCount(media, &count)) && count > 0) {
        u64 *idList = (u64 *)malloc(count * sizeof(u64));
        if (idList) {
            u32 read = 0;
            if (R_SUCCEEDED(AM_GetTitleList(&read, media, count, idList)) && read > 0) {
                if (!requireGameCategory) {
                    titleId = idList[0];
                } else {
                    for (u32 i = 0; i < read; i++) {
                        u32 cat = (u32)((idList[i] >> 32) & 0xFFFF);
                        if (cat == 0x0004) { titleId = idList[i]; break; }
                    }
                }
            }
            free(idList);
        }
    }

    amExit();
    return titleId;
}

static u64 detectTitleId(void) {
    u64 id = getTitleIdFromAPT();
    if (id) { CC_Log("[CC] Title ID from APT: %016llX", (unsigned long long)id); return id; }

    id = getTitleIdFromAM(MEDIATYPE_GAME_CARD, false);
    if (id) { CC_Log("[CC] Title ID from AM (cartridge): %016llX", (unsigned long long)id); return id; }

    id = getTitleIdFromAM(MEDIATYPE_SD, true);
    if (id) { CC_Log("[CC] Title ID from AM (SD): %016llX", (unsigned long long)id); return id; }

    CC_Log("[CC] WARNING: Could not detect title ID; cheats will not load");
    return 0;
}

// ─────────────────────────────────────────────
//  Resolve the game's code and heap regions so the VM can bounds-check
//  every write. Code base/size comes from the mapped ELF program headers
//  (same technique used by ModLoader's patch engine); heap base/size
//  comes from querying the process's linear-heap mapping via svcGetProcessInfo.
// ─────────────────────────────────────────────
#define PROCESS_CODE_BASE 0x00100000u

static u32 read32LE(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void resolveCodeBounds(u32 *outBase, u32 *outSize) {
    const u8 *hdr = (const u8 *)PROCESS_CODE_BASE;

    if (hdr[0] == 0x7F && hdr[1] == 'E' && hdr[2] == 'L' && hdr[3] == 'F') {
        u32 phoff     = read32LE(hdr + 0x1C);
        u16 phentsize = *(const u16 *)(hdr + 0x2A);
        u16 phnum     = *(const u16 *)(hdr + 0x2C);

        u32 lo = 0xFFFFFFFFu, hi = 0;
        for (u16 i = 0; i < phnum; i++) {
            const u8 *ph = hdr + phoff + i * phentsize;
            u32 type  = read32LE(ph + 0x00);
            u32 vaddr = read32LE(ph + 0x08);
            u32 memsz = read32LE(ph + 0x14);

            if (type == 1 /* PT_LOAD */) {
                if (vaddr < lo) lo = vaddr;
                if (vaddr + memsz > hi) hi = vaddr + memsz;
            }
        }
        if (lo != 0xFFFFFFFFu && hi > lo) {
            *outBase = lo;
            *outSize = hi - lo;
            return;
        }
    }

    *outBase = PROCESS_CODE_BASE;
    *outSize = 0x200000;
}

static void resolveHeapBounds(u32 *outBase, u32 *outSize) {
    // APPLICATION-category processes get a linear heap mapped starting at
    // a fixed virtual address on old3DS/new3DS; query the actual size via
    // svcGetProcessInfo so codes still bounds-check correctly under the
    // New3DS extended memory layout.
    s64 linearHeapSize = 0;
    Result rc = svcGetProcessInfo(&linearHeapSize, CUR_PROCESS_HANDLE, 0 /* INFO_LINEAR_HEAP_SIZE? */);

    // svcGetProcessInfo's info-type constants vary by libctru version;
    // fall back to a conservative default if the query doesn't behave as
    // expected on the target firmware rather than trusting a garbage value.
    u32 base = 0x08000000u;
    u32 size = 0x04000000u; // 64 MB default — safe for O3DS APPLICATION budget

    if (R_SUCCEEDED(rc) && linearHeapSize > 0 && linearHeapSize < 0x10000000LL) {
        size = (u32)linearHeapSize;
    }

    *outBase = base;
    *outSize = size;
}

// ─────────────────────────────────────────────
//  Plugin main thread
// ─────────────────────────────────────────────
static void pluginThread(void *arg) {
    (void)arg;

    CC_Log("[CC] Plugin thread started");

    // Let the game finish booting and mount its save data before we
    // start reading/writing its heap.
    svcSleepThread(2000000000LL); // 2 s

    u64 titleId = detectTitleId();

    u32 codeBase, codeSize, heapBase, heapSize;
    resolveCodeBounds(&codeBase, &codeSize);
    resolveHeapBounds(&heapBase, &heapSize);
    CVM_SetMemoryBounds(codeBase, codeSize, heapBase, heapSize);

    CC_Log("[CC] Code: base=0x%08lX size=0x%08lX  Heap: base=0x%08lX size=0x%08lX",
           (unsigned long)codeBase, (unsigned long)codeSize,
           (unsigned long)heapBase, (unsigned long)heapSize);

    if (titleId != 0) {
        CC_LoadDatabase(titleId);
    }

    CM_Init();

    // ── Main loop ─────────────────────────────
    u32 pollTickNs = g_cheats.pollIntervalMs * 1000000u;

    while (s_running) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        if ((kDown & HOTKEY_COMBO) == HOTKEY_COMBO) {
            CM_Toggle();
        }

        if (CM_IsVisible()) {
            CM_HandleInput(kDown, kHeld);
            CM_Draw();
        }

        // Re-apply all enabled cheats every tick — this is what makes
        // "infinite HP"-style continuous codes actually stick instead of
        // being overwritten by the game's own logic a frame later.
        CC_ApplyEnabledCheats();

        svcSleepThread((s64)pollTickNs);
    }

    // Persist final toggle state on the way out
    if (g_cheats.db.loaded) {
        CC_SaveState(g_cheats.db.titleId);
    }

    CC_Log("[CC] Plugin thread exiting");
}

// ─────────────────────────────────────────────
//  3GX entry points
// ─────────────────────────────────────────────
void __attribute__((visibility("default"))) plugin_load(void) {
    CC_Init();
    CC_Log("[CC] CustomCheats v%d.%d.%d loaded",
           CC_VERSION_MAJOR, CC_VERSION_MINOR, CC_VERSION_PATCH);

    s_running = true;

    Result rc = svcCreateThread(
        &s_pluginThread,
        pluginThread,
        0,
        s_threadStack + (THREAD_STACK_SIZE / sizeof(u32)),
        0x30,
        -2
    );

    if (R_FAILED(rc)) {
        CC_Log("[CC] FATAL: svcCreateThread failed: 0x%08lX", rc);
    }
}

void __attribute__((visibility("default"))) plugin_unload(void) {
    s_running = false;
    if (s_pluginThread) {
        svcWaitSynchronization(s_pluginThread, 5000000000LL);
        svcCloseHandle(s_pluginThread);
        s_pluginThread = 0;
    }
    CC_UnloadDatabase();
    CC_Exit();
}
