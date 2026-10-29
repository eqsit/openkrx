/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com. */
#include "menus.h"

#include <engine/shared/config.h>

#include <game/client/ui.h>
#include <game/localization.h>

void CMenus::RenderSettingsKrx(CUIRect MainView)
{
	static int s_Section = 0;
	static CButtonContainer s_aSectionButtons[2];
	const char *const apSections[] = {Localize("Avoid"), Localize("Misc")};
	CUIRect Tabs, Button;
	MainView.HSplitTop(26.0f, &Tabs, &MainView);
	MainView.HSplitTop(10.0f, nullptr, &MainView);
	for(int Index = 0; Index < 2; ++Index)
	{
		Tabs.VSplitLeft(100.0f, &Button, &Tabs);
		Tabs.VSplitLeft(5.0f, nullptr, &Tabs);
		if(DoButton_MenuTab(&s_aSectionButtons[Index], apSections[Index], s_Section == Index, &Button, IGraphics::CORNER_ALL))
			s_Section = Index;
	}
	if(s_Section == 0)
	{
		RenderSettingsAvoid(MainView);
		return;
	}

	CUIRect Panel, Header;
	MainView.HSplitTop(10.0f, nullptr, &MainView);
	MainView.VSplitMid(&Panel, nullptr);
	Panel.h = 125.0f;
	Panel.Draw(ColorRGBA(1, 1, 1, 0.25f), IGraphics::CORNER_ALL, 10.0f);
	Panel.VMargin(10.0f, &Panel);
	Panel.HSplitTop(5.0f, nullptr, &Panel);
	Panel.HSplitTop(30.0f, &Header, &Panel);
	Ui()->DoLabel(&Header, Localize("Misc"), 20.0f, TEXTALIGN_ML);
	Panel.HSplitTop(20.0f, &Button, &Panel);
	if(DoButton_CheckBox(&g_Config.m_KrxGrenadeSaver, Localize("Auto grenade save"), g_Config.m_KrxGrenadeSaver, &Button))
		g_Config.m_KrxGrenadeSaver ^= 1;
	Ui()->DoToolTip(&g_Config.m_KrxGrenadeSaver, &Button, Localize("Automatically chooses a grenade shot to avoid freeze."));
	Panel.HSplitTop(20.0f, &Button, &Panel);
	if(DoButton_CheckBox(&g_Config.m_RocketPreserveJump, Localize("Keep manual jump with Rocket Save"), g_Config.m_RocketPreserveJump, &Button))
		g_Config.m_RocketPreserveJump ^= 1;
	Ui()->DoToolTip(&g_Config.m_RocketPreserveJump, &Button, Localize("Keeps your jump or double jump when a grenade can rescue it. Does not jump automatically."));
	Panel.HSplitTop(20.0f, &Button, &Panel);
	if(DoButton_CheckBox(&g_Config.m_AvoidEmergencyHook, Localize("Only necessary automatic hook"), g_Config.m_AvoidEmergencyHook, &Button))
		g_Config.m_AvoidEmergencyHook ^= 1;
	Ui()->DoToolTip(&g_Config.m_AvoidEmergencyHook, &Button, Localize("Releases or skips an automatic hook when the same movement without it is safe. Manual hook input is not filtered."));
	Panel.HSplitTop(20.0f, &Button, &Panel);
	if(DoButton_CheckBox(&g_Config.m_AvoidKeepDirection, Localize("Keep direction when safe"), g_Config.m_AvoidKeepDirection, &Button))
		g_Config.m_AvoidKeepDirection ^= 1;
	Ui()->DoToolTip(&g_Config.m_AvoidKeepDirection, &Button, Localize("Avoid keeps your direction if the same rescue input is safe without steering sideways."));
}
