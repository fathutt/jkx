#!/usr/bin/env python3
"""Assert the Vulkan 1.3 feature baseline JKX targets is actually available.

Runs against whatever driver the environment exposes. On CI that is lavapipe,
which was verified on 2026-08-08 to support the entire baseline plus the six
extendedDynamicState3 features needed to collapse the pipeline key.

Two reasons this exists:

  * A silent feature downgrade is exactly the failure mode the standards forbid
    (section 8.2). The upstream renderer disables PBR outright when
    maxBoundDescriptorSets < 11, without printing anything -- and lavapipe
    reports 8, so a software-rendering CI job would happily test the wrong path.
  * It documents, executably, what "Vulkan 1.3 baseline" means for this project.

Usage:
    tools/ci/check_vulkan_baseline.py [--allow-missing name,name]
"""

from __future__ import annotations

import shutil
import subprocess
import sys

# Core Vulkan 1.3 features the engine relies on unconditionally.
REQUIRED_FEATURES = [
    "dynamicRendering",
    "synchronization2",
    "timelineSemaphore",
    "descriptorIndexing",
    "runtimeDescriptorArray",
    "shaderSampledImageArrayNonUniformIndexing",
    "descriptorBindingPartiallyBound",
    "descriptorBindingVariableDescriptorCount",
    "maintenance4",
]

# Not core in 1.3. Detected at runtime, always with a static fallback, but we
# want CI to notice if the software driver stops providing them.
OPTIONAL_FEATURES = [
    "extendedDynamicState3ColorBlendEnable",
    "extendedDynamicState3ColorBlendEquation",
    "extendedDynamicState3ColorWriteMask",
    "extendedDynamicState3PolygonMode",
    "extendedDynamicState3RasterizationSamples",
    "extendedDynamicState3AlphaToCoverageEnable",
]

# Limits worth surfacing because the code has historically branched on them.
INTERESTING_LIMITS = [
    "maxBoundDescriptorSets",
    "maxPerStageDescriptorSampledImages",
    "maxDescriptorSetSampledImages",
]


def dump() -> str:
    if shutil.which("vulkaninfo") is None:
        print("vulkaninfo not found; install vulkan-tools", file=sys.stderr)
        raise SystemExit(2)
    proc = subprocess.run(["vulkaninfo"], capture_output=True, text=True)
    if proc.returncode != 0 and not proc.stdout:
        print(proc.stderr, file=sys.stderr)
        raise SystemExit(2)
    return proc.stdout


def find(text: str, name: str) -> str | None:
    """Return the value reported for a feature or limit, if any."""
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped.startswith(name):
            continue
        rest = stripped[len(name):].lstrip()
        if rest.startswith("="):
            return rest[1:].strip()
    return None


def main(argv: list[str]) -> int:
    allow: set[str] = set()
    for arg in argv[1:]:
        if arg.startswith("--allow-missing"):
            _, _, value = arg.partition("=")
            allow.update(v.strip() for v in value.split(",") if v.strip())

    text = dump()

    device = find(text, "deviceName") or "<unknown>"
    api = find(text, "apiVersion") or "<unknown>"
    print(f"device : {device}")
    print(f"api    : {api}\n")

    missing: list[str] = []

    print("required (Vulkan 1.3 core):")
    for name in REQUIRED_FEATURES:
        value = find(text, name)
        ok = value is not None and value.lower().startswith("true")
        print(f"  {'OK  ' if ok else 'MISS'}  {name} = {value}")
        if not ok and name not in allow:
            missing.append(name)

    print("\noptional (runtime-detected, static fallback exists):")
    for name in OPTIONAL_FEATURES:
        value = find(text, name)
        ok = value is not None and value.lower().startswith("true")
        print(f"  {'OK  ' if ok else '--  '}  {name} = {value}")

    print("\nlimits:")
    for name in INTERESTING_LIMITS:
        print(f"  {name} = {find(text, name)}")

    sets = find(text, "maxBoundDescriptorSets")
    if sets is not None and sets.isdigit() and int(sets) < 11:
        print(
            f"\nnote: maxBoundDescriptorSets = {sets} (< 11). Upstream rd-vulkan would "
            "silently fall back to useFastLight here and disable PBR without a message. "
            "Until bindless lands, the renderer must fail loudly instead."
        )

    if missing:
        print(f"\nFAILED: {len(missing)} required feature(s) missing: {', '.join(missing)}")
        return 1

    print("\nOK: Vulkan 1.3 baseline satisfied")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
