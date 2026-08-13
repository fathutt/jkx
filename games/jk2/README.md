# Jedi Outcast gamecode

The single-player gamecode for Jedi Knight II: Jedi Outcast - game, cgame and
its own copy of ICARUS. It builds into `jk2gameARCH`, which the engine loads
from beside itself.

    -DBuildJK2Game=ON     this library
    -DBuildJK2Engine=ON   the engine to run it, jkx_jk2

Both are ON by default. The engine is not a second engine: it is code/ built
with -DJK2_MODE, so the only thing in this directory is what the two games
disagree about.

The retail `jk2gamex86.dll` is not supported and cannot be, the same as Jedi
Academy's - GAME_API_VERSION and the whole of game_import_t have moved out from
under it. See code/api.
