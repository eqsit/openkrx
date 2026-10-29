/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com. */
#include "menus.h"

#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/shared/config.h>

#include <game/client/components/key_binder.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

// Recovered KRX renderer 0x14025ac10. Exact rectangles and conditional heights
// are recorded in research/codex/UI-GEOMETRY-AUDIT.md, with assembly evidence.
void CMenus::RenderSettingsAvoid(CUIRect MainView)
{
	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 120.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);
	CUIRect Left, Right;
	MainView.VSplitMid(&Left, &Right);
	Right.VMargin(10.0f, &Right);
	const auto Card = [&](CUIRect &Column, float Height, const char *pTitle, CUIRect &Body, CUIRect *pHeader = nullptr) {
		Column.HSplitTop(10.0f, nullptr, &Column);
		CUIRect Panel, Header;
		Column.HSplitTop(Height, &Panel, &Column);
		if(!s_ScrollRegion.AddRect(Panel))
			return false;
		Panel.Draw(ColorRGBA(1, 1, 1, 0.25f), IGraphics::CORNER_ALL, 10.0f);
		Panel.VMargin(10.0f, &Body);
		Body.HSplitTop(5.0f, nullptr, &Body);
		Body.HSplitTop(30.0f, &Header, &Body);
		if(pHeader)
			*pHeader = Header;
		else
			Ui()->DoLabel(&Header, Localize(pTitle), 20.0f, TEXTALIGN_ML);
		return true;
	};
	const auto Check = [&](CUIRect &Body, int &Value, const char *pText, const char *pTip = nullptr, bool Indent = false) {
		CUIRect Row;
		Body.HSplitTop(20.0f, &Row, &Body);
		if(Indent)
			Row.VSplitLeft(20.0f, nullptr, &Row);
		if(DoButton_CheckBox(&Value, Localize(pText), Value, &Row))
			Value ^= 1;
		if(pTip)
			Ui()->DoToolTip(&Value, &Row, Localize(pTip));
	};
	const auto Slider = [&](CUIRect &Body, int &Value, const char *pText, int Min, int Max, const char *pTip = nullptr, bool Indent = false) {
		CUIRect Row;
		Body.HSplitTop(20.0f, &Row, &Body);
		if(Indent)
			Row.VSplitLeft(20.0f, nullptr, &Row);
		// KRX uses flag 2 (NOCLAMPVALUE). Opening the page must preserve ticks=0.
		Ui()->DoScrollbarOption(&Value, &Value, &Row, Localize(pText), Min, Max,
			&CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE);
		if(pTip)
			Ui()->DoToolTip(&Value, &Row, Localize(pTip));
	};
	CUIRect Body, Header, Row;
	if(Card(Left, g_Config.m_AvoidAfkProtection ? 155.0f : 135.0f, "Main", Body, &Header))
	{
		CUIRect Help;
		Header.VSplitRight(40.0f, &Header, &Help);
		Ui()->DoLabel(&Header, Localize("Main"), 20.0f, TEXTALIGN_ML);
		static CButtonContainer s_HelpButton;
		if(Ui()->DoButton_FontIcon(&s_HelpButton, FontIcon::BOOK, 0, &Help, BUTTONFLAG_LEFT))
			Client()->ViewLink("https://krxclient.gitbook.io/docs/ultimate-features/avoid/blatant");
		Body.HSplitTop(5.0f, nullptr, &Body);
		Body.HSplitTop(20.0f, &Row, &Body);
		static CButtonContainer s_EnableButton;
		const ColorRGBA Color = g_Config.m_AvoidFreeze ? ColorRGBA(1, 0, 0, 0.5f) : ColorRGBA(1, 1, 1, 0.5f);
		if(DoButton_Menu(&s_EnableButton, g_Config.m_AvoidFreeze ? Localize("Stop") : Localize("Enable"), 0, &Row,
			   BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, Color))
		{
			g_Config.m_AvoidFreeze ^= 1;
			if(g_Config.m_AvoidFreeze)
				g_Config.m_AvoidAgentType = 2;
		}
		Ui()->DoToolTip(&s_EnableButton, &Row, Localize("Enable blatant gores bot"));
		Body.HSplitTop(5.0f, nullptr, &Body);
		Check(Body, g_Config.m_AvoidPlayerPrediction, "Player Prediction", "Different from settings player prediction - this completely removes all characters");
		Check(Body, g_Config.m_AvoidNsif, "NSIF", "Uses last best known input if No Safe Input is Found");
		Check(Body, g_Config.m_AvoidAfkProtection, "Afk protection", "Disables your bot in x seconds. Doesn't work if you are gonna freeze before x seconds passes.");
		if(g_Config.m_AvoidAfkProtection)
			Slider(Body, g_Config.m_AvoidAfkTime, "Seconds", 1, 300, nullptr, true);
	}
	if(Card(Left, g_Config.m_AvoidTrackPoints ? 165.0f : 145.0f, "Settings", Body))
	{
		Slider(Body, g_Config.m_AvoidBlatantCheckTicks, "Check ticks", 1, 50, "How many inputs to check");
		Slider(Body, g_Config.m_AvoidKickInTicks, "Kick in ticks", 1, 50, "If the bot lives for this many ticks it won't 'kick in'.");
		Check(Body, g_Config.m_AvoidBlatantHook, "Hook", "Enables hook assistance.");
		Check(Body, g_Config.m_AvoidBlatantDirection, "Direction", "Enables direction assistance.");
		Check(Body, g_Config.m_AvoidTrackPoints, "Enable track point", "Enables track point feature that tracks the last hookable direction.");
		if(g_Config.m_AvoidTrackPoints)
			Check(Body, g_Config.m_AvoidSafeAimTracking, "Safe tracking", "Does a check first to see if tracking the aim dir would result in a worse result than not tracking.", true);
	}
	if(Card(Left, 90.0f, "Hotkeys", Body))
	{
		static const char *const apLabels[] = {"Auto Drag", "Track Point"};
		static const char *const apCommands[] = {"+toggle krx_avoid_tile_auto_drag 1 0", "toggle krx_avoid_tile_track_points 1 0"};
		static CButtonContainer s_aKeyButtons[2], s_aClearButtons[2];
		for(int Index = 0; Index < 2; ++Index)
		{
			CBindSlot Current = EMPTY_BIND_SLOT;
			for(int Modifier = 0; Modifier < KeyModifier::COMBINATION_COUNT; ++Modifier)
				for(int Key = 0; Key < KEY_LAST; ++Key)
					if(str_comp(GameClient()->m_Binds.Get(Key, Modifier), apCommands[Index]) == 0)
						Current = CBindSlot(Key, Modifier);
			Body.HSplitTop(20.0f, &Row, &Body);
			CUIRect Label;
			Row.VSplitLeft(135.0f, &Label, &Row);
			char aLabel[64];
			str_format(aLabel, sizeof(aLabel), "%s:", Localize(apLabels[Index]));
			Ui()->DoLabel(&Label, aLabel, 13.0f, TEXTALIGN_ML);
			const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&s_aKeyButtons[Index], &s_aClearButtons[Index], &Row, Current, false, false);
			if(!Result.m_Aborted && Result.m_Bind != Current)
			{
				if(Current != EMPTY_BIND_SLOT)
					GameClient()->m_Binds.Bind(Current.m_Key, "", false, Current.m_ModifierMask);
				if(Result.m_Bind != EMPTY_BIND_SLOT)
					GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, apCommands[Index], false, Result.m_Bind.m_ModifierMask);
			}
			Body.HSplitTop(2.0f, nullptr, &Body);
		}
	}
	if(Card(Right, g_Config.m_AvoidBlatantUnfreezeTile ? 125.0f : 105.0f, "Tiles", Body))
	{
		Check(Body, g_Config.m_AvoidBlatantTeles, "Teles");
		Check(Body, g_Config.m_AvoidBlatantDeath, "Death");
		Check(Body, g_Config.m_AvoidBlatantUnfreezeTile, "Unfreeze", "Amount of ticks to simulate to find an unfreeze tile");
		if(g_Config.m_AvoidBlatantUnfreezeTile)
			Slider(Body, g_Config.m_AvoidBlatantUnfreezeTicks, "Ticks", 1, 50, "How many ticks to simulate forward to check for unfreeze", true);
	}
	if(Card(Right, g_Config.m_AvoidBlatantAimbot ? 145.0f : 65.0f, "Aimb", Body))
	{
		Check(Body, g_Config.m_AvoidBlatantAimbot, "Enab");
		if(g_Config.m_AvoidBlatantAimbot)
		{
			Slider(Body, g_Config.m_AvoidAimbotSegments, "Segments", 1, 64, "Amount of aim dirs that will be considered to find the best aim dir for each type of aimbot");
			Slider(Body, g_Config.m_AvoidBlatantFov, "FOV", 10, 360);
			Check(Body, g_Config.m_AvoidAutoAimAimbot, "Auto aim", "Considers longest lived aim direction");
			Check(Body, g_Config.m_AvoidAimAssistAimbot, "Aim assist", "Considers longest lived aim direction closest to mouse");
		}
	}
	if(Card(Right, 105.0f, "Visuals", Body))
	{
		Check(Body, g_Config.m_AvoidDrawTrackPoint, "Track point", "Shows visuals for trackpoint");
		Check(Body, g_Config.m_AvoidDrawAimbot, "Aimb", "Shows visuals for aimbot");
		Check(Body, g_Config.m_AvoidDrawPath, "Path", "Shows visuals for path");
	}
	s_ScrollRegion.End();
}
