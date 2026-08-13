# Vendored dependencies

Everything here is someone else's source, kept in the tree at a pinned version.
Section 12.4 of [`docs/CODING-STANDARDS.md`](../docs/CODING-STANDARDS.md) says
this file exists; it did not, so the rule was true of nothing. It is now.

Nothing in this directory is linted, formatted or checked by the gates in
`tools/ci`: `check_ascii.py`, `check_msvc.py`, `check_sources.py` and
`check_strings.py` all skip it. That is deliberate - it is not ours to change,
and a local edit here is a fork we would have to carry forever.

## The licence question

The whole tree is **GPLv2 without the "or later" clause**, inherited from the
Raven/Activision release. That is a narrower constraint than plain GPLv2:
without "or later" we cannot move a file to GPLv3, so anything vendored has to
be compatible with GPLv2 *specifically*.

Public domain, Unlicense, MIT, MIT-0 and BSD all are. **Apache-2.0 is not** -
its patent-termination clause is incompatible with GPLv2, and the usual
resolution ("upgrade to GPLv3") is exactly the one we do not have. Check before
vendoring, not after.

## What is here

| Directory | Version | Licence | Used by |
|---|---|---|---|
| `dr_libs/` | dr_mp3 0.7.4 | Unlicense **or** MIT-0, at our choice | MP3 decoding, `code/client/snd_codec.cpp` |
| `gsl-lite/` | 0.41.0 | MIT | `shared/qcommon/safe/`, engine and both games |
| `jpeg-9a/` | 9a, 19 Jan 2014 | IJG (BSD-like) | texture loading |
| `libpng/` | 1.6.53 | libpng (BSD-like) | texture loading |
| `minizip/` | 1.1, Feb 2010 | zlib | `.pk3` reading |
| `vma/` | 3.4.0 | MIT | Vulkan memory allocation |
| `volk/` | header 359 | MIT | Vulkan entry point loading |
| `zlib/` | 1.2.8 | zlib | deflate, and under minizip |

## Updating one

Replace the files, update the version in the table above, and say in the commit
message what changed and why. A dependency that moves without a line in the log
is a dependency nobody can reason about later.

`jpeg-9a`, `libpng`, `zlib` and `minizip` are built from source only when the
matching `UseInternal*` option is on - by default the system or vcpkg copies are
used, and these are the fallback. `gsl-lite`, `vma`, `volk` and `dr_libs` are
header-only or single-file and are always compiled in.
