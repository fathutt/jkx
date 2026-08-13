#!/bin/sh
# Regenerates the compressed-audio fixture that tests/snd_codec_test.cpp reads.
#
# One second of a 440 Hz sine at 22,050 Hz, mono, 32 kbit/s: small enough to
# commit, long enough that the decode window has to scroll several times while
# the test walks it.
#
# -write_xing 0 and -id3v2_version 0 keep the file to bare MPEG frames, so the
# test also covers the case with no seek table and no tag - which is the harder
# one for a decoder to report a length for, and the one the sniffing code has
# to recognise without help.
#
# The result is committed rather than built, because a machine running the
# tests has no other reason to have an encoder on it.
set -e
out="$(dirname "$0")/fixtures/tone.mp3"
ffmpeg -f lavfi -i "sine=frequency=440:sample_rate=22050:duration=1.0" \
       -ac 1 -b:a 32k -write_xing 0 -id3v2_version 0 -y "$out"
echo "wrote $out"
