/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com. */
#include "menus.h"

#include <base/str.h>

#include <engine/client.h>
#include <engine/font_icons.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/key_binder.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/editor/editor.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <cstring>

// Layout recovered from KRX 0x1402482d0 and its six page renderers.
// Evidence and original conditional card sizes: research/easy-port-20261030/.
void CMenus::RenderSettingsKrx(CUIRect MainView)
{
	static CButtonContainer s_aTabs[6];
	const char *const apTabs[] = {"Aimbot", "Misc", "Visuals & HUD", "Avoid", "TAS", "Settings"};
	CUIRect Tabs, Button;
	MainView.HSplitTop(20.0f, &Tabs, &MainView);
	const float TabWidth = Tabs.w / 6.0f;
	g_Config.m_KrxMenuTab = std::clamp(g_Config.m_KrxMenuTab, 0, 5);
	for(int Index = 0; Index < 6; ++Index)
	{
		Tabs.VSplitLeft(TabWidth, &Button, &Tabs);
		if(DoButton_MenuTab(&s_aTabs[Index], Localize(apTabs[Index]), g_Config.m_KrxMenuTab == Index, &Button,
			   IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f))
			g_Config.m_KrxMenuTab = Index;
	}
	if(g_Config.m_KrxMenuTab == 3)
	{
		// Original Avoid page has a 20-unit agent dropdown after two 2-unit gaps.
		MainView.HSplitTop(4.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		static CUi::SDropDownState s_AgentDropDown;
		static CScrollRegion s_AgentDropDownScroll;
		s_AgentDropDown.m_SelectionPopupContext.m_pScrollRegion = &s_AgentDropDownScroll;
		const char *apAgents[] = {"Basic", "Legit", "Blatant", "Fent Bot", "Pilot Bot"};
		const int Agent = Ui()->DoDropDown(&Button, std::clamp(g_Config.m_AvoidAgentType, 0, 4), apAgents, 5, s_AgentDropDown);
		if(Agent != g_Config.m_AvoidAgentType)
		{
			g_Config.m_AvoidAgentType = Agent;
			g_Config.m_AvoidFreeze = 0;
		}
		if(Agent == 2)
		{
			RenderSettingsAvoid(MainView);
			return;
		}
	}

	static CScrollRegion s_aScrollRegions[6];
	CScrollRegion &Scroll = s_aScrollRegions[g_Config.m_KrxMenuTab];
	CScrollRegionParams Params;
	Params.m_ScrollUnit = 120.0f;
	Scroll.Begin(&MainView, &Params);
	CUIRect Left, Right, Body, Row;
	MainView.VSplitMid(&Left, &Right);
	if(g_Config.m_KrxMenuTab == 4)
		Left.VMargin(10.0f, &Left);
	else
		Right.VMargin(10.0f, &Right);
	const auto Card = [&](CUIRect &Column, float Height, const char *pTitle, const char *pHelpUrl = nullptr) {
		Column.HSplitTop(10.0f, nullptr, &Column);
		CUIRect Panel, Header;
		Column.HSplitTop(Height, &Panel, &Column);
		if(!Scroll.AddRect(Panel))
			return false;
		Panel.Draw(ColorRGBA(1, 1, 1, 0.25f), IGraphics::CORNER_ALL, 10.0f);
		Panel.VMargin(10.0f, &Body);
		Body.HSplitTop(5.0f, nullptr, &Body);
		Body.HSplitTop(30.0f, &Header, &Body);
		if(pHelpUrl)
		{
			CUIRect Help;
			Header.VSplitRight(40.0f, &Header, &Help);
			static CButtonContainer s_aHelp[6];
			if(Ui()->DoButton_FontIcon(&s_aHelp[g_Config.m_KrxMenuTab], FontIcon::BOOK, 0, &Help, BUTTONFLAG_LEFT))
				Client()->ViewLink(pHelpUrl);
		}
		Ui()->DoLabel(&Header, Localize(pTitle), 20.0f,
			g_Config.m_KrxMenuTab == 3 || g_Config.m_KrxMenuTab == 4 ? TEXTALIGN_TL : TEXTALIGN_ML);
		return true;
	};
	const auto Check = [&](int &Value, const char *pText, bool Indent = false, const char *pTip = nullptr) {
		Body.HSplitTop(20.0f, &Row, &Body);
		if(Indent)
			Row.VSplitLeft(20.0f, nullptr, &Row);
		if(DoButton_CheckBox(&Value, Localize(pText), Value, &Row))
			Value ^= 1;
		if(pTip)
			Ui()->DoToolTip(&Value, &Row, Localize(pTip));
	};
	const auto Slider = [&](int &Value, const char *pText, int Min, int Max, bool Indent = false, const char *pTip = nullptr) {
		Body.HSplitTop(20.0f, &Row, &Body);
		if(Indent)
			Row.VSplitLeft(20.0f, nullptr, &Row);
		Ui()->DoScrollbarOption(&Value, &Value, &Row, Localize(pText), Min, Max,
			&CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);
		if(pTip)
			Ui()->DoToolTip(&Value, &Row, Localize(pTip));
	};
	const auto Text = [&](char *pValue, size_t Size, const char *pText, int Index, const char *pEmpty = nullptr) {
		static CLineInput s_aInputs[10];
		Body.HSplitTop(20.0f, &Row, &Body);
		CUIRect Label;
		if(pText && *pText)
		{
			Row.VSplitLeft(85.0f, &Label, &Row);
			Ui()->DoLabel(&Label, Localize(pText), 13.0f, TEXTALIGN_ML);
		}
		s_aInputs[Index].SetBuffer(pValue, Size);
		s_aInputs[Index].SetEmptyText(pEmpty ? Localize(pEmpty) : "");
		Ui()->DoClearableEditBox(&s_aInputs[Index], &Row, 13.0f);
	};
	const auto Pair = [&](int &First, const char *pFirst, int &Second, const char *pSecond,
				  const char *pFirstTip = nullptr, const char *pSecondTip = nullptr) {
		Body.HSplitTop(20.0f, &Row, &Body);
		CUIRect LeftRow, RightRow;
		Row.VSplitLeft(200.0f, &LeftRow, &RightRow);
		if(DoButton_CheckBox(&First, Localize(pFirst), First, &LeftRow))
			First ^= 1;
		if(DoButton_CheckBox(&Second, Localize(pSecond), Second, &RightRow))
			Second ^= 1;
		if(pFirstTip)
			Ui()->DoToolTip(&First, &LeftRow, Localize(pFirstTip));
		if(pSecondTip)
			Ui()->DoToolTip(&Second, &RightRow, Localize(pSecondTip));
	};
	const auto FlatSlider = [&](int &Value, const char *pText, int Min, int Max, const char *pTip = nullptr) {
		Body.HSplitTop(20.0f, &Row, &Body);
		CUIRect Label;
		Row.VSplitMid(&Label, &Row);
		char aLabel[256];
		str_format(aLabel, sizeof(aLabel), "%s: %d", Localize(pText), Value);
		Ui()->DoLabel(&Label, aLabel, 13.0f, TEXTALIGN_ML);
		const float Position = Ui()->DoScrollbarH(&Value, &Row, (Value - Min) / (float)(Max - Min));
		Value = Min + (int)(Position * (Max - Min) + .1f);
		if(pTip)
			Ui()->DoToolTip(&Value, &Row, Localize(pTip));
	};
	static CButtonContainer s_aColorReset[32];
	const auto Color = [&](unsigned &Value, const char *pText, int Index, unsigned Default) {
		DoLine_ColorPicker(&s_aColorReset[Index], 25.0f, 14.0f, g_Config.m_KrxMenuTab == 4 ? 2.0f : 0.0f, &Body, Localize(pText), &Value,
			color_cast<ColorRGBA>(ColorHSLA(Default)), false);
	};
	const auto Choice = [&](int &Value, const char *pText, const char **ppNames, int Count, int Index, bool Indent = false) {
		static CUi::SDropDownState s_aDropDowns[20];
		static CScrollRegion s_aDropDownScrolls[20];
		Body.HSplitTop(20.0f, &Row, &Body);
		if(Indent)
			Row.VSplitLeft(20.0f, nullptr, &Row);
		CUIRect Label;
		if(pText && *pText)
		{
			Row.VSplitLeft(80.0f, &Label, &Row);
			Ui()->DoLabel(&Label, Localize(pText), 13.0f, TEXTALIGN_ML);
		}
		s_aDropDowns[Index].m_SelectionPopupContext.m_pScrollRegion = &s_aDropDownScrolls[Index];
		Value = Ui()->DoDropDown(&Row, Value, ppNames, Count, s_aDropDowns[Index]);
	};
	const auto Key = [&](int Index, const char *pLabel, const char *pCommand, const char *pLegacyCommand = nullptr) {
		static CButtonContainer s_aKeys[24], s_aClear[24];
		CBindSlot Current = EMPTY_BIND_SLOT;
		for(int Modifier = 0; Modifier < KeyModifier::COMBINATION_COUNT; ++Modifier)
			for(int KeyIndex = 0; KeyIndex < KEY_LAST; ++KeyIndex)
				if(str_comp(GameClient()->m_Binds.Get(KeyIndex, Modifier), pCommand) == 0 ||
					(pLegacyCommand && str_comp(GameClient()->m_Binds.Get(KeyIndex, Modifier), pLegacyCommand) == 0))
					Current = CBindSlot(KeyIndex, Modifier);
		Body.HSplitTop(20.0f, &Row, &Body);
		CUIRect Label;
		Row.VSplitLeft(135.0f, &Label, &Row);
		Ui()->DoLabel(&Label, Localize(pLabel), 13.0f, g_Config.m_KrxMenuTab == 4 ? TEXTALIGN_TL : TEXTALIGN_ML);
		const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&s_aKeys[Index], &s_aClear[Index], &Row, Current, false, false);
		if(!Result.m_Aborted && Result.m_Bind != Current)
		{
			if(Current != EMPTY_BIND_SLOT)
				GameClient()->m_Binds.Bind(Current.m_Key, "", false, Current.m_ModifierMask);
			if(Result.m_Bind != EMPTY_BIND_SLOT)
				GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, pCommand, false, Result.m_Bind.m_ModifierMask);
		}
		Body.HSplitTop(2.0f, nullptr, &Body);
	};

	if(g_Config.m_KrxMenuTab == 0)
	{
		const float MainHeight = (g_Config.m_KrxAimbotTargetGlow ? 220.0f : 165.0f) + (g_Config.m_KrxAimbotAdvancedSettings ? 40.0f : 0.0f);
		if(Card(Left, MainHeight, "Main"))
		{
			Check(g_Config.m_KrxAimbot, "Enable");
			Check(g_Config.m_KrxAimbotDrawFov, "Draw FOV");
			Check(g_Config.m_KrxAimbotTargetBox, "Target Box");
			Check(g_Config.m_KrxAimbotTargetGlow, "Target Glow");
			if(g_Config.m_KrxAimbotTargetGlow)
			{
				Color(g_Config.m_KrxAimbotTargetGlowWeaponColor, "Weapon", 6, 0x00ff80);
				Color(g_Config.m_KrxAimbotTargetGlowHookColor, "Hook", 7, 0x00ff80);
			}
			Check(g_Config.m_KrxAimbotAdvancedSettings, "Advanced Settings");
			Check(g_Config.m_KrxAimbotPerfectSilent, "Perfect Silent", false, "Only for 25 TPS servers; automatic shots and hooks wait for an even predicted tick");
			if(g_Config.m_KrxAimbotAdvancedSettings)
			{
				Slider(g_Config.m_KrxAimbotMinShootLength, "Min Shoot Delay", 1, 10, true);
				Slider(g_Config.m_KrxAimbotMaxShootLength, "Max Shoot Delay", 1, 10, true);
			}
		}
		if(Card(Left, 115.0f, "Hotkeys"))
		{
			Key(13, "Aimbot:", "toggle krx_aimbot 1 0");
			Key(11, "Aimbot Auto Hook:", "+toggle krx_aimbot_autohook_key 1 0");
			Key(12, "Aimbot Auto Shoot:", "+toggle krx_aimbot_autoshoot_key 1 0");
		}
		struct CWeaponSettings
		{
			const char *m_pName;
			int *m_pEnable, *m_pFov, *m_pPriority, *m_pSilent, *m_pIgnoreFriends;
		};
		const CWeaponSettings aWeapons[] = {
			{"Hook", &g_Config.m_KrxAimbotWeaponHook, &g_Config.m_KrxAimbotFovHook, &g_Config.m_KrxAimbotTargetPriorityHook, &g_Config.m_KrxAimbotSilentHook, &g_Config.m_KrxAimbotIgnoreFriendsHook},
			{"Hammer", &g_Config.m_KrxAimbotWeaponHammer, &g_Config.m_KrxAimbotFovHammer, &g_Config.m_KrxAimbotTargetPriorityHammer, &g_Config.m_KrxAimbotSilentHammer, &g_Config.m_KrxAimbotIgnoreFriendsHammer},
			{"Pistol", &g_Config.m_KrxAimbotWeaponGun, &g_Config.m_KrxAimbotFovGun, &g_Config.m_KrxAimbotTargetPriorityGun, &g_Config.m_KrxAimbotSilentGun, &g_Config.m_KrxAimbotIgnoreFriendsGun},
			{"Shotgun", &g_Config.m_KrxAimbotWeaponShotgun, &g_Config.m_KrxAimbotFovShotgun, &g_Config.m_KrxAimbotTargetPriorityShotgun, &g_Config.m_KrxAimbotSilentShotgun, &g_Config.m_KrxAimbotIgnoreFriendsShotgun},
			{"Grenade", &g_Config.m_KrxAimbotWeaponGrenade, &g_Config.m_KrxAimbotFovGrenade, &g_Config.m_KrxAimbotTargetPriorityGrenade, &g_Config.m_KrxAimbotSilentGrenade, &g_Config.m_KrxAimbotIgnoreFriendsGrenade},
			{"Laser", &g_Config.m_KrxAimbotWeaponLaser, &g_Config.m_KrxAimbotFovLaser, &g_Config.m_KrxAimbotTargetPriorityLaser, &g_Config.m_KrxAimbotSilentLaser, &g_Config.m_KrxAimbotIgnoreFriendsLaser},
		};
		for(int Index = 0; Index < 6; ++Index)
		{
			const auto &Weapon = aWeapons[Index];
			if(Card(Right, *Weapon.m_pEnable ? (Index == 0 || Index == 4 ? 165.0f : 145.0f) : 65.0f, Weapon.m_pName))
			{
				Check(*Weapon.m_pEnable, "Enable");
				if(*Weapon.m_pEnable)
				{
					Slider(*Weapon.m_pFov, "FOV", 1, 315);
					Check(*Weapon.m_pPriority, *Weapon.m_pPriority ? "Target Priority: Closest to Player" : "Target Priority: Closest to Crosshair");
					Check(*Weapon.m_pSilent, "Silent");
					Check(*Weapon.m_pIgnoreFriends, "Ignore Friends");
					if(Index == 0)
						Slider(g_Config.m_KrxAimbotAccuracy, "Accuracy", 1, 200);
					if(Index == 4)
						Check(g_Config.m_KrxAimbotGrenadeMovePrediction, "Grenade move pred");
				}
			}
		}
	}
	else if(g_Config.m_KrxMenuTab == 1)
	{
		if(Card(Left, 225.0f + 20.0f * g_Config.m_KrxQuickStop + 40.0f * g_Config.m_KrxDummyFly, "Bots", "https://krxclient.gitbook.io/docs/features/misc"))
		{
			Check(g_Config.m_KrxMoonwalk, "Moonwalk");
			Check(g_Config.m_KrxAutoFire, "Auto Fire");
			Check(g_Config.m_KrxAutoRehook, "Auto Rehook");
			Check(g_Config.m_KrxAutoJumpSave, "Auto jump save", false, "Automatically jumps when it can save you");
			Check(g_Config.m_KrxGrenadeSaver, "Auto grenade save", false, "Automatically shoots a grenade to try and save you");
			Check(g_Config.m_KrxQuickStop, "Quick Stop");
			if(g_Config.m_KrxQuickStop)
				Check(g_Config.m_KrxQuickStopGrounded, "Ground Only", true);
			Check(g_Config.m_KrxDummyFly, "Dummy Fly", false, "Makes the dummy hammer + hook too");
			if(g_Config.m_KrxDummyFly)
			{
				Check(g_Config.m_KrxDummyFlyHook, "Hook", true);
				Check(g_Config.m_KrxDummyFlyKinta, "Kinta", true);
			}
			Check(g_Config.m_KrxJetRide, "Jet Ride", false, "Works like hook ride bot but with jetpack");
			Check(g_Config.m_KrxAutoAled, "Auto Aled", false, "Auto hammers when an aled is possible");
		}
		if(Card(Left, g_Config.m_KrxAutoEdgeAdvancedSettings ? 345 : 245, "Auto Edge"))
		{
			Check(g_Config.m_KrxAutoEdge, "Enable");
			Check(g_Config.m_KrxAutoEdgeAdvancedSettings, "Advanced settings");
			if(g_Config.m_KrxAutoEdgeAdvancedSettings)
			{
				Slider(g_Config.m_KrxAutoEdgePopulationSize, "Population Size", 128, 8192);
				Slider(g_Config.m_KrxAutoEdgeExplorationDepth, "Exploration Depth", 1, 50);
				Slider(g_Config.m_KrxAutoEdgeTopKCandidates, "Top-K Candidates", 1, 100);
				Slider(g_Config.m_KrxAutoEdgeSequenceLength, "Sequence Length", 1, 20);
				Slider(g_Config.m_KrxAutoEdgeDetectionRange, "Detection Range", 100, 800);
			}
			Check(g_Config.m_KrxAutoEdgeDetectFreeze, "Detect Freeze");
			Check(g_Config.m_KrxAutoEdgeDetectDeath, "Detect Death");
			Check(g_Config.m_KrxAutoEdgeDetectTele, "Detect Teleporters");
			Check(g_Config.m_KrxAutoEdgeShowVisitedTiles, "Visited tiles", false, "The original registers this option but has no rendering consumer");
			Check(g_Config.m_KrxAutoEdgeShowFoundEdges, "Show Found Edges");
			Check(g_Config.m_KrxAutoEdgeShowLockedEdge, "Show Locked Edge");
		}
		if(Card(Left, g_Config.m_KrxSpoofVersion ? 105.0f : 85.0f, "Protections"))
		{
			Check(g_Config.m_KrxRandomTimeoutSeed, "Random Timeout Seed");
			Check(g_Config.m_KrxSpoofVersion, "Version Spoofer");
			if(g_Config.m_KrxSpoofVersion)
				Slider(g_Config.m_KrxSpoofVersionNr, "Version", 10000, 20000, true);
		}
		if(Card(Left, g_Config.m_KrxModDetector ? 105.0f : 65.0f, "Mod Detector"))
		{
			Check(g_Config.m_KrxModDetector, "Enable");
			if(g_Config.m_KrxModDetector)
			{
				Pair(g_Config.m_KrxModDetectorNames, "Detect Names", g_Config.m_KrxModDetectorWarn, "Detect Suspicious", nullptr,
					"Warns you in F1 console and in HUD, if a suspicous player is detected");
				Pair(g_Config.m_KrxModDetectorLeave, "Leave on Mod", g_Config.m_KrxModDetectorWarnLeave, "Leave on Suspicious",
					"Will leave when a mod is detected",
					"Will leave when a suspicious player is detected(Suggestion: keep this off unless you want to be very stealthy)");
			}
		}
		if(Card(Left, 65.0f + 20.0f * g_Config.m_KrxAutoShotgun + (g_Config.m_KrxAutoShotgun && g_Config.m_KrxAutoShotgunAdvanced ? 120.0f : 0.0f), "Auto Shotgun"))
		{
			Check(g_Config.m_KrxAutoShotgun, "Enable");
			if(g_Config.m_KrxAutoShotgun)
			{
				Check(g_Config.m_KrxAutoShotgunAdvanced, "Advanced settings");
				if(g_Config.m_KrxAutoShotgunAdvanced)
				{
					Check(g_Config.m_KrxAutoShotgunMostBounces, g_Config.m_KrxAutoShotgunMostBounces ? "Bounces: Most" : "Bounces: Least");
					Check(g_Config.m_KrxAutoShotgunHighestVel, g_Config.m_KrxAutoShotgunHighestVel ? "Speed: Most" : "Speed: Least");
					Check(g_Config.m_KrxAutoShotgunSilent, "Silent");
					Slider(g_Config.m_KrxAutoShotgunPoints, "Points", 1, 100);
					Slider(g_Config.m_KrxAutoShotgunTicks, "Ticks", 1, 100);
					Slider(g_Config.m_KrxAutoShotgunFov, "FOV", 1, 315);
				}
			}
		}
		if(Card(Left, 65.0f + 40.0f * g_Config.m_KrxUnfreezeBot + (g_Config.m_KrxUnfreezeBot && g_Config.m_KrxUnfreezeBotAdvanced ? 120.0f : 0.0f), "Auto Unfreeze"))
		{
			Check(g_Config.m_KrxUnfreezeBot, "Enable");
			if(g_Config.m_KrxUnfreezeBot)
			{
				Check(g_Config.m_KrxUnfreezeBotEsp, "ESP");
				Check(g_Config.m_KrxUnfreezeBotAdvanced, "Advanced settings");
				if(g_Config.m_KrxUnfreezeBotAdvanced)
				{
					Check(g_Config.m_KrxUnfreezeBotMostBounces, g_Config.m_KrxUnfreezeBotMostBounces ? "Bounces: Most" : "Bounces: Least");
					Check(g_Config.m_KrxUnfreezeBotSilent, "Silent");
					Slider(g_Config.m_KrxUnfreezeBotPoints, "Points", 1, 100);
					Slider(g_Config.m_KrxUnfreezeBotCurDirTicks, "Current dir ticks", 1, 100);
					Slider(g_Config.m_KrxUnfreezeBotTicks, "Ticks", 1, 100);
					Slider(g_Config.m_KrxUnfreezeBotFov, "FOV", 1, 315);
				}
			}
		}
		if(Card(Left, g_Config.m_KrxFakeAim ? (g_Config.m_KrxFakeAimMode == 2 ? 145.0f : 125.0f) : 65.0f, "Fake aim"))
		{
			Check(g_Config.m_KrxFakeAim, "Enable");
			if(g_Config.m_KrxFakeAim)
			{
				Check(g_Config.m_KrxFakeAimSendAlways, "Send always");
				Check(g_Config.m_KrxFakeAimVisible, "Visible");
				const char *apModes[] = {"Mouse pos", "Robot aim", "Spinbot", "Random", "Fake angles", "Aimbot troll aim"};
				Choice(g_Config.m_KrxFakeAimMode, "", apModes, 6, 1);
				if(g_Config.m_KrxFakeAimMode == 2)
					Slider(g_Config.m_KrxFakeAimSpeed, "Speed", -50, 50);
			}
		}
		const int NameField = g_Config.m_KrxChangeNameOnFinish && !g_Config.m_KrxChangeNameOnFinishRandom;
		if(Card(Right, 185.0f + 20.0f * (g_Config.m_KrxChangeNameOnFinish + NameField + g_Config.m_KrxAutoTeam + g_Config.m_KrxAntiAfk + g_Config.m_ClFastInput), "Other"))
		{
			Check(g_Config.m_KrxChangeNameOnFinish, "Change name on finish", false, "Changes name on finish");
			if(g_Config.m_KrxChangeNameOnFinish)
			{
				if(!g_Config.m_KrxChangeNameOnFinishRandom)
					Text(g_Config.m_KrxChangeNameOnFinishName, sizeof(g_Config.m_KrxChangeNameOnFinishName), "", 0, "ILoveKRX!");
				Check(g_Config.m_KrxChangeNameOnFinishRandom, "Random name", true, "Creates a random name on finish");
			}
			Check(g_Config.m_KrxAutoTeam, "Auto team", false, "Automatically joins a team and locks it");
			if(g_Config.m_KrxAutoTeam)
				FlatSlider(g_Config.m_KrxAutoTeamPreferred, "Team", 0, 63, "Preferred team number");
			Check(g_Config.m_KrxAntiAfk, "Anti afk");
			if(g_Config.m_KrxAntiAfk)
				Slider(g_Config.m_KrxAntiAfkSeconds, "Seconds", 1, 300, true, "Duration in seconds for getting afk on a server");
			Check(g_Config.m_KrxKillOnFreeze, "Kill on freeze", false, "Automatically kills when you get frozen");
			Check(g_Config.m_ClFastInput, "Fast input", false, "1 tick faster visual input delay");
			if(g_Config.m_ClFastInput)
				Check(g_Config.m_ClFastInputOthers, "Fast input others", true, "Makes dragging other tees easier(increases other tees visual latency");
			Check(g_Config.m_KrxHideBubble, "Hide Chat Bubble", false, "Keep Avoid, Rocket Save and other helpers active in chat and menus");
			Body.HSplitTop(20.0f, &Row, &Body);
			CUIRect VerifyButton;
			Row.VSplitLeft(200.0f, &Row, &VerifyButton);
			if(DoButton_CheckBox(&g_Config.m_ClAutoVerify, "Auto Verify", g_Config.m_ClAutoVerify, &Row))
				g_Config.m_ClAutoVerify ^= 1;
			Ui()->DoToolTip(&g_Config.m_ClAutoVerify, &Row, Localize("Will verify you for DDOS protected servers (visits the site ger10.ddnet.tw)"));
			static CButtonContainer s_ManualVerify;
			if(DoButton_Menu(&s_ManualVerify, Localize("Manual Verify"), 0, &VerifyButton))
				Client()->ViewLink("https://ger10.ddnet.tw/");
		}
		if(Card(Right, g_Config.m_KrxChatRepeater ? 185.0f : 145.0f, "Troll"))
		{
			Check(g_Config.m_KrxEmoteSpam, "Emote Spam");
			Check(g_Config.m_KrxKillsay, "Killsay/Deathsay");
			Check(g_Config.m_KrxFancyChatFont, "Fancy Chat Font");
			Check(g_Config.m_KrxMassMentionSpam, "Mass Mention Spam");
			Check(g_Config.m_KrxChatRepeater, "Chat Repeater");
			if(g_Config.m_KrxChatRepeater)
			{
				FlatSlider(g_Config.m_KrxChatRepeaterLength, "Min Message Length", 1, 20);
				Slider(g_Config.m_KrxChatRepeaterCooldown, "Cooldown (seconds)", 5, 10);
			}
		}
		if(Card(Right, g_Config.m_KrxAutoVote ? 85.0f : 65.0f, "Auto Vote"))
		{
			Check(g_Config.m_KrxAutoVote, "Enable");
			if(g_Config.m_KrxAutoVote)
				Check(g_Config.m_KrxAutoVoteF4, g_Config.m_KrxAutoVoteF4 ? "Mode: F4" : "Mode: F3");
		}
		if(Card(Right, g_Config.m_KrxIdStealer ? 205.0f : 65.0f, "ID Stealer"))
		{
			Check(g_Config.m_KrxIdStealer, "Enable");
			if(g_Config.m_KrxIdStealer)
			{
				Check(g_Config.m_KrxIdStealerClosest, g_Config.m_KrxIdStealerClosest ? "Target Mode: Closest Player" : "Target Mode: Random Player");
				Check(g_Config.m_KrxIdStealerName, "Steal Name");
				Check(g_Config.m_KrxIdStealerClan, "Steal Clan");
				Check(g_Config.m_KrxIdStealerSkin, "Steal Skin");
				Check(g_Config.m_KrxIdStealerFlag, "Steal Flag");
				Check(g_Config.m_KrxIdStealerEmote, "Steal Eye Emote");
				FlatSlider(g_Config.m_KrxIdStealerSpeed, "Stealer Speed", 5, 60);
			}
		}
		if(Card(Right, g_Config.m_KrxGhostMove ? 145.0f : 65.0f, "Ghost Move"))
		{
			Check(g_Config.m_KrxGhostMove, "Enable", false, "Makes it so others don't see your arrows for direction, jump, ...");
			if(g_Config.m_KrxGhostMove)
			{
				Check(g_Config.m_KrxGhostMoveDirection, "Direction", false, "Makes it so others don't see your arrows for direction");
				Check(g_Config.m_KrxGhostMoveJump, "Jump", false, "Makes it so others don't see your arrows for jump");
				Check(g_Config.m_KrxGhostMoveHook, "Hook", false, "Makes it so others don't see your hook");
				Check(g_Config.m_KrxGhostMoveHookClosest, "Hook closest", false, "Makes it so you hook the closest tee, others don't see you changing aim at all and the hook is invisible to them");
			}
		}
		if(Card(Right, g_Config.m_KrxAutoVoteKickMode == 1 ? 150.0f : 125.0f, "Auto Vote Kick"))
		{
			Check(g_Config.m_KrxAutoVoteKick, "Enable");
			Check(g_Config.m_KrxAutoVoteKickMode, g_Config.m_KrxAutoVoteKickMode ? "Mode: Specific Target" : "Mode: Random Target");
			if(g_Config.m_KrxAutoVoteKickMode == 1)
			{
				const int Target = g_Config.m_KrxAutoVoteKickTarget;
				char aTarget[128];
				if(GameClient()->m_Snap.m_pLocalInfo && Target >= 0 && Target < MAX_CLIENTS && GameClient()->m_aClients[Target].m_Active)
					str_format(aTarget, sizeof(aTarget), "%s: %s", Localize("Target Player"), GameClient()->m_aClients[Target].m_aName);
				else
					str_format(aTarget, sizeof(aTarget), "%s: %d", Localize("Target Player ID"), Target);
				Body.HSplitTop(20.0f, &Row, &Body);
				CUIRect Label;
				Row.VSplitMid(&Label, &Row);
				Ui()->DoLabel(&Label, aTarget, 13.0f, TEXTALIGN_ML);
				g_Config.m_KrxAutoVoteKickTarget = std::clamp((int)(Ui()->DoScrollbarH(&g_Config.m_KrxAutoVoteKickTarget, &Row, Target / 63.0f) * 63), 0, 63);
			}
			Body.HSplitTop(20.0f, &Row, &Body);
			Ui()->DoLabel(&Row, Localize("Kick Reason:"), 13.0f, TEXTALIGN_ML);
			Text(g_Config.m_KrxAutoVoteKickReason, sizeof(g_Config.m_KrxAutoVoteKickReason), "", 1, "Enter reason here...");
		}
	}
	else if(g_Config.m_KrxMenuTab == 2)
	{
		if(Card(Left, g_Config.m_KrxNowPlayingHud ? 180 : 65, "Now Playing HUD"))
		{
			Check(g_Config.m_KrxNowPlayingHud, "Enable");
			if(g_Config.m_KrxNowPlayingHud)
			{
				Check(g_Config.m_KrxNowPlayingSegmented, "Segmented");
				Slider(g_Config.m_KrxNowPlayingGradientSpeed, "Speed", 1, 100);
				Color(g_Config.m_KrxNowPlayingGradientStartColor, "Start", 26, 0x55ff80);
				Color(g_Config.m_KrxNowPlayingGradientEndColor, "End", 27, 0x55ff42);
				Check(g_Config.m_KrxNowPlayingGradientRainbow, "Rainbow");
			}
		}
		if(Card(Left, g_Config.m_KrxFeaturesHud ? 260.0f : 105.0f, "HUD", "https://krxclient.gitbook.io/docs/features/visuals-hud"))
		{
			Check(g_Config.m_KrxFeaturesHud, "Features HUD", false, "Hides the HUD with features (Aimbot, Avoid, ...)");
			if(g_Config.m_KrxFeaturesHud)
			{
				Check(g_Config.m_KrxFeaturesHudSegmented, "Segmented");
				Slider(g_Config.m_KrxFeaturesHudOpacity, "Opacity", 1, 100);
				Slider(g_Config.m_KrxFeaturesHudFontSize, "Font size", 1, 10);
				Slider(g_Config.m_KrxFeaturesHudGradientSpeed, "Speed", 1, 100);
				Color(g_Config.m_KrxFeaturesHudGradientStartColor, "Start", 8, 0x55ff80);
				Color(g_Config.m_KrxFeaturesHudGradientEndColor, "End", 9, 0x55ff42);
				Check(g_Config.m_KrxFeaturesHudGradientRainbow, "Rainbow");
			}
			Check(g_Config.m_KrxNotifications, "Notifications", false, "Shows notifications in top right corner from bots");
			Check(g_Config.m_KrxTasHud, "TAS HUD");
		}
		if(Card(Left, g_Config.m_KrxWatermark ? 180.0f : 65.0f, "Watermark"))
		{
			Check(g_Config.m_KrxWatermark, "Enable");
			if(g_Config.m_KrxWatermark)
			{
				Check(g_Config.m_KrxWatermarkSegmented, "Segmented");
				Slider(g_Config.m_KrxWatermarkGradientSpeed, "Speed", 1, 100);
				Color(g_Config.m_KrxWatermarkGradientStartColor, "Start", 10, 0x55ff80);
				Color(g_Config.m_KrxWatermarkGradientEndColor, "End", 11, 0x55ff42);
				Check(g_Config.m_KrxWatermarkGradientRainbow, "Rainbow");
			}
		}
		if(Card(Left, g_Config.m_KrxCustomTheme ? 140.0f : 65.0f, "Custom Menu"))
		{
			Check(g_Config.m_KrxCustomTheme, "Enable");
			if(g_Config.m_KrxCustomTheme)
			{
				Color(g_Config.m_KrxCustomAccentColor, "Accent", 0, 0x9bffdc);
				Color(g_Config.m_KrxCustomBgColor, "Background", 1, 0xb3ff18);
				Check(g_Config.m_KrxRainbowMenu, "Rainbow");
			}
		}
		if(Card(Left, g_Config.m_ClOutline ? 415.0f : 65.0f, "Tile Outlines"))
		{
			Check(g_Config.m_ClOutline, "Enable");
			if(g_Config.m_ClOutline)
			{
				Check(g_Config.m_ClOutlineInEntities, "Only in entities");
				Check(g_Config.m_ClOutlineFreeze, "Freeze");
				Check(g_Config.m_ClOutlineSolid, "Walls");
				Check(g_Config.m_ClOutlineTele, "Teleports");
				Check(g_Config.m_ClOutlineUnfreeze, "Unfreeze");
				Check(g_Config.m_ClOutlineKill, "Death");
				Slider(g_Config.m_ClOutlineWidth, "Outline width", 1, 16);
				Slider(g_Config.m_ClOutlineAlpha, "Outline alpha", 1, 100);
				Slider(g_Config.m_ClOutlineAlphaSolid, "Outline alpha(walls)", 1, 100);
				Color(g_Config.m_ClOutlineColorFreeze, "Freeze outline color", 13, 0);
				Color(g_Config.m_ClOutlineColorSolid, "Walls outline color", 17, 0);
				Color(g_Config.m_ClOutlineColorTele, "Teleports outline color", 16, 0);
				Color(g_Config.m_ClOutlineColorUnfreeze, "Unfreeze outline color", 15, 0);
				Color(g_Config.m_ClOutlineColorKill, "Death outline color", 14, 0);
			}
		}
		if(Card(Left, g_Config.m_ClLastNotify ? 250.0f : 210.0f, "KoG"))
		{
			Check(g_Config.m_ClFrozenTeesHud, "Show Frozen HUD");
			Check(g_Config.m_ClFrozenTeesHudSkins, "Use Skins Instead of Ninja Tees");
			Check(g_Config.m_ClFrozenTeesOnlyInteam, "Only Show After Joining a Team");
			FlatSlider(g_Config.m_ClFrozenTeesMaxRows, "Max Rows", 1, 6);
			// Original visual slider spans 8..27 although registry clamps at20.
			FlatSlider(g_Config.m_ClFrozenTeesSize, "Tee Size", 8, 27);
			Body.HSplitTop(20.0f, &Row, &Body);
			CUIRect CountFrozen;
			Row.VSplitMid(&Row, &CountFrozen);
			if(DoButton_CheckBox(&g_Config.m_ClFrozenTeesText, Localize("Tees Right Alive Text"), g_Config.m_ClFrozenTeesText > 0, &Row))
				g_Config.m_ClFrozenTeesText = g_Config.m_ClFrozenTeesText < 1;
			static CButtonContainer s_CountFrozen;
			if(g_Config.m_ClFrozenTeesText && DoButton_CheckBox(&s_CountFrozen, Localize("Count Frozen Tees"), g_Config.m_ClFrozenTeesText == 2, &CountFrozen))
				g_Config.m_ClFrozenTeesText = (g_Config.m_ClFrozenTeesText != 2) + 1;
			Check(g_Config.m_ClLastNotify, "Show When You Are Last");
			if(g_Config.m_ClLastNotify)
			{
				Text(g_Config.m_ClLastNotifyText, sizeof(g_Config.m_ClLastNotifyText), "", 3, "Last!");
				Color(g_Config.m_ClLastNotifyColor, "Notification Color", 21, 0x100);
			}
			Check(g_Config.m_ClCursorInSpec, "Show Cursor in Free Spectate");
		}
		if(Card(Right, g_Config.m_KrxDrawAimLines ? 180.0f : 65.0f, "Aim Lines"))
		{
			Check(g_Config.m_KrxDrawAimLines, "Draw Aim Lines");
			if(g_Config.m_KrxDrawAimLines)
			{
				Check(g_Config.m_KrxDrawAimLinesSelfOnly, "Self Only");
				Check(g_Config.m_KrxDrawAimLinesGun, "Pistol");
				Check(g_Config.m_KrxDrawAimLinesShotgun, "Shotgun");
				Check(g_Config.m_KrxDrawAimLinesGrenade, "Grenade");
				Check(g_Config.m_KrxDrawAimLinesLaser, "Laser");
				FlatSlider(g_Config.m_KrxDrawAimLinesAlpha, "Alpha", 0, 100);
			}
		}
		if(Card(Right, g_Config.m_KrxDrawBulletLines ? 160.0f : 65.0f, "Bullet Lines"))
		{
			Check(g_Config.m_KrxDrawBulletLines, "Draw Bullet Lines");
			if(g_Config.m_KrxDrawBulletLines)
			{
				Check(g_Config.m_KrxDrawBulletLinesSelfOnly, "Self Only");
				Check(g_Config.m_KrxDrawBulletLinesGun, "Pistol");
				Check(g_Config.m_KrxDrawBulletLinesShotgun, "Shotgun");
				Check(g_Config.m_KrxDrawBulletLinesGrenade, "Grenade");
				FlatSlider(g_Config.m_KrxDrawBulletLinesAlpha, "Alpha", 0, 100);
			}
		}
		const bool Rainbow = (g_Config.m_KrxCustomColorTee && g_Config.m_KrxCustomColorTeeRainbow) ||
				     (g_Config.m_KrxCustomColorHook && g_Config.m_KrxCustomColorHookRainbow) ||
				     (g_Config.m_KrxCustomColorWeapon && g_Config.m_KrxCustomColorWeaponRainbow);
		if(Card(Right, 105.0f + 50.0f * (g_Config.m_KrxCustomColorTee + g_Config.m_KrxCustomColorHook + g_Config.m_KrxCustomColorWeapon) + (Rainbow ? 20.0f : 0.0f), "Custom Colors"))
		{
			Check(g_Config.m_KrxCustomColorTee, "Tee");
			if(g_Config.m_KrxCustomColorTee)
			{
				Color(g_Config.m_KrxCustomColorTeeCol, "Color", 2, 0x55ff80);
				Check(g_Config.m_KrxCustomColorTeeRainbow, "Rainbow", true);
			}
			Check(g_Config.m_KrxCustomColorHook, "Hook");
			if(g_Config.m_KrxCustomColorHook)
			{
				Color(g_Config.m_KrxCustomColorHookCol, "Color", 3, 0x55ff80);
				Check(g_Config.m_KrxCustomColorHookRainbow, "Rainbow", true);
			}
			Check(g_Config.m_KrxCustomColorWeapon, "Weapon");
			if(g_Config.m_KrxCustomColorWeapon)
			{
				Color(g_Config.m_KrxCustomColorWeaponCol, "Color", 4, 0x55ff80);
				Check(g_Config.m_KrxCustomColorWeaponRainbow, "Rainbow", true);
			}
			if(Rainbow)
				Slider(g_Config.m_KrxCustomColorRainbowSpeed, "Rainbow speed", 1, 100);
		}
		if(Card(Right, g_Config.m_KrxCustomFreezeColorTee ? 90.0f : 70.0f, "Custom Frozen Color"))
		{
			Check(g_Config.m_KrxCustomFreezeColorTee, "Enable");
			if(g_Config.m_KrxCustomFreezeColorTee)
				Color(g_Config.m_KrxCustomFreezeColorTeeCol, "Color", 5, 0x55ff80);
		}
		if(Card(Right, g_Config.m_KrxTrajectoryEsp ? 240.0f : 65.0f, "Trajectory"))
		{
			Check(g_Config.m_KrxTrajectoryEsp, "Enable");
			if(g_Config.m_KrxTrajectoryEsp)
			{
				Check(g_Config.m_KrxTrajectoryEspSelfOnly, "Self only");
				Slider(g_Config.m_KrxTrajectoryEspTicks, "Ticks", 1, 50, false, "Trajectory will predict this much ticks");
				Slider(g_Config.m_KrxTrajectoryEspPathThickness, "Path thickness", 1, 10, false, "Thickness of dots or line.");
				const char *apModes[] = {"Full line", "Dotted"};
				Choice(g_Config.m_KrxTrajectoryEspPathMode, "", apModes, 2, 5);
				Body.HSplitTop(5.0f, nullptr, &Body);
				Color(g_Config.m_KrxTrajectoryEspPathColor, "Local color", 18, 0x55ff80);
				Body.HSplitTop(5.0f, nullptr, &Body);
				Color(g_Config.m_KrxTrajectoryEspPathColorFrozen, "Frozen color", 19, 0x1ff80);
				Body.HSplitTop(5.0f, nullptr, &Body);
				Color(g_Config.m_KrxTrajectoryEspPathColorOthers, "Others color", 20, 0x80ff80);
			}
		}
		if(Card(Right, g_Config.m_KrxTrail ? (g_Config.m_KrxTrailType == 4 ? 255.0f : 140.0f) : 65.0f, "Trail"))
		{
			Check(g_Config.m_KrxTrail, "Enable");
			if(g_Config.m_KrxTrail)
			{
				const char *apModes[] = {"Smoke trail", "Bullet trail", "Powerup trail", "Sparkle trail", "Tater trail"};
				Choice(g_Config.m_KrxTrailType, "", apModes, 5, 2);
				if(g_Config.m_KrxTrailType == 4)
				{
					Slider(g_Config.m_ClTeeTrailWidth, "Trail width", 0, 20);
					Slider(g_Config.m_ClTeeTrailLength, "Trail length", 5, 200);
					Slider(g_Config.m_ClTeeTrailAlpha, "Trail alpha", 1, 100);
					Check(g_Config.m_ClTeeTrailTaper, "Trail low taper");
					Check(g_Config.m_ClTeeTrailFade, "Trail fade");
					const char *apColors[] = {"Solid", "Tee", "Rainbow", "Speed", "Random"};
					Choice(g_Config.m_ClTeeTrailColorMode, "Color", apColors, 5, 12);
					Color(g_Config.m_ClTeeTrailColor, "Solid color", 25, 0xff);
				}
				else
				{
					Color(g_Config.m_KrxTrailColor, "Color", 12, 0xb3ff18);
					Check(g_Config.m_KrxTrailColorRainbow, "Rainbow");
				}
			}
		}
		if(Card(Right, g_Config.m_KrxSmoothCam ? 105.0f : 85.0f, "Cam"))
		{
			Check(g_Config.m_KrxSmoothCam, "Smooth cam", false, "Smooth camera for in-game or demo. Mostly useful in demo");
			if(g_Config.m_KrxSmoothCam)
				Slider(g_Config.m_KrxSmoothCamSmoothness, "Smoothness", 1, 100, true, "Smoothness of smooth cam(lower = faster)");
			Check(g_Config.m_KrxSuperDyncam, "Super dyncam", false, "Enables super zoom when using dyncam");
		}
	}
	else if(g_Config.m_KrxMenuTab == 3)
	{
		const auto Action = [&](const char *pText, int Index) {
			static CButtonContainer s_aActions[8];
			Body.HSplitTop(20, &Row, &Body);
			return DoButton_Menu(&s_aActions[Index], Localize(pText), 0, &Row);
		};
		const float Height = 185 + (g_Config.m_KrxFentLightTile ? 20 : 0) + (g_Config.m_KrxFentAdvancedSettings ? 60 : 0);
		if(g_Config.m_AvoidAgentType < 2)
		{
			if(Card(Left, g_Config.m_AvoidAfkProtection ? 145 : 125, "Main"))
			{
				Check(g_Config.m_AvoidFreeze, "Enable");
				Check(g_Config.m_AvoidPlayerPrediction, "Player Prediction");
				Check(g_Config.m_AvoidAfkProtection, "Afk protection");
				if(g_Config.m_AvoidAfkProtection)
					Slider(g_Config.m_AvoidAfkTime, "Seconds", 1, 300);
			}
			if(g_Config.m_AvoidAgentType == 1)
			{
				if(Card(Left, 205, "Settings"))
				{
					Slider(g_Config.m_AvoidLegitCheckTicks, "Check ticks", 1, 50);
					Slider(g_Config.m_ClAvoidNumIterations, "Quality", 1, 1000);
					Check(g_Config.m_AvoidLegitDirection, "Direction");
					Check(g_Config.m_AvoidLegitHook, "Hook");
					Slider(g_Config.m_AvoidDirectionWeight, "Direction priority", 1, 1000);
					Slider(g_Config.m_AvoidHookWeight, "Hook priority", 1, 1000);
					Slider(g_Config.m_AvoidLifespanWeight, "Life priority", 1, 1000);
					Slider(g_Config.m_AvoidExplorationConstant, "Randomness", 1, 1000);
				}
				if(Card(Right, g_Config.m_AvoidLegitUnfreeze ? 125 : 105, "Tiles"))
				{
					Check(g_Config.m_AvoidLegitTeles, "Teles");
					Check(g_Config.m_AvoidLegitDeath, "Death");
					Check(g_Config.m_AvoidLegitUnfreeze, "Unfreeze");
					if(g_Config.m_AvoidLegitUnfreeze)
						Slider(g_Config.m_AvoidLegitUnfreezeTicks, "Ticks", 1, 30);
				}
			}
			Scroll.End();
			return;
		}
		if(g_Config.m_AvoidAgentType == 4 && Card(Left, 245, "Pilot Bot", "https://krxclient.gitbook.io/docs/ultimate-features/avoid/pilot-bot"))
		{
			Check(g_Config.m_AvoidFreeze, "Enable Pilot Bot");
			Check(g_Config.m_AvoidPlayerPrediction, "Player Prediction");
			const char *apModes[] = {"Autonomous", "Follow Cursor", "Follow Player"};
			Choice(g_Config.m_AvoidPilotBotMode, "Mode", apModes, 3, 13);
			Slider(g_Config.m_AvoidPilotBotPopulationSize, "Population Size", 128, 8192);
			Slider(g_Config.m_AvoidPilotBotExplorationDepth, "Exploration Depth", 5, 50);
			Slider(g_Config.m_AvoidPilotBotTopKCandidates, "Top-K Candidates", 1, 100);
			Slider(g_Config.m_AvoidPilotBotSequenceLength, "Sequence Length", 1, 20);
		}
		if(g_Config.m_AvoidAgentType == 3 && Card(Left, Height, "Main", "https://krxclient.gitbook.io/docs/ultimate-features/avoid/fentbot"))
		{
			Check(g_Config.m_AvoidPlayerPrediction, "Player Prediction", false, "Different from settings player prediction - this completely removes all characters");
			Check(g_Config.m_KrxFentLightTile, "Unfreeze", false, "Supports going through light tiles");
			if(g_Config.m_KrxFentLightTile)
				Slider(g_Config.m_KrxFentLightTileRadius, "Radius", 0, 20, false, "Radius for the light tiles");
			Check(g_Config.m_KrxFentAdvancedSettings, "Advanced Settings");
			if(g_Config.m_KrxFentAdvancedSettings)
			{
				Slider(g_Config.m_KrxFentTicks, "Fent ticks", 1000, 10000);
				Slider(g_Config.m_KrxFentTweakerActions, "Tweaker inputs", 50, 5000);
				Slider(g_Config.m_KrxFentTweakerTicks, "Tweaker ticks", 1, 30);
				Slider(g_Config.m_KrxFentTweakerDosage, "Tweaker dosage", 1, 500);
			}
			else
			{
				const char *apQuality[] = {"Low", "Mid", "Max"};
				Choice(g_Config.m_KrxFentQualitySetting, "Quality", apQuality, 3, 8);
			}
			Body.HSplitTop(5, nullptr, &Body);
			if(Action(g_Config.m_AvoidFreeze ? "Overdose" : "Inject fent", 0))
				g_Config.m_AvoidFreeze ^= 1;
		}
		if(Card(Left, 145, "Visuals"))
		{
			Check(g_Config.m_AvoidDrawPath, "Render path", false, "Shows visuals for path");
			Check(g_Config.m_KrxTileEditorEnable, "Enable Editor");
			Check(g_Config.m_KrxPathFindingRenderFlowField, "Render pathfinding", false, "Shows visuals for pathfinding");
			Check(g_Config.m_KrxFentSpectateScan, "Spectate scan", false, "Spectates the latest position of the scan");
		}
		if(Card(Right, 105, "Pathfinding"))
		{
			if(Action("Recalculate", 1))
				g_Config.m_KrxPathFinding = 1;
			if(Action("Auto Finish", 2))
				g_Config.m_KrxTileEditorAutoFinish = 1;
		}
		if(Card(Right, 165, "Tile Editor"))
		{
			Check(g_Config.m_KrxTileEditorEnable, "Enable Editor");
			const char *apTypes[] = {"Tunnel", "Finish"};
			Choice(g_Config.m_KrxTileEditorType, "Type", apTypes, 2, 14);
			if(Action("Auto Tunnels", 3))
				g_Config.m_KrxTileEditorAutoTunnel = 1;
			Slider(g_Config.m_KrxTileEditorAutoTunnelWidth, "Auto Tunnel Width", 0, 10);
			if(Action("Clear All Tiles", 4))
				g_Config.m_KrxTileEditorClear = 1;
		}
	}
	else if(g_Config.m_KrxMenuTab == 4)
	{
		GameClient()->m_Tas.CheckAutoSyncVault();
		const auto Action = [&](const char *pText, int Index, ColorRGBA Color = ColorRGBA(1, 1, 1, .5f)) {
			static CButtonContainer s_aActions[16];
			Body.HSplitTop(20, &Row, &Body);
			return DoButton_Menu(&s_aActions[Index], Localize(pText), 0, &Row, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5, 0, Color);
		};
		const bool HasReplay = GameClient()->m_Tas.Replay().m_aFrames[0].size() > 50;
		if(Card(Left, HasReplay ? 115 : 90, "Main", "https://krxclient.gitbook.io/docs/ultimate-features/tas"))
		{
			Slider(g_Config.m_KrxTasTps, "TPS", 1, 50, false, "Sets the tick speed of TAS(default teeworlds TPS: 50)");
			static CButtonContainer s_aTransport[3];
			CUIRect Buttons[3], Rest;
			Body.HSplitTop(5, nullptr, &Body);
			Body.HSplitTop(20, &Rest, &Body);
			const float Width = Rest.w / 3;
			Rest.VSplitLeft(Width, &Buttons[0], &Rest);
			Rest.VSplitLeft(Width, &Buttons[1], &Buttons[2]);
			Buttons[0].VSplitRight(5, &Buttons[0], nullptr);
			Buttons[1].VSplitRight(5, &Buttons[1], nullptr);
			if(Ui()->DoButton_FontIcon(&s_aTransport[0], g_Config.m_KrxRecordReplay ? FontIcon::STOP : FontIcon::CIRCLE_PLAY, 0, &Buttons[0], BUTTONFLAG_LEFT))
				g_Config.m_KrxRecordReplay ^= 1;
			Ui()->DoToolTip(&s_aTransport[0], &Buttons[0], Localize(g_Config.m_KrxRecordReplay ? "Stop recording TAS" : "Start recording TAS"));
			if(DoButton_Menu(&s_aTransport[1], g_Config.m_KrxTasPause ? Localize("Unpause") : Localize("Pause"), 0, &Buttons[1], BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5, 0, g_Config.m_KrxTasPause ? ColorRGBA(1, 0, 0, .5f) : ColorRGBA(1, 1, 1, .5f)))
				g_Config.m_KrxTasPause ^= 1;
			const bool FentBusy = GameClient()->m_Fent.Busy();
			if(DoButton_Menu(&s_aTransport[2], g_Config.m_KrxLoadReplay ? Localize("Stop replay") : Localize("Playback replay"), 0, &Buttons[2], BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5, 0, g_Config.m_KrxLoadReplay ? ColorRGBA(1, 0, 0, .5f) : ColorRGBA(1, 1, 1, .5f)) && !FentBusy)
				g_Config.m_KrxLoadReplay ^= 1;
			Ui()->DoToolTip(&s_aTransport[1], &Buttons[1], Localize(g_Config.m_KrxTasPause ? "Unpause TAS" : "Pause TAS"));
			Ui()->DoToolTip(&s_aTransport[2], &Buttons[2], Localize(FentBusy ? "Fent is still searching. Wait for Replay transferred to TAS." : g_Config.m_KrxLoadReplay ? "Stop replay" :
																						       "Playback replay"));
			if(HasReplay)
			{
				Body.HSplitTop(5, nullptr, &Body);
				static bool s_ConfirmClear = false;
				if(!s_ConfirmClear)
				{
					static CButtonContainer s_ClearReplay;
					Body.HSplitTop(20, &Row, &Body);
					if(Ui()->DoButton_FontIcon(&s_ClearReplay, FontIcon::TRASH, 0, &Row, BUTTONFLAG_LEFT))
						s_ConfirmClear = true;
					Ui()->DoToolTip(&s_ClearReplay, &Row, Localize("Clear current replay"));
				}
				else
				{
					CUIRect Confirm, Cancel;
					Body.HSplitTop(20, &Row, &Body);
					Row.VSplitMid(&Confirm, &Cancel, 5);
					static CButtonContainer s_Clear, s_Cancel;
					if(Ui()->DoButton_FontIcon(&s_Clear, "\uF00C", 0, &Confirm, BUTTONFLAG_LEFT))
					{
						g_Config.m_KrxTasRespawn ^= 1;
						s_ConfirmClear = false;
					}
					if(Ui()->DoButton_FontIcon(&s_Cancel, FontIcon::XMARK, 0, &Cancel, BUTTONFLAG_LEFT))
						s_ConfirmClear = false;
				}
			}
		}
		if(Card(Left, 125 + (g_Config.m_KrxTasAutoReplay && g_Config.m_KrxAutoSaveReplayEnabled ? 20 : 0), "Settings"))
		{
			CUIRect SettingsLeft, SettingsRight;
			Body.VSplitMid(&SettingsLeft, &SettingsRight);
			Body = SettingsLeft;
			Check(g_Config.m_KrxTasUseSound, "Enable sound", false, "Enables using sounds in the TAS world");
			Check(g_Config.m_KrxTasShowEffects, "Enable effects", false, "Enables showing effects(like hammer hit, double jump, ...) in TAS world");
			Check(g_Config.m_KrxTasShowAim, "Show real aim", false, "Shows where you are aiming while replaying a TAS");
			Check(g_Config.m_KrxTasPlayerPrediction, "Predict players", false, "When enabled, adds every tee to the TAS world");
			Body = SettingsRight;
			Check(g_Config.m_KrxTasAutoReplay, "Auto Replay", false, "Auto replays TAS");
			if(g_Config.m_KrxTasAutoReplay)
			{
				Check(g_Config.m_KrxTasAutoReplayFreeze, "On freeze", true, "Restarts auto replay when frozen");
				Check(g_Config.m_KrxTasAutoReplayKill, "Kill", true, "Kills on end of replay");
			}
			Check(g_Config.m_KrxAutoSaveReplayEnabled, "Auto Save Replay", false, "Automatically saves a TAS replay to the replays-folder/auto/ directory");
			if(g_Config.m_KrxAutoSaveReplayEnabled)
				Slider(g_Config.m_KrxAutoSaveReplayInterval, "Seconds", 10, 120, true, "Sets the interval in seconds for autosave");
		}
		if(Card(Left, 180, "Hotkeys"))
		{
			Key(16, "Record:", "toggle krx_recordreplay 1 0");
			Key(17, "Load:", "toggle krx_loadreplay 1 0");
			Key(18, "Clear:", "+toggle krx_tasrespawn 1 0");
			Key(19, "Pause:", "toggle krx_taspause 0 1");
			Key(20, "Rewind:", "+toggle krx_tasrewind 1 0");
			Key(21, "Forward:", "+toggle krx_tasforward 1 0");
		}
		const auto &Replay = GameClient()->m_Tas.Replay();
		const auto &Files = GameClient()->m_Tas.ReplayFiles();
		const bool HasLegacy = std::any_of(Files.begin(), Files.end(), [](const auto &Name) { return str_endswith(Name.c_str(), ".json"); });
		const bool ShowReplayInfo = !Replay.m_Author.empty() && !Replay.m_aFrames[0].empty() && GameClient()->m_Tas.FinishTicks() != 0;
		if(Card(Right, (ShowReplayInfo ? 175 : 115) + (HasLegacy ? 25 : 0), "Replay"))
		{
			// 263510: popup browser, immediate children, activation opens a
			// directory or loads a replay. Keep its list stable across frames.
			struct CReplayBrowser
			{
				struct CEntry
				{
					std::string m_Name;
					bool m_Directory;
				};
				CMenus *m_pMenus = nullptr;
				std::string m_Directory;
				std::vector<CEntry> m_vEntries;
				CListBox m_List;
				int m_Selected = -1;
				void Populate()
				{
					m_vEntries.clear();
					m_pMenus->Storage()->ListDirectory(IStorage::TYPE_ALL, ("replays/" + m_Directory).c_str(), [](const char *pName, int IsDir, int, void *pUser) {
						if(pName[0] != '.' && (IsDir || str_endswith(pName, ".tas") || str_endswith(pName, ".json")))
							static_cast<CReplayBrowser *>(pUser)->m_vEntries.push_back({pName, IsDir != 0});
						return 0; }, this);
					std::sort(m_vEntries.begin(), m_vEntries.end(), [](const auto &A, const auto &B) {
						return A.m_Directory != B.m_Directory ? A.m_Directory > B.m_Directory : A.m_Name < B.m_Name;
					});
					m_vEntries.erase(std::unique(m_vEntries.begin(), m_vEntries.end(), [](const auto &A, const auto &B) { return A.m_Name == B.m_Name && A.m_Directory == B.m_Directory; }), m_vEntries.end());
					if(!m_Directory.empty())
						m_vEntries.insert(m_vEntries.begin(), {"..", true});
					m_Selected = -1;
					m_List.ScrollToSelected();
				}
				static CUi::EPopupMenuFunctionResult Render(void *pContext, CUIRect View, bool Active)
				{
					auto &Browser = *static_cast<CReplayBrowser *>(pContext);
					auto *pMenus = Browser.m_pMenus;
					Browser.m_List.SetActive(Active);
					Browser.m_List.DoStart(20, Browser.m_vEntries.size(), 1, 3, Browser.m_Selected, &View, false);
					for(const auto &Entry : Browser.m_vEntries)
					{
						const auto Item = Browser.m_List.DoNextItem(&Entry);
						if(!Item.m_Visible)
							continue;
						CUIRect Icon, Label;
						Item.m_Rect.VSplitLeft(20, &Icon, &Label);
						pMenus->TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
						pMenus->Ui()->DoLabel(&Icon, Entry.m_Directory ? Entry.m_Name == ".." ? FontIcon::FOLDER_TREE : FontIcon::FOLDER : FontIcon::MAP, 12, TEXTALIGN_ML);
						pMenus->TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
						pMenus->Ui()->DoLabel(&Label, Entry.m_Name.c_str(), 10, TEXTALIGN_ML);
					}
					Browser.m_Selected = Browser.m_List.DoEnd();
					if(Browser.m_List.WasItemActivated() && Browser.m_Selected >= 0 && Browser.m_Selected < (int)Browser.m_vEntries.size())
					{
						const auto Entry = Browser.m_vEntries[Browser.m_Selected];
						if(!Entry.m_Directory)
						{
							str_copy(g_Config.m_KrxReplayFile, (Browser.m_Directory + Entry.m_Name).c_str());
							pMenus->GameClient()->m_Tas.LoadReplay(g_Config.m_KrxReplayFile);
							return CUi::POPUP_CLOSE_CURRENT;
						}
						if(Entry.m_Name == "..")
						{
							Browser.m_Directory.pop_back();
							const auto Slash = Browser.m_Directory.find_last_of('/');
							Browser.m_Directory.resize(Slash == std::string::npos ? 0 : Slash + 1);
						}
						else
							Browser.m_Directory += Entry.m_Name + "/";
						Browser.Populate();
					}
					return CUi::POPUP_KEEP_OPEN;
				}
			};
			static CReplayBrowser s_Browser;
			static SPopupMenuId s_BrowserPopup;
			static CButtonContainer s_OpenBrowser;
			Body.HSplitTop(20, &Row, &Body);
			CUIRect Folder, Refresh, Load, Browse = Row;
			Browse.VSplitLeft(30, &Folder, &Browse);
			Folder.VSplitRight(5, &Folder, nullptr);
			Browse.VSplitRight(55, &Browse, &Refresh);
			Browse.VSplitRight(5, &Browse, nullptr);
			Refresh.VSplitRight(25, &Refresh, &Load);
			Refresh.VSplitRight(5, &Refresh, nullptr);
			static CButtonContainer s_OpenFolder, s_RefreshFiles, s_LoadFile;
			if(Ui()->DoButton_FontIcon(&s_OpenFolder, FontIcon::FOLDER, 0, &Folder, BUTTONFLAG_LEFT))
			{
				char aDirectory[IO_MAX_PATH_LENGTH];
				Storage()->CreateFolder("replays", IStorage::TYPE_SAVE);
				Storage()->GetCompletePath(IStorage::TYPE_SAVE, "replays", aDirectory, sizeof(aDirectory));
				Client()->ViewFile(aDirectory);
			}
			if(Ui()->DoButton_FontIcon(&s_RefreshFiles, FontIcon::ARROW_ROTATE_LEFT, 0, &Refresh, BUTTONFLAG_LEFT))
				GameClient()->m_Tas.RefreshFiles();
			if(Ui()->DoButton_FontIcon(&s_LoadFile, FontIcon::RIGHT_TO_BRACKET, 0, &Load, BUTTONFLAG_LEFT))
				g_Config.m_KrxLoadReplayFromFile = 1;
			const char *pFilename = std::strrchr(g_Config.m_KrxReplayFile, '/');
			pFilename = pFilename ? pFilename + 1 : g_Config.m_KrxReplayFile;
			if(DoButton_Menu(&s_OpenBrowser, pFilename, 0, &Browse))
			{
				GameClient()->m_Tas.RefreshFiles();
				s_Browser.m_pMenus = this;
				s_Browser.Populate();
				Ui()->DoPopupMenu(&s_BrowserPopup, Browse.x, Browse.y + Browse.h, Browse.w, 250, &s_Browser, CReplayBrowser::Render);
			}
			if(ShowReplayInfo)
			{
				Body.HSplitTop(20, &Row, &Body);
				Ui()->DoLabel(&Row, Localize("Selected replay:"), 15, TEXTALIGN_ML);
				char aInfo[256];
				str_format(aInfo, sizeof(aInfo), "Author: %s", Replay.m_Author.c_str());
				Body.HSplitTop(20, &Row, &Body);
				CUIRect Total;
				Row.VSplitMid(&Row, &Total);
				Ui()->DoLabel(&Row, aInfo, 10, TEXTALIGN_ML);
				str_format(aInfo, sizeof(aInfo), "Total Time: %dm %.2fs (%d t)", (int)Replay.m_aFrames[0].size() / 3000, std::fmod(Replay.m_aFrames[0].size() / 50.0f, 60.0f), (int)Replay.m_aFrames[0].size());
				Ui()->DoLabel(&Total, aInfo, 10, TEXTALIGN_ML);
				Body.HSplitTop(20, &Row, &Body);
				Row.VSplitMid(&Row, &Total);
				str_format(aInfo, sizeof(aInfo), "Map: %s", Replay.m_Map.c_str());
				Ui()->DoLabel(&Row, aInfo, 10, TEXTALIGN_ML);
				str_format(aInfo, sizeof(aInfo), "Finish Time: %dm %.2fs (%d t)", GameClient()->m_Tas.FinishTicks() / 3000, std::fmod(GameClient()->m_Tas.FinishTicks() / 50.0f, 60.0f), GameClient()->m_Tas.FinishTicks());
				Ui()->DoLabel(&Total, aInfo, 10, TEXTALIGN_ML);
			}
			Body.HSplitTop(5, nullptr, &Body);
			CUIRect Validate, GetTime;
			Body.HSplitTop(20, &Row, &Body);
			Row.VSplitMid(&Validate, &GetTime, 5);
			static CButtonContainer s_Validate, s_GetTime;
			if(DoButton_Menu(&s_Validate, Localize("Validate replay"), 0, &Validate))
				GameClient()->m_Tas.ValidateReplay();
			if(DoButton_Menu(&s_GetTime, Localize("Get replay time"), 0, &GetTime))
				GameClient()->m_Tas.GetReplayTime();
			Body.HSplitTop(5, nullptr, &Body);
			if(Action("Save replay", 7, ColorRGBA(.5f, .5f, 0, .5f)))
				m_Popup = POPUP_SAVE_KRX_REPLAY;
			if(HasLegacy)
			{
				Body.HSplitTop(5, nullptr, &Body);
				if(Action("Convert replays to new format", 13))
					GameClient()->m_Tas.ConvertLegacyReplays();
			}
		}
		if(Card(Right, 75, "Replay Vault"))
		{
			CUIRect Sync, Rest;
			Body.HSplitTop(20, &Row, &Rest);
			Row.VSplitMid(&Body, &Sync, 5);
			if(Action("Auto Sync", 14, g_Config.m_KrxTasReplayVaultAutoSync ? ColorRGBA(0, 1, 0, .5f) : ColorRGBA(1, 1, 1, .5f)))
				g_Config.m_KrxTasReplayVaultAutoSync ^= 1;
			Body = Sync;
			if(Action("Sync now", 10))
				GameClient()->m_Tas.SyncVault();
			Body = Rest;
			Body.HSplitTop(20, &Row, &Body);
			Ui()->DoLabel(&Row, Localize("Automatically sync with the replay vault from github.com/krxclient/krx-replays"), 10, TEXTALIGN_ML);
		}
		if(Card(Right, 85, "Tools"))
		{
			Slider(g_Config.m_KrxTickControlTicks, "Ticks", 1, 10, false, "Sets the tick speed rewinding/forwarding");
			CUIRect Toggles[4], Rest;
			Body.HSplitTop(20, &Rest, &Body);
			for(int I = 0; I < 4; ++I)
			{
				Rest.VSplitLeft(Rest.w / (4 - I), &Toggles[I], &Rest);
				if(I != 3)
					Toggles[I].VSplitRight(5, &Toggles[I], nullptr);
			}
			static CButtonContainer s_aTools[4];
			int *apValues[] = {&g_Config.m_KrxTasAutoRewind, &g_Config.m_KrxTasAutoForward, &g_Config.m_KrxTickControlPause, &g_Config.m_KrxTickControlStep};
			const char *apNames[] = {"Auto rewind", "Auto forward", "Auto pause", "Step"};
			const char *apTips[] = {"Automatically rewinds when frozen to a safe position", "Automatically forward when frozen to 1 tick before unfreezing", "When forwarding/rewinding automatically pauses TAS", "When pressing forward/rewind only does it by 1 tick(i.e. steps)"};
			for(int I = 0; I < 4; ++I)
			{
				if(DoButton_Menu(&s_aTools[I], Localize(apNames[I]), 0, &Toggles[I], BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5, 0, *apValues[I] ? ColorRGBA(0, 1, 0, .5f) : ColorRGBA(1, 1, 1, .5f)))
					*apValues[I] ^= 1;
				Ui()->DoToolTip(&s_aTools[I], &Toggles[I], Localize(apTips[I]));
			}
		}
		if(Card(Right, 90, "Fake Aim"))
		{
			const char *apModes[] = {"Robot aim", "Spinbot", "Random", "Smooth"};
			Choice(g_Config.m_KrxTasFakeAimMode, "", apModes, 4, 9);
			Body.HSplitTop(5, nullptr, &Body);
			if(Action("Add fake aim", 11))
				GameClient()->m_Tas.AddFakeAim();
		}
		if(Card(Right, 105, "Analyzer"))
		{
			Slider(g_Config.m_KrxTasAnalyzerSpeed, "Reference hook speed (x100)", 0, 8100);
			if(Action("Analyze replay", 12))
				g_Config.m_KrxTasAnalyzerAnalyze = 1;
		}
		if(Card(Right, (g_Config.m_KrxTasDrawPath ? 175 : 105) + (g_Config.m_KrxTasDrawPredictionPath ? 120 : 0), "Visuals"))
		{
			Check(g_Config.m_KrxTasDrawStartEndPos, "Draw start/end pos", false, "Draws the start and the end position of the replay");
			Check(g_Config.m_KrxTasDrawPath, "Draw replay path", false, "Draw path during replay");
			if(g_Config.m_KrxTasDrawPath)
			{
				const float OriginalX = Body.x, OriginalWidth = Body.w;
				Body.VSplitLeft(20, nullptr, &Body);
				Check(g_Config.m_KrxTasDrawPathSegmented, "Segmented", false, "Draw segmented path during replay");
				const char *apModes[] = {"Full line", "Dotted"};
				Choice(g_Config.m_KrxTasDrawPathMode, "", apModes, 2, 10);
				Body.HSplitTop(5, nullptr, &Body);
				Color(g_Config.m_KrxTasDrawPathColor, "Color", 21, 0x55ff80);
				Body.x = OriginalX;
				Body.w = OriginalWidth;
			}
			Check(g_Config.m_KrxTasDrawPredictionPath, "Draw prediction path", false, "Draws the prediction path of your moves when recording a TAS");
			if(g_Config.m_KrxTasDrawPredictionPath)
			{
				Body.VSplitLeft(20, nullptr, &Body);
				const char *apModes[] = {"Full line", "Dotted"};
				Choice(g_Config.m_KrxTasDrawPredictionPathMode, "", apModes, 2, 11);
				Body.HSplitTop(5, nullptr, &Body);
				Color(g_Config.m_KrxTasDrawPredictionPathColor, "Local color", 22, 0x55ff80);
				Body.HSplitTop(5, nullptr, &Body);
				Color(g_Config.m_KrxTasDrawPredictionPathColorFrozen, "Frozen color", 23, 0x1ff80);
				Body.HSplitTop(5, nullptr, &Body);
				Color(g_Config.m_KrxTasDrawPredictionPathColorOthers, "Others color", 24, 0x80ff80);
			}
		}
	}
	else if(g_Config.m_KrxMenuTab == 5)
	{
		if(Card(Left, 290.0f, "Hotkeys"))
		{
			Key(0, "Balance Bot:", "+toggle krx_balancebot 1 0");
			Key(1, "Emote Spam:", "+toggle krx_emotespam 1 0");
			Key(2, "Hook Spam:", "+toggle krx_hookspam 1 0");
			Key(3, "Super DynCam:", "+toggle krx_superdyncam 1 0");
			Key(4, "Pixel Walk:", "toggle krx_pixelwalk 1 0");
			Key(5, "Hook Nearest Collision:", "+toggle krx_hooknearestcollisionautohook 1 0");
			Key(6, "Quick Stop:", "toggle krx_quickstop 1 0");
			Key(7, "Hook Ride (risky):", "toggle krx_hookride 1 0");
			Key(8, "Avoid Freeze:", "toggle krx_avoidfreeze 1 0");
			Key(9, "Auto Edge:", "+toggle krx_autoedge 1 0", "toggle krx_autoedge 1 0");
			Body.HSplitTop(2.0f, nullptr, &Body);
			Key(10, "Laser Unfreeze Bot:", "toggle krx_unfreezebot 1 0");
		}
		if(Card(Right, g_Config.m_KrxPredictionAdvancedSettings ? 225.0f : 125.0f, "Settings"))
		{
			Check(g_Config.m_KrxPredictionTeleports, "Teleport prediction", false, "Enables global teleport support, when disabled teles are treated like freeze");
			Check(g_Config.m_KrxPredictionAdvancedSettings, "Advanced settings", false, "Shows advanced prediction settings");
			if(g_Config.m_KrxPredictionAdvancedSettings)
			{
				Check(g_Config.m_KrxPredictionDeathTile, "Death tile prediction", false, "Enables global death tile support, when disabled death tile isn't predicted by client");
				Check(g_Config.m_KrxPredictionHeart, "Heart tile prediction", false, "Enables global heart tile support, disable when heart tile doesn't freeze");
				Check(g_Config.m_KrxPredictionMoveRestriction, "Move restrictions prediction", false, "Recommended for speed: OFF | When off removes predicting the stopper tiles");
				Check(g_Config.m_KrxPredictionPlayers, "Player loop", false, "Recommended for speed: OFF | When off removes checking player collisions");
				Slider(g_Config.m_ClPredictionMargin, "Prediction margin", 20, 300, false, "Minimum 20 margin is required for bots to work properly");
			}
			Slider(g_Config.m_KrxBalanceBotOffset, "Balance Bot Offset", 1, 10);
			Slider(g_Config.m_KrxHookNearestFov, "Hook Nearest Fov", 1, 360);
		}
		static int s_InitialRpcId = g_Config.m_KrxDiscordRpcId;
		const bool RpcChanged = s_InitialRpcId != g_Config.m_KrxDiscordRpcId;
		const float RpcHeight = g_Config.m_KrxDiscordRpc ? (RpcChanged ? 110.0f : 90.0f) + (g_Config.m_KrxDiscordRpcId == 3 ? 25.0f : 0.0f) : 65.0f;
		if(Card(Right, RpcHeight, "Discord RPC"))
		{
			Check(g_Config.m_KrxDiscordRpc, "Enable", false, "Enables Discord RPC");
			if(g_Config.m_KrxDiscordRpc)
			{
				Body.HSplitTop(5.0f, nullptr, &Body);
				const char *apRpcIds[] = {"KRX", "DDNet", "TClient", "Custom"};
				Choice(g_Config.m_KrxDiscordRpcId, "", apRpcIds, 4, 6);
				if(g_Config.m_KrxDiscordRpcId == 3)
				{
					Body.HSplitTop(5.0f, nullptr, &Body);
					Text(g_Config.m_KrxDiscordRpcCustom, sizeof(g_Config.m_KrxDiscordRpcCustom), "", 2, "Enter Custom RPC ID");
				}
				if(s_InitialRpcId != g_Config.m_KrxDiscordRpcId)
				{
					Body.HSplitTop(5.0f, nullptr, &Body);
					Body.HSplitTop(20.0f, &Row, &Body);
					CUIRect Restart;
					Row.VSplitRight(100.0f, &Row, &Restart);
					Ui()->DoLabel(&Row, Localize("You must restart the game for all settings to take effect."), 14.0f, TEXTALIGN_ML);
					static CButtonContainer s_RestartRpc;
					if(DoButton_Menu(&s_RestartRpc, Localize("Restart"), 0, &Restart))
					{
						if(Client()->State() == IClient::STATE_ONLINE || GameClient()->Editor()->HasUnsavedData())
							m_Popup = POPUP_RESTART;
						else
							Client()->Restart();
					}
				}
			}
		}
		// Explicit OpenKRX additions remain separate from recovered KRX cards.
		if(Card(Right, 170.0f, "Benchmarks"))
		{
			Slider(g_Config.m_KrxPredBenchmarkThreads, "Prediction threads", 1, 512);
			Slider(g_Config.m_KrxPredBenchmarkRuns, "Prediction runs", 1, 20);
			static CButtonContainer s_PredictionBenchmark, s_FlowBenchmark;
			Body.HSplitTop(20, &Row, &Body);
			if(DoButton_Menu(&s_PredictionBenchmark, Localize("Benchmark prediction"), 0, &Row))
				g_Config.m_KrxPredBenchmark = 1;
			Slider(g_Config.m_KrxFlowFieldBenchmarkRuns, "Flow field runs", 1, 100);
			Body.HSplitTop(20, &Row, &Body);
			if(DoButton_Menu(&s_FlowBenchmark, Localize("Benchmark flow field"), 0, &Row))
				g_Config.m_KrxFlowFieldBenchmark = 1;
			Body.HSplitTop(20, &Row, &Body);
			Ui()->DoLabel(&Row, Localize("Results appear in the F1 console."), 12, TEXTALIGN_ML);
		}
		if(Card(Left, 100.0f, "OpenKRX"))
		{
			Check(g_Config.m_RocketPreserveJump, "Keep manual jump with Rocket Save");
			Check(g_Config.m_RocketRestoreWeapon, "Restore weapon after Rocket Save");
			Check(g_Config.m_AvoidDebug, "Record Avoid decisions");
		}
	}
	Scroll.End();
}
