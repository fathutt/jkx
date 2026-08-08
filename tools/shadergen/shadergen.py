#!/usr/bin/env python3
"""Cross-platform GLSL -> SPIR-V build for the Vulkan renderer.

Replaces shaders/tools/compile_threaded.cpp, which was Windows-only (windows.h,
_beginthreadex, _findfirst), lived outside CMake, shelled out to a separate
glslangValidator process per shader, and emitted a 61.7 MB C file that was
committed to git.

What changes:

  * permutations are declared in shaders/shaders.json, not in nested C++ loops;
  * compilation goes through glslc with -MD, so CMake tracks #include edges by
    itself and a change to global.h rebuilds exactly what depends on it;
  * output is one shaders.pak, built at build time and never committed
    (standards 9.3);
  * modules are looked up by name hash at runtime and created lazily, instead of
    572 vkCreateShaderModule calls at startup.

Subcommands:

    plan   emit the variant list as a CMake-includable file
    build  compile everything directly (no CMake needed; useful for iteration)
    pack   assemble compiled .spv files into shaders.pak
    list   print the variants a manifest expands to

Formatting of the pak is documented in vk_shader_pak.h.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import re
import shutil
import struct
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

PAK_MAGIC = b"JKSP"
PAK_VERSION = 1

STAGE_NAMES = {"vert": "vertex", "frag": "fragment", "geom": "geometry", "comp": "compute"}


# ---------------------------------------------------------------------------
# manifest expansion
# ---------------------------------------------------------------------------

@dataclass
class Variant:
    name: str
    source: str
    stage: str
    defines: list[str] = field(default_factory=list)
    slot: str | None = None

    @property
    def spv(self) -> str:
        return f"{self.name}.spv"


_SAFE_EXPR = re.compile(r"^[A-Za-z0-9_.\s!=<>()+\-*/%&|]*$")


def eval_guard(expr: str, indices: dict[str, int]) -> bool:
    """Evaluate a 'when'/'condition' guard against the indices chosen so far.

    Deliberately restricted: names look like 'tx.index', operators are plain
    arithmetic and comparison. No attribute access, no calls, no builtins.
    """
    if not _SAFE_EXPR.match(expr):
        raise ValueError(f"unsafe guard expression: {expr!r}")
    scope = {f"{axis}_index": value for axis, value in indices.items()}
    normalised = re.sub(r"\b([A-Za-z_][A-Za-z0-9_]*)\.index\b", r"\1_index", expr)
    return bool(eval(normalised, {"__builtins__": {}}, scope))  # noqa: S307 - guarded above


def substitute(template: str, ids: dict[str, str], indices: dict[str, int]) -> str:
    out = template
    for axis, value in ids.items():
        out = out.replace("{" + axis + "}", value)
    for axis, value in indices.items():
        out = out.replace("{" + axis + ".index}", str(value))
    if "{" in out:
        raise ValueError(f"unresolved placeholder in {template!r} -> {out!r}")
    return out


def expand_program(program: dict, axes: dict) -> list[Variant]:
    """Expand one manifest entry into concrete variants."""
    source = program["source"]
    stage = program["stage"]

    product = program.get("product")
    if not product:
        return [Variant(program["name"], source, stage, list(program.get("defines", [])))]

    variants: list[Variant] = []

    def recurse(depth: int, ids: dict[str, str], indices: dict[str, int], defines: list[str]) -> None:
        if depth == len(product):
            name = substitute(program["name"], ids, indices)
            slot = substitute(program["slot"], ids, indices) if program.get("slot") else None
            extra = list(program.get("defines", []))
            for rule in program.get("extra_defines_when", []):
                if eval_guard(rule["condition"], indices):
                    extra += rule["defines"]
            variants.append(Variant(name, source, stage, defines + extra, slot))
            return

        axis = product[depth]
        for index, option in enumerate(axes[axis]):
            guard = option.get("when")
            if guard and not eval_guard(guard, indices):
                continue
            option_defines = [substitute(d, ids, indices) for d in option.get("defines", [])]
            recurse(
                depth + 1,
                {**ids, axis: option["id"]},
                {**indices, axis: index},
                defines + option_defines,
            )

    recurse(0, {}, {}, [])
    return variants


def load_manifest(path: Path) -> tuple[dict, list[Variant]]:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    axes = manifest["axes"]

    variants: list[Variant] = []
    seen: dict[str, Variant] = {}
    for program in manifest["programs"]:
        for variant in expand_program(program, axes):
            if variant.name in seen:
                raise SystemExit(f"duplicate variant name: {variant.name}")
            seen[variant.name] = variant
            variants.append(variant)

    # single-variant shaders, discovered by extension
    standalone = manifest.get("standalone", {})
    glsl_dir = path.parent / "glsl"
    for suffix, stage in standalone.get("extensions", {}).items():
        for src in sorted(glsl_dir.glob(f"*{suffix}")):
            name = src.stem + "_" + stage
            if name in seen:
                continue
            variant = Variant(name, src.name, stage)
            seen[name] = variant
            variants.append(variant)

    return manifest, variants


# ---------------------------------------------------------------------------
# name hashing - must match vk_shader_pak.h exactly
# ---------------------------------------------------------------------------

def name_hash(name: str) -> int:
    """FNV-1a, 64 bit."""
    h = 0xCBF29CE484222325
    for byte in name.encode("ascii"):
        h ^= byte
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


# ---------------------------------------------------------------------------
# subcommands
# ---------------------------------------------------------------------------

def cmd_list(args: argparse.Namespace) -> int:
    _, variants = load_manifest(Path(args.manifest))
    by_source: dict[str, int] = {}
    for v in variants:
        by_source[v.source] = by_source.get(v.source, 0) + 1
        if args.verbose:
            print(f"{v.name:60s} {v.stage:5s} {v.source:26s} {' '.join(v.defines)}")
    for source, count in sorted(by_source.items(), key=lambda kv: -kv[1]):
        print(f"{count:5d}  {source}")
    print(f"{len(variants):5d}  TOTAL")
    return 0


def cmd_plan(args: argparse.Namespace) -> int:
    """Emit the variant list for CMake to turn into custom commands."""
    manifest_path = Path(args.manifest)
    manifest, variants = load_manifest(manifest_path)

    lines = [
        "# Generated by tools/shadergen/shadergen.py -- do not edit.",
        f"set(JKX_SHADER_TARGET_ENV \"{manifest.get('glsl_version', 'vulkan1.3')}\")",
        f"set(JKX_SHADER_COUNT {len(variants)})",
        "set(JKX_SHADER_VARIANTS",
    ]
    for v in variants:
        # Defines are comma-joined and "-" stands for none: CMake drops a
        # trailing empty element when splitting, so an empty field would make
        # the record arity vary.
        defines = ",".join(v.defines) if v.defines else "-"
        lines.append(f'    "{v.name}|{v.source}|{v.stage}|{defines}"')
    lines.append(")")

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"planned {len(variants)} variant(s) -> {out}")
    return 0


def compile_one(
    compiler: str, glsl_dir: Path, out_dir: Path, target_env: str, variant: Variant, optimise: bool
) -> tuple[Variant, int, str]:
    src = glsl_dir / variant.source
    dst = out_dir / variant.spv
    dep = dst.with_suffix(".spv.d")
    cmd = [
        compiler,
        f"--target-env={target_env}",
        f"-fshader-stage={STAGE_NAMES[variant.stage]}",
        "-I", str(glsl_dir),
        "-MD", "-MF", str(dep),
        "-o", str(dst),
    ]
    if optimise:
        cmd.append("-O")
    cmd += [f"-D{d}" for d in variant.defines]
    cmd.append(str(src))
    proc = subprocess.run(cmd, capture_output=True, text=True)
    return variant, proc.returncode, (proc.stderr or proc.stdout)


def cmd_build(args: argparse.Namespace) -> int:
    manifest_path = Path(args.manifest)
    manifest, variants = load_manifest(manifest_path)
    glsl_dir = manifest_path.parent / "glsl"
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    compiler = args.compiler or shutil.which("glslc")
    if compiler is None:
        print("glslc not found; install the Vulkan SDK or shaderc", file=sys.stderr)
        return 2

    target_env = manifest.get("glsl_version", "vulkan1.3")
    failures: list[tuple[Variant, str]] = []

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [
            pool.submit(compile_one, compiler, glsl_dir, out_dir, target_env, v, not args.no_optimise)
            for v in variants
        ]
        for done, future in enumerate(concurrent.futures.as_completed(futures), start=1):
            variant, code, message = future.result()
            if code != 0:
                failures.append((variant, message))
            if args.verbose or code != 0:
                status = "ok  " if code == 0 else "FAIL"
                print(f"[{done}/{len(variants)}] {status} {variant.name}")
                if code != 0:
                    print(message.rstrip())

    total = sum(f.stat().st_size for f in out_dir.glob("*.spv"))
    print(f"\ncompiled {len(variants) - len(failures)}/{len(variants)} variant(s), "
          f"{total / 1024:.0f} KiB of SPIR-V")
    if failures:
        print(f"FAILED: {len(failures)} variant(s)")
        for variant, message in failures[:10]:
            print(f"  {variant.name}: {message.splitlines()[0] if message else 'unknown error'}")
        return 1
    return 0


def cmd_pack(args: argparse.Namespace) -> int:
    """Assemble .spv files into a single pak. Layout is described in vk_shader_pak.h."""
    manifest_path = Path(args.manifest)
    _, variants = load_manifest(manifest_path)
    spv_dir = Path(args.spv_dir)

    entries: list[tuple[int, str, bytes]] = []
    missing: list[str] = []
    for variant in variants:
        blob_path = spv_dir / variant.spv
        if not blob_path.is_file():
            missing.append(variant.name)
            continue
        entries.append((name_hash(variant.name), variant.name, blob_path.read_bytes()))

    if missing:
        print(f"FAILED: {len(missing)} variant(s) not compiled, e.g. {missing[:3]}", file=sys.stderr)
        return 1

    entries.sort(key=lambda e: e[0])
    for i in range(1, len(entries)):
        if entries[i][0] == entries[i - 1][0]:
            print(f"FAILED: hash collision between {entries[i - 1][1]!r} and {entries[i][1]!r}",
                  file=sys.stderr)
            return 1

    header = struct.calcsize("<4sIII")
    table = struct.calcsize("<QII") * len(entries)
    offset = header + table

    table_bytes = bytearray()
    blob_bytes = bytearray()
    for h, _name, blob in entries:
        padded = (len(blob) + 3) & ~3
        table_bytes += struct.pack("<QII", h, offset + len(blob_bytes), len(blob))
        blob_bytes += blob + b"\0" * (padded - len(blob))

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("wb") as fh:
        fh.write(struct.pack("<4sIII", PAK_MAGIC, PAK_VERSION, len(entries), 0))
        fh.write(table_bytes)
        fh.write(blob_bytes)

    size = out.stat().st_size
    print(f"packed {len(entries)} module(s), {size / 1024:.0f} KiB -> {out}")
    print(f"(the C array this replaces was 61.7 MB of source and 1.9 M lines)")
    return 0


def main(argv: list[str]) -> int:
    here = Path(__file__).resolve().parents[2]
    default_manifest = here / "code" / "rd-vulkan" / "shaders" / "shaders.json"

    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--manifest", default=str(default_manifest))
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("list", help="print expanded variants")
    p.add_argument("-v", "--verbose", action="store_true")
    p.set_defaults(func=cmd_list)

    p = sub.add_parser("plan", help="emit CMake variant list")
    p.add_argument("--out", required=True)
    p.set_defaults(func=cmd_plan)

    p = sub.add_parser("build", help="compile all variants")
    p.add_argument("--out", required=True)
    p.add_argument("--compiler", default=None)
    p.add_argument("-j", "--jobs", type=int, default=0)
    p.add_argument("--no-optimise", action="store_true")
    p.add_argument("-v", "--verbose", action="store_true")
    p.set_defaults(func=cmd_build)

    p = sub.add_parser("pack", help="assemble shaders.pak")
    p.add_argument("--spv-dir", required=True)
    p.add_argument("--out", required=True)
    p.set_defaults(func=cmd_pack)

    args = parser.parse_args(argv[1:])
    if getattr(args, "jobs", 0) == 0:
        import os
        args.jobs = max(1, (os.cpu_count() or 2))
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
