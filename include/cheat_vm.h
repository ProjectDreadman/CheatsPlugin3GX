#pragma once
#ifndef CHEAT_VM_H
#define CHEAT_VM_H

#include "cheat_engine.h"

// ═══════════════════════════════════════════════════════════════════════════
//  Cheat VM — interprets Gateway/AR-style code lines against the running
//  game's memory space.
//
//  Supported opcode set (first nibble of the address word):
//
//    0XXXXXXX YYYYYYYY   32-bit write:   *(u32*)(base+XXXXXXX) = YYYYYYYY
//    1XXXXXXX 0000YYYY   16-bit write:   *(u16*)(base+XXXXXXX) = YYYY
//    2XXXXXXX 000000YY    8-bit write:   *(u8 *)(base+XXXXXXX) = YY
//    3XXXXXXX YYYYYYYY   32-bit if-equal:  if *(u32*)(base+X)==Y then continue, else skip block
//    4XXXXXXX YYYYYYYY   32-bit if-not-equal (skip-if-equal variant)
//    5XXXXXXX YYYYYYYY   16-bit if-equal
//    6XXXXXXX YYYYYYYY   16-bit if-not-equal
//    8XXXXXXX YYYYYYYY   32-bit add to memory:  *(u32*)(base+X) += Y
//    9XXXXXXX 000000NN   repeat NN times, advancing address by 4 each time,
//                        applying the next code line's write
//    A0000000 NNNNNNNN   load offset register = NNNNNNNN
//    B0000000 00000000   end-if / pop conditional block
//    C0000000 NNNNNNNN   loop start, repeat following block NNNNNNNN times
//    D0000000 00000000   loop end
//    D2000000 00000000   terminator / end of cheat (no-op marker)
//    D3000000 XXXXXXXX   set base address register = XXXXXXXX (module base)
//    D4000000 NNNNNNNN   delay NNNNNNNN ms before next line (clamped)
//    94000130 YYYYZZZZ   "if button held" gate (Gateway pad-gate convention):
//                        YYYY = required-held mask, ZZZZ = required-not-held mask
//
//  This is a pragmatic subset covering the opcodes that appear in the vast
//  majority of real-world community cheat databases. Anything unrecognised
//  is flagged (Cheat.hasError = true) and skipped rather than guessed at,
//  since guessing wrong can corrupt game state or crash the title.
// ═══════════════════════════════════════════════════════════════════════════

#include <3ds.h>
#include <stdbool.h>

// ─────────────────────────────────────────────
//  VM result
// ─────────────────────────────────────────────
typedef enum {
    VM_OK             = 0,
    VM_ERR_BOUNDS     = -1,   // target address outside the game's mapped region
    VM_ERR_UNSUPPORTED= -2,   // opcode not implemented
    VM_ERR_MALFORMED  = -3,   // line count/structure invalid
} VMResult;

// ─────────────────────────────────────────────
//  Validate a cheat's code lines without executing them.
//  Sets cheat->hasError on any unsupported opcode.
//  Returns true if the cheat is safe to enable.
// ─────────────────────────────────────────────
bool CVM_Validate(Cheat *cheat);

// ─────────────────────────────────────────────
//  Execute one full pass of a cheat's code lines against the game's
//  memory.  Called once per poll tick for every enabled cheat — most
//  Gateway-style codes are designed to be re-applied continuously
//  (e.g. infinite HP), which is why this isn't a one-shot patch.
// ─────────────────────────────────────────────
VMResult CVM_Execute(const Cheat *cheat);

// ─────────────────────────────────────────────
//  Memory region bounds used for the bounds check.
//  Populated once at startup from the process's mapped segments.
// ─────────────────────────────────────────────
void CVM_SetMemoryBounds(u32 base, u32 size, u32 heapBase, u32 heapSize);

// ─────────────────────────────────────────────
//  Button-gate check (opcode 0x9 "94000130" convention):
//  returns true if the currently-held pad state satisfies the gate.
// ─────────────────────────────────────────────
bool CVM_CheckButtonGate(u32 requireHeld, u32 requireNotHeld);

#endif // CHEAT_VM_H
