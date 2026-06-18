// ═══════════════════════════════════════════════════════════════════════════
//  CustomCheats — Cheat Database Parser
//
//  Parses the flat-text cheat file format into a CheatDatabase.
// ═══════════════════════════════════════════════════════════════════════════

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "cheat_parser.h"
#include "cheat_vm.h"
#include "cheat_engine.h"

// ─────────────────────────────────────────────
//  Trim helper (local copy — keeps this file self-contained)
// ─────────────────────────────────────────────
static void trim(char *s) {
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);

    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
}

// ─────────────────────────────────────────────
//  Is this a valid hex digit string of the given length?
// ─────────────────────────────────────────────
static bool isHexRun(const char *s, int len) {
    for (int i = 0; i < len; i++) {
        if (!isxdigit((unsigned char)s[i])) return false;
    }
    return true;
}

// ─────────────────────────────────────────────
//  Parse "XXXXXXXX XXXXXXXX" → CheatCodeLine
// ─────────────────────────────────────────────
bool CP_ParseCodeLine(const char *line, CheatCodeLine *out) {
    // Expect exactly: 8 hex, whitespace, 8 hex (trailing junk ignored)
    size_t len = strlen(line);
    if (len < 17) return false;
    if (!isHexRun(line, 8)) return false;
    if (!isspace((unsigned char)line[8])) return false;

    // Skip whitespace run
    size_t i = 8;
    while (i < len && isspace((unsigned char)line[i])) i++;
    if (len - i < 8) return false;
    if (!isHexRun(line + i, 8)) return false;

    char addrBuf[9] = {0};
    char valBuf[9]  = {0};
    memcpy(addrBuf, line, 8);
    memcpy(valBuf, line + i, 8);

    out->addr = (u32)strtoul(addrBuf, NULL, 16);
    out->val  = (u32)strtoul(valBuf,  NULL, 16);
    return true;
}

// ─────────────────────────────────────────────
//  Split a "[Category] Name" header into category + name.
//  If there's no bracket pair inside the header text itself, category
//  is left empty and the whole string becomes the name.
// ─────────────────────────────────────────────
static void splitCategoryName(const char *header, char *category, char *name) {
    category[0] = '\0';
    name[0] = '\0';

    // header is the text already stripped of the OUTER [ ] that opened
    // the section. We look for an embedded "[Cat] Rest" convention.
    if (header[0] == '[') {
        const char *close = strchr(header, ']');
        if (close) {
            size_t catLen = (size_t)(close - header - 1);
            if (catLen > 0 && catLen < CC_MAX_CATEGORY) {
                memcpy(category, header + 1, catLen);
                category[catLen] = '\0';
            }
            const char *rest = close + 1;
            while (*rest == ' ') rest++;
            strncpy(name, rest, CC_MAX_NAME - 1);
            return;
        }
    }
    strncpy(name, header, CC_MAX_NAME - 1);
}

// ─────────────────────────────────────────────
//  Parse a whole file
// ─────────────────────────────────────────────
int CP_ParseFile(const char *path, CheatDatabase *db) {
    if (!path || !db) return -1;

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    memset(db, 0, sizeof(*db));

    char line[512];
    Cheat *cur = NULL;

    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        trim(line);

        if (line[0] == '\0') continue;

        // Game name header comment: "*Game: Foo" or ";Game: Foo"
        if ((line[0] == '*' || line[0] == ';') && db->gameName[0] == '\0') {
            const char *colon = strchr(line, ':');
            if (colon && (strncasecmp(line + 1, "Game", 4) == 0)) {
                const char *namePart = colon + 1;
                while (*namePart == ' ') namePart++;
                strncpy(db->gameName, namePart, CC_MAX_NAME - 1);
                continue;
            }
        }

        // Comment / note line — attach to the current cheat if one is open
        if (line[0] == '*' || line[0] == ';') {
            if (cur && cur->note[0] == '\0') {
                strncpy(cur->note, line + 1, CC_MAX_NOTE - 1);
                trim(cur->note);
            }
            continue;
        }

        // New cheat header
        if (line[0] == '[') {
            char *close = strrchr(line, ']');
            // Find the LAST ']' on the line so "[Cat] Name" still opens
            // correctly even if Name itself doesn't contain brackets.
            // (Outer bracket is the cheat header delimiter; an inner
            //  "[Cat]" is handled separately below.)
            if (!close) continue;

            if (db->cheatCount >= CC_MAX_CHEATS) {
                // Database full — stop parsing further entries cleanly
                break;
            }

            cur = &db->cheats[db->cheatCount++];
            memset(cur, 0, sizeof(*cur));
            cur->enabled       = false;
            cur->categoryIndex = -1;

            // Outer header text is everything between the FIRST '[' and
            // the matching LAST ']' on the line.
            char headerBuf[CC_MAX_NAME + CC_MAX_CATEGORY + 4];
            size_t headerLen = (size_t)(close - line - 1);
            if (headerLen >= sizeof(headerBuf)) headerLen = sizeof(headerBuf) - 1;
            memcpy(headerBuf, line + 1, headerLen);
            headerBuf[headerLen] = '\0';

            splitCategoryName(headerBuf, cur->category, cur->name);
            if (cur->name[0] == '\0') {
                strncpy(cur->name, headerBuf, CC_MAX_NAME - 1);
            }
            continue;
        }

        // Code line — must belong to a currently open cheat
        if (!cur) continue;
        if (cur->lineCount >= CC_MAX_CODELINES) continue;

        CheatCodeLine codeLine;
        if (CP_ParseCodeLine(line, &codeLine)) {
            cur->lines[cur->lineCount++] = codeLine;
        }
        // Lines that don't match the hex pattern and aren't headers or
        // comments are silently skipped (tolerates stray blank/odd lines
        // some community cheat files contain).
    }

    fclose(f);

    // Validate every parsed cheat so the UI can flag broken entries
    // up front rather than discovering it mid-gameplay.
    for (u32 i = 0; i < db->cheatCount; i++) {
        CVM_Validate(&db->cheats[i]);
    }

    db->loaded = true;
    return 0;
}
