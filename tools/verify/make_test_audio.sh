#!/bin/sh
# Regenerates the compressed-audio fixtures that tests/snd_codec_test.cpp reads.
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
dir="$(dirname "$0")/fixtures"

ffmpeg -f lavfi -i "sine=frequency=440:sample_rate=22050:duration=1.0" \
       -ac 1 -b:a 32k -write_xing 0 -id3v2_version 0 -y "$dir/tone.mp3"

ffmpeg -f lavfi -i "sine=frequency=440:sample_rate=22050:duration=1.0" \
       -ac 1 -c:a libvorbis -b:a 48k -y "$dir/tone.ogg"

# The same second of tone, but carrying a Xing header whose frame count says
# zero. Every .mp3 the retail games ship is like this, and dr_mp3 reads that
# header at init and believes it - so asking for the length gave zero, and a
# sound of zero length does not load. Four hundred and eighty seven of them,
# including every line of dialogue, and none of it visible here until a file
# with the defect was in the fixtures.
#
# ffmpeg will not write a false count, so the count is written to zero
# afterwards. The audio is untouched: what is being tested is that the engine
# does not believe the header.
ffmpeg -f lavfi -i "sine=frequency=440:sample_rate=22050:duration=1.0" \
       -ac 1 -b:a 32k -y "$dir/tone_zerocount.mp3"

python3 - "$dir/tone_zerocount.mp3" <<'PATCH'
import sys
path = sys.argv[1]
data = bytearray(open(path, "rb").read())
at = data.find(b"Info")           # ffmpeg writes Info for constant bitrate
if at < 0:
    at = data.find(b"Xing")
if at < 0:
    raise SystemExit("no Xing/Info header to falsify - encoder changed?")
if not int.from_bytes(data[at + 4:at + 8], "big") & 1:
    raise SystemExit("the header carries no frame count to falsify")
data[at + 8:at + 12] = (0).to_bytes(4, "big")
open(path, "wb").write(bytes(data))
PATCH

# Music is stereo at 44,100 Hz and everything else in these fixtures is mono at
# 22,050, which is how a whole class of arithmetic stayed invisible: every offset
# the streaming window works in is a count of OUTPUT frames, and a mono frame is
# two bytes while a stereo one is four. A test that only ever sees two-byte
# frames cannot tell the two apart, and neither could the test that was here.
#
# Four seconds, so the 50,000-byte window - which holds 0.28 seconds of stereo -
# scrolls a dozen times while the test walks it. Different tones in the two
# channels, so a downmix, a swap or a half-speed read is a wrong frequency in a
# named channel rather than a vague failure.
ffmpeg -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=4.0" \
       -f lavfi -i "sine=frequency=660:sample_rate=44100:duration=4.0" \
       -filter_complex "[0:a][1:a]amerge=inputs=2[a]" -map "[a]" \
       -ac 2 -b:a 64k -write_xing 0 -id3v2_version 0 -y "$dir/tone_stereo.mp3"

# The same four seconds as Vorbis. Both formats matter here for a reason the
# MP3 alone does not show: a memory-backed MP3 decoder holds no allocation of
# its own and so survives being copied byte for byte, while a Vorbis decoder is
# a pointer to a heap object and does not. The stream-cloning checks only
# discriminate when they are run against this one.
ffmpeg -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=4.0" \
       -f lavfi -i "sine=frequency=660:sample_rate=44100:duration=4.0" \
       -filter_complex "[0:a][1:a]amerge=inputs=2[a]" -map "[a]" \
       -ac 2 -c:a libvorbis -b:a 80k -y "$dir/tone_stereo.ogg"

echo "wrote $dir/tone.mp3, $dir/tone.ogg, $dir/tone_zerocount.mp3, $dir/tone_stereo.mp3 and $dir/tone_stereo.ogg"
