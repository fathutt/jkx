#!/usr/bin/env python3
"""Print a TGA's width and height, space separated.

Small on purpose. Every other checker here answers a question about the pixels;
this one answers a question about how many of them there are, which is what a
window resize changes and what no pixel check can see - a frame at the wrong
size is still a perfectly good picture of the right scene.
"""

import struct
import sys


def main():
    if len(sys.argv) != 2:
        print("usage: tga_size.py FILE", file=sys.stderr)
        return 2

    with open(sys.argv[1], "rb") as f:
        header = f.read(18)

    if len(header) < 18:
        print("not a TGA: %s" % sys.argv[1], file=sys.stderr)
        return 2

    width, height = struct.unpack_from("<HH", header, 12)
    print("%d %d" % (width, height))
    return 0


if __name__ == "__main__":
    sys.exit(main())
