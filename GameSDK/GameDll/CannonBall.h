/*************************************************************************
Crytek Source File.
Copyright (C), Crytek Studios, 2001-2004.
-------------------------------------------------------------------------
$Id$
$DateTime$
Description: CannonBall

-------------------------------------------------------------------------
History:
- 12:10:2005   11:15 : Created by M�rcio Martins

*************************************************************************/
#pragma once

#ifndef _CannonBall_H__
#define _CannonBall_H__

#include "Projectile.h"

struct ISkeletonPose;

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//Debug CannonBall penetration

#if !defined(XENON) && !defined(PS3) && !defined(_RELEASE)
	#define DEBUG_CannonBall_PENETRATION
#endif

#if !defined(XENON) && !defined(PS3)
	#define CannonBall_PENETRATION_BACKSIDE_FX_ENABLED_SP	1
#else
	#define CannonBall_PENETRATION_BACKSIDE_FX_ENABLED_SP	0
#endif

#ifdef DEBUG_CannonBall_PENETRATION

#define MAX_DEBUG_CannonBall_HITS 64
#define DEFAULT_DEBUG_CannonBall_HIT_LIFETIME 5.0f

struct SDebugCannonBallPenetration
{
private:

	struct SDebugCannonBallHit
	{
		SDebugCannonBallHit()
			: lifeTime(0.0f)
		{

		}

		Vec3 hitPosition;
		Vec3 CannonBallDirection;

		float damage;
		float lifeTime;

		int8 surfacePierceability;
		bool isBackFaceHit;
		bool stoppedCannonBall;
		bool tooThick;
	};

public:

	SDebugCannonBallPenetration()
		: m_nextHit(0)
	{
	}

	void AddCannonBallHit(const Vec3& hitPosition, const Vec3& hitDirection, float currentDamage, int8 surfacePierceability, bool isBackFace, bool stoppedCannonBall, bool tooThick);
	void Update(float frameTime);

private:

	const char* GetPenetrationLevelByPierceability(int8 surfacePierceability) const;

	SDebugCannonBallHit m_hitsList[MAX_DEBUG_CannonBall_HITS];

	uint32 m_nextHit;
};

#endif //DEBUG_CannonBall_PENETRATION
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

struct SPhysicsRayWrapper;

class CCannonBall : public CProjectile
{
private: 
	typedef CProjectile BaseClass;

	struct SBackHitInfo
	{
		Vec3 pt;	
	};

public:
	static void StaticInit();
	static void StaticShutdown();

public:
	CCannonBall();
	virtual ~CCannonBall();

	// CProjectile
	virtual void HandleEvent(const SGameObjectEvent &);
	virtual void SetParams(const SProjectileDesc& projectileDesc);
	virtual void ReInitFromPool();
	virtual bool IsAlive() const;

	virtual void SetDamageCap(float cap) { m_damageCap = cap; };
	virtual void UpdateLinkedDamage(EntityId hitActorId, float totalAccumDamage);
	// ~CProjectile

#ifdef DEBUG_CannonBall_PENETRATION
	static void UpdateCannonBallPenetrationDebug(float frameTime)
	{
		s_debugCannonBallPenetration.Update(frameTime);
	}
#endif

	static IEntityClass*	EntityClass;

protected:

	virtual void SetUpParticleParams(IEntity* pOwnerEntity, uint8 pierceabilityModifier);
	void ProcessHit(CGameRules& gameRules, const EventPhysCollision& collision, IEntity& target, float damage, int hitMatId, const Vec3& hitDir);
	bool CheckForPreviousHit(EntityId targetId, float& damage);
	bool ShouldSpawnBackSideEffect(IEntity* pHitTarget);
	void DestroyAtHitPosition(const Vec3& hitPosition);
	ILINE int16 GetCannonBallPierceability() const { return m_cannonBallPierceability; };

private:

	struct SActorHitInfo
	{
		SActorHitInfo(EntityId _actorId, float _accumDamage, bool _previousHit) :
			linkedAccumDamage(_accumDamage), 
			hitActorId(_actorId),
			previousHit(_previousHit) {};

		float			linkedAccumDamage;
		EntityId	hitActorId;
		bool			previousHit;
	};

	void EmitUnderwaterTracer(const Vec3& pos, const Vec3& destination);
	bool FilterFriendlyAIHit(IEntity* pHitTarget);
	float GetFinalDamage(const Vec3& hitPos) const;
	ILINE float GetDamageAfterPenetrationFallOff() const { return ((float)m_damage - m_accumulatedDamageFallOffAfterPenetration); };

	void HandlePierceableSurface(const EventPhysCollision* pCollision, IEntity* pHitTarget, const Vec3& hitDirection, bool bProcessedCollisionEvent);
	bool ShouldDestroyCannonBall() const;

	bool RayTraceGeometry(const EventPhysCollision* pCollision, const Vec3& pos, const Vec3& hitDirection, SBackHitInfo* pBackHitInfo);
	int GetRopeBoneId(const EventPhysCollision& collision, IEntity& target, IPhysicalEntity* pRopePhysicalEntity) const;

	static SPhysicsRayWrapper* s_pRayWrapper;

#ifdef DEBUG_CannonBall_PENETRATION
	//CannonBall penetration debug
	static SDebugCannonBallPenetration s_debugCannonBallPenetration;
#endif

	std::vector <SActorHitInfo> m_hitActors;

	float m_damageCap;
	
	float m_damageFallOffStart;
	float m_damageFallOffAmount;
	float m_damageFalloffMin;
	float m_pointBlankAmount;
	float m_pointBlankDistance;
	float m_pointBlankFalloffDistance;

	float m_accumulatedDamageFallOffAfterPenetration;
	int16	m_cannonBallPierceability;
	int16	m_penetrationCount;

	bool m_alive;
	bool m_ownerIsPlayer;
	bool m_backSideEffectsDisabled;
};


#endif // __CannonBall_H__