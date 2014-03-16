////////////////////////////////////////////////////////////////////////////
//
//  Crytek Engine Source File.
//  Copyright (C), Crytek Studios, 2001-2012.
// -------------------------------------------------------------------------
//  File name:   UILobbyMP.cpp
//  Version:     v1.00
//  Created:     08/06/2012 by Michiel Meesters.
//  Description: 
// -------------------------------------------------------------------------
//  History:
//
////////////////////////////////////////////////////////////////////////////
#include "StdAfx.h"
#include "UILobbyMP.h"

#include <IGameFramework.h>
#include "Game.h"
#include "Actor.h"
#include "GameRules.h"
#include "Network/Lobby/GameLobby.h"
#include "Network/Lobby/GameBrowser.h"
#include "Network/Lobby/GameLobbyManager.h"
#include "IPlatformOS.h"
#include "ICryLobby.h"
#include "ICryReward.h"
#include "ICryStats.h"
#include "ICryFriends.h"
#include "Network/Squad/SquadManager.h"

static CUILobbyMP *pUILObbyMP = NULL;

////////////////////////////////////////////////////////////////////////////
CUILobbyMP::CUILobbyMP()
	: m_pUIEvents(NULL)
	, m_pUIFunctions(NULL)
{
	pUILObbyMP = this;
}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::InitEventSystem()
{
	if (!gEnv->pFlashUI)
		return;

	ICVar* pServerVar = gEnv->pConsole->GetCVar("cl_serveraddr");

	// events to send from this class to UI flowgraphs
	m_pUIFunctions = gEnv->pFlashUI->CreateEventSystem("LobbyMP", IUIEventSystem::eEST_SYSTEM_TO_UI);
	m_eventSender.Init(m_pUIFunctions);

	{
		SUIEventDesc evtDesc("ServerFound", "Triggered when server is found");
		evtDesc.AddParam<SUIParameterDesc::eUIPT_Int>("ServerId", "ID of the server");
		evtDesc.AddParam<SUIParameterDesc::eUIPT_String>("ServerName", "Name of the server");
		m_eventSender.RegisterEvent<eUIE_ServerFound>(evtDesc);
	}

	{
		SUIEventDesc evtDesc("PlayerList", "Triggered when GetPlayerList is fired");
		evtDesc.SetDynamic("PlayerList", "List of players");
		m_eventSender.RegisterEvent<eUIE_PlayerListReturn>(evtDesc);
	}

	{
		SUIEventDesc evtDesc("PlayerIDList", "Triggered when GetPlayerList is fired");
		evtDesc.SetDynamic("PlayerIDs", "List of player ids");
		m_eventSender.RegisterEvent<eUIE_PlayerIdListReturn>(evtDesc);
	}

	{
		SUIEventDesc evtDesc("InviteAccepted", "An invite to a game was accepted");
		m_eventSender.RegisterEvent<eUIE_InviteAccepted>(evtDesc);
	}

	{
		SUIEventDesc evtDesc("SearchStarted", "A lobby search query was started");
		m_eventSender.RegisterEvent<eUIE_SearchStarted>(evtDesc);
	}

	{
		SUIEventDesc evtDesc("SearchCompleted", "A lobby search query was completed");
		m_eventSender.RegisterEvent<eUIE_SearchCompleted>(evtDesc);
	}

	{
		SUIEventDesc evtDesc("NatTypeUpdated", "Triggered when new NAT info is available (psn/xlive)");
		m_eventSender.RegisterEvent<eUIE_NatTypeUpdated>(evtDesc);
	}

	{
		SUIEventDesc evtDesc("ShowLoadingDialog", "Requests to show loading dialog - when something takes longer then usual");
		evtDesc.AddParam<SUIParameterDesc::eUIPT_String>("LoadingDialog", "tag specifies which loading dialog");
		m_eventSender.RegisterEvent<eUIE_ShowLoadingDialog>(evtDesc);
	}

	{
		SUIEventDesc evtDesc("HideLoadingDialog", "Requests to hide a loading dialog - when something took longer then usual");
		evtDesc.AddParam<SUIParameterDesc::eUIPT_String>("LoadingDialog", "tag specifies which loading dialog");
		m_eventSender.RegisterEvent<eUIE_HideLoadingDialog>(evtDesc);
	}

	// events that can be sent from UI flowgraphs to this class
	m_pUIEvents = gEnv->pFlashUI->CreateEventSystem("LobbyMP", IUIEventSystem::eEST_UI_TO_SYSTEM);
	m_eventDispatcher.Init(m_pUIEvents, this, "CUILobbyMP");

	{
		SUIEventDesc evtDesc("HostGame", "Host a game, providing a map-path");
		evtDesc.AddParam<SUIParameterDesc::eUIPT_Bool>("LAN", "1: Lan, 0: Internet");
		evtDesc.AddParam<SUIParameterDesc::eUIPT_String>("MapPath", "path to desired map");
		evtDesc.AddParam<SUIParameterDesc::eUIPT_String>("Gamerules", "desired Gamerules to be used");
		m_eventDispatcher.RegisterEvent(evtDesc, &CUILobbyMP::HostGame);
	}


	{
		SUIEventDesc evtDesc("JoinGame", "Join a game");
		evtDesc.AddParam<SUIParameterDesc::eUIPT_Int>("serverID", "provided from search servers");
		m_eventDispatcher.RegisterEvent(evtDesc, &CUILobbyMP::JoinGame);
	}

	{
		SUIEventDesc evtDesc("LeaveGame", "Leave game session");
		m_eventDispatcher.RegisterEvent(evtDesc, &CUILobbyMP::LeaveGame);
	}

	{
		SUIEventDesc evtDesc("SetMultiplayer", "Sets Multiplayer");
		evtDesc.AddParam<SUIParameterDesc::eUIPT_Bool>("isMultiplayer", "true: multiplayer - false: singleplayer");
		m_eventDispatcher.RegisterEvent(evtDesc, &CUILobbyMP::SetMultiplayer);
	}

	{
		SUIEventDesc evtDesc("LockController", "Locks or Unlocks the exclusive controller index");
		evtDesc.AddParam<SUIParameterDesc::eUIPT_Bool>("LockController", "true: lock - false: unlock");
		m_eventDispatcher.RegisterEvent(evtDesc, &CUILobbyMP::LockController);
	}

	{
		SUIEventDesc evtDesc("SearchGames", "Start quick-game with default settings");
		evtDesc.AddParam<SUIParameterDesc::eUIPT_Bool>("LAN", "1: LAN or 0: Internet");
		m_eventDispatcher.RegisterEvent(evtDesc, &CUILobbyMP::SearchGames);
	}

	{
		SUIEventDesc evtDesc("AwardTrophy", "Test award trophy");
		evtDesc.AddParam<SUIParameterDesc::eUIPT_Int>("TrophyNr", "Index of trophy");
		m_eventDispatcher.RegisterEvent(evtDesc, &CUILobbyMP::AwardTrophy);
	}

	{
		SUIEventDesc evtDesc("GetPlayerlist", "Get list of all players in lobby");
		m_eventDispatcher.RegisterEvent(evtDesc, &CUILobbyMP::GetPlayerList);
	}


	{
		SUIEventDesc evtDesc("ReadLeaderboard", "Test reading leaderboard");
		m_eventDispatcher.RegisterEvent(evtDesc, &CUILobbyMP::ReadLeaderBoard);
	}

	{
		SUIEventDesc evtDesc("RegisterLeaderboard", "Test registering leaderboard");
		m_eventDispatcher.RegisterEvent(evtDesc, &CUILobbyMP::RegisterLeaderBoard);
	}

	{
		SUIEventDesc evtDesc("WriteLeaderboard", "Test writing to leaderboard");
		m_eventDispatcher.RegisterEvent(evtDesc, &CUILobbyMP::WriteLeaderBoard);
	}

	gEnv->pFlashUI->RegisterModule(this, "CUILobbyMP");
}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::UnloadEventSystem()
{
	if (gEnv->pFlashUI)
		gEnv->pFlashUI->UnregisterModule(this);
}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::Reset()
{
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
// ui functions


////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::ServerFound(SCrySessionSearchResult session, string sServerName)
{
	if (gEnv->pFlashUI)
	{
		m_eventSender.SendEvent<eUIE_ServerFound>((int)m_FoundServers.size(), sServerName);
		m_FoundServers.push_back(session);
	}
}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::PlayerListReturn(const SUIArguments& players, const SUIArguments& playerids)
{
	if (gEnv->pFlashUI)
	{
		m_eventSender.SendEvent<eUIE_PlayerListReturn>(players);
		m_eventSender.SendEvent<eUIE_PlayerIdListReturn>(playerids);
	}
}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::InviteAccepted()
{
	if (gEnv->pFlashUI)
		m_eventSender.SendEvent<eUIE_InviteAccepted>();
}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::SearchStarted()
{
	if (gEnv->pFlashUI)
		m_eventSender.SendEvent<eUIE_SearchStarted>();
}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::SearchCompleted()
{
	if (gEnv->pFlashUI)
		m_eventSender.SendEvent<eUIE_SearchCompleted>();
}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::ShowLoadingDialog(const  char* sLoadingDialog)
{
	if (gEnv->pFlashUI)
		m_eventSender.SendEvent<eUIE_ShowLoadingDialog>(sLoadingDialog);
};

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::HideLoadingDialog(const  char* sLoadingDialog)
{
	if (gEnv->pFlashUI)
		m_eventSender.SendEvent<eUIE_HideLoadingDialog>(sLoadingDialog);
};

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::UpdateNatType()
{
	CGameBrowser *pGameBrowser = g_pGame->GetGameBrowser();
	if(pGameBrowser && 	gEnv->pFlashUI)
	{
		string natString = pGameBrowser->GetNatTypeString();
		m_eventSender.SendEvent<eUIE_NatTypeUpdated>(natString);
	}
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
void CUILobbyMP::JoinGame(unsigned int sessionID)
{
	bool result = false;

	CGameLobby *pGameLobby = g_pGame->GetGameLobby();
	CGameLobbyManager *pGameLobbyMgr = g_pGame->GetGameLobbyManager();
	CSquadManager *pSquadMgr = g_pGame->GetSquadManager();

	if(pGameLobbyMgr)
	{
		pGameLobbyMgr->SetMultiplayer(true);
	}

	if(pSquadMgr)
	{
		pSquadMgr->SetMultiplayer(true);
	}

	if(pGameLobby && m_FoundServers.size() > sessionID)
		result = pGameLobby->JoinServer(m_FoundServers[sessionID].m_id, m_FoundServers[sessionID].m_data.m_name, CryMatchMakingInvalidConnectionUID, false);

	return;
}

//////////////////////////////////////////////////////////////////////////
void CUILobbyMP::HostGame(bool bLan, string sMapPath, string sGameRules)
{
	if (CGameLobbyManager* pGameLobbyManager = g_pGame->GetGameLobbyManager())
	{
		pGameLobbyManager->SetMultiplayer(true);
	}

	if (CSquadManager* pSquadManager = g_pGame->GetSquadManager())
	{
		pSquadManager->SetMultiplayer(true);
	}

	if (CGameLobby* pGameLobby = g_pGame->GetGameLobby())
	{
		CGameLobby::SetLobbyService(bLan ? eCLS_LAN : eCLS_Online);
				
		if(!g_pGame->GetIGameFramework()->GetIGameRulesSystem()->HaveGameRules(sGameRules))
		{
			 sGameRules = "DeathMatch";
		}

		pGameLobby->CreateSessionFromSettings(sGameRules, sMapPath);
	}
}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::SearchGames(bool bLan)
{
	//Clear results
	m_FoundServers.clear();
	
	CGameBrowser *pGameBrowser = g_pGame->GetGameBrowser();
	if (pGameBrowser)
	{
		if(bLan)
			CGameLobby::SetLobbyService(eCLS_LAN);
		else
			CGameLobby::SetLobbyService(eCLS_Online);

		pGameBrowser->StartSearchingForServers(CUILobbyMP::MatchmakingSessionSearchCallback);
	}

}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::GetPlayerList()
{
	CGameLobby* pGameLobby = g_pGame->GetGameLobby();
	SSessionNames sessions = pGameLobby->GetSessionNames();
	SUIArguments players, ids;
	for (uint i = 0; i < sessions.Size(); i++)
	{
		SSessionNames::SSessionName &player = sessions.m_sessionNames[i];
		players.AddArgument( string(player.m_name) );
		ids.AddArgument(player.m_conId.m_uid);
	}
	
	PlayerListReturn(players, ids);
}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::LeaveGame()
{
	CGameLobby* pGameLobby = g_pGame->GetGameLobby();
	if (pGameLobby)
	{
    //TODO: FIX THIS




	}
}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::MatchmakingSessionSearchCallback(CryLobbyTaskID taskID, ECryLobbyError error, SCrySessionSearchResult* session, void* arg)
{
	int breakhere = 0;
	if (error == eCLE_SuccessContinue || error == eCLE_Success)
	{
		if(session)
		{
			CGameBrowser *pGameBrowser = g_pGame->GetGameBrowser();
			CGameLobby *pGameLobby = g_pGame->GetGameLobby();
			pUILObbyMP->ServerFound(*session, session->m_data.m_name);
		}
	}
}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::AwardTrophy(int trophy)
{
	ICryLobby* pLobby = gEnv->pNetwork->GetLobby();
	unsigned int user =  gEnv->pGame->GetIGameFramework()->GetIPlayerProfileManager()->GetExclusiveControllerDeviceIndex();
	if(user != IPlatformOS::Unknown_User)
	{
		uint32 achievementId = trophy;

		//-- Award() only puts awards into a queue to be awarded.
		//-- This code here only asserts that the award was added to the queue successfully, not if the award was successfully unlocked.
		//-- Function has been modified for a CryLobbyTaskId, callback and callback args parameters, similar to most other lobby functions,
		//-- to allow for callback to trigger when the award is successfully unlocked (eCLE_Success) or failed to unlock (eCLE_InternalError).
		//-- In the case of trying to unlock an award that has already been unlocked, the callback will return eCLE_Success.
		//-- Successful return value of the Award function has been changed from incorrect eCRE_Awarded to more sensible eCRE_Queued.
	
		//achievements are only for online lobbies
		if(pLobby && pLobby->GetLobbyService(eCLS_Online) && pLobby->GetLobbyService(eCLS_Online)->GetReward())
		{
			ECryRewardError error = pLobby->GetLobbyService(eCLS_Online)->GetReward()->Award(user, achievementId, NULL, NULL, NULL);
		}
	}
}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::RegisterLeaderBoard()
{
//TODO: FIX THIS























}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::GetFriends()
	{
//TODO: FIX THIS









}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::GetFriendsCB(CryLobbyTaskID taskID, ECryLobbyError error, SFriendInfo* pFriendInfo, uint32 numFriends, void* pArg)
{
//TODO: FIX THIS
















}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::InviteFriends(CryLobbyTaskID taskID, ECryLobbyError error, void* pArg)
{
	int breakhere = 0;
}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::RegisterLeaderboardCB(CryLobbyTaskID TaskID, ECryLobbyError Error, void *Arg)
{
	if (Error > eCLE_SuccessContinue)
		GameWarning("CUILobbyMP::RegisterLeaderboardCB: Failed to register, code: %i", Error);
}


////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::WriteLeaderBoard()
{
//TODO: FIX THIS






























}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::WriteLeaderboardCallback(CryLobbyTaskID TaskID, ECryLobbyError Error, void *Arg)
{
	if (Error > eCLE_SuccessContinue)
		GameWarning("CQuerySystem::WriteLeaderboardCallback: Failed to write, code: %i", Error);
	//test

//TODO: FIX THIS








}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::ReadLeaderBoard()
{
//TODO: FIX THIS


























}

//////////////  CALLBACK FOR LEADERBOARDS //////////////////////////////////
void CUILobbyMP::ReadLeaderBoardCB(CryLobbyTaskID TaskID, ECryLobbyError Error, SCryStatsLeaderBoardReadResult *Result, void *Arg)
{
	// DatabaseIDs are 1-based, so offset the leaderboard type
	int breakhere = 0;
	breakhere++;
	breakhere = breakhere;
	if(Error == eCLE_Success)
		CryLogAlways("READ LEADERBOARD: SUCCES");
	//const int TypeIndex = (int(Arg)-LBType_FIRST);
	//CCardLeaderboard *LeaderboardCard = (CCardLeaderboard*)g_pGame->GetCardManager()->FindCardByDatabaseID(CARDTYPE_Leaderboard, TypeIndex+1).Get();
	//LeaderboardCard->m_numQueriesPending--;

	//if (Error > eCLE_SuccessContinue)
	//{
	//	GameWarning("CCardLeaderboard::ReadMediumLeaderboardCallback: Failed to read leaderboard %s, code: %i", GLeaderboardNames[TypeIndex], Error);
	//	return;
	//}

	//if (!Result)
	//{
	//	GameWarning("CCardLeaderboard::ReadMediumLeaderboardCallback: Received a NULL result while attempting to read leaderboard %s!", GLeaderboardNames[TypeIndex]);
	//	return;
	//}

	//CryUserID LocalUser = g_pGame->GetExclusiveControllerUserID();
	//if (!LocalUser.IsValid())
	//{
	//	GameWarning("CCardLeaderboard::ReadMediumLeaderboardCallback: Got a local userID that is not valid");
	//	return;
	//}

	//const EQueryParamType ParamType = ParamTypeForLeaderboard(LeaderboardCard->GetLeaderboardType());
	//for (int RowIndex = 0 ; RowIndex < Result->numRows ; RowIndex++)
	//{
	//	const SCryStatsLeaderBoardReadRow ReadRow = Result->pRows[RowIndex];

	//	// Skip unranked players
	//	if (ReadRow.rank == 0)
	//		continue;

	//	CCardUser *PlayerCard = (CCardUser*)g_pGame->GetCardManager()->CreatePlayerCard(ReadRow.userID.get()->GetGUIDAsUint64(), (uint16)-1, false).Get();

	//	// Share the name since we've already fetched it anyways
	//	PlayerCard->SetPlayerName(ReadRow.name);

	//	const bool bIsFirstPlace = (ReadRow.rank == 1);
	//	if (bIsFirstPlace)
	//		LeaderboardCard->SetCardField("first_flashid", QPT_ID_UserCard, ToString(PlayerCard->DatabaseID));

	//	const bool bIsLocalPlayer = (LocalUser == ReadRow.userID);
	//	if (bIsLocalPlayer)
	//	{
	//		LeaderboardCard->SetCardField("player_flashid", QPT_ID_UserCard, ToString(PlayerCard->DatabaseID));
	//		LeaderboardCard->SetCardField("player_rank", QPT_Integer, ToString(ReadRow.rank - 1));
	//	}

	//	const SCryStatsLeaderBoardData RowData = ReadRow.data;
	//	for (int ColumnIndex = 0 ; ColumnIndex < RowData.numColumns ; ColumnIndex++)
	//	{
	//		const stack_string NewValue(ToString(RowData.score.score));
	//		if (bIsFirstPlace)
	//			LeaderboardCard->SetCardField("first_value", ParamType, NewValue.c_str());
	//		if (bIsLocalPlayer)
	//			LeaderboardCard->SetCardField("player_value", ParamType, NewValue.c_str());
	//	}
	//}
}

////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::SetMultiplayer( bool bIsMultiplayer )
{
	CGameLobbyManager *pGameLobbyMgr = g_pGame->GetGameLobbyManager();
	CSquadManager *pSquadMgr = g_pGame->GetSquadManager();

	if(pGameLobbyMgr)
	{
		pGameLobbyMgr->SetMultiplayer(bIsMultiplayer);
	}

	if(pSquadMgr)
	{
		pSquadMgr->SetMultiplayer(bIsMultiplayer);
	}

	gEnv->bMultiplayer = bIsMultiplayer;
	g_pGameCVars->g_multiplayerDefault = bIsMultiplayer;
	g_pGame->InitGameType(bIsMultiplayer);
}


////////////////////////////////////////////////////////////////////////////
void CUILobbyMP::LockController( bool bLock )
{
	if(bLock)
		g_pGame->SetExclusiveControllerFromPreviousInput();
	else
		g_pGame->RemoveExclusiveController();
}

////////////////////////////////////////////////////////////////////////////
REGISTER_UI_EVENTSYSTEM( CUILobbyMP );
