#include "StdAfx.h"
#include "UICVars.h"

CUICVars::CUICVars()
{

}

CUICVars::~CUICVars()
{

}

void CUICVars::RegisterConsoleCommandsAndVars( void )
{
	REGISTER_CVAR(hud_hide, 0, 0, "");

	REGISTER_CVAR(hud_detach, 0, 0, "");
	REGISTER_CVAR(hud_bobHud, 2.5f, 0, "");
	REGISTER_CVAR(hud_debug3dpos, 0, 0, "");

	REGISTER_CVAR(hud_cameraOverride, 0, 0, "");
	REGISTER_CVAR(hud_cameraDistance, 1.0f, 0, "");
	REGISTER_CVAR(hud_cameraOffsetZ, 1.0f, 0, "");

	REGISTER_CVAR(hud_overscanBorder_depthScale, 3.0f, 0, "");

	REGISTER_CVAR(hud_cgf_positionScaleFOV, 0.0f, 0, "");
	REGISTER_CVAR(hud_cgf_positionRightScale, 0.0f, 0, "");

	REGISTER_CVAR(hud_tagging_enabled, 1, 0, "");
	REGISTER_CVAR(hud_tagging_duration_assaultDefenders, 1.0f, 0, "");

	REGISTER_CVAR(hud_warningDisplayTimeSP, 1.0f, 0, "");
	REGISTER_CVAR(hud_warningDisplayTimeMP, 1.0f, 0, "");

	REGISTER_CVAR(hud_inputprompts_dropPromptTime, 1.0f, 0, "");

	REGISTER_CVAR(hud_Crosshair_shotgun_spreadMultiplier, 1.0f, 0, "");
	REGISTER_CVAR(hud_tagging_duration, 1.0f, 0, "");
	REGISTER_CVAR(hud_double_taptime, 0.f, 0, "");
	REGISTER_CVAR(hud_tagnames_EnemyTimeUntilLockObtained, 1.0f, 0, "");
	REGISTER_CVAR(hud_InterestPointsAtActorsHeads, 1.0f, 0, "");

	REGISTER_CVAR(hud_Crosshair_ironsight_fadeInDelay, 1.0f, 0, "");
	REGISTER_CVAR(hud_Crosshair_ironsight_fadeInTime, 1.0f, 0, "");
	REGISTER_CVAR(hud_Crosshair_ironsight_fadeOutTime, 1.0f, 0, "");

	REGISTER_CVAR(hud_Crosshair_laser_fadeInTime, 1.0f, 0, "");
	REGISTER_CVAR(hud_Crosshair_laser_fadeOutTime, 1.0f, 0, "");

	REGISTER_CVAR(hud_stereo_icon_depth_multiplier, 1.0f, 0, "");
	REGISTER_CVAR(hud_stereo_minDist, 1.0f, 0, "");

	// rank text colour in tganmes
	hud_colour_enemy = REGISTER_STRING("hud_colour_enemy","AC0000",0,"An enemy specific hex code colour string. Used in tagnames, battlelog etc.");
	hud_colour_friend = REGISTER_STRING("hud_colour_friend","9AD5B7",0,"A friend specific hex code colour string. Used in tagnames, battlelog etc..");
	hud_colour_squaddie = REGISTER_STRING("hud_colour_squaddie","00CCFF",0,"A friend on local player's sqaud specific hex code colour string. Used in tagnames, battlelog etc.");
	hud_colour_localclient = REGISTER_STRING("hud_colour_localclient","FFC800",0,"A local player specific hex code colour string. Used in tagnames, battlelog etc..");

}

void CUICVars::UnregisterConsoleCommandsAndVars( void )
{
	gEnv->pConsole->UnregisterVariable("hud_hide", true);

	gEnv->pConsole->UnregisterVariable("hud_detach", true);
	gEnv->pConsole->UnregisterVariable("hud_bobHud", true);
	gEnv->pConsole->UnregisterVariable("hud_debug3dpos", true);

	gEnv->pConsole->UnregisterVariable("hud_cameraOverride", true);
	gEnv->pConsole->UnregisterVariable("hud_cameraDistance", true);
	gEnv->pConsole->UnregisterVariable("hud_cameraOffsetZ", true);

	gEnv->pConsole->UnregisterVariable("hud_overscanBorder_depthScale", true);

	gEnv->pConsole->UnregisterVariable("hud_cgf_positionScaleFOV", true);
	gEnv->pConsole->UnregisterVariable("hud_cgf_positionRightScale", true);

	gEnv->pConsole->UnregisterVariable("hud_tagging_enabled", true);
	gEnv->pConsole->UnregisterVariable("hud_tagging_duration_assaultDefenders", true);

	gEnv->pConsole->UnregisterVariable("hud_warningDisplayTimeSP", true);
	gEnv->pConsole->UnregisterVariable("hud_warningDisplayTimeMP", true);

	gEnv->pConsole->UnregisterVariable("hud_inputprompts_dropPromptTime", true);

	gEnv->pConsole->UnregisterVariable("hud_Crosshair_shotgun_spreadMultiplier", true);
	gEnv->pConsole->UnregisterVariable("hud_tagging_duration", true);
	gEnv->pConsole->UnregisterVariable("hud_double_taptime", true);
	gEnv->pConsole->UnregisterVariable("hud_tagnames_EnemyTimeUntilLockObtained", true);
	gEnv->pConsole->UnregisterVariable("hud_InterestPointsAtActorsHeads", true);

	gEnv->pConsole->UnregisterVariable("hud_Crosshair_ironsight_fadeInDelay", true);
	gEnv->pConsole->UnregisterVariable("hud_Crosshair_ironsight_fadeInTime", true);
	gEnv->pConsole->UnregisterVariable("hud_Crosshair_ironsight_fadeOutTime", true);

	gEnv->pConsole->UnregisterVariable("hud_Crosshair_laser_fadeInTime", true);
	gEnv->pConsole->UnregisterVariable("hud_Crosshair_laser_fadeOutTime", true);

	gEnv->pConsole->UnregisterVariable("hud_stereo_icon_depth_multiplier", true);
	gEnv->pConsole->UnregisterVariable("hud_stereo_minDist", true);

	gEnv->pConsole->UnregisterVariable("hud_colour_enemy", true);
	gEnv->pConsole->UnregisterVariable("hud_colour_friend", true);
	gEnv->pConsole->UnregisterVariable("hud_colour_squaddie", true);
	gEnv->pConsole->UnregisterVariable("hud_colour_localclient", true);
}
