# CustomCheats — Luma3DS 3GX Plugin

Browse and toggle custom cheats **live, from an in-game menu**, for any 3DS game — no need to dig through Rosalina or restart the game when you want to flip a code on or off.

---

## Features

| Feature | Details |
|---|---|
| **In-game toggle menu** | Bottom-screen overlay, hotkey `R + SELECT` |
| **Per-game cheat database** | Plain-text files keyed by title ID |
| **Live enable/disable** | No reboot required |
| **Persistent state** | Toggle choices survive between sessions |
| **Categories** | Group related cheats with `[Category] Name` headers |
| **Code detail view** | Inspect raw hex codes and notes per cheat |
| **Safety validation** | Unsupported/malformed cheats are flagged, not guessed at |
| **Auto-disable on fault** | A cheat that writes out-of-bounds disables itself with a log entry |
| **Gateway/AR-compatible** | Reuses the widely-shared community cheat-code format |

---

## Requirements

- **Luma3DS 13.0+** with 3GX plugin support enabled
- **SD card** formatted as FAT32

---

## Installation

1. Build with devkitARM (see [Building](#building)), or use a pre-built `CustomCheats.3gx`.
2. Copy to SD card:
   ```
   sdmc:/luma/plugins/CustomCheats.3gx
   ```
3. Copy the config file:
   ```
   sdmc:/luma/plugins/CustomCheats/config.ini
   ```
4. Enable plugins in Luma3DS: hold **SELECT** while booting → *Enable game patching* → *Enable 3GX plugins*.

---

## Adding Cheats

Cheat files live at:

```
sdmc:/cheats/<title-id>.txt
```

Find the title ID the same way as for ModLoader-style plugins — Rosalina's process list, or sites like GameTDB/3dsdb.

### File format

```ini
*Game: Super Cheat Bros
*Free-form comment lines start with * or ;

[Infinite HP]
0A123456 000003E7

[Boss Rush] Unlock All Levels
1A123458 0000FFFF
*Note: enable before entering the level select menu
```

- `[Name]` opens a new cheat. `[Category] Name` groups it under "Category" in the menu.
- Each code line is two 8-digit hex words separated by whitespace — the same shape used by the long-running Gateway/Action Replay-style community databases, so existing code dumps generally drop in with no editing.
- Lines starting with `*` or `;` become a note shown in the cheat's detail view.

A full annotated example covering every supported opcode ships at `data/example_cheats.txt`.

---

## Supported Code Types

| Opcode | Meaning |
|---|---|
| `0XXXXXXX YYYYYYYY` | 32-bit write |
| `1XXXXXXX 0000YYYY` | 16-bit write |
| `2XXXXXXX 000000YY` | 8-bit write |
| `3/4XXXXXXX YYYYYYYY` | 32-bit if-equal / if-not-equal |
| `5/6XXXXXXX YYYYYYYY` | 16-bit if-equal / if-not-equal |
| `8XXXXXXX YYYYYYYY` | Add to memory |
| `94000130 YYYYZZZZ` | Button gate (held / not-held mask) |
| `A0000000 NNNNNNNN` | Load offset register |
| `B0000000 00000000` | End conditional block |
| `C0000000 NNNNNNNN` | Loop start (repeat N times) |
| `D0000000 00000000` | Loop end |
| `D2000000 00000000` | Terminator (no-op) |
| `D3000000 XXXXXXXX` | Set base address register |
| `D4000000 NNNNNNNN` | Delay N ms |

Anything outside this set is flagged with a grey **`!`** marker in the menu and can't be enabled — the plugin won't guess at an opcode's meaning and risk corrupting game state.

**Continuous vs. one-shot:** most useful cheats (infinite HP, unlimited ammo) need to be re-applied every frame because the game keeps overwriting the value — this plugin re-applies all enabled cheats roughly 30 times a second by default, configurable via `poll_interval_ms`.

---

## In-Game Menu

Press **R + SELECT** to open the overlay.

| Control | Action |
|---|---|
| `↑` / `↓` | Move selection |
| `A` | Toggle the selected cheat on/off |
| `X` | View raw codes + notes for the selected cheat |
| `B` | Back to the list (from detail view) |
| `L` / `R` | Switch pages |
| `R + SELECT` | Hide the menu |

Toggling a cheat takes effect immediately and is saved right away, so a crash or power cycle won't lose your choice.

---

## Config

`sdmc:/luma/plugins/CustomCheats/config.ini`:

```ini
[CustomCheats]
enabled          = true
logging          = true
debug            = false
poll_interval_ms = 33
```

---

## Building

```bash
# devkitPro, if you don't already have it
bash <(curl -sSL https://pkg.devkitpro.org/installer/linux)
dkp-pacman -S 3ds-dev

git clone https://github.com/ProjectDreadman/CheatsPlugin3GX
cd CheatsPlugin3GX
make
make install SD_MOUNT=/media/youruser/SDCARD
```

---

## How It Works

```
Plugin thread (≈30 Hz)
        │
        ├─ hidScanInput() → check R+SELECT hotkey / menu input
        │
        └─ CC_ApplyEnabledCheats()
                │
                for each enabled, non-error cheat:
                │
            CVM_Execute(cheat)
                │
        ┌───────┴────────┐
        │  walk code lines │
        │  bounds-check    │
        │  every address   │
        │  against the     │
        │  game's code +   │
        │  heap regions    │
        └───────┬────────┘
                │
        write / conditional skip / loop / button-gate
                │
        out-of-bounds? → auto-disable cheat, log it
```

1. On load, `CC_LoadDatabase()` parses `sdmc:/cheats/<titleid>.txt` into a list of named cheats, validating each one's opcodes up front via `CVM_Validate()`.
2. Saved on/off state from a previous session is restored by matching cheat **names** (not list position), so editing the cheat file between sessions doesn't silently apply the wrong saved state to the wrong entry.
3. Every poll tick, `CC_ApplyEnabledCheats()` runs the small VM in `cheat_vm.c` over each enabled cheat's code lines, translating Gateway/AR-style heap-relative addresses to absolute addresses and bounds-checking every single write against the game's resolved code and heap regions before it touches memory.
4. Toggling a cheat in the overlay persists immediately to `sdmc:/luma/plugins/CustomCheats/state/<titleid>.ini`.

---

## Safety Notes

- Every memory write is bounds-checked against the game's actual mapped code and heap regions — addresses outside those ranges are rejected rather than written, and the offending cheat auto-disables itself.
- Conditional (`3/4/5/6`) and loop (`C`/`D0`) blocks are validated for balance at load time; a cheat with an unmatched block is flagged as unsupported rather than risking infinite loops on the poll thread.
- This plugin can't validate that a *correctly-formed* code targets the *right* piece of game state — a well-formed cheat can still apply the wrong effect if the underlying code was written for a different game version. Always source cheat codes from a reputable per-game database.

---

## License

MIT — see [LICENSE](LICENSE)
