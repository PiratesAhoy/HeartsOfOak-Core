/*************************************************************************
Crytek Source File.
Copyright (C), Crytek Studios, 2001-2012.
-------------------------------------------------------------------------
*************************************************************************/

#include "StdAfx.h"
#include "ScriptBind_TowerSearchLight.h"
#include "TowerSearchLight.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CScriptBind_TowerSearchLight::CScriptBind_TowerSearchLight( ISystem *pSystem )
	: m_pSystem(pSystem)
{
	Init(pSystem->GetIScriptSystem(), m_pSystem, 1);
	RegisterMethods();
}

CScriptBind_TowerSearchLight::~CScriptBind_TowerSearchLight()
{
}


//////////////////////////////////////////////////////////////////////////
void CScriptBind_TowerSearchLight::RegisterMethods()
{
#undef SCRIPT_REG_CLASSNAME
#define SCRIPT_REG_CLASSNAME &CScriptBind_TowerSearchLight::

	SCRIPT_REG_TEMPLFUNC(Enable, "");
	SCRIPT_REG_TEMPLFUNC(Disable, "");
	SCRIPT_REG_TEMPLFUNC(Sleep, "");
	SCRIPT_REG_TEMPLFUNC(Wakeup, "");
	SCRIPT_REG_TEMPLFUNC(DisableWeaponSpot, "nSpot");
	SCRIPT_REG_TEMPLFUNC(SetEntityIdleMovement, "entityId");
	SCRIPT_REG_TEMPLFUNC(SetAlertAIGroupID, "nAiGroupID");
	SCRIPT_REG_TEMPLFUNC(OnPropertyChange, "");
}

//////////////////////////////////////////////////////////////////////////
void CScriptBind_TowerSearchLight::AttachTo( CTowerSearchLight* pTowerSearchLight )
{
	IScriptTable *pScriptTable = pTowerSearchLight->GetEntity()->GetScriptTable();

	if (pScriptTable)
	{
		SmartScriptTable thisTable(m_pSS);

		thisTable->SetValue("__this", ScriptHandle(pTowerSearchLight->GetEntityId()));
		thisTable->Delegate(GetMethodsTable());

		pScriptTable->SetValue("TowerSearchLight", thisTable);
	}

	m_towerSearchLightsMap.insert(TTowerSearchLightsMap::value_type(pTowerSearchLight->GetEntityId(), pTowerSearchLight));
}

//////////////////////////////////////////////////////////////////////////
void CScriptBind_TowerSearchLight::Detach( EntityId entityId )
{
	m_towerSearchLightsMap.erase(entityId);
}

//////////////////////////////////////////////////////////////////////////
CTowerSearchLight* CScriptBind_TowerSearchLight::GetTowerSearchLight( IFunctionHandler *pH )
{
	void* pThis = pH->GetThis();

	if (pThis)
	{
		const EntityId objectId = (EntityId)(UINT_PTR)pThis;
		TTowerSearchLightsMap::const_iterator cit = m_towerSearchLightsMap.find(objectId);
		if (cit != m_towerSearchLightsMap.end())
		{
			return cit->second;
		}
	}

	return NULL;
}

//////////////////////////////////////////////////////////////////////////
void CScriptBind_TowerSearchLight::GetMemoryUsage(ICrySizer *pSizer) const
{
	pSizer->AddObject(this, sizeof(*this));
	pSizer->AddContainer(m_towerSearchLightsMap);
}


//////////////////////////////////////////////////////////////////////////

int CScriptBind_TowerSearchLight::Enable( IFunctionHandler *pH )
{
	CTowerSearchLight *pTowerSearchLight = GetTowerSearchLight(pH);
	if (pTowerSearchLight)
		pTowerSearchLight->Enable();

	return pH->EndFunction();
}


//////////////////////////////////////////////////////////////////////////
int CScriptBind_TowerSearchLight::Disable( IFunctionHandler *pH )
{
	CTowerSearchLight *pTowerSearchLight = GetTowerSearchLight(pH);
	if (pTowerSearchLight)
		pTowerSearchLight->Disable();

	return pH->EndFunction();
}


//////////////////////////////////////////////////////////////////////////

int CScriptBind_TowerSearchLight::Sleep( IFunctionHandler *pH )
{
	CTowerSearchLight *pTowerSearchLight = GetTowerSearchLight(pH);
	if (pTowerSearchLight)
		pTowerSearchLight->Sleep();

	return pH->EndFunction();
}


//////////////////////////////////////////////////////////////////////////

int CScriptBind_TowerSearchLight::Wakeup( IFunctionHandler *pH )
{
	CTowerSearchLight *pTowerSearchLight = GetTowerSearchLight(pH);
	if (pTowerSearchLight)
		pTowerSearchLight->Wakeup();

	return pH->EndFunction();
}


//////////////////////////////////////////////////////////////////////////
int CScriptBind_TowerSearchLight::DisableWeaponSpot( IFunctionHandler *pH, int nSpot )
{
	CTowerSearchLight *pTowerSearchLight = GetTowerSearchLight(pH);
	if (pTowerSearchLight)
		pTowerSearchLight->DisableWeaponSpot( nSpot );

	return pH->EndFunction();
}


//////////////////////////////////////////////////////////////////////////
int CScriptBind_TowerSearchLight::SetEntityIdleMovement( IFunctionHandler *pH, ScriptHandle targetEntityId )
{
	CTowerSearchLight *pTowerSearchLight = GetTowerSearchLight(pH);
	if (pTowerSearchLight)
		pTowerSearchLight->SetEntityIdleMovement( (EntityId)targetEntityId.n );

	return pH->EndFunction();
}


//////////////////////////////////////////////////////////////////////////
int CScriptBind_TowerSearchLight::SetAlertAIGroupID( IFunctionHandler *pH, int AIGroupID )
{
	CTowerSearchLight *pTowerSearchLight = GetTowerSearchLight(pH);
	if (pTowerSearchLight)
		pTowerSearchLight->SetAlertAIGroupID( AIGroupID );

	return pH->EndFunction();
}



//////////////////////////////////////////////////////////////////////////
int CScriptBind_TowerSearchLight::OnPropertyChange( IFunctionHandler *pH )
{
	CTowerSearchLight *pTowerSearchLight = GetTowerSearchLight(pH);
	if (pTowerSearchLight)
		pTowerSearchLight->OnPropertyChange();

	return pH->EndFunction();
}
