/*************************************************************************
Crytek Source File.
Copyright (C), Crytek Studios, 2001-2012.
-------------------------------------------------------------------------

Description: Tower Searchlight entity. 
It routinely follows a path with its searchlight, shots at the player when detects it, 
tries to follow the player after detecting it, reacts to AI seeing the player, etc
*************************************************************************/

#include "StdAfx.h"
#include "TowerSearchLight.h"
#include "ScriptBind_TowerSearchLight.h"
#include "IAISystem.h"
#include <VisionMapTypes.h>
#include "IItemSystem.h"
#include "TypeInfo_impl.h"
#include <IRenderAuxGeom.h>
#include <IAIObject.h>
#include <IAIActor.h>
#include "Player.h"
#include "WeaponSystem.h"
#include "AI/GameAISystem.h"


#define MY_MAIN_UPDATE_SLOT   0

#ifdef DEBUG_INFO_TOWERSEARCHLIGHT
ICVar* CTowerSearchLight::m_pcvarDebugTowerName = NULL;
ICVar* CTowerSearchLight::m_pcvarDebugTower = NULL;
#endif

//////////////////////////////////////////////////////////////////////////
CTowerSearchLight::CTowerSearchLight()
: m_enabled( false )
, m_entityIdleMovement( 0 )
, m_state( ST_IDLE )
// most members need to be initialized inside Enable()
// lua parameter members need to be initialized inside ReadLuaProperties()
{
}

//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::RegisterDebugCVars()
{
#ifdef DEBUG_INFO_TOWERSEARCHLIGHT
	if (!m_pcvarDebugTowerName)
		m_pcvarDebugTowerName = REGISTER_STRING("g_TowerSearchLightDebugName", "", 0, "to limit towerSearchLight debug info to a single entity. The tower needs to be enabled for it to work" );
	if (!m_pcvarDebugTower)
		m_pcvarDebugTower = REGISTER_INT("g_TowerSearchLightDebug", 0, 0, "debug level for the towersearchlight debug info. 0 = no debuginfo, 1 = vision line, 2 = hearing range, 4 = attachments, 8 = states, 16 = weapon spots. 32 = movement. Can combine them by adding the numbers. for example, 25 = vision line+states+weapon spots." );
#endif
}

//////////////////////////////////////////////////////////////////////////
CTowerSearchLight::~CTowerSearchLight()
{
	Disable();
	if (g_pGame)
	{
		g_pGame->GetTowerSearchLightScriptBind()->Detach(GetEntityId());
		if ( CGameAISystem* pGameAiSystem = g_pGame->GetGameAISystem() )
		{
			pGameAiSystem->GetAIAwarenessToPlayerHelper().UnregisterAwarenessEntity( this );
		}
	}
}


//////////////////////////////////////////////////////////////////////////
bool CTowerSearchLight::Init( IGameObject * pGameObject )
{
	m_luaParams.numWeaponsSpots = 0;  // need to be initialized, because is used as a limit to traverse a table.
	SetGameObject(pGameObject);
	g_pGame->GetTowerSearchLightScriptBind()->AttachTo(this);
	m_weapon.SetOwnerId( GetEntityId() );
	m_enemyHasEverBeenInView = false;

	if ( CGameAISystem* pGameAiSystem = g_pGame->GetGameAISystem() )
		pGameAiSystem->GetAIAwarenessToPlayerHelper().RegisterAwarenessEntity( this );

	return true;
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::PostInit( IGameObject * pGameObject )
{
	Reset();
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::Release()
{
	delete this;
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::Update( SEntityUpdateContext& ctx, int slot )
{
#ifdef DEBUG_INFO_TOWERSEARCHLIGHT
if (m_pcvarDebugTower->GetIVal()>0)
{
	bool isLimitedByName = (m_pcvarDebugTowerName->GetString() && m_pcvarDebugTowerName->GetString()[0]!=0);
	if (!isLimitedByName || stricmp(m_pcvarDebugTowerName->GetString(), GetEntity()->GetName())==0)
		ShowDebugInfo( isLimitedByName );
}
#endif

	Update_TargetVisibility( ctx );

	switch (m_state)
	{
		case ST_IDLE:
			Update_IDLE( ctx );
			break;

		case ST_ENEMY_IN_VIEW:
			Update_ENEMY_IN_VIEW( ctx );
			break;

		case ST_ENEMY_LOST:
			Update_ENEMY_LOST( ctx );
			break;
	}
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::FullSerialize( TSerialize ser )
{
	bool enabled = m_enabled;
	bool sleeping = m_sleeping;
	ser.Value("enabled", enabled );
	ser.Value("sleeping", sleeping );
	ser.Value("entityIdleMovement", m_entityIdleMovement );
	ser.Value("alertAIGroupID", m_AlertAIGroupID);
	ser.Value("enemyHasEverBeenInView", m_enemyHasEverBeenInView );

	if (ser.IsReading())
	{
		Disable();
		if (enabled)
		{
			uint32 state = ST_IDLE;
			ser.Value( "state", state );
			ReadLuaProperties();
			Enable();
			if (sleeping)
				Sleep();
			else
			{
				ser.Value( "lastTargetSeenPos", m_lastTargetSeenPos );
				if (state!=ST_IDLE)
				{
					SetState( ST_ENEMY_LOST );
					m_lightMovement.isMoving = false;
					LookAtPos( m_lastTargetSeenPos );
				}
			}
		}
	}

	if (ser.IsWriting())
	{
		if (m_enabled)
		{
			uint32 state = m_state;
			ser.Value( "state", state );
			ser.Value( "lastTargetSeenPos", m_lastTargetSeenPos );
		}
	}
}


void CTowerSearchLight::PostSerialize()
{
	if ( CGameAISystem* pGameAiSystem = g_pGame->GetGameAISystem() )
		pGameAiSystem->GetAIAwarenessToPlayerHelper().RegisterAwarenessEntity( this );
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::Update_TargetVisibility( SEntityUpdateContext& ctx )
{
// m_targetIdInViewCone -> the one that comes from the AI vision system, the tower does not use it directly
// m_targetIdInView  -> the one that the tower actually considers as being "seen"

#ifndef _RELEASE
	static ICVar* s_pIgnorePlayer = gEnv->pConsole->GetCVar( "ai_IgnorePlayer" );
	if (s_pIgnorePlayer && s_pIgnorePlayer->GetIVal()!=0)
	{
		m_targetIdInView = 0;
		return;
	}
#endif

	if (m_sleeping)
	{
		m_targetIdInView = 0;
		return;
	}

	IActor* pClientActor = gEnv->pGame->GetIGameFramework()->GetClientActor();
	if (!pClientActor)
		return;

	//...alwaysSeePlayer
	if (m_luaParams.alwaysSeePlayer && !gEnv->IsEditing() )
	{
		m_targetIdInView = pClientActor->GetEntityId();
		return;
	}

	//
	m_targetIdInView = m_targetIdInViewCone;


	//...visionPersistence
	if (m_targetIdInViewCone==0)
	{
		if (m_visionPersistenceTimeLeft>0)
		{
			m_visionPersistenceTimeLeft -= ctx.fFrameTime;
			m_targetIdInView = pClientActor->GetEntityId();
		}
	}
	else
		m_visionPersistenceTimeLeft = m_luaParams.visionPersistenceTime;
	

	//...stealh code
	if (m_targetIdInView)
	{
		IEntity* pTargetEntity = pClientActor->GetEntity();
		IAIObject* pTargetAIObject = pTargetEntity->GetAI();
		if (pTargetAIObject)
		{
			IAIActor* pTargetAIActor = pTargetAIObject->CastToIAIActor();
			if (pTargetAIActor && !m_luaParams.canDetectStealth && pTargetAIActor->IsInvisibleFrom( GetEntity()->GetWorldPos(), true ))
				m_targetIdInView = 0;
		}
	}
}


//////////////////////////////////////////////////////////////////////////
IAIActor* CTowerSearchLight::GetClientAIActor()
{
	IAIActor* pClientAIActor = NULL;
	IActor* pClientActor = gEnv->pGame->GetIGameFramework()->GetClientActor();
	if (pClientActor)
	{
		IAIObject* pClientAIObject = pClientActor->GetEntity()->GetAI();
		if (pClientAIObject)
			pClientAIActor = pClientAIObject->CastToIAIActor();
	}
	return pClientAIActor;
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::Update_IDLE( const SEntityUpdateContext& ctx )
{
	m_timeToUnlockDetectionSoundSequence -= ctx.fFrameTime;  // no problem if it goes negative
	if (m_targetIdInView)
		SetState( ST_ENEMY_IN_VIEW );
	CheckAlertAIGroup( ctx );
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::Update_ENEMY_IN_VIEW( const SEntityUpdateContext& ctx )
{
	if (m_targetIdInView==0 && !m_burst.isDoingBurst)
	{
		SetState( ST_ENEMY_LOST );
		return;
	}

	if (m_targetIdInView)
	{
		PlayDetectionSounds( ctx );
		RecalculateTargetPos( m_targetIdInView );
	}
	UpdateMovementSearchLight( ctx );
	UpdateBurst( ctx );
	UpdateLasers( ctx );

	//...
	m_timeToMove -= ctx.fFrameTime;
	if (m_timeToMove<=0)
	{
		StartMovingSearchLightTo( m_lastTargetSeenPos, m_luaParams.ST_ENEMY_IN_VIEW.trackSpeed );
		m_timeToMove = m_luaParams.ST_ENEMY_IN_VIEW.followDelay;
	}

	//...
	m_timeToNextBurst -= ctx.fFrameTime;

	if (m_timeToNextBurst<=0)
	{
		float dispersionMax = m_luaParams.burstDispersion;
		float dispersionMin = 0;
		m_timeToNextBurst = m_luaParams.ST_ENEMY_IN_VIEW.timeBetweenBursts;

		if (m_burstsDoneInThisState<m_luaParams.ST_ENEMY_IN_VIEW.numWarningBursts)
		{
			dispersionMax += m_luaParams.ST_ENEMY_IN_VIEW.errorAddedToWarningBursts;
			dispersionMin += m_luaParams.ST_ENEMY_IN_VIEW.errorAddedToWarningBursts;
			m_timeToNextBurst = m_luaParams.ST_ENEMY_IN_VIEW.timeBetweenWarningBursts;
		}
		StartBurst( dispersionMin, dispersionMax, true );
	}
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::Update_ENEMY_LOST( const SEntityUpdateContext& ctx )
{
	if (m_targetIdInView)
	{
		SetState( ST_ENEMY_IN_VIEW );
		return;
	}

	m_timeToUnlockDetectionSoundSequence -= ctx.fFrameTime;  // no problem if it goes negative

	UpdateBurst( ctx );
	UpdateLasers( ctx );

	// searchlight movement
	UpdateMovementSearchLight( ctx );
	if (!m_lightMovement.isMoving)
	{
		Vec3 posToGo = CalcErrorPositionOppositeCircle( m_lastTargetSeenPos, m_luaParams.ST_ENEMY_LOST.maxDistSearch, m_luaParams.ST_ENEMY_LOST.maxDistSearch, m_searchLightPos );
		StartMovingSearchLightTo( posToGo, m_luaParams.ST_ENEMY_LOST.searchSpeed, false );
	}

	// shooting
	m_timeToNextBurst -= ctx.fFrameTime;
	if (m_timeToNextBurst<=0)
	{
		m_timeToNextBurst = m_luaParams.ST_ENEMY_LOST.timeBetweenBursts;
		StartBurst( m_luaParams.ST_ENEMY_LOST.minErrorShoot, m_luaParams.ST_ENEMY_LOST.maxErrorShoot, false );
	}


	m_timeSinceLostTarget += ctx.fFrameTime;
	if (m_timeSinceLostTarget>=m_luaParams.ST_ENEMY_LOST.timeSearching)
		SetState( ST_IDLE );

	CheckAlertAIGroup( ctx );
}



//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::StartBurst( float dispersionMin, float dispersionMax, bool predictTargetMovement )
{
	OutputFlowEvent( "Burst" );
	m_burstsDoneInThisState ++;
	m_burst.predictTargetMovement = predictTargetMovement;

	int numWeaponsEnabled = m_luaParams.numWeaponsSpots - m_numWeaponsSpotDisabled;

	if (numWeaponsEnabled>0)
	{
		m_burst.isDoingBurst = true;
		m_burst.shootsDone = 0;
		m_burst.preshootsDone = 0;
		m_burst.timeForNextShoot = m_luaParams.preshootTime;
		m_burst.timeForNextPreshoot = 0;
		m_burst.dispersionErrorStep = ( dispersionMax - dispersionMin ) / max( numWeaponsEnabled-1, 1 );  // 
		m_burst.currDispersionError = dispersionMin + (numWeaponsEnabled-1) * m_burst.dispersionErrorStep;
		m_soundPreshoot.Play( GetEntityId() );
	}
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::UpdateBurst( const SEntityUpdateContext& ctx )
{
	if (m_burst.isDoingBurst)
	{
		m_burst.timeForNextPreshoot -= ctx.fFrameTime;

		// preshoot
		if (m_burst.preshootsDone<m_luaParams.numWeaponsSpots && m_burst.timeForNextPreshoot<=0)
		{
			m_burst.timeForNextPreshoot = m_luaParams.timeBetweenShootsInABurst;
			int weaponIndex = m_burst.preshootsDone;
			TWeaponSpot& weaponSpot = m_weaponSpot[weaponIndex];
			if (weaponSpot.isWeaponEnabled)
			{
				float estimatedAmmoFlyTime = m_weapon.EstimateProjectileFlyTime( weaponSpot.weaponPos, m_lastTargetSeenPos );
				float estimatedTimeToImpact = m_luaParams.preshootTime + estimatedAmmoFlyTime;
				Vec3 futureTargetPos = m_burst.predictTargetMovement ? CalcFutureTargetPos( estimatedTimeToImpact ) : m_lastTargetSeenPos;

				weaponSpot.laserTargetPos = CalcErrorPosition( futureTargetPos, m_burst.currDispersionError/2, m_burst.currDispersionError );
				m_burst.currDispersionError = max( m_burst.currDispersionError-m_burst.dispersionErrorStep, 0.f );
				OutputFlowEvent( "Preshoot" );
				ShowLaser( weaponIndex, estimatedTimeToImpact );
			}
			m_burst.preshootsDone++;
		}


		// shoot
		m_burst.timeForNextShoot -= ctx.fFrameTime;
		if (m_burst.timeForNextShoot<=0)
		{
			m_burst.timeForNextShoot = m_luaParams.timeBetweenShootsInABurst;
			int weaponIndex = m_burst.shootsDone;
			TWeaponSpot& weaponSpot = m_weaponSpot[weaponIndex];
			if (weaponSpot.isWeaponEnabled)
			{
				OutputFlowEvent( "Shoot" );
				m_weapon.Shoot( weaponSpot.weaponPos, weaponSpot.laserTargetPos );
			}
			m_burst.timeForNextShoot = m_luaParams.timeBetweenShootsInABurst;
			m_burst.shootsDone ++;
			if (m_burst.shootsDone>=m_luaParams.numWeaponsSpots)
				m_burst.isDoingBurst = false;
		}
	}
}


//////////////////////////////////////////////////////////////////////////
Vec3 CTowerSearchLight::CalcFutureTargetPos( float time )
{
	Vec3 futureTargetPos = m_lastTargetSeenPos;

	IActor* pActor = g_pGame->GetIGameFramework()->GetIActorSystem()->GetActor( m_targetIdInView );
	if (pActor)
	{
		const SActorPhysics& actorPhysics = (static_cast<CActor*>(pActor))->GetActorPhysics();
		Vec3 speedVec = actorPhysics.velocity;

		if (speedVec.GetLengthSquared2D()>=m_luaParams.minPlayerSpeedForOffsetDistPrediction)
		{
			Vec3 movementPrediction = speedVec * time;
			movementPrediction.z = 0;
			if (m_luaParams.maxDistancePrediction>0 && movementPrediction.GetLengthSquared2D()>m_luaParams.maxDistancePrediction*m_luaParams.maxDistancePrediction)
			{
				float overshoot = movementPrediction.GetLength2D() - m_luaParams.maxDistancePrediction;
				Vec3 overshootVec = movementPrediction.GetNormalized() * overshoot;
				movementPrediction -= overshootVec;
			}

			futureTargetPos += movementPrediction;

			// move farther the position a distance along the direction the target is looking at, juts to place the shoots always in front(ish) of the player for gameplay feeling reasons.
			Vec3 targetDir = pActor->GetEntity()->GetForwardDir();
			targetDir.z = 0;
			targetDir.Normalize();
			futureTargetPos += targetDir * m_luaParams.offsetDistPrediction;
		}
	}
	return futureTargetPos;
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::UpdateLasers( const SEntityUpdateContext& ctx )
{
	for (uint32 i=0; i<m_luaParams.numWeaponsSpots; ++i)
	{
		TWeaponSpot& weaponSpot = m_weaponSpot[i];

		if (weaponSpot.isLaserActive)
		{
			weaponSpot.laserDurationLeft -= ctx.fFrameTime;
			if (weaponSpot.laserDurationLeft<=0)
			{
				weaponSpot.isLaserActive = false;
				DisableRenderSlot( weaponSpot.laserSlot );
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::ShowLaser( int numWeaponSpot, float duration )
{
	assert( numWeaponSpot>=0 && numWeaponSpot<MAX_NUM_WEAPON_SPOTS );
	TWeaponSpot& weaponSpot = m_weaponSpot[numWeaponSpot];
	weaponSpot.isLaserActive = true;
	weaponSpot.laserDurationLeft = duration;
	EnableRenderSlot( weaponSpot.laserSlot );
	UpdateLaserTM( weaponSpot );
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::UpdateLaserTM( const TWeaponSpot& weaponSpot )
{
	if (weaponSpot.isLaserActive && weaponSpot.laserSlot!=-1)
	{
		Vec3 dir = weaponSpot.laserTargetPos - weaponSpot.weaponPos;
		if (dir.GetLengthSquared()>0.1f) // just in case
		{
			Matrix33 matEndRot;
			matEndRot.SetRotationVDir( dir.GetNormalized() );
			Matrix34 matEnd = matEndRot;
			matEnd.SetTranslation( weaponSpot.weaponPos );
			Matrix34 invMatParent( GetEntity()->GetWorldTM() );
			invMatParent.Invert();
			Matrix34 matLocal = invMatParent * matEnd;
			Matrix34 matLocalScaled = matLocal * Matrix34::CreateScale( m_laserScale );
			GetEntity()->SetSlotLocalTM( weaponSpot.laserSlot, matLocalScaled );
			EnableRenderSlot( weaponSpot.laserSlot );
		}
	}
}


void CTowerSearchLight::UpdateLasersTM()
{
	for (uint32 i=0; i<m_luaParams.numWeaponsSpots; ++i)
	{
		TWeaponSpot& weaponSpot = m_weaponSpot[i];
		UpdateLaserTM( weaponSpot );
	}
}

void CTowerSearchLight::DisableLasers()
{
	for (uint32 i=0; i<m_luaParams.numWeaponsSpots; ++i)
	{
		if (m_weaponSpot[i].isLaserActive)
		{
			m_weaponSpot[i].isLaserActive = false;
			DisableRenderSlot( m_weaponSpot[i].laserSlot );
		}
	}
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::EnableRenderSlot( int slot )
{
	GetEntity()->SetSlotFlags( slot, GetEntity()->GetSlotFlags(slot)|ENTITY_SLOT_RENDER);
}
void CTowerSearchLight::DisableRenderSlot( int slot )
{
	GetEntity()->SetSlotFlags( slot, GetEntity()->GetSlotFlags(slot)&(~ENTITY_SLOT_RENDER));
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::SetState( EState newState )
{
	LeavingState();
	m_state = newState;
	EnteringState();
}


//////////////////////////////////////////////////////////////////////////
//....is assumed that this is called when we are leaving the current m_state
void CTowerSearchLight::LeavingState()
{
	switch (m_state)
	{
		case ST_ENEMY_IN_VIEW:
		{
			m_timeToUnlockDetectionSoundSequence = m_luaParams.ST_ENEMY_IN_VIEW.detectionSoundSequenceCoolDownTime;
			OutputFlowEvent( "PlayerLost" );
			break;
		}
	}
}


//////////////////////////////////////////////////////////////////////////
// assumed that this is called right after m_state is set with the new value
void CTowerSearchLight::EnteringState()
{
	m_burstsDoneInThisState = 0;
	DisableLasers();
	m_burst.isDoingBurst = false;

	switch (m_state)
	{
	case ST_IDLE:
		{
			m_lightMovement.isMoving = false;
			break;
		}

	case ST_ENEMY_IN_VIEW:
		{
			m_lightMovement.isMoving = false;
			m_timeToNextBurst = m_luaParams.ST_ENEMY_IN_VIEW.timeToFirstBurst;
			IAIActor* pClientAIActor = GetClientAIActor();
			if (pClientAIActor && pClientAIActor->IsInvisibleFrom( GetEntity()->GetWorldPos(), true ))
				m_timeToNextBurst = m_luaParams.ST_ENEMY_IN_VIEW.timeToFirstBurstIfStealth;
			m_timeToMove = 0;
			m_audioDetectionTimeCounter = 0;
			for (uint i=0; i<MAX_NUM_SOUNDS_DETECTION; ++i)
				m_soundsDetection[i].played = false;
			RecalculateTargetPos( m_targetIdInView );
			LookAtPos( m_lastTargetSeenPos );
			OutputFlowEvent( "PlayerDetected" );
			NotifyGroupTargetSpotted();
			m_enemyHasEverBeenInView = true;
			break;
		}

	case ST_ENEMY_LOST:
		{
			m_timeToNextBurst = m_luaParams.ST_ENEMY_LOST.timeToFirstBurst;
			StartMovingSearchLightTo( m_lastTargetSeenPos, m_luaParams.ST_ENEMY_LOST.searchSpeed );
			m_timeSinceLostTarget = 0;
			break;
		}
	}
}




//////////////////////////////////////////////////////////////////////////
// returns a random position in 2d around a given one, with a min and max radius
Vec3 CTowerSearchLight::CalcErrorPosition( const Vec3& pos, float minErrorDist, float maxErrorDist )
{
	const float errAng = DEG2RAD( cry_frand() * 360 );
	const float errDist = cry_frand() * ( maxErrorDist - minErrorDist ) + minErrorDist;

	float fSin;
	float fCos;
	cry_sincosf( errAng, &fSin, &fCos );

	Vec3 dir( fSin, fCos, 0.f );

	const Vec3 outPos = pos + dir * errDist;
	return outPos;
}


//////////////////////////////////////////////////////////////////////////
// similar to CalcErrorPosition(), but the "angle" is not totally random, it is centered at the opposite side of the circle relative to a given 'origin' position
Vec3 CTowerSearchLight::CalcErrorPositionOppositeCircle( const Vec3& pos, float minErrorDist, float maxErrorDist, const Vec3& posOrigin )
{
	Vec3 posOriginDif = posOrigin - pos;
	posOriginDif.z = 0;
	if (posOriginDif.IsZero())
		return CalcErrorPosition( pos, minErrorDist, maxErrorDist );

	float angOrigin = cry_atan2f( posOriginDif.x, posOriginDif.y );

	const float SPREAD_ANG = 120.f; // the final position will be calculated along an arc of this size, centered at the opposite position relative to posOrigin
	float randAng = (cry_frand() * SPREAD_ANG)-(SPREAD_ANG/2);
	const float errAng = DEG2RAD( randAng ) + angOrigin + DEG2RAD(180.f);
	const float errDist = cry_frand() * ( maxErrorDist - minErrorDist ) + minErrorDist;

	float fSin;
	float fCos;
	cry_sincosf( errAng, &fSin, &fCos );

	Vec3 dir( fSin, fCos, 0.f );

	const Vec3 outPos = pos + dir * errDist;
	return outPos;
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::StartMovingSearchLightTo( const Vec3& pos, float speed, bool linear )
{
	m_lightMovement.posDeltaEnd = pos - m_searchLightPos;
	float dist = m_lightMovement.posDeltaEnd.GetLength2D();

	m_lightMovement.isMoving = true;
	m_lightMovement.duration = max( dist / speed, 0.000001f);
	m_lightMovement.timeStart = gEnv->pTimer->GetFrameStartTime().GetSeconds();
	m_lightMovement.posStart = m_searchLightPos;
	m_lightMovement.isLinear = linear;
	m_lightMovement.smoothCD_val = 0;
	m_lightMovement.smoothCD_valRate = 0;
}



//////////////////////////////////////////////////////////////////////////
bool CTowerSearchLight::UpdateMovementSearchLight( const SEntityUpdateContext& ctx )
{
	bool justFinished = false;
	if (m_lightMovement.isMoving)
	{
		//........
		if (m_lightMovement.isLinear)
		{
			float normTime = ( ctx.fCurrTime - m_lightMovement.timeStart ) / m_lightMovement.duration;
			if (normTime>=1.f)
			{
				m_lightMovement.isMoving = false;
				normTime = 1.f;
				justFinished = true;
			}

			Vec3 pos = m_lightMovement.posStart + m_lightMovement.posDeltaEnd * normTime;
			LookAtPos( pos );
		}
		else //.................
		{
			SmoothCD( m_lightMovement.smoothCD_val, m_lightMovement.smoothCD_valRate, ctx.fFrameTime, 1.f, m_lightMovement.duration );
			Vec3 pos = m_lightMovement.posStart + ( m_lightMovement.posDeltaEnd * m_lightMovement.smoothCD_val );
			LookAtPos( pos );
			if (fabsf(1.f-m_lightMovement.smoothCD_val)<0.05f) // arbitrary value to check when near enough to finish
			{
				m_lightMovement.isMoving = false;
				justFinished = true;
			}
		}

	}
	return justFinished;
}



//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::ProcessEvent( SEntityEvent& entityEvent )
{
	switch(entityEvent.event)
	{
		case ENTITY_EVENT_XFORM:
		{
			UpdateVisionParams();
			break;
		}

		case ENTITY_EVENT_RESET:
		{
			Reset();
			m_soundBackground.Stop();
			break;
		}

		case ENTITY_EVENT_START_GAME:
		{
			UpdateAttachmentsVisibility();
			if (m_enabled)
			{
				m_soundBackground.Play( m_entityIdleMovement );
				m_weapon.OnEnable();
			}
			break;
		}

		case ENTITY_EVENT_LINK:
		case ENTITY_EVENT_DELINK:
		{
			ReloadAttachmentsInfo();
			break;
		}
	}
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::Reset()
{
	const bool forceDisable = true;
	Disable( forceDisable ); // we want to disable even if the state was already "disabled". This is to make sure that everything is properly initialized.
	ReadLuaProperties();
	if (m_luaParams.enabledFromStart)
		Enable();
}



//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::Enable()
{
	if (m_enabled)
		return;

	m_sleeping = false;
	m_lightMovement.isMoving = false;
	m_lastTargetSeenPos = Vec3Constants<float>::fVec3_Zero;
	m_searchLightPos = Vec3Constants<float>::fVec3_Zero;
	m_lastAIGroupAlertPos = Vec3Constants<float>::fVec3_Zero;
	m_audioDetectionTimeCounter = 0;
	m_timeToCheckAlertAIGroup = 0;
	m_timeToUnlockDetectionSoundSequence = 0;
	m_numWeaponsSpotDisabled = 0;
	m_visionPersistenceTimeLeft = 0;

	for (uint32 i=0; i<MAX_NUM_SOUNDS_DETECTION; ++i)
	{
		m_soundsDetection[i].played = false;
		m_soundsDetection[i].lastTimePlayed = 0;
	}

	m_weapon.OnEnable();
	SetState( ST_IDLE );

	m_soundBackground.Play( m_entityIdleMovement );
	LookAtEntity( m_entityIdleMovement );
	gEnv->pEntitySystem->AddEntityEventListener( m_entityIdleMovement, ENTITY_EVENT_XFORM, this );

	UpdateAIEventListener();
	EnableVision();
	m_enabled = true;

	UpdateAttachmentsVisibility();
	GetGameObject()->EnableUpdateSlot( this, MY_MAIN_UPDATE_SLOT );
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::Sleep()
{
	m_sleeping = true;
	m_targetIdInView = 0;
	if (m_state!=ST_IDLE)
		SetState( ST_IDLE );
}

void CTowerSearchLight::Wakeup()
{
	m_sleeping = false;
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::UpdateAIEventListener()
{
	gEnv->pAISystem->RegisterAIEventListener( this, GetEntity()->GetWorldPos(), m_luaParams.hearingRange, (1<<AISTIM_SOUND) | (1<<AISTIM_BULLET_HIT) | (1<<AISTIM_EXPLOSION) ); // internally is updated if is already registered
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::Disable( bool force )
{
	if (!m_enabled && !force)
		return;

	DisableLasers();

	if (gEnv->pAISystem)
	{
		gEnv->pAISystem->GetVisionMap()->UnregisterObserver( m_visionId );
		gEnv->pAISystem->UnregisterAIEventListener( this );
	}

	if (gEnv->pEntitySystem)
		gEnv->pEntitySystem->RemoveEntityEventListener( m_entityIdleMovement, ENTITY_EVENT_XFORM, this );

	GetGameObject()->DisableUpdateSlot( this, MY_MAIN_UPDATE_SLOT );

	m_soundBackground.Stop( GetEntityId() );
	m_enabled = false;

	if (!gEnv->IsEditor() || gEnv->IsEditorGameMode()) // to avoid hiding attachments when the user is just editing the "enabled" checkbox in the editor.
		UpdateAttachmentsVisibility();
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::EnableVision()
{
	m_targetIdInView = 0;
	m_targetIdInViewCone = 0;
	stack_string visionIdName = GetEntity()->GetName();
	visionIdName += ".Vision";
	IVisionMap* pVisionMap = gEnv->pAISystem->GetVisionMap();
	m_visionId = pVisionMap->CreateVisionID( visionIdName );

	ObserverParams observerParams;
	observerParams.orientationUpdateTreshold = 0;
	observerParams.factionMask = ~0u;
	observerParams.entityID = GetEntityId();

	observerParams.primaryFoVCos = m_visionFOVCos;
	observerParams.peripheralFoVCos = m_visionFOVCos;

	observerParams.callback = functor( *this, &CTowerSearchLight::CallBackViewChanged );
	observerParams.typeMask = Player;
	observerParams.priority = eVeryHighPriority;

	observerParams.skipList[ 0 ] = GetEntity()->GetPhysics();
	observerParams.skipListSize = GetEntity()->GetPhysics() ? 1 : 0;

	observerParams.eyePos = GetEntity()->GetWorldPos();
	observerParams.eyeDir = GetEntity()->GetForwardDir().GetNormalized();
	observerParams.sightRange = m_luaParams.visionRange;
	pVisionMap->RegisterObserver( m_visionId, observerParams );
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::SetEntityIdleMovement( EntityId entityId )
{
	if (m_enabled && m_entityIdleMovement!=0)
	{
		gEnv->pEntitySystem->RemoveEntityEventListener( m_entityIdleMovement, ENTITY_EVENT_XFORM, this );
	}

	m_entityIdleMovement = entityId;
	if (m_enabled)
	{
		gEnv->pEntitySystem->AddEntityEventListener( m_entityIdleMovement, ENTITY_EVENT_XFORM, this );
		LookAtEntity( m_entityIdleMovement );
		UpdateVisionParams();
	}
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::RecalculateTargetPos( EntityId targetId)
{
	IEntity* pEntity = gEnv->pEntitySystem->GetEntity( targetId );
	if (pEntity)
	{
		AABB box;
		pEntity->GetWorldBounds(box);
		m_lastTargetSeenPos = box.GetCenter();
	}
}

//////////////////////////////////////////////////////////////////////////
///.......callback from the VisionMap............................................................
void CTowerSearchLight::CallBackViewChanged( const VisionID& observerID, const ObserverParams& observerParams, const VisionID& observableID, const ObservableParams& observableParams, bool visible )
{
	if (!m_enabled)
		return;

	// for now (and probably forever), this turret thing only works with the player
	IActor* pClientActor = gEnv->pGame->GetIGameFramework()->GetClientActor();
	if (!pClientActor || observableParams.entityID!=pClientActor->GetEntityId())
		return;

	m_targetIdInViewCone = visible ? observableParams.entityID : 0;
}



//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::OnEntityEvent( IEntity *pEntity, SEntityEvent &event )
{
	switch (event.event)
	{
		case ENTITY_EVENT_XFORM:
		{
			if (m_enabled && m_entityIdleMovement==pEntity->GetId() && m_state==ST_IDLE)
			{
				LookAtPos( pEntity->GetWorldPos() );
			}
			break;
		}
	}
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::OnAIEvent(EAIStimulusType type, const Vec3& pos, float radius, float threat, EntityId sender)
{
	if (!m_enabled || m_sleeping)
		return;
	if (threat<1.f)
		return;

	IActor* pClientActor = gEnv->pGame->GetIGameFramework()->GetClientActor();
	if (!pClientActor)
		return;

	if (pClientActor->GetEntityId()!=sender && sender!=0)
		return;

	float maxDist = m_luaParams.hearingRange;
	float maxDist2 = maxDist * maxDist;
	float dist2 = ( pos - GetEntity()->GetWorldPos() ).GetLengthSquared2D();

	if (dist2>maxDist2)
		return;

	TargetDetectedIndirectly( pos );
}


//////////////////////////////////////////////////////////////////////////
// called when a sound is heard, or the associated AI group sees the player
void CTowerSearchLight::TargetDetectedIndirectly( const Vec3& pos )
{
	switch (m_state)
	{
		case ST_IDLE: 
			m_lastTargetSeenPos = pos;
			SetState( ST_ENEMY_LOST );
			m_lightMovement.isMoving = false;
			LookAtPos( m_lastTargetSeenPos ); // from ST_IDLE, we want the light to directly jump into position
			OutputFlowEvent( "SoundHeard" );
			break;
		case ST_ENEMY_LOST:
			m_lastTargetSeenPos = pos;
			m_timeSinceLostTarget = 0;
			StartMovingSearchLightTo( m_lastTargetSeenPos, m_luaParams.ST_ENEMY_LOST.searchSpeed ); // from ST_ENEMY_LOST, we just move the light (should not be that far away anyway )
			break;
	}
}


//////////////////////////////////////////////////////////////////////////
// apparently we currently dont have a better way to do this
// a middle option would be to add this code as a new centralized module into GameAISystem, but for now here is good enough
void CTowerSearchLight::CheckAlertAIGroup( const SEntityUpdateContext& ctx )
{
	if (m_AlertAIGroupID==-1 || m_sleeping)
		return;

	m_timeToCheckAlertAIGroup -= ctx.fFrameTime;
	if (m_timeToCheckAlertAIGroup>0)
		return;

	const float TIME_BETWEEN_ALERT_AIGROUP_CHECKS = 10.f;
	m_timeToCheckAlertAIGroup = TIME_BETWEEN_ALERT_AIGROUP_CHECKS;

	IActorIteratorPtr actorIt = gEnv->pGame->GetIGameFramework()->GetIActorSystem()->CreateActorIterator();
	while (IActor *pActor=actorIt->Next())
	{
		const IAIObject* pAIObject = pActor->GetEntity()->GetAI();
		if (pAIObject && pAIObject->GetGroupId()==m_AlertAIGroupID)
		{
			const IAIActor* pAIActor = pAIObject->CastToIAIActor();
			if (pAIActor && pAIActor->IsActive())
			{
				int alertness = pAIActor->GetProxy()->GetAlertnessState();
				const IAIObject* pAttentionTarget = pAIActor->GetAttentionTarget();
				if (alertness>0 && pAttentionTarget)
				{
					float dist2 = m_lastAIGroupAlertPos.GetSquaredDistance2D( pAttentionTarget->GetPos() );
					float MIN_DIST_TO_USE_NEWPOS = 5;
					if (dist2>(MIN_DIST_TO_USE_NEWPOS*MIN_DIST_TO_USE_NEWPOS))
					{
						m_lastAIGroupAlertPos = pAttentionTarget->GetPos();
						TargetDetectedIndirectly( pAttentionTarget->GetPos() );
						break;
					}
				}
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////
// Caution, don't use this code as a reference on how to communicate with other ai group members!
// Since the tower is not even an AIObject, we need to workaround the issue that it doesn't have
// a real attention target on the ai system side, and no real way to share it with the other members of the group.
void CTowerSearchLight::NotifyGroupTargetSpotted()
{
	if (m_AlertAIGroupID==-1)
		return;

	IAIObject* const pAllyAIObject = gEnv->pAISystem->GetGroupMember( m_AlertAIGroupID, 0 );
	if (!pAllyAIObject)
		return;

	const tAIObjectID allyAIObjectId = pAllyAIObject->GetAIObjectID();

	TargetTrackHelpers::SStimulusEvent stimulusEventInfo;
	stimulusEventInfo.eStimulusType = TargetTrackHelpers::eEST_Generic;
	stimulusEventInfo.eTargetThreat = AITHREAT_THREATENING;
	stimulusEventInfo.vPos = m_lastTargetSeenPos;

	const float radius = 100.0f;

	ITargetTrackManager* const pTargetTrackManager = gEnv->pAISystem->GetTargetTrackManager();
	pTargetTrackManager->HandleStimulusEventInRange( allyAIObjectId, "TurretSpottedTarget", stimulusEventInfo, radius );

	const char* const signalName = "GroupMemberEnteredCombat";
	const uint32 signalNameCrc32 = gEnv->pSystem->GetCrc32Gen()->GetCRC32( signalName );
	gEnv->pAISystem->SendSignal( SIGNALFILTER_GROUPONLY, 1, signalName, pAllyAIObject, NULL, signalNameCrc32 );
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::UpdateVisionParams()
{
	ObserverParams observerParams;
	observerParams.eyeDir = GetEntity()->GetForwardDir().GetNormalized();
	gEnv->pAISystem->GetVisionMap()->ObserverChanged( m_visionId, observerParams, eChangedOrientation );
}

//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::OnPropertyChange()
{
	ReadLuaProperties();
	if (m_luaParams.enabledFromStart)
		Enable();
	else
		Disable();
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::ReadLuaProperties()
{
	// set fallback values. should be redundant as all values should come from the .lua file.
	m_luaParams.enabledFromStart = true;
	m_luaParams.canDetectStealth = false;
	m_luaParams.alwaysSeePlayer = false;
	float visionFOV = 5.f;
	m_luaParams.visionRange = 1000.f;
	m_luaParams.hearingRange = 200.f;
	m_luaParams.visionPersistenceTime = 0;
	m_luaParams.minPlayerSpeedForOffsetDistPrediction = 0;
	m_luaParams.maxDistancePrediction = 0;
	m_AlertAIGroupID = -1;
	m_luaParams.burstDispersion = 10;
	m_luaParams.timeBetweenShootsInABurst = 0.5f;
	m_luaParams.offsetDistPrediction = 10;
	m_luaParams.preshootTime = 2;

	const char* pWeaponName = NULL;
	const char* pLaserBeamModelName = NULL;
	const char* pSoundBackgroundName = NULL;
	const char* pSoundShootName = NULL;
	const char* pSoundPreshootName = NULL;
	float laserBeamThicknessScale = 20.f;

	IScriptTable* pScriptTable = GetEntity()->GetScriptTable();
	if (!pScriptTable)
		return;

	SmartScriptTable pPropertiesTable;
	const bool hasPropertiesTable = pScriptTable->GetValue( "Properties", pPropertiesTable );
	if (!hasPropertiesTable)
		return;

	pPropertiesTable->GetValue( "bEnabled", m_luaParams.enabledFromStart );
	pPropertiesTable->GetValue( "bCanDetectStealth", m_luaParams.canDetectStealth );
	pPropertiesTable->GetValue( "bAlwaysSeePlayer", m_luaParams.alwaysSeePlayer );
	pPropertiesTable->GetValue( "visionFOV", visionFOV );
	pPropertiesTable->GetValue( "visionRange", m_luaParams.visionRange );
	pPropertiesTable->GetValue( "hearingRange", m_luaParams.hearingRange );
	pPropertiesTable->GetValue( "visionPersistenceTime", m_luaParams.visionPersistenceTime );
	pPropertiesTable->GetValue( "minPlayerSpeedForOffsetDistPrediction", m_luaParams.minPlayerSpeedForOffsetDistPrediction );
	pPropertiesTable->GetValue( "maxDistancePrediction", m_luaParams.maxDistancePrediction );
	pPropertiesTable->GetValue( "alertAIGroupId", m_AlertAIGroupID );
	pPropertiesTable->GetValue( "burstDispersion", m_luaParams.burstDispersion );
	pPropertiesTable->GetValue( "timeBetweenShootsInABurst", m_luaParams.timeBetweenShootsInABurst );
	pPropertiesTable->GetValue( "offsetDistPrediction", m_luaParams.offsetDistPrediction );
	pPropertiesTable->GetValue( "preshootTime", m_luaParams.preshootTime );

	pPropertiesTable->GetValue( "weapon", pWeaponName );
	pPropertiesTable->GetValue( "objLaserBeamModel", pLaserBeamModelName );
	pPropertiesTable->GetValue( "audioBackground", pSoundBackgroundName );
	pPropertiesTable->GetValue( "audioShoot", pSoundShootName );
	pPropertiesTable->GetValue( "audioPreshoot", pSoundPreshootName );
	pPropertiesTable->GetValue( "laserBeamThicknessScale", laserBeamThicknessScale );

	m_visionFOVCos = cosf( DEG2RAD( visionFOV ) );


	m_soundBackground.SetSignalSafe( pSoundBackgroundName );
	m_soundPreshoot.SetSignalSafe( pSoundPreshootName );
	m_weapon.SetSoundShot( pSoundShootName );
	m_weapon.SetWeaponClass( pWeaponName );


	// behaviour params......
	{
		// fallback values
		m_luaParams.ST_ENEMY_IN_VIEW.timeToFirstBurst = 1.5f;
		m_luaParams.ST_ENEMY_IN_VIEW.timeToFirstBurstIfStealth = 1.5f;
		m_luaParams.ST_ENEMY_IN_VIEW.numWarningBursts = 0;
		m_luaParams.ST_ENEMY_IN_VIEW.timeBetweenWarningBursts = 5.f;
		m_luaParams.ST_ENEMY_IN_VIEW.errorAddedToWarningBursts = 10.f;
		m_luaParams.ST_ENEMY_IN_VIEW.timeBetweenBursts = 5.f;
		m_luaParams.ST_ENEMY_IN_VIEW.followDelay = 1.f;
		m_luaParams.ST_ENEMY_IN_VIEW.trackSpeed = 3.f;
		m_luaParams.ST_ENEMY_IN_VIEW.detectionSoundSequenceCoolDownTime = 20.f;

		m_luaParams.ST_ENEMY_LOST.maxDistSearch = 6.f;
		m_luaParams.ST_ENEMY_LOST.timeSearching = 10.f;
		m_luaParams.ST_ENEMY_LOST.searchSpeed = 3.f;
		m_luaParams.ST_ENEMY_LOST.timeToFirstBurst = 1000.f;
		m_luaParams.ST_ENEMY_LOST.timeBetweenBursts = 10.f;
		m_luaParams.ST_ENEMY_LOST.minErrorShoot = 5.f;
		m_luaParams.ST_ENEMY_LOST.maxErrorShoot = 15.f;

		ScriptTablePtr tableBehaviour;
		if (pPropertiesTable->GetValue("behaviour", tableBehaviour))
		{
			ScriptTablePtr table_ST_ENEMY_IN_VIEW;
			if (tableBehaviour->GetValue("enemyInView", table_ST_ENEMY_IN_VIEW))
			{
				table_ST_ENEMY_IN_VIEW->GetValue( "timeToFirstBurst", m_luaParams.ST_ENEMY_IN_VIEW.timeToFirstBurst );
				table_ST_ENEMY_IN_VIEW->GetValue( "timeToFirstBurstIfStealth", m_luaParams.ST_ENEMY_IN_VIEW.timeToFirstBurstIfStealth );
				table_ST_ENEMY_IN_VIEW->GetValue( "numWarningBursts", m_luaParams.ST_ENEMY_IN_VIEW.numWarningBursts );
				table_ST_ENEMY_IN_VIEW->GetValue( "timeBetweenWarningBursts", m_luaParams.ST_ENEMY_IN_VIEW.timeBetweenWarningBursts );
				table_ST_ENEMY_IN_VIEW->GetValue( "errorAddedToWarningBursts", m_luaParams.ST_ENEMY_IN_VIEW.errorAddedToWarningBursts );
				table_ST_ENEMY_IN_VIEW->GetValue( "timeBetweenBursts", m_luaParams.ST_ENEMY_IN_VIEW.timeBetweenBursts );
				table_ST_ENEMY_IN_VIEW->GetValue( "followDelay", m_luaParams.ST_ENEMY_IN_VIEW.followDelay );
				table_ST_ENEMY_IN_VIEW->GetValue( "trackSpeed", m_luaParams.ST_ENEMY_IN_VIEW.trackSpeed );
				table_ST_ENEMY_IN_VIEW->GetValue( "detectionSoundSequenceCoolDownTime", m_luaParams.ST_ENEMY_IN_VIEW.detectionSoundSequenceCoolDownTime );

				for (uint i=0; i<MAX_NUM_SOUNDS_DETECTION; ++i)
				{
					const char* pSoundDetectionName = NULL;
					stack_string tableName;
					ScriptTablePtr tableAudioDetection;
					tableName.Format("audioDetection%d", i+1 );
					if (table_ST_ENEMY_IN_VIEW->GetValue( tableName.c_str(), tableAudioDetection))
					{
						tableAudioDetection->GetValue( "audio", pSoundDetectionName );
						if (pSoundDetectionName && pSoundDetectionName[0]!=0)
							m_soundsDetection[i].signal.SetSignal( pSoundDetectionName );
						else
							m_soundsDetection[i].signal.SetSignal( INVALID_AUDIOSIGNAL_ID );
						tableAudioDetection->GetValue( "delay", m_soundsDetection[i].delay );
					}
				}
			}
			ScriptTablePtr table_ST_ENEMY_LOST;
			if (tableBehaviour->GetValue("enemyLost", table_ST_ENEMY_LOST))
			{
				table_ST_ENEMY_LOST->GetValue( "maxDistSearch", m_luaParams.ST_ENEMY_LOST.maxDistSearch );
				table_ST_ENEMY_LOST->GetValue( "timeSearching", m_luaParams.ST_ENEMY_LOST.timeSearching );
				table_ST_ENEMY_LOST->GetValue( "searchSpeed", m_luaParams.ST_ENEMY_LOST.searchSpeed );
				table_ST_ENEMY_LOST->GetValue( "timeToFirstBurst", m_luaParams.ST_ENEMY_LOST.timeToFirstBurst );
				table_ST_ENEMY_LOST->GetValue( "timeBetweenBursts", m_luaParams.ST_ENEMY_LOST.timeBetweenBursts );
				table_ST_ENEMY_LOST->GetValue( "minErrorShoot", m_luaParams.ST_ENEMY_LOST.minErrorShoot );
				table_ST_ENEMY_LOST->GetValue( "maxErrorShoot", m_luaParams.ST_ENEMY_LOST.maxErrorShoot );
			}
		}
	}


	// weapon spots
	{
		int numActiveWeaponSpots = 0;
		bool laserScaleCalculated = false;
		m_laserScale = Vec3(1,1,1);

		for (uint32 i=0; i<MAX_NUM_WEAPON_SPOTS; ++i)
		{
			m_weaponSpot[i].Reset();
		}

		ScriptTablePtr tableWeaponSpots;
		if (pPropertiesTable->GetValue("weaponSpots", tableWeaponSpots))
		{
			int slot = 0;
			for (uint32 i=0; i<MAX_NUM_WEAPON_SPOTS; ++i)
			{
				ScriptTablePtr tableWeaponSpot;
				stack_string tableName;
				tableName.Format("spot%d",i+1);
				if (tableWeaponSpots->GetValue( tableName.c_str(), tableWeaponSpot))
				{
					bool isEnabled = false;
					tableWeaponSpot->GetValue( "bEnabled", isEnabled );
					if (isEnabled)
					{
						TWeaponSpot& weaponSpot = m_weaponSpot[numActiveWeaponSpots];

						weaponSpot.isWeaponEnabled = true;
						Vec3 offsetPos = Vec3(0,0,0);
						tableWeaponSpot->GetValue( "vOffset", offsetPos );
						weaponSpot.weaponPos = GetEntity()->GetWorldPos() + offsetPos;

						weaponSpot.laserSlot = GetEntity()->LoadGeometry( slot, pLaserBeamModelName );
						slot = max( weaponSpot.laserSlot + 1, 0 );

						if (weaponSpot.laserSlot!=-1 && !laserScaleCalculated)
						{
							laserScaleCalculated = true;
							SEntitySlotInfo slotInfo;
							GetEntity()->GetSlotInfo( weaponSpot.laserSlot, slotInfo );
							if (slotInfo.pStatObj)
							{
								AABB bbox = slotInfo.pStatObj->GetAABB();
								m_laserScale = Vec3( laserBeamThicknessScale, m_luaParams.visionRange/(bbox.max.y+0.00001f), laserBeamThicknessScale );
							}
						}

						numActiveWeaponSpots ++;
					}
				}
			}
		}
		m_luaParams.numWeaponsSpots = numActiveWeaponSpots;
	}

	ReloadAttachmentsInfo();
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::ReloadAttachmentsInfo()
{
	IScriptTable* pScriptTable = GetEntity()->GetScriptTable();
	if (!pScriptTable)
		return;

	SmartScriptTable pPropertiesTable;
	const bool hasPropertiesTable = pScriptTable->GetValue( "Properties", pPropertiesTable );
	if (!hasPropertiesTable)
		return;

	for (uint32 i=0; i<MAX_NUM_ATTACHMENTS; ++i)
		m_attachments[i].entityId = 0;

	ScriptTablePtr tableListAttachments;
	if (pPropertiesTable->GetValue("attachments", tableListAttachments))
	{
		for (uint32 i=0; i<MAX_NUM_ATTACHMENTS; ++i)
		{
			ScriptTablePtr tableAttachment;
			stack_string tableName;
			tableName.Format("attachment%d",i+1);
			if (tableListAttachments->GetValue( tableName.c_str(), tableAttachment))
			{
				const char* pLinkName = NULL;
				tableAttachment->GetValue( "linkName", pLinkName );
				m_attachments[i].entityId = GetEntityLinkByName( pLinkName );
				tableAttachment->GetValue( "distFromTarget", m_attachments[i].distFromTarget );
				float rotZ;
				tableAttachment->GetValue( "rotationZ", rotZ );
				m_attachments[i].rot.SetRotationZ( DEG2RAD(rotZ) );
			}
		}
	}
}



//////////////////////////////////////////////////////////////////////////
EntityId CTowerSearchLight::GetEntityLinkByName( const char* pLinkName )
{
	if (!pLinkName)
		return 0;

	IEntityLink* pLink = GetEntity()->GetEntityLinks();

	while (pLink)
	{
		if (stricmp(pLink->name, pLinkName)==0)
		{
			return pLink->entityId;
		}
		pLink = pLink->next;
	}
	return 0;
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::LookAtPos( const Vec3& targetPos )
{
	Vec3 dir = targetPos - GetEntity()->GetWorldPos();
	Matrix34 mat;
	mat.SetTranslation( GetEntity()->GetWorldPos() );
	Matrix33 rot;
	rot.SetRotationVDir( dir.GetNormalized() );
	mat.SetRotation33( rot );
	GetEntity()->SetWorldTM( mat );
	m_searchLightPos = targetPos;
	UpdateAIEventListener();
	UpdateVisionParams();
	UpdateAttachmentsPos();
	UpdateLasersTM();

	Vec3 soundRelPos(0, (targetPos - GetEntity()->GetWorldPos()).GetLength(), 0);
	m_soundBackground.SetOffsetPos( GetEntityId(), soundRelPos );
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::UpdateAttachmentsPos()
{
	Quat rotTower = GetEntity()->GetWorldRotation();
	for (uint32 i=0; i<MAX_NUM_ATTACHMENTS; ++i)
	{
		IEntity* pEntityAttachment = gEnv->pEntitySystem->GetEntity( m_attachments[i].entityId );
		if (pEntityAttachment)
		{
			Quat worldRot = rotTower * m_attachments[i].rot;
			pEntityAttachment->SetRotation( worldRot );
			// positions the attachment in the line from the tower to the searchlight position, at a distance "distFromTarget" from the searchlight position.
			float posYLoc = ( m_searchLightPos - GetEntity()->GetWorldPos() ).GetLengthFast() - m_attachments[i].distFromTarget;
			Vec3 worldPos = GetEntity()->GetWorldTM() * Vec3( 0, posYLoc, 0 );
			pEntityAttachment->SetPos( worldPos );
		}
	}
}



//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::UpdateAttachmentsVisibility()
{
	for (int i=0; i<MAX_NUM_ATTACHMENTS; ++i)
	{
		IEntity* pAttachment = gEnv->pEntitySystem->GetEntity( m_attachments[i].entityId );
		if (pAttachment)
			pAttachment->Hide( !m_enabled );
	}
}



//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::LookAtEntity( EntityId entityId )
{
	IEntity* pEntity = gEnv->pEntitySystem->GetEntity( entityId );
	if (pEntity)
		LookAtPos( pEntity->GetPos() );
}

//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::PlayDetectionSounds( const SEntityUpdateContext& ctx )
{
	if (m_timeToUnlockDetectionSoundSequence<=0 && m_targetIdInView)
	{
		m_timeToUnlockDetectionSoundSequence = m_luaParams.ST_ENEMY_IN_VIEW.detectionSoundSequenceCoolDownTime;
		m_audioDetectionTimeCounter += ctx.fFrameTime;
		const float MIN_TIME_TO_REPEAT_DETECTION_SOUNDS = 3.f;

		for (uint32 i=0; i<MAX_NUM_SOUNDS_DETECTION; ++i)
		{
			TAudioDetection& soundInfo = m_soundsDetection[i];
			if (!soundInfo.played && ctx.fCurrTime - soundInfo.lastTimePlayed > MIN_TIME_TO_REPEAT_DETECTION_SOUNDS && m_audioDetectionTimeCounter>=soundInfo.delay)
			{
				soundInfo.signal.Play( GetEntityId() );
				soundInfo.lastTimePlayed = ctx.fCurrTime;
				soundInfo.played = true;
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::DisableWeaponSpot( uint32 nSpot )
{
	if (nSpot>=0 && nSpot<m_luaParams.numWeaponsSpots)
	{
		if (nSpot<MAX_NUM_WEAPON_SPOTS) // just in case
		{
			m_weaponSpot[nSpot].isWeaponEnabled = false;
			m_weaponSpot[nSpot].isLaserActive = false;
			DisableRenderSlot(m_weaponSpot[nSpot].laserSlot);
		}
	}
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::OutputFlowEvent( const char* pOutputName )
{
	SEntityEvent event( ENTITY_EVENT_SCRIPT_EVENT );
	event.nParam[0] = (INT_PTR)pOutputName;
	event.nParam[1] = IEntityClass::EVT_BOOL;
	bool bValue = true;
	event.nParam[2] = (INT_PTR)&bValue;
	GetEntity()->SendEvent( event );
}


//////////////////////////////////////////////////////////////////////////
int CTowerSearchLight::GetAwarenessToActor( IAIObject* pAIObject, CActor* pActor ) const
{
	int awareness = 0;

	if (m_enabled)
	{
		if (m_state==ST_ENEMY_IN_VIEW)
			awareness = 2;
		else
			if (m_enemyHasEverBeenInView)
				awareness = 1;
	}

	return awareness;
}



//////////////////////////////////////////////////////////////////////////
#ifdef DEBUG_INFO_TOWERSEARCHLIGHT
void CTowerSearchLight::ShowDebugInfo( bool isLimitedByName )
{
	enum 
	{
		FL_VISLINE					= BIT(0),
		FL_HEARING_RANGE		= BIT(1),
		FL_ATTACHMENTS			= BIT(2),
		FL_STATES						= BIT(3),
		FL_WEAPONPOTS				= BIT(4),
		FL_MOVEMENT         = BIT(5),
	};

	if (isLimitedByName && (m_pcvarDebugTower->GetIVal()&FL_STATES)!=0) // only show states info when the infodebug is limited to 1 tower
	{
		float white[] = {1.0f,1.0f,1.0f,1.0f};
		float posX = 20;
		float posY = 100;

		const char* pStateName = "<UNKNOWN>";
		switch (m_state)
		{
			case ST_IDLE: pStateName = "ST_IDLE"; break;
			case ST_ENEMY_IN_VIEW: pStateName = "ST_ENEMY_IN_VIEW"; break;
			case ST_ENEMY_LOST: pStateName = "ST_ENEMY_LOST"; break;
		}

		gEnv->pRenderer->Draw2dLabel(posX, posY, 2.0f, white, false, "state: %s   sleep: %d", pStateName, m_sleeping );
		posY +=20;

#ifdef FULL_DEBUG_INFO_TOWERSEARCHLIGHT
		if (m_state!=ST_IDLE)
		{
			gEnv->pRenderer->Draw2dLabel(posX, posY,      2.0f, white, false, "m_targetIdInViewCone: %d", m_targetIdInViewCone);
			gEnv->pRenderer->Draw2dLabel(posX, posY+20,		2.0f, white, false, "m_timeToMove: %.3f", m_timeToMove);
			gEnv->pRenderer->Draw2dLabel(posX, posY+20*2,	2.0f, white, false, "m_timeToNextBurst: %.3f", m_timeToNextBurst);
			gEnv->pRenderer->Draw2dLabel(posX, posY+20*3, 2.0f, white, false, "doing burst: %d", m_burst.isDoingBurst);
			posY += 20*4;
			if (m_state==ST_ENEMY_LOST)
			{
				gEnv->pRenderer->Draw2dLabel(posX, posY, 2.0f, white, false, "m_timeSinceLostTarget: %.3f", m_timeSinceLostTarget);
				posY += 20;
			}
			if (m_burst.isDoingBurst)
			{
				gEnv->pRenderer->Draw2dLabel(posX, posY, 2.0f, white, false,			"        preshootsDone: %d shootsDone: %d  ", m_burst.preshootsDone, m_burst.shootsDone );
				gEnv->pRenderer->Draw2dLabel(posX, posY+20*1, 2.0f, white, false, "        timeForNextPre/Shoot: %.2f/%.2f, currDispersionError: %.2f", m_burst.timeForNextPreshoot, m_burst.timeForNextShoot, m_burst.currDispersionError);
				posY += 20*2;
			}
			ShowMovementDebugInfo( posX, posY );
		}
#endif
	}

	if (m_lightMovement.isMoving && (m_pcvarDebugTower->GetIVal()&FL_MOVEMENT)!=0)
	{
		gEnv->pRenderer->GetIRenderAuxGeom()->DrawLine( m_lightMovement.posStart+Vec3(0,0,0.5f), Col_White, m_lightMovement.posStart + m_lightMovement.posDeltaEnd + Vec3(0,0,0.5f), Col_White );
		gEnv->pRenderer->GetIRenderAuxGeom()->DrawCylinder( m_lastTargetSeenPos, Vec3(0,0,1), m_luaParams.ST_ENEMY_LOST.maxDistSearch, 0.25f, Col_Pink );
	}

	if ( (m_pcvarDebugTower->GetIVal()&FL_VISLINE)!=0)
	{
		Vec3 dirView = GetEntity()->GetForwardDir().GetNormalized();
		Vec3 posEnd = GetEntity()->GetWorldPos() + ( dirView * m_luaParams.visionRange );
		gEnv->pRenderer->GetIRenderAuxGeom()->DrawLine( GetEntity()->GetWorldPos(), Col_Red, posEnd, Col_Red );
	}

	if ( (m_pcvarDebugTower->GetIVal()&FL_WEAPONPOTS)!=0)
	{
		for (uint32 i=0; i<m_luaParams.numWeaponsSpots; i++)
		{
			TWeaponSpot& weaponSpot = m_weaponSpot[i];

			gEnv->pRenderer->GetIRenderAuxGeom()->DrawSphere( weaponSpot.weaponPos, 1, Col_Red, true );
		}
	}


	if ( (m_pcvarDebugTower->GetIVal()&FL_ATTACHMENTS)!=0)
	{
		for (uint32 i=0; i<MAX_NUM_ATTACHMENTS; ++i)
		{
			IEntity* pEntityAttachment = gEnv->pEntitySystem->GetEntity( m_attachments[i].entityId );
			ColorB colors[]={ Col_Red, Col_Green, Col_Blue, Col_White, Col_Black };
			uint numColors = sizeof(colors) / sizeof(colors[0]);
			if (pEntityAttachment)
			{
				gEnv->pRenderer->GetIRenderAuxGeom()->DrawSphere( pEntityAttachment->GetWorldPos(), 1, colors[i%numColors], true );
			}
		}
	}

	if ( (m_pcvarDebugTower->GetIVal()&FL_HEARING_RANGE)!=0)
	{
		Vec3 pos = GetEntity()->GetWorldPos();
		pos.z = 0;
		gEnv->pRenderer->GetIRenderAuxGeom()->DrawCylinder( pos, Vec3(0,0,1), m_luaParams.hearingRange, GetEntity()->GetWorldPos().z*2, ColorB(255,255,255,255) );
	}
}
#endif

#ifdef FULL_DEBUG_INFO_TOWERSEARCHLIGHT
void CTowerSearchLight::ShowMovementDebugInfo( float posX, float posY )
{
	float white[] = {1.0f,1.0f,1.0f,1.0f};

	if (m_lightMovement.isMoving)
	{
		gEnv->pRenderer->Draw2dLabel(posX, posY, 2.0f, white, false, "Duration: %f", m_lightMovement.duration );
		gEnv->pRenderer->Draw2dLabel(posX, posY+20, 2.0f, white, false, "TimeToEnd: %f", m_lightMovement.duration - ( gEnv->pTimer->GetFrameStartTime().GetSeconds() - m_lightMovement.timeStart ));
	}
}

#endif

//////////////////////////////////////////////////////////////////////////
CTowerSearchLight::CWeaponManager::CWeaponManager()
: m_towerId( 0 )
, m_weaponId ( 0 )
{
}

//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::CWeaponManager::SetOwnerId( EntityId towerId )
{
	m_towerId = towerId;
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::CWeaponManager::SetSoundShot( const char* pSoundShotName )
{
	m_soundShoot.SetSignalSafe( pSoundShotName );
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::CWeaponManager::SetWeaponClass( const char* pWeaponClassName )
{
	IEntity* pWeaponEntity = gEnv->pEntitySystem->GetEntity( m_weaponId );
	IEntityClass* pNewClass = gEnv->pEntitySystem->GetClassRegistry()->FindClass( pWeaponClassName );

	if (!pWeaponEntity || pWeaponEntity->GetClass()!=pNewClass)
	{
		if (pWeaponEntity)  // this is supposed to only happen inside editor, when the weapon type is changed
		{
			const bool forceRemoveNow = true;
			gEnv->pEntitySystem->RemoveEntity( m_weaponId, forceRemoveNow );
		}
		InitWeapon( pWeaponClassName );
	}
}

//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::CWeaponManager::InitWeapon( const char* pWeaponClassName )
{
	assert( m_weaponId==0 );
	IEntity* pEntity = gEnv->pEntitySystem->GetEntity( m_towerId );
	stack_string weaponName = pEntity->GetName();
	weaponName += ".Weapon";

	SEntitySpawnParams entitySpawnParams;
	entitySpawnParams.sName = weaponName;
	entitySpawnParams.nFlags |= ( ENTITY_FLAG_NO_PROXIMITY | ENTITY_FLAG_NEVER_NETWORK_STATIC );

	entitySpawnParams.pClass = gEnv->pEntitySystem->GetClassRegistry()->FindClass( pWeaponClassName );
	if ( entitySpawnParams.pClass == NULL )
		return;

	IEntity* pWeaponEntity = gEnv->pEntitySystem->SpawnEntity( entitySpawnParams );
	if (pWeaponEntity)
		m_weaponId = pWeaponEntity->GetId();

	IWeapon* pWeapon = GetWeapon();
	if (pWeapon)
		pWeapon->AddEventListener( this, __FUNCTION__ );
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::CWeaponManager::OnEnable()
{
	IWeapon* pWeapon = GetWeapon();
	if (pWeapon)
		pWeapon->AddEventListener( this, __FUNCTION__ );
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::CWeaponManager::Shoot( const Vec3& weaponPos, const Vec3& targetPos )
{
	IEntity* pWeaponEntity = gEnv->pEntitySystem->GetEntity( m_weaponId );
	if (!pWeaponEntity)
		return;

	IWeapon* pWeapon = GetWeapon();
	if (!pWeapon)
		return;

	pWeaponEntity->SetPos( weaponPos );

	// update weapon orientation
	Vec3 dir = targetPos - weaponPos;
	dir.NormalizeSafe(Vec3Constants<float>::fVec3_OneY);
	Matrix33 rotation = Matrix33::CreateRotationVDir( dir );
	pWeaponEntity->SetRotation( Quat( rotation ) );

	// target pos
	pWeapon->SetDestination( targetPos );

	// replenish ammo
	IFireMode *pFireMode = pWeapon->GetFireMode(pWeapon->GetCurrentFireMode());
	if (pFireMode)
		pWeapon->SetAmmoCount( pFireMode->GetAmmoType(), pFireMode->GetClipSize() );

	pWeapon->StartFire();
	m_soundShoot.Play( m_towerId );
}


//////////////////////////////////////////////////////////////////////////
void CTowerSearchLight::CWeaponManager::OnShoot( IWeapon *pWeapon, EntityId shooterId, EntityId ammoId, IEntityClass* pAmmoType, const Vec3 &pos, const Vec3 &dir, const Vec3 &vel)
{
	pWeapon->StopFire();
}


//////////////////////////////////////////////////////////////////////////
IWeapon* CTowerSearchLight::CWeaponManager::GetWeapon()
{
	IItem* pItem = gEnv->pGame->GetIGameFramework()->GetIItemSystem()->GetItem( m_weaponId );
	return pItem ? pItem->GetIWeapon() : NULL;
}


//////////////////////////////////////////////////////////////////////////
float CTowerSearchLight::CWeaponManager::EstimateProjectileFlyTime( const Vec3& weaponPos, const Vec3& targetPos )
{
	float flyTime = 0;
	IWeapon* pWeapon = GetWeapon();
	if (pWeapon)
	{
		IFireMode* pFireMode = pWeapon->GetFireMode( pWeapon->GetCurrentFireMode() );
		if (pFireMode)
		{
			IEntityClass* pAmmoClass = pFireMode->GetAmmoType();
			const SAmmoParams* pAmmoParams = g_pGame->GetWeaponSystem()->GetAmmoParams( pAmmoClass );
			if (pAmmoParams && pAmmoParams->speed>0.0001f)
			{
				float distance = weaponPos.GetDistance( targetPos );
				flyTime = distance / pAmmoParams->speed;
			}
		}
	}
	return flyTime;
}
