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

import json
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

    # What a fresh Steam install actually contains, plus the OpenJK build that
    # tends to be sitting next to it. All of them start and run; none has a
    # Vulkan renderer, so launching one produces a confident report about the
    # wrong renderer. That is the failure this refusal exists to prevent.
    for name in ("JediAcademy.exe", "jasp.exe", "jamp.exe", "openjk.x86.exe"):
        (game / name).write_text("", encoding="ascii")

    try:
        verify.find_binary(game, None)
        check(False, "an install with no Vulkan-capable engine is refused")
    except SystemExit as exc:
        message = str(exc)
        check("none of them" in message, "an install with no Vulkan-capable engine is refused")
        check("openjk.x86.exe" in message and "JediAcademy.exe" in message,
              "the message names every engine it did find")
        check("releases/download" in message, "the message says where to get a Vulkan build")

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
        check("none of them" not in str(exc),
              "a directory with neither gets the other message")


def test_renderer_present() -> None:
    # An engine that can load rd-vulkan is not enough: the library has to be
    # there. When it is missing the engine falls back to OpenGL and runs
    # normally, so nothing downstream would notice.
    tmp = Path(tempfile.mkdtemp())
    game = tmp / "Jedi Academy"
    (game / "base").mkdir(parents=True)
    engine = game / "eternaljk.x86_64.exe"
    engine.write_text("", encoding="ascii")

    check(verify.renderer_libraries(game, engine) == [], "no renderer is detected as absent")

    message = verify.missing_renderer_message(game, engine)
    check("falls back to OpenGL" in message, "the message explains why silence is dangerous")
    check("releases/download" in message, "the message says where to get one")

    lib = game / "rd-vulkan_x86_64.dll"
    lib.write_text("", encoding="ascii")
    check(verify.renderer_libraries(game, engine) == [lib], "the renderer is found next to base/")

    # A build kept elsewhere, with --binary pointing into it, ships its own copy.
    build = tmp / "build"
    build.mkdir()
    other = build / "jkx_ja.x86_64"
    other.write_text("", encoding="ascii")
    (build / "rd-vulkan_x86_64.so").write_text("", encoding="ascii")
    check(len(verify.renderer_libraries(game, other)) == 2,
          "renderers are looked for next to the binary as well as next to base/")


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


def test_gamecode() -> None:
    # On Windows the gamecode ships inside a pk3 and, with sv_pure set, the
    # engine will only take it from one. So "is the dll on disk" is the wrong
    # question; "is it inside a game directory the filesystem searches" is the
    # right one. Getting this wrong looks like reaching the main menu and then
    # dying with VM_CreateLegacy on ui failed.
    import zipfile  # noqa: PLC0415

    tmp = Path(tempfile.mkdtemp())
    game = tmp / "GameData"
    (game / "base").mkdir(parents=True)
    binary = game / "eternaljk.x86_64.exe"
    binary.write_text("", encoding="ascii")

    found = verify.find_gamecode(game, binary)
    check(all(v is None for v in found.values()), "gamecode absent is detected")
    message = verify.missing_gamecode_message(game, binary, found)
    check("bins_" in message and "unpacked into the same folder as base/" in message,
          "the message explains that the whole archive has to be unpacked")

    # Exactly what the published Windows build looks like: one pk3 in the fork's
    # own game directory, holding all three modules.
    mod = game / "EternalJK"
    mod.mkdir()
    suffix = ".dll" if verify.platform.system() == "Windows" else ".so"
    pak = mod / "bins_2026.pk3"
    with zipfile.ZipFile(pak, "w") as zf:
        for stem in ("ui", "cgame", "jampgame"):
            zf.writestr(f"{stem}x86_64{suffix}", "")

    found = verify.find_gamecode(game, binary)
    check(all(v == pak for v in found.values()), "gamecode inside a pk3 is found")

    # A loose dll next to it wins, which is what a local build produces.
    loose = mod / f"uix86_64{suffix}"
    loose.write_text("", encoding="ascii")
    check(verify.find_gamecode(game, binary)[f"uix86_64{suffix}"] == loose,
          "a loose module is preferred over the pk3 copy")

    # A 32-bit engine needs 32-bit modules, and must not be told the 64-bit ones
    # will do.
    found = verify.find_gamecode(game, game / "eternaljk.x86.exe")
    check(all(v is None for v in found.values()), "modules of the wrong architecture do not count")


def test_merge_into() -> None:
    # The published archive may unpack one level down. Moving it up has to merge
    # directories: EternalJK/ holds the gamecode, and a directory that already
    # exists must not make the move a no-op.
    tmp = Path(tempfile.mkdtemp())
    src = tmp / "JediAcademy"
    dst = tmp / "GameData"
    (src / "EternalJK").mkdir(parents=True)
    (dst / "EternalJK").mkdir(parents=True)
    (src / "eternaljk.x86_64.exe").write_text("new", encoding="ascii")
    (src / "EternalJK" / "bins.pk3").write_text("new", encoding="ascii")
    (dst / "eternaljk.x86_64.exe").write_text("old", encoding="ascii")

    verify.merge_into(src, dst)
    check((dst / "EternalJK" / "bins.pk3").is_file(),
          "a directory that already exists is merged, not skipped")
    check((dst / "eternaljk.x86_64.exe").read_text(encoding="ascii") == "new",
          "existing files are overwritten")


def test_startup_cvars() -> None:
    # cl_renderer is CVAR_LATCH. Written from a config that runs after startup
    # it takes effect on the next vid_restart, so the startup scenario would
    # measure the OpenGL renderer while claiming to measure Vulkan. It has to be
    # on the command line, and the config must not set it at all.
    cvars = dict(verify.STARTUP_CVARS)
    check(cvars.get("cl_renderer") == "rd-vulkan", "cl_renderer is set before startup")
    # Sound off crashed the client during cgame load, three runs out of three,
    # so it stays on however much noise it adds to the timings.
    check(cvars.get("s_initsound") == "1", "sound is left enabled")
    check(cvars.get("logfile") == "2", "the console log is written as it happens")

    cfg = verify.build_cfg(verify.SCENARIOS["startup"]["cfg"], "mp/ffa3")
    check("cl_renderer" not in cfg, "the config does not set cl_renderer")
    check("vkinfo" in cfg and cfg.strip().endswith("quit"), "the config still ends in quit")

    cfg = verify.build_cfg(verify.SCENARIOS["map"]["cfg"], "t1_sour")
    check("map t1_sour" in cfg, "the map name is substituted")


MAP_DUMP = """
------ Server Initialization ------
Server: mp/ffa3
pipeline handles: 604
pipeline descriptors: 1211, base: 12
"""

MAP_FAILED_DUMP = """
Couldn't load maps/mp/nosuchmap.bsp
"""


def test_map_detection() -> None:
    # A map that fails to load costs almost nothing, so the "map load" timing is
    # a failure wearing the costume of a result. The first real run measured
    # 0.4 s for a map load, which is not a fast load, it is no load.
    loaded = verify.parse_console(MAP_DUMP)
    check(loaded.get("server_init") is True, "a started server is recognised")
    check(loaded.get("map_name") == "mp/ffa3", "the map name is read back")
    check(loaded.get("ibl") is None, "no IBL bake is reported when there is no env.json")

    failed = verify.parse_console(MAP_FAILED_DUMP)
    check(failed.get("map_failed") == "maps/mp/nosuchmap.bsp", "a failed load is recognised")
    check(failed.get("server_init") is None, "a failed load did not start a server")

    ibl = verify.parse_console("Loaded Enviroment JSON: cubemaps/mp/ffa3/env.json\n")
    check(ibl.get("ibl") == "cubemaps/mp/ffa3/env.json", "an actual bake is recognised")


def test_crash_reporting() -> None:
    tmp = Path(tempfile.mkdtemp())

    class Args:
        binary = "eternaljk.x86_64.exe"
        resolved_game = "/games/GameData"
        map = "mp/ffa3"
        runs = 3

    # 0xC0000005. The process is killed outright, so there is no dialog and no
    # crash log; the exit code is the only thing that survives, which is why it
    # is now recorded instead of discarded.
    results = {"map": {"samples": [8.3, 8.0, 8.0], "median": 8.0,
                       "exit_codes": [3221225477, 3221225477, 3221225477]}}
    out = tmp / "crash.md"
    verify.write_report(out, results, {}, Args(), {},
                        [{"variant": "OpenGL renderer", "exit": 0, "seconds": 9.0},
                         {"variant": "sound enabled", "exit": 3221225477, "seconds": 8.1}])
    text = out.read_text(encoding="utf-8")
    check("did not exit cleanly" in text, "a crashing run is called a crash")
    check("0xC0000005" in text, "the exit code is translated into something readable")
    check("| OpenGL renderer | clean |" in text, "triage results are tabulated")
    check("**OpenGL renderer**" in text, "the variant that avoided the crash is highlighted")

    check("No jkx_crash.txt was written" in text,
          "a crash with no stack says why there is no stack")

    out = tmp / "clean.md"
    verify.write_report(out, {"map": {"samples": [8.0], "median": 8.0, "exit_codes": [0]}},
                        {}, Args(), {}, [])
    check("did not exit cleanly" not in out.read_text(encoding="utf-8"),
          "a clean run says nothing about crashes")


def test_intermittent_crash_triage() -> None:
    """Four clean variants after a flaky crash must not read as four answers."""
    tmp = Path(tempfile.mkdtemp())

    class Args:
        binary = "jkx_ja.x86_64.exe"
        resolved_game = "/games/GameData"
        map = "mp/ffa3"
        runs = 3

    variants = [{"variant": "sound enabled", "exit": 0, "seconds": 4.0},
                {"variant": "OpenGL renderer", "exit": 0, "seconds": 4.0},
                {"variant": "a different map", "exit": 0, "seconds": 4.0}]

    # Crashed once out of three: every variant survives its single run.
    flaky = {"vid_restart": {"samples": [4.4, 1.9, 4.3], "median": 4.3,
                             "exit_codes": [0, 3221225477, 0]}}
    out = tmp / "flaky.md"
    verify.write_report(out, flaky, {}, Args(), {}, variants)
    text = out.read_text(encoding="utf-8")
    check("crash is intermittent" in text, "an intermittent crash is called intermittent")
    check("avoids the crash" not in text,
          "no variant is credited with fixing a crash it never faced")
    check("jkx_<scenario>_run<n>.log" in text,
          "the report points at the preserved log of the run that died")

    # Crashed every time: the variants mean what they say.
    always = {"map": {"samples": [8.0, 8.0, 8.0], "median": 8.0,
                      "exit_codes": [3221225477] * 3}}
    out = tmp / "always.md"
    verify.write_report(out, always, {}, Args(), {}, variants)
    text = out.read_text(encoding="utf-8")
    check("avoids the crash" in text, "a reliable crash still gets a verdict")
    check("crash is intermittent" not in text,
          "a reliable crash is not called intermittent")


def test_trace_timings_are_flagged() -> None:
    """A diagnostics build's numbers must not read as a baseline."""
    tmp = Path(tempfile.mkdtemp())

    class Args:
        binary = "jkx_ja.x86_64.exe"
        resolved_game = "/games/GameData"
        map = "mp/ffa3"
        runs = 3

    results = {"startup": {"samples": [2.8], "median": 2.8, "exit_codes": [0]}}
    trace = ("VK_VENDOR: NVIDIA\nJKX: lighting path = PBR (maxBoundDescriptorSets 32)\n"
             "JKX: trace build, writing vk_log.log next to the executable\n")

    out = tmp / "trace.md"
    verify.write_report(out, results, verify.parse_console(trace), Args(), {})
    text = out.read_text(encoding="utf-8")
    check("diagnostics build" in text, "trace timings carry a warning")
    check("windows-x86_64-trace" in text, "the warning names the package to avoid")

    out = tmp / "plain.md"
    verify.write_report(out, results, verify.parse_console(
        "VK_VENDOR: NVIDIA\nJKX: lighting path = PBR (maxBoundDescriptorSets 32)\n"), Args(), {})
    check("diagnostics build" not in out.read_text(encoding="utf-8"),
          "a plain build's timings carry no warning")


def test_provenance() -> None:
    """A report has to say which tool wrote it and which package it measured."""
    stamp = {"commit": "861b650deadbeef", "built": "2026-08-09T11:00:00Z",
             "package": "windows-x86_64"}

    agreed = "\n".join(verify.provenance_section("861b650", stamp))
    check("verify: 861b650" in agreed, "the report names the tool revision")
    check("windows-x86_64" in agreed, "the report names the package")
    check("different commits" not in agreed,
          "matching revisions produce no warning")

    mismatched = "\n".join(verify.provenance_section("0000000", stamp))
    check("different commits" in mismatched,
          "a tool older than the package is called out")

    unpackaged = "\n".join(verify.provenance_section("861b650", {}))
    check("no jkx-build.txt" in unpackaged,
          "a build with no stamp is described, not silently accepted")

    unknown = "\n".join(verify.provenance_section("unknown - not run from a git checkout", stamp))
    check("different commits" not in unknown,
          "an unknown tool revision does not fake a mismatch")

    tmp = Path(tempfile.mkdtemp())
    (tmp / "jkx-build.txt").write_text(
        "861b650deadbeef\n2026-08-09T11:00:00Z\npackage: windows-x86_64\n", encoding="utf-8")
    read = verify.package_stamp(tmp)
    check(read.get("commit") == "861b650deadbeef", "the stamp's commit is read")
    check(read.get("package") == "windows-x86_64", "the stamp's package name is read")
    check(verify.package_stamp(Path(tempfile.mkdtemp()) / "sub" / "dir") == {},
          "a missing stamp is an empty dict, not an error")


def test_crash_stack() -> None:
    """The fault handler's output has to reach the report, not just dumps/."""
    tmp = Path(tempfile.mkdtemp())

    class Args:
        binary = "jkx_ja.x86_64.exe"
        resolved_game = "/games/GameData"
        map = "mp/ffa3"
        runs = 1

    stack = tmp / "jkx_map_crash.txt"
    stack.write_text(
        "\n=== JKX crash report (unhandled) ===\n"
        "exception: 0xc0000005 ACCESS_VIOLATION\n"
        "address:   0x00007ffb0000abcd\n"
        "operation: read of 0x0000000000000000\n"
        "stack:\n"
        "   0  0x00007ffb0000abcd  rd-jkx_x86_64+0x1abcd  vk_create_image+0x4d"
        "  (vk_image.cpp:812)\n"
        "=== end of report ===\n", encoding="utf-8")

    out = tmp / "stack.md"
    verify.write_report(out, {"map": {"samples": [1.0], "median": 1.0,
                                      "exit_codes": [3221225477]}},
                        {}, Args(), {}, [], [stack])
    text = out.read_text(encoding="utf-8")
    check("## Crash stack" in text, "a captured stack gets its own section")
    check("vk_create_image+0x4d" in text, "the symbolised frame is quoted verbatim")
    check("jkx_map_crash.txt" in text, "the report names the file the stack came from")
    check("No jkx_crash.txt was written" not in text,
          "the missing-stack note is suppressed when a stack is present")

    empty = tmp / "jkx_startup_crash.txt"
    empty.write_text("   \n", encoding="utf-8")
    out = tmp / "empty.md"
    verify.write_report(out, {"map": {"samples": [1.0], "median": 1.0, "exit_codes": [0]}},
                        {}, Args(), {}, [], [empty])
    check("## Crash stack" not in out.read_text(encoding="utf-8"),
          "an empty crash file does not produce an empty section")


def test_fault_handler_reported() -> None:
    """Whether the package can report a crash is part of build identity."""
    tmp = Path(tempfile.mkdtemp())

    class Args:
        binary = "jkx_ja.x86_64.exe"
        resolved_game = "/games/GameData"
        map = "mp/ffa3"
        runs = 1

    with_handler = ("VK_VENDOR: NVIDIA\n"
                    "JKX: lighting path = PBR (maxBoundDescriptorSets 32)\n"
                    "JKX: crash reports go to D:\\Games\\GameData\\jkx_crash.txt\n")
    out = tmp / "with.md"
    verify.write_report(out, {}, verify.parse_console(with_handler), Args(), {})
    check("fault handler: installed" in out.read_text(encoding="utf-8"),
          "a package with the handler says so")

    without = ("VK_VENDOR: NVIDIA\n"
               "JKX: lighting path = PBR (maxBoundDescriptorSets 32)\n")
    out = tmp / "without.md"
    verify.write_report(out, {}, verify.parse_console(without), Args(), {})
    check("fault handler: absent" in out.read_text(encoding="utf-8"),
          "a package without the handler says so too")


def test_build_identity() -> None:
    # The artifact and the build it replaces have identical file names, so an
    # unpack that skips existing files leaves the old engine in place and the
    # run measures the baseline a second time. It happened on the first real
    # artifact, and nothing in the report said so.
    upstream = "EternalJK: Aug  6 2026 win_msvc-x86_64\nVK_VENDOR: NVIDIA\n"
    info = verify.parse_console(upstream)
    check(info.get("engine_build") == "EternalJK: Aug  6 2026 win_msvc-x86_64",
          "the engine name and build date are read back")
    check(info.get("jkx_marker") is None, "an upstream build carries no JKX marker")

    ours = upstream + "JKX: lighting path = PBR (maxBoundDescriptorSets 32)\n"
    check(verify.parse_console(ours).get("jkx_marker") is True, "our renderer identifies itself")

    tmp = Path(tempfile.mkdtemp())

    class Args:
        binary = "eternaljk.x86_64.exe"
        resolved_game = "/games/GameData"
        map = "mp/ffa3"
        runs = 3

    out = tmp / "upstream.md"
    verify.write_report(out, {}, info, Args(), {})
    text = out.read_text(encoding="utf-8")
    check("NOT a JKX build" in text, "an upstream build is called out as one")
    check("skips existing files" in text or "leaves the old build" in text,
          "the report says what to check")

    out = tmp / "ours.md"
    verify.write_report(out, {}, verify.parse_console(ours), Args(), {})
    check("is a JKX build" in out.read_text(encoding="utf-8"), "our build is recognised")


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

    # Per-stage pipeline counts, and the map verdict.
    out = tmp / "stages.md"
    stages = {
        "startup": verify.parse_console(PBR_DUMP),
        "vid_restart": {},
        "map": verify.parse_console(MAP_DUMP),
    }
    verify.write_report(out, results, verify.parse_console(PBR_DUMP), Args(), stages)
    text = out.read_text(encoding="utf-8")
    check("| startup to main menu | 2304 | 1712 |" in text, "menu pipeline counts are attributed")
    check("| startup + map load | 1211 | 604 |" in text, "map pipeline counts are attributed")
    check("`mp/ffa3` loaded." in text, "the report confirms the map came up")
    check("no IBL bake happened" in text, "the report says when there was no bake to measure")

    out = tmp / "nomap.md"
    verify.write_report(out, results, verify.parse_console(PBR_DUMP), Args(),
                        {"map": verify.parse_console(MAP_FAILED_DUMP)})
    check("failed to load" in out.read_text(encoding="utf-8"),
          "a map that never loaded is called out, not reported as a fast load")

    # Silence has two causes and they need different answers: no dump file at
    # all, versus a dump that simply does not contain the line. Reporting the
    # second as "the map never started" contradicts a timing that says it did.
    out = tmp / "nodump.md"
    verify.write_report(out, results, verify.parse_console(PBR_DUMP), Args(),
                        {"map": {"dump": None}})
    check("Nothing was written for this scenario" in out.read_text(encoding="utf-8"),
          "no evidence at all is reported as that")

    # The log is written as it happens and survives an error, so its silence is
    # decisive where a condump's is not.
    out = tmp / "nolog.md"
    verify.write_report(out, results, verify.parse_console(PBR_DUMP), Args(),
                        {"map": {"dump": None, "log": "/somewhere/jkx_map.log"}})
    check("The map did not start" in out.read_text(encoding="utf-8"),
          "a log without server init is treated as decisive")

    out = tmp / "quiet.md"
    verify.write_report(out, results, verify.parse_console(PBR_DUMP), Args(),
                        {"map": {"dump": "/somewhere/jkx_map.txt"}})
    text = out.read_text(encoding="utf-8")
    check("a load that took" in text,
          "a dump without the marker defers to the timing instead of contradicting it")

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


# --------------------------------------------------------------------------
# the whole thing, against an engine that only pretends to render
# --------------------------------------------------------------------------

FAKE_ENGINE = r'''#!/usr/bin/env python3
# Stands in for the engine: reads the config it was told to exec, finds the
# condump filename in it, and writes a plausible dump where the real engine
# would - fs_homepath/<mod dir>, not fs_homepath itself.
import sys
from pathlib import Path

argv = sys.argv[1:]
values = {}
for i, a in enumerate(argv):
    if a == "+set" and i + 2 < len(argv):
        values[argv[i + 1]] = argv[i + 2]
    if a == "+exec" and i + 1 < len(argv):
        values["cfg"] = argv[i + 1]

cfg = Path(values["fs_basepath"]) / "base" / values["cfg"]
name = None
for line in cfg.read_text().splitlines():
    if line.startswith("condump "):
        name = line.split(None, 1)[1].strip()

out = Path(values["fs_homepath"]) / "EternalJK"
out.mkdir(parents=True, exist_ok=True)
(out / name).write_text(
    "cl_renderer is " + values.get("cl_renderer", "?") + "\n"
    "VK_VENDOR: NVIDIA\n"
    "VK_RENDERER: NVIDIA GeForce RTX 4070\n"
    "VK_VERSION: 1.3.280\n"
    "JKX: lighting path = PBR (maxBoundDescriptorSets 32)\n"
    "pipeline handles: 1712\n"
    "pipeline descriptors: 2304, base: 12\n")
'''


def test_end_to_end() -> None:
    import platform  # noqa: PLC0415 - only needed here
    if platform.system() == "Windows":
        print("skip  end to end (needs a POSIX shebang)")
        return

    tmp = Path(tempfile.mkdtemp())
    game = tmp / "Jedi Academy"
    (game / "base").mkdir(parents=True)

    engine = game / "jkx_ja.x86_64"
    engine.write_text(FAKE_ENGINE, encoding="ascii")
    engine.chmod(0o755)
    (game / "rd-vulkan_x86_64.so").write_text("", encoding="ascii")
    for stem in ("ui", "cgame", "jampgame"):
        (game / "base" / f"{stem}x86_64.so").write_text("", encoding="ascii")

    home = tmp / "home"
    out = tmp / "report.md"
    code = verify.main(["verify.py", "--game", str(game), "--runs", "1",
                        "--home", str(home), "--out", str(out)])
    check(code == 0, "a full run exits cleanly")
    check(out.is_file(), "the report is written where --out asked")

    text = out.read_text(encoding="utf-8")
    check("**PBR is active**" in text, "the dump written by the run reaches the report")
    check("NVIDIA GeForce RTX 4070" in text, "the device section is filled in")

    data = json.loads(out.with_suffix(".json").read_text(encoding="utf-8"))
    check(sorted(data["results"]) == ["map", "startup", "vid_restart"],
          "all three scenarios ran")

    # The dump was written to home/EternalJK/, not home/, and was still found.
    check((home / "EternalJK" / "jkx_startup.txt").is_file(),
          "dumps land in the mod directory, and are found there")

    # And the renderer really was selected on the command line.
    dump = (home / "EternalJK" / "jkx_startup.txt").read_text(encoding="utf-8")
    check("cl_renderer is rd-vulkan" in dump, "the engine was started on rd-vulkan")


def main() -> int:
    for test in (test_vdf, test_layout, test_detection, test_binary, test_renderer_present, test_gamecode,
                 test_merge_into, test_startup_cvars, test_map_detection,
                 test_crash_reporting,
                 test_intermittent_crash_triage,
                 test_trace_timings_are_flagged,
                 test_provenance,
                 test_crash_stack,
                 test_fault_handler_reported,
                 test_parsing, test_build_identity, test_report, test_missing, test_end_to_end):
        print(f"\n{test.__name__}")
        test()
    print()
    print(f"{len(FAILURES)} failure(s)")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
