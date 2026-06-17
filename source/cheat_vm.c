// ═══════════════════════════════════════════════════════════════════════════
//  CustomCheats — Cheat VM
//  Interprets Gateway/AR-style code lines against the running game's
//  mapped memory, with bounds checking on every write.
// ═══════════════════════════════════════════════════════════════════════════

#include <3ds.h>
#include <string.h>

#include "cheat_vm.h"
#include "cheat_engine.h"

// ─────────────────────────────────────────────
//  Memory bounds (set once at startup from the process's mapped segments)
// ─────────────────────────────────────────────
static u32 s_codeBase  = 0x00100000u;
static u32 s_codeSize  = 0x00200000u;
static u32 s_heapBase  = 0x08000000u;   // typical APPLICATION heap region
static u32 s_heapSize  = 0x04000000u;   // generous default; overwritten at init

void CVM_SetMemoryBounds(u32 base, u32 size, u32 heapBase, u32 heapSize) {
    s_codeBase = base;
    s_codeSize = size;
    s_heapBase = heapBase;
    s_heapSize = heapSize;
}

// ─────────────────────────────────────────────
//  Bounds check — codes address memory relative to the game's heap base
//  by Gateway/AR convention (i.e. the address in the code IS the heap
//  offset, not an absolute address). We translate then bounds-check
//  against both known regions.
// ─────────────────────────────────────────────
static bool isWritable(u32 absoluteAddr, u32 width) {
    if (absoluteAddr < 0x1000) return false; // never allow near-null writes

    if (absoluteAddr >= s_codeBase && absoluteAddr + width <= s_codeBase + s_codeSize)
        return true;
    if (absoluteAddr >= s_heapBase && absoluteAddr + width <= s_heapBase + s_heapSize)
        return true;

    return false;
}

// Gateway/AR codes encode addresses as heap-relative offsets in the low
// 28 bits of the first word; translate to an absolute address.
static inline u32 toAbsolute(u32 codeAddr) {
    return s_heapBase + (codeAddr & 0x0FFFFFFFu);
}

// ─────────────────────────────────────────────
//  Opcode classification (top nibble of the first word)
// ─────────────────────────────────────────────
typedef enum {
    OP_WRITE32        = 0x0,
    OP_WRITE16        = 0x1,
    OP_WRITE8         = 0x2,
    OP_IF_EQ32        = 0x3,
    OP_IF_NEQ32       = 0x4,
    OP_IF_EQ16        = 0x5,
    OP_IF_NEQ16       = 0x6,
    OP_BUTTON_GATE    = 0x9,  // special-cased on full first word, see below
    OP_ADD32          = 0x8,
    OP_LOAD_OFFSET    = 0xA,
    OP_END_IF         = 0xB,
    OP_LOOP_START     = 0xC,
    OP_LOOP_END       = 0xD0,
    OP_TERMINATOR      = 0xD2,
    OP_SET_BASE        = 0xD3,
    OP_DELAY           = 0xD4,
} CheatOpcode;

// ─────────────────────────────────────────────
//  Validate — walk the cheat's lines once, classifying each opcode and
//  flagging anything unsupported. Does NOT touch memory.
// ─────────────────────────────────────────────
bool CVM_Validate(Cheat *cheat) {
    cheat->hasError  = false;
    cheat->isOneShot = true;

    if (cheat->lineCount == 0) {
        cheat->hasError = true;
        return false;
    }

    int condDepth = 0;
    int loopDepth = 0;

    for (u32 i = 0; i < cheat->lineCount; i++) {
        u32 word0 = cheat->lines[i].addr;

        // Full-word special cases first (button gate, terminator family)
        if (word0 == 0x94000130u) { cheat->isOneShot = false; continue; }
        if ((word0 & 0xFF000000u) == 0xD0000000u) {
            // D0/D2/D3/D4 family — distinguish by second-byte nibble group
            u32 sub = word0 & 0x0F000000u;
            if (sub == 0x00000000u) { loopDepth--; continue; }                 // D0 loop end
            if (sub == 0x02000000u) { continue; }                              // D2 terminator
            if (sub == 0x03000000u) { continue; }                              // D3 set-base
            if (sub == 0x04000000u) { cheat->isOneShot = false; continue; }    // D4 delay
            cheat->hasError = true;
            return false;
        }

        u32 op = (word0 >> 28) & 0xF;
        switch (op) {
            case OP_WRITE32:
            case OP_WRITE16:
            case OP_WRITE8:
            case OP_ADD32:
                cheat->isOneShot = false; // repeated writes are the common case
                break;

            case OP_IF_EQ32:
            case OP_IF_NEQ32:
            case OP_IF_EQ16:
            case OP_IF_NEQ16:
                condDepth++;
                cheat->isOneShot = false;
                break;

            case OP_LOAD_OFFSET:
                break;

            case OP_END_IF:
                condDepth--;
                break;

            case OP_LOOP_START:
                loopDepth++;
                cheat->isOneShot = false;
                break;

            default:
                cheat->hasError = true;
                return false;
        }
    }

    if (condDepth != 0 || loopDepth != 0) {
        // Unbalanced conditional/loop blocks — refuse to run rather than
        // risk an infinite loop or corrupted control flow at execute time.
        cheat->hasError = true;
        return false;
    }

    return true;
}

// ─────────────────────────────────────────────
//  Button gate check
//  94000130 YYYYZZZZ: YYYY = buttons that must be HELD, ZZZZ = buttons
//  that must NOT be held, for the remainder of the block to execute.
// ─────────────────────────────────────────────
bool CVM_CheckButtonGate(u32 requireHeld, u32 requireNotHeld) {
    hidScanInput();
    u32 held = hidKeysHeld();
    if ((held & requireHeld) != requireHeld) return false;
    if ((held & requireNotHeld) != 0) return false;
    return true;
}

// ─────────────────────────────────────────────
//  Execute
//
//  Single linear pass with a small explicit "skip" counter to implement
//  conditional blocks (3/4/5/6-type) and a loop stack for C/D0 blocks.
//  This intentionally does NOT support nested loops containing nested
//  conditionals beyond a shallow depth — that covers the overwhelming
//  majority of real cheat files without the complexity (and risk) of a
//  full general-purpose bytecode VM running against live game memory.
// ─────────────────────────────────────────────
#define MAX_LOOP_DEPTH 4

typedef struct {
    u32 startIndex;
    u32 remaining;
} LoopFrame;

VMResult CVM_Execute(const Cheat *cheat) {
    if (cheat->hasError) return VM_ERR_MALFORMED;
    if (cheat->lineCount == 0) return VM_ERR_MALFORMED;

    u32 offsetReg   = 0;     // set by OP_LOAD_OFFSET (0xA)
    u32 baseOverride= 0;     // set by D3 — 0 means "use default heap base"
    int skipDepth   = 0;     // >0 means we're inside a failed conditional

    LoopFrame loopStack[MAX_LOOP_DEPTH];
    int loopTop = -1;

    for (u32 i = 0; i < cheat->lineCount; i++) {
        u32 word0 = cheat->lines[i].addr;
        u32 word1 = cheat->lines[i].val;

        // ── Button gate ───────────────────────
        if (word0 == 0x94000130u) {
            u32 requireHeld    = (word1 >> 16) & 0xFFFFu;
            u32 requireNotHeld = (word1)       & 0xFFFFu;
            if (!CVM_CheckButtonGate(requireHeld, requireNotHeld)) {
                // Gate failed — abort this entire cheat for this tick
                return VM_OK;
            }
            continue;
        }

        // ── D-family control codes ────────────
        if ((word0 & 0xFF000000u) == 0xD0000000u) {
            u32 sub = word0 & 0x0F000000u;

            if (sub == 0x00000000u) { // D0: loop end
                if (skipDepth == 0 && loopTop >= 0) {
                    LoopFrame *lf = &loopStack[loopTop];
                    if (lf->remaining > 1) {
                        lf->remaining--;
                        i = lf->startIndex; // jump back (loop continues)
                    } else {
                        loopTop--;
                    }
                }
                continue;
            }
            if (sub == 0x02000000u) continue;            // D2: terminator, no-op
            if (sub == 0x03000000u) {                     // D3: set base address
                if (skipDepth == 0) baseOverride = word1;
                continue;
            }
            if (sub == 0x04000000u) {                      // D4: delay
                if (skipDepth == 0) {
                    u32 ms = word1;
                    if (ms > 1000) ms = 1000; // clamp — never stall the poll thread long
                    svcSleepThread((s64)ms * 1000000LL);
                }
                continue;
            }
            continue; // unrecognised D-family, already filtered by Validate
        }

        u32 op = (word0 >> 28) & 0xF;

        // While inside a failed conditional, skip everything until the
        // matching OP_END_IF (0xB) closes the block.
        if (skipDepth > 0) {
            if (op == OP_END_IF) skipDepth--;
            continue;
        }

        u32 effBase = baseOverride ? baseOverride : s_heapBase;
        u32 addr    = effBase + ((word0 & 0x0FFFFFFFu) + offsetReg);

        switch (op) {
            case OP_WRITE32:
                if (!isWritable(addr, 4)) return VM_ERR_BOUNDS;
                *(volatile u32 *)addr = word1;
                break;

            case OP_WRITE16:
                if (!isWritable(addr, 2)) return VM_ERR_BOUNDS;
                *(volatile u16 *)addr = (u16)(word1 & 0xFFFFu);
                break;

            case OP_WRITE8:
                if (!isWritable(addr, 1)) return VM_ERR_BOUNDS;
                *(volatile u8 *)addr = (u8)(word1 & 0xFFu);
                break;

            case OP_ADD32:
                if (!isWritable(addr, 4)) return VM_ERR_BOUNDS;
                *(volatile u32 *)addr += word1;
                break;

            case OP_IF_EQ32:
                if (!isWritable(addr, 4)) return VM_ERR_BOUNDS;
                if (*(volatile u32 *)addr != word1) skipDepth = 1;
                break;

            case OP_IF_NEQ32:
                if (!isWritable(addr, 4)) return VM_ERR_BOUNDS;
                if (*(volatile u32 *)addr == word1) skipDepth = 1;
                break;

            case OP_IF_EQ16:
                if (!isWritable(addr, 2)) return VM_ERR_BOUNDS;
                if (*(volatile u16 *)addr != (u16)word1) skipDepth = 1;
                break;

            case OP_IF_NEQ16:
                if (!isWritable(addr, 2)) return VM_ERR_BOUNDS;
                if (*(volatile u16 *)addr == (u16)word1) skipDepth = 1;
                break;

            case OP_LOAD_OFFSET:
                offsetReg = word1;
                break;

            case OP_END_IF:
                // No-op when not skipping (already balanced)
                break;

            case OP_LOOP_START:
                if (loopTop + 1 < MAX_LOOP_DEPTH) {
                    loopTop++;
                    loopStack[loopTop].startIndex = i;
                    loopStack[loopTop].remaining   = word1 ? word1 : 1;
                }
                break;

            default:
                return VM_ERR_UNSUPPORTED;
        }
    }

    return VM_OK;
}
