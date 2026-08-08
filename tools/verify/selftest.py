#!/usr/bin/env python3
"""Self-test for verify.py.

verify.py is the one command the user is asked to run, and most of it can only
be exercised with a GPU and the retail game. The parts that can be tested
without either are the two that decide whether the run happens at all: finding
the Steam install, and reading the console dump afterwards. Both are also the
parts written blind against Windows from a Linux container, so they get tested
here and in CI rather than on the user's machine.

    python3 tools/verify/selftest.py
"""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import verify  # noqa: E402

FAILURES: list[str] = []


def check(condition: bool, what: str) -> None:
    print(("ok   " if condition else "FAIL ") + what)
    if not condition:
        FAILURES.append(what)


# --------------------------------------------------------------------------
# libraryfolders.vdf, all three shapes Valve has shipped
# --------------------------------------------------------------------------

def test_vdf() -> None:
    # 2013 shape: numbered keys straight in the root object.
    old = '"LibraryFolders"\n{\n\t"1"\t\t"D:\\\\SteamLibrary"\n}\n'
    check(verify.vdf_paths(old) == [r"D:\SteamLibrary"], "vdf: 2013 shape, backslashes unescaped")

    # 2021 shape: a nested object per library with a "path" key.
    new = ('"libraryfolders"\n{\n\t"0"\n\t{\n\t\t"path"\t\t"C:\\\\Program Files (x86)\\\\Steam"\n'
           '\t\t"label"\t\t""\n\t}\n\t"1"\n\t{\n\t\t"path"\t\t"E:\\\\Games\\\\Steam"\n\t}\n}\n')
    check(verify.vdf_paths(new) == [r"C:\Program Files (x86)\Steam", r"E:\Games\Steam"],
          "vdf: 2021 shape, both libraries")

    # Linux: forward slashes, nothing to unescape.
    linux = '"libraryfolders"\n{\n\t"0"\n\t{\n\t\t"path"\t\t"/home/u/.local/share/Steam"\n\t}\n}\n'
    check(verify.vdf_paths(linux) == ["/home/u/.local/share/Steam"], "vdf: Linux paths")

    # Single backslashes, which some third-party tools write.
    check(verify.vdf_paths(r'"path" "F:\Steam"') == [r"F:\Steam"], "vdf: single backslashes")

    # Everything that is not an absolute path stays out.
    noise = '"apps"\n{\n\t"6020"\t\t"12345"\n\t"label"\t\t""\n\t"contentid"\t"9"\n}\n'
    check(verify.vdf_paths(noise) == [], "vdf: non-paths ignored")


# --------------------------------------------------------------------------
# Steam detection
# --------------------------------------------------------------------------

def test_layout() -> None:
    tmp = Path(tempfile.mkdtemp())

    # Retail and the older Steam depots: everything under GameData.
    old = tmp / "old"
    (old / "GameData" / "base").mkdir(parents=True)
    check(verify.game_root(old) == old / "GameData", "GameData layout resolves to GameData")

    # Current Steam depot of Jedi Academy: base/ sits in the install folder and
    # JediAcademy.exe next to it, with no GameData at all.
    flat = tmp / "flat"
    (flat / "base").mkdir(parents=True)
    (flat / "JediAcademy.exe").write_text("", encoding="ascii")
    check(verify.game_root(flat) == flat, "flat layout resolves to the install folder")

    # Present in Steam but never finished downloading.
    empty = tmp / "empty"
    empty.mkdir()
    check(verify.game_root(empty) is None, "install without base/ is not a game root")


def test_detection() -> None:
    tmp = Path(tempfile.mkdtemp())
    home = tmp / "home"
    root = home / ".steam" / "steam"
    other = tmp / "SteamLibrary"

    (root / "steamapps" / "common").mkdir(parents=True)
    (other / "steamapps" / "common").mkdir(parents=True)

    # Jedi Academy in the main library under the current long folder name and
    # the current flat layout; Jedi Outcast on a second library under the name
    # Steam used to use, with the GameData layout. Both must be found.
    jka = root / "steamapps" / "common" / "STAR WARS Jedi Knight - Jedi Academy"
    jk2 = other / "steamapps" / "common" / "Jedi Outcast" / "GameData"
    (jka / "base").mkdir(parents=True)
    (jka / "JediAcademy.exe").write_text("", encoding="ascii")
    (jk2 / "base").mkdir(parents=True)

    (root / "steamapps" / "libraryfolders.vdf").write_text(
        '"libraryfolders"\n{\n'
        f'\t"0"\n\t{{\n\t\t"path"\t\t"{root}"\n\t}}\n'
        f'\t"1"\n\t{{\n\t\t"path"\t\t"{other}"\n\t}}\n'
        '\t"2"\n\t{\n\t\t"path"\t\t"D:\\\\Gone"\n\t}\n'
        '}\n', encoding="utf-8")

    verify.Path.home = staticmethod(lambda: home)

    libraries = [p.resolve() for p in verify.steam_libraries(root)]
    check(other.resolve() in libraries, "second library picked up from the vdf")
    check(all("Gone" not in str(p) for p in libraries), "library that no longer exists is dropped")
    check(root.resolve() in verify.steam_roots(), "root found under ~/.steam/steam")

    games = verify.find_steam_games()
    by_key = {g["key"]: g for g in games}
    check(sorted(by_key) == ["jk2", "jka"], f"both games detected (got {sorted(by_key)})")
    check(by_key.get("jka", {}).get("path") == jka.resolve(), "flat install resolves to itself")
    check(by_key.get("jk2", {}).get("path") == jk2.resolve(), "GameData install resolves to GameData")
    check(by_key.get("jka", {}).get("appid") == 6020 and by_key.get("jk2", {}).get("appid") == 6030,
          "app ids are right")
    check(len(games) == len({str(g["path"]) for g in games}), "no install reported twice")

    # A copy that exists but was never fully installed has no base/, and running
    # against it would fail deep inside the engine instead of here.
    (other / "steamapps" / "common" / "Jedi Academy").mkdir(parents=True)
    check(len(verify.find_steam_games()) == 2, "install without base/ is not offered")

    class Args:
        game = None
        title = None

    args = Args()
    check(verify.resolve_game(args) == jka.resolve(), "with both installed, jka is the default")
    args.title = "jk2"
    check(verify.resolve_game(args) == jk2.resolve(), "--title jk2 switches")
    args.title = None
    args.game = str(jka)
    check(verify.resolve_game(args) == jka.resolve(), "--game wins over detection")
    args.game = str(jk2.parent)
    check(verify.resolve_game(args) == jk2.resolve(),
          "--game accepts the install folder and finds GameData under it")


# --------------------------------------------------------------------------
# picking the executable
# --------------------------------------------------------------------------

def test_binary() -> None:
    tmp = Path(tempfile.mkdtemp())
    game = tmp / "Jedi Academy"
    (game / "base").mkdir(parents=True)

    # What a fresh Steam install actually contains. None of it can load
    # rd-vulkan, so the tool has to say so instead of launching the 2003 build
    # and reporting whatever came out.
    for name in ("JediAcademy.exe", "jasp.exe", "jamp.exe"):
        (game / name).write_text("", encoding="ascii")

    try:
        verify.find_binary(game, None)
        check(False, "retail-only install is refused")
    except SystemExit as exc:
        message = str(exc)
        check("cannot load rd-vulkan" in message, "retail-only install is refused")
        check("JediAcademy.exe" in message, "the message names what it did find")

    # Once one of ours is dropped in next to them, it wins.
    (game / "eternaljk.x86_64.exe").write_text("", encoding="ascii")
    check(verify.find_binary(game, None) == game / "eternaljk.x86_64.exe",
          "an engine of ours is preferred over the retail executables")

    (game / "jkx_ja.x86_64.exe").write_text("", encoding="ascii")
    check(verify.find_binary(game, None) == game / "jkx_ja.x86_64.exe",
          "JKX wins over EternalJK when both are present")

    explicit = game / "custom.exe"
    explicit.write_text("", encoding="ascii")
    check(verify.find_binary(game, str(explicit)) == explicit, "--binary is taken as given")

    try:
        verify.find_binary(game, str(game / "nope.exe"))
        check(False, "--binary pointing at nothing is rejected")
    except SystemExit:
        check(True, "--binary pointing at nothing is rejected")

    bare = tmp / "bare"
    (bare / "base").mkdir(parents=True)
    try:
        verify.find_binary(bare, None)
        check(False, "a directory with neither is refused")
    except SystemExit as exc:
        check("cannot load rd-vulkan" not in str(exc),
              "a directory with neither gets the other message")


# --------------------------------------------------------------------------
# console parsing and the report
# --------------------------------------------------------------------------

PBR_DUMP = """
VK_VENDOR: NVIDIA
VK_RENDERER: NVIDIA GeForce RTX 4070
VK_VERSION: 1.3.280
JKX: lighting path = PBR (maxBoundDescriptorSets 32)
reusing pipeline cache (4096 KiB)
pipeline descriptors: 2304
pipeline handles: 1712
"""

FASTLIGHT_DUMP = """
VK_VENDOR: Mesa
VK_RENDERER: llvmpipe
VK_VERSION: 1.3.274
JKX: lighting path = fastlight (PBR OFF): this device reports maxBoundDescriptorSets 8, and the PBR path needs 11.
VUID-VkImageMemoryBarrier2-oldLayout-01197
Validation Error: something
"""


def test_parsing() -> None:
    pbr = verify.parse_console(PBR_DUMP)
    check(pbr.get("lighting_path") == "PBR", "PBR run reports the PBR path")
    check(pbr.get("max_bound_sets") == "32", "descriptor set limit read")
    check(pbr.get("cache_reused") == "4096", "pipeline cache reuse read")
    check(pbr.get("pipeline_defs") == "2304" and pbr.get("pipeline_handles") == "1712",
          "pipeline counts read")
    check(pbr.get("validation_hits") == [], "clean run has no validation hits")

    fast = verify.parse_console(FASTLIGHT_DUMP)
    check(fast.get("lighting_path") == "fastlight", "fastlight run is recognised")
    check(fast.get("max_bound_sets") == "8", "the limit that caused it is captured")
    check(len(fast.get("validation_hits", [])) == 2, "validation messages collected")

    check(verify.parse_console("").get("lighting_path") is None,
          "a build predating the diagnostic reports nothing rather than guessing")


def test_startup_cvars() -> None:
    # cl_renderer is CVAR_LATCH. Written from a config that runs after startup
    # it takes effect on the next vid_restart, so the startup scenario would
    # measure the OpenGL renderer while claiming to measure Vulkan. It has to be
    # on the command line, and the config must not set it at all.
    cvars = dict(verify.STARTUP_CVARS)
    check(cvars.get("cl_renderer") == "rd-vulkan", "cl_renderer is set before startup")
    check(cvars.get("s_initsound") == "0", "sound init stays out of the timings")

    cfg = verify.build_cfg(verify.SCENARIOS["startup"]["cfg"], "mp/ffa3")
    check("cl_renderer" not in cfg, "the config does not set cl_renderer")
    check("vkinfo" in cfg and cfg.strip().endswith("quit"), "the config still ends in quit")

    cfg = verify.build_cfg(verify.SCENARIOS["map"]["cfg"], "t1_sour")
    check("map t1_sour" in cfg, "the map name is substituted")


def test_report() -> None:
    tmp = Path(tempfile.mkdtemp())

    class Args:
        binary = "jkx_ja.x86_64"
        resolved_game = "/games/GameData"
        map = "mp/ffa3"
        runs = 3

    results = {
        "startup": {"samples": [12.0, 11.0, 11.5], "median": 11.5},
        "vid_restart": {"samples": [20.0, 19.0, 19.5], "median": 19.5},
        "map": {"samples": [40.0, 41.0, 40.5], "median": 40.5},
    }

    out = tmp / "pbr.md"
    verify.write_report(out, results, verify.parse_console(PBR_DUMP), Args())
    text = out.read_text(encoding="utf-8")
    check("**PBR is active**" in text, "report says PBR is active")
    check("| vid_restart alone | 8.0 s |" in text, "restart cost is startup subtracted out")
    check("| map load alone | 29.0 s |" in text, "map cost is startup subtracted out")

    out = tmp / "fast.md"
    verify.write_report(out, results, verify.parse_console(FASTLIGHT_DUMP), Args())
    text = out.read_text(encoding="utf-8")
    check("**PBR is OFF" in text, "report is loud when PBR was off")
    check("VUID-VkImageMemoryBarrier2-oldLayout-01197" in text, "validation messages reach the report")

    # The failure the retail executable produces, and the one an install without
    # rd-vulkan next to the engine produces: OpenGL ran instead.
    out = tmp / "gl.md"
    verify.write_report(out, results, verify.parse_console("GL_VENDOR: NVIDIA\nGL_RENDERER: x\n"), Args())
    check("did not report itself" in out.read_text(encoding="utf-8"),
          "report calls out a run where rd-vulkan never loaded")

    out = tmp / "empty.md"
    verify.write_report(out, {}, verify.parse_console(""), Args())
    text = out.read_text(encoding="utf-8")
    check("predates the diagnostic" in text, "report is honest when it learned nothing")
    check("| - |" in text, "missing timings render as a dash rather than crashing")


def test_missing() -> None:
    # No Steam at all is a different problem from Steam without the game, and
    # the fix differs, so the two messages must not be interchangeable.
    empty = Path(tempfile.mkdtemp())
    verify.Path.home = staticmethod(lambda: empty)
    check("No Steam installation found" in verify.not_found_message(),
          "no Steam at all is reported as such")

    (empty / ".steam" / "steam" / "steamapps" / "common").mkdir(parents=True)
    message = verify.not_found_message()
    check("Steam is installed, but neither" in message, "Steam without the game is reported as such")
    check(str(empty) in message, "the message says where it looked")

    class Args:
        game = None
        title = None

    try:
        verify.resolve_game(Args())
        check(False, "resolve_game exits when there is nothing to run")
    except SystemExit:
        check(True, "resolve_game exits when there is nothing to run")

    class BadPath:
        game = str(empty / "nope")
        title = None

    try:
        verify.resolve_game(BadPath())
        check(False, "a --game path that does not exist is rejected up front")
    except SystemExit:
        check(True, "a --game path that does not exist is rejected up front")


def main() -> int:
    for test in (test_vdf, test_layout, test_detection, test_binary, test_startup_cvars,
                 test_parsing, test_report, test_missing):
        print(f"\n{test.__name__}")
        test()
    print()
    print(f"{len(FAILURES)} failure(s)")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
