/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// The Outcast-to-Academy bone remap, and the question of when to apply it.
//
// A .glm stores bone INDICES, so an Outcast character mesh played on Academy's
// renumbered skeleton has to have every reference translated through a table of
// 72 numbers. Getting either half of that wrong - the table, or the decision to
// use it - folds the character up, and neither half is visible in a screenshot
// until it is already wrong.
//
// The decision is what this mostly checks. Academy asked whether the mesh named
// 72 bones and whether the string "_humanoid" appeared in the name of its
// skeleton; the engine here is built once for both games, so in Jedi Outcast
// that test said yes to an Outcast mesh sitting on the Outcast skeleton it was
// authored against and remapped it onto itself. The case is the first one
// below, and it is the reason this file exists.
//
// No renderer, no window, no model files. A table and two integers.

#include "../code/rd-vulkan/tr_ghoul2_bonemap.h"

#include <cstdio>

namespace
{

int g_failures = 0;
int g_checks = 0;

void check( bool condition, const char *what )
{
	g_checks++;
	if ( !condition ) {
		g_failures++;
		printf( "FAIL: %s\n", what );
	}
}

// The Academy humanoid skeleton, whose size the table was written against. Not
// a number this code gets to choose: it is however many bones the table's
// outputs need, which the last case below states as an invariant rather than
// taking on trust.
const int iJA_HUMANOID_BONES = 53;


void testTheDecision()
{
	// Outcast mesh on the Outcast skeleton. THIS is the one the old test got
	// wrong: both are 72, the indices already mean what the skeleton says, and
	// remapping them onto themselves is how every humanoid in Jedi Outcast came
	// out folded up.
	check( !R_NeedsJOBoneRemap( iJO_HUMANOID_BONES, iJO_HUMANOID_BONES ),
		"an Outcast mesh on the Outcast skeleton must be left alone" );

	// Outcast mesh on the Academy skeleton. The case the table is for.
	check( R_NeedsJOBoneRemap( iJO_HUMANOID_BONES, iJA_HUMANOID_BONES ),
		"an Outcast mesh on the Academy skeleton must be remapped" );

	// Academy mesh on the Academy skeleton, which is the ordinary case and by
	// far the most common one.
	check( !R_NeedsJOBoneRemap( iJA_HUMANOID_BONES, iJA_HUMANOID_BONES ),
		"an Academy mesh on the Academy skeleton must be left alone" );

	// Anything else. A mesh built for a 40-bone skeleton has indices this table
	// knows nothing about, whatever it is bound to - the table's domain is the
	// Outcast humanoid and only that.
	check( !R_NeedsJOBoneRemap( 40, iJA_HUMANOID_BONES ),
		"a mesh built for some other skeleton is not the table's business" );
	check( !R_NeedsJOBoneRemap( 40, 40 ),
		"nor is one that matches some other skeleton" );

	// A 72-bone skeleton that is not the Outcast humanoid cannot be told from
	// one that is, and the answer is the same either way: the mesh matches what
	// it is bound to, so nothing is translated. The old test would have
	// remapped this one whenever the folder name happened to contain the
	// string, which is the other direction the asset name got it wrong in.
	check( !R_NeedsJOBoneRemap( 72, 72 ),
		"a custom 72-bone skeleton and its own mesh agree" );
}


void testTheTable()
{
	int	i;
	int	collapsed = 0;
	int	seen[iJA_HUMANOID_BONES] = { 0 };

	// Every entry has to land inside the Academy skeleton. This is what makes
	// the remapped index safe to hand to the skinning path, which reads it
	// every frame and does not check it.
	for ( i = 0; i < iJO_HUMANOID_BONES; i++ ) {
		const int mapped = R_RemapJOBone( i, iJA_HUMANOID_BONES );

		if ( mapped < 0 || mapped >= iJA_HUMANOID_BONES ) {
			check( false, "every Outcast bone maps into the Academy skeleton" );
			printf( "  bone %i maps to %i\n", i, mapped );
			break;
		}
		seen[mapped]++;
	}

	// And it has to need all of them. If the table's largest output were below
	// 52 the constant above would be wrong, and nothing else in this file would
	// notice - every case would still pass, against a skeleton declared bigger
	// than the table can reach.
	check( seen[iJA_HUMANOID_BONES - 1] > 0,
		"the table reaches the last bone of the Academy skeleton" );

	// The compression is real: Outcast had separate bones for things Academy
	// merged - the toes into the ankles, the finger joints into one bone per
	// finger - so 72 indices arrive at 50 distinct ones.
	for ( i = 0; i < iJA_HUMANOID_BONES; i++ ) {
		if ( seen[i] > 1 ) {
			collapsed += seen[i] - 1;
		}
	}
	check( collapsed == 22, "22 of the 72 Outcast bones are merged away" );

	// Three Academy bones have nothing pointing at them, because Outcast had no
	// equivalent: the two tails and the left hanging tag. A remapped Outcast
	// character therefore has nothing attached to them, which is correct and
	// worth stating so that a future edit to the table has to think about it.
	check( seen[49] == 0 && seen[50] == 0 && seen[51] == 0,
		"ltail, rtail and lhang_tag_bone have no Outcast equivalent" );
}


void testTheRefusals()
{
	// An index out of the table's domain comes from a file, and the answer is
	// "no", not a read past the end of the table.
	check( R_RemapJOBone( -1, iJA_HUMANOID_BONES ) < 0,
		"a negative bone reference is refused" );
	check( R_RemapJOBone( iJO_HUMANOID_BONES, iJA_HUMANOID_BONES ) < 0,
		"a bone reference past the end of the Outcast skeleton is refused" );

	// And a skeleton too small to hold the result is refused rather than
	// indexed into. The table was written against one particular skeleton; a
	// custom one of the wrong size is not that skeleton.
	check( R_RemapJOBone( 71, iJA_HUMANOID_BONES - 1 ) < 0,
		"a skeleton too small for the table's last output is refused" );
	check( R_RemapJOBone( 0, 1 ) == 0,
		"...but a reference the small skeleton can hold still works" );
}

}	// namespace


int main( void )
{
	testTheDecision();
	testTheTable();
	testTheRefusals();

	printf( "ghoul2 bone remap: %i check(s), %i failure(s)\n", g_checks, g_failures );
	return g_failures ? 1 : 0;
}
