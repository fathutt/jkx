# JKX Engine

A single-player engine for **Jedi Knight II: Jedi Outcast** and **Jedi Knight: Jedi Academy**, built on
Vulkan 1.3.

Forked from [OpenJK](https://github.com/JACoders/OpenJK) with the Vulkan renderer from
[JKSunny/EternalJK](https://github.com/JKSunny/EternalJK) (`pbr` branch), which in turn derives from
[Quake3e](https://github.com/ec-/Quake3e). Multiplayer is removed; the OpenGL renderers are gone; the
game data stays untouched.

You need the original game files. This is an engine, not a game.

---

## Status

| | |
|---|---|
| Builds | both engines and both game libraries, on Linux and Windows |
| Renderer | Vulkan only. `rd-vanilla` and the runtime renderer loader are both gone |
| Language | C++20, `-Wall -Wextra` |
| Platforms | Windows x64, Linux x64 |
| Under test | both games reach a loaded map, draw a frame with a head-up display and quit, on every commit - with no GPU and no retail assets. See `tools/verify` |
| Not yet checked on real hardware | the PBR lighting path. The software rasteriser CI runs on reports a Vulkan limit that switches it off, so it has never executed there |

One engine binary runs both games. Which one it is is `com_game`, a command-line-only
cvar read once at startup; the gamecode is still two modules, loaded by name.

    jkx.x86_64  +set com_game academy  +  jagamex86_64      Jedi Academy
    jkx.x86_64  +set com_game outcast  +  jogamex86_64      Jedi Outcast

`jkx_launcher` finds the retail installations, works out which game each one is from
its assets, and passes the answer through, so nobody has to type it.

---

## Building

### Linux

```sh
sudo apt install cmake ninja-build libsdl2-dev libjpeg-dev libpng-dev \
                 zlib1g-dev libopenal-dev libgl1-mesa-dev libvulkan-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Shaders are compiled into `base/shaders.pak` when `glslc` is on the path - it ships with the Vulkan
SDK. Without it the build still completes and the engine has no shaders to load, which it will say.

### Windows

Visual Studio 2022, x64. Dependencies via vcpkg:

```
vcpkg install sdl2[core,vulkan]:x64-windows libjpeg-turbo:x64-windows libpng:x64-windows zlib:x64-windows
cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

### Options

Everything is ON by default; these are for building less than all of it.

| Option | Default | What it does |
|---|---|---|
| `BuildEngine` | `ON` | the engine, which is one binary for both games |
| `BuildJAGame` / `BuildJOGame` | `ON` | the gamecode: Jedi Academy, Jedi Outcast |
| `BuildRdVulkan` | `ON` | the renderer, which is linked into the engine rather than loaded |
| `BuildTests` | `OFF` | the unit tests |
| `JKX_BUILD_SHADERS` | auto | on when `glslc` is found |
| `JKX_WARNINGS_AS_ERRORS` | `OFF` | `-Werror` / `/WX` |
| `JKX_ENABLE_ASAN` / `_UBSAN` / `_TSAN` | `OFF` | sanitizers |

---

## Contributing

Read [`docs/CODING-STANDARDS.md`](docs/CODING-STANDARDS.md) first. It is short and it is enforced.

The rule people trip over most:

> **Rule 1 — code and comments are Latin-only. Cyrillic belongs in documentation.**

Before pushing, run what CI runs:

```sh
tools/ci/local.sh              # everything, fourteen stages, ~35 minutes cold
tools/ci/local.sh policy       # just the gates, ~10 seconds
```

`policy` is the one worth running constantly. It is nine checks, and each exists because something
got through once:

| Check | Refuses |
|---|---|
| `check_ascii.py` | non-Latin characters in code (rule 1) |
| `check_layering.py` | a new include that points up a layer - engine into game, render into game |
| `check_interface.py` | the engine seeing more of the gamecode than it does today |
| `check_sources.py` | a source on disk that no CMake list builds, and project files for any build system that is not CMake |
| `check_cvars.py` | one setting registered under two names, or two spellings of one name |
| `check_msvc.py` | declaration shapes that only MSVC accepts |
| `check_commits.py` | a commit message that points at a conversation |
| `actionlint` | broken workflows. **Install `shellcheck` too** - without it actionlint reads the YAML and skips every `run:` block, silently |

Two of those are ratchets rather than gates: `check_layering.py` carries 25 inherited violations in
`tools/ci/layering-baseline.txt` and `check_interface.py` carries a line count. Both files may shrink
and never grow.

The rest of `local.sh` is builds - Release, Debug, Windows cross, sanitizers - the unit tests, and six
headless runs of the engine itself. The one thing it cannot cover is MSVC, which only GitHub has.

---

## Documentation

| Document | What it is |
|---|---|
| [`docs/CODING-STANDARDS.md`](docs/CODING-STANDARDS.md) | How we write code, and which defect each rule prevents |
| [`code/api/README.md`](code/api/README.md) | What the engine and the games promise each other, and what does not belong there |
| [`tools/verify/README.md`](tools/verify/README.md) | The headless bench: what it covers and what it has caught |

The working documents - the backlog, the roadmaps, the phase reports and the
upstream survey - are not in this repository. They were, and a copy of a document
is a second source of truth that drifts silently: the committed backlog was
forty kilobytes behind the live one before anyone noticed. They live in one place
now and this file no longer pretends otherwise.

Design documents are in Russian; code is not. See rule 1.

---

## Layout

```
code/qcommon      engine core            code/rd-vulkan   the renderer
code/client       client                 code/rd-common   renderer code shared with the engine
code/server       server                 code/ghoul2      skeletal animation
code/api          what the engine and    code/ui          menus, compiled into the engine
                  the games promise      shared/          platform, SDL, safe utilities
                  each other
games/ja/        Jedi Academy: game, cgame, icarus
games/jo/        Jedi Outcast: game, cgame, icarus
shared/win32      Windows resources      third_party/     vendored dependencies
tools/ci          the gates              tools/verify     the headless bench
```

`shared/win32` is one copy of each resource script. `product.h` there holds the
strings; the engine's no longer differ per game, because the engine no longer
does. There used to be two copies of each script, and because resource scripts
compile only on Windows, both went stale twice without anything failing.

`code/api` is three headers and it is the whole of what the engine may see of a game. It was 13,139
lines through 13 include sites when that was first measured, and `check_interface.py` is what keeps it
from growing back.

---

## Licence

**GPLv2**, without the "or later" clause — inherited from the Raven/Activision release, Quake III
Arena, Quake3e and EternalJK. Any derivative must be GPLv2. See [`LICENSE.txt`](LICENSE.txt).

Lineage of the renderer sources, in order: id Software -> Raven Software -> OpenJK contributors ->
ec-/Quake3e -> kennyalive -> JKSunny -> this project.
