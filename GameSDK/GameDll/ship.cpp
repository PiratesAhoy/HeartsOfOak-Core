#include "StdAfx.h"
#include "ship.h"

CShip::CShip()
{
}

CShip::~CShip() 
{
}


bool CShip::Init(IGameObject *pGameObject)
{
	CryLogAlways("Init");
	return true;
}

void CShip::Update(SEntityUpdateContext &ctx, int updateSlot)
{


}

void CShip::SetAuthority(bool auth)
{

}

bool CShip::Reset()
{
	IScriptTable*  pTable = GetEntity()->GetScriptTable();
	if (pTable != NULL)
	{
		SmartScriptTable propertiesTable;
		if (pTable->GetValue("Properties", propertiesTable))
		{
			//propertiesTable->GetValue("bCurrentlyDealingDamage", m_dangerous);
			//propertiesTable->GetValue("bDoesFriendlyFireDamage", m_friendlyFireEnabled);
			//propertiesTable->GetValue("fDamageToDeal", m_damageDealt);
			//propertiesTable->GetValue("fTimeBetweenHits", m_timeBetweenHits);
		}
	}
	CryLogAlways("Reset");
	return true;
}

void CShip::ProcessEvent(SEntityEvent &event)
{
	
	switch(event.event)
	{
	case ENTITY_EVENT_COLLISION:
		{
			CryLogAlways("OnCollision");
			break;
		}
	case ENTITY_EVENT_ONHIT:
		{
			CryLogAlways("OnHit");	
		}
		break;
	case ENTITY_EVENT_RESET:
		{
		    Reset();
		}
		break;
	}

}

void CShip::HandleEvent(const SGameObjectEvent &event)
{
		
}

void CShip::Release()
{
	delete this;
}

void CShip::PostInit(IGameObject *pGameObject)
{
	pGameObject->EnableUpdateSlot(this, 0);
	pGameObject->EnablePostUpdates(this);
}

void CShip::FullSerialize(TSerialize ser)
{
}

bool CShip::ReloadExtension( IGameObject * pGameObject, const SEntitySpawnParams &params )
{
	return true;
}

bool CShip::GetEntityPoolSignature( TSerialize signature ) 
{
	return true;
}

void CShip::OnHit(const HitInfo* hitInfo)
{
	CryLogAlways("Hit");
}