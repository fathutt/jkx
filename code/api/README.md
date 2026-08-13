code/api - the contract between the engine and the games
========================================================

Three headers, and they belong to neither side.

	g_public.h	what the server and the game module promise each other
	cg_public.h	the client game's system calls
	ui_public.h	what the engine calls in the menu system

Everything here is included by both the engine and the gamecode. That is the
whole entry requirement, and it is why the directory exists: a header describing
a boundary cannot live on one side of it. These three lived in code/game,
code/cgame and code/ui, so every engine build had to reach up into the gamecode
to find out what the gamecode had promised - and the layering gate had to carry
that as inherited debt rather than refuse it.

Now it can refuse it. code/api is its own layer in tools/ci/check_layering.py:
anyone may include it, and it may include the engine and the platform and
nothing else. An engine file that includes code/game again is a new violation
with no baseline entry to hide behind, which is what this directory was for.

What is not here
----------------

bg_public.h is not here, and that is the test this directory has to keep
passing. It is named like an interface and it is not one: 765 lines of weapon
events, animation frames and entity flags, different in the two games, and the
engine referenced seven of its 432 names - four of them only inside comments.
It is gamecode with a public-sounding filename.

The same for weapons.h, g_items.h and teams.h, and the same for
surfaceflags.h, channels.h and statindex.h in the other direction - those three
are read by q_shared.h, so they are engine, and they are in shared/qcommon.

The rule that sorts them: if only one side would ever change it, it is not a
contract. Put it on that side.

How big it is
-------------

tools/ci/check_interface.py counts these three headers, transitively, and holds
the total under a ceiling that may be lowered and not raised. It was 13,139
lines through 13 include sites when the count started, and it is what it says on
the tin now. The directory does not make the number smaller; it makes the number
the only thing left to argue about.
