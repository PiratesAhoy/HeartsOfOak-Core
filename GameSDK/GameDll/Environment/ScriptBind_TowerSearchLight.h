/*************************************************************************
Crytek Source File.
Copyright (C), Crytek Studios, 2001-2012.
-------------------------------------------------------------------------
*************************************************************************/

#pragma once

#ifndef _SCRIPTBIND_TOWERSEARCHLIGHT_H_
#define _SCRIPTBIND_TOWERSEARCHLIGHT_H_

class CTowerSearchLight;

class CScriptBind_TowerSearchLight : public CScriptableBase
{
private:

	typedef std::map<EntityId, CTowerSearchLight*> TTowerSearchLightsMap;

public:
	CScriptBind_TowerSearchLight(ISystem *pSystem);
	virtual ~CScriptBind_TowerSearchLight();

	virtual void GetMemoryUsage(ICrySizer *pSizer) const;

	void AttachTo(CTowerSearchLight *pTowerSearchLight);
	void Detach(EntityId entityId);

	int Enable( IFunctionHandler *pH );
	int Disable( IFunctionHandler *pH );
	int Sleep( IFunctionHandler *pH );
	int Wakeup( IFunctionHandler *pH );
	int SetEntityIdleMovement( IFunctionHandler *pH, ScriptHandle targetEntityId );
	int SetAlertAIGroupID( IFunctionHandler *pH, int AIGroupID );
	int OnPropertyChange( IFunctionHandler *pH );
	int DisableWeaponSpot( IFunctionHandler *pH, int nSpot );

private:
	void RegisterMethods();

	CTowerSearchLight *GetTowerSearchLight(IFunctionHandler *pH);

	ISystem					*m_pSystem;
	IGameFramework	*m_pGameFrameWork;

	TTowerSearchLightsMap m_towerSearchLightsMap;
};


#endif