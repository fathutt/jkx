#!/usr/bin/env python3
"""Every lane in the local run has a step in CI, and the pairing is written down.

The rule was already in the notes - a lane that lives on one machine stops
existing along with that machine - and seven lanes did not follow it. Three of
them measured things nothing else measures: the interface at 32:9, a game saved
and loaded, and the engine started with no material definitions at all. Nobody
noticed, because the local run is green and the local run is the one a person
watches.

Pairing them by parsing environment blocks was tried first and is worse than it
looks: local.sh sets some of its variables from shell arguments, several lanes
share a setting with a different lane, and a rule loose enough to pair them all
is loose enough to pair the wrong ones. So the pairing is a table, kept here.
The check is then mechanical and hard to fool: a stage with no entry fails, an
entry naming a step that is not in the workflow fails.

Adding a lane means adding a line here. That is the point - it is the moment
when "and put it in CI too" is either done or visibly not done.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LOCAL = os.path.join(ROOT, "tools", "ci", "local.sh")
WORKFLOW = os.path.join(ROOT, ".github", "workflows", "ci.yml")

# Local stage -> the name of the CI step that runs the same thing.
# None means the stage is a build or a tool run rather than a lane, and the
# reason is in the comment beside it.
PAIRS = {
    "policy":               "Rule 1 - code is Latin-only",
    "release":              "Configure",
    "debug":                "Configure",
    "variants":             "Configure without the upload queue",
    "windows":              "Configure",
    "sanitizers":           "Build with sanitizers",
    "tests":                "shader pak reader",
    "smoke":                "The engine draws on the Vulkan renderer",
    "smokewide":            "The same frames, at 32:9",
    "smokejk2":             "The same frames, as Jedi Outcast",
    "smokesave":            "A game saved and loaded",
    "smokeskin":            "A skin that will not register",
    "smokeskinshift":       "Skin handles that do not shift",
    "smokelightmap":        "A map lit by its lightmap",
    "smokehdrlightmap":     "The world lit by an external HDR lightmap",
    "smokegamecmd":         "A console command reaching the game",
    "smokeshadow":          "No polygon reaches the renderer without a material",
    "smokecloud":           "An unimplemented entity type does not drop the level",
    "smokemapent":          "A model the map owns",
    "smokemenumodel":       "A model in a menu",
    "smokemenulight":       "A model in a menu, lit",
    "smokepbrchar":         "A character with a normal map",
    "smokevidrestart":      "The renderer torn down and rebuilt",
    "smokevsync":           "Vsync changed without a restart",
    "smokeresize":          "The window resized without a restart",
    "smokemsaa":            "The sample count changed without a restart",
    "smokedglow":           "Dynamic glow changed without a restart",
    "smokepicmip":          "Texture settings changed without a restart",
    "smokecubemap":         "Cubemaps generated and convolved",
    "smoketransparency":    "Blending, against a backdrop of a known colour",
    "smokesoftparticles":   "Soft particles",
    "smokewater":           "Water",
    "smokeweather":         "Rain",
    "smoketcmod":           "Texture coordinates, moved and not moved",
    "smokedeform":          "Geometry deformed, and a control that must not move",
    "smokephys":            "Physical maps, unpacked six ways",
    "smokephysshaded":      "Physical maps, shaded",
    "smokepak":             "The same frames, out of an archive",
    "move":                 "The player moves",
    "smokesan":             "The same frames, under the sanitizers",
    "prepass":              "The depth prepass changes nothing",
    "fog":                  "Fog",
    "noassets":             "No assets at all",
    "badbsp":               "A map file broken on purpose",
}


def local_stages():
    text = open(LOCAL, encoding="utf-8").read()
    match = re.search(r"^    STAGES=\(([^\n]*)\)", text, re.M)
    if not match:
        raise SystemExit("local.sh: could not find the default STAGES list")
    return match.group(1).split()


def workflow_steps():
    text = open(WORKFLOW, encoding="utf-8").read()
    return set(re.findall(r"^      - name: (.+)$", text, re.M))


def main():
    stages = local_stages()
    steps = workflow_steps()
    bad = 0

    for stage in stages:
        if stage not in PAIRS:
            print("%s runs locally and this table does not say what runs it in CI."
                  % stage)
            print("  Add it to PAIRS in tools/ci/check_lanes.py, naming the step.")
            bad += 1
            continue
        step = PAIRS[stage]
        if step not in steps:
            print("%s is paired with a CI step called %r, and no step has that name."
                  % (stage, step))
            bad += 1

    for stage in PAIRS:
        if stage not in stages:
            print("%s is in this table and not in local.sh's stage list." % stage)
            bad += 1

    if bad:
        print()
        print("A lane that lives on one machine stops existing along with it.")
        return 1

    print("checked %d stage(s), each one paired with a CI step" % len(stages))
    return 0


if __name__ == "__main__":
    sys.exit(main())
