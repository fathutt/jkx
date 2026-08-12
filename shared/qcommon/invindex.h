/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// Filename:	invindex.h
//
// accessed from both server and game modules
//
// Same shape as statindex.h and persindex.h: playerState_t carries an inventory
// array, q_shared.h sizes it - MAX_INVENTORY, with the comment "See INV_MAX" -
// and the entries are named by an enumeration that lived in g_items.h next to
// four hundred lines of pickup models, respawn times and item spawn functions.
//
// The server's "give inventory" walks that array and so has to know how far to
// walk, which is the whole of its interest in g_items.h. It was including the
// file twice over for it, in sv_ccmds.cpp and in sv_savegame.cpp, and in
// sv_savegame.cpp it turned out to use nothing from it at all.
//
// The enumeration is identical in the two games. One copy, then, and it lives in
// the engine layer next to q_math.h: the engine may not include the gamecode, and
// this is something both sides need. q_shared.h pulls it in beside MAX_INVENTORY.

#ifndef INVINDEX_H
#define INVINDEX_H


// player_state->inventory[] indexes
enum //# item_e
{
	INV_ELECTROBINOCULARS,
	INV_BACTA_CANISTER,
	INV_SEEKER,
	INV_LIGHTAMP_GOGGLES,
	INV_SENTRY,
	//# #eol
	INV_GOODIE_KEY,	// don't want to include keys in the icarus list
	INV_SECURITY_KEY,

	INV_MAX						// Be sure to update MAX_INVENTORY
};


#endif	// #ifndef INVINDEX_H


/////////////////////// eof /////////////////////
