/*************************************************************************
Crytek Source File.
Copyright (C), Crytek Studios, 2001-2009.
-------------------------------------------------------------------------
$Id:$
$DateTime$
Description:  Mounted machine gun that can be ripped off by the player
and move around with it
-------------------------------------------------------------------------
History:
- 20:01:2009: Created by Benito G.R.
  30:09:2009: Ported from Rippable turret gun

*************************************************************************/

#include "StdAfx.h"
#include "Cannon.h"
#include "GameActions.h"
#include "Game.h"
#include "GameInputActionHandlers.h"
#include "Player.h"
#include "ScreenEffects.h"
#include "ItemSharedParams.h"
#include "GameRules.h"
#include "UI/HUD/HUDEventWrapper.h"
#include "Battlechatter.h"
#include "PersistantStats.h"
#include "RecordingSystem.h"
#include "FireMode.h"
#include "Melee.h"
#include "ItemAnimation.h"

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

namespace
{


	bool s_ripOffPromptIsVisible = false;


	void DoRipOffPrompt(EntityId who, bool doIt)
	{
		if (who == g_pGame->GetIGameFramework()->GetClientActorId() && doIt != s_ripOffPromptIsVisible)
		{
			const char* ripoffInteraction = "@ui_interaction_ripofforstopusing";
			if (doIt)
			{
				SHUDEventWrapper::InteractionRequest(true, ripoffInteraction, "special", "player", 1.0f);
			}
			else
			{
				SHUDEventWrapper::ClearInteractionMsg(eHIMT_INTERACTION, ripoffInteraction);
			}

			s_ripOffPromptIsVisible = doIt;
		}
	}


}


struct CCannon::EndRippingOff
{
	EndRippingOff(CCannon *_weapon): pHMGWeapon(_weapon){};

	CCannon *pHMGWeapon;

	void execute(CItem *_this)
	{
		pHMGWeapon->FinishRipOff();
	}
};



struct CCannon::RemoveViewLimitsAction
{
	RemoveViewLimitsAction(CCannon *_weapon): pHMGWeapon(_weapon){};

	CCannon *pHMGWeapon;

	void execute(CItem *_this)
	{
		CActor *pOwner = pHMGWeapon->GetOwnerActor();

		if (pOwner)
		{
			SActorParams &params = pOwner->GetActorParams();

			params.viewLimits.SetViewLimit(Vec3Constants<float>::fVec3_OneY, 0.0f, 0.01f, 0.f, 0.f, SViewLimitParams::eVLS_Item);
		}
	}
};



//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

CCannon::CCannon():
m_rippedOff(false),
m_rippingOff(false),
m_linkedParentId(0),
m_rotatingSoundID(INVALID_SOUNDID),
m_RotationSoundTimeOut(0),
m_lastXAngle(0),
m_lastZAngle(0),
m_lastUsedFrame(-1)
{
	RegisterActionsCannon();
}

CCannon::~CCannon()
{
	if(m_stats.used)
	{
		m_stats.dropped = true; //prevent StopUse calling Drop
		StopUse(GetOwnerId());
	}
}


void CCannon::RegisterActionsCannon()
{
	CGameInputActionHandlers::TCannonActionHandler& CannonActionHandler = g_pGame->GetGameInputActionHandlers().GetCCannonActionHandler();

	if (CannonActionHandler.GetNumHandlers() == 0)
	{
#define ADD_HANDLER(action, func) CannonActionHandler.AddHandler(actions.action, &CCannon::func)
		const CGameActions& actions = g_pGame->Actions();

		ADD_HANDLER(special,OnActionRipOff);
		ADD_HANDLER(weapon_change_firemode,OnActionFiremode);
#undef ADD_HANDLER
	}
}


bool CCannon::Init(IGameObject * pGameObject)
{
	BaseClass::Init(pGameObject);
	//GetEntity()->LoadCharacter(0,"Objects/drakes_legacy/weapons/cannons/cannon_24pdr_9ft6in_bronze_tall.cga");
	Physicalize(true, true);
	return true;
}

void CCannon::OnReset()
{
	BaseClass::OnReset();
	Recovering = false; //HoO
	Recoiling = false; //HoO
	Recovered = false;
	m_rippedOff = m_rippingOff = false;
	m_stats.mounted = true;
	m_rotatingSoundID = INVALID_SOUNDID;
	m_lastUsedFrame = -1;
	parentOffset = Vec3(0);
	
	Physicalize(true, true);

	RequireUpdate(eIUS_General);

	//get the properties for the recoil
	SmartScriptTable properties;
	IScriptTable* pScriptTable = GetEntity()->GetScriptTable();
	pScriptTable->GetValue("Properties", properties);
	if(pScriptTable  && properties)
	{   
	   properties->GetValue("fRecoilDistance",recoilDistance);
	   properties->GetValue("fRecoilStep",recoilStep);
	   properties->GetValue("fRecoverStep",recoverStep);
	}


	//m_linkedParentId = GetEntity()->GetParent()->GetId();
	if (m_linkedParentId != 0)
	{
		IEntity* pLinkedParent = gEnv->pEntitySystem->GetEntity(m_linkedParentId);
		if (pLinkedParent)
		{
			Vec3 parentPos = pLinkedParent->GetPos();
			Vec3 childPos = GetEntity()->GetPos();
			pLinkedParent->AttachChild(GetEntity());
			parentOffset = parentPos - childPos;
		} else {
			parentOffset = Vec3(0);
		}

		m_linkedParentId = 0;
	}


	//find out where this cannon will recoil to and start from
	const Matrix34& weaponTM = GetEntity()->GetWorldTM();
	//returnPos = weaponTM.GetTranslation(); //this is the starting position
	returnPos = GetEntity()->GetPos();
	//const Vec3 point3 = returnPos - (recoilDistance * weaponTM.GetColumn1()); //this is the ending position
	recoilPos = returnPos - (recoilDistance * weaponTM.GetColumn1());
	moveDistance = 0;


	//The next lines are to cache the trail effect for rockets
    CItemParticleEffectCache& particleCache = g_pGame->GetGameSharedParametersStorage()->GetItemResourceCache().GetParticleEffectCache();
            //Edit the xml files in GameSDK\Scripts\Entities\Items\XML\Ammo
            //To find the strings for these effects - e.g. Rocket.xml
            //See 5 posts down if you want code to read
            //this effect name from the Rocket.xml file

    particleCache.CacheParticle("weapon_fx.cannon.cannonball.muzzle_flash");
}

//HoO
void CCannon::OnShoot(EntityId userId, EntityId ammoId, IEntityClass* pAmmoType, const Vec3 &pos, const Vec3 &dir, const Vec3&vel)
{
	if (!Recovering && !Recoiling) { //if the cannon is still recovering from the last shot or currently firing, don't fire
	BaseClass::OnShoot(userId, ammoId, pAmmoType, pos, dir, vel);

	const ItemString& soundName = m_stats.fp ? m_sharedparams->pMountParams->shoot_sound_fp : m_sharedparams->pMountParams->shoot_sound_tp;
	if (m_sharedparams->pMountParams) 
	{
		if (soundName)
		{
			tSoundID result = 0;
			IEntitySoundProxy* pSoundProxy = GetSoundProxy(true);
			const ItemString& soundName = m_stats.fp ? m_sharedparams->pMountParams->shoot_sound_fp : m_sharedparams->pMountParams->shoot_sound_tp;
			pSoundProxy->PlaySoundEx(soundName.c_str(), ZERO, FORWARD_DIRECTION, FLAG_SOUND_DEFAULT_3D, 0, 1.0f, 0, 2000, eSoundSemantic_Weapon);
			pSoundProxy->Done();
		}
	}

	moveDistance = 0;
	Recoiling = true;

	/*IScriptSystem* pSS = gEnv->pScriptSystem;
	IScriptTable *pScriptTable = this->GetEntity()->GetScriptTable();
    pSS->BeginCall(pScriptTable,"Recoil");
    pSS->PushFuncParam("1");   
    pSS->PushFuncParam("3");     
    pSS->EndCall();
*/
	//Recoiling = true; //stars the recoil update process
	
	////this->GetEntity()->EnablePhysics(true);
	//GetEntity()->LoadCharacter(0,"Objects/drakes_legacy/weapons/cannons/cannon_24pdr_9ft6in_bronze_tall.cga");
	//m_object = this->GetEntity()->GetCharacter(0);
	//////m_object = GetEntity()->LoadCharacter(0,"Objects/drakes_legacy/weapons/cannons/cannon_24pdr_9ft6in_bronze_tall.cga");

	//if (m_object) {
	//	PlayAnimation(m_sharedparams->pMountParams->shoot_recoil_anim.c_str(), false, 3.0f);
	//};

	};
}

bool CCannon::PlayAnimation( const char *animation, bool bLooped, float fBlendTime )
{
	bool playing = false;

	if (m_object)
	{
		ICharacterInstance *character = GetEntity()->GetCharacter(0);
		ISkeletonAnim* pSkeleton;
		if(m_object && (pSkeleton=m_object->GetISkeletonAnim()))
		{
			CryCharAnimationParams Params;
			Params.m_fAllowMultilayerAnim = 1;
			Params.m_nLayerID = 0;
			if(bLooped)
				Params.m_nFlags |= CA_LOOP_ANIMATION;
				Params.m_nFlags |= CA_REMOVE_FROM_FIFO;
				//Params.m_nUserToken = token;

				Params.m_fTransTime = fBlendTime;
				Params.m_fPlaybackSpeed = 1.2f;
				//CryLogAlways("ANIMATION");
				pSkeleton->StartAnimation(animation, Params);
		}
		/*
		ISkeletonAnim* pSkeleton = m_object->GetISkeletonAnim();
		assert(pSkeleton);

		CryCharAnimationParams animParams;
		if (bLooped)
			animParams.m_nFlags |= CA_LOOP_ANIMATION;
		animParams.m_fTransTime = fBlendTime;

		const int amountAnimationsInFIFO = pSkeleton->GetNumAnimsInFIFO(0);
		const uint32 maxAnimationsAllowedInQueue = 2;
		if(amountAnimationsInFIFO >= maxAnimationsAllowedInQueue)
		{
			animParams.m_nFlags |= CA_REMOVE_FROM_FIFO;
		}

		playing = pSkeleton->StartAnimation( animation, animParams );
		assert(pSkeleton->GetNumAnimsInFIFO(0) <= maxAnimationsAllowedInQueue);
		m_object->SetPlaybackScale( 1.0f );
		*/ 
	}
	return playing;
}


void CCannon::RecoilImpulse(const Vec3& firingPos, const Vec3& firingDir)
{
	EntityId id = this->GetEntityId(); // (m_pWeapon->GetHostId()) ? m_pWeapon->GetHostId() : m_pWeapon->GetOwnerId();
	IEntity* pEntity = gEnv->pEntitySystem->GetEntity(id);
	IPhysicalEntity* pPhysicalEntity = pEntity ? pEntity->GetPhysics() : NULL;

	if (pPhysicalEntity)
	{        
		pe_action_impulse impulse;
		impulse.impulse = -firingDir * 10.0f; 
		impulse.point = firingPos;
		pPhysicalEntity->Action(&impulse);
	}
}
//HoO - End


void CCannon::UpdateFPView(float frameTime)
{
	BaseClass::UpdateFPView(frameTime);

	/*if(!gEnv->bMultiplayer)*/
	{
		if(!m_rippedOff && !m_rippingOff)
		{
			CActor* pOwner = GetOwnerActor();
			if(!pOwner)
				return;

			if (CanRipOff())
			{
				if(gEnv->bMultiplayer || !pOwner->GetLinkedVehicle())
				{
					DoRipOffPrompt(GetOwnerId(), true);
				}
			}
		}
	}
}

void CCannon::OnAction(EntityId actorId, const ActionId& actionId, int activationMode, float value)
{
	if(m_rippingOff)
		return;

	bool filtered = false;

	CGameInputActionHandlers::TCannonActionHandler& CannonActionHandler = g_pGame->GetGameInputActionHandlers().GetCCannonActionHandler();

	bool handled = CannonActionHandler.Dispatch(this, actorId, actionId, activationMode, value, filtered);
	if(!handled || !filtered)
	{
		BaseClass::OnAction(actorId, actionId, activationMode, value);
	}
}

bool CCannon::OnActionRipOff(EntityId actorId, const ActionId& actionId, int activationMode, float value)
{
	//TODO: This has to be synchronized for MP
	if((activationMode == eAAM_OnPress) && !m_rippedOff && !m_rippingOff)
	{
		if(CanRipOff())
		{
			TryRipOffGun();
			ClearInputFlag(eWeaponAction_Fire);
		}
		return true;
	}

	return false;
}

bool CCannon::OnActionFiremode(EntityId actorId, const ActionId& actionId, int activationMode, float value)
{
	//DO NOTHING... (filters command, and doesn't allow to switch fire modes

	return true;
}

void CCannon::Use(EntityId userId)
{
	int frameID = gEnv->pRenderer->GetFrameID();
	if (m_lastUsedFrame == frameID)
		return;

	m_lastUsedFrame = frameID;

	if (!m_owner.GetId())
	{
		StartUse(userId);
		HighlightWeapon(false);
	}
	else if (m_owner.GetId() == userId)
	{
		if (m_rippedOff || m_rippingOff)
		{
			FinishRipOff();
			DeselectAndDrop(userId);
		}
		else
		{
			StopUse(userId);
		}
	}
}

bool CCannon::CanPickUp(EntityId userId) const
{
	if (!m_rippedOff)
		return false;

	return BaseClass::CanPickUp(userId);
}

bool CCannon::CanUse(EntityId userId) const
{
	EntityId ownerId = m_owner.GetId();

	if (m_rippedOff)
	{
		CActor* pActor = GetActor(userId);
		if (pActor && pActor->IsSwimming())
			return false;

		if (ownerId == 0 || ownerId == userId)
			return true;
	}
	else if(IActor* pActor = gEnv->pGame->GetIGameFramework()->GetIActorSystem()->GetActor(userId))
	{
		IItem* pItem = pActor->GetCurrentItem(false);
		if(pItem)
		{
			if(pItem->IsBusy())
			{
				return false;
			}
			if(IWeapon* pWeapon = pItem->GetIWeapon())
			{
				if(pWeapon->IsReloading())
				{
					return false;
				}
			}
		}
	}

	return BaseClass::CanUse(userId);
}

void CCannon::StartUse(EntityId userId)
{
	HighlightWeapon(false);

	if(m_rippedOff)
	{
		m_stats.dropped = false;
		BaseClass::StartUse(userId);
	}
	else
	{
		CWeapon::StartUse(userId);

		if (IsOwnerFP())
		{
			DrawSlot(eIGS_FirstPerson, true, true);
		}
	}

	RegisterAsUser();

	CActor* pOwner = GetOwnerActor();
	if (pOwner)
	{
		CHANGED_NETWORK_STATE(pOwner, CPlayer::ASPECT_CURRENT_ITEM);

		if (!m_rippedOff)
		{
			IPlayerInput* pPlayerInput = pOwner && pOwner->IsPlayer() ? static_cast<CPlayer*>(pOwner)->GetPlayerInput() : 0;
			if (pPlayerInput)
				pPlayerInput->ClearXIMovement();
		}
	}

	m_expended_ammo = 0;
}

void CCannon::StopUse(EntityId userId)
{
	UnRegisterAsUser();

	if (m_rippedOff || m_rippingOff)
	{
		CActor *pActor = GetOwnerActor();

		m_rippedOff = true;
		m_rippingOff = false;
		RemoveViewLimits();
		BaseClass::StopUse(userId);

		if(pActor)
		{
			pActor->LockInteractor(GetEntityId(), false);
		}
	}
	else
	{
		CActor *pActor = GetOwnerActor();
		if (!pActor)
		{
			return;
		}

		if (m_isFiring)
		{
			StopFire();
		}
		DoRipOffPrompt(GetOwnerId(), false);
		SetViewMode(eIVM_ThirdPerson);
		DrawSlot(eIGS_ThirdPerson, true);

		if(gEnv->bMultiplayer)
		{
			HighlightWeapon(true);
		}

		//The use of CWeapon::StopUse() here and not BaseClass::StopUse() is deliberate; it avoids the '::Drop()' call that CHeavyWeapon makes
		CWeapon::StopUse(userId);
	}
}

void CCannon::TryRipOffGun()
{
	CActor *pActor = GetOwnerActor();
	if(!pActor)
		return;

	PerformRipOff(pActor);
	
	if(gEnv->bServer)
	{
		CHANGED_NETWORK_STATE(this, ASPECT_RIPOFF);
	}
	else
	{
		GetGameObject()->InvokeRMI(SvRequestRipOff(), EmptyParams(), eRMI_ToServer);
	}
}

void CCannon::PerformRipOff(CActor* pOwner)
{
	ExitZoom(true);

	UnlinkMountedGun();
	SetUnMountedConfiguration();	// This needs to come after the call to UnlinkMountedGun otherwise killcam doesn't work properly
	AttachToHand(true);
	StopFire();
	Physicalize(false, false);

	if (pOwner)
	{
		HandleHeavyWeaponPro(*pOwner);

		float speedOverride = 1.0f;
		if(pOwner->IsPlayer())
		{
			CPlayer* pOwnerPlayer = static_cast<CPlayer*>(pOwner);
			speedOverride = pOwnerPlayer->GetModifiableValues().GetValue(kPMV_HeavyWeaponRipOffSpeedOverride);
		}

		PlayAction(GetFragmentIds().rip_off, 0, false, eIPAF_Default, speedOverride);

		m_rippingOff = true;
		m_stats.dropped = false;

		DoRipOffPrompt(GetOwnerId(), false);

		int timeDelay = GetCurrentAnimationTime(eIGS_Owner);
		timeDelay = (timeDelay > 0) ? timeDelay : 2000;
		int removeViewLimitDelay = int(timeDelay * 0.65f);
		GetScheduler()->TimerAction(timeDelay, CSchedulerAction<EndRippingOff>::Create(EndRippingOff(this)), false);
		GetScheduler()->TimerAction(removeViewLimitDelay, CSchedulerAction<RemoveViewLimitsAction>::Create(RemoveViewLimitsAction(this)), false);

		if(!pOwner->IsThirdPerson() && !(m_stats.viewmode&eIVM_FirstPerson))
		{
			SetViewMode(eIVM_FirstPerson);
		}

		//Lock view in place during rip off
		SActorParams &params = pOwner->GetActorParams();

		Vec3 limitDir(ZERO);
		
		bool bUseMovementState = true;

		if (pOwner->IsClient() && (g_pGame->GetHostMigrationState() != CGame::eHMS_NotMigrating))
		{
			// If this happens during a host migration, our aim direction may not have made it into the movement
			// controller yet, get it from the saved migration params instead
			const CGameRules::SHostMigrationClientControlledParams *pHostMigrationParams = g_pGame->GetGameRules()->GetHostMigrationClientParams();
			if (pHostMigrationParams)
			{
				limitDir = pHostMigrationParams->m_aimDirection;
				bUseMovementState = false;
			}
		}

		if (bUseMovementState)
		{
			IMovementController *pMovementController = pOwner->GetMovementController();
			SMovementState state;
			pMovementController->GetMovementState(state);

			limitDir = state.aimDirection;
		}

		params.viewLimits.SetViewLimit(limitDir, 0.01f, 0.01f, 0.01f, 0.01f, SViewLimitParams::eVLS_Item);

		pOwner->SetSpeedMultipler(SActorParams::eSMR_Item, 0.0f);

		if(!gEnv->bMultiplayer)
			pOwner->LockInteractor(GetEntityId(), false);

	}

	TriggerRespawn();

	if (pOwner)
	{
		if (CRecordingSystem* pRecordingSystem = g_pGame->GetRecordingSystem()) 
		{
			pRecordingSystem->OnWeaponRippedOff(this);
		}

		BATTLECHATTER(BC_Ripoff, GetOwnerId());

		if(pOwner->IsClient())
		{
			g_pGame->GetPersistantStats()->IncrementClientStats(EIPS_RipOffMountedWeapon);
			ClearInputFlag(eWeaponAction_Zoom);
		}
	}
	else
	{
		//--- If ripped off without an actor we should finish instantly
		m_rippingOff = false;
		m_rippedOff = true;
	}
}

void CCannon::UnlinkMountedGun()
{
	CActor* pActor = GetOwnerActor();
	if (pActor)
	{
		pActor->LinkToMountedWeapon(0);
		if (GetEntity()->GetParent())
		{
			m_linkedParentId = GetEntity()->GetParent()->GetId();
			GetEntity()->DetachThis();
		}
	}
	m_stats.mounted = false;
}

void CCannon::GetAngleLimits(EStance stance, float& minAngle, float& maxAngle)
{
	if (!m_rippedOff)
		return;

	const float minAngleStandValue = -50.0f;
	const float minAngleCrouchValue = -20.0f;
	const float maxAngleValue = 70.0f;

	maxAngle = maxAngleValue;
	minAngle = stance==STANCE_CROUCH ? minAngleCrouchValue : minAngleStandValue;
}

bool CCannon::UpdateAimAnims( SParams_WeaponFPAiming &aimAnimParams)
{
	if (!m_rippedOff && !m_rippingOff)
	{
		IFireMode* pFireMode = GetFireMode(GetCurrentFireMode());
		aimAnimParams.shoulderLookParams = 
			pFireMode ?
			&static_cast<CFireMode*>(pFireMode)->GetShared()->aimLookParams :
		&m_sharedparams->params.aimLookParams;

		return true;
	}

	return BaseClass::UpdateAimAnims(aimAnimParams);
}

void CCannon::Update( SEntityUpdateContext& ctx, int slot )
{
	BaseClass::Update(ctx, slot);
	
	if (m_rotatingSoundID!=INVALID_SOUNDID)
	{
		if (m_RotationSoundTimeOut>0)
		{
			m_RotationSoundTimeOut -= ctx.fFrameTime;
			RequireUpdate( eIUS_General );
		}
		else
		{
			StopSound(m_rotatingSoundID);
			m_rotatingSoundID = INVALID_SOUNDID;
		}
	}

	// Helper for editor placing
	if (gEnv->IsEditing())
	{
		// If host id is not 0, it means it is mounted to a vehicle, so don't render the helper in that case
		if (!GetHostId())
		{
			IRenderAuxGeom* pRenderAux = gEnv->pRenderer->GetIRenderAuxGeom();

			const Matrix34& weaponTM = GetEntity()->GetWorldTM();
			const Vec3 point1 = weaponTM.GetTranslation();
			//const Vec3 point2 = point1 - (m_sharedparams->pMountParams->ground_distance * weaponTM.GetColumn2());
			const Vec3 point3 = point1 - (recoilDistance * weaponTM.GetColumn1());

			//pRenderAux->DrawLine(point1, ColorB(0, 192, 0), point2, ColorB(0, 192, 0), 3.0f);
			pRenderAux->DrawLine(point1, ColorB(0, 192, 0), point3, ColorB(0, 192, 0), 3.0f);
			//pRenderAux->DrawSphere(point3, 0.15f, ColorB(192, 0, 0));
			//pRenderAux->DrawLine(point1, ColorB(0, 192, 0), point2, ColorB(0, 192, 0), 3.0f);
			pRenderAux->DrawSphere(returnPos, 0.10f, ColorB(192, 0, 0));
			pRenderAux->DrawSphere(recoilPos, 0.10f, ColorB(192, 192, 0));


			RequireUpdate(eIUS_General);
		}
	}

	//HoO - Recoil
	// 
	if (Recoiling || Recovering || Recovered) 
	{
		const Matrix34& weaponTM = GetEntity()->GetLocalTM();
		//const Vec3 point3 = point1 - (recoilDistance * weaponTM.GetColumn1());
		//Vec3 startingPos = weaponTM.GetTranslation(); 
		if (Recoiling)
		{
			//Vec3 moveTo = this->GetEntity()->GetWorldRotation() * this->GetEntity()->GetForwardDir() * recoilStep ;
			GetEntity()->SetPos(returnPos + (moveDistance * -weaponTM.GetColumn1()));  //returnPos - recoilPos + Vec3(recoilStep,0,0)); //startingPos - (recoilStep * GetEntity()->GetForwardDir()));
			moveDistance = moveDistance + recoilStep;
			if (moveDistance >= recoilDistance) {
				Recoiling = false;
				Recovering = true;
			};
		}
		if (Recovering)
		{
			GetEntity()->SetPos(returnPos + (moveDistance * -weaponTM.GetColumn1()));
			moveDistance = moveDistance - recoverStep;
			if (moveDistance <= 0) {
				//GetEntity()->SetPos(recoilPos);
				Recovering = false;
				Recovered = true;
			};
		}
		if (Recovered)
		{
			moveDistance = 0;
			GetEntity()->SetPos(returnPos);
			Recovered = false;
		}
	}
}

void CCannon::OnFireWhenOutOfAmmo()
{
	BaseClass::OnFireWhenOutOfAmmo();

	if (!IsReloading())
	{
		s_ripOffPromptIsVisible = false;
	}
}

void CCannon::OnOutOfAmmo(IEntityClass* pAmmoType)
{
	BaseClass::OnOutOfAmmo(pAmmoType);
	
	if(gEnv->bMultiplayer)
	{
		CActor * pActor = GetOwnerActor();
		if(pActor)
		{
			pActor->UseItem(GetEntityId());
		}
	}
}

void CCannon::UpdateIKMounted( IActor* pActor, const Vec3& vGunXAxis )
{

}

void CCannon::SwitchToRippedOffFireMode()
{
	if(GetCurrentFireMode() != 1)
	{
		RequestFireMode(1);
	}
}

void CCannon::SetUnMountedConfiguration()
{
	SwitchToRippedOffFireMode();

	IFireMode * pMountedFireMode = GetFireMode(0);
	assert(pMountedFireMode);
	
	pMountedFireMode->Enable(false);

	ExitZoom(true);

	//Second zoom mode is supposed to be unmounted
	if(GetZoomMode(1))
	{
		EnableZoomMode(1, true);
		SetCurrentZoomMode(1);
	}

	//Just in case, it was not clear properly
	CActor* pOwner = GetOwnerActor();
	if ((pOwner != NULL) && pOwner->IsClient())
	{
		float defaultFov = 55.0f;
		gEnv->pRenderer->EF_Query(EFQ_SetDrawNearFov,defaultFov);
	}
}

void CCannon::ProcessEvent(SEntityEvent& event)
{
	if ((event.event == ENTITY_EVENT_XFORM) && IsMounted() && GetOwnerId())
	{
		const float Z_EPSILON = 0.01f;
		const Ang3& worldAngles = GetEntity()->GetWorldAngles();
		float xAngle = worldAngles.x;
		float zAngle = worldAngles.z;
		bool xAnglesAreEquivalent = (fabs(xAngle-m_lastXAngle)<Z_EPSILON);
		bool zAnglesAreEquivalent = (fabs(zAngle-m_lastZAngle)<Z_EPSILON);
		if (!xAnglesAreEquivalent || !zAnglesAreEquivalent)
		{
			if (m_rotatingSoundID==INVALID_SOUNDID)
				m_rotatingSoundID = PlayRotationSound();
			m_RotationSoundTimeOut = 0.15f;
			RequireUpdate( eIUS_General );
			m_lastXAngle = xAngle;
			m_lastZAngle = zAngle;
		}
			
		int flags = (int)event.nParam[0];
		if ((flags & ENTITY_XFORM_FROM_PARENT) && !(flags & ENTITY_XFORM_USER))
		{
			if (CActor* pOwnerActor = GetOwnerActor())
			{
				pOwnerActor->UpdateMountedGunController(true);
			}
		}
	}

	BaseClass::ProcessEvent(event);
}

void CCannon::Select(bool select)
{
	if (select == IsSelected())
		return;
	BaseClass::Select(select);
	if (select && m_rippedOff)
		SetUnMountedConfiguration();
}

void CCannon::FadeCrosshair( float to, float time, float delay)
{
	if (IsMounted())
	{
		BaseClass::FadeCrosshair(to, time, delay);
	}
	else
	{
		BaseClass::FadeCrosshair(1.0f, time, delay);
	}
}

void CCannon::FullSerialize( TSerialize ser )
{
	BaseClass::FullSerialize(ser);

	ser.Value("linkedParentId", m_linkedParentId);
	ser.Value("rippedOff", m_rippedOff);
	ser.Value("rippingOff", m_rippingOff);
	ser.Value("lastZAngle", m_lastZAngle);
}

void CCannon::PostSerialize()
{
	BaseClass::PostSerialize();

	if(m_rippingOff)
	{
		m_rippingOff = false;
		m_rippedOff = true;
	}

	StartUse(GetOwnerId());
}

bool CCannon::NetSerialize(TSerialize ser, EEntityAspects aspect, uint8 profile, int flags)
{
	if (!BaseClass::NetSerialize(ser, aspect, profile, flags))
		return false;

	if(aspect == ASPECT_RIPOFF)
	{
		ser.Value("ripOff", static_cast<CCannon*>(this), &CCannon::IsRippingOrRippedOff, &CCannon::SetRippingOff, 'bool');
	}

	return true;
}

NetworkAspectType CCannon::GetNetSerializeAspects()
{
	return BaseClass::GetNetSerializeAspects() | ASPECT_RIPOFF;
}

void CCannon::InitClient(int channelId)
{
	CWeapon::InitClient(channelId); //Avoid calling CHeavyWeapon::InitClient as mounted weapons need special case logic to handle late joiners (Based on the ripoff state)

	if(m_rippingOff || m_rippedOff)
	{
		IActor *pActor = GetOwnerActor();
		if(pActor)
		{
			EntityId ownerID = pActor->GetEntity()->GetId();
			GetGameObject()->InvokeRMIWithDependentObject(ClRipOff(), BaseClass::SHeavyWeaponUserParams(ownerID), eRMI_ToClientChannel, ownerID, channelId);	
		}
		else
		{
			GetGameObject()->InvokeRMI(ClDropped(), EmptyParams(), eRMI_ToClientChannel, channelId);
		}
	}
	
	if(m_bIsHighlighted && !m_rippingOff)
	{
		GetGameObject()->InvokeRMI(ClHeavyWeaponHighlighted(), SNoParams(), eRMI_ToClientChannel, channelId);		 
	}
}

void CCannon::PostInit( IGameObject * pGameObject )
{
	BaseClass::PostInit(pGameObject);

	if(gEnv->bMultiplayer && !gEnv->IsDedicated())
	{
		if(g_pGame->GetIGameFramework()->GetClientActor() != NULL)
		{
			HighlightWeapon(true);
		}
		else
		{
			g_pGame->GetGameRules()->RegisterClientConnectionListener(this); //We unregister again once we received the event (in CHeavyWeapon::OnOwnClientEnteredGame)
		}
	}
}

void CCannon::SetRippingOff(bool ripOff)
{
	if(ripOff && !m_rippingOff && !m_rippedOff)
	{
		PerformRipOff(GetOwnerActor());
	}
}

void CCannon::FinishRipOff()
{
	m_rippingOff = false;
	m_rippedOff = true;
	
	RemoveViewLimits();

	if(IsClient() && gEnv->pGame->GetIGameFramework()->GetClientActorId()==GetOwnerId())
	{
		if(IEntity* pEntity = GetEntity())
		{
			const char* collectibleId = pEntity->GetClass()->GetName();
			CPersistantStats* pStats = g_pGame->GetPersistantStats();
			if(pStats && pStats->GetStat(collectibleId, EMPS_SPWeaponByName) == 0)
			{
				pStats->SetMapStat(EMPS_SPWeaponByName, collectibleId, eDatabaseStatValueFlag_Available);

				if(!gEnv->bMultiplayer)
				{
					// Show hud unlock msg
					SHUDEventWrapper::DisplayWeaponUnlockMsg(collectibleId);
				}
			}
		}
	}

	CActor* pOwner = GetOwnerActor();
	IActionController* pController = pOwner ? pOwner->GetAnimatedCharacter()->GetActionController() : NULL;
	if(pController)
	{
		CMannequinUserParamsManager& mannequinUserParams = g_pGame->GetIGameFramework()->GetMannequinInterface().GetMannequinUserParamsManager();
		const SMannequinItemParams* pParams = mannequinUserParams.FindOrCreateParams<SMannequinItemParams>(pController);

		UpdateMountedTags(pParams, pController->GetContext().state, true);
	}
}

void CCannon::RemoveViewLimits()
{
	ApplyViewLimit(GetOwnerId(), false);
}

void CCannon::OnBeginCutScene()
{
	if (m_rippedOff)
	{
		Hide(true);
	}
}

void CCannon::OnEndCutScene()
{
	if (m_rippedOff)
	{
		Hide(false);

		PlayAction(GetSelectAction());
	}
}


IMPLEMENT_RMI(CCannon, SvRequestRipOff)
{
	CHECK_OWNER_REQUEST();

	if (!m_rippingOff && !m_rippedOff)
	{
		PerformRipOff(GetOwnerActor());

		CHANGED_NETWORK_STATE(this, ASPECT_RIPOFF);
	}

	return true;
}

IMPLEMENT_RMI(CCannon, ClRipOff)
{
	IActor *pActor = gEnv->pGame->GetIGameFramework()->GetIActorSystem()->GetActor(params.ownerId);
	if(pActor && (!m_rippingOff || m_rippedOff))
	{
		SetUnMountedConfiguration();
		UnlinkMountedGun();
		FinishRipOff();
		StartUse(params.ownerId);
	}

	return true;
}

IMPLEMENT_RMI(CCannon, ClDropped)
{
	if(!m_rippedOff)
	{
		SetUnMountedConfiguration();
		UnlinkMountedGun();
		FinishRipOff();
		EnableUpdate(false);
		BaseClass::Drop(5.0f);
	}

	return true;
}

bool CCannon::CanDeselect() const
{
	return !IsRippingOff();
}

bool CCannon::CanRipOff() const
{
	bool bCanRipOff = true;
	return bCanRipOff;
}

void CCannon::ForceRippingOff( bool ripOff )
{
	SetRippingOff(ripOff);

	if (ripOff)
	{
		// If we're forcing to ripped-off, make sure we're using the right firemode (firemode is server
		// controlled so may have lost during a host migration)
		SwitchToRippedOffFireMode();
	}

	if (gEnv->bServer)
	{
		CHANGED_NETWORK_STATE(this, ASPECT_RIPOFF);
	}
}

void CCannon::ReadProperties(IScriptTable *pProperties)
{
	BaseClass::ReadProperties(pProperties);

	if (gEnv->bMultiplayer && pProperties)
	{
		ReadMountedProperties(pProperties);
	}
}

tSoundID CCannon::PlayRotationSound()
{
	tSoundID result = 0;
	IEntitySoundProxy* pSoundProxy = GetSoundProxy(true);
	if (pSoundProxy)
	{
		const ItemString& soundName = m_stats.fp ? m_sharedparams->pMountParams->rotate_sound_fp : m_sharedparams->pMountParams->rotate_sound_tp;
		result = pSoundProxy->PlaySoundEx(soundName.c_str(), ZERO, FORWARD_DIRECTION, FLAG_SOUND_DEFAULT_3D, 0, 1.0f, 0, 0, eSoundSemantic_Weapon);
	}
	return result;
}

bool CCannon::AllowInteraction( EntityId interactionEntity, EInteractionType interactionType )
{
	if(interactionType==eInteraction_GameRulesPickup && (IsRippingOff()||!IsRippedOff()))
	{
		return false;
	}
	return BaseClass::AllowInteraction(interactionEntity, interactionType);
}
