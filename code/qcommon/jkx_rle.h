/*
===========================================================================
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

#pragma once

// The run-length coding a savegame chunk is stored in.
//
// Lifted out of SavedGame so that it can be handed malformed input by a test,
// which is the only way to find out what it does with any - and what it did was
// three different overflows, from a file that players send each other.
//
//   - The source index was never bounded. The loop runs until the OUTPUT is
//     full, so a stream that ends early keeps reading: src_buffer[src_index++]
//     on a std::vector, whose operator[] checks nothing, walks off the end of
//     the compressed buffer for as long as the output has room.
//   - The destination index was never bounded either. A run count is up to 127
//     and the only thing compared against the space left is the count AFTER it
//     has been written, so one byte of room and a count of 127 writes 126 bytes
//     past the end of the output - with bytes out of the file.
//   - A count of zero hangs. Neither branch runs, neither index advances except
//     the source one, and remain_size never changes: an infinite loop reading
//     further and further out of bounds. One zero byte anywhere in the stream.
//
// And a fourth, quieter: the count is an int8_t and negating -128 does not fit
// in one, so a run of that length went into uninitialized_copy_n as a negative
// count.
//
// The format is unchanged - this reads and writes exactly what the old code
// did, which the round trip in the test holds it to. What changed is that it
// answers false instead of walking off a buffer.

#include <stddef.h>

// Decode srcLen bytes into exactly dstLen bytes. Answers false when the input
// runs out, when it asks to write more than dstLen, or when it contains a zero
// count - in which case dst holds whatever was decoded before that point and
// nothing outside it has been touched.
bool RLE_Decode( const unsigned char *src, size_t srcLen,
	unsigned char *dst, size_t dstLen );

// The largest an encoding of srcLen bytes can be, which is what the encoder's
// output buffer has to be sized to.
size_t RLE_MaxEncodedSize( size_t srcLen );

// Encode srcLen bytes. Answers the number of bytes written, or 0 when dstLen is
// smaller than RLE_MaxEncodedSize( srcLen ).
size_t RLE_Encode( const unsigned char *src, size_t srcLen,
	unsigned char *dst, size_t dstLen );
