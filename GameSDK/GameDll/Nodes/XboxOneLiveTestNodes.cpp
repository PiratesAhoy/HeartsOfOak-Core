#include "StdAfx.h"
#include "Game.h"
#include "Nodes/G2FlowBaseNode.h"

#ifdef DURANGO
#include "../XboxOneLive/CryEngineSDK_XBLiveEvents.h"
#include "XboxOneLive/XboxLiveGameEvents.h"
#include "../ESraLibCore/include/ESraLibCore.h"
#include "../ESraLibCore/include/ESraLibCore\party.h"

//using namespace Windows::Foundation;
#endif

#pragma optimize("", off)

//////////////////////////////////////////////////////////////////////////
class CFlowXboxLiveEventTest : public CFlowBaseNode<eNCT_Instanced>
{
public:
	CFlowXboxLiveEventTest( SActivationInfo * pActInfo ) {}

	virtual ~CFlowXboxLiveEventTest() {}

	IFlowNodePtr Clone( SActivationInfo * pActInfo )
	{
		return new CFlowXboxLiveEventTest(pActInfo);
	}

	void Serialize(SActivationInfo* pActInfo, TSerialize ser) {}

	enum EInputPorts
	{
		EIP_Send = 0,
		EIP_Poll,
	};

	enum EOutputPorts
	{
		EOP_Success = 0,
		EOP_Fail,
	};

	virtual void GetConfiguration(SFlowNodeConfig& config)
	{
		static const SInputPortConfig inputs[] = {
			InputPortConfig_Void("Send", _HELP("Send test event")),
			InputPortConfig_Void("Poll", _HELP("Poll test event count")),
			{0}
		};

		static const SOutputPortConfig outputs[] = {
			OutputPortConfig_Void     ("Success",  _HELP("Triggers if sending succeeded")),
			OutputPortConfig_Void     ("Fail",  _HELP("Triggers if sending failed")),
			{0}
		};

		config.nFlags |= EFLN_TARGET_ENTITY;
		config.pInputPorts = inputs;
		config.pOutputPorts = outputs;
		config.sDescription = _HELP("Send test event to xbox live");
		config.SetCategory(EFLN_ADVANCED);
	}

	virtual void ProcessEvent( EFlowEvent event, SActivationInfo *pActInfo )
	{
		switch (event)
		{
		case eFE_Initialize:
			{
#ifdef DURANGO
				ULONG result = EventRegisterCGBH_06BFF3BF();
				CryLogAlways("Registered events: %u", result);
#endif
			}
			break;
		case eFE_Activate:
#ifdef DURANGO
		
			if(IsPortActive(pActInfo, EIP_Send))
			{
				SUserXUID XUID;
				XboxLiveGameEvents::GetUserXUID(XUID);
				GUID* roundId = new GUID();
				ZeroMemory( roundId, sizeof(GUID) );
				ULONG result = XBL_GAME_EVENT(EnemyDefeated
					, XUID.id, // UserId
					1,                  // SectionId
					g_pGame->GetPlayerSessionId(),    // PlayerSessionId
					L"", // MultiplayerSessionId
					1,                  // GameplayModeId
					1,                  // DifficultyLevelId
					roundId,            // RoundId
					1,                  // PlayerRoleId
					1,                  // PlayerWeaponTypeId
					1,                  // EnemyRoleId
					1,                  // KillTypeId
					1,                  // LocationX
					1,                  // LocationY
					1,                   // LocationZ
					1
					);

				if (result == ERROR_SUCCESS)
				{
					CryLogAlways( "EnemyDefeated event was fired" );
				}
				else
				{
					CryLogAlways( "Failed to fire EnemyDefeated event" ); 
				}
			}
#endif
			break;
		}
	}

	virtual void GetMemoryUsage(ICrySizer * s) const
	{
		s->Add(*this);
	}
};


//////////////////////////////////////////////////////////////////////////
class CFlowXboxLiveLeaderboardTest : public CFlowBaseNode<eNCT_Instanced>
{
public:
	CFlowXboxLiveLeaderboardTest( SActivationInfo * pActInfo ) {}

	virtual ~CFlowXboxLiveLeaderboardTest() {}

	IFlowNodePtr Clone( SActivationInfo * pActInfo )
	{
		return new CFlowXboxLiveLeaderboardTest(pActInfo);
	}

	void Serialize(SActivationInfo* pActInfo, TSerialize ser) {}

	enum EInputPorts
	{
		EIP_Poll = 0,
	};

	enum EOutputPorts
	{
		EOP_Success = 0,
		EOP_Fail,
	};

	virtual void GetConfiguration(SFlowNodeConfig& config)
	{
		static const SInputPortConfig inputs[] = {
			InputPortConfig_Void("Poll", _HELP("Poll leaderboard")),
			{0}
		};

		static const SOutputPortConfig outputs[] = {
			OutputPortConfig_Void     ("Success",  _HELP("Triggers if sending succeeded")),
			OutputPortConfig_Void     ("Fail",  _HELP("Triggers if sending failed")),
			{0}
		};

		config.nFlags |= EFLN_TARGET_ENTITY;
		config.pInputPorts = inputs;
		config.pOutputPorts = outputs;
		config.sDescription = _HELP("Read an example leaderboard");
		config.SetCategory(EFLN_ADVANCED);
	}

	virtual void ProcessEvent( EFlowEvent event, SActivationInfo *pActInfo )
	{
		switch (event)
		{
		case eFE_Initialize:
			{
			}
			break;
		case eFE_Activate:
#ifdef DURANGO

			if(IsPortActive(pActInfo, EIP_Poll))
			{
				// leaderboard
				IPlatformOS* pPlatformOS = GetISystem()->GetPlatformOS();
				int userIndex = pPlatformOS->GetFirstSignedInUser();
				auto user = Live::State::Manager::Instance().GetUserById( pPlatformOS->UserGetId(userIndex) );
				wstring leaderboardName(L"EnemiesDefeated");
				int startIndex = 0;
				int numEntries = 5;
				if ( user != nullptr )
				{

					user->RequestLeaderboardDataAsync(leaderboardName, startIndex, numEntries).then(
						[]( std::tuple<HRESULT, Live::LeaderboardData > result )
					{ 
						HRESULT hr = std::get<0>(result);
						if ( hr == S_OK )
						{
							Live::LeaderboardData data;
							data = std::get<1>(result);
							int breakhere = 0;
							CryLogAlways("got some data");
						}
					});
				}
			}
#endif
			break;
		}
	}

	virtual void GetMemoryUsage(ICrySizer * s) const
	{
		s->Add(*this);
	}
};


//////////////////////////////////////////////////////////////////////////
class CFlowXboxLivePartyTest : public CFlowBaseNode<eNCT_Instanced>
{
public:
	CFlowXboxLivePartyTest( SActivationInfo * pActInfo ) {}

	virtual ~CFlowXboxLivePartyTest() {}

	IFlowNodePtr Clone( SActivationInfo * pActInfo )
	{
		return new CFlowXboxLivePartyTest(pActInfo);
	}

	void Serialize(SActivationInfo* pActInfo, TSerialize ser) {}

	enum EInputPorts
	{
		EIP_Poll = 0,
	};

	enum EOutputPorts
	{
		EOP_Success = 0,
		EOP_Fail,
	};

	virtual void GetConfiguration(SFlowNodeConfig& config)
	{
		static const SInputPortConfig inputs[] = {
			InputPortConfig_Void("Poll", _HELP("Poll party info")),
			{0}
		};

		static const SOutputPortConfig outputs[] = {
			OutputPortConfig_Void     ("Success",  _HELP("Triggers if sending succeeded")),
			OutputPortConfig_Void     ("Fail",  _HELP("Triggers if sending failed")),
			{0}
		};

		config.nFlags |= EFLN_TARGET_ENTITY;
		config.pInputPorts = inputs;
		config.pOutputPorts = outputs;
		config.sDescription = _HELP("poll party info for xbox one live");
		config.SetCategory(EFLN_ADVANCED);
	}

	virtual void ProcessEvent( EFlowEvent event, SActivationInfo *pActInfo )
	{
		switch (event)
		{
		case eFE_Initialize:
			{
			}
			break;
		case eFE_Activate:
#ifdef DURANGO

			if(IsPortActive(pActInfo, EIP_Poll))
			{
				HRESULT hr = Live::State::Party::Instance().ReregisterCallbacks();
				CryLogAlways( "InitiateSession Party ReregisterCallbacks returned 0x%08x", hr );
				Live::State::Party::Instance().GetPartyViewAsync().then(
					[](HRESULT res)
				{
					CryLogAlways( "Party GetPartyViewAsync returned 0x%08x", res );
					size_t memberCount = Live::State::Party::Instance().GetPartyMembers()->size();
					CryLogAlways("Partymembers: %d", memberCount);

				});
			}
#endif
			break;
		}
	}

	virtual void GetMemoryUsage(ICrySizer * s) const
	{
		s->Add(*this);
	}
};

//////////////////////////////////////////////////////////////////////////
REGISTER_FLOW_NODE( "Actor:XboxLiveEventTest", CFlowXboxLiveEventTest);
REGISTER_FLOW_NODE( "Actor:XboxLiveLeaderboardTest", CFlowXboxLiveLeaderboardTest);
REGISTER_FLOW_NODE( "Actor:XboxLivePartyTest", CFlowXboxLivePartyTest);
