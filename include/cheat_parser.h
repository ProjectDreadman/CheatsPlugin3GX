#pragma once
#ifndef CHEAT_PARSER_H
#define CHEAT_PARSER_H

#include "cheat_engine.h"

// ═══════════════════════════════════════════════════════════════════════════
//  Cheat file parser
//
//  Supports the common flat-text cheat database format used across the
//  3DS cheat-code scene (Citra/RetroArch/Gateway-compatible):
//
//    [Cheat Display Name]
//    94000130 FFFB0000
//    1216A800 000003E7
//    D2000000 00000000
//    *Optional comment line starting with '*'
//
//    [Category] Another Cheat
//    ...
//
//  Rules:
//   - A line starting with '[' and containing ']' opens a new cheat;
//     everything between the brackets is the name UNLESS it matches
//     "[CategoryName]" exactly on its own line followed by indented or
//     prefixed entries, in which case it's treated as a category header.
//   - Lines of the form "XXXXXXXX XXXXXXXX" (8 hex digits, space, 8 hex
//     digits) are code lines belonging to the current cheat.
//   - Lines starting with '*' or ';' are comments/notes, attached to the
//     current cheat's note field.
//   - Blank lines are ignored.
// ═══════════════════════════════════════════════════════════════════════════

// Parse a cheat file from `path` into `db`. Returns 0 on success.
int CP_ParseFile(const char *path, CheatDatabase *db);

// Parse a single line into a CheatCodeLine. Returns true if it matched
// the "XXXXXXXX XXXXXXXX" hex pattern.
bool CP_ParseCodeLine(const char *line, CheatCodeLine *out);

#endif // CHEAT_PARSER_H
