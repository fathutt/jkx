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

#include "qcommon/q_shared.h"
#include "g_local.h"


#include "g_functions.h"
#include "g_vehicles.h"
#include "../cgame/cg_local.h"


#define bgEntity_t gentity_t

extern gentity_t *NPC_Spawn_Do( gentity_t *pEnt, qboolean fullSpawnNow );
extern qboolean G_ClearLineOfSight(const vec3_t point1, const vec3_t point2, int ignore, int clipmask);

extern qboolean G_SetG2PlayerModelInfo( gentity_t *pEnt, const char *modelName, const char *customSkin, const char *surfOff, const char *surfOn );
extern void G_RemovePlayerModel( gentity_t *pEnt );
extern void G_ChangePlayerModel( gentity_t *pEnt, const char *newModel );
extern void G_RemoveWeaponModels( gentity_t *pEnt );
extern void CG_ChangeWeapon( int num );
extern float DotToSpot( vec3_t spot, vec3_t from, vec3_t fromAngles );
extern qboolean Q3_TaskIDPending( gentity_t *ent, taskID_t taskType );
extern void SetClientViewAngle( gentity_t *ent, vec3_t angle );

extern vmCvar_t	cg_thirdPersonAlpha;
extern vec3_t playerMins;
extern vec3_t playerMaxs;
extern cvar_t	*g_speederControlScheme;
extern cvar_t *in_joystick;
extern void PM_SetAnim(pmove_t	*pm,int setAnimParts,int anim,int setAnimFlags, int blendTime);
extern int PM_AnimLength( int index, animNumber_t anim );
extern void NPC_SetAnim(gentity_t	*ent,int setAnimParts,int anim,int setAnimFlags, int iBlend);
extern void G_Knockdown( gentity_t *self, gentity_t *attacker, const vec3_t pushDir, float strength, qboolean breakSaberLock );

extern void PM_SetTorsoAnimTimer( gentity_t *ent, int *torsoAnimTimer, int time );
extern void PM_SetLegsAnimTimer( gentity_t *ent, int *legsAnimTimer, int time );

extern qboolean BG_UnrestrainedPitchRoll( playerState_t *ps, Vehicle_t *pVeh );

void Vehicle_SetAnim(gentity_t *ent,int setAnimParts,int anim,int setAnimFlags, int iBlend)
{
	NPC_SetAnim(ent, setAnimParts, anim, setAnimFlags, iBlend);
}

void G_VehicleTrace( trace_t *results, const vec3_t start, const vec3_t tMins, const vec3_t tMaxs, const vec3_t end, int passEntityNum, int contentmask )
{
	gi.trace( results, start, tMins, tMaxs, end, passEntityNum, contentmask, (EG2_Collision)0, 0 );
}

Vehicle_t *G_IsRidingVehicle( gentity_t *pEnt )
{
	gentity_t *ent = (gentity_t *)pEnt;

	if ( ent && ent->client && ent->client->NPC_class != CLASS_VEHICLE && ent->s.m_iVehicleNum != 0 ) //ent->client && ( ent->client->ps.eFlags & EF_IN_VEHICLE ) && ent->owner )
	{
		return g_entities[ent->s.m_iVehicleNum].m_pVehicle;
	}
	return NULL;
}

bool	G_IsRidingTurboVehicle( gentity_t *pEnt )
{
	gentity_t *ent = (gentity_t *)pEnt;

	if ( ent && ent->client && ent->client->NPC_class != CLASS_VEHICLE && ent->s.m_iVehicleNum != 0 ) //ent->client && ( ent->client->ps.eFlags & EF_IN_VEHICLE ) && ent->owner )
	{
		return (level.time<g_entities[ent->s.m_iVehicleNum].m_pVehicle->m_iTurboTime);
	}
	return false;
}



float	G_CanJumpToEnemyVeh(Vehicle_t *pVeh, const usercmd_t *pUcmd )
{
	gentity_t*	rider = pVeh->m_pPilot;

	// If There Is An Enemy And We Are At The Same Z Height
	//------------------------------------------------------
	if (rider &&
		rider->enemy &&
		pUcmd->rightmove &&
		fabsf(rider->enemy->currentOrigin[2] - rider->currentOrigin[2])<50.0f)
	{
		if (level.time<pVeh->m_safeJumpMountTime)
		{
			return pVeh->m_safeJumpMountRightDot;
		}


		// If The Enemy Is Riding Another Vehicle
		//----------------------------------------
		Vehicle_t*	enemyVeh = G_IsRidingVehicle(rider->enemy);
		if (enemyVeh)
		{
			vec3_t	enemyFwd;
			vec3_t	toEnemy;
			float	toEnemyDistance;
			vec3_t	riderFwd;
			vec3_t	riderRight;
			float	riderRightDot;

			// If He Is Close Enough And Going The Same Speed
			//------------------------------------------------
			VectorSubtract(rider->enemy->currentOrigin, rider->currentOrigin, toEnemy);
		 	toEnemyDistance = VectorNormalize(toEnemy);
			if (toEnemyDistance<70.0f &&
				pVeh->m_pParentEntity->resultspeed>100.0f &&
				fabsf(pVeh->m_pParentEntity->resultspeed - enemyVeh->m_pParentEntity->resultspeed)<100.0f)
			{
				// If He Is Either To The Left Or Right Of Me
				//--------------------------------------------
				AngleVectors(rider->currentAngles, riderFwd, riderRight, 0);
				riderRightDot = DotProduct(riderRight, toEnemy);
				if ((pUcmd->rightmove>0 && riderRightDot>0.2) || (pUcmd->rightmove<0 &&riderRightDot<-0.2))
				{
					// If We Are Both Going About The Same Direction
					//-----------------------------------------------
					AngleVectors(rider->enemy->currentAngles, enemyFwd, 0, 0);
					if (DotProduct(enemyFwd, riderFwd)>0.2f)
					{
						pVeh->m_safeJumpMountTime = level.time + Q_irand(3000, 4000);	// Ok, now you get a 3 sec window
						pVeh->m_safeJumpMountRightDot = riderRightDot;
						return riderRightDot;
					}// Same Direction?
				}// To Left Or Right?
			}// Close Enough & Same Speed?
		}// Enemy Riding A Vehicle?
	}// Has Enemy And On Same Z-Height
	return 0.0f;
}

// Spawn this vehicle into the world.
void G_VehicleSpawn( gentity_t *self )
{
	float yaw;
	gentity_t *vehEnt;

	VectorCopy( self->currentOrigin, self->s.origin );

	gi.linkentity( self );

	if ( !self->count )
	{
		self->count = 1;
	}

	//save this because self gets removed in next func
	yaw = self->s.angles[YAW];

	vehEnt = NPC_Spawn_Do( self, qtrue );

	if ( !vehEnt )
	{
		return;//return NULL;
	}

	vehEnt->s.angles[YAW] = yaw;
	if ( vehEnt->m_pVehicle->m_pVehicleInfo->type != VH_ANIMAL )
	{
		vehEnt->NPC->behaviorState = BS_CINEMATIC;
	}

	if (vehEnt->spawnflags & 1)
	{ //die without pilot
		vehEnt->m_pVehicle->m_iPilotTime = level.time + vehEnt->endFrame;
	}
	//return vehEnt;
}

// Attachs an entity to the vehicle it's riding (it's owner).
void G_AttachToVehicle( gentity_t *pEnt, usercmd_t **ucmd )
{
	gentity_t		*vehEnt;
	mdxaBone_t		boltMatrix;
	gentity_t		*ent;

	if ( !pEnt || !ucmd )
		return;

	ent = (gentity_t *)pEnt;

	vehEnt = ent->owner;
	ent->waypoint = vehEnt->waypoint; // take the veh's waypoint as your own

	if ( !vehEnt->m_pVehicle )
		return;

	// Get the driver tag.
	gi.G2API_GetBoltMatrix( vehEnt->ghoul2, vehEnt->playerModel, vehEnt->crotchBolt, &boltMatrix,
							vehEnt->m_pVehicle->m_vOrientation, vehEnt->currentOrigin,
							(cg.time?cg.time:level.time), NULL, vehEnt->s.modelScale );
	gi.G2API_GiveMeVectorFromMatrix( boltMatrix, ORIGIN, ent->client->ps.origin );
	gi.linkentity( ent );
}

void G_KnockOffVehicle( gentity_t *pRider, gentity_t *self, qboolean bPull )
{
	Vehicle_t *pVeh = NULL;
	vec3_t riderAngles, fDir, rDir, dir2Me;
	float	fDot, rDot;

	if ( !pRider || !pRider->client )
	{
		return;
	}

	pVeh = G_IsRidingVehicle( pRider );

	if ( !pVeh || !pVeh->m_pVehicleInfo )
	{
		return;
	}

	VectorCopy( pRider->currentAngles, riderAngles );
	riderAngles[0] = 0;
	AngleVectors( riderAngles, fDir, rDir, NULL );
	VectorSubtract( self->currentOrigin, pRider->currentOrigin, dir2Me );
	dir2Me[2] = 0;
	VectorNormalize( dir2Me );
	fDot = DotProduct( fDir, dir2Me );
	if ( fDot >= 0.5f )
	{//I'm in front of them
		if ( bPull )
		{//pull them foward
			pVeh->m_EjectDir = VEH_EJECT_FRONT;
		}
		else
		{//push them back
			pVeh->m_EjectDir = VEH_EJECT_REAR;
		}
	}
	else if ( fDot <= -0.5f )
	{//I'm behind them
		if ( bPull )
		{//pull them back
			pVeh->m_EjectDir = VEH_EJECT_REAR;
		}
		else
		{//push them forward
			pVeh->m_EjectDir = VEH_EJECT_FRONT;
		}
	}
	else
	{//to the side of them
		rDot = DotProduct( fDir, dir2Me );
		if ( rDot >= 0.0f )
		{//to the right
			if ( bPull )
			{//pull them right
				pVeh->m_EjectDir = VEH_EJECT_RIGHT;
			}
			else
			{//push them left
				pVeh->m_EjectDir = VEH_EJECT_LEFT;
			}
		}
		else
		{//to the left
			if ( bPull )
			{//pull them left
				pVeh->m_EjectDir = VEH_EJECT_LEFT;
			}
			else
			{//push them right
				pVeh->m_EjectDir = VEH_EJECT_RIGHT;
			}
		}
	}
	//now forcibly eject them
	pVeh->m_pVehicleInfo->Eject( pVeh, pRider, qtrue );
}

void G_DrivableATSTDie( gentity_t *self )
{
}

void G_DriveATST( gentity_t *pEnt, gentity_t *atst )
{
	if ( pEnt->NPC_type && pEnt->client && (pEnt->client->NPC_class == CLASS_ATST) )
	{//already an atst, switch back
		//open hatch
		G_RemovePlayerModel( pEnt );
		pEnt->NPC_type = "player";
		pEnt->client->NPC_class = CLASS_PLAYER;
		pEnt->flags &= ~FL_SHIELDED;
		pEnt->client->ps.eFlags &= ~EF_IN_ATST;
		//size
		VectorCopy( playerMins, pEnt->mins );
		VectorCopy( playerMaxs, pEnt->maxs );
		pEnt->client->crouchheight = CROUCH_MAXS_2;
		pEnt->client->standheight = DEFAULT_MAXS_2;
		pEnt->s.radius = 0;//clear it so the G2-model-setting stuff will recalc it
		G_ChangePlayerModel( pEnt, pEnt->NPC_type );
		//G_SetG2PlayerModel( pEnt, pEnt->NPC_type, NULL, NULL, NULL );

		//FIXME: reset/4 their weapon
		pEnt->client->ps.stats[STAT_WEAPONS] &= ~(( 1 << WP_ATST_MAIN )|( 1 << WP_ATST_SIDE ));
		pEnt->client->ps.ammo[weaponData[WP_ATST_MAIN].ammoIndex] = 0;
		pEnt->client->ps.ammo[weaponData[WP_ATST_SIDE].ammoIndex] = 0;
		if ( pEnt->client->ps.stats[STAT_WEAPONS] & (1<<WP_BLASTER) )
		{
			CG_ChangeWeapon( WP_BLASTER );
			//camera
			if ( cg_gunAutoFirst.integer )
			{//go back to first person
				gi.cvar_set( "cg_thirdperson", "0" );
			}
		}
		else
		{
			CG_ChangeWeapon( WP_NONE );
		}
		cg.overrides.active &= ~(CG_OVERRIDE_3RD_PERSON_RNG|CG_OVERRIDE_3RD_PERSON_VOF|CG_OVERRIDE_3RD_PERSON_POF|CG_OVERRIDE_3RD_PERSON_APH);
		cg.overrides.thirdPersonRange = cg.overrides.thirdPersonVertOffset = cg.overrides.thirdPersonPitchOffset = 0;
		cg.overrides.thirdPersonAlpha = cg_thirdPersonAlpha.value;
		pEnt->client->ps.viewheight = pEnt->maxs[2] + STANDARD_VIEWHEIGHT_OFFSET;
		//pEnt->mass = 10;
	}
	else
	{//become an atst
		pEnt->NPC_type = "atst";
		pEnt->client->NPC_class = CLASS_ATST;
		pEnt->client->ps.eFlags |= EF_IN_ATST;
		pEnt->flags |= FL_SHIELDED;
		//size
		VectorSet( pEnt->mins, ATST_MINS0, ATST_MINS1, ATST_MINS2 );
		VectorSet( pEnt->maxs, ATST_MAXS0, ATST_MAXS1, ATST_MAXS2 );
		pEnt->client->crouchheight = ATST_MAXS2;
		pEnt->client->standheight = ATST_MAXS2;
		if ( !atst )
		{//no pEnt to copy from
			G_ChangePlayerModel( pEnt, "atst" );
			//G_SetG2PlayerModel( pEnt, "atst", NULL, NULL, NULL );
			NPC_SetAnim( pEnt, SETANIM_BOTH, BOTH_STAND1, SETANIM_FLAG_OVERRIDE, 200 );
		}
		else
		{
			G_RemovePlayerModel( pEnt );
			G_RemoveWeaponModels( pEnt );
			gi.G2API_CopyGhoul2Instance( atst->ghoul2, pEnt->ghoul2, -1 );
			pEnt->playerModel = 0;
			G_SetG2PlayerModelInfo( pEnt, "atst", NULL, NULL, NULL );
			//turn off hatch underside
			gi.G2API_SetSurfaceOnOff( &pEnt->ghoul2[pEnt->playerModel], "head_hatchcover", 0x00000002/*G2SURFACEFLAG_OFF*/ );
			G_Sound( pEnt, G_SoundIndex( "sound/chars/atst/atst_hatch_close" ));
		}
		pEnt->s.radius = 320;
		//weapon
		gitem_t	*item = FindItemForWeapon( WP_ATST_MAIN );	//precache the weapon
		CG_RegisterItemSounds( (item-bg_itemlist) );
		CG_RegisterItemVisuals( (item-bg_itemlist) );
		item = FindItemForWeapon( WP_ATST_SIDE );	//precache the weapon
		CG_RegisterItemSounds( (item-bg_itemlist) );
		CG_RegisterItemVisuals( (item-bg_itemlist) );
		pEnt->client->ps.stats[STAT_WEAPONS] |= ( 1 << WP_ATST_MAIN )|( 1 << WP_ATST_SIDE );
		pEnt->client->ps.ammo[weaponData[WP_ATST_MAIN].ammoIndex] = ammoData[weaponData[WP_ATST_MAIN].ammoIndex].max;
		pEnt->client->ps.ammo[weaponData[WP_ATST_SIDE].ammoIndex] = ammoData[weaponData[WP_ATST_SIDE].ammoIndex].max;
		CG_ChangeWeapon( WP_ATST_MAIN );
		//HACKHACKHACKTEMP
		item = FindItemForWeapon( WP_EMPLACED_GUN );
		CG_RegisterItemSounds( (item-bg_itemlist) );
		CG_RegisterItemVisuals( (item-bg_itemlist) );
		item = FindItemForWeapon( WP_ROCKET_LAUNCHER );
		CG_RegisterItemSounds( (item-bg_itemlist) );
		CG_RegisterItemVisuals( (item-bg_itemlist) );
		item = FindItemForWeapon( WP_BOWCASTER );
		CG_RegisterItemSounds( (item-bg_itemlist) );
		CG_RegisterItemVisuals( (item-bg_itemlist) );
		//HACKHACKHACKTEMP
		//FIXME: these get lost in load/save!  Must use variables that are set every frame or saved/loaded
		//camera
		gi.cvar_set( "cg_thirdperson", "1" );
		cg.overrides.active |= CG_OVERRIDE_3RD_PERSON_RNG;
		cg.overrides.thirdPersonRange = 240;
		//cg.overrides.thirdPersonVertOffset = 100;
		//cg.overrides.thirdPersonPitchOffset = -30;
		//FIXME: this gets stomped in pmove?
		pEnt->client->ps.viewheight = 120;
		//FIXME: setting these broke things very badly...?
		//pEnt->client->standheight = 200;
		//pEnt->client->crouchheight = 200;
		//pEnt->mass = 300;
		//movement
		//pEnt->client->ps.speed = 0;//FIXME: override speed?
		//FIXME: slow turn turning/can't turn if not moving?
	}
}

// Animate the vehicle and it's riders.
void Animate( Vehicle_t *pVeh )
{
	// Validate a pilot rider.
	if ( pVeh->m_pPilot )
	{
		if (pVeh->m_pVehicleInfo->AnimateRiders)
		{
			pVeh->m_pVehicleInfo->AnimateRiders( pVeh );
		}
	}

	pVeh->m_pVehicleInfo->AnimateVehicle( pVeh );
}

// Determine whether this entity is able to board this vehicle or not.
bool ValidateBoard( Vehicle_t *pVeh, bgEntity_t *pEnt )
{
	// Determine where the entity is entering the vehicle from (left, right, or back).
	vec3_t vVehToEnt;
	vec3_t vVehDir;
	const gentity_t *parent = (gentity_t *)pVeh->m_pParentEntity;
	const gentity_t *ent = (gentity_t *)pEnt;
	vec3_t vVehAngles;
	float fDot;

	if ( pVeh->m_iDieTime>0)
	{
		return false;
	}

	if ( ent->health <= 0 )
	{//dead men can't ride vehicles
		return false;
	}

	if ( pVeh->m_pPilot != NULL )
	{//already have a driver!
		if ( pVeh->m_pVehicleInfo->type == VH_FIGHTER )
		{//I know, I know, this should by in the fighters's validateboard()
			//can never steal a fighter from it's pilot
			return false;
		}
		else if ( pVeh->m_pVehicleInfo->type == VH_WALKER )
		{//I know, I know, this should by in the walker's validateboard()
			if ( !ent->client || ent->client->ps.groundEntityNum != parent->s.number )
			{//can only steal an occupied AT-ST if you're on top (by the hatch)
				return false;
			}
		}
		else if (pVeh->m_pVehicleInfo->type == VH_SPEEDER)
		{//you can only steal the bike from the driver if you landed on the driver or bike
			return (pVeh->m_iBoarding==VEH_MOUNT_THROW_LEFT || pVeh->m_iBoarding==VEH_MOUNT_THROW_RIGHT);
		}
	}
	// Yes, you shouldn't have put this here (you 'should' have made an 'overriden' ValidateBoard func), but in this
	// instance it's more than adequate (which is why I do it too :-). Making a whole other function for this is silly.
	else if ( pVeh->m_pVehicleInfo->type == VH_FIGHTER )
	{
		// If you're a fighter, you allow everyone to enter you from all directions.
		return true;
	}

	// Clear out all orientation axis except for the yaw.
	VectorSet(vVehAngles, 0, parent->currentAngles[YAW], 0);

	// Vector from Entity to Vehicle.
	VectorSubtract( ent->currentOrigin, parent->currentOrigin, vVehToEnt );
	vVehToEnt[2] = 0;
	VectorNormalize( vVehToEnt );

	// Get the right vector.
	AngleVectors( vVehAngles, NULL, vVehDir, NULL );
	VectorNormalize( vVehDir );

	// Find the angle between the vehicle right vector and the vehicle to entity vector.
	fDot = DotProduct( vVehToEnt, vVehDir );

	// If the entity is within a certain angle to the left of the vehicle...
	if ( fDot >= 0.5f )
	{
		// Right board.
		pVeh->m_iBoarding = -2;
	}
	else if ( fDot <= -0.5f )
	{
		// Left board.
		pVeh->m_iBoarding = -1;
	}
	// Maybe they're trying to board from the back...
	else
	{
		// The forward vector of the vehicle.
	//	AngleVectors( vVehAngles, vVehDir, NULL, NULL );
	//	VectorNormalize( vVehDir );

		// Find the angle between the vehicle forward and the vehicle to entity vector.
	//	fDot = DotProduct( vVehToEnt, vVehDir );

		// If the entity is within a certain angle behind the vehicle...
		//if ( fDot <= -0.85f )
		{
			// Jump board.
			pVeh->m_iBoarding = -3;
		}
	}

	// If for some reason we couldn't board, leave...
	if ( pVeh->m_iBoarding > -1 )
		return false;

	return true;
}

// Board this Vehicle (get on). The first entity to board an empty vehicle becomes the Pilot.
bool Board( Vehicle_t *pVeh, bgEntity_t *pEnt )
{
	vec3_t vPlayerDir;
	gentity_t *ent = (gentity_t *)pEnt;
	gentity_t *parent = (gentity_t *)pVeh->m_pParentEntity;

	// If it's not a valid entity, OR if the vehicle is blowing up (it's dead), OR it's not
	// empty, OR we're already being boarded, OR the person trying to get on us is already
	// in a vehicle (that was a fun bug :-), leave!
	if ( !ent || parent->health <= 0 /*|| !( parent->client->ps.eFlags & EF_EMPTY_VEHICLE )*/ || (pVeh->m_iBoarding > 0) ||
		 ( ent->s.m_iVehicleNum != 0 ) )
		return false;

	// Bucking so we can't do anything (NOTE: Should probably be a better name since fighters don't buck...).
	if ( pVeh->m_ulFlags & VEH_BUCKING )
		return false;

	// Validate the entity's ability to board this vehicle.
	if ( !pVeh->m_pVehicleInfo->ValidateBoard( pVeh, pEnt ) )
		return false;

	// FIXME FIXME!!! Ask Mike monday where ent->client->ps.eFlags might be getting changed!!! It is always 0 (when it should
	// be 1024) so a person riding a vehicle is able to ride another vehicle!!!!!!!!

	// Tell everybody their status.
	// ALWAYS let the player be the pilot.
	if ( ent->s.number < MAX_CLIENTS )
	{
		pVeh->m_pOldPilot = pVeh->m_pPilot;


		pVeh->m_pVehicleInfo->SetPilot( pVeh, ent );
		ent->s.m_iVehicleNum = parent->s.number;
		parent->owner = ent;

#ifdef QAGAME
		{
			gentity_t *gParent = (gentity_t *)parent;
			if ( (gParent->spawnflags&2) )
			{//was being suspended
				gParent->spawnflags &= ~2;//SUSPENDED - clear this spawnflag, no longer docked, okay to free-fall if not in space
				//gParent->client->ps.eFlags &= ~EF_RADAROBJECT;
				G_Sound( gParent, CHAN_AUTO, G_SoundIndex( "sound/vehicles/common/release.wav" ) );
				if ( gParent->fly_sound_debounce_time )
				{//we should drop like a rock for a few seconds
					pVeh->m_iDropTime = level.time + gParent->fly_sound_debounce_time;
				}
			}
		}
#endif

		gi.cvar_set( "cg_thirdperson", "1" );								//go to third person
		CG_CenterPrint( "@SP_INGAME_EXIT_VIEW", SCREEN_HEIGHT * 0.86 );		//tell them how to get out!

		//FIXME: rider needs to look in vehicle's direction when he gets in
		// Clear these since they're used to turn the vehicle now.
		/*SetClientViewAngle( ent, pVeh->m_vOrientation );
		memset( &parent->client->usercmd, 0, sizeof( usercmd_t ) );
		memset( &pVeh->m_ucmd, 0, sizeof( usercmd_t ) );
		VectorClear( parent->client->ps.viewangles );
		VectorClear( parent->client->ps.delta_angles );*/

		// Set the looping sound only when there is a pilot (when the vehicle is "on").
		if ( pVeh->m_pVehicleInfo->soundLoop )
		{
			parent->s.loopSound = pVeh->m_pVehicleInfo->soundLoop;
		}
	}
	else
	{
		// If there's no pilot, try to drive this vehicle.
		if ( pVeh->m_pPilot == NULL )
		{
			pVeh->m_pVehicleInfo->SetPilot( pVeh, ent );
			// TODO: Set pilot should do all this stuff....
			parent->owner = ent;
			// Set the looping sound only when there is a pilot (when the vehicle is "on").
			if ( pVeh->m_pVehicleInfo->soundLoop )
			{
				parent->s.loopSound = pVeh->m_pVehicleInfo->soundLoop;
			}

			parent->client->ps.speed = 0;
			memset( &pVeh->m_ucmd, 0, sizeof( usercmd_t ) );
		}
		// We're full, sorry...
		else
		{
			return false;
		}
	}

	// Make sure the entity knows it's in a vehicle.
	ent->s.m_iVehicleNum = parent->s.number;
	ent->owner = parent;
	parent->s.m_iVehicleNum = ent->s.number+1;

	//memset( &ent->client->usercmd, 0, sizeof( usercmd_t ) );

	//FIXME: no saber or weapons if numHands = 2, should switch to speeder weapon, no attack anim on player
	if ( pVeh->m_pVehicleInfo->numHands == 2 )
	{//switch to vehicle weapon
		if (ent->s.number<MAX_CLIENTS)
		{// Riding means you get WP_NONE
			ent->client->ps.stats[ STAT_WEAPONS ] |= (1<<WP_NONE);
		}
		if ( (ent->client->ps.weapon != WP_SABER
			&& ent->client->ps.weapon != WP_BLASTER) || !(pVeh->m_pVehicleInfo->type == VH_ANIMAL || pVeh->m_pVehicleInfo->type == VH_SPEEDER))
		{//switch to weapon none?
			if (ent->s.number<MAX_CLIENTS)
			{
				CG_ChangeWeapon(WP_NONE);
			}
			ent->client->ps.weapon = WP_NONE;
			G_RemoveWeaponModels( ent );
		}
	}

	if ( pVeh->m_pVehicleInfo->hideRider )
	{//hide the rider
		pVeh->m_pVehicleInfo->Ghost( pVeh, (bgEntity_t *)ent );
	}

	// Play the start sounds
	if ( pVeh->m_pVehicleInfo->soundOn )
	{
		// NOTE: Use this type so it's spatialized and updates play origin as bike moves - MCG
		G_SoundIndexOnEnt( parent, CHAN_AUTO, pVeh->m_pVehicleInfo->soundOn );
	}

	VectorCopy( pVeh->m_vOrientation, vPlayerDir );
	vPlayerDir[ROLL] = 0;
	SetClientViewAngle( ent, vPlayerDir );

	return true;
}

bool VEH_TryEject( Vehicle_t *pVeh,
				  gentity_t *parent,
				  gentity_t *ent,
				  int ejectDir,
				  vec3_t vExitPos )
{
	float		fBias;
	float		fVehDiag;
	float		fEntDiag;
	vec3_t		vEntMins, vEntMaxs, vVehLeaveDir, vVehAngles;
	trace_t		m_ExitTrace;

	// Make sure that the entity is not 'stuck' inside the vehicle (since their bboxes will now intersect).
	// This makes the entity leave the vehicle from the right side.
	VectorSet(vVehAngles, 0, parent->currentAngles[YAW], 0);
	switch ( ejectDir )
	{
		// Left.
		case VEH_EJECT_LEFT:
			AngleVectors( vVehAngles, NULL, vVehLeaveDir, NULL );
			vVehLeaveDir[0] = -vVehLeaveDir[0];
			vVehLeaveDir[1] = -vVehLeaveDir[1];
			vVehLeaveDir[2] = -vVehLeaveDir[2];
			break;
		// Right.
		case VEH_EJECT_RIGHT:
			AngleVectors( vVehAngles, NULL, vVehLeaveDir, NULL );
			break;
		// Front.
		case VEH_EJECT_FRONT:
			AngleVectors( vVehAngles, vVehLeaveDir, NULL, NULL );
			break;
		// Rear.
		case VEH_EJECT_REAR:
			AngleVectors( vVehAngles, vVehLeaveDir, NULL, NULL );
			vVehLeaveDir[0] = -vVehLeaveDir[0];
			vVehLeaveDir[1] = -vVehLeaveDir[1];
			vVehLeaveDir[2] = -vVehLeaveDir[2];
			break;
		// Top.
		case VEH_EJECT_TOP:
			AngleVectors( vVehAngles, NULL, NULL, vVehLeaveDir );
			break;
		// Bottom?.
		case VEH_EJECT_BOTTOM:
			break;
	}
	VectorNormalize( vVehLeaveDir );
	//NOTE: not sure why following line was needed - MCG
	//pVeh->m_EjectDir = VEH_EJECT_LEFT;

	// Since (as of this time) the collidable geometry of the entity is just an axis
	// aligned box, we need to get the diagonal length of it in case we come out on that side.
	// Diagonal Length == squareroot( squared( Sidex / 2 ) + squared( Sidey / 2 ) );

	// TODO: DO diagonal for entity.

	fBias = 1.0f;
	if (pVeh->m_pVehicleInfo->type == VH_WALKER)
	{ //hacktastic!
		fBias += 0.2f;
	}
	VectorCopy( ent->currentOrigin, vExitPos );
	fVehDiag = sqrtf( ( parent->maxs[0] * parent->maxs[0] ) + ( parent->maxs[1] * parent->maxs[1] ) );
	VectorCopy( ent->maxs, vEntMaxs );
	fEntDiag = sqrtf( ( vEntMaxs[0] * vEntMaxs[0] ) + ( vEntMaxs[1] * vEntMaxs[1] ) );
	vVehLeaveDir[0] *= ( fVehDiag + fEntDiag ) * fBias;	// x
	vVehLeaveDir[1] *= ( fVehDiag + fEntDiag ) * fBias;	// y
	vVehLeaveDir[2] *= ( fVehDiag + fEntDiag ) * fBias;
	VectorAdd( vExitPos, vVehLeaveDir, vExitPos );

	//we actually could end up *not* getting off if the trace fails...
	// Check to see if this new position is a valid place for our entity to go.
	VectorCopy(ent->mins, vEntMins);
	VectorCopy(ent->maxs, vEntMaxs);
	G_VehicleTrace( &m_ExitTrace, ent->currentOrigin, vEntMins, vEntMaxs, vExitPos, ent->s.number, ent->clipmask );

	if ( m_ExitTrace.allsolid//in solid
		|| m_ExitTrace.startsolid)
	{
		return false;
	}
	// If the trace hit something, we can't go there!
	if ( m_ExitTrace.fraction < 1.0f )
	{//not totally clear
		if ( (parent->clipmask&ent->contents) )//vehicle could actually get stuck on body
		{//the trace hit the vehicle, don't let them get out, just in case
			return false;
		}
		//otherwise, use the trace.endpos
		VectorCopy( m_ExitTrace.endpos, vExitPos );
	}
	return true;
}

void G_EjectDroidUnit( Vehicle_t *pVeh, qboolean kill )
{
	pVeh->m_pDroidUnit->s.m_iVehicleNum = ENTITYNUM_NONE;
	pVeh->m_pDroidUnit->owner = NULL;
//	pVeh->m_pDroidUnit->s.otherEntityNum2 = ENTITYNUM_NONE;
#ifdef QAGAME
	{
		gentity_t *droidEnt = (gentity_t *)pVeh->m_pDroidUnit;
		droidEnt->flags &= ~FL_UNDYING;
		droidEnt->r.ownerNum = ENTITYNUM_NONE;
		if ( droidEnt->client )
		{
			droidEnt->client->ps.m_iVehicleNum = ENTITYNUM_NONE;
		}
		if ( kill )
		{//Kill them, too
			//FIXME: proper origin, MOD and attacker (for credit/death message)?  Get from vehicle?
			G_MuteSound(droidEnt->s.number, CHAN_VOICE);
			G_Damage( droidEnt, NULL, NULL, NULL, droidEnt->s.origin, 10000, 0, MOD_SUICIDE );//FIXME: proper MOD?  Get from vehicle?
		}
	}
#endif
	pVeh->m_pDroidUnit = NULL;
}

// Eject the pilot from the vehicle.
bool Eject( Vehicle_t *pVeh, bgEntity_t *pEnt, qboolean forceEject )
{
	gentity_t	*parent;
	vec3_t		vExitPos;
	vec3_t		vPlayerDir;
	gentity_t	*ent = (gentity_t *)pEnt;
	int			firstEjectDir;


	// Validate.
	if ( !ent )
	{
		return false;
	}
	if ( !forceEject )
	{
		if ( !( pVeh->m_iBoarding == 0 || pVeh->m_iBoarding == -999 || ( pVeh->m_iBoarding < -3 && pVeh->m_iBoarding >= -9 ) ) )
		{
			return false;
		}
	}

	// Raven left a note here, commented out, marked FIXME three times over: on
	// boarding a vehicle the rider's weapon should be put away - CG_ChangeWeapon
	// to WP_NONE and G_RemoveWeaponModels. It was fenced off behind _JK2MP as
	// well as commented out, so it was dead twice. The fence is gone; the note
	// is worth keeping as a note, and whether the rider should holster is a
	// question about the game rather than about the build.

	parent = (gentity_t *)pVeh->m_pParentEntity;

	//Try ejecting in every direction
	if ( pVeh->m_EjectDir < VEH_EJECT_LEFT )
	{
		pVeh->m_EjectDir = VEH_EJECT_LEFT;
	}
	else if ( pVeh->m_EjectDir > VEH_EJECT_BOTTOM )
	{
		pVeh->m_EjectDir = VEH_EJECT_BOTTOM;
	}
	firstEjectDir = pVeh->m_EjectDir;
	while ( !VEH_TryEject( pVeh, parent, ent, pVeh->m_EjectDir, vExitPos ) )
	{
		pVeh->m_EjectDir++;
		if ( pVeh->m_EjectDir > VEH_EJECT_BOTTOM )
		{
			pVeh->m_EjectDir = VEH_EJECT_LEFT;
		}
		if ( pVeh->m_EjectDir == firstEjectDir )
		{//they all failed
			if ( forceEject )
			{//we want to always get out, just eject him here
				VectorCopy( ent->currentOrigin, vExitPos );
				break;
			}
			else
			{//can't eject
				return false;
			}
		}
	}

	// Move them to the exit position.
	G_SetOrigin( ent, vExitPos );
	gi.linkentity( ent );

	// If it's the player, stop overrides.
	if ( ent->s.number < MAX_CLIENTS )
	{
		cg.overrides.active = 0;
	}


	// If he's the pilot...
	if ( (gentity_t *)pVeh->m_pPilot == ent )
	{

		pVeh->m_pPilot = NULL;
		parent->owner = NULL;

		//keep these current angles
		//SetClientViewAngle( parent, pVeh->m_vOrientation );
		memset( &parent->client->usercmd, 0, sizeof( usercmd_t ) );
		memset( &pVeh->m_ucmd, 0, sizeof( usercmd_t ) );

	}
	else if (ent==(gentity_t *)pVeh->m_pOldPilot)
	{
		pVeh->m_pOldPilot = 0;
	}

		if ( pVeh->m_pVehicleInfo->hideRider )
		{
			pVeh->m_pVehicleInfo->UnGhost( pVeh, (bgEntity_t *)ent );
		}

	// If the vehicle now has no pilot...
	if ( pVeh->m_pPilot == NULL  )
	{
		parent->s.loopSound = 0;
		// Completely empty vehicle...?
		parent->s.m_iVehicleNum = 0;
	}


	// Client not in a vehicle.
	ent->owner = NULL;
	ent->s.m_iVehicleNum = 0;

	// Jump out.
/*	if ( ent->client->ps.velocity[2] < JUMP_VELOCITY )
	{
		ent->client->ps.velocity[2] = JUMP_VELOCITY;
	}
	else
	{
		ent->client->ps.velocity[2] += JUMP_VELOCITY;
	}*/

	// Make sure entity is facing the direction it got off at.
	VectorCopy( pVeh->m_vOrientation, vPlayerDir );
	vPlayerDir[ROLL] = 0;
	SetClientViewAngle( ent, vPlayerDir );

	//if was using vehicle weapon, remove it and switch to normal weapon when hop out...
	if ( ent->client->ps.weapon == WP_NONE )
	{//FIXME: check against this vehicle's gun from the g_vehicleInfo table
		//remove the vehicle's weapon from me
		//ent->client->ps.stats[STAT_WEAPONS] &= ~( 1 << WP_EMPLACED_GUN );
		//ent->client->ps.ammo[weaponData[WP_EMPLACED_GUN].ammoIndex] = 0;//maybe store this ammo on the vehicle before clearing it?
		//switch back to a normal weapon we're carrying

		//FIXME: store the weapon we were using when we got on and restore that when hop off
/*		if ( (ent->client->ps.stats[STAT_WEAPONS]&(1<<WP_SABER)) )
		{
			CG_ChangeWeapon( WP_SABER );
		}
		else
		{//go through all weapons and switch to highest held
			for ( int checkWp = WP_NUM_WEAPONS-1; checkWp > WP_NONE; checkWp-- )
			{
				if ( (ent->client->ps.stats[STAT_WEAPONS]&(1<<checkWp)) )
				{
					CG_ChangeWeapon( checkWp );
				}
			}
			if ( checkWp == WP_NONE )
			{
				CG_ChangeWeapon( WP_NONE );
			}
		}*/
	}
	else
	{//FIXME: if they have their saber out:
		//if dualSabers, add the second saber into the left hand
		//saber[0] has more than one blade, turn them all on
		//NOTE: this is because you're only allowed to use your first saber's first blade on a vehicle
	}

/*	if ( !ent->s.number && ent->client->ps.weapon != WP_SABER
		&& cg_gunAutoFirst.value )
	{
		gi.cvar_set( "cg_thirdperson", "0" );
	}*/
	PM_SetLegsAnimTimer( ent, &ent->client->ps.legsAnimTimer, 0 );
	PM_SetTorsoAnimTimer( ent, &ent->client->ps.torsoAnimTimer, 0 );

	// Set how long until this vehicle can be boarded again.
	pVeh->m_iBoarding = level.time + 1000;

	return true;
}

// Eject all the inhabitants of this vehicle.
bool EjectAll( Vehicle_t *pVeh )
{
	// TODO: Setup a default escape for ever vehicle type.

	pVeh->m_EjectDir = VEH_EJECT_TOP;
	// Make sure no other boarding calls exist. We MUST exit.
	pVeh->m_iBoarding = 0;
	pVeh->m_bWasBoarding = false;

	// Throw them off.
	if ( pVeh->m_pPilot )
	{
#ifdef QAGAME
		gentity_t *pilot = (gentity_t*)pVeh->m_pPilot;
#endif
		pVeh->m_pVehicleInfo->Eject( pVeh, pVeh->m_pPilot, qtrue );
#ifdef QAGAME
		if ( pVeh->m_pVehicleInfo->killRiderOnDeath && pilot )
		{//Kill them, too
			//FIXME: proper origin, MOD and attacker (for credit/death message)?  Get from vehicle?
			G_MuteSound(pilot->s.number, CHAN_VOICE);
			G_Damage( pilot, player, player, NULL, pilot->s.origin, 10000, 0, MOD_SUICIDE );
		}
#endif
	}
	if ( pVeh->m_pOldPilot )
	{
#ifdef QAGAME
		gentity_t *pilot = (gentity_t*)pVeh->m_pOldPilot;
#endif
		pVeh->m_pVehicleInfo->Eject( pVeh, pVeh->m_pOldPilot, qtrue );
#ifdef QAGAME
		if ( pVeh->m_pVehicleInfo->killRiderOnDeath && pilot )
		{//Kill them, too
			//FIXME: proper origin, MOD and attacker (for credit/death message)?  Get from vehicle?
			G_MuteSound(pilot->s.number, CHAN_VOICE);
			G_Damage( pilot, player, player, NULL, pilot->s.origin, 10000, 0, MOD_SUICIDE );
		}
#endif
	}

	if ( pVeh->m_pDroidUnit )
	{
		G_EjectDroidUnit( pVeh, pVeh->m_pVehicleInfo->killRiderOnDeath );
	}

	return true;
}

// Start a delay until the vehicle explodes.
static void StartDeathDelay( Vehicle_t *pVeh, int iDelayTimeOverride )
{
	gentity_t *parent = (gentity_t *)pVeh->m_pParentEntity;

	if ( iDelayTimeOverride )
	{
		pVeh->m_iDieTime = level.time + iDelayTimeOverride;
	}
	else
	{
		pVeh->m_iDieTime = level.time + pVeh->m_pVehicleInfo->explosionDelay;
	}

	// Armor Gone Effects (Fire)
	//---------------------------
	if (pVeh->m_pVehicleInfo->iArmorGoneFX)
	{
		if (!(pVeh->m_ulFlags&VEH_ARMORGONE) && (pVeh->m_iArmor <= 0))
		{
			pVeh->m_ulFlags |= VEH_ARMORGONE;
			G_PlayEffect(pVeh->m_pVehicleInfo->iArmorGoneFX, parent->playerModel, parent->crotchBolt, parent->s.number, parent->currentOrigin, 1, qtrue);
			parent->s.loopSound = G_SoundIndex( "sound/vehicles/common/fire_lp.wav" );
		}
	}
}

// Decide whether to explode the vehicle or not.
static void DeathUpdate( Vehicle_t *pVeh )
{
	gentity_t *parent = (gentity_t *)pVeh->m_pParentEntity;

	if ( level.time >= pVeh->m_iDieTime )
	{
		// If the vehicle is not empty.
		if ( pVeh->m_pVehicleInfo->Inhabited( pVeh ) )
		{
			if (pVeh->m_pPilot)
			{
				pVeh->m_pPilot->client->noRagTime = -1;		// no ragdoll for you
			}

			pVeh->m_pVehicleInfo->EjectAll( pVeh );
		}

		if ( !pVeh->m_pVehicleInfo->Inhabited( pVeh ) )
		{ //explode now as long as we managed to kick everyone out
			vec3_t	lMins, lMaxs, bottom;
			trace_t	trace;


			// Kill All Client Side Looping Effects
			//--------------------------------------
			if (pVeh->m_pVehicleInfo->iExhaustFX)
			{
				for (int i=0; (i<MAX_VEHICLE_EXHAUSTS && pVeh->m_iExhaustTag[i]!=-1); i++)
				{
					G_StopEffect(pVeh->m_pVehicleInfo->iExhaustFX, parent->playerModel, pVeh->m_iExhaustTag[i], parent->s.number);
				}
			}
			if (pVeh->m_pVehicleInfo->iArmorLowFX)
			{
				G_StopEffect(pVeh->m_pVehicleInfo->iArmorLowFX,  parent->playerModel, parent->crotchBolt, parent->s.number);
			}
			if (pVeh->m_pVehicleInfo->iArmorGoneFX)
			{
				G_StopEffect(pVeh->m_pVehicleInfo->iArmorGoneFX, parent->playerModel, parent->crotchBolt, parent->s.number);
			}
			//--------------------------------------
			if ( pVeh->m_pVehicleInfo->iExplodeFX )
			{
				vec3_t fxAng = { 0.0f, -1.0f, 0.0f };
				G_PlayEffect( pVeh->m_pVehicleInfo->iExplodeFX, parent->currentOrigin, fxAng );

				//trace down and place mark
				VectorCopy( parent->currentOrigin, bottom );
				bottom[2] -= 80;
				G_VehicleTrace( &trace, parent->currentOrigin, vec3_origin, vec3_origin, bottom, parent->s.number, CONTENTS_SOLID );
				if ( trace.fraction < 1.0f )
				{
					VectorCopy( trace.endpos, bottom );
					bottom[2] += 2;
					G_PlayEffect( "ships/ship_explosion_mark", trace.endpos );
				}
			}

			parent->takedamage = qfalse;//so we don't recursively damage ourselves
			if ( pVeh->m_pVehicleInfo->explosionRadius > 0 && pVeh->m_pVehicleInfo->explosionDamage > 0 )
			{
				VectorCopy( parent->mins, lMins );
				lMins[2] = -4;//to keep it off the ground a *little*
				VectorCopy( parent->maxs, lMaxs );
				VectorCopy( parent->currentOrigin, bottom );
				bottom[2] += parent->mins[2] - 32;
				G_VehicleTrace( &trace, parent->currentOrigin, lMins, lMaxs, bottom, parent->s.number, CONTENTS_SOLID );
				G_RadiusDamage( trace.endpos, player, pVeh->m_pVehicleInfo->explosionDamage, pVeh->m_pVehicleInfo->explosionRadius, NULL, MOD_EXPLOSIVE );//FIXME: extern damage and radius or base on fuel
			}

			parent->e_ThinkFunc = thinkF_G_FreeEntity;
			parent->nextthink = level.time + FRAMETIME;
		}
	}
	else
	{//let everyone around me know I'm gonna blow!
		if ( !Q_irand( 0, 10 ) )
		{//not so often...
			AddSoundEvent( parent, parent->currentOrigin, 512, AEL_DANGER );
			AddSightEvent( parent, parent->currentOrigin, 512, AEL_DANGER, 100 );
		}
	}
}

// Register all the assets used by this vehicle.
void RegisterAssets( Vehicle_t *pVeh )
{
}

extern void ChangeWeapon( gentity_t *ent, int newWeapon );

// Initialize the vehicle.
bool Initialize( Vehicle_t *pVeh )
{
	gentity_t *parent = (gentity_t *)pVeh->m_pParentEntity;
	int i = 0;

	if ( !parent || !parent->client )
		return false;

	parent->s.m_iVehicleNum = 0;
	{
	pVeh->m_iArmor = pVeh->m_pVehicleInfo->armor;
	parent->client->pers.maxHealth = parent->client->ps.stats[STAT_MAX_HEALTH] = parent->NPC->stats.health = parent->health = parent->client->ps.stats[STAT_HEALTH] = pVeh->m_iArmor;
	pVeh->m_iShields = pVeh->m_pVehicleInfo->shields;
	parent->client->ps.stats[STAT_ARMOR] = pVeh->m_iShields;
	}
	parent->mass = pVeh->m_pVehicleInfo->mass;
	//initialize the ammo to max
	for ( i = 0; i < MAX_VEHICLE_WEAPONS; i++ )
	{
		parent->client->ps.ammo[i] = pVeh->weaponStatus[i].ammo = pVeh->m_pVehicleInfo->weapon[i].ammoMax;
	}
	for ( i = 0; i < MAX_VEHICLE_TURRETS; i++ )
	{
		pVeh->turretStatus[i].nextMuzzle = (pVeh->m_pVehicleInfo->turret[i].iMuzzle[i]-1);
		parent->client->ps.ammo[MAX_VEHICLE_WEAPONS+i] = pVeh->turretStatus[i].ammo = pVeh->m_pVehicleInfo->turret[i].iAmmoMax;
		if ( pVeh->m_pVehicleInfo->turret[i].bAI )
		{//they're going to be finding enemies, init this to NONE
			pVeh->turretStatus[i].enemyEntNum = ENTITYNUM_NONE;
		}
	}
	//begin stopped...?
	parent->client->ps.speed = 0;

	VectorClear( pVeh->m_vOrientation );
	pVeh->m_vOrientation[YAW] = parent->s.angles[YAW];

	if ( pVeh->m_pVehicleInfo->gravity &&
		pVeh->m_pVehicleInfo->gravity != g_gravity->value )
	{//not normal gravity
		parent->svFlags |= SVF_CUSTOM_GRAVITY;
		parent->client->ps.gravity = pVeh->m_pVehicleInfo->gravity;
	}

	/*
	if ( pVeh->m_iVehicleTypeID == VH_FIGHTER )
	{
		pVeh->m_ulFlags = VEH_GEARSOPEN;
	}
	else
	*/
	//why?! -rww
	{
		pVeh->m_ulFlags = 0;
	}
	pVeh->m_fTimeModifier = 1.0f;
	pVeh->m_iBoarding = 0;
	pVeh->m_bWasBoarding = false;
	pVeh->m_pOldPilot = NULL;
	VectorClear(pVeh->m_vBoardingVelocity);
	pVeh->m_pPilot = NULL;
	memset( &pVeh->m_ucmd, 0, sizeof( usercmd_t ) );
	pVeh->m_iDieTime = 0;
	pVeh->m_EjectDir = VEH_EJECT_LEFT;

	//pVeh->m_iDriverTag = -1;
	//pVeh->m_iLeftExhaustTag = -1;
	//pVeh->m_iRightExhaustTag = -1;
	//pVeh->m_iGun1Tag = -1;
	//pVeh->m_iGun1Bone = -1;
	//pVeh->m_iLeftWingBone = -1;
	//pVeh->m_iRightWingBone = -1;
	memset( pVeh->m_iExhaustTag, -1, sizeof( int ) * MAX_VEHICLE_EXHAUSTS );
	memset( pVeh->m_iMuzzleTag, -1, sizeof( int ) * MAX_VEHICLE_MUZZLES );
	// FIXME! Use external values read from the vehicle data file!
	memset( pVeh->m_Muzzles, 0, sizeof( Muzzle ) * MAX_VEHICLE_MUZZLES );
	pVeh->m_iDroidUnitTag = -1;

	//initialize to blaster, just since it's a basic weapon and there's no lightsaber crap...?
	parent->client->ps.weapon = WP_BLASTER;
	parent->client->ps.weaponstate = WEAPON_READY;
	parent->client->ps.stats[STAT_WEAPONS] |= (1<<WP_BLASTER);

	//Initialize to landed (wings closed, gears down) animation
	{
		int iFlags = SETANIM_FLAG_NORMAL, iBlend = 300;
		NPC_SetAnim( pVeh->m_pParentEntity, SETANIM_BOTH, BOTH_VS_IDLE, iFlags, iBlend );
	}

	return true;
}



// Like a think or move command, this updates various vehicle properties.
static bool Update( Vehicle_t *pVeh, const usercmd_t *pUmcd )
{
	gentity_t *parent = (gentity_t *)pVeh->m_pParentEntity;
	gentity_t *pilotEnt;
	//static float fMod = 1000.0f / 60.0f;
	vec3_t vVehAngles;
	int i;
	int	prevSpeed;
	int	nextSpeed;
	int	curTime;
	int halfMaxSpeed;
	playerState_t *parentPS;
	qboolean linkHeld = qfalse;


	parentPS = &pVeh->m_pParentEntity->client->ps;

	curTime = level.time;

	//increment the ammo for all rechargeable weapons
	for ( i = 0; i < MAX_VEHICLE_WEAPONS; i++ )
	{
		if ( pVeh->m_pVehicleInfo->weapon[i].ID > VEH_WEAPON_BASE//have a weapon in this slot
			&& pVeh->m_pVehicleInfo->weapon[i].ammoRechargeMS//its ammo is rechargable
			&& pVeh->weaponStatus[i].ammo < pVeh->m_pVehicleInfo->weapon[i].ammoMax//its ammo is below max
			&& pUmcd->serverTime-pVeh->weaponStatus[i].lastAmmoInc >= pVeh->m_pVehicleInfo->weapon[i].ammoRechargeMS )//enough time has passed
		{//add 1 to the ammo
			pVeh->weaponStatus[i].lastAmmoInc = pUmcd->serverTime;
			pVeh->weaponStatus[i].ammo++;
			//NOTE: in order to send the vehicle's ammo info to the client, we copy the ammo into the first 2 ammo slots on the vehicle NPC's client->ps.ammo array
			if ( parent && parent->client )
			{
				parent->client->ps.ammo[i] = pVeh->weaponStatus[i].ammo;
			}
		}
	}
	for ( i = 0; i < MAX_VEHICLE_TURRETS; i++ )
	{
		if ( pVeh->m_pVehicleInfo->turret[i].iWeapon > VEH_WEAPON_BASE//have a weapon in this slot
			&& pVeh->m_pVehicleInfo->turret[i].iAmmoRechargeMS//its ammo is rechargable
			&& pVeh->turretStatus[i].ammo < pVeh->m_pVehicleInfo->turret[i].iAmmoMax//its ammo is below max
			&& pUmcd->serverTime-pVeh->turretStatus[i].lastAmmoInc >= pVeh->m_pVehicleInfo->turret[i].iAmmoRechargeMS )//enough time has passed
		{//add 1 to the ammo
			pVeh->turretStatus[i].lastAmmoInc = pUmcd->serverTime;
			pVeh->turretStatus[i].ammo++;
			//NOTE: in order to send the vehicle's ammo info to the client, we copy the ammo into the first 2 ammo slots on the vehicle NPC's client->ps.ammo array
			if ( parent && parent->client )
			{
				parent->client->ps.ammo[MAX_VEHICLE_WEAPONS+i] = pVeh->turretStatus[i].ammo;
			}
		}
	}

	//increment shields for rechargable shields
	if ( pVeh->m_pVehicleInfo->shieldRechargeMS
		&& parentPS->stats[STAT_ARMOR] > 0 //still have some shields left
		&& parentPS->stats[STAT_ARMOR] < pVeh->m_pVehicleInfo->shields//its below max
		&& pUmcd->serverTime-pVeh->lastShieldInc >= pVeh->m_pVehicleInfo->shieldRechargeMS )//enough time has passed
	{
		parentPS->stats[STAT_ARMOR]++;
		if ( parentPS->stats[STAT_ARMOR] > pVeh->m_pVehicleInfo->shields )
		{
			parentPS->stats[STAT_ARMOR] = pVeh->m_pVehicleInfo->shields;
		}
		pVeh->m_iShields = parentPS->stats[STAT_ARMOR];
	}


	// See whether this vehicle should be dieing or dead.
	if ( pVeh->m_iDieTime != 0
		|| (parent->health <= 0)
		)
	{//NOTE!!!: This HAS to be consistent with cgame!!!
		// Keep track of the old orientation.
		VectorCopy( pVeh->m_vOrientation, pVeh->m_vPrevOrientation );

		// Process the orient commands.
		pVeh->m_pVehicleInfo->ProcessOrientCommands( pVeh );
		// Need to copy orientation to our entity's viewangles so that it renders at the proper angle and currentAngles is correct.
		SetClientViewAngle( parent, pVeh->m_vOrientation );
		if ( pVeh->m_pPilot )
		{
			SetClientViewAngle( (gentity_t *)pVeh->m_pPilot, pVeh->m_vOrientation );
		}
		/*
		for ( i = 0; i < pVeh->m_pVehicleInfo->maxPassengers; i++ )
		{
			if ( pVeh->m_ppPassengers[i] )
			{
				SetClientViewAngle( (gentity_t *)pVeh->m_ppPassengers[i], pVeh->m_vOrientation );
			}
		}
		*/

		// Process the move commands.
		pVeh->m_pVehicleInfo->ProcessMoveCommands( pVeh );

		// Setup the move direction.
		if ( pVeh->m_pVehicleInfo->type == VH_FIGHTER )
		{
			AngleVectors( pVeh->m_vOrientation, parent->client->ps.moveDir, NULL, NULL );
		}
		else
		{
			VectorSet(vVehAngles, 0, pVeh->m_vOrientation[YAW], 0);
			AngleVectors( vVehAngles, parent->client->ps.moveDir, NULL, NULL );
		}
		pVeh->m_pVehicleInfo->DeathUpdate( pVeh );
		return false;
	}
	// Vehicle dead!


	if (parent->spawnflags & 1)
	{//NOTE: in SP, this actually just checks LOS to the Player
		if (pVeh->m_iPilotTime < level.time)
		{//do another check?
			if ( !player || G_ClearLineOfSight(pVeh->m_pParentEntity->currentOrigin, player->currentOrigin, pVeh->m_pParentEntity->s.number, MASK_OPAQUE ) )
			{
				pVeh->m_iPilotTime = level.time + pVeh->m_pParentEntity->endFrame;
			}
		}
		if (pVeh->m_iPilotTime && pVeh->m_iPilotTime < level.time)
		{ //die
			//FIXME: does this give proper credit to the enemy who shot you down?
            G_Damage(parent, player, player, NULL, parent->client->ps.origin, 99999, DAMAGE_NO_PROTECTION, MOD_SUICIDE);
		}
	}

//	if (level.time<pVeh->m_iTurboTime || pVeh->m_pVehicleInfo->type==VH_ANIMAL)
	// always knock guys around now...
	{
		vec3_t	dir;
		vec3_t	projectedPosition;
		VectorCopy(parent->client->ps.velocity, dir);
		VectorMA(parent->currentOrigin, 0.1f, dir, projectedPosition);

		float	force = VectorNormalize(dir);
		force /= 10.0f;
		if (force>30.0f)
		{
			trace_t	tr;
			G_VehicleTrace(&tr, parent->currentOrigin, parent->mins, parent->maxs, projectedPosition, parent->s.number, CONTENTS_BODY);
			if (tr.fraction<1.0f &&
				!tr.allsolid &&
				!tr.startsolid &&
				tr.entityNum!=ENTITYNUM_NONE &&
				tr.entityNum!=ENTITYNUM_WORLD &&
				(level.time<pVeh->m_iTurboTime || Q_irand(0,3)==0))
			{
				gentity_t*	other = &g_entities[tr.entityNum];
				if (other && other->client && !other->s.m_iVehicleNum)
				{
					G_Throw( other, dir, force/10.0f );
					G_Knockdown( other, parent, dir, force, qtrue );
					G_Damage( other, player, player, parent->client->ps.velocity, parent->currentOrigin, force, DAMAGE_NO_ARMOR|DAMAGE_EXTRA_KNOCKBACK, MOD_IMPACT);
				}
			}
		}
	}


	// If we're not done mounting, can't do anything.
	if ( pVeh->m_iBoarding != 0 )
	{
		if (!pVeh->m_bWasBoarding)
		{
			VectorCopy(parentPS->velocity, pVeh->m_vBoardingVelocity);
			pVeh->m_bWasBoarding = true;
		}

		// See if we're done boarding.
		if ( pVeh->m_iBoarding > -1 && pVeh->m_iBoarding <= level.time )
		{
			pVeh->m_bWasBoarding = false;
			pVeh->m_iBoarding = 0;
		}
		else
		{
			return false;
		}
	}

	parent = (gentity_t *)pVeh->m_pParentEntity;

	// Validate vehicle.
	if ( !parent || !parent->client || parent->health <= 0 )
		return false;

	// See if any of the riders are dead and if so kick em off.
	if ( pVeh->m_pPilot )
	{
		pilotEnt = (gentity_t *)pVeh->m_pPilot;

		if (pilotEnt->health <= 0)
		{
			pVeh->m_pVehicleInfo->Eject( pVeh, pVeh->m_pPilot, qtrue );
		}
	}

	// Copy over the commands for local storage.
	memcpy( &pVeh->m_ucmd, pUmcd, sizeof( usercmd_t ) );
	memcpy( &parent->client->pers.lastCommand, pUmcd, sizeof( usercmd_t ) );

	/*
	// Update time modifier.
	pVeh->m_fTimeModifier = pVeh->m_ucmd.serverTime - parent->client->ps.commandTime;
	//sanity check
	if ( pVeh->m_fTimeModifier < 1 )
	{
		pVeh->m_fTimeModifier = 1;
	}
	else if ( pVeh->m_fTimeModifier > 200 )
	{
		pVeh->m_fTimeModifier = 200;
	}
	//normalize to 1.0f at 20fps
	pVeh->m_fTimeModifier = pVeh->m_fTimeModifier / fMod;
	*/

	//check for weapon linking/unlinking command
	for ( i = 0; i < MAX_VEHICLE_WEAPONS; i++ )
	{//HMM... can't get a seperate command for each weapon, so do them all...?
		if ( pVeh->m_pVehicleInfo->weapon[i].linkable == 2 )
		{//always linked
			//FIXME: just set this once, on Initialize...?
			if ( !pVeh->weaponStatus[i].linked )
			{
				pVeh->weaponStatus[i].linked = qtrue;
			}
		}
		//FIXME: implement... just a console command bound to a key?
		else if ( 0 )
		{//pilot pressed the "weapon link" toggle button
			//playerState_t *pilotPS;
			//gentity_t *rider = parent->owner;
			//pilotPS = &rider->client->ps;
			if ( !pVeh->linkWeaponToggleHeld )//so we don't hold it down and toggle it back and forth
			{//okay to toggle
				if ( pVeh->m_pVehicleInfo->weapon[i].linkable == 1 )
				{//link-toggleable
					pVeh->weaponStatus[i].linked = (qboolean)!pVeh->weaponStatus[i].linked;
				}
			}
			linkHeld = qtrue;
		}
	}
	if ( linkHeld )
	{
		//so we don't hold it down and toggle it back and forth
		pVeh->linkWeaponToggleHeld = qtrue;
	}
	else
	{
		//so we don't hold it down and toggle it back and forth
		pVeh->linkWeaponToggleHeld = qfalse;
	}

#ifdef QAGAME
	for ( i = 0; i < MAX_VEHICLE_TURRETS; i++ )
	{//HMM... can't get a seperate command for each weapon, so do them all...?
		VEH_TurretThink( pVeh, parent, i );
	}
#endif


	// Keep track of the old orientation.
	VectorCopy( pVeh->m_vOrientation, pVeh->m_vPrevOrientation );

	// Process the orient commands.
	pVeh->m_pVehicleInfo->ProcessOrientCommands( pVeh );
	// Need to copy orientation to our entity's viewangles so that it renders at the proper angle and currentAngles is correct.
	SetClientViewAngle( parent, pVeh->m_vOrientation );
	if ( pVeh->m_pPilot )
	{
		if ( !BG_UnrestrainedPitchRoll( &pVeh->m_pPilot->client->ps, pVeh ) )
		{
			SetClientViewAngle( (gentity_t *)pVeh->m_pPilot, pVeh->m_vOrientation );
		}
	}
	/*
	for ( i = 0; i < pVeh->m_pVehicleInfo->maxPassengers; i++ )
	{
		if ( pVeh->m_ppPassengers[i] )
		{
			SetClientViewAngle( (gentity_t *)pVeh->m_ppPassengers[i], pVeh->m_vOrientation );
		}
	}
	*/

	// Process the move commands.
	prevSpeed = parentPS->speed;
	pVeh->m_pVehicleInfo->ProcessMoveCommands( pVeh );
	nextSpeed = parentPS->speed;
	halfMaxSpeed = pVeh->m_pVehicleInfo->speedMax*0.5f;


// Shifting Sounds
//=====================================================================
	if (pVeh->m_iTurboTime<curTime &&
		pVeh->m_iSoundDebounceTimer<curTime &&
		((nextSpeed>prevSpeed && nextSpeed>halfMaxSpeed &&	prevSpeed<halfMaxSpeed) || (nextSpeed>halfMaxSpeed && !Q_irand(0,1000)))
 		)
	{
		int	shiftSound = Q_irand(1,4);
		switch (shiftSound)
		{
		case 1: shiftSound=pVeh->m_pVehicleInfo->soundShift1; break;
		case 2: shiftSound=pVeh->m_pVehicleInfo->soundShift2; break;
		case 3: shiftSound=pVeh->m_pVehicleInfo->soundShift3; break;
		case 4: shiftSound=pVeh->m_pVehicleInfo->soundShift4; break;
		}
		if (shiftSound)
		{
			pVeh->m_iSoundDebounceTimer = curTime + Q_irand(1000, 4000);
			// NOTE: Use this type so it's spatialized and updates play origin as bike moves - MCG
			G_SoundIndexOnEnt( pVeh->m_pParentEntity, CHAN_AUTO, shiftSound);
		}
	}
//=====================================================================


	// Setup the move direction.
	if ( pVeh->m_pVehicleInfo->type == VH_FIGHTER )
	{
		AngleVectors( pVeh->m_vOrientation, parent->client->ps.moveDir, NULL, NULL );
	}
	else
	{
		VectorSet(vVehAngles, 0, pVeh->m_vOrientation[YAW], 0);
		AngleVectors( vVehAngles, parent->client->ps.moveDir, NULL, NULL );
	}



	// Make sure the vehicle takes on the enemy of it's rider (for homing missles for instance).
	if ( pVeh->m_pPilot )
	{
		parent->enemy = pVeh->m_pPilot->enemy;
	}


	return true;
}


// Update the properties of a Rider (that may reflect what happens to the vehicle).
static bool UpdateRider( Vehicle_t *pVeh, bgEntity_t *pRider, usercmd_t *pUmcd )
{
	gentity_t *parent;
	gentity_t *rider;

	if ( pVeh->m_iBoarding != 0 && pVeh->m_iDieTime==0)
		return true;

	parent = (gentity_t *)pVeh->m_pParentEntity;
	rider = (gentity_t *)pRider;
	// Regular exit.
	if ( pUmcd->buttons & BUTTON_USE && pVeh->m_pVehicleInfo->type!=VH_SPEEDER)
	{
		if ( pVeh->m_pVehicleInfo->type == VH_WALKER )
		{//just get the fuck out
			pVeh->m_EjectDir = VEH_EJECT_REAR;
			if ( pVeh->m_pVehicleInfo->Eject( pVeh, pRider, qfalse ) )
				return false;
		}
		else if ( !(pVeh->m_ulFlags & VEH_FLYING))
		{
			// If going too fast, roll off.
			if ((parent->client->ps.speed<=600) && pUmcd->rightmove!=0)
			{
				if ( pVeh->m_pVehicleInfo->Eject( pVeh, pRider, qfalse ) )
				{
					animNumber_t Anim;
					int iFlags = SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD | SETANIM_FLAG_HOLDLESS, iBlend = 300;
					if ( pUmcd->rightmove > 0 )
					{
						Anim = BOTH_ROLL_R;
						pVeh->m_EjectDir = VEH_EJECT_RIGHT;
					}
					else
					{
						Anim = BOTH_ROLL_L;
						pVeh->m_EjectDir = VEH_EJECT_LEFT;
					}
					VectorScale( parent->client->ps.velocity, 0.25f, rider->client->ps.velocity );
#if 1
					Vehicle_SetAnim( rider, SETANIM_BOTH, Anim, iFlags, iBlend );
#else

#endif
					//PM_SetAnim(pm,SETANIM_BOTH,anim,SETANIM_FLAG_OVERRIDE|SETANIM_FLAG_HOLD|SETANIM_FLAG_HOLDLESS);
					rider->client->ps.weaponTime = rider->client->ps.torsoAnimTimer - 200;//just to make sure it's cleared when roll is done
					G_AddEvent( rider, EV_ROLL, 0 );
					return false;
				}
			}
			else
			{
				// FIXME: Check trace to see if we should start playing the animation.
				animNumber_t Anim;
				int iFlags = SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD, iBlend = 500;
				if ( pUmcd->rightmove > 0 )
				{
					Anim = BOTH_VS_DISMOUNT_R;
					pVeh->m_EjectDir = VEH_EJECT_RIGHT;
				}
				else
				{
					Anim = BOTH_VS_DISMOUNT_L;
					pVeh->m_EjectDir = VEH_EJECT_LEFT;
				}

				if ( pVeh->m_iBoarding <= 1 )
				{
					int iAnimLen;
					// NOTE: I know I shouldn't reuse pVeh->m_iBoarding so many times for so many different
					// purposes, but it's not used anywhere else right here so why waste memory???
					iAnimLen = PM_AnimLength( pRider->client->clientInfo.animFileIndex, Anim );
					pVeh->m_iBoarding = level.time + iAnimLen;
					// Weird huh? Well I wanted to reuse flags and this should never be set in an
					// entity, so what the heck.
					rider->client->ps.eFlags |= EF_VEH_BOARDING;

					// Make sure they can't fire when leaving.
					rider->client->ps.weaponTime = iAnimLen;
				}

				VectorScale( parent->client->ps.velocity, 0.25f, rider->client->ps.velocity );

				Vehicle_SetAnim( rider, SETANIM_BOTH, Anim, iFlags, iBlend );
			}
		}
		// Flying, so just fall off.
		else
		{
			pVeh->m_EjectDir = VEH_EJECT_LEFT;
			if ( pVeh->m_pVehicleInfo->Eject( pVeh, pRider, qfalse ) )
				return false;
		}
	}

	// Getting off animation complete (if we had one going)?
	if ( pVeh->m_iBoarding < level.time && (rider->client->ps.eFlags & EF_VEH_BOARDING) )
	{
		rider->client->ps.eFlags &= ~EF_VEH_BOARDING;
		// Eject this guy now.
		if ( pVeh->m_pVehicleInfo->Eject( pVeh, pRider, qfalse ) )
		{
			return false;
		}
	}

	if ( pVeh->m_pVehicleInfo->type != VH_FIGHTER
		&& pVeh->m_pVehicleInfo->type != VH_WALKER  )
	{
		// Jump off.
		if ( pUmcd->upmove > 0 )
		{

// NOT IN MULTI PLAYER!
//===================================================================
			float riderRightDot = G_CanJumpToEnemyVeh(pVeh, pUmcd);
			if (riderRightDot!=0.0f)
			{
				// Eject Player From Current Vehicle
				//-----------------------------------
				pVeh->m_EjectDir = VEH_EJECT_TOP;
				pVeh->m_pVehicleInfo->Eject( pVeh, pRider, qtrue );

				// Send Current Vehicle Spinning Out Of Control
				//----------------------------------------------
				pVeh->m_pVehicleInfo->StartDeathDelay(pVeh, 10000);
				pVeh->m_ulFlags |= (VEH_OUTOFCONTROL);
 				VectorScale(pVeh->m_pParentEntity->client->ps.velocity, 1.0f, pVeh->m_pParentEntity->pos3);

				// Todo: Throw Old Vehicle Away From The New Vehicle Some
				//-------------------------------------------------------
				vec3_t	toEnemy;
				VectorSubtract(pVeh->m_pParentEntity->currentOrigin, rider->enemy->currentOrigin, toEnemy);
				VectorNormalize(toEnemy);
				G_Throw(pVeh->m_pParentEntity, toEnemy, 50);

				// Start Boarding On Enemy's Vehicle
				//-----------------------------------
				Vehicle_t*	enemyVeh = G_IsRidingVehicle(rider->enemy);
				enemyVeh->m_iBoarding = (riderRightDot>0)?(VEH_MOUNT_THROW_RIGHT):(VEH_MOUNT_THROW_LEFT);
				enemyVeh->m_pVehicleInfo->Board(enemyVeh, rider);
			}

			// Don't Jump Off If Holding Strafe Key and Moving Fast
			else if (pUmcd->rightmove && (parent->client->ps.speed>=10))
			{
				return true;
			}
//===================================================================

			if ( pVeh->m_pVehicleInfo->Eject( pVeh, pRider, qfalse ) )
			{
				// Allow them to force jump off.
				VectorScale( parent->client->ps.velocity, 0.5f, rider->client->ps.velocity );
				rider->client->ps.velocity[2] += JUMP_VELOCITY;
				rider->client->ps.pm_flags |= ( PMF_JUMPING | PMF_JUMP_HELD );
				rider->client->ps.forceJumpZStart = rider->client->ps.origin[2];

				if ( !Q3_TaskIDPending( rider, TID_CHAN_VOICE ) )
				{
					G_AddEvent( rider, EV_JUMP, 0 );
				}
#if 1
				Vehicle_SetAnim( rider, SETANIM_BOTH, BOTH_JUMP1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD, 300 );
#else

#endif
				return false;
			}
		}

		// Roll off.
	}

	return true;
}


// Attachs all the riders of this vehicle to their appropriate tag (*driver, *pass1, *pass2, whatever...).
static void AttachRiders( Vehicle_t *pVeh )
{
	// If we have a pilot, attach him to the driver tag.
	if ( pVeh->m_pPilot )
	{
		gentity_t * const parent = pVeh->m_pParentEntity;
		gentity_t * const pilot = pVeh->m_pPilot;
		mdxaBone_t	boltMatrix;

		pilot->waypoint = parent->waypoint; // take the veh's waypoint as your own

		// Get the driver tag.
		gi.G2API_GetBoltMatrix( parent->ghoul2, parent->playerModel, parent->crotchBolt, &boltMatrix,
								pVeh->m_vOrientation, parent->currentOrigin,
								(cg.time?cg.time:level.time), NULL, parent->s.modelScale );
		gi.G2API_GiveMeVectorFromMatrix( boltMatrix, ORIGIN, pilot->client->ps.origin );
		G_SetOrigin( pilot, pilot->client->ps.origin );
		gi.linkentity( pilot );
	}
	// If we have a pilot, attach him to the driver tag.
	if ( pVeh->m_pOldPilot )
	{
		gentity_t * const parent = pVeh->m_pParentEntity;
		gentity_t * const pilot = pVeh->m_pOldPilot;
		mdxaBone_t	boltMatrix;

		pilot->waypoint = parent->waypoint; // take the veh's waypoint as your own

		// Get the driver tag.
		gi.G2API_GetBoltMatrix( parent->ghoul2, parent->playerModel, parent->crotchBolt, &boltMatrix,
								pVeh->m_vOrientation, parent->currentOrigin,
								(cg.time?cg.time:level.time), NULL, parent->s.modelScale );
		gi.G2API_GiveMeVectorFromMatrix( boltMatrix, ORIGIN, pilot->client->ps.origin );
		G_SetOrigin( pilot, pilot->client->ps.origin );
		gi.linkentity( pilot );
	}
}

// Make someone invisible and un-collidable.
static void Ghost( Vehicle_t *pVeh, bgEntity_t *pEnt )
{
	gentity_t *ent;

	if ( !pEnt )
		return;

	ent = (gentity_t *)pEnt;

	ent->s.eFlags |= EF_NODRAW;
	if ( ent->client )
	{
		ent->client->ps.eFlags |= EF_NODRAW;
	}
	ent->contents = 0;
}

// Make someone visible and collidable.
static void UnGhost( Vehicle_t *pVeh, bgEntity_t *pEnt )
{
	gentity_t *ent;

	if ( !pEnt )
		return;

	ent = (gentity_t *)pEnt;

	ent->s.eFlags &= ~EF_NODRAW;
	if ( ent->client )
	{
		ent->client->ps.eFlags &= ~EF_NODRAW;
	}
	ent->contents = CONTENTS_BODY;
}


// Set the parent entity of this Vehicle NPC.
void SetParent( Vehicle_t *pVeh, bgEntity_t *pParentEntity ) { pVeh->m_pParentEntity = pParentEntity; }

// Add a pilot to the vehicle.
void SetPilot( Vehicle_t *pVeh, bgEntity_t *pPilot ) { pVeh->m_pPilot = pPilot; }

// Add a passenger to the vehicle (false if we're full).
bool AddPassenger( Vehicle_t *pVeh ) { return false; }

// Whether this vehicle is currently inhabited (by anyone) or not.
bool Inhabited( Vehicle_t *pVeh ) { return ( pVeh->m_pPilot ) ? true : false; }


// Setup the shared functions (one's that all vehicles would generally use).
void G_SetSharedVehicleFunctions( vehicleInfo_t *pVehInfo )
{
//	pVehInfo->AnimateVehicle				=		AnimateVehicle;
//	pVehInfo->AnimateRiders					=		AnimateRiders;
	pVehInfo->ValidateBoard					=		ValidateBoard;
	pVehInfo->SetParent						=		SetParent;
	pVehInfo->SetPilot						=		SetPilot;
	pVehInfo->AddPassenger					=		AddPassenger;
	pVehInfo->Animate						=		Animate;
	pVehInfo->Board							=		Board;
	pVehInfo->Eject							=		Eject;
	pVehInfo->EjectAll						=		EjectAll;
	pVehInfo->StartDeathDelay				=		StartDeathDelay;
	pVehInfo->DeathUpdate					=		DeathUpdate;
	pVehInfo->RegisterAssets				=		RegisterAssets;
	pVehInfo->Initialize					=		Initialize;
	pVehInfo->Update						=		Update;
	pVehInfo->UpdateRider					=		UpdateRider;
//	pVehInfo->ProcessMoveCommands			=		ProcessMoveCommands;
//	pVehInfo->ProcessOrientCommands			=		ProcessOrientCommands;
	pVehInfo->AttachRiders					=		AttachRiders;
	pVehInfo->Ghost							=		Ghost;
	pVehInfo->UnGhost						=		UnGhost;
	pVehInfo->Inhabited						=		Inhabited;
}

