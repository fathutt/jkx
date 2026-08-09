#!/usr/bin/env python3
"""One command that measures everything the plan is betting on.

The verification checklist in docs/Phase1-Report.md needs a GPU and the retail
game files, so it cannot run in CI. This turns it into a single command with no
arguments:

    python tools/verify/verify.py

The game is found in the Steam libraries; --game overrides that when it is
installed somewhere else. It launches the engine several times with generated
configs, times each run, collects the console output, and writes
verification-report.md next to itself. Nothing is typed at the console and
nothing has to be watched.

What it measures, and why each one matters:

  startup            what a persistent pipeline cache saves. Upstream created
                     the cache with no initial data and never saved it, so
                     every pipeline is compiled from scratch on every launch.
                     Measured on an RTX 3070: 2.5 s to the menu, where only 64
                     pipeline objects exist. The cache matters in a loaded map,
                     not at the menu.
  vid_restart        the same cost again, and the one players hit most.
  map load           geometry, textures, and the IBL bake where there is one.
                     The bake only runs for maps shipping cubemaps/<map>/
                     env.json, which retail maps do not, so on a stock install
                     it is not in this number.
  lighting path      whether PBR is actually on. On a device reporting
                     maxBoundDescriptorSets < 11 it silently switched itself
                     off, so a measurement could be of the wrong path entirely.
  validation         with a debug build, how clean the renderer is.

Requires only Python 3.9+. No dependencies.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

# --------------------------------------------------------------------------
# scenarios
# --------------------------------------------------------------------------

# Each scenario is a config the engine executes on startup. Every one ends in
# quit, so the process exits on its own and the wall clock is the measurement.
SCENARIOS = {
    "startup": {
        "label": "startup to main menu",
        "cfg": [
            "vkinfo",
            "condump jkx_startup.txt",
            "quit",
        ],
        "note": "cost a persistent pipeline cache removes",
    },
    "vid_restart": {
        "label": "startup + vid_restart",
        "cfg": [
            "vid_restart",
            "vkinfo",
            "condump jkx_vidrestart.txt",
            "quit",
        ],
        "note": "subtract startup to get the restart cost alone",
    },
    "map": {
        "label": "startup + map load",
        "cfg": [
            "map {map}",
            # Dumped twice, to different files. The first dump happens as soon
            # as the map is up, so the load output survives whatever happens
            # next; the previous run produced no dump at all and left nothing
            # to diagnose. Frames only tick once the map is up, so these waits
            # are rendering time, not a timeout on the load.
            "wait 60",
            "condump jkx_map.txt",
            "wait 200",
            # In a loaded map this is the number that matters: pipelines are
            # created on demand, so the menu count says almost nothing.
            "vkinfo",
            "condump jkx_mapinfo.txt",
            "quit",
        ],
        "note": "subtract startup to get the load cost alone",
    },
}


# The condumps the scenarios above produce, in the order they are concatenated
# before parsing.
DUMPS = ("jkx_startup.txt", "jkx_vidrestart.txt", "jkx_map.txt")

# The map scenario writes a second, later dump. It is read when present and
# ignored when the run died before producing it.
EXTRA_DUMPS = ("jkx_mapinfo.txt",)


# Set on the command line rather than in the config, because +set is applied
# before subsystems start. cl_renderer in particular is CVAR_LATCH: written from
# a config that runs after startup it would take effect on the next vid_restart,
# so two of the three scenarios would quietly measure the OpenGL renderer.
STARTUP_CVARS = [
    ("cl_renderer", "rd-vulkan"),
    # Written continuously and flushed per line, so it survives an error or a
    # crash. condump only writes what is still in the console ring buffer, and
    # only if the command after it ever runs - which is how a map scenario that
    # took six seconds of real work managed to leave no evidence at all.
    ("logfile", "2"),
    ("com_maxfps", "250"),
    ("r_fullscreen", "0"),
    ("r_mode", "4"),
    # Sound stays ON. Turning it off to keep audio init out of the timings made
    # the client die with an access violation the moment cgame started loading -
    # three runs out of three, and the triage pass proved it was this and not
    # the renderer. A couple of hundred milliseconds of audio init in every
    # number is the cheaper problem.
    ("s_initsound", "1"),
    ("in_joystick", "0"),
]


def build_cfg(lines: list[str], mapname: str) -> str:
    header = ["// Generated by tools/verify/verify.py. Safe to delete."]
    body = [line.replace("{map}", mapname) for line in lines]
    return "\n".join(header + body) + "\n"


# --------------------------------------------------------------------------
# console output parsing
# --------------------------------------------------------------------------

PATTERNS = {
    "lighting_path": re.compile(r"JKX: lighting path = (\w+)"),
    "max_bound_sets": re.compile(r"maxBoundDescriptorSets (\d+)"),
    "vendor": re.compile(r"VK_VENDOR:\s*(.+)"),
    "renderer": re.compile(r"VK_RENDERER:\s*(.+)"),
    "version": re.compile(r"VK_VERSION:\s*(.+)"),
    "pipeline_handles": re.compile(r"pipeline handles:\s*(\d+)"),
    "pipeline_defs": re.compile(r"pipeline descriptors:\s*(\d+)"),
    "cache_reused": re.compile(r"reusing pipeline cache \((\d+) KiB\)"),
    "cache_saved": re.compile(r"saved pipeline cache \((\d+) KiB\)"),
    "cache_rebuild": re.compile(r"pipeline cache is for a different device"),
    "cubemaps": re.compile(r"(\d+) cubemaps"),
    # Did the map actually come up? A map that fails to load costs almost
    # nothing, and 0.4 s of "map load" is a failure that looks like a result.
    "server_init": re.compile(r"-+ Server Initialization -+"),
    "map_name": re.compile(r"Server:\s*(\S+)"),
    "map_failed": re.compile(r"Couldn't load (\S+)"),
    "ibl": re.compile(r"Loaded Enviroment JSON: (\S+)"),
    # A portable build ignores fs_homepath entirely and writes next to itself,
    # so forcing a homepath does nothing and the run does touch the game folder.
    "portable": re.compile(r"fs_portable enabled"),
    # What the map load actually consists of.
    "cgame_init": re.compile(r"CL_InitCGame:\s*([\d.]+) seconds"),
    "vbo_surfaces": re.compile(r"found (\d+) VBO surfaces"),
    "image_chunks": re.compile(r"image chunks: (\d+)"),
    "chunk_size": re.compile(r"image chunk\[0\] items: \d+ size: (\d+)kbytes"),
    "vbo_buffers": re.compile(r"VBO buffers: (\d+)"),
}

VALIDATION = re.compile(r"(VUID-[A-Za-z0-9_\-]+|validation layer|Validation Error)")


def parse_console(text: str) -> dict:
    out: dict = {}
    for key, pattern in PATTERNS.items():
        match = pattern.search(text)
        if match:
            out[key] = match.group(1).strip() if match.groups() else True
    out["validation_hits"] = sorted(set(VALIDATION.findall(text)))
    return out


# --------------------------------------------------------------------------
# finding the games
# --------------------------------------------------------------------------

# Steam app ids and the directory each one installs into.
STEAM_GAMES = [
    (6020, "Jedi Academy", "Star Wars Jedi Knight - Jedi Academy", "jka"),
    (6030, "Jedi Outcast", "Star Wars Jedi Knight II - Jedi Outcast", "jk2"),
]

# Folder names Steam has used for these two over the years. Both games have been
# renamed more than once, so match on any of them rather than on one.
INSTALL_DIR_ALIASES = {
    "jka": [
        "Jedi Academy",
        "Star Wars Jedi Knight - Jedi Academy",
        "STAR WARS Jedi Knight - Jedi Academy",
        "Star Wars Jedi Knight Jedi Academy",
    ],
    "jk2": [
        "Jedi Outcast",
        "Star Wars Jedi Knight II - Jedi Outcast",
        "STAR WARS Jedi Knight II - Jedi Outcast",
        "Star Wars Jedi Knight II Jedi Outcast",
    ],
}


def steam_roots() -> list[Path]:
    """Every Steam installation this machine might have."""
    roots: list[Path] = []
    system = platform.system()

    if system == "Windows":
        try:
            import winreg  # noqa: PLC0415 - Windows only

            for hive, key in ((winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam"),
                              (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam")):
                try:
                    with winreg.OpenKey(hive, key) as handle:
                        for value in ("SteamPath", "InstallPath"):
                            try:
                                roots.append(Path(winreg.QueryValueEx(handle, value)[0]))
                            except OSError:
                                pass
                except OSError:
                    pass
        except ImportError:
            pass
        roots += [
            Path(r"C:\Program Files (x86)\Steam"),
            Path(r"C:\Program Files\Steam"),
        ]
    else:
        home = Path.home()
        roots += [
            home / ".steam" / "steam",
            home / ".steam" / "root",
            home / ".local" / "share" / "Steam",
            # flatpak keeps its own copy
            home / ".var" / "app" / "com.valvesoftware.Steam" / "data" / "Steam",
        ]

    seen: list[Path] = []
    for root in roots:
        try:
            resolved = root.expanduser().resolve()
        except OSError:
            continue
        if resolved.is_dir() and resolved not in seen:
            seen.append(resolved)
    return seen


# A quoted absolute path: either a drive letter followed by one or two
# backslashes, or a leading slash. Steam escapes backslashes in the file, so
# "D:\\Games" on disk is a single path with doubled separators, but not every
# writer of this file bothers, hence the optional second one.
VDF_PATH = re.compile(r'"((?:[A-Za-z]:\\\\?|/)[^"]*)"')


def vdf_paths(text: str) -> list[str]:
    """Absolute paths mentioned in a libraryfolders.vdf, unescaped.

    Valve's key-value format has changed shape twice. Rather than parse it
    properly, pull out every quoted absolute path; that survives all three
    versions, and the caller drops the ones that are not directories.
    """
    return [m.replace("\\\\", "\\") for m in VDF_PATH.findall(text)]


def steam_libraries(root: Path) -> list[Path]:
    """Every library folder, including the ones on other drives."""
    libraries = [root]

    for name in ("steamapps", "SteamApps"):
        vdf = root / name / "libraryfolders.vdf"
        if not vdf.is_file():
            continue
        try:
            text = vdf.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for candidate in vdf_paths(text):
            path = Path(candidate)
            if path.is_dir() and path not in libraries:
                libraries.append(path)

    return libraries


# Installs seen in the wild that are worth trying before scanning anything.
# Cheap to check and skipped when absent, so an entry that is wrong for one
# machine costs nothing on another.
KNOWN_INSTALLS = [
    ("jka", r"D:\Games\Steam\steamapps\common\Jedi Academy"),
]


def game_root(install: Path) -> Path | None:
    """The directory the engine should treat as fs_basepath.

    Two layouts exist. The retail and older Steam depots put everything under
    GameData; the current Steam depot of Jedi Academy has base/ directly in the
    install folder, with JediAcademy.exe next to it. Both are valid, so pick
    whichever one actually holds base/ rather than insisting on GameData.
    """
    if (install / "GameData" / "base").is_dir():
        return install / "GameData"
    if (install / "base").is_dir():
        return install
    return None


def find_steam_games() -> list[dict]:
    """Every install we know how to drive, as a list of game roots."""
    found: list[dict] = []
    seen: set[Path] = set()
    labels = {key: label for _appid, label, _dir, key in STEAM_GAMES}
    appids = {key: appid for appid, _label, _dir, key in STEAM_GAMES}

    def add(key: str, install: Path, library: Path | None) -> None:
        root = game_root(install)
        if root is None:
            return  # present in Steam but never fully installed
        resolved = root.resolve()
        if resolved in seen:
            return
        seen.add(resolved)
        found.append({
            "key": key,
            "label": labels[key],
            "appid": appids[key],
            "path": resolved,
            "library": library,
        })

    for key, hint in KNOWN_INSTALLS:
        install = Path(hint)
        if install.is_dir():
            add(key, install, None)

    for root in steam_roots():
        for library in steam_libraries(root):
            for name in ("steamapps", "SteamApps"):
                common = library / name / "common"
                if not common.is_dir():
                    continue

                for _appid, _label, _default_dir, key in STEAM_GAMES:
                    for alias in INSTALL_DIR_ALIASES[key]:
                        install = common / alias
                        if install.is_dir():
                            add(key, install, library)
    return found


def not_found_message() -> str:
    """Says which of the two things went wrong, because the fix differs."""
    roots = steam_roots()
    if not roots:
        return ("No Steam installation found on this machine.\n"
                "Pass --game with the path to the game's install folder.")
    return ("Steam is installed, but neither Jedi Academy nor Jedi Outcast is.\n"
            "Searched: " + ", ".join(str(r) for r in roots) + "\n"
            "If the game lives elsewhere, pass --game with its install folder.")


def resolve_game(args) -> Path:
    """--game if given, otherwise whatever Steam has installed."""
    if args.game:
        path = Path(args.game).expanduser().resolve()
        if not path.is_dir():
            raise SystemExit(f"game directory not found: {path}")
        # Accept either the install folder or the GameData inside it.
        root = game_root(path)
        if root is None:
            raise SystemExit(f"no base/ directory under {path} or {path / 'GameData'}; "
                             "is this the game's install folder?")
        return root.resolve()

    games = find_steam_games()
    if not games:
        raise SystemExit(not_found_message())

    wanted = args.title
    if wanted:
        matches = [g for g in games if g["key"] == wanted]
        if not matches:
            raise SystemExit(f"{wanted} is not installed; found: "
                             + ", ".join(g["key"] for g in games))
    else:
        # Default to Jedi Academy: it is what the renderer was written against.
        matches = [g for g in games if g["key"] == "jka"] or games

    chosen = matches[0]
    print(f"found    : {chosen['label']} (app {chosen['appid']})")
    if len(games) > 1:
        others = ", ".join(f"{g['key']}" for g in games if g is not chosen)
        print(f"           also installed: {others} (use --title to switch)")
    return chosen["path"]


# --------------------------------------------------------------------------
# running
# --------------------------------------------------------------------------

# Engines that ship a Vulkan renderer, best first. JKX is what this tree will
# build; EternalJK is the fork the renderer came from, and until phase 2 lands
# it is the only thing that can run it.
#
# OpenJK is deliberately absent. It is a perfectly good engine and it is often
# already installed next to the retail files, but it has no Vulkan renderer at
# all - only rd-vanilla and rd-rend2, both OpenGL. Accepting it meant the tool
# would launch something, everything would appear to work, and the report would
# describe a renderer this project is not working on.
ENGINE_NAMES = [
    "jkx_ja.x86_64.exe", "jkx_ja.x86_64", "jkx_ja.exe",
    "jkx_jo.x86_64.exe", "jkx_jo.x86_64", "jkx_jo.exe",
    "eternaljk.x86_64.exe", "eternaljk.x86.exe",
    "eternaljk.x86_64", "eternaljk.x86",
]

# Engines with no Vulkan support that are commonly sitting in the same folder.
# Naming them is the difference between "install the right build" and "something
# is wrong with your install".
GL_ONLY_NAMES = [
    "openjk.x86_64.exe", "openjk.x86.exe", "openjk.x86_64", "openjk.x86",
    "openjk_sp.x86_64.exe", "openjk_sp.x86.exe", "openjk_sp.x86_64",
    "japp.x86_64.exe", "japp.x86.exe",
    "JediAcademy.exe", "jasp.exe", "jamp.exe",
    "JediOutcast.exe", "jk2sp.exe", "jk2mp.exe",
]

# The renderer itself. The engine loads it by name at startup, so its absence is
# not an error the engine reports loudly - it falls back to OpenGL and carries
# on, which is exactly the failure this tool exists to catch.
RENDERER_STEMS = ["rd-vulkan_x86_64", "rd-vulkan_x86"]
RENDERER_SUFFIXES = [".dll", ".so", ".dylib"]

ENGINE_DOWNLOAD = {
    "Windows": ("EternalJK-windows-x86_64.zip",
                "https://github.com/JKSunny/EternalJK/releases/download/"
                "latest-pbr/EternalJK-windows-x86_64.zip"),
    "Linux": ("EternalJK-linux-x86_64.tar.gz",
              "https://github.com/JKSunny/EternalJK/releases/download/"
              "latest-pbr/EternalJK-linux-x86_64.tar.gz"),
}


def renderer_libraries(game_dir: Path, binary: Path) -> list[Path]:
    """Every rd-vulkan the engine could load, anywhere it looks for one."""
    found: list[Path] = []
    for directory in {game_dir, binary.parent}:
        if not directory.is_dir():
            continue
        for stem in RENDERER_STEMS:
            for suffix in RENDERER_SUFFIXES:
                candidate = directory / (stem + suffix)
                if candidate.is_file():
                    found.append(candidate)
    return found


def missing_renderer_message(game_dir: Path, binary: Path) -> str:
    asset, url = ENGINE_DOWNLOAD.get(platform.system(), ("", ""))
    lines = [
        f"{binary.name} is there, but no rd-vulkan library is next to it.",
        "",
        "The engine loads the renderer by name at startup. When it is missing the",
        "engine does not fail: it falls back to OpenGL and runs, so the whole",
        "measurement would be of the wrong renderer.",
        "",
        f"Expected one of: {', '.join(s + RENDERER_SUFFIXES[0] for s in RENDERER_STEMS)}",
        f"next to {binary.parent}",
    ]
    if url:
        lines += [
            "",
            "A build with the Vulkan renderer is published by the fork this project",
            "was started from:",
            f"  {url}",
            "",
            "Unpack it over the game directory, or let this script do it:",
            "  verify --fetch-engine",
        ]
    return "\n".join(lines)


def find_binary(game_dir: Path, explicit: str | None) -> Path:
    if explicit:
        candidate = Path(explicit).expanduser()
        if not candidate.is_file():
            raise SystemExit(f"binary not found: {candidate}")
        return candidate

    # The engine has to sit next to base/, but a build directory in this repo is
    # worth checking too: it saves a copy for anyone measuring a fresh build.
    search = [game_dir, game_dir.parent]
    repo = Path(__file__).resolve().parents[2]
    search += [repo / "build", repo / "build" / "Release", repo / "build-san"]

    for directory in search:
        for name in ENGINE_NAMES:
            candidate = directory / name
            if candidate.is_file():
                return candidate

    others = [name for name in GL_ONLY_NAMES if (game_dir / name).is_file()]
    if others:
        asset, url = ENGINE_DOWNLOAD.get(platform.system(), ("", ""))
        message = [
            f"{game_dir} has playable engines ({', '.join(others)}), but none of them",
            "has a Vulkan renderer: the retail executables are the 2003 build, and OpenJK",
            "ships only the two OpenGL renderers. There is nothing here to measure.",
        ]
        if url:
            message += [
                "",
                "The fork this project was started from publishes a build with rd-vulkan:",
                f"  {url}",
                "",
                "Unpack it over the game directory, or let this script do it:",
                "  verify --fetch-engine",
            ]
        message += ["", "Alternatively pass --binary with the path to a build of your own."]
        raise SystemExit("\n".join(message))

    raise SystemExit(
        f"no engine binary found in {game_dir}.\n"
        f"Looked for: {', '.join(ENGINE_NAMES)}\n"
        "Pass --binary to point at it explicitly."
    )


# The gamecode the client cannot start without. On Windows these ship inside a
# pk3 rather than loose, and with sv_pure set - which is the multiplayer default
# - the engine will ONLY take them from a pk3: Sys_DLLNeedsUnpacking() returns
# true, and a failed unpack is returned as failure without falling back to the
# filesystem. So "the dll is somewhere on disk" is not the question; "is it in a
# game directory the filesystem searches" is.
GAMECODE_STEMS = ["ui", "cgame", "jampgame"]


def gamecode_dirs(game_dir: Path) -> list[Path]:
    """Where the engine looks for gamecode, in its own order."""
    return [game_dir / "EternalJK", game_dir / "base", game_dir]


def find_gamecode(game_dir: Path, binary: Path) -> dict:
    """Locate each gamecode module, loose or inside a pk3."""
    import zipfile  # noqa: PLC0415 - only needed here

    arch = "x86_64" if "x86_64" in binary.name else "x86"
    suffix = ".dll" if platform.system() == "Windows" else ".so"

    found: dict = {}
    for stem in GAMECODE_STEMS:
        name = f"{stem}{arch}{suffix}"
        found[name] = None
        for directory in gamecode_dirs(game_dir):
            if not directory.is_dir():
                continue
            loose = directory / name
            if loose.is_file():
                found[name] = loose
                break
            for pak in sorted(directory.glob("*.pk3")):
                try:
                    with zipfile.ZipFile(pak) as zf:
                        if any(Path(entry).name.lower() == name.lower() for entry in zf.namelist()):
                            found[name] = pak
                            break
                except (OSError, zipfile.BadZipFile):
                    continue
            if found[name] is not None:
                break
    return found


def missing_gamecode_message(game_dir: Path, binary: Path, found: dict) -> str:
    missing = [name for name, where in found.items() if where is None]
    return "\n".join([
        f"the gamecode is not reachable from {game_dir}",
        "",
        "Missing: " + ", ".join(missing),
        "Looked in: " + ", ".join(str(d.name) or "." for d in gamecode_dirs(game_dir)),
        "",
        "The published build ships these inside EternalJK/bins_*.pk3, so the whole",
        "archive has to be unpacked into the same folder as base/, not just the",
        "executable and the renderer. Without them the engine reaches the main menu",
        "and then dies with 'VM_CreateLegacy on ui failed'.",
        "",
        "Run with --doctor to see exactly what is where.",
    ])


def doctor(game_dir: Path, args) -> int:
    """Print what is installed and whether it adds up to a runnable engine."""
    print(f"game root : {game_dir}")
    print(f"layout    : {'GameData' if game_dir.name == 'GameData' else 'flat'}")
    print()

    for directory in [game_dir] + gamecode_dirs(game_dir)[:2]:
        if not directory.is_dir():
            print(f"{directory}  (missing)")
            continue
        entries = sorted(p.name for p in directory.iterdir() if p.is_file())
        interesting = [e for e in entries
                       if e.endswith((".exe", ".dll", ".so", ".pk3"))
                       or e in ENGINE_NAMES]
        print(f"{directory}")
        if not interesting:
            print("    (nothing relevant)")
        for name in interesting[:40]:
            print(f"    {name}")
        if len(interesting) > 40:
            print(f"    ... and {len(interesting) - 40} more")
        print()

    try:
        binary = find_binary(game_dir, args.binary)
    except SystemExit as exc:
        print(str(exc))
        return 1

    print(f"engine    : {binary}")
    libs = renderer_libraries(game_dir, binary)
    print(f"renderer  : {libs[0] if libs else 'MISSING - would silently fall back to OpenGL'}")

    for name, where in find_gamecode(game_dir, binary).items():
        print(f"{name:20s}: {where if where else 'MISSING'}")
    return 0


def fetch_engine(game_dir: Path) -> None:
    """Download and unpack a build that has the Vulkan renderer.

    This installs someone else's binaries into the game directory, so it only
    ever happens because --fetch-engine was passed. The URL is printed first;
    nothing is downloaded silently.
    """
    import tarfile      # noqa: PLC0415 - only needed on this path
    import urllib.request  # noqa: PLC0415
    import zipfile      # noqa: PLC0415

    entry = ENGINE_DOWNLOAD.get(platform.system())
    if entry is None:
        raise SystemExit(f"no published build for {platform.system()}; build one and use --binary")
    asset, url = entry

    print(f"downloading {url}")
    archive = game_dir / asset
    try:
        with urllib.request.urlopen(url) as response, archive.open("wb") as out:  # noqa: S310
            shutil.copyfileobj(response, out)
    except OSError as exc:
        raise SystemExit(f"download failed: {exc}\nDownload it by hand and unpack into {game_dir}")

    print(f"unpacking into {game_dir}")
    if asset.endswith(".zip"):
        with zipfile.ZipFile(archive) as zf:
            zf.extractall(game_dir)
    else:
        with tarfile.open(archive) as tf:
            tf.extractall(game_dir)
    archive.unlink()

    # The published archives are the install tree, so the engine may have landed
    # one level down rather than beside base/. Move it up, merging directories
    # rather than skipping them: EternalJK/ holds the gamecode, and an existing
    # EternalJK/ from an earlier attempt would otherwise leave it behind.
    for stem in RENDERER_STEMS:
        for suffix in RENDERER_SUFFIXES:
            for found in game_dir.glob(f"*/{stem}{suffix}"):
                print(f"moving {found.parent.name}/* up next to base/")
                merge_into(found.parent, game_dir)
                return


def merge_into(source: Path, destination: Path) -> None:
    """Move everything from source into destination, overwriting file by file."""
    for item in list(source.iterdir()):
        target = destination / item.name
        if item.is_dir():
            target.mkdir(exist_ok=True)
            merge_into(item, target)
            continue
        if target.exists():
            target.unlink()
        shutil.move(str(item), str(target))


def find_all(roots: list[Path], name: str) -> list[Path]:
    """Every copy of a file the engine may have written, at either depth."""
    hits: list[Path] = []
    for root in roots:
        if not root.is_dir():
            continue
        direct = root / name
        if direct.is_file():
            hits.append(direct)
        hits += [p for p in root.glob(f"*/{name}") if p.is_file()]
    return hits


# One variable at a time, re-run only when something crashed. An access
# violation with no stack is not much to go on, but "it crashes on Vulkan and
# not on OpenGL" or "it crashes with sound off and not with sound on" narrows it
# to a subsystem in about half a minute, without a debugger.
TRIAGE = [
    ("sound enabled", [("s_initsound", "1")], None),
    ("OpenGL renderer", [("cl_renderer", "rd-eternaljk")], None),
    ("a different map", [], "mp/ffa1"),
    ("the driver's own video mode", [("r_mode", "-1")], None),
]


def run_scenario(binary: Path, game_dir: Path, cfg_dir: Path, home: Path, name: str,
                 mapname: str, timeout: int,
                 overrides: list[tuple[str, str]] | None = None,
                 tag: str = "") -> tuple[float, int]:
    scenario = SCENARIOS[name]
    cfg_name = f"jkx_verify_{name}.cfg"
    (cfg_dir / cfg_name).write_text(build_cfg(scenario["cfg"], mapname), encoding="ascii")

    # One log per scenario: the engine always writes the same name, so the
    # previous run's copy has to be out of the way before this one starts.
    for stale in find_all([home, game_dir], "qconsole.log"):
        stale.unlink()

    # fs_homepath is forced so the dumps, the pipeline cache and the configs this
    # run writes all land somewhere known and disposable. Without it the engine
    # picks a per-platform directory under a mod name that depends on the build,
    # and the run also edits the player's own configuration.
    cvars = dict(STARTUP_CVARS)
    cvars.update(dict(overrides or []))

    cmd = [
        str(binary),
        "+set", "fs_basepath", str(game_dir),
        "+set", "fs_homepath", str(home),
    ]
    for cvar, value in cvars.items():
        cmd += ["+set", cvar, value]
    cmd += ["+exec", cfg_name]

    start = time.perf_counter()
    try:
        proc = subprocess.run(cmd, cwd=str(game_dir), capture_output=True, timeout=timeout)
        code = proc.returncode
    except subprocess.TimeoutExpired:
        return (float(timeout), -1)

    # Kept because a scenario that produces no condump leaves nothing else to
    # look at, and "it did not work" is not a diagnosis.
    stream = (proc.stdout or b"") + (proc.stderr or b"")
    if stream.strip():
        (home / f"jkx_{name}_stdout.txt").write_bytes(stream)

    for log in find_all([home, game_dir], "qconsole.log"):
        shutil.copy2(log, home / f"jkx_{name}{tag}.log")
        log.unlink()

    return (time.perf_counter() - start, code)


def find_dump(roots: list[Path], name: str) -> Path | None:
    """Locate a condump wherever the engine decided to put it.

    condump writes into fs_homepath/<fs_gamedir>, and fs_gamedir is the mod
    directory: "base" for OpenJK, "EternalJK" for the fork, something else for a
    build with its own basegame. A portable build ignores fs_homepath entirely
    and writes next to the executable. Rather than encode all of that, look for
    the file and take the newest match.
    """
    best: Path | None = None
    for root in roots:
        if not root.is_dir():
            continue
        for candidate in root.glob(f"*/{name}"):
            if best is None or candidate.stat().st_mtime > best.stat().st_mtime:
                best = candidate
        direct = root / name
        if direct.is_file() and (best is None or direct.stat().st_mtime > best.stat().st_mtime):
            best = direct
    return best


def read_dump(roots: list[Path], name: str) -> str:
    path = find_dump(roots, name)
    if path is None:
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


# --------------------------------------------------------------------------
# report
# --------------------------------------------------------------------------

def fmt(seconds: float | None) -> str:
    return "-" if seconds is None else f"{seconds:.1f} s"


def write_report(path: Path, results: dict, info: dict, args, stages: dict | None = None,
                 triage: list | None = None) -> None:
    stages = stages or {}
    triage = triage or []
    startup = results.get("startup", {}).get("median")
    restart = results.get("vid_restart", {}).get("median")
    mapload = results.get("map", {}).get("median")

    restart_only = (restart - startup) if (restart and startup) else None
    map_only = (mapload - startup) if (mapload and startup) else None

    lines = [
        "# JKX: verification run",
        "",
        f"- date: {time.strftime('%Y-%m-%d %H:%M')}",
        f"- host: {platform.system()} {platform.machine()}",
        f"- binary: `{args.binary or 'auto-detected'}`",
        f"- game: `{args.resolved_game}`",
        f"- map: `{args.map}`",
        f"- runs per scenario: {args.runs}",
        "",
        "## Device",
        "",
    ]
    for key, label in (("vendor", "vendor"), ("renderer", "renderer"), ("version", "Vulkan")):
        lines.append(f"- {label}: {info.get(key, '?')}")

    if not info.get("vendor"):
        lines += [
            "",
            "**The Vulkan renderer did not report itself.** `vkinfo` prints VK_VENDOR from",
            "rd-vulkan, so its absence means rd-vulkan was not the renderer that ran, and",
            "nothing below describes the code this project is working on. The usual cause is",
            "that rd-vulkan_" + ("x86_64.dll" if platform.system() == "Windows" else "x86_64.so")
            + " is not next to the executable, so the engine fell back",
            "to the OpenGL renderer.",
        ]

    path_name = info.get("lighting_path")
    sets = info.get("max_bound_sets")
    lines += ["", "## Lighting path", ""]
    if path_name is None:
        lines.append("Not reported. This build predates the diagnostic; rebuild the renderer from")
        lines.append("this tree, or treat every number below as being of an unknown path.")
    elif path_name == "PBR":
        lines.append(f"**PBR is active** (maxBoundDescriptorSets {sets}). Measurements below are of the PBR path.")
    else:
        lines.append(f"**PBR is OFF, fastlight is running** (maxBoundDescriptorSets {sets}).")
        lines.append("")
        lines.append("Everything below therefore measures the non-PBR path. If this device reports")
        lines.append("fewer than 11 descriptor sets, that is the limit bindless removes; it is why")
        lines.append("bindless is on the critical path rather than an optimisation.")

    lines += [
        "",
        "## Timings",
        "",
        "| what | median | note |",
        "|---|---|---|",
        f"| startup to main menu | {fmt(startup)} | cost a persistent pipeline cache removes |",
        f"| vid_restart alone | {fmt(restart_only)} | the same cost, and the one players hit |",
        f"| map load alone | {fmt(map_only)} | dominated by the IBL bake |",
        "",
    ]

    for name, data in results.items():
        if data.get("samples"):
            spread = ", ".join(f"{s:.1f}" for s in data["samples"])
            lines.append(f"- {SCENARIOS[name]['label']}: {spread} s")
    lines.append("")

    # A non-zero exit is a crash, and a crashed run is not a measurement.
    crashed = {name: data["exit_codes"] for name, data in results.items()
               if any(code != 0 for code in data.get("exit_codes", []))}
    if crashed:
        lines.append("**Some runs did not exit cleanly:**")
        lines.append("")
        for name, codes in crashed.items():
            lines.append(f"- {SCENARIOS[name]['label']}: exit codes {codes}")
        lines.append("")
        lines.append("The engine is asked to quit at the end of every scenario, so anything other")
        lines.append("than zero means it died instead. Those timings measure a crash.")
        lines.append("")
        if any(code == 3221225477 for codes in crashed.values() for code in codes):
            lines.append("3221225477 is 0xC0000005, an access violation: the process was killed by")
            lines.append("the operating system, with no error dialog and no crash log, because")
            lines.append("nothing in the engine gets to run after that.")
            lines.append("")

    if triage:
        lines += ["## Crash triage", "",
                  "The crashing scenario re-run with one thing changed at a time:", "",
                  "| variant | result |", "|---|---|"]
        for entry in triage:
            verdict = "clean" if entry["exit"] == 0 else f"crashed (exit {entry['exit']})"
            lines.append(f"| {entry['variant']} | {verdict} |")
        clean = [e["variant"] for e in triage if e["exit"] == 0]
        lines.append("")
        if clean:
            lines.append("Changing " + ", ".join(f"**{c}**" for c in clean) + " avoids the crash,")
            lines.append("which is where to start looking.")
        else:
            lines.append("None of the variants avoided it, so it is not the renderer choice, the")
            lines.append("sound setting, the video mode or that particular map on their own.")
        lines.append("")

    if info.get("portable"):
        lines.append("This is a portable build: it ignores fs_homepath and writes next to itself,")
        lines.append("so the run wrote its configs and logs into the game folder rather than into")
        lines.append("the disposable one this tool passes.")
        lines.append("")

    lines += ["## Pipeline cache", ""]
    if info.get("cache_reused"):
        lines.append(f"Reused a cache of {info['cache_reused']} KiB, so this build has the persistent cache.")
    elif info.get("cache_saved"):
        lines.append(f"Saved {info['cache_saved']} KiB. Run again: the second startup should be faster.")
    elif info.get("cache_rebuild"):
        lines.append("Cache was rejected as belonging to another device or driver, and rebuilt.")
    else:
        lines.append("No cache messages. Either this build predates the persistent cache, or the")
        lines.append("homepath is not writable. The startup number above is then the uncached cost.")

    # Did the map come up at all? A map that failed to load costs almost
    # nothing, so the "map load" row above would be a failure wearing the
    # costume of a result.
    map_stage = stages.get("map", {})
    if results.get("map"):
        lines += ["", "## Map", ""]
        if map_stage.get("map_failed"):
            lines.append(f"**{map_stage['map_failed']} failed to load.** The map row above measures")
            lines.append("the failure, not a load. Pass --map with a map this install has.")
        elif map_stage.get("server_init"):
            loaded = map_stage.get("map_name", args.map)
            lines.append(f"`{loaded}` loaded.")
            if map_stage.get("cgame_init"):
                lines.append(f"Client game initialisation alone took "
                             f"{map_stage['cgame_init']} s of it.")
            if map_stage.get("vbo_surfaces"):
                lines.append(f"The map is {map_stage['vbo_surfaces']} VBO surfaces.")
            if map_stage.get("ibl"):
                lines.append(f"IBL probes were baked from `{map_stage['ibl']}`, so the load cost")
                lines.append("includes the bake.")
            else:
                lines.append("No cubemap definition was found for it, so **no IBL bake happened**:")
                lines.append("the load cost above is geometry and textures only. The bake only")
                lines.append("applies to maps shipping cubemaps/<map>/env.json, which retail maps")
                lines.append("do not - that is worth knowing before optimising it.")
        elif map_stage.get("log"):
            lines.append("**The map did not start.** The console log is written as it happens and")
            lines.append("survives errors, and it contains no server initialisation, so this is not")
            lines.append("a gap in the evidence. Whatever the seconds above were spent on, it was")
            lines.append("not a loaded map. The log is in dumps/ next to this report.")
        elif map_stage.get("dump") is None:
            lines.append("**Nothing was written for this scenario** - no log, no dump. The timing")
            lines.append("above is still wall clock. Look in dumps/ next to this report.")
        else:
            lines.append("**No server initialisation appeared in the dump.** The dump only holds")
            lines.append("what was still in the console buffer, so this is weaker evidence than the")
            lines.append("timing: a load that took seconds happened, whatever the dump says.")

    lines += ["", "## Pipelines", ""]
    lines.append("| stage | definitions | created objects |")
    lines.append("|---|---|---|")
    for name in SCENARIOS:
        stage = stages.get(name) or {}
        if stage.get("pipeline_defs") or stage.get("pipeline_handles"):
            lines.append(f"| {SCENARIOS[name]['label']} | {stage.get('pipeline_defs', '?')} "
                         f"| {stage.get('pipeline_handles', '?')} |")
    if not any((stages.get(n) or {}).get("pipeline_defs") for n in SCENARIOS):
        lines.append(f"| overall | {info.get('pipeline_defs', '?')} "
                     f"| {info.get('pipeline_handles', '?')} |")
    lines.append("")
    chunks = (stages.get("map") or {}).get("image_chunks") \
        or (stages.get("startup") or {}).get("image_chunks")
    size = (stages.get("map") or {}).get("chunk_size") \
        or (stages.get("startup") or {}).get("chunk_size")
    if chunks and size:
        total = int(chunks) * int(size) // 1024
        lines += ["", "## Texture memory", "",
                  f"{chunks} chunks of {int(size) // 1024} MB, {total} MB in total.",
                  "",
                  "Upstream allocates textures out of fixed chunks and never returns any of it",
                  "until the map changes, so this is a high-water mark that stays. Replacing that",
                  "with VMA is one of the phase 1 changes; this is the number to compare against."]

    lines.append("")
    lines.append("Pipelines are created on demand, so the menu number is a fraction of what a")
    lines.append("loaded map needs. Dynamic rendering plus extended dynamic state should cut the")
    lines.append("object count by roughly an order of magnitude; this is the before number.")

    hits = info.get("validation_hits") or []
    lines += ["", "## Validation", ""]
    if hits:
        lines.append(f"{len(hits)} distinct message(s):")
        lines.append("")
        for h in hits[:20]:
            lines.append(f"- `{h}`")
    else:
        lines.append("None seen. Note that validation layers are only active in a debug build,")
        lines.append("so silence here is not proof of cleanliness.")

    lines += [
        "",
        "## Still needs a human",
        "",
        "- FPS against rd-vanilla on the same scene (`/cl_renderer rd-vanilla; vid_restart`)",
        "- one RenderDoc capture: draw call count, pipeline switches, where time goes",
        "- hitching when new materials first appear, which is the lazy pipeline creation",
        "",
        "Attach this file and the console dumps from the homepath.",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", default=None,
                        help="the game's install folder (auto-detected from Steam when omitted)")
    parser.add_argument("--title", choices=["jka", "jk2"], default=None,
                        help="which game, when both are installed (default: jka)")
    parser.add_argument("--list", action="store_true", help="list detected installs and exit")
    parser.add_argument("--no-triage", action="store_true",
                        help="do not re-run variants when a scenario crashes")
    parser.add_argument("--doctor", action="store_true",
                        help="print what is installed and what is missing, then exit")
    parser.add_argument("--fetch-engine", action="store_true",
                        help="download a published build that has the Vulkan renderer")
    parser.add_argument("--binary", default=None, help="engine binary (auto-detected by default)")
    parser.add_argument("--map", default="mp/ffa3", help="map to load")
    parser.add_argument("--runs", type=int, default=3, help="runs per scenario (median is reported)")
    parser.add_argument("--timeout", type=int, default=300, help="seconds before a run is abandoned")
    parser.add_argument("--home", default=None,
                        help="fs_homepath for the runs (default: run-home next to this script)")
    parser.add_argument("--out", default=None, help="report path")
    args = parser.parse_args(argv[1:])

    if args.list:
        games = find_steam_games()
        if not games:
            print(not_found_message())
            return 1
        for g in games:
            print(f"{g['key']:4s}  {g['label']:16s}  {g['path']}")
        return 0

    game_dir = resolve_game(args)
    args.resolved_game = game_dir

    if args.fetch_engine:
        fetch_engine(game_dir)

    if args.doctor:
        return doctor(game_dir, args)

    binary = find_binary(game_dir, args.binary)

    # Fail here rather than after nine launches: without the renderer the engine
    # runs on OpenGL and every number below would be of the wrong thing.
    if not renderer_libraries(game_dir, binary):
        raise SystemExit(missing_renderer_message(game_dir, binary))

    gamecode = find_gamecode(game_dir, binary)
    if any(where is None for where in gamecode.values()):
        raise SystemExit(missing_gamecode_message(game_dir, binary, gamecode))

    cfg_dir = game_dir / "base"
    if not cfg_dir.is_dir():
        raise SystemExit(f"no base/ directory under {game_dir}")

    home = Path(args.home).expanduser().resolve() if args.home \
        else Path(__file__).resolve().parent / "run-home"
    home.mkdir(parents=True, exist_ok=True)

    print(f"binary   : {binary}")
    print(f"game     : {game_dir}")
    print(f"homepath : {home}")
    print(f"map      : {args.map}")
    print()

    # Old dumps would otherwise be parsed as if they were from this run.
    dump_roots = [home, game_dir]
    for stale in DUMPS:
        found = find_dump(dump_roots, stale)
        if found is not None:
            found.unlink()

    results: dict = {}
    for name, scenario in SCENARIOS.items():
        samples = []
        codes = []
        print(f"{scenario['label']}: ", end="", flush=True)
        for run in range(args.runs):
            elapsed, code = run_scenario(binary, game_dir, cfg_dir, home, name, args.map,
                                         args.timeout)
            if code == -1:
                print(f"\n  run {run + 1} timed out after {args.timeout}s; skipping this scenario")
                samples = []
                break
            samples.append(elapsed)
            codes.append(code)
            print(f"{elapsed:.1f}s{'' if code == 0 else f' (exit {code})'} ", end="", flush=True)
        print()
        if samples:
            results[name] = {"samples": samples, "median": statistics.median(samples),
                             "exit_codes": codes}

    # A crash is not a measurement, so if one happened, spend half a minute
    # finding out which subsystem it belongs to before writing the report.
    triage: list[dict] = []
    crashed = [name for name, data in results.items()
               if any(code != 0 for code in data.get("exit_codes", []))]
    if crashed and not args.no_triage:
        target = "map" if "map" in crashed else crashed[0]
        print(f"\ntriage ({SCENARIOS[target]['label']} crashed):")
        for label, overrides, mapname in TRIAGE:
            elapsed, code = run_scenario(binary, game_dir, cfg_dir, home, target,
                                         mapname or args.map, args.timeout, overrides,
                                         tag="_" + label.split()[0].lower())
            triage.append({"variant": label, "exit": code, "seconds": elapsed,
                           "overrides": overrides, "map": mapname or args.map})
            print(f"  {label:28s} {elapsed:5.1f}s  {'clean' if code == 0 else f'exit {code}'}")

    # Parsed per scenario as well as together: "how many pipelines exist" means
    # something different at the main menu and in a loaded map, and merging the
    # dumps first would have silently reported the menu number for both.
    def read(path: Path | None) -> str:
        return path.read_text(encoding="utf-8", errors="replace") if path and path.is_file() else ""

    stages = {}
    texts = []
    evidence_files: list[Path] = []
    for scenario, dump_name in zip(SCENARIOS, DUMPS):
        # The per-scenario log is the whole console, written as it happened; the
        # condumps are a subset and may be missing entirely. Read everything and
        # let the log dominate.
        log = home / f"jkx_{scenario}.log"
        dumps = [find_dump(dump_roots, dump_name)]
        if scenario == "map":
            dumps += [find_dump(dump_roots, extra) for extra in EXTRA_DUMPS]

        text = "\n".join([read(log)] + [read(d) for d in dumps])
        stages[scenario] = parse_console(text)
        stages[scenario]["log"] = str(log) if log.is_file() else None
        stages[scenario]["dump"] = next((str(d) for d in dumps if d), None)
        texts.append(text)

        evidence_files += [d for d in dumps if d]
        if log.is_file():
            evidence_files.append(log)

    console = "\n".join(texts)
    if not console.strip():
        print("\nWarning: no console dumps were written. Timings are still valid, but the")
        print("device, lighting path and validation sections will be empty.")
    info = parse_console(console)

    out = Path(args.out) if args.out else Path(__file__).resolve().parent / "verification-report.md"
    write_report(out, results, info, args, stages, triage)

    evidence = out.parent / "dumps"
    evidence.mkdir(exist_ok=True)
    for path in evidence_files + sorted(home.glob("jkx_*_stdout.txt")):
        if path and path.is_file():
            shutil.copy2(path, evidence / path.name)

    print()
    print(f"report: {out}")
    print(f"dumps : {evidence}")
    if console.strip() and not info.get("vendor"):
        print("NOTE: rd-vulkan did not run - the engine fell back to OpenGL. See the report.")
    if info.get("lighting_path") == "fastlight":
        print("NOTE: PBR was OFF for this run; see the report.")
    if info.get("validation_hits"):
        print(f"NOTE: {len(info['validation_hits'])} validation message(s); see the report.")

    (out.with_suffix(".json")).write_text(
        json.dumps({"results": results, "info": info, "stages": stages, "triage": triage},
                   indent=2, default=str),
        encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
