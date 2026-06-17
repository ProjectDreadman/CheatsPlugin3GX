#pragma once
#ifndef INI_PARSER_H
#define INI_PARSER_H

#include <stddef.h>
#include <stdbool.h>

// ═══════════════════════════════════════════════════════════════════════════
//  Tiny INI parser — used for config.ini and per-game cheat-state files.
// ═══════════════════════════════════════════════════════════════════════════

#define INI_MAX_SECTION   64
#define INI_MAX_KEY       96
#define INI_MAX_VALUE     256
#define INI_MAX_ENTRIES   320   // generous: one entry per cheat-state line

typedef struct {
    char section[INI_MAX_SECTION];
    char key[INI_MAX_KEY];
    char value[INI_MAX_VALUE];
} IniEntry;

typedef struct {
    IniEntry entries[INI_MAX_ENTRIES];
    int      count;
} IniFile;

int  INI_Parse(const char *path, IniFile *out);
const char *INI_Get(const IniFile *ini, const char *section, const char *key);
bool INI_GetBool(const IniFile *ini, const char *section, const char *key, bool def);
int  INI_GetInt (const IniFile *ini, const char *section, const char *key, int  def);

// Writer — used to persist cheat toggle state between sessions.
int  INI_BeginWrite(const char *path);
int  INI_WriteSection(const char *section);
int  INI_WriteBool(const char *key, bool value);
int  INI_EndWrite(void);

#endif // INI_PARSER_H
