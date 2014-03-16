/*************************************************************************
Crytek Source File.
Copyright (C), Crytek Studios, 2001-2012.
-------------------------------------------------------------------------

Description: Tower Searchlight entity. 
It routinely follows a path with its searchlight, shots at the player when detects it, 
tries to follow the player after detecting it, reacts to AI seeing the player, etc
*************************************************************************/

#pragma once

#ifndef _TOWER_SEARCHLIGHT_H_
#define _TOWER_SEARCHLIGHT_H_

#include <IVisionMap.h>
#include <IGameObject.h>
#include "IWeapon.h"
#include "Audio/AudioSignalPlayer.h"
#include "AI/AIAwarenessToPlayerHelper.h"

#ifndef _RELEASE
#define DEBUG_INFO_TOWERSEARCHLIGHT
#endif

#ifdef _DEBUG
#define FULL_DEBUG_INFO_TOWERSEARCHLIGHT
#endif


//////////////////////////////////////////////////////////////////////////
class CTowerSearchLight 
	: public CGameObjectExtensionHelper<CTowerSearchLight
	, IGameObjectExtension>, public IEntityEventListener
	, public IAIEventListener
	, public CAIAwarenessToPlayerHelper::IAwarenessEntity
{
	enum EState
	{
		ST_IDLE = 0,				// basic state, searchlight is following the established path (if there is one)
		ST_ENEMY_IN_VIEW,   // enemy is detected, searchlight tries to follow it and shoots it
		ST_ENEMY_LOST,      // enemy was detected but now is not, searchlight moves around a bit trying to find him, sometimes shooting randomly around
	};

	enum { MAX_NUM_ATTACHMENTS = 4};
	enum { MAX_NUM_SOUNDS_DETECTION = 3};
	enum { MAX_NUM_WEAPON_SPOTS = 3 };


public:

	CTowerSearchLight();
	virtual ~CTowerSearchLight();

	// IGameObjectExtension
	virtual bool Init( IGameObject * pGameObject );
	virtual void InitClient( int channelId ) {};
	virtual void PostInit( IGameObject * pGameObject );
	virtual void PostInitClient( int channelId ) {};
	virtual bool ReloadExtension( IGameObject * pGameObject, const SEntitySpawnParams &params ) { CRY_ASSERT(false); ResetGameObject(); return false; }
	virtual void PostReloadExtension( IGameObject * pGameObject, const SEntitySpawnParams &params ) {}
	virtual bool GetEntityPoolSignature( TSerialize signature ) { CRY_ASSERT(false); return true; }
	virtual void Release();
	virtual void FullSerialize( TSerialize ser );
	virtual bool NetSerialize( TSerialize ser, EEntityAspects aspect, uint8 profile, int flags ) { return false; };
	virtual void PostSerialize();
	virtual void SerializeSpawnInfo( TSerialize ser ) {}
	virtual ISerializableInfoPtr GetSpawnInfo() {return 0;}
	virtual void Update( SEntityUpdateContext& ctx, int slot );
	virtual void HandleEvent( const SGameObjectEvent& gameObjectEvent ) {}
	virtual void ProcessEvent( SEntityEvent& entityEvent );
	virtual void SetChannelId( uint16 id ) {};
	virtual void SetAuthority( bool auth ) {};
	virtual void PostUpdate( float frameTime ) { CRY_ASSERT(false); }
	virtual void PostRemoteSpawn() {};
	virtual void GetMemoryUsage( ICrySizer *pSizer ) const {};
	// ~IGameObjectExtension

	// IAwarenessEntity
	virtual int GetAwarenessToActor( IAIObject* pAIObject, CActor* pActor ) const;
	// ~IAwarenessEntity

	void OnEntityEvent( IEntity *pEntity, SEntityEvent &event );
	void OnAIEvent(EAIStimulusType type, const Vec3& pos, float radius, float threat, EntityId sender);

	void SetEntityIdleMovement( EntityId entityId );
	void Enable();
	void Disable( bool force = false );
	void Sleep();
	void Wakeup();
	void OnPropertyChange();
	void DisableWeaponSpot( uint32 nSpot );
	void SetAlertAIGroupID( uint32 AIGroupID ) { m_AlertAIGroupID = AIGroupID; }

	static void RegisterDebugCVars();
private:

	struct TWeaponSpot;

	void Reset();
	void UpdateVisionParams();
	void CallBackViewChanged( const VisionID& observerID, const ObserverParams& observerParams, const VisionID& observableID, const ObservableParams& observableParams, bool visible );
	void EnableVision();
	void LookAtPos( const Vec3& targetPos );
	void LookAtEntity( EntityId entityId );
	void PlayDetectionSounds( const SEntityUpdateContext& ctx );
	void SetState( EState newState );
	void LeavingState();
	void EnteringState();
	void Update_IDLE( const SEntityUpdateContext& ctx );
	void Update_ENEMY_IN_VIEW( const SEntityUpdateContext& ctx );
	void Update_ENEMY_LOST( const SEntityUpdateContext& ctx );
	void StartMovingSearchLightTo( const Vec3& pos, float speed, bool linear=true );
	bool UpdateMovementSearchLight( const SEntityUpdateContext& ctx );
	void RecalculateTargetPos( EntityId targetId );
	void UpdateAIEventListener();
	void Update_TargetVisibility( SEntityUpdateContext& ctx );
	EntityId GetEntityLinkByName( const char* pLinkName );
	void UpdateAttachmentsPos();
	void ReloadAttachmentsInfo();
	void UpdateAttachmentsVisibility();
	void TargetDetectedIndirectly( const Vec3& pos );
	void CheckAlertAIGroup( const SEntityUpdateContext& ctx );
	void NotifyGroupTargetSpotted();
	void DisableLasers();
	void UpdateLasers( const SEntityUpdateContext& ctx );
	void UpdateLasersTM();
	void ShowLaser( int weaponSpot, float duration );
	void StartBurst( float dispersionMin, float dispersionMax, bool predictTargetMovement );
	void UpdateBurst( const SEntityUpdateContext& ctx );
	Vec3 CalcFutureTargetPos( float time );
	void UpdateLaserTM( const TWeaponSpot& weaponSpot );
	void EnableRenderSlot( int slot );
	void DisableRenderSlot( int slot );
	Vec3 CalcErrorPosition( const Vec3& pos, float minErrorDist, float maxErrorDist );
	Vec3 CalcErrorPositionOppositeCircle( const Vec3& pos, float minErrorDist, float maxErrorDist, const Vec3& posOpposite );
	void ReadLuaProperties();
	void OutputFlowEvent( const char* pOutputName );
	IAIActor* GetClientAIActor();

	#ifdef DEBUG_INFO_TOWERSEARCHLIGHT
	void ShowDebugInfo( bool isLimitedByName );
	#endif
	#ifdef FULL_DEBUG_INFO_TOWERSEARCHLIGHT
	void ShowMovementDebugInfo( float posX, float posY );
	#endif

private:

	// attachments are extra graphic entities that are manually moved and hide/unhide from here. IE beam light graphic, fog volume.
	struct TAttachment
	{
		TAttachment()
		: entityId( 0 )
		{}

		EntityId	entityId;
		float			distFromTarget;
		Quat			rot;
	};

  //..............................................................................................
	// encapsulates weapon management from the main class
	class CWeaponManager : public IWeaponEventListener
	{
	public:
		CWeaponManager();
		~CWeaponManager() {}

		void SetOwnerId( EntityId towerId );
		void SetWeaponClass( const char* pWeaponClassName );
		void SetSoundShot( const char* pSoundShot );
		void Shoot( const Vec3& weaponPos, const Vec3& targetPos );
		void OnEnable();
		float EstimateProjectileFlyTime( const Vec3& weaponPos, const Vec3& targetPos );

		// IWeaponEventListener
		virtual void OnShoot(IWeapon *pWeapon, EntityId shooterId, EntityId ammoId, IEntityClass* pAmmoType, const Vec3 &pos, const Vec3 &dir, const Vec3 &vel);
		virtual void OnStartFire(IWeapon *pWeapon, EntityId shooterId){}
		virtual void OnStopFire(IWeapon *pWeapon, EntityId shooterId){}
		virtual void OnFireModeChanged(IWeapon *pWeapon, int currentFireMode) {}
		virtual void OnStartReload(IWeapon *pWeapon, EntityId shooterId, IEntityClass* pAmmoType){}
		virtual void OnEndReload(IWeapon *pWeapon, EntityId shooterId, IEntityClass* pAmmoType){}
		virtual void OnSetAmmoCount(IWeapon *pWeapon, EntityId shooterId) {}
		virtual void OnOutOfAmmo(IWeapon *pWeapon, IEntityClass* pAmmoType){}
		virtual void OnReadyToFire(IWeapon *pWeapon){}   
		virtual void OnPickedUp(IWeapon *pWeapon, EntityId actorId, bool destroyed){}
		virtual void OnDropped(IWeapon *pWeapon, EntityId actorId){}
		virtual void OnMelee(IWeapon* pWeapon, EntityId shooterId){}
		virtual void OnStartTargetting(IWeapon *pWeapon){}
		virtual void OnStopTargetting(IWeapon *pWeapon){}
		virtual void OnSelected(IWeapon *pWeapon, bool select) {}
		virtual void OnEndBurst(IWeapon *pWeapon, EntityId shooterId) {}
		// ~IWeaponEventListener

	private:
		void InitWeapon( const char* pWeaponClassName );
		IWeapon* GetWeapon();

	private:
		EntityId						m_towerId;
		EntityId						m_weaponId;
		CAudioSignalPlayer	m_soundShoot; // played at target position
	};
	//..............................................................................................



private:

	bool								m_enabled;
	bool								m_enemyHasEverBeenInView;
	bool								m_sleeping; // when sleeping, the tower will move the lights and stuff along the predefined path, but will not react to ani stimulus like sound, AI notification, or player
	// m_targetIdInViewCone is the value directly returned by the vision system.
	// m_targetIdInView is the value the tower actually uses to react to. Its value comes from m_targetIdInViewCone, but not directly. 
	//                  Some circunstances like stealth, visionPersistence, etc, can cause it to be different
	EntityId						m_targetIdInViewCone;
	EntityId						m_targetIdInView;
	Vec3								m_lastTargetSeenPos;
	EntityId						m_entityIdleMovement;   // the position of this entity defines where the searchlight is looking at when in idle mode
	VisionID						m_visionId;
	float								m_visionFOVCos;
	CWeaponManager			m_weapon;
	EState							m_state;
	float								m_timeToCheckAlertAIGroup;
	float								m_timeToUnlockDetectionSoundSequence;  // after detection sequence has been played, there is a cool down time to not play it too often
	CAudioSignalPlayer	m_soundBackground;  // constantly played at target point
	CAudioSignalPlayer	m_soundPreshoot;
	float								m_timeToMove;
	float								m_timeToNextBurst;
	Vec3								m_searchLightPos;   // where the spot light is
	uint32							m_burstsDoneInThisState;
	float								m_timeSinceLostTarget;
	TAttachment					m_attachments[ MAX_NUM_ATTACHMENTS ];
	Vec3								m_laserScale;
	int									m_numWeaponsSpotDisabled;
	int									m_AlertAIGroupID;
	float								m_visionPersistenceTimeLeft;
	Vec3								m_lastAIGroupAlertPos;

	struct TAudioDetection
	{
		CAudioSignalPlayer	signal;
		float								delay;
		float								lastTimePlayed;
		bool								played;
	}										m_soundsDetection[ MAX_NUM_SOUNDS_DETECTION ];
	float								m_audioDetectionTimeCounter;

	struct
	{
		Vec3							posStart;
		Vec3							posDeltaEnd;   // posEnd - posStart
		float							timeStart;
		float							duration;
		float							smoothCD_val;        // both smoothCD values are used for the non-linear movement calcs.
		float							smoothCD_valRate;
		bool							isMoving;
		bool							isLinear;
	}										m_lightMovement;
	
	struct TWeaponSpot
	{
		int 							laserSlot;
		float							laserDurationLeft;
		Vec3							laserTargetPos;
		Vec3							weaponPos;
		bool							isLaserActive;
		bool							isWeaponEnabled;

		void Reset() { laserSlot = -1; isLaserActive = false; isWeaponEnabled = false; }
	}										m_weaponSpot[MAX_NUM_WEAPON_SPOTS];

	struct
	{
		float							timeForNextShoot;
		float							timeForNextPreshoot;
		uint32						shootsDone;
		uint32						preshootsDone;
		float							dispersionErrorStep; // error distance added to each shoot. Lineally interpolated from Max to Min between first and last shoot.
		float							currDispersionError;
		bool							isDoingBurst;
		bool							predictTargetMovement;
	}										m_burst;

	// note: most lua parameters are commented in the lua file.
	struct  
	{
		struct
		{
			float timeToFirstBurst;
			float timeToFirstBurstIfStealth;
			uint32 numWarningBursts;
			float timeBetweenWarningBursts;
			float errorAddedToWarningBursts;
			float timeBetweenBursts;
			float followDelay;
			float trackSpeed;
			float detectionSoundSequenceCoolDownTime;
		}				ST_ENEMY_IN_VIEW;

		struct  
		{
			float maxDistSearch;
			float timeSearching;
			float searchSpeed;
			float timeToFirstBurst;
			float timeBetweenBursts;
			float minErrorShoot;
			float maxErrorShoot;
		}				ST_ENEMY_LOST;

		bool		enabledFromStart;
		bool		canDetectStealth;
		bool		alwaysSeePlayer;
		float		visionFOV;
		float		visionRange;
		float		hearingRange;
		int			alertAIGroupId;
		float		burstDispersion; 
		float		timeBetweenShootsInABurst;
		float		offsetDistPrediction;
		float		preshootTime;
		uint32	numWeaponsSpots;
		float		visionPersistenceTime;
		float		minPlayerSpeedForOffsetDistPrediction;
		float		maxDistancePrediction;
	}										m_luaParams;

#ifdef DEBUG_INFO_TOWERSEARCHLIGHT
		static ICVar*			m_pcvarDebugTowerName;
		static ICVar*			m_pcvarDebugTower;
#endif
};

#endif