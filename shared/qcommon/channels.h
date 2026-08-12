/*
===========================================================================
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

// These entries are now also duplicated in ModView, so tell me if you need any adding or removing.
//  Note that the order is ok to change, I only read/write text strings of them anyway, but tell me if there
//	are different choices to offer in the pulldown box. I know, it's tacky, but ModView wasn't planned as an
//	editor and this was never an external file. A great combination...   - Ste.
//
// Sound channels, engine side.
//
// This moved here from code/game because that is not what it is. The engine
// picks channels, mixes them and attenuates them; the gamecode names one when
// it starts a sound. q_shared.h has always been the file that included it, and
// q_shared.h is the bottom of the tree.
//
// codeJK2 carried its own copy, and the two had drifted: this list has
// CHAN_VOICE_GLOBAL fifth and CHAN_MUSIC last, that one had CHAN_MENU1 and
// CHAN_VOICE_GLOBAL last, so every channel from the sixth onwards was a
// different number in the two files. Nothing included it - both games reach
// this list through q_shared.h, and always did - so nothing was ever wrong at
// run time. It was a copy that disagreed with the one in use and looked
// authoritative, which is the kind of file that is only ever read once, in the
// wrong direction. Deleted.

typedef enum //# soundChannel_e
{
	CHAN_AUTO,	//## %s !!"W:\game\base\!!sound\*.wav;*.mp3" # Auto-picks an empty channel to play sound on
	CHAN_LOCAL,	//## %s !!"W:\game\base\!!sound\*.wav;*.mp3" # menu sounds, etc
	CHAN_WEAPON,//## %s !!"W:\game\base\!!sound\*.wav;*.mp3"
	CHAN_VOICE, //## %s !!"W:\game\base\!!sound\voice\*.wav;*.mp3" # Voice sounds cause mouth animation
	CHAN_VOICE_ATTEN, //## %s !!"W:\game\base\!!sound\voice\*.wav;*.mp3" # Causes mouth animation but still use normal sound falloff
	CHAN_VOICE_GLOBAL,  //## %s !!"W:\game\base\!!sound\voice\*.wav;*.mp3" # Causes mouth animation and is broadcast with no separation
	CHAN_ITEM,  //## %s !!"W:\game\base\!!sound\*.wav;*.mp3"
	CHAN_BODY,	//## %s !!"W:\game\base\!!sound\*.wav;*.mp3"
	CHAN_AMBIENT,//## %s !!"W:\game\base\!!sound\*.wav;*.mp3" # added for ambient sounds
	CHAN_LOCAL_SOUND,	//## %s !!"W:\game\base\!!sound\*.wav;*.mp3" #chat messages, etc
	CHAN_ANNOUNCER,		//## %s !!"W:\game\base\!!sound\*.wav;*.mp3" #announcer voices, etc
	CHAN_LESS_ATTEN,	//## %s !!"W:\game\base\!!sound\*.wav;*.mp3" #attenuates similar to chan_voice, but uses empty channel auto-pick behaviour
	CHAN_MUSIC,	//played as a looping sound - added by BTO (VV)
} soundChannel_t;

