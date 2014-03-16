////////////////////////////////////////////////////////////////////////////
//
//  CryEngine Source File.
//  Copyright (C), Crytek, 1999-2009.
// -------------------------------------------------------------------------
//  File name:   StatsAgentPipe.cpp
//  Version:     v1.00
//  Created:     20/10/2011 by Sandy MacPherson
//  Description: Wrapper around platform-dependent pipe comms
// -------------------------------------------------------------------------
//  History:
//
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"

#if defined(ENABLE_STATS_AGENT)

#if defined(WIN32) || defined(WIN64) 
# include "CryWindows.h"
#elif defined(PS3)
#	include <cell/cell_fs.h>
#elif defined(XENON)
# include <Xbdm.h> // please be sure that stats agent never gets defined in release builds for TRC fail
#endif

#include "ProjectDefines.h"
#include "StatsAgentPipe.h"

namespace
{
	const int BUFFER_SIZE = 1024;

	CryFixedStringT<BUFFER_SIZE> s_pCommandBuffer;
	bool s_pipeOpen = false;

#if defined(XENON) || defined(DURANGO) //FIXME ?
	const char *PIPE_BASE_NAME = "";
	volatile bool s_commandWaiting = false;
#elif defined(PS3)
	const char *PIPE_BASE_NAME = "/app_home/\\\\.\\pipe\\CrysisTargetComms";
	int s_pipe = INVALID_HANDLE_VALUE;
#elif defined(WIN32) || defined(WIN64)
	const char *PIPE_BASE_NAME = "\\\\.\\pipe\\CrysisTargetComms";
	HANDLE s_pipe = INVALID_HANDLE_VALUE;
#endif
};

static int statsagent_debug = 0;

#if defined(XENON)
	HRESULT __stdcall CommandProcessor( LPCSTR pCommand, LPSTR pResponse, DWORD response, PDM_CMDCONT pdmcc );
#endif

bool CStatsAgentPipe::PipeOpen()
{
	return s_pipeOpen;
}

void CStatsAgentPipe::OpenPipe(const char *szPipeName)
{
	REGISTER_CVAR(statsagent_debug, 0, 0, "Enable/Disable StatsAgent debug messages");

	CryFixedStringT<255> buffer(PIPE_BASE_NAME);

#if !defined(XENON)
	// Construct the pipe name
	buffer += szPipeName;
	buffer.TrimRight();
	buffer += ".pipe";
#endif

	CreatePipe(buffer.c_str());

	if (statsagent_debug && s_pipeOpen)
	{
		CryLogAlways("CStatsAgent: Pipe connection \"%s\" is open", buffer.c_str());
	}

#if !defined(XENON)
	if (s_pipeOpen)
	{
		char pMsg[] = "connected";
		if (!Send(pMsg))
			ClosePipe();
	}
	else
	{
		if (statsagent_debug)
			CryLogAlways("CStatsAgent: Unable to connect pipe %s", buffer.c_str());
	}
#endif
}

bool CStatsAgentPipe::CreatePipe(const char *szName)
{
#if defined(XENON)
	DmRegisterCommandProcessor("crysis_statsagent", CommandProcessor);
	s_pipeOpen = true;
	s_commandWaiting = false;
#elif defined(PS3)
	s_pipeOpen = cellFsOpen(szName, CELL_FS_O_RDWR, &s_pipe, NULL, 0) == CELL_FS_SUCCEEDED;
#elif defined(WIN32) || defined(WIN64)
	s_pipe = ::CreateFile(szName, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
	DWORD dwMode = PIPE_NOWAIT;
	if (s_pipe != INVALID_HANDLE_VALUE)
	{
		s_pipeOpen = ::SetNamedPipeHandleState(s_pipe, &dwMode, NULL, NULL) == TRUE;
	}	
#endif

	return s_pipeOpen;
}

void CStatsAgentPipe::ClosePipe()
{
	if (s_pipeOpen)
	{
#if defined(WIN32) || defined(WIN64) 
		::CloseHandle(s_pipe);
		s_pipe = INVALID_HANDLE_VALUE;
#elif defined(PS3)
		cellFsClose( s_pipe );
		s_pipe = INVALID_HANDLE_VALUE;
#else if defined(XENON)
		// Nothing to shutdown on the XBox
#endif
		s_pipeOpen = false;
	}
}

bool CStatsAgentPipe::Send(const char *szMessage, const char *szPrefix, const char* szDebugTag)
{
	CryFixedStringT<BUFFER_SIZE> pBuffer;
	if (szPrefix)
	{
		pBuffer = szPrefix;
		pBuffer.Trim();
		pBuffer += " ";
	}
	pBuffer += szMessage;

	bool ok = true;
	uint32 nBytes = pBuffer.size() + 1;

	if (statsagent_debug)
	{
		if (szDebugTag)
		{
			CryLogAlways("CStatsAgent: Sending message \"%s\" [%s]", pBuffer.c_str(), szDebugTag);
		}
		else
		{
			CryLogAlways("CStatsAgent: Sending message \"%s\"", pBuffer.c_str());
		}
	}

#if defined(XENON)
	pBuffer.insert(0, "crysis_statsagent!");
	DmSendNotificationString(pBuffer.c_str());
#elif defined(WIN32) || defined(WIN64)
	DWORD tx;
	ok = ::WriteFile(s_pipe, pBuffer.c_str(), nBytes, &tx, 0) == TRUE;
#elif defined(PS3)
	uint64_t tx;
	ok = cellFsWrite(s_pipe, pBuffer.c_str(), nBytes, &tx) == CELL_FS_SUCCEEDED;
#endif

	if (statsagent_debug && !ok)
		CryLogAlways("CStatsAgent: Unable to write to pipe");

	return ok;
}

const char* CStatsAgentPipe::Receive()
{
	const char *szResult = NULL;

#if defined(XENON)
	if (s_commandWaiting)
	{
		s_commandWaiting = false;
		szResult = s_pCommandBuffer;
	}
#elif defined(PS3) || defined(WIN32) || defined(WIN64)
	#if defined(PS3)
	uint64 size;
	if (cellFsRead(s_pipe, s_pCommandBuffer.m_strBuf, BUFFER_SIZE - 1, &size) == CELL_FS_SUCCEEDED && size > 0)
	#elif defined(WIN32) || defined(WIN64)
	DWORD size;
	if (::ReadFile(s_pipe, s_pCommandBuffer.m_strBuf, BUFFER_SIZE - 1, &size, 0) && size > 0)
	#endif
	{
		s_pCommandBuffer.m_strBuf[size] = '\0';
		s_pCommandBuffer.TrimRight('\n');
		szResult = s_pCommandBuffer.c_str();
	}
#endif

	if (statsagent_debug && szResult)
		CryLogAlways("CStatsAgent: Received message \"%s\"", szResult);

	return szResult;
}

#if defined(XENON)
//--------------------------------------------------------------------------------------
// Temporary replacement for CRT string funcs, since
// we can't call CRT functions on the debug monitor
// thread right now.
// From MS DebugChannel sample project
//--------------------------------------------------------------------------------------


//--------------------------------------------------------------------------------------
// Name: dbgstrlen
// Desc: Critical section safe strlen() function
//--------------------------------------------------------------------------------------
static int dbgstrlen( const CHAR* str )
{
	const CHAR* strEnd = str;

	while( *strEnd )
		strEnd++;

	return strEnd - str;
}


//--------------------------------------------------------------------------------------
// Name: dbgtolower
// Desc: Returns lowercase of char
//--------------------------------------------------------------------------------------
inline CHAR dbgtolower( CHAR ch )
{
	if( ch >= 'A' && ch <= 'Z' )
		return ch - ( 'A' - 'a' );
	else
		return ch;
}


//--------------------------------------------------------------------------------------
// Name: dbgstrnicmp
// Desc: Critical section safe string compare.
//       Returns zero if the strings match.
//--------------------------------------------------------------------------------------
static INT dbgstrnicmp( const CHAR* str1, const CHAR* str2, int n )
{
	while( n > 0 )
	{
		if( dbgtolower( *str1 ) != dbgtolower( *str2 ) )
			return *str1 - *str2;
		--n;
		++str1;
		++str2;
	}

	return 0;
}


static VOID dbgstrcpy( CHAR* strDest, const CHAR* strSrc )
{
	while( ( *strDest++ = *strSrc++ ) != 0 );
}

//--------------------------------------------------------------------------------------
// End of Temporary replacement for CRT string funcs from MS DebugChannel sample
//--------------------------------------------------------------------------------------

HRESULT __stdcall CommandProcessor( LPCSTR pCommand, LPSTR pResponse, DWORD response, PDM_CMDCONT pdmcc )
{
	//CryLogAlways( "Received: %s", pCommand );
	// Check for and strip off the handler name - write specifies a command

	if ( !dbgstrnicmp( pCommand, "crysis_statsagent!write ", 24 ) )
	{
		// Acknowledge the command
		dbgstrcpy( pResponse, "Processed." );
		// Store the command to be processed
		s_pCommandBuffer = pCommand + 24;
		// Trim it to a single line
		s_pCommandBuffer.replace('\n', '\0');
		s_commandWaiting = true;
		return XBDM_NOERR;
	}
	// Check for and strip off the handler name - read specifies a request for a response
	else if ( !dbgstrnicmp( pCommand, "crysis_statsagent!read", 22 ) )
	{
		// Always respond with "Connected."
		dbgstrcpy( pResponse, "Connected." );
		return XBDM_NOERR;
	}
	else
	{
		// Acknowledge that the command failed
		dbgstrcpy( pResponse, "Failed." );
		return XBDM_UNDEFINED;
	}
}
#endif

#endif	// defined(ENABLE_STATS_AGENT)
