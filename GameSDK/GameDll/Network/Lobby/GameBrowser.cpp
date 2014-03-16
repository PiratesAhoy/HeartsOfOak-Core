#include "StdAfx.h"

#include "ICrySignIn.h"
#include "GameBrowser.h"
#include "GameLobbyData.h"

#include "ICryLobby.h"
#include "ICryMatchMaking.h"
#include "IGameFramework.h"
#include "IPlayerProfiles.h"

#include "DLCManager.h"
#include "PersistantStats.h"
#include "PlaylistManager.h"
#include "Game.h"

#include "CryEndian.h"

#include "GameCVars.h"
#include "Utility/StringUtils.h"
#include "GameCodeCoverage/GameCodeCoverageTracker.h"
#include "Network/Squad/SquadManager.h"
#include "Network/Lobby/GameLobbyCVars.h"
#include "RichPresence.h"

#include <IPlayerProfiles.h>

#if defined(USE_SESSION_SEARCH_SIMULATOR)
#include "SessionSearchSimulator.h"
#endif
#include "UI/UILobbyMP.h"
#include "ISystem.h"
#include "UI/UIManager.h"

static char s_profileName[CRYLOBBY_USER_NAME_LENGTH] = "";

#define LIVE_TITLE_ID									(0x3CAEEC88)
#define LIVE_SERVICE_CONFIG_ID				L"e1039253-2550-49c7-b785-4934f078c685"

#define START_SEARCHING_FOR_SERVERS_NUM_DATA	3

#define MAX_NUM_PER_FRIEND_ID_SEARCH 15	// Number of favourite Id's to search for at once.
#define MIN_TIME_BETWEEN_SEARCHES 1.f		// Time in secs before performing another search query. See m_delayedSearchType. Excludes favouriteId searches.

//-------------------------------------------------------------------------
CGameBrowser::CGameBrowser( void )
{
#if defined(USE_SESSION_SEARCH_SIMULATOR)
	m_pSessionSearchSimulator = NULL;
#endif //defined(USE_SESSION_SEARCH_SIMULATOR)

	m_NatType = eNT_Unknown;

	m_searchingTask = CryLobbyInvalidTaskID;

	m_bFavouriteIdSearch = false;

	m_delayedSearchType = eDST_None;

	m_lastSearchTime = 0.f;

#if IMPLEMENT_PC_BLADES
	m_currentFavouriteIdSearchType = CGameServerLists::eGSL_Favourite;
	m_currentSearchFavouriteIdIndex = 0;
	m_numSearchFavouriteIds = 0;
	memset(m_searchFavouriteIds, INVALID_SESSION_FAVOURITE_ID, sizeof(m_searchFavouriteIds));
#endif

	Init(); // init and trigger a request to register data - needs to be done early.
}

//-------------------------------------------------------------------------
void CGameBrowser::Init( void )
{
	gEnv->pNetwork->GetLobby()->RegisterEventInterest(eCLSE_NatType, CGameBrowser::GetNatTypeCallback, this);

#if defined(USE_SESSION_SEARCH_SIMULATOR)
	m_pSessionSearchSimulator = new CSessionSearchSimulator();
#endif //defined(USE_SESSION_SEARCH_SIMULATOR)
}

//--------------------------------------------------------------------------
CGameBrowser::~CGameBrowser()
{
#if defined(USE_SESSION_SEARCH_SIMULATOR)
	SAFE_DELETE( m_pSessionSearchSimulator );
#endif //defined(USE_SESSION_SEARCH_SIMULATOR)
}

//-------------------------------------------------------------------------
void CGameBrowser::StartSearchingForServers(CryMatchmakingSessionSearchCallback cb)
{
	ICryLobby *lobby = gEnv->pNetwork->GetLobby();
	if (lobby != NULL && lobby->GetLobbyService())
	{
		CCCPOINT (GameLobby_StartSearchingForServers);

		if (CanStartSearch())
		{
			CryLog("[UI] Delayed Searching for sessions");
			m_delayedSearchType = eDST_Full;

			NOTIFY_UILOBBY_MP(SearchStarted());

			return;
		}

		SCrySessionSearchParam param;
		SCrySessionSearchData data[START_SEARCHING_FOR_SERVERS_NUM_DATA];

		param.m_type = REQUIRED_SESSIONS_QUERY;
		param.m_data = data;
#if defined(XENON) || defined(PS3)
		param.m_numFreeSlots = max(g_pGame->GetSquadManager()->GetSquadSize(), 1);
#else
		param.m_numFreeSlots = 0; 
#endif
		param.m_maxNumReturn = g_pGameCVars->g_maxGameBrowserResults;
		param.m_ranked = false;

		int curData = 0;

#if defined(XENON)
		data[curData].m_operator = eCSSO_Equal;
		data[curData].m_data.m_id = REQUIRED_SESSIONS_SEARCH_PARAM;
		data[curData].m_data.m_type = eCLUDT_Int32;
		data[curData].m_data.m_int32 = 0;
		++curData;
#endif

		CRY_ASSERT_MESSAGE( curData < START_SEARCHING_FOR_SERVERS_NUM_DATA, "Session search data buffer overrun" );
		data[curData].m_operator = eCSSO_Equal;
		data[curData].m_data.m_id = LID_MATCHDATA_VERSION;
		data[curData].m_data.m_type = eCLUDT_Int32;
		data[curData].m_data.m_int32 = GameLobbyData::GetVersion();
		++curData;


		// if you want to use this, make sure the search query in the SPA asks for this param as well
		if (!g_pGameCVars->g_ignoreDLCRequirements)
		{
			// Note: GetSquadCommonDLCs is actually a bit field, so it should really be doing a bitwise & to determine
			// if the client can join the server. However this is not supported so the less than equal operator
			// is used instead. This may return some false positives but never any false negatives, the false
			// positives will be filtered out when the results are retreived.
			CRY_ASSERT_MESSAGE( curData < START_SEARCHING_FOR_SERVERS_NUM_DATA, "Session search data buffer overrun" );
			data[curData].m_operator = eCSSO_LessThanEqual;
			data[curData].m_data.m_id = LID_MATCHDATA_REQUIRED_DLCS;
			data[curData].m_data.m_type = eCLUDT_Int32;
			data[curData].m_data.m_int32 = g_pGame->GetDLCManager()->GetSquadCommonDLCs();
			++curData;
		}

		param.m_numData = curData;

		CRY_ASSERT_MESSAGE(m_searchingTask==CryLobbyInvalidTaskID,"CGameBrowser Trying to search for sessions when you think you are already searching.");

		ECryLobbyError error = StartSearchingForServers(&param, cb, this, false);

		CRY_ASSERT_MESSAGE(error==eCLE_Success,"CGameBrowser searching for sessions failed.");

		if (error == eCLE_Success)
		{
			NOTIFY_UILOBBY_MP(SearchStarted());

			CryLogAlways("CCGameBrowser::StartSearchingForServers %d", m_searchingTask);
		}
		else
		{
			NOTIFY_UILOBBY_MP(SearchCompleted());

			m_searchingTask = CryLobbyInvalidTaskID;
		}
	}
	else
	{
		CRY_ASSERT_MESSAGE(0,"CGameBrowser Cannot search for servers : no lobby service available.");
	}
}

//-------------------------------------------------------------------------
ECryLobbyError CGameBrowser::StartSearchingForServers(SCrySessionSearchParam* param, CryMatchmakingSessionSearchCallback cb, void* cbArg, const bool bFavouriteIdSearch)
{
	m_bFavouriteIdSearch = bFavouriteIdSearch;
	m_delayedSearchType = eDST_None;
	m_lastSearchTime = gEnv->pTimer->GetCurrTime(); 

#if defined(USE_SESSION_SEARCH_SIMULATOR)	
	if( m_pSessionSearchSimulator && CGameLobbyCVars::Get()->gl_searchSimulatorEnabled )
	{
		const char* filepath = gEnv->pConsole->GetCVar( "gl_searchSimulatorFilepath" )->GetString();
		if( filepath != NULL && strcmpi( filepath, m_pSessionSearchSimulator->GetCurrentFilepath() ) != 0  )
		{
			m_pSessionSearchSimulator->OpenSessionListXML( filepath );
		}

		m_pSessionSearchSimulator->OutputSessionListBlock( m_searchingTask, cb, cbArg );
		return eCLE_Success;
	}
	else
#endif //defined(USE_SESSION_SEARCH_SIMULATOR)
	{
		ECryLobbyError error = eCLE_ServiceNotSupported;
		ICryLobby *lobby = gEnv->pNetwork->GetLobby();
		uint32 userIndex = g_pGame->GetExclusiveControllerDeviceIndex();
		if(lobby)
		{
			CryLobbyTaskID previousTask = m_searchingTask;
			error = lobby->GetLobbyService()->GetMatchMaking()->SessionSearch(userIndex, param, &m_searchingTask, cb, cbArg);
			CryLog("CGameBrowser::StartSearchingForServers previousTask=%u, newTask=%u", previousTask, m_searchingTask);
		}
		return error;
	}
}

//-------------------------------------------------------------------------
void CGameBrowser::CancelSearching(bool feedback /*= true*/)
{
	CryLog("CGameBrowser::CancelSearching");

	if (m_searchingTask != CryLobbyInvalidTaskID)
	{
		CryLog("  canceling search task %u", m_searchingTask);
		ICryLobby *pLobby = gEnv->pNetwork->GetLobby();
		pLobby->GetMatchMaking()->CancelTask(m_searchingTask);
		// Calling FinishedSearch will clear m_searchingTask
	}

	m_delayedSearchType = eDST_None;

	FinishedSearch(feedback, true);
}

//-------------------------------------------------------------------------
void CGameBrowser::FinishedSearch(bool feedback, bool finishedSearch)
{
	CryLog("CGameBrowser::FinishedSearch()");
	m_searchingTask = CryLobbyInvalidTaskID;

#if IMPLEMENT_PC_BLADES
	if (m_bFavouriteIdSearch)
	{
		bool bSearchOver = true;

		if (!finishedSearch && (m_currentSearchFavouriteIdIndex < m_numSearchFavouriteIds))
		{
			if (DoFavouriteIdSearch())
			{
				feedback = false;
				bSearchOver = false;
			}
		}

		if (bSearchOver)	
		{
			for (uint32 i = 0; i < m_numSearchFavouriteIds; ++i)
			{
				if (m_searchFavouriteIds[i] != INVALID_SESSION_FAVOURITE_ID)
				{
					g_pGame->GetGameServerLists()->ServerNotFound(m_currentFavouriteIdSearchType, m_searchFavouriteIds[i]);
				}

				m_searchFavouriteIds[i] = INVALID_SESSION_FAVOURITE_ID;
			}
		}
	}
#endif
		
	if(feedback)
	{
		NOTIFY_UILOBBY_MP(SearchCompleted());
	}
}

bool CGameBrowser::CanStartSearch()
{
	const float currTime = gEnv->pTimer->GetCurrTime();
	return ((m_lastSearchTime + MIN_TIME_BETWEEN_SEARCHES) >= currTime);
}

//---------------------------------------
void CGameBrowser::Update(const float dt)
{
	if (m_delayedSearchType != eDST_None)
	{
		if (m_searchingTask==CryLobbyInvalidTaskID && !CanStartSearch())
		{
			CryLog("[UI] Activate delayed search %d", m_delayedSearchType);
			if (m_delayedSearchType == eDST_Full)
			{
				StartSearchingForServers();
			}
			else if (m_delayedSearchType == eDST_FavouriteId)
			{
				if (!DoFavouriteIdSearch())
				{
					NOTIFY_UILOBBY_MP(SearchCompleted());
				}
			}

			m_delayedSearchType = eDST_None;
		}
	}
}

//------------
// CALLBACKS
//------------

// This is a new callback since adding PSN support
//Basically whenever this callback is fired, you are required to fill in the requestedParams that are asked for.
//At present specifications are that the callback can fire multiple times.
//
// 'PCom'		void*		ptr to static sceNpCommunitcationId					(not copied - DO NOT PLACE ON STACK!)
// 'PPas'		void*		ptr to static sceNpCommunicationPassphrase	(not copied - DO NOT PLACE ON STACK!)
// 'XSvc'		uint32	Service Id for XLSP servers
// 'XPor'		uint16	Port for XLSP server to communicate with Telemetry
// 'LUnm'		void*		ptr to user name for local user - used by LAN (due to lack of guid) (is copied internally - DO NOT PLACE ON STACK)
//

#if USE_STEAM
#define STEAM_APPID (000000)
#endif // USE_STEAM

#if defined(PS3)

#include <np.h>

// For security reasons these values must NOT be placed outside of the executable (e.g. in xml data for instance!)

static const SceNpCommunicationId s_communication_id = {
	{'N','P','W','R','0','0','0','0','0'},
	'\0',
	0,
	0
};

static const SceNpCommunicationPassphrase s_communication_passphrase = {
	{
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
	}
};

const SceNpCommunicationSignature s_communication_signature = {
	{
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	}
};

//JAT - the temp store id for the old temp product code

static const char* s_testStore_id = "EP0006-NPXX00000_00";	// Development store id
static const char* s_sceeStore_id = "EP0006-BLES00000_00";	// SCEE store id
static const char* s_sceaStore_id = "UP0006-BLUS00000_00";	// SCEA store id
static const char* s_scejStore_id = "JP0006-BLJM00000_00";	// SCEJ store id

void CGameBrowser::GetXMBString(const char* str, SCryLobbyXMBString* pData)
{
	assert(str && pData);

	// The HUD is not initialised by the time this function is called
	// so we have to use the LocalizationManager directly, not the HUD LocalizeString().

	if (gEnv->pSystem)
	{
		ILocalizationManager* pLocMgr = gEnv->pSystem->GetLocalizationManager();
		if (pLocMgr)
		{
			wstring localizedString;
			string utf8string;

			pLocMgr->LocalizeString(str, localizedString);
			CryStringUtils::WStrToUTF8(localizedString, utf8string);

			strncpy((char*)pData->m_pStringBuffer, utf8string, pData->m_sizeOfStringBuffer);
			pData->m_sizeOfStringBuffer = strlen(utf8string); //-- we want number of bytes, so strlen is ok with utf-8 too.
			
			return;
		}
	}

	//-- Failed to get a localised string, so just copy the string identifier into the buffer
	strncpy((char*)pData->m_pStringBuffer, str, pData->m_sizeOfStringBuffer);
	pData->m_sizeOfStringBuffer = strlen(str); //-- we want number of bytes, so strlen is ok with utf-8 too.
}

#elif defined(ORBIS)

// For security reasons these values must NOT be placed outside of the executable (e.g. in xml data for instance!)
#include <np.h>

static const SceNpTitleId s_title_id = {
	{'N','P','X','X','0','0','0','0','0','_','0','0','\0'},
	{0,0,0}
};

static const SceNpTitleSecret s_title_secret = {
	{
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
	}
};

#endif


#if defined(PS3) || defined(ORBIS) || USE_STEAM

/* static */
const char* CGameBrowser::GetGameModeStringFromId(int32 id)
{
	char *strGameMode = NULL;
	switch(id)
	{
	case RICHPRESENCE_GAMEMODES_INSTANTACTION:
		strGameMode = "@ui_rules_InstantAction";
		break;

	case RICHPRESENCE_GAMEMODES_TEAMINSTANTACTION:
		strGameMode = "@ui_rules_TeamInstantAction";
		break;

	case RICHPRESENCE_GAMEMODES_ASSAULT:
		strGameMode = "@ui_rules_Assault";
		break;

	case RICHPRESENCE_GAMEMODES_CAPTURETHEFLAG:
		strGameMode = "@ui_rules_CaptureTheFlag";
		break;

	case RICHPRESENCE_GAMEMODES_CRASHSITE:
		strGameMode = "@ui_rules_CrashSite";
		break;

	case RICHPRESENCE_GAMEMODES_ALLORNOTHING:
		strGameMode = "@ui_rules_AllOrNothing";
		break;

	case RICHPRESENCE_GAMEMODES_BOMBTHEBASE:
		strGameMode = "@ui_rules_BombTheBase";
		break;

	default:
		CRY_ASSERT_MESSAGE(false, "Failed to find game rules rich presence string");
		break;	
	}

	return strGameMode;
}

/* static */
const char* CGameBrowser::GetMapStringFromId(int32 id)
{
	char *strMap = NULL;
	return strMap;
}

/* static */
void CGameBrowser::LocalisePresenceString(CryFixedStringT<MAX_PRESENCE_STRING_SIZE> &out, const char* stringId)
{
	out = CHUDUtils::LocalizeString( stringId );
}

/*static*/
void CGameBrowser::UnpackRecievedInGamePresenceString(CryFixedStringT<MAX_PRESENCE_STRING_SIZE> &out, const CryFixedStringT<MAX_PRESENCE_STRING_SIZE>& inString)
{
	out = inString;
}

/*static*/
void CGameBrowser::LocaliseInGamePresenceString(CryFixedStringT<MAX_PRESENCE_STRING_SIZE> &out, const char* stringId, const int32 gameModeId, const int32 mapId)
{
	out = CHUDUtils::LocalizeString("@mp_rp_gameplay", GetGameModeStringFromId(gameModeId), GetMapStringFromId(mapId));
}

/* static */
bool CGameBrowser::CreatePresenceString(CryFixedStringT<MAX_PRESENCE_STRING_SIZE> &out, SCryLobbyUserData *pData, uint32 numData)
{
	bool result = true;

	if(numData > 0)
	{
		CRY_ASSERT_MESSAGE(pData[CRichPresence::eRPT_String].m_id == RICHPRESENCE_ID, "");

		// pData[0] indicates the type of rich presence we setting, i.e. frontend, lobby, in-game
		// additional pData's are parameters that can be passed into the rich presence string, only used for gameplay at the moment
		switch(pData[CRichPresence::eRPT_String].m_int32)
		{
		case RICHPRESENCE_FRONTEND:
			LocalisePresenceString(out, "@mp_rp_frontend");
			break;

		case RICHPRESENCE_LOBBY:
			LocalisePresenceString(out, "@mp_rp_lobby");
			break;

		case RICHPRESENCE_GAMEPLAY:
			if(numData == 3)
			{
				const int gameModeId = pData[CRichPresence::eRPT_Param1].m_int32;
				const int mapId = pData[CRichPresence::eRPT_Param2].m_int32;
				LocaliseInGamePresenceString( out, "@mp_rp_gameplay", gameModeId, mapId );
			}
#if !defined(_RELEASE)
			else
			{
				CRY_ASSERT_MESSAGE(numData == 3, "Invalid data passed for gameplay rich presence state");
				result = false;
			}
#endif
			break;

		case RICHPRESENCE_SINGLEPLAYER:
			LocalisePresenceString(out, "@mp_rp_singleplayer");
			break;

		case RICHPRESENCE_IDLE:
			LocalisePresenceString(out, "@mp_rp_idle");
			break;

		default:
			CRY_ASSERT_MESSAGE(false, "[RichPresence] unknown rich presence type given");
			result = false;
			break;
		}
	}
	else
	{
		CryLog("[RichPresence] Failed to set rich presence because numData was 0 or there was no hud");
		result = false;
	}

	return result;
}

#endif

void CGameBrowser::ConfigurationCallback(ECryLobbyService service, SConfigurationParams *requestedParams, uint32 paramCount)
{
	uint32 a;
	for (a=0;a<paramCount;++a)
	{
		switch (requestedParams[a].m_fourCCID)
		{
		case CLCC_LAN_USER_NAME:
			{
				uint32 userIndex = g_pGame->GetExclusiveControllerDeviceIndex();

				IPlatformOS *pPlatformOS = gEnv->pSystem->GetPlatformOS();
				IPlatformOS::TUserName tUserName = "";
				if(pPlatformOS)
				{
					pPlatformOS->UserGetName(userIndex, tUserName);
				}
			
				// this will null terminate for us if necessary	
				cry_strncpy(s_profileName, tUserName.c_str(), CRYLOBBY_USER_NAME_LENGTH);
				int instance = gEnv->pSystem->GetApplicationInstance();
				if (instance>0)
				{
					size_t length = strlen(s_profileName);
					if (length + 3 < CRYLOBBY_USER_NAME_LENGTH)
					{
						s_profileName[length] = '(';
						s_profileName[length+1] = '0' + instance;
						s_profileName[length+2] = ')';
						s_profileName[length+3] = 0;
					}
				}

				requestedParams[a].m_pData = s_profileName;
			}
			break;

#if defined(PS3) || defined(ORBIS) || USE_STEAM
		case CLCC_CRYLOBBY_PRESENCE_CONVERTER:
			{
				SCryLobbyPresenceConverter* pConverter = (SCryLobbyPresenceConverter*)requestedParams[a].m_pData;
				if (pConverter)
				{
					//-- Use the pConverter->m_numData data items in pConverter->m_pData to create a string in pConverter->m_pStringBuffer
					//-- Use the initial value of pConverter->sizeOfStringBuffer as a maximum string length allowed, but
					//-- update pConverter->sizeOfStringBuffer to the correct length when the string is filled in.
					//-- Set pConverter->sizeOfStringBuffer = 0 to invalidate bad data so it isn't sent to PSN.
					CryFixedStringT<MAX_PRESENCE_STRING_SIZE> strPresence;
					if(CreatePresenceString(strPresence, pConverter->m_pData, pConverter->m_numData))
					{
						CryLog("[RichPresence] Succeeded %s", strPresence.c_str());
						sprintf((char*)pConverter->m_pStringBuffer, "%s", strPresence.c_str());
						pConverter->m_sizeOfStringBuffer = strlen((char*)pConverter->m_pStringBuffer);

#if defined(PS3)
						if (g_pGame)
						{
							pConverter->m_sessionId = g_pGame->GetPendingRichPresenceSessionID();
						}
#endif
					}
					else
					{
						CryLog("[RichPresence] Failed to create rich presence string");
						pConverter->m_sizeOfStringBuffer = 0;
					}
				}
			}
			break;
#endif
			
#if defined(XENON) || defined(DURANGO)
		case CLCC_LIVE_TITLE_ID:
			requestedParams[a].m_32 = LIVE_TITLE_ID;
			break;
#endif//XENON || DURANGO

#if defined(DURANGO)
		case CLCC_LIVE_SERVICE_CONFIG_ID:
			requestedParams[a].m_pData = LIVE_SERVICE_CONFIG_ID;
			break;
#endif//DURANGO

#if defined(ORBIS)
			// These are Orbis only - they replace PS3 CLCC_PSN_COMMUNICATION_ID, CLCC_PSN_COMMUNICATION_PASSPHRASE and CLCC_PSN_COMMUNICATION_SIGNATURE
		case CLCC_PSN_TITLE_ID:
			requestedParams[a].m_pData = (void*)&s_title_id;
			break;
		case CLCC_PSN_TITLE_SECRET:
			requestedParams[a].m_pData = (void*)&s_title_secret;
			break;
#endif

#if defined(PS3)
		case CLCC_PSN_COMMUNICATION_ID:
			requestedParams[a].m_pData = (void*)&s_communication_id;
			break;
		case CLCC_PSN_COMMUNICATION_PASSPHRASE:
			requestedParams[a].m_pData = (void*)&s_communication_passphrase;
			break;
		case CLCC_PSN_COMMUNICATION_SIGNATURE:
			requestedParams[a].m_pData = (void*)&s_communication_signature;
			break;
		case CLCC_PSN_CUSTOM_MENU_GAME_INVITE_STRING:
			GetXMBString("@xmb_invite_button", (SCryLobbyXMBString*)requestedParams[a].m_pData);			// <= SCE_NP_CUSTOM_MENU_ACTION_CHARACTER_MAX (max 21 chars + nul)	
			break;
		case CLCC_PSN_CUSTOM_MENU_GAME_JOIN_STRING:
			GetXMBString("@xmb_join_button", (SCryLobbyXMBString*)requestedParams[a].m_pData);				// <= SCE_NP_CUSTOM_MENU_ACTION_CHARACTER_MAX (max 21 chars + nul)	
			break;
		case CLCC_PSN_INVITE_SUBJECT_STRING:
			GetXMBString("@xmb_invite_sub", (SCryLobbyXMBString*)requestedParams[a].m_pData);					// < SCE_NP_BASIC_SUBJECT_CHARACTER_MAX (max 17 chars + nul)
			break;
		case CLCC_PSN_INVITE_BODY_STRING:
			GetXMBString("@xmb_invite_body", (SCryLobbyXMBString*)requestedParams[a].m_pData);				// < SCE_NP_BASIC_BODY_CHARACTER_MAX (max 511 chars + nul)																
			break;
		case CLCC_PSN_FRIEND_REQUEST_SUBJECT_STRING:
			GetXMBString("@xmb_friendrq_sub", (SCryLobbyXMBString*)requestedParams[a].m_pData);				// < SCE_NP_BASIC_SUBJECT_CHARACTER_MAX (max 17 chars + nul)																
			break;
		case CLCC_PSN_FRIEND_REQUEST_BODY_STRING:
			GetXMBString("@xmb_friendrq_body", (SCryLobbyXMBString*)requestedParams[a].m_pData);			// < SCE_NP_BASIC_BODY_CHARACTER_MAX	(max 511 chars + nul)																
			break;
		case CLCC_PSN_AGE_LIMIT:
			{
				SAgeData *pAgeData = (SAgeData*)requestedParams[a].m_pData;

				int userRegion = (pAgeData->countryCode[0] << 8) + pAgeData->countryCode[1];
				g_pGame->SetUserRegion(userRegion);

				if (strncmp(gPS3Env->sTitleID,"BLES",4)==0)
				{
					if (strncmp(pAgeData->countryCode,"de",2)==0)
					{
						// GERMANY
						requestedParams[a].m_32 = 18;
					}
					else if(strncmp(pAgeData->countryCode,"au",2)==0)
					{
						// AUSTRALIA
						requestedParams[a].m_32 = 15;
					}
					else
					{
						// EUROPE EXCLUDING GERMANY AND AUSTRALIA
						requestedParams[a].m_32 = 16;
					}
				}
				else
				{
					// US AND JAPAN (and any unknown).
					requestedParams[a].m_32 = 17;
				}
			}
			break;
		case CLCC_PSN_STORE_ID:
			if (strncmp(gPS3Env->sTitleID,"BLES",4)==0)
			{
				requestedParams[a].m_pData = (void*)s_sceeStore_id;
				break;
			}
			if (strncmp(gPS3Env->sTitleID,"BLUS",4)==0)
			{
				requestedParams[a].m_pData = (void*)s_sceaStore_id;
				break;
			}
			if (strncmp(gPS3Env->sTitleID,"BLJM",4)==0)
			{
				requestedParams[a].m_pData = (void*)s_scejStore_id;
				break;
			}
			requestedParams[a].m_pData = (void*)s_testStore_id;
			break;
		case CLCC_PSN_IS_DLC_INSTALLED:
			if (g_pGame && g_pGame->GetDLCManager() && g_pGame->GetDLCManager()->IsDLCLoaded(requestedParams[a].m_8))
			{
				requestedParams[a].m_8 = 1;
			}
			else
			{
				requestedParams[a].m_8 = 0;
			}
			break;
#endif//PS3

		case CLCC_CRYSTATS_ENCRYPTION_KEY:
			{
#if defined(XENON)
				requestedParams[a].m_pData = (void*)"";
#elif defined(PS3) || defined(ORBIS)
				requestedParams[a].m_pData = (void*)"";
#else
				requestedParams[a].m_pData = (void*)"";
#endif
			}
			break;

		case CLCC_MATCHMAKING_SESSION_PASSWORD_MAX_LENGTH:
			requestedParams[a].m_8 = MATCHMAKING_SESSION_PASSWORD_MAX_LENGTH;
			break;

#if USE_STEAM
#if !defined(RELEASE)
		case CLCC_STEAM_APPID:
			requestedParams[a].m_32 = STEAM_APPID;
			break;
#endif // !defined(RELEASE)
#endif // USE_STEAM

		default:
			CRY_ASSERT_MESSAGE(0,"Unknown Configuration Parameter Requested!");
			break;
		}
	}
}

//-------------------------------------------------------------------------
// Returns the NAT type when set-up.
void CGameBrowser::GetNatTypeCallback(UCryLobbyEventData eventData, void *arg)
{
	SCryLobbyNatTypeData *natTypeData = eventData.pNatTypeData;
	if (natTypeData)
	{
		CGameBrowser* pGameBrowser = (CGameBrowser*) arg;
		pGameBrowser->SetNatType(natTypeData->m_curState);

		const char* natString = pGameBrowser->GetNatTypeString();
		CryLog( "natString=%s", natString);

#if !defined(_RELEASE)
		if(g_pGameCVars)
		{
			g_pGameCVars->net_nat_type->ForceSet(natString);
		}
#endif
		if(g_pGame)
		{
			NOTIFY_UILOBBY_MP(UpdateNatType());
		}
	}
}

//-------------------------------------------------------------------------
const ENatType CGameBrowser::GetNatType() const
{
	return m_NatType;
}

//-------------------------------------------------------------------------
const char * CGameBrowser::GetNatTypeString() const
{
	switch(m_NatType)
	{
	case eNT_Open:
		return "@ui_mp_nattype_open";
	case eNT_Moderate:
		return "@ui_mp_nattype_moderate";
	case eNT_Strict:
		return "@ui_mp_nattype_strict";
	default:
		return "@ui_mp_nattype_unknown";
	};
	return "";
}

//-------------------------------------------------------------------------
// Register the data in CryLobby.
void CGameBrowser::InitialiseCallback(ECryLobbyService service, ECryLobbyError error, void* arg)
{
	assert( error == eCLE_Success );

	if (error == eCLE_Success)
	{
		SCryLobbyUserData userData[eLDI_Num];

		userData[eLDI_Gamemode].m_id = LID_MATCHDATA_GAMEMODE;
		userData[eLDI_Gamemode].m_type = eCLUDT_Int32;
		userData[eLDI_Gamemode].m_int32 = 0;

		userData[eLDI_Version].m_id = LID_MATCHDATA_VERSION;
		userData[eLDI_Version].m_type = eCLUDT_Int32;
		userData[eLDI_Version].m_int32 = 0;

		userData[eLDI_Playlist].m_id = LID_MATCHDATA_PLAYLIST;
		userData[eLDI_Playlist].m_type = eCLUDT_Int32;
		userData[eLDI_Playlist].m_int32 = 0;

		userData[eLDI_Variant].m_id = LID_MATCHDATA_VARIANT;
		userData[eLDI_Variant].m_type = eCLUDT_Int32;
		userData[eLDI_Variant].m_int32 = 0;

		userData[eLDI_RequiredDLCs].m_id = LID_MATCHDATA_REQUIRED_DLCS;
		userData[eLDI_RequiredDLCs].m_type = eCLUDT_Int32;
		userData[eLDI_RequiredDLCs].m_int32 = 0;

#if GAMELOBBY_USE_COUNTRY_FILTERING
		userData[eLDI_Country].m_id = LID_MATCHDATA_COUNTRY;
		userData[eLDI_Country].m_type = eCLUDT_Int32;
		userData[eLDI_Country].m_int32 = 0;
#endif

		userData[eLDI_Language].m_id = LID_MATCHDATA_LANGUAGE;
		userData[eLDI_Language].m_type = eCLUDT_Int32;
		userData[eLDI_Language].m_int32 = 0;

		userData[eLDI_Map].m_id = LID_MATCHDATA_MAP;
		userData[eLDI_Map].m_type = eCLUDT_Int32;
		userData[eLDI_Map].m_int32 = 0;

		userData[eLDI_Skill].m_id = LID_MATCHDATA_SKILL;
		userData[eLDI_Skill].m_type = eCLUDT_Int32;
		userData[eLDI_Skill].m_int32 = 0;

		userData[eLDI_Active].m_id = LID_MATCHDATA_ACTIVE;
		userData[eLDI_Active].m_type = eCLUDT_Int32;
		userData[eLDI_Active].m_int32 = 0;

		// we only want to register the user data with the service here, not set the service itself, we
		// already do that during the Init call
		gEnv->pNetwork->GetLobby()->GetLobbyService(service)->GetMatchMaking()->SessionRegisterUserData(userData, eLDI_Num, 0, NULL, NULL);

#if !defined( DEDICATED_SERVER )
		if ( gEnv->IsDedicated() )
#endif
		{
			if ( service == eCLS_Online )
			{
				TUserCredentials        credentials;
				
				gEnv->pNetwork->GetLobby()->GetLobbyService( service )->GetSignIn()->SignInUser( 0, credentials, NULL, NULL, NULL );
			}
		}
	}
}

//-------------------------------------------------------------------------
// Process a search result.
void CGameBrowser::MatchmakingSessionSearchCallback(CryLobbyTaskID taskID, ECryLobbyError error, SCrySessionSearchResult* session, void* arg)
{




	CGameBrowser* pGameBrowser = (CGameBrowser*) arg;

	if (error == eCLE_SuccessContinue || error == eCLE_Success)
	{


















































































	}
	else if (error != eCLE_SuccessUnreachable)
	{
		CryLogAlways("CGameBrowser search for sessions error %d", (int)error);
		CGameLobby::ShowErrorDialog(error, NULL, NULL, NULL);
	}

	if ((error != eCLE_SuccessContinue) && (error != eCLE_SuccessUnreachable))
	{
		CryLogAlways("CCGameBrowser::MatchmakingSessionSearchCallback DONE");

		pGameBrowser->FinishedSearch(true, !pGameBrowser->m_bFavouriteIdSearch); // FavouriteId might start another search after this one has finished
	}
}

//-------------------------------------------------------------------------
// static
void CGameBrowser::InitLobbyServiceType()
{
	// Setup the default lobby service. 
	ECryLobbyService defaultLobbyService = eCLS_LAN;

#if LEADERBOARD_PLATFORM
	if (gEnv->IsDedicated())
	{
		if ( g_pGameCVars && (g_pGameCVars->g_useOnlineServiceForDedicated))
		{
			defaultLobbyService = eCLS_Online;
		}
	}
	else
	{
		defaultLobbyService = eCLS_Online;									// We support leaderboards so must use online by default to ensure stats are posted correctly
	}
#endif

#if !defined(_RELEASE) || defined(PERFORMANCE_BUILD)
	if (g_pGameCVars && g_pGameCVars->net_initLobbyServiceToLan)
	{
		defaultLobbyService = eCLS_LAN;
	}
#endif

	CGameLobby::SetLobbyService(defaultLobbyService);
}

#if IMPLEMENT_PC_BLADES
void CGameBrowser::StartFavouriteIdSearch( const CGameServerLists::EGameServerLists serverList, uint32 *pFavouriteIds, uint32 numFavouriteIds )
{
	CRY_ASSERT(numFavouriteIds <= CGameServerLists::k_maxServersStoredInList);
	numFavouriteIds = MIN(numFavouriteIds, CGameServerLists::k_maxServersStoredInList);
	if (numFavouriteIds <= CGameServerLists::k_maxServersStoredInList)
	{
		memset(m_searchFavouriteIds, INVALID_SESSION_FAVOURITE_ID, sizeof(m_searchFavouriteIds));

		for (uint32 i=0; i<numFavouriteIds; ++i)
		{
			m_searchFavouriteIds[i] = pFavouriteIds[i];
		}

		m_currentSearchFavouriteIdIndex = 0;
		m_numSearchFavouriteIds = numFavouriteIds;

		if (CanStartSearch())
		{
			CryLog("[UI] Delayed Searching for favId sessions");
			m_delayedSearchType = eDST_FavouriteId;

			NOTIFY_UILOBBY_MP(SearchStarted());
		}
		else
		{
			if (DoFavouriteIdSearch())
			{
				m_currentFavouriteIdSearchType = serverList;

				NOTIFY_UILOBBY_MP(SearchStarted());
			}
		}
	}
}
#endif

//-------------------------------------------------------------------------
bool CGameBrowser::DoFavouriteIdSearch()
{
	CryLog("[UI] DoFavouriteIdSearch");

	bool bResult = false;

	return bResult;
}
