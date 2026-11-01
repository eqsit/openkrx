/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com. */
#include "menus.h"

#include <base/str.h>

#include <engine/client.h>
#include <engine/font_icons.h>
#include <engine/shared/config.h>

#include <game/client/components/key_binder.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/editor/editor.h>
#include <game/localization.h>

#include <algorithm>

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
		const char *apAgents[] = {"Basic (Later)", "Legit (Later)", "Blatant", "Fent (Later)"};
		const int Agent = Ui()->DoDropDown(&Button, g_Config.m_AvoidAgentType, apAgents, 4, s_AgentDropDown);
		if(Agent != g_Config.m_AvoidAgentType)
		{
			g_Config.m_AvoidAgentType = Agent;
			if(Agent != 2)
				g_Config.m_AvoidFreeze = 0;
		}
		if(g_Config.m_AvoidAgentType == 2)
			RenderSettingsAvoid(MainView);
		else
		{
			MainView.HSplitTop(10.0f, nullptr, &MainView);
			MainView.HSplitTop(40.0f, &Button, &MainView);
			Ui()->DoLabel(&Button, Localize("This agent will be added later. Blatant is available."), 14.0f, TEXTALIGN_ML);
		}
		return;
	}

	static CScrollRegion s_aScrollRegions[6];
	CScrollRegion &Scroll = s_aScrollRegions[g_Config.m_KrxMenuTab];
	CScrollRegionParams Params;
	Params.m_ScrollUnit = 120.0f;
	Scroll.Begin(&MainView, &Params);
	CUIRect Left, Right, Body, Row;
	MainView.VSplitMid(&Left, &Right);
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
		Ui()->DoLabel(&Header, Localize(pTitle), 20.0f, TEXTALIGN_ML);
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
	const auto Later = [&](const char *pText) {
		Body.HSplitTop(20.0f, &Row, &Body);
		CUIRect Badge;
		Row.VSplitRight(40.0f, &Row, &Badge);
		TextRender()->TextColor(ColorRGBA(1, 1, 1, 0.45f));
		Ui()->DoLabel(&Row, Localize(pText), 13.0f, TEXTALIGN_ML);
		Ui()->DoLabel(&Badge, Localize("Later"), 10.0f, TEXTALIGN_MR);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
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
		static CLineInput s_aInputs[6];
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
		DoLine_ColorPicker(&s_aColorReset[Index], 25.0f, 14.0f, 0.0f, &Body, Localize(pText), &Value,
			color_cast<ColorRGBA>(ColorHSLA(Default)), false);
	};
	const auto Choice = [&](int &Value, const char *pText, const char **ppNames, int Count, int Index, bool Indent = false) {
		static CUi::SDropDownState s_aDropDowns[8];
		static CScrollRegion s_aDropDownScrolls[8];
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
	const auto Key = [&](int Index, const char *pLabel, const char *pCommand) {
		static CButtonContainer s_aKeys[16], s_aClear[16];
		CBindSlot Current = EMPTY_BIND_SLOT;
		for(int Modifier = 0; Modifier < KeyModifier::COMBINATION_COUNT; ++Modifier)
			for(int KeyIndex = 0; KeyIndex < KEY_LAST; ++KeyIndex)
				if(str_comp(GameClient()->m_Binds.Get(KeyIndex, Modifier), pCommand) == 0)
					Current = CBindSlot(KeyIndex, Modifier);
		Body.HSplitTop(20.0f, &Row, &Body);
		CUIRect Label;
		Row.VSplitLeft(135.0f, &Label, &Row);
		Ui()->DoLabel(&Label, Localize(pLabel), 13.0f, TEXTALIGN_ML);
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
		const float MainHeight = (g_Config.m_KrxAimbotTargetGlow ? 200.0f : 145.0f) + (g_Config.m_KrxAimbotAdvancedSettings ? 40.0f : 0.0f);
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
			if(g_Config.m_KrxAimbotAdvancedSettings)
			{
				Slider(g_Config.m_KrxAimbotMinShootLength, "Min Shoot Delay", 1, 10, true);
				Slider(g_Config.m_KrxAimbotMaxShootLength, "Max Shoot Delay", 1, 10, true);
			}
		}
		if(Card(Left, 90.0f, "Hotkeys"))
		{
			Key(11, "Auto Hook:", "+toggle krx_aimbot_autohook_key 1 0");
			Key(12, "Auto Shoot:", "+toggle krx_aimbot_autoshoot_key 1 0");
		}
		struct CWeaponSettings
		{
			const char *m_pName;
			int *m_pEnable, *m_pFov, *m_pPriority, *m_pSilent, *m_pIgnoreFriends;
		};
		const CWeaponSettings aWeapons[] = {
			{"Hook", &g_Config.m_KrxAimbotWeaponHook, &g_Config.m_KrxAimbotFovHook, &g_Config.m_KrxAimbotTargetPriorityHook, &g_Config.m_KrxAimbotSilentHook, &g_Config.m_KrxAimbotIgnoreFriendsHook},
			{"Hammer", &g_Config.m_KrxAimbotWeaponHammer, &g_Config.m_KrxAimbotFovHammer, &g_Config.m_KrxAimbotTargetPriorityHammer, &g_Config.m_KrxAimbotSilentHammer, &g_Config.m_KrxAimbotIgnoreFriendsHammer},
			{"Gun", &g_Config.m_KrxAimbotWeaponGun, &g_Config.m_KrxAimbotFovGun, &g_Config.m_KrxAimbotTargetPriorityGun, &g_Config.m_KrxAimbotSilentGun, &g_Config.m_KrxAimbotIgnoreFriendsGun},
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
		if(Card(Left, 225.0f + 20.0f * g_Config.m_KrxQuickStop, "Bots", "https://krxclient.gitbook.io/docs/features/misc"))
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
			Check(g_Config.m_KrxJetRide, "Jet Ride", false, "Works like hook ride bot but with jetpack");
			Check(g_Config.m_KrxAutoAled, "Auto Aled", false, "Auto hammers when an aled is possible");
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
			Check(g_Config.m_KrxHideBubble, "Hide Chat Bubble");
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
		if(Card(Right, g_Config.m_KrxChatRepeater ? 165.0f : 145.0f, "Troll"))
		{
			Check(g_Config.m_KrxEmoteSpam, "Emote Spam");
			Check(g_Config.m_KrxKillsay, "Killsay/Deathsay");
			Check(g_Config.m_KrxFancyChatFont, "Fancy Chat Font");
			Check(g_Config.m_KrxMassMentionSpam, "Mass Mention Spam");
			Check(g_Config.m_KrxChatRepeater, "Chat Repeater");
			if(g_Config.m_KrxChatRepeater)
				FlatSlider(g_Config.m_KrxChatRepeaterLength, "Min Message Length", 1, 20);
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
			Later("TAS HUD");
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
		if(Card(Right, g_Config.m_KrxTrail ? 140.0f : 65.0f, "Trail"))
		{
			Check(g_Config.m_KrxTrail, "Enable");
			if(g_Config.m_KrxTrail)
			{
				const char *apModes[] = {"Smoke trail", "Bullet trail", "Powerup trail", "Sparkle trail"};
				Choice(g_Config.m_KrxTrailMode, "", apModes, 4, 2);
				Color(g_Config.m_KrxTrailColor, "Color", 12, 0xb3ff18);
				Check(g_Config.m_KrxTrailColorRainbow, "Rainbow");
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
	else if(g_Config.m_KrxMenuTab == 4)
	{
		if(Card(Left, 165.0f, "Main"))
		{
			Later("TPS");
			Later("Start recording TAS");
			Later("Pause TAS");
			Later("Playback replay");
			Later("Clear current replay");
		}
		if(Card(Left, 185.0f, "Settings"))
		{
			Later("Enable sound");
			Later("Enable effects");
			Later("Show real aim");
			Later("Predict players");
			Later("Auto Replay");
			Later("Auto Save Replay");
		}
		if(Card(Left, 130.0f, "Hotkeys"))
		{
			Later("Record");
			Later("Replay");
			Later("Rewind");
			Later("Forward");
		}
		if(Card(Right, 200.0f, "Replay"))
		{
			Later("Selected replay:");
			Later("Load");
			Later("Save");
			Later("Delete");
		}
		if(Card(Right, 85.0f, "Replay Vault"))
			Later("Auto sync");
		if(Card(Right, 145.0f, "Tick Control"))
		{
			Later("Pause");
			Later("Ticks");
			Later("Step");
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
			Key(9, "Auto Edge:", "toggle krx_autoedge 1 0");
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
		if(Card(Left, 100.0f, "OpenKRX"))
		{
			Check(g_Config.m_RocketPreserveJump, "Keep manual jump with Rocket Save");
			Check(g_Config.m_RocketRestoreWeapon, "Restore weapon after Rocket Save");
			Check(g_Config.m_AvoidDebug, "Record Avoid decisions");
		}
	}
	Scroll.End();
}
