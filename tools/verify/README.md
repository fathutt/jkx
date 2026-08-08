# Verification run

One command. It launches the engine a few times with generated configs, times
each run, reads the console dumps, and writes `verification-report.md` here.

```
python tools/verify/verify.py --game "C:/Program Files (x86)/Steam/steamapps/common/Jedi Academy/GameData"
```

Linux:

```
python3 tools/verify/verify.py --game ~/.steam/steam/steamapps/common/Jedi\ Academy/GameData
```

Useful flags: `--map mp/ffa3`, `--runs 5`, `--binary <path>` if auto-detection
picks the wrong executable.

The engine exits on its own after each scenario. Nothing needs to be typed at
the console and nothing needs watching.

Three things still need a person, and the report ends with them: FPS against
rd-vanilla on the same scene, one RenderDoc capture, and whether new materials
cause hitching when they first appear.

Requires Python 3.9+ and nothing else.
