# JKX

A single-player engine for **Jedi Knight II: Jedi Outcast** and **Jedi Knight: Jedi Academy**, built on
Vulkan 1.3.

Forked from [OpenJK](https://github.com/JACoders/OpenJK) with the Vulkan renderer from
[JKSunny/EternalJK](https://github.com/JKSunny/EternalJK) (`pbr` branch), which in turn derives from
[Quake3e](https://github.com/ec-/Quake3e). Multiplayer is removed; the OpenGL renderers are on their
way out; the game data stays untouched.

You need the original game files. This is an engine, not a game.

---

## Status

Phase 0 of the plan.

| | |
|---|---|
| Builds | JKA SP + JK2 SP engines, gamecode, and the legacy `rd-vanilla` renderer |
| Language | C++20, `-Wall -Wextra` |
| Platforms | Windows x64, Linux x64 |
| Vulkan renderer | Imported, **not yet buildable** — waiting on the cross-platform shader toolchain (phase 1) |
| Multiplayer | Removed (~509k lines) |

`rd-vanilla` is kept deliberately: it is the visual reference the Vulkan port is compared against, and
it is deleted only once both campaigns are playable on Vulkan.

---

## Building

### Linux

```sh
sudo apt install cmake ninja-build libsdl2-dev libjpeg-dev libpng-dev \
                 zlib1g-dev libopenal-dev libgl1-mesa-dev libvulkan-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Windows

Visual Studio 2022, x64. Dependencies via vcpkg:

```
vcpkg install sdl2:x64-windows libjpeg-turbo:x64-windows libpng:x64-windows zlib:x64-windows
cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

### Useful options

| Option | Default | What it does |
|---|---|---|
| `JKX_WARNINGS_AS_ERRORS` | `OFF` | `-Werror` / `/WX` |
| `JKX_ENABLE_ASAN` | `OFF` | AddressSanitizer |
| `JKX_ENABLE_UBSAN` | `OFF` | UndefinedBehaviorSanitizer |
| `JKX_ENABLE_TSAN` | `OFF` | ThreadSanitizer |
| `JKX_BUILD_VULKAN_RENDERER` | `OFF` | Vulkan renderer (needs the phase-1 shader toolchain) |

---

## Contributing

Read [`docs/CODING-STANDARDS.md`](docs/CODING-STANDARDS.md) first. It is short and it is enforced.

The rule people trip over most:

> **Rule 1 — code and comments are Latin-only. Cyrillic belongs in documentation.**

Run the same gates CI runs, before pushing:

```sh
python3 tools/ci/check_ascii.py              # rule 1
python3 tools/ci/check_layering.py .         # layer dependencies (ratchet)
python3 tools/ci/check_vulkan_baseline.py    # Vulkan 1.3 feature baseline
clang-format --dry-run --Werror <changed files>
```

`check_layering.py` is a ratchet: the tree starts with 44 inherited violations recorded in
`tools/ci/layering-baseline.txt`. That file may shrink, never grow.

---

## Documentation

| Document | What it is |
|---|---|
| [`docs/CODING-STANDARDS.md`](docs/CODING-STANDARDS.md) | How we write code, and which defect each rule prevents |

The working documents - the backlog, the roadmaps, the phase reports and the
upstream survey - are not in this repository. They were, and a copy of a document
is a second source of truth that drifts silently: the committed backlog was
forty kilobytes behind the live one before anyone noticed. They live in one place
now and this file no longer pretends otherwise.

Design documents are in Russian; code is not. See rule 1.

---

## Layout

```
code/qcommon      engine core            code/rd-vulkan   Vulkan renderer (phase 1)
code/client       client                 code/rd-common   shared renderer code
code/server       server                 code/ghoul2      skeletal animation
code/api          the engine-game        code/ui          menus, compiled into the engine
                  contract, three        code/icarus      scripting VM
                  headers                shared/          platform, SDL, safe utilities
games/jka/        JKA gameplay: game, cgame
games/jk2/        JK2 gameplay: game, cgame, icarus
tools/ci          policy checks
```

The `engine/ render/ game/` reorganisation described in the roadmap lands at the end of phase 2, once
the Vulkan renderer is wired into the SP tree. Doing it earlier would make cherry-picking upstream
renderer fixes needlessly painful.

---

## Licence

**GPLv2**, without the "or later" clause — inherited from the Raven/Activision release, Quake III
Arena, Quake3e and EternalJK. Any derivative must be GPLv2. See [`LICENSE.txt`](LICENSE.txt).

Lineage of the renderer sources, in order: id Software -> Raven Software -> OpenJK contributors ->
ec-/Quake3e -> kennyalive -> JKSunny -> this project.
