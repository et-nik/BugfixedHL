#ifndef CSCOREPANEL_H
#define CSCOREPANEL_H

#include <cdll_dll.h>
#include <vgui_controls/Frame.h>
#include "vgui2/IViewportPanel.h"
#include "vgui2/ViewportPanelNames.h"
#include <tier1/UtlMap.h>
#include <steam/steam_api.h>

class IViewport;
class CHudScoreBoard;

namespace vgui2
{
	class Label;
	class TextEntry;
	class Menu;
}

class CPlayerListPanel;

class CScorePanel : public vgui2::Frame, public IViewportPanel
{
public:
	DECLARE_CLASS_SIMPLE(CScorePanel, vgui2::Frame);

	// column widths at 640
	enum
	{
		AVATAR_OFFSET = 4,
		AVATAR_WIDTH = 64,
		NAME_WIDTH = 184,
		STEAMID_WIDTH = 100,
		EFF_WIDTH = 60,
		SCORE_WIDTH = 60,
		DEATH_WIDTH = 60,
		PING_WIDTH = 80
	};
	// total = 544

public:
	CScorePanel(IViewport *pParent);
	virtual ~CScorePanel();

	void FullUpdate();
	void UpdateClientInfo(int client, bool autoUpdate = true);	// autoUpdate - whether to update player count and resize at the end of client update
	void UpdateAllClients();
	void EnableMousePointer(bool enable);

	//IViewportPanel overrides
	const char *GetName() override
	{
		return VIEWPORT_PANEL_SCORE;
	}

	void SetData(KeyValues *data) override {}
	void Reset() override;
	void Update() override {}
	virtual void ApplySchemeSettings(vgui2::IScheme *pScheme);
	virtual void OnKeyCodeTyped(vgui2::KeyCode code);

	bool NeedsUpdate() override
	{
		return false;
	}

	bool HasInputElements() override
	{
		return true;
	}

	void ShowPanel(bool state) override;

	// VGUI functions:
	vgui2::VPANEL GetVPanel() override final
	{
		return BaseClass::GetVPanel();
	}

	bool IsVisible() override final
	{
		return BaseClass::IsVisible();
	}

	void SetParent(vgui2::VPANEL parent) override final
	{
		BaseClass::SetParent(parent);
	}

	// Messages
	MESSAGE_FUNC_CHARPTR(OnCommandOverride, "Command", command);	// For some reason, virtual function override doesn't work
	MESSAGE_FUNC_INT(OnItemContextMenu, "ItemContextMenu", itemID);

	CPanelAnimationVar(int, m_iAvatarWidth, "avatar_width", "34");

	friend class CHudScoreBoard;

private:
	struct team_info_t
	{
		char name[MAX_TEAM_NAME] = { 0 };
		int kills = 0;	// Calculated kills
		int deaths = 0;	// Calculated deaths
		int players = 0;// Number of players
	};

	struct menu_info_t
	{
		// Global data
		int muteItemID;
		int profilePageItemID;
		int profileUrlItemID;

		// Selected player info
		int itemID;
		int client;
		long long steamID64;
	};

	static CScorePanel *m_sSingleton;
	IViewport *m_pViewport;
	CPlayerListPanel *m_pPlayerList = nullptr;
	vgui2::Label *m_pServerNameLabel = nullptr;
	vgui2::Label *m_pMapNameLabel = nullptr;
	vgui2::Label *m_pPlayerCountLabel = nullptr;
	vgui2::Label *m_pTimerLabel = nullptr;
	vgui2::Menu *m_pMenu = nullptr;

	int m_pHeader = 0;
	int m_iPlayerCount = 0;
	int m_iMargin = 100;
	int m_iMinHeight = 320;

	vgui2::ImageList *m_pImageList;
	CUtlMap<CSteamID, int> m_mapAvatarsToImageList;
	int m_iAvatarPaddingLeft = 0, m_iAvatarPaddingRight = 0;

	team_info_t m_pTeamInfo[MAX_TEAMS + 1];
	int m_pClientItems[MAX_PLAYERS + 1];
	int m_pClientTeams[MAX_PLAYERS + 1];

	menu_info_t m_pMenuInfo;

	void RecalcItems();
	void UpdateServerName();
	void UpdateMapName();
	void UpdatePlayerCount();
	void AddHeader();
	void Resize();
	void UpdatePlayerAvatar(int playerIndex, KeyValues *kv);

	// Menu
	void CreatePlayerMenu();
	void OpenPlayerMenu(int itemID);

	// Sorting functions
	static bool StaticPlayerSortFunc(CPlayerListPanel *list, int itemID1, int itemID2);
};

#endif