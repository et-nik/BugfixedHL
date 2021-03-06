/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
//
//  hud.h
//
// class CHud declaration
//
// CHud handles the message, calculation, and drawing the HUD
//

#ifndef CHUD_H
#define CHUD_H

#include <list>
#include <memory>
#include "wrect.h"
#include "cl_dll.h"
#include "ammo.h"
#include <eiface.h>
#include "CHudBase.h"
#include <SDK_Color.h>

bool ParseColor( char *string, RGBA &rgba );

#include "..\game_shared\voice_status.h"

//-----------------------------------------------------
// Forward declarations
//-----------------------------------------------------
class TeamFortressViewport;
class CHudSpectator;
class CHudAmmo;
class CHudAmmoSecondary;
class CHudHealth;
class CHudGeiger;
class CHudTrain;
class CHudStatusBar;
class CHudDeathNotice;
class CHudMenu;
class CHudSayText;
class CHudBattery;
class CHudFlashlight;
class CHudTextMessage;
class CHudMessage;
class CHudTimer;
class CHudScores;
class CHudStatusIcons;
class CHudScoreBoard;

//-----------------------------------------------------
// Game info structures declaration
//-----------------------------------------------------
struct extra_player_info_t 
{
	short frags;
	short deaths;
	short playerclass;
	short teamnumber;
	char teamname[MAX_TEAM_NAME];
};

struct team_info_t 
{
	char name[MAX_TEAM_NAME];
	short frags;
	short deaths;
	short ping;
	short packetloss;
	short ownteam;
	short players;
	int already_drawn;
	int scores_overriden;
	int teamnumber;
};

//-----------------------------------------------------
// Game info global variables
//-----------------------------------------------------
extern hud_player_info_t	g_PlayerInfoList[MAX_PLAYERS + 1];		// player info from the engine
extern extra_player_info_t	g_PlayerExtraInfo[MAX_PLAYERS + 1];		// additional player info sent directly to the client dll
extern team_info_t			g_TeamInfo[MAX_TEAMS + 1];
extern int					g_IsSpectator[MAX_PLAYERS + 1];
extern char					g_PlayerSteamId[MAX_PLAYERS + 1][MAX_STEAMID + 1];

//-----------------------------------------------------
// CharWidths
//-----------------------------------------------------
#define MAX_BASE_CHARS 255
struct CharWidths
{
	int indexes[MAX_BASE_CHARS];
	int widths[MAX_BASE_CHARS];
	CharWidths* next;
	CharWidths()
	{
		Reset();
		next = NULL;
	}
	void Reset()
	{
		memset(indexes, 0, MAX_BASE_CHARS);
		memset(widths, 0, MAX_BASE_CHARS);
	}
};

//-----------------------------------------------------
// AG hud elements
//-----------------------------------------------------
#include "aghudglobal.h"
#include "aghudcountdown.h"
#include "aghudctf.h"
#include "aghudlocation.h"
#include "aghudlongjump.h"
#include "aghudnextmap.h"
#include "aghudplayerid.h"
#include "aghudsettings.h"
#include "aghudsuddendeath.h"
#include "aghudtimeout.h"
#include "aghudvote.h"

#define HUD_ELEM_INIT_FULL(type, var) var = std::unique_ptr<type>(new type()); var->m_isDeletable = true; var->Init();
#define HUD_ELEM_INIT(x) HUD_ELEM_INIT_FULL(CHud##x, m_##x)

class CHud
{
public:
	inline HLHSPRITE GetSprite(int index)
	{
		return (index < 0) ? 0 : m_rghSprites[index];
	}
	inline wrect_t& GetSpriteRect(int index)
	{
		return m_rgrcRects[index];
	}
	int GetSpriteIndex(const char *SpriteName);	// gets a sprite index, for use in the m_rghSprites[] array
	void AddSprite(client_sprite_t *p);

	//-----------------------------------------------------
	// HUD elements
	//-----------------------------------------------------
	std::unique_ptr<CHudAmmo>			m_Ammo	= nullptr;
	std::unique_ptr<CHudHealth>			m_Health = nullptr;
	std::unique_ptr<CHudSpectator>		m_Spectator = nullptr;
	std::unique_ptr<CHudGeiger>			m_Geiger = nullptr;
	std::unique_ptr<CHudBattery>		m_Battery = nullptr;
	std::unique_ptr<CHudTrain>			m_Train = nullptr;
	std::unique_ptr<CHudFlashlight>		m_Flash = nullptr;
	std::unique_ptr<CHudMessage>		m_Message = nullptr;
	std::unique_ptr<CHudStatusBar>		m_StatusBar = nullptr;
	std::unique_ptr<CHudDeathNotice>	m_DeathNotice = nullptr;
	std::unique_ptr<CHudSayText>		m_SayText = nullptr;
	std::unique_ptr<CHudMenu>			m_Menu = nullptr;
	std::unique_ptr<CHudAmmoSecondary>	m_AmmoSecondary = nullptr;
	std::unique_ptr<CHudTextMessage>	m_TextMessage = nullptr;
	std::unique_ptr<CHudStatusIcons>	m_StatusIcons = nullptr;
	std::unique_ptr<CHudTimer>			m_Timer = nullptr;
	std::unique_ptr<CHudScores>			m_Scores = nullptr;
	std::unique_ptr<CHudScoreBoard>		m_ScoreBoard = nullptr;		// VGUI2 scoreboard

	//-----------------------------------------------------
	// AG HUD elements
	//-----------------------------------------------------
	AgHudGlobal			m_Global;
	AgHudCountdown		m_Countdown;
	AgHudCTF			m_CTF;
	AgHudLocation		m_Location;
	AgHudLongjump		m_Longjump;
	AgHudNextmap		m_Nextmap;
	AgHudPlayerId		m_PlayerId;
	AgHudSettings		m_Settings;
	AgHudSuddenDeath	m_SuddenDeath;
	AgHudTimeout		m_Timeout;
	AgHudVote			m_Vote;

	void Init(void);
	void VidInit(void);
	void Think(void);
	int Redraw(float flTime, int intermission);
	int UpdateClientData(client_data_t *cdata, float time);

	CHud();
	~CHud();			// destructor, frees allocated memory

	//-----------------------------------------------------
	// User messages
	//-----------------------------------------------------
	int _cdecl MsgFunc_Damage(const char *pszName, int iSize, void *pbuf);
	int _cdecl MsgFunc_GameMode(const char *pszName, int iSize, void *pbuf);
	int _cdecl MsgFunc_Logo(const char *pszName, int iSize, void *pbuf);
	int _cdecl MsgFunc_ResetHUD(const char *pszName, int iSize, void *pbuf);
	void _cdecl MsgFunc_InitHUD(const char *pszName, int iSize, void *pbuf);
	void _cdecl MsgFunc_ViewMode(const char *pszName, int iSize, void *pbuf);
	int _cdecl MsgFunc_SetFOV(const char *pszName, int iSize, void *pbuf);
	int  _cdecl MsgFunc_Concuss(const char *pszName, int iSize, void *pbuf);

	// Screen information
	SCREENINFO	m_scrinfo;

	int	m_iWeaponBits;
	int	m_fPlayerDead;
	int m_iIntermission;

	// sprite indexes
	int m_HUD_number_0;

	void AddHudElem(CHudBase *elem);

	float GetSensitivity();

	HLHSPRITE	m_hsprCursor;
	float	m_flTime;	   // the current client time
	float	m_fOldTime;  // the time at which the HUD was last redrawn
	double	m_flTimeDelta; // the difference between flTime and fOldTime
	Vector	m_vecOrigin;
	Vector	m_vecAngles;
	int		m_iKeyBits;
	int		m_iHideHUDDisplay;
	int		m_iFOV;
	int		m_Teamplay;
	int		m_iRes;
	cvar_t	*m_pCvarBunnyHop;
	cvar_t	*m_pCvarStealMouse;
	cvar_t	*m_pCvarDraw;
	cvar_t	*m_pCvarDim;
	cvar_t	*m_pCvarShowNextmap;
	cvar_t	*m_pCvarShowLoss;
	cvar_t	*m_pCvarShowSteamId;
	cvar_t	*m_pCvarShowKd;
	cvar_t	*m_pCvarColorText;
	cvar_t	*m_pCvarRDynamicEntLight;

	int m_iFontHeight;
	int DrawHudNumber(int x, int y, int iFlags, int iNumber, int r, int g, int b);
	int DrawHudString(int x, int y, const char *szString, int r, int g, int b);
	int DrawHudStringReverse(int xpos, int ypos, const char *szString, int r, int g, int b);
	int DrawHudStringColorCodes(int x, int y, const char *string, int _r, int _g, int _b);
	int DrawHudStringReverseColorCodes(int x, int y, const char *string, int _r, int _g, int _b);
	int DrawHudNumberString(int xpos, int ypos, int iNumber, int r, int g, int b);
	int GetNumWidth(int iNumber, int iFlags);
	int GetHudCharWidth(int c);
	int CalculateCharWidth(int c);
	void GetHudColor(int hudPart, int value, int &r, int &g, int &b);
	float GetHudTransparency();

	inline const char *GetServerName()
	{
		return m_szServerName;
	}

	inline void SetServerNamePtr(const char *ptr)	// Called in TeamFortressViewport
	{
		m_szServerName = ptr;
	}

	inline SDK_Color GetTeamColor(int team)
	{
		return m_pTeamColors[team % HLARRAYSIZE(m_pTeamColors)];
	}

private:
	std::list<CHudBase *>	m_HudList;
	HLHSPRITE				m_hsprLogo;
	int						m_iLogo;
	client_sprite_t			*m_pSpriteList;
	int						m_iSpriteCount;
	int						m_iSpriteCountAlloc;
	int						m_iSpriteCountAllRes;
	float					m_flMouseSensitivity;
	int						m_iConcussionEffect;
	CharWidths				m_CharWidths;

	cvar_t	*m_pCvarColor;
	cvar_t	*m_pCvarColor1;
	cvar_t	*m_pCvarColor2;
	cvar_t	*m_pCvarColor3;

	RGBA	m_hudColor;
	RGBA	m_hudColor1;
	RGBA	m_hudColor2;
	RGBA	m_hudColor3;

	// the memory for these arrays are allocated in the first call to CHud::VidInit(), when the hud.txt and associated sprites are loaded.
	// freed in ~CHud()
	HLHSPRITE *m_rghSprites;	/*[HUD_SPRITE_COUNT]*/			// the sprites loaded from hud.txt
	wrect_t *m_rgrcRects;	/*[HUD_SPRITE_COUNT]*/
	char *m_rgszSpriteNames; /*[HUD_SPRITE_COUNT][MAX_SPRITE_NAME_LENGTH]*/

	struct cvar_s *default_fov;
	const char *m_szServerName = nullptr;	// Points to gViewPort->m_szServerName

	// Team colors for VGUI2
	SDK_Color m_pTeamColors[5] = {
		SDK_Color(216, 216, 216, 255),	// "Off" white (default)
		SDK_Color(125, 165, 210, 255),	// Blue
		SDK_Color(200, 90, 70, 255),	// Red
		SDK_Color(225, 205, 45, 255),	// Yellow
		SDK_Color(145, 215, 140, 255)	// Green
	};
};

extern CHud gHUD;
extern TeamFortressViewport *gViewPort;

extern int g_iPlayerClass;
extern int g_iTeamNumber;
extern int g_iUser1;
extern int g_iUser2;
extern int g_iUser3;

#endif