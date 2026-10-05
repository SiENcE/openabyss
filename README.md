# OpenAbyss

A reimplementation of *Ultima Underworld: The Stygian Abyss* (1992) in
portable C11: the title, the menu, character generation, the whole game,
its cutscenes, music and saves, played the way the original plays them.

**This repository contains no part of the game.** You need your own copy.
OpenAbyss is made and tested against **the GOG release** of Ultima
Underworld; other releases may work, and are untested -- the program says
so when it meets one, and a report of how it went is welcome.

## Getting it

**Windows and Linux builds** are on the releases page: a zip for Windows
(unpack it anywhere and run `openabyss.exe`) and an AppImage for Linux
(make it executable and run it). macOS is not built yet.

**From source** you need a C11 compiler and CMake 3.16 or later; SDL 3 is
used if it is installed, and otherwise downloaded and built with the
program:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build                # builds build/openabyss
./build/openabyss
```

On Linux with SDL 3 installed (and `pkg-config`), `make` builds `./openabyss`
as well. For Windows, it can be cross-compiled with MinGW-w64 on Linux or in
WSL: `cmake/mingw-w64-x86_64.cmake` says how. SDL 3.4 or later writes screenshots as PNG; 3.2 writes them as BMP.

## Finding the game

GOG's release holds the game as a CD image, `game.gog`, in the folder GOG
installs it to. The first time it runs, OpenAbyss looks for that folder
where GOG, GOG Galaxy, Heroic and Wine put it; if it finds nothing it asks
you for the folder -- choose the one GOG installed the game to. It then
copies the game out of `game.gog` once (about 12 MB), into its per-user
folder (`~/.local/share/openabyss/` on Linux, `%APPDATA%\openabyss\` on
Windows), and remembers it in `openabyss.cfg` there; `./openabyss
--locate` asks again.

A folder already holding the game's files works as well: the one with
`UW.EXE` and the folders `DATA`, `CRIT` and `CUTS`, or the folder above it
that holds it in `UW`.

### Without installing: the offline installer

GOG's offline installer can be unpacked without running it, on Linux or
Windows, with [innoextract](https://constexpr.org/innoextract/):

1. On gog.com, open Ultima Underworld in your library and download the
   **offline backup game installer** (`setup_ultima_underworld_....exe`).
   The small "GOG Galaxy" installer is only a downloader and holds no game
   files.
2. Take `game.gog` out of it, into a folder of your choosing:

   ```sh
   mkdir -p ~/Games/UltimaUnderworld && cd ~/Games/UltimaUnderworld
   innoextract -I game.gog /path/to/setup_ultima_underworld_*.exe
   ```

3. Run OpenAbyss. A folder in `Games` in your home folder whose name holds
   "underworld" is found by itself; anywhere else, choose it when
   OpenAbyss asks, or give it once as `--dir`.

`game.gog` is an ordinary ISO image under another name, so it can also be
opened with 7-Zip or, renamed `game.iso`, with a file manager, and its `UW`
folder used directly.

A folder can also be given every time: `--dir /path/to/UW`, or the
`UW_DATA` environment variable, or running OpenAbyss from inside it. The
case of the files' names does not matter: a copy extracted in lower case
works as well.

## Playing

**Sound plays by default** -- the music through an AdLib model (the game's
own ADLIB.ADV voice layer and an OPL2) and the cutscenes' voices through a
digital one. `--nosound` silences it; `--sound` keeps it on even for a
saved game that had it switched off.

**The window** shows the 320 x 200 picture at 4:3, as a VGA monitor did
(`--square` for square pixels), resizable; `--scale N` sets its first size,
`--fullscreen` or Alt-Enter full screen, `--no-vsync` and `--pace MS` the
timing.

**The keys** are the original's. In the dungeon, the movement keys act for
as long as they are held:

| Key | |
|---|---|
| `w` / `s` / `x` | run forward / walk forward / walk back |
| `a` / `d` | turn left / right |
| `z` / `c` | slide left / right |
| Shift with `w` `s` `x` `a` `d` | one step or turn at a time |
| `j` / Shift-`j` | jump / standing jump |
| `e` / `q` | rise / sink, while levitating or flying |
| `1` / `2` / `3` | look down / straight ahead again / look up |
| `p` / `;` / `.` | attack: bash / slash / thrust |
| F1 .. F6 | the action icons: options, talk, get, look, fight, use |
| F7 | the character's statistics |
| F8 | cast the spell on the rune shelf |
| F9 | use the Track skill |
| F10 | sleep |
| Ctrl-s / Ctrl-r | save / restore a game |
| Ctrl-m / Ctrl-f / Ctrl-d | music / sound / detail options |
| Ctrl-q | quit, asking first |
| keypad 1..9 | glide the cursor toward that edge or corner |
| Tab / Shift-Tab | jump the cursor between the screen's three parts |
| `1` .. `4` | in a conversation, the answer to give |
| Alt-q | screenshot, `uwpicNNN.png` in the working directory |
| Alt-x | quit at once |
| Alt-F7 / Alt-F8 | the game's version / the level and position |

**The mouse** plays as the original's: the left button held in the view
walks -- turning toward the side the cursor is on, forward the higher it
is, and in the view's bottom strip sliding left, back or right by thirds --
and both buttons together jump. Clicks act through the action icons and
the panels.

**The textures** are drawn as the original drew them, which is why a floor
or a wall seen at a slant swims as you move. `--perspective` draws them
perspective-correct instead: the same faces covering the same pixels, with
the texture inside each one placed where it belongs.

**Saves** go where the original keeps them, `SAVE1`..`SAVE4` beside the
game's files, when that directory is writable, and otherwise to SDL's
per-user data directory; `--saves DIR` puts them elsewhere. They are the
original's format: a save made here loads in the original, and the other
way round.

`--cutscene N` plays one cutscene on its own (the introduction is 1).

## The code

`src/` is the game as a library of C modules, named for the routines of
the original they reimplement; `src/tools/uwshell.c` is the program around
it -- the window, the clock, input, sound and the game's modes. The
library draws nothing itself: it computes into the same memory layout the
original used, and the program presents it.

## Licence and trademarks

The code is under the MIT licence (`LICENSE`). Ultima and Ultima Underworld
are trademarks of their owners; this project is not affiliated with or
endorsed by them, and it distributes none of the game's files.
