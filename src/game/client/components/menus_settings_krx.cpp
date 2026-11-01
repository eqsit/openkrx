/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com. */
#include "menus.h"

#include <base/str.h>

#include <engine/shared/config.h>

#include <game/client/components/key_binder.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
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
	const auto Card = [&](CUIRect &Column, float Height, const char *pTitle) {
		Column.HSplitTop(10.0f, nullptr, &Column);
		CUIRect Panel, Header;
		Column.HSplitTop(Height, &Panel, &Column);
		if(!Scroll.AddRect(Panel))
			return false;
		Panel.Draw(ColorRGBA(1, 1, 1, 0.25f), IGraphics::CORNER_ALL, 10.0f);
		Panel.VMargin(10.0f, &Body);
		Body.HSplitTop(5.0f, nullptr, &Body);
		Body.HSplitTop(30.0f, &Header, &Body);
		Ui()->DoLabel(&Header, Localize(pTitle), 20.0f, TEXTALIGN_ML);
		return true;
	};
	const auto Check = [&](int &Value, const char *pText, bool Indent = false) {
		Body.HSplitTop(20.0f, &Row, &Body);
		if(Indent)
			Row.VSplitLeft(20.0f, nullptr, &Row);
		if(DoButton_CheckBox(&Value, Localize(pText), Value, &Row))
			Value ^= 1;
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
	const auto Slider = [&](int &Value, const char *pText, int Min, int Max, bool Indent = false) {
		Body.HSplitTop(20.0f, &Row, &Body);
		if(Indent)
			Row.VSplitLeft(20.0f, nullptr, &Row);
		Ui()->DoScrollbarOption(&Value, &Value, &Row, Localize(pText), Min, Max,
			&CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);
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
		Row.VSplitLeft(80.0f, &Label, &Row);
		Ui()->DoLabel(&Label, Localize(pText), 13.0f, TEXTALIGN_ML);
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
			Check(g_Config.m_KrxAimbot, "Enab");
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
				Check(*Weapon.m_pEnable, "Enab");
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
		if(Card(Left, 225.0f + 20.0f * (g_Config.m_KrxQuickStop + g_Config.m_KrxAutoJumpSave), "Bots"))
		{
			Check(g_Config.m_KrxMoonwalk, "Moonwalk");
			Check(g_Config.m_KrxAutoFire, "Auto Fire");
			Check(g_Config.m_KrxAutoRehook, "Auto Rehook");
			Check(g_Config.m_KrxAutoJumpSave, "Auto jump save");
			if(g_Config.m_KrxAutoJumpSave)
			{
				const char *apModes[] = {"Simple", "Advanced - No Velocity", "Advanced - Velocity"};
				Choice(g_Config.m_KrxAutoJumpSaveMode, "Mode", apModes, 3, 0, true);
			}
			Check(g_Config.m_KrxGrenadeSaver, "Auto grenade save");
			Check(g_Config.m_KrxQuickStop, "Quick Stop");
			if(g_Config.m_KrxQuickStop)
				Check(g_Config.m_KrxQuickStopGrounded, "Ground Only", true);
			Check(g_Config.m_KrxDummyFly, "Dummy Fly");
			Check(g_Config.m_KrxJetRide, "Jet Ride");
			Check(g_Config.m_KrxAutoAled, "Auto Aled");
		}
		if(Card(Left, 85.0f, "Protections"))
		{
			Check(g_Config.m_KrxRandomTimeoutSeed, "Random Timeout Seed");
			Later("Version Spoofer");
		}
		if(Card(Left, 65.0f, "Mod Detector"))
			Later("Enable");
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
				Choice(g_Config.m_KrxFakeAimMode, "Mode", apModes, 6, 1);
				if(g_Config.m_KrxFakeAimMode == 2)
					Slider(g_Config.m_KrxFakeAimSpeed, "Speed", -50, 50);
			}
		}
		if(Card(Right, 205.0f + 20.0f * (g_Config.m_KrxAutoTeam + g_Config.m_KrxAntiAfk), "Other"))
		{
			Later("Change name on finish");
			Check(g_Config.m_KrxAutoTeam, "Auto team");
			if(g_Config.m_KrxAutoTeam)
				Slider(g_Config.m_KrxAutoTeamPreferred, "Team", 1, 63, true);
			Check(g_Config.m_KrxAntiAfk, "Anti afk");
			if(g_Config.m_KrxAntiAfk)
				Slider(g_Config.m_KrxAntiAfkSeconds, "Seconds", 1, 300, true);
			Check(g_Config.m_KrxKillOnFreeze, "Kill on freeze");
			Later("Fast input");
			Later("Fast input others");
			Check(g_Config.m_KrxHideBubble, "Hide Chat Bubble");
			Later("Auto Verify");
		}
		if(Card(Right, 145.0f, "Troll"))
		{
			Check(g_Config.m_KrxEmoteSpam, "Emote Spam");
			Later("Killsay/Deathsay");
			Later("Fancy Chat Font");
			Later("Mass Mention Spam");
			Later("Chat Repeater");
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
				Slider(g_Config.m_KrxIdStealerSpeed, "Stealer Speed", 5, 60);
			}
		}
		if(Card(Right, 65.0f, "Ghost Move"))
			Later("Enable");
		if(Card(Right, 125.0f, "Auto Vote Kick"))
		{
			Later("Enable");
			Later("Mode: Random Target");
			Later("Kick Reason:");
		}
	}
	else if(g_Config.m_KrxMenuTab == 2)
	{
		if(Card(Left, g_Config.m_KrxFeaturesHud ? 260.0f : 105.0f, "HUD"))
		{
			Check(g_Config.m_KrxFeaturesHud, "Features HUD");
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
			Later("Notifications");
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
		if(Card(Left, 65.0f, "Tile Outlines"))
			Later("Enable");
		if(Card(Left, 210.0f, "KoG"))
		{
			Later("Show Frozen HUD");
			Later("Use Skins Instead of Ninja Tees");
			Later("Only Show After Joining a Team");
			Later("Max Rows");
			Later("Tee Size");
			Later("Tees Right Alive Text");
			Later("Count Frozen Tees");
			Later("Show Cursor in Free Spectate");
		}
		if(Card(Right, g_Config.m_KrxDrawAimLines ? 185.0f : 65.0f, "Aim Lines"))
		{
			Check(g_Config.m_KrxDrawAimLines, "Draw Aim Lines");
			if(g_Config.m_KrxDrawAimLines)
			{
				Check(g_Config.m_KrxDrawAimLinesSelfOnly, "Self Only");
				Check(g_Config.m_KrxDrawAimLinesShotgun, "Shotgun");
				Check(g_Config.m_KrxDrawAimLinesGun, "Gun");
				Check(g_Config.m_KrxDrawAimLinesGrenade, "Grenade");
				Check(g_Config.m_KrxDrawAimLinesLaser, "Laser");
				Slider(g_Config.m_KrxDrawAimLinesAlpha, "Alpha", 0, 100);
			}
		}
		if(Card(Right, g_Config.m_KrxDrawBulletLines ? 165.0f : 65.0f, "Bullet Lines"))
		{
			Check(g_Config.m_KrxDrawBulletLines, "Draw Bullet Lines");
			if(g_Config.m_KrxDrawBulletLines)
			{
				Check(g_Config.m_KrxDrawBulletLinesSelfOnly, "Self Only");
				Check(g_Config.m_KrxDrawBulletLinesGun, "Gun");
				Check(g_Config.m_KrxDrawBulletLinesGrenade, "Grenade");
				Check(g_Config.m_KrxDrawBulletLinesShotgun, "Shotgun");
				Slider(g_Config.m_KrxDrawBulletLinesAlpha, "Alpha", 0, 100);
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
		if(Card(Right, 65.0f, "Trajectory"))
			Later("Enable");
		if(Card(Right, g_Config.m_KrxTrail ? 130.0f : 65.0f, "Trail"))
		{
			Check(g_Config.m_KrxTrail, "Enable");
			if(g_Config.m_KrxTrail)
			{
				const char *apModes[] = {"Smoke", "Bullet", "Shine", "Sparkle"};
				Choice(g_Config.m_KrxTrailMode, "Mode", apModes, 4, 2);
				Color(g_Config.m_KrxTrailColor, "Color", 12, 0xb3ff18);
				Check(g_Config.m_KrxTrailColorRainbow, "Rainbow");
			}
		}
		if(Card(Right, g_Config.m_KrxSmoothCam ? 105.0f : 85.0f, "Cam"))
		{
			Check(g_Config.m_KrxSmoothCam, "Smooth cam");
			if(g_Config.m_KrxSmoothCam)
				Slider(g_Config.m_KrxSmoothCamSmoothness, "Smoothness", 1, 100, true);
			Check(g_Config.m_KrxSuperDyncam, "Super dyncam");
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
			Later("Auto Edge");
			Body.HSplitTop(2.0f, nullptr, &Body);
			Key(10, "Laser Unfreeze Bot:", "toggle krx_unfreezebot 1 0");
		}
		if(Card(Right, 125.0f, "Settings"))
		{
			Later("Teleport prediction");
			Later("Advanced settings");
			Slider(g_Config.m_KrxBalanceBotOffset, "Balance Bot Offset", 1, 10);
			Slider(g_Config.m_KrxHookNearestFov, "Hook Nearest Fov", 1, 360);
		}
		if(Card(Right, 85.0f, "Discord RPC"))
			Later("Enable");
		// Explicit OpenKRX additions remain separate from recovered KRX cards.
		if(Card(Left, 125.0f, "OpenKRX"))
		{
			Check(g_Config.m_RocketPreserveJump, "Keep manual jump with Rocket Save");
			Check(g_Config.m_AvoidEmergencyHook, "Only necessary automatic hook");
			Check(g_Config.m_AvoidKeepDirection, "Keep direction when safe");
			Check(g_Config.m_AvoidDebug, "Record Avoid decisions");
		}
	}
	Scroll.End();
}
