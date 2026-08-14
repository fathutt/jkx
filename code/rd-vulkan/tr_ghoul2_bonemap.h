/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

#pragma once

// A Jedi Outcast humanoid mesh, played on a Jedi Academy skeleton.
//
// A .glm stores, per vertex, the INDEX of the bone that moves it - not its
// name. The indices mean whatever the skeleton they were authored against says
// they mean, and Academy renumbered the humanoid skeleton: Outcast's had 72
// bones, Academy's has 53, with the fingers collapsed and the face moved.
// Playing an Outcast mesh on the Academy skeleton therefore needs every bone
// reference put through the table below, or the character folds up.
//
// The table is the whole of what Academy did about backward compatibility with
// its predecessor's character models, and it is reproduced here unchanged. The
// two skeletons it was derived from are written out at the bottom of this file,
// because the table is 72 numbers with no way to check them against anything.
//
// What changed here is HOW the case is recognised. Academy asked whether the
// mesh named 72 bones and whether the name of its skeleton contained the string
// "_humanoid":
//
//     if (mdxm->numBones == 72 && strstr(mdxm->animName, "_humanoid"))
//
// The string is an asset path from the retail game, and the test is wrong in
// both directions. A custom skeleton with 72 bones in a folder whose name
// happens to contain "_humanoid" gets remapped and should not. And the engine
// this file belongs to is built once and linked into BOTH games - the renderer
// does not know which one it is drawing - so in Jedi Outcast, where the mesh
// and the skeleton are both Outcast's and agree perfectly, the test fires and
// the engine remaps a model onto a skeleton it was already built for.
//
// The question the remap actually asks is not "is this asset called _humanoid".
// It is "do the indices in this mesh mean what the skeleton it is bound to says
// they mean". The mesh records how many bones its skeleton had; the skeleton
// says how many it has. When the mesh says 72 - the size of the table's domain,
// which is what makes the table applicable at all - and the skeleton it got is
// a different size, the indices are Outcast's and the skeleton is not. When
// both say 72 it is Outcast's mesh on Outcast's skeleton and nothing needs
// doing, which is the Outcast case the old test got wrong.
//
// No asset name appears in this decision.

// The size of the Outcast humanoid skeleton, and therefore the domain of the
// table: an index from a mesh built against it is 0..71 and nothing else.
#define iJK2_HUMANOID_BONES		72


// Does a mesh built for a skeleton of meshSkeletonBones bones, bound to a
// skeleton of skeletonBones bones, need its bone references remapped?
inline bool R_NeedsJK2BoneRemap( int meshSkeletonBones, int skeletonBones )
{
	return meshSkeletonBones == iJK2_HUMANOID_BONES
		&& skeletonBones != iJK2_HUMANOID_BONES;
}


// One bone reference, translated. Answers -1 when the index is not one this
// table can translate, which is a statement about the file rather than about
// the engine: the index came out of a .glm and nothing has checked it.
inline int R_RemapJK2Bone( int oldIndex, int skeletonBones );


/*

Some information used in the creation of the JK2 - JKA bone remap table

These are the old bones:
Complete list of all 72 bones:

*/

static const int OldToNewRemapTable[iJK2_HUMANOID_BONES] = {
0,// Bone 0:   "model_root":           Parent: ""  (index -1)
1,// Bone 1:   "pelvis":               Parent: "model_root"  (index 0)
2,// Bone 2:   "Motion":               Parent: "pelvis"  (index 1)
3,// Bone 3:   "lfemurYZ":             Parent: "pelvis"  (index 1)
4,// Bone 4:   "lfemurX":              Parent: "pelvis"  (index 1)
5,// Bone 5:   "ltibia":               Parent: "pelvis"  (index 1)
6,// Bone 6:   "ltalus":               Parent: "pelvis"  (index 1)
6,// Bone 7:   "ltarsal":              Parent: "pelvis"  (index 1)
7,// Bone 8:   "rfemurYZ":             Parent: "pelvis"  (index 1)
8,// Bone 9:   "rfemurX":	            Parent: "pelvis"  (index 1)
9,// Bone10:   "rtibia":	            Parent: "pelvis"  (index 1)
10,// Bone11:   "rtalus":	            Parent: "pelvis"  (index 1)
10,// Bone12:   "rtarsal":              Parent: "pelvis"  (index 1)
11,// Bone13:   "lower_lumbar":         Parent: "pelvis"  (index 1)
12,// Bone14:   "upper_lumbar":	        Parent: "lower_lumbar"  (index 13)
13,// Bone15:   "thoracic":	            Parent: "upper_lumbar"  (index 14)
14,// Bone16:   "cervical":	            Parent: "thoracic"  (index 15)
15,// Bone17:   "cranium":              Parent: "cervical"  (index 16)
16,// Bone18:   "ceyebrow":	            Parent: "face_always_"  (index 71)
17,// Bone19:   "jaw":                  Parent: "face_always_"  (index 71)
18,// Bone20:   "lblip2":	            Parent: "face_always_"  (index 71)
19,// Bone21:   "leye":		            Parent: "face_always_"  (index 71)
20,// Bone22:   "rblip2":	            Parent: "face_always_"  (index 71)
21,// Bone23:   "ltlip2":               Parent: "face_always_"  (index 71)
22,// Bone24:   "rtlip2":	            Parent: "face_always_"  (index 71)
23,// Bone25:   "reye":		            Parent: "face_always_"  (index 71)
24,// Bone26:   "rclavical":            Parent: "thoracic"  (index 15)
25,// Bone27:   "rhumerus":             Parent: "thoracic"  (index 15)
26,// Bone28:   "rhumerusX":            Parent: "thoracic"  (index 15)
27,// Bone29:   "rradius":              Parent: "thoracic"  (index 15)
28,// Bone30:   "rradiusX":             Parent: "thoracic"  (index 15)
29,// Bone31:   "rhand":                Parent: "thoracic"  (index 15)
29,// Bone32:   "mc7":                  Parent: "thoracic"  (index 15)
34,// Bone33:   "r_d5_j1":              Parent: "thoracic"  (index 15)
35,// Bone34:   "r_d5_j2":              Parent: "thoracic"  (index 15)
35,// Bone35:   "r_d5_j3":              Parent: "thoracic"  (index 15)
30,// Bone36:   "r_d1_j1":              Parent: "thoracic"  (index 15)
31,// Bone37:   "r_d1_j2":              Parent: "thoracic"  (index 15)
31,// Bone38:   "r_d1_j3":              Parent: "thoracic"  (index 15)
32,// Bone39:   "r_d2_j1":              Parent: "thoracic"  (index 15)
33,// Bone40:   "r_d2_j2":              Parent: "thoracic"  (index 15)
33,// Bone41:   "r_d2_j3":              Parent: "thoracic"  (index 15)
32,// Bone42:   "r_d3_j1":			    Parent: "thoracic"  (index 15)
33,// Bone43:   "r_d3_j2":		        Parent: "thoracic"  (index 15)
33,// Bone44:   "r_d3_j3":              Parent: "thoracic"  (index 15)
34,// Bone45:   "r_d4_j1":              Parent: "thoracic"  (index 15)
35,// Bone46:   "r_d4_j2":	            Parent: "thoracic"  (index 15)
35,// Bone47:   "r_d4_j3":		        Parent: "thoracic"  (index 15)
36,// Bone48:   "rhang_tag_bone":	    Parent: "thoracic"  (index 15)
37,// Bone49:   "lclavical":            Parent: "thoracic"  (index 15)
38,// Bone50:   "lhumerus":	            Parent: "thoracic"  (index 15)
39,// Bone51:   "lhumerusX":	        Parent: "thoracic"  (index 15)
40,// Bone52:   "lradius":	            Parent: "thoracic"  (index 15)
41,// Bone53:   "lradiusX":	            Parent: "thoracic"  (index 15)
42,// Bone54:   "lhand":	            Parent: "thoracic"  (index 15)
42,// Bone55:   "mc5":		            Parent: "thoracic"  (index 15)
43,// Bone56:   "l_d5_j1":	            Parent: "thoracic"  (index 15)
44,// Bone57:   "l_d5_j2":	            Parent: "thoracic"  (index 15)
44,// Bone58:   "l_d5_j3":	            Parent: "thoracic"  (index 15)
43,// Bone59:   "l_d4_j1":	            Parent: "thoracic"  (index 15)
44,// Bone60:   "l_d4_j2":	            Parent: "thoracic"  (index 15)
44,// Bone61:   "l_d4_j3":	            Parent: "thoracic"  (index 15)
45,// Bone62:   "l_d3_j1":	            Parent: "thoracic"  (index 15)
46,// Bone63:   "l_d3_j2":	            Parent: "thoracic"  (index 15)
46,// Bone64:   "l_d3_j3":	            Parent: "thoracic"  (index 15)
45,// Bone65:   "l_d2_j1":	            Parent: "thoracic"  (index 15)
46,// Bone66:   "l_d2_j2":	            Parent: "thoracic"  (index 15)
46,// Bone67:   "l_d2_j3":	            Parent: "thoracic"  (index 15)
47,// Bone68:   "l_d1_j1":				Parent: "thoracic"  (index 15)
48,// Bone69:   "l_d1_j2":	            Parent: "thoracic"  (index 15)
48,// Bone70:   "l_d1_j3":				Parent: "thoracic"  (index 15)
52// Bone71:   "face_always_":			Parent: "cranium"  (index 17)
};


/*

Bone   0:   "model_root":
            Parent: ""  (index -1)
            #Kids:  1
            Child 0: (index 1), name "pelvis"

Bone   1:   "pelvis":
            Parent: "model_root"  (index 0)
            #Kids:  4
            Child 0: (index 2), name "Motion"
            Child 1: (index 3), name "lfemurYZ"
            Child 2: (index 7), name "rfemurYZ"
            Child 3: (index 11), name "lower_lumbar"

Bone   2:   "Motion":
            Parent: "pelvis"  (index 1)
            #Kids:  0

Bone   3:   "lfemurYZ":
            Parent: "pelvis"  (index 1)
            #Kids:  3
            Child 0: (index 4), name "lfemurX"
            Child 1: (index 5), name "ltibia"
            Child 2: (index 49), name "ltail"

Bone   4:   "lfemurX":
            Parent: "lfemurYZ"  (index 3)
            #Kids:  0

Bone   5:   "ltibia":
            Parent: "lfemurYZ"  (index 3)
            #Kids:  1
            Child 0: (index 6), name "ltalus"

Bone   6:   "ltalus":
            Parent: "ltibia"  (index 5)
            #Kids:  0

Bone   7:   "rfemurYZ":
            Parent: "pelvis"  (index 1)
            #Kids:  3
            Child 0: (index 8), name "rfemurX"
            Child 1: (index 9), name "rtibia"
            Child 2: (index 50), name "rtail"

Bone   8:   "rfemurX":
            Parent: "rfemurYZ"  (index 7)
            #Kids:  0

Bone   9:   "rtibia":
            Parent: "rfemurYZ"  (index 7)
            #Kids:  1
            Child 0: (index 10), name "rtalus"

Bone  10:   "rtalus":
            Parent: "rtibia"  (index 9)
            #Kids:  0

Bone  11:   "lower_lumbar":
            Parent: "pelvis"  (index 1)
            #Kids:  1
            Child 0: (index 12), name "upper_lumbar"

Bone  12:   "upper_lumbar":
            Parent: "lower_lumbar"  (index 11)
            #Kids:  1
            Child 0: (index 13), name "thoracic"

Bone  13:   "thoracic":
            Parent: "upper_lumbar"  (index 12)
            #Kids:  5
            Child 0: (index 14), name "cervical"
            Child 1: (index 24), name "rclavical"
            Child 2: (index 25), name "rhumerus"
            Child 3: (index 37), name "lclavical"
            Child 4: (index 38), name "lhumerus"

Bone  14:   "cervical":
            Parent: "thoracic"  (index 13)
            #Kids:  1
            Child 0: (index 15), name "cranium"

Bone  15:   "cranium":
            Parent: "cervical"  (index 14)
            #Kids:  1
            Child 0: (index 52), name "face_always_"

Bone  16:   "ceyebrow":
            Parent: "face_always_"  (index 52)
            #Kids:  0

Bone  17:   "jaw":
            Parent: "face_always_"  (index 52)
            #Kids:  0

Bone  18:   "lblip2":
            Parent: "face_always_"  (index 52)
            #Kids:  0

Bone  19:   "leye":
            Parent: "face_always_"  (index 52)
            #Kids:  0

Bone  20:   "rblip2":
            Parent: "face_always_"  (index 52)
            #Kids:  0

Bone  21:   "ltlip2":
            Parent: "face_always_"  (index 52)
            #Kids:  0

Bone  22:   "rtlip2":
            Parent: "face_always_"  (index 52)
            #Kids:  0

Bone  23:   "reye":
            Parent: "face_always_"  (index 52)
            #Kids:  0

Bone  24:   "rclavical":
            Parent: "thoracic"  (index 13)
            #Kids:  0

Bone  25:   "rhumerus":
            Parent: "thoracic"  (index 13)
            #Kids:  2
            Child 0: (index 26), name "rhumerusX"
            Child 1: (index 27), name "rradius"

Bone  26:   "rhumerusX":
            Parent: "rhumerus"  (index 25)
            #Kids:  0

Bone  27:   "rradius":
            Parent: "rhumerus"  (index 25)
            #Kids:  9
            Child 0: (index 28), name "rradiusX"
            Child 1: (index 29), name "rhand"
            Child 2: (index 30), name "r_d1_j1"
            Child 3: (index 31), name "r_d1_j2"
            Child 4: (index 32), name "r_d2_j1"
            Child 5: (index 33), name "r_d2_j2"
            Child 6: (index 34), name "r_d4_j1"
            Child 7: (index 35), name "r_d4_j2"
            Child 8: (index 36), name "rhang_tag_bone"

Bone  28:   "rradiusX":
            Parent: "rradius"  (index 27)
            #Kids:  0

Bone  29:   "rhand":
            Parent: "rradius"  (index 27)
            #Kids:  0

Bone  30:   "r_d1_j1":
            Parent: "rradius"  (index 27)
            #Kids:  0

Bone  31:   "r_d1_j2":
            Parent: "rradius"  (index 27)
            #Kids:  0

Bone  32:   "r_d2_j1":
            Parent: "rradius"  (index 27)
            #Kids:  0

Bone  33:   "r_d2_j2":
            Parent: "rradius"  (index 27)
            #Kids:  0

Bone  34:   "r_d4_j1":
            Parent: "rradius"  (index 27)
            #Kids:  0

Bone  35:   "r_d4_j2":
            Parent: "rradius"  (index 27)
            #Kids:  0

Bone  36:   "rhang_tag_bone":
            Parent: "rradius"  (index 27)
            #Kids:  0

Bone  37:   "lclavical":
            Parent: "thoracic"  (index 13)
            #Kids:  0

Bone  38:   "lhumerus":
            Parent: "thoracic"  (index 13)
            #Kids:  2
            Child 0: (index 39), name "lhumerusX"
            Child 1: (index 40), name "lradius"

Bone  39:   "lhumerusX":
            Parent: "lhumerus"  (index 38)
            #Kids:  0

Bone  40:   "lradius":
            Parent: "lhumerus"  (index 38)
            #Kids:  9
            Child 0: (index 41), name "lradiusX"
            Child 1: (index 42), name "lhand"
            Child 2: (index 43), name "l_d4_j1"
            Child 3: (index 44), name "l_d4_j2"
            Child 4: (index 45), name "l_d2_j1"
            Child 5: (index 46), name "l_d2_j2"
            Child 6: (index 47), name "l_d1_j1"
            Child 7: (index 48), name "l_d1_j2"
            Child 8: (index 51), name "lhang_tag_bone"

Bone  41:   "lradiusX":
            Parent: "lradius"  (index 40)
            #Kids:  0

Bone  42:   "lhand":
            Parent: "lradius"  (index 40)
            #Kids:  0

Bone  43:   "l_d4_j1":
            Parent: "lradius"  (index 40)
            #Kids:  0

Bone  44:   "l_d4_j2":
            Parent: "lradius"  (index 40)
            #Kids:  0

Bone  45:   "l_d2_j1":
            Parent: "lradius"  (index 40)
            #Kids:  0

Bone  46:   "l_d2_j2":
            Parent: "lradius"  (index 40)
            #Kids:  0

Bone  47:   "l_d1_j1":
            Parent: "lradius"  (index 40)
            #Kids:  0

Bone  48:   "l_d1_j2":
            Parent: "lradius"  (index 40)
            #Kids:  0

Bone  49:   "ltail":
            Parent: "lfemurYZ"  (index 3)
            #Kids:  0

Bone  50:   "rtail":
            Parent: "rfemurYZ"  (index 7)
            #Kids:  0

Bone  51:   "lhang_tag_bone":
            Parent: "lradius"  (index 40)
            #Kids:  0

Bone  52:   "face_always_":
            Parent: "cranium"  (index 15)
            #Kids:  8
            Child 0: (index 16), name "ceyebrow"
            Child 1: (index 17), name "jaw"
            Child 2: (index 18), name "lblip2"
            Child 3: (index 19), name "leye"
            Child 4: (index 20), name "rblip2"
            Child 5: (index 21), name "ltlip2"
            Child 6: (index 22), name "rtlip2"
            Child 7: (index 23), name "reye"



*/


inline int R_RemapJK2Bone( int oldIndex, int skeletonBones )
{
	int newIndex;

	if ( oldIndex < 0 || oldIndex >= iJK2_HUMANOID_BONES ) {
		return -1;
	}

	newIndex = OldToNewRemapTable[oldIndex];

	// The table was written against one particular Academy skeleton. A mesh
	// remapped onto a smaller one would index past the end of it, and the bone
	// index is read every frame by the skinning path.
	if ( newIndex >= skeletonBones ) {
		return -1;
	}

	return newIndex;
}
