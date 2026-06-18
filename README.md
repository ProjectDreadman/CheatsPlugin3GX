# CustomCheats — Luma3DS 3GX Plugin

Browse and toggle custom cheats **live, from an in-game menu**, for any 3DS game — no need to dig through Rosalina or restart the game when you want to flip a code on or off.

---

## Features

| Feature | Details |
|---|---|
| **In-game toggle menu** | Bottom-screen overlay, hotkey `R + SELECT` |
| **Per-game cheat database** | Plain-text files keyed by title ID |
| **Live enable/disable** | No reboot required |
| **Dynamic reload** | Edit `cheats.txt` on the SD card, reload it from the in-game menu — no recompiling, no restarting the game |
| **Categories** | Group related cheats under `[Category] Name` headers; browse by category or see everything at once |
| **Persistent state** | Toggle choices survive between sessions |
| **Code detail view** | Inspect raw hex codes and notes per cheat |
| **Two GUI renderers** | Safe framebuffer renderer (default) or an experimental Citro2D (GPU-accelerated) renderer |
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

Press **R + SELECT** to open the overlay. It opens on the **Categories** page.

| Page | Control | Action |
|---|---|---|
| Categories | `↑` / `↓` | Move selection |
| Categories | `A` | Open "All Cheats" or the selected category |
| Categories | `X` | **Reload** `cheats.txt` from the SD card right now, no restart needed |
| Categories | `Y` | Open the About page |
| List | `↑` / `↓` | Move selection |
| List | `A` | Toggle the selected cheat on/off |
| List | `X` | View raw codes + notes for the selected cheat |
| List | `B` | Back to Categories |
| Detail | `↑` / `↓` | Scroll through code lines |
| Detail | `B` | Back to the list |
| (any) | `R + SELECT` | Hide the menu |

Toggling a cheat takes effect immediately and is saved right away, so a crash or power cycle won't lose your choice.

### Dynamic reloading

Edit `sdmc:/cheats/<title-id>.txt` on a PC (swap the SD card, or use an FTP/USB-access homebrew app that doesn't require ejecting it), then press **X** on the Categories page. The file is re-parsed on the spot:

- Cheats you had enabled are kept enabled if a cheat with the same name still exists in the edited file.
- Cheats you removed from the file disappear from the menu.
- Newly added cheats show up disabled by default, exactly like a fresh load.

No recompiling the plugin and no restarting the game are required — only the cheat *database* is dynamic; the plugin binary itself still needs a rebuild for code changes.

### Categories

Add a category to any cheat by prefixing its header with `[Category]`:

```ini
[Player] Infinite HP
0A123456 000003E7
```

All cheats sharing the same category text (case-insensitive) are grouped together on the Categories page, which also shows a live `(enabled/total)` count per category. Cheats with a plain `[Name]` header and no category prefix are reachable only through "All Cheats" — they don't create a stray empty category.

---

## Graphical Interface

CustomCheats ships with **two interchangeable menu renderers**:

| Renderer | How it draws | Risk | Default |
|---|---|---|---|
| **Framebuffer** | Direct pixel writes to the bottom screen's framebuffer | None — never touches the GPU command queue | ✅ Yes |
| **Citro2D** | GPU-accelerated shapes + scalable text via citro2d | See below | ❌ No (opt-in) |

### Why the Citro2D backend is opt-in, not default

A 3GX plugin's code runs **inside the game's own process**, sharing its address space and — critically — its GPU command queue. By the time the plugin's thread runs, the game has almost certainly already initialized citro3d and owns the active render targets. Citro2D was never designed with "share a GPU context with a process you don't control" as a use case; every official example calls `C3D_Init()` itself, because normally your program *is* the only thing using the GPU.

This plugin's Citro2D backend (`gui_citro2d.c`) does **not** call `C3D_Init()` — instead it:

1. Checks for an explicit safety confirmation from the Luma3DS plugin SDK (`plgIsGpuContextActive()`) before touching any Citro2D/Citro3D call. If that hook isn't available on your Luma build, the backend refuses to activate at all and the plugin transparently uses the framebuffer renderer — `gui_backend_citro2d = true` in config.ini is a *request*, not a guarantee.
2. Falls back to the framebuffer renderer **permanently for the rest of that game session** the instant a draw call reports a problem, rather than retrying.
3. Only ever targets the bottom screen, to minimize overlap with whatever the game is doing on top.

Even with those guards, submitting `C3D_FrameBegin()`/`C3D_FrameEnd()` from a second thread against a GPU command queue the game's own main thread is also using is inherently fragile — the practical effect can range from a perfectly fine frame to a visibly torn/glitched menu, and in the worst case a GPU stall. That's *why* it's off by default rather than something this plugin tries to paper over.

### Enabling it

```bash
make ENABLE_CITRO2D=1
```

then set in `config.ini`:

```ini
gui_backend_citro2d = true
```

If you only ever build with plain `make` (no `ENABLE_CITRO2D=1`), the Citro2D source file isn't even compiled, so you don't need citro2d/citro3d installed at all to build and use the rest of the plugin.

---

## Config

`sdmc:/luma/plugins/CustomCheats/config.ini`:

```ini
[CustomCheats]
enabled             = true
logging             = true
debug               = false
poll_interval_ms    = 33
gui_backend_citro2d = false
```

---

## Building

```bash
# devkitPro, if you don't already have it
bash <(curl -sSL https://pkg.devkitpro.org/installer/linux)
dkp-pacman -S 3ds-dev

git clone https://github.com/yourname/CustomCheats3GX
cd CustomCheats3GX
make
make install SD_MOUNT=/media/youruser/SDCARD
```

To also build the experimental Citro2D GUI backend (see [Graphical Interface](#graphical-interface) before enabling it):

```bash
dkp-pacman -S 3ds-citro2d 3ds-citro3d
make ENABLE_CITRO2D=1
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
