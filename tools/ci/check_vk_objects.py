#!/usr/bin/env python3
"""Every Vulkan object the renderer creates, and whether anything destroys it.

An object still alive when vkDestroyDevice runs is a defect the compiler cannot
see and a software rasteriser will not complain about. The hardware does: the
report that started this names vkDestroyDevice, from two different callers, with
a read through a pointer of -1 inside the driver's own teardown.

The sweep is mechanical. A handle that reaches a vkCreate* and never a
vkDestroy* has nothing to argue about, and the first run of it found three:

    vk.compute_normalmap_pipeline
    vk.pipeline_layout_compute_normalmap
    vk.set_layout_compute_normalmap

made inside vk_initialize and destroyed nowhere - so a fresh set on every
vid_restart, and all of them alive at vkDestroyDevice.

What this does NOT see is worth writing down, because a check whose blind spots
are undocumented is a check that gets trusted too far:

  - anything created through VMA (vmaCreateImage, vmaCreateBuffer) or through
    this tree's own helpers rather than a bare vkCreate*;
  - anything destroyed by a wrapper rather than by a bare vkDestroy*;
  - objects destroyed on one path and not another. This asks whether a destroy
    exists at all, not whether it is reached.

So a green run here means "nothing is obviously abandoned", not "nothing leaks".

Usage:
    tools/ci/check_vk_objects.py [root]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

RENDERER = "code/rd-vulkan"

# vkCreateSomething( ..., &vk.handle ) - the last argument is the one written.
CREATE = re.compile(
    r'vkCreate(\w+)\s*\((?:[^;()]|\([^;()]*\))*?&\s*(vk\.[A-Za-z0-9_.\[\]]+)\s*\)',
    re.S,
)

# vkDestroySomething( vk.device, vk.handle, ... ) and the instance-level ones,
# which take vk.instance instead.
DESTROY = re.compile(
    r'vkDestroy(\w+)\s*\(\s*vk\.(?:device|instance)\s*,\s*(vk\.[A-Za-z0-9_.\[\]]+)'
)

# The two ends of the process, destroyed by their own calls rather than through
# a device or an instance, and the debug objects, which have EXT destroys.
NOT_CHILDREN = {
    "vk.device",
    "vk.instance",
    "vk.debug_callback",
    "vk.debug_utils_messenger",
}


def base(name: str) -> str:
    return name.split("[")[0]


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    renderer = root / RENDERER

    if not renderer.is_dir():
        print(f"no {RENDERER} under {root} - run this from the top of the repository",
              file=sys.stderr)
        return 2

    created: dict[str, list[str]] = {}
    destroyed: set[str] = set()

    for path in sorted(renderer.rglob("*")):
        if path.suffix not in {".cpp", ".c", ".h"} or not path.is_file():
            continue

        text = path.read_text(errors="replace")

        for match in CREATE.finditer(text):
            handle = base(match.group(2))
            line = text.count("\n", 0, match.start()) + 1
            created.setdefault(handle, []).append(
                f"{path.name}:{line}  vkCreate{match.group(1)}")

        for match in DESTROY.finditer(text):
            destroyed.add(base(match.group(2)))

    abandoned = sorted(
        h for h in created if h not in destroyed and h not in NOT_CHILDREN)

    if abandoned:
        print("error: created and never destroyed:")
        for handle in abandoned:
            print(f"  {handle}")
            for where in created[handle]:
                print(f"      {where}")
        print()
        print("Every one of these is alive when vkDestroyDevice runs, and a")
        print("second one is made on every vid_restart. Add the destroy on the")
        print("shutdown path beside the others, or - if the handle really is")
        print("owned somewhere this cannot see - say so in NOT_CHILDREN with")
        print("the reason.")
        return 1

    print(f"checked {len(created)} handle(s) created with vkCreate*, "
          f"{len(destroyed)} destroyed")
    print("OK: nothing is created without something to destroy it")
    return 0


if __name__ == "__main__":
    sys.exit(main())
