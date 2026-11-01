#include "krx_visuals_pre_tas.h"

#include <base/str.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/render.h>
#include <game/collision.h>
#include <game/layers.h>
#include <game/mapitems.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace OpenKrx
{
	bool CPreTasVisuals::ShouldSuspendAvoid()
	{
		// REA: 0x1402cc7a0; grounded helper 0x1402b4540. Preserve strict
		// comparison, timer rearming, and the original two-direction-key gate.
		if(!g_Config.m_AvoidAfkProtection)
			return false;
		bool Expired = false;
		if(g_Config.m_AvoidAfkTime != 0)
		{
			const int64_t Period = (int64_t)g_Config.m_AvoidAfkTime * time_freq();
			if(m_AfkLastActivity < time_get() - Period)
			{
				Expired = true;
				m_AfkLastActivity = time_get() - Period - 1;
			}
		}
		const int Slot = g_Config.m_ClDummy;
		const int Id = GameClient()->m_aLocalIds[Slot];
		bool Grounded = false;
		if(Id >= 0 && Id < LEGACY_MAX_CLIENTS)
		{
			const vec2 Pos = GameClient()->m_aClients[Id].m_RenderPos;
			Grounded = Collision()->CheckPoint(Pos + vec2(14, 19)) ||
				   Collision()->CheckPoint(Pos + vec2(-14, 19)) ||
				   (Collision()->GetMoveRestrictions(Pos + vec2(0, 18), 0.0f) & CANTMOVE_DOWN);
		}
		const auto &Controls = GameClient()->m_Controls;
		const vec2 Mouse = Controls.m_aMousePos[Slot];
		if((Grounded || Expired) && Controls.m_aInputDirectionLeft[Slot] && Controls.m_aInputDirectionRight[Slot] &&
			(m_AfkMouse.x != 0 || m_AfkMouse.y != 0) && m_AfkMouse == Mouse)
			return Expired;
		m_AfkMouse = Mouse;
		m_AfkLastActivity = time_get();
		return false;
	}

	namespace
	{
		bool Playing(CGameClient *pClient)
		{
			return (pClient->m_Controls.m_aInputData[g_Config.m_ClDummy].m_PlayerFlags & PLAYERFLAG_PLAYING) &&
			       pClient->Client()->State() == IClient::STATE_ONLINE;
		}
		bool Spectating(CGameClient *pClient)
		{
			const int Id = pClient->m_aLocalIds[g_Config.m_ClDummy];
			return Id < 0 || Id >= LEGACY_MAX_CLIENTS || pClient->m_aClients[Id].m_Team == TEAM_SPECTATORS ||
			       pClient->m_Snap.m_SpecInfo.m_Active || pClient->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW;
		}
		ColorRGBA PathColor(unsigned Packed)
		{
			ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(Packed, true));
			if(Color.a == 0)
				Color.a = .75f;
			return Color;
		}
	}

	void CPreTasVisuals::PredictTrajectory()
	{
		// REA 0x140309a90, called first in 0x14026d2d0, before all input bots.
		m_vTrajectories.clear();
		if(!g_Config.m_KrxTrajectoryEsp || !Playing(GameClient()) || Spectating(GameClient()))
			return;
		CGameWorld World;
		World.CopyWorldIsolated(&GameClient()->m_PredictedWorld);
		World.m_WorldConfig.m_KrxPrediction = true;
		World.m_WorldConfig.m_KrxAvoidDeath = g_Config.m_AvoidFreeze && g_Config.m_AvoidAgentType == 2 && g_Config.m_AvoidBlatantDeath;
		World.m_WorldConfig.m_KrxAvoidTeles = g_Config.m_AvoidFreeze && g_Config.m_AvoidAgentType == 2 && g_Config.m_AvoidBlatantTeles;
		World.m_WorldConfig.m_KrxExplosionStrengthValid = true;
		World.m_WorldConfig.m_KrxExplosionStrength = GameClient()->m_aTuning[g_Config.m_ClDummy].m_ExplosionStrength;
		const int Own = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
		for(int Id = 0; Id < LEGACY_MAX_CLIENTS; ++Id)
		{
			auto *pCharacter = World.GetCharacterById(Id);
			if(!pCharacter || (g_Config.m_KrxTrajectoryEspSelfOnly && Id != Own) || pCharacter->Core()->m_Vel == vec2(0, 0))
				continue;
			CTrajectory Path;
			Path.m_ClientId = Id;
			Path.m_Input = Id == Own ? GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy] : pCharacter->DiagnosticCurrentInput();
			m_vTrajectories.push_back(std::move(Path));
		}
		for(int Tick = 0; Tick <= g_Config.m_KrxTrajectoryEspTicks; ++Tick)
		{
			for(auto &Path : m_vTrajectories)
				if(auto *pCharacter = World.GetCharacterById(Path.m_ClientId))
					pCharacter->OnDirectInput(&Path.m_Input);
			++World.m_GameTick;
			for(auto &Path : m_vTrajectories)
				if(auto *pCharacter = World.GetCharacterById(Path.m_ClientId))
					pCharacter->OnPredictedInput(&Path.m_Input);
			World.Tick();
			for(auto &Path : m_vTrajectories)
			{
				auto *pCharacter = World.GetCharacterById(Path.m_ClientId);
				if(!pCharacter)
					continue;
				bool Frozen = pCharacter->m_FreezeTime > 0;
				const std::array<int, 6> aFreezeTiles{9, 26, 10, 31, 63, 2};
				const auto vIndices = Collision()->GetMapIndices(pCharacter->m_PrevPos, pCharacter->m_Pos);
				for(int Tile : aFreezeTiles)
				{
					for(int Index : vIndices)
					{
						Frozen |= Collision()->GetTileIndex(Index) == Tile || Collision()->GetFrontTileIndex(Index) == Tile;
						if(Tile == 26)
							Frozen |= Collision()->IsTeleport(Index) != 0;
						else if(Tile == 10)
							Frozen |= Collision()->IsEvilTeleport(Index) != 0;
						else if(Tile == 31)
							Frozen |= Collision()->IsCheckTeleport(Index);
						else if(Tile == 63)
							Frozen |= Collision()->IsCheckEvilTeleport(Index);
					}
					Frozen |= Collision()->GetTile((int)pCharacter->m_Pos.x, (int)pCharacter->m_Pos.y) == Tile;
				}
				Path.m_vPoints.push_back({pCharacter->m_Pos, Frozen});
			}
		}
	}

	void CPreTasVisuals::WorldLine(vec2 From, vec2 To, float Width, ColorRGBA Color)
	{
		// 0x1402b82f0: reject sub-unit segments; effective half-width includes
		// camera zoom and is (.25 * (configured width - 1) + .5).
		const vec2 Delta = To - From;
		if(length(Delta) < 1)
			return;
		Graphics()->TextureClear();
		if(Width <= 0)
		{
			Graphics()->LinesBegin();
			Graphics()->SetColor(Color);
			IGraphics::CLineItem Line(From.x, From.y, To.x, To.y);
			Graphics()->LinesDraw(&Line, 1);
			Graphics()->LinesEnd();
			return;
		}
		const vec2 Perp = vec2(Delta.y, -Delta.x) / length(Delta) * GameClient()->m_Camera.m_Zoom * ((Width - 1) * .25f + .5f);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(Color);
		IGraphics::CFreeformItem Quad(To - Perp, To + Perp, From - Perp, From + Perp);
		Graphics()->QuadsDrawFreeform(&Quad, 1);
		Graphics()->QuadsEnd();
	}

	void CPreTasVisuals::WorldCircle(vec2 Center, float Radius, ColorRGBA Color, float Width, bool Filled)
	{
		vec2 Prev = Center + vec2(std::cos(-.1f), std::sin(-.1f)) * Radius;
		for(float Angle = 0; Angle < 2 * pi; Angle += .1f)
		{
			const vec2 Next = Center + vec2(std::cos(Angle), std::sin(Angle)) * Radius;
			WorldLine(Prev, Next, Width, Color);
			Prev = Next;
		}
		if(Filled)
		{
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Color.a = .5f;
			Graphics()->SetColor(Color);
			Graphics()->DrawCircle(Center.x, Center.y, Radius, 32);
			Graphics()->QuadsEnd();
		}
	}

	void CPreTasVisuals::RenderTrajectory()
	{
		if(!g_Config.m_KrxTrajectoryEsp || !Playing(GameClient()) || Spectating(GameClient()))
			return;
		for(const auto &Path : m_vTrajectories)
		{
			if(Path.m_vPoints.empty())
				continue;
			const bool Local = Path.m_ClientId == GameClient()->m_aLocalIds[0] || Path.m_ClientId == GameClient()->m_aLocalIds[1];
			const ColorRGBA Normal = PathColor(Local ? g_Config.m_KrxTrajectoryEspPathColor : g_Config.m_KrxTrajectoryEspPathColorOthers);
			const ColorRGBA Frozen = PathColor(g_Config.m_KrxTrajectoryEspPathColorFrozen);
			vec2 Previous = Path.m_vPoints.front().m_Position;
			for(size_t Index = 0; Index < Path.m_vPoints.size(); ++Index)
			{
				const auto &Point = Path.m_vPoints[Index];
				ColorRGBA Color = Point.m_Frozen ? Frozen : Normal;
				const float Fade = 1 - .5f * (float)Index / (float)Path.m_vPoints.size();
				Color.r *= Fade;
				Color.g *= Fade;
				Color.b *= Fade;
				if(g_Config.m_KrxTrajectoryEspPathMode == 0)
					WorldLine(Previous, Point.m_Position, g_Config.m_KrxTrajectoryEspPathThickness, Color);
				else
					WorldCircle(Point.m_Position, g_Config.m_KrxTrajectoryEspPathThickness, Color, 1, true);
				Previous = Point.m_Position;
			}
		}
	}

	void CPreTasVisuals::RenderTileOutlines()
	{
		// Original 0x140308db0 only uses the game tile layer and tele layer.
		if(!g_Config.m_ClOutline || !Playing(GameClient()) ||
			(g_Config.m_ClOutlineInEntities && g_Config.m_ClOverlayEntities == 0))
			return;
		const auto *pLayer = Layers()->GameLayer();
		if(!pLayer)
			return;
		const auto *pTiles = static_cast<const CTile *>(Layers()->Map()->GetData(pLayer->m_Data));
		if(!pTiles || Layers()->Map()->GetDataSize(pLayer->m_Data) < (int64_t)pLayer->m_Width * pLayer->m_Height * (int)sizeof(CTile))
			return;
		const int Width = pLayer->m_Width, Height = pLayer->m_Height;
		if(Width <= 0 || Height <= 0)
			return;
		const CScreenRect Screen = Graphics()->GetScreen();
		int X0 = (int)(Screen.m_TopLeft.x / 32) - 1, Y0 = (int)(Screen.m_TopLeft.y / 32) - 1;
		int X1 = (int)(Screen.m_BottomRight.x / 32) + 1, Y1 = (int)(Screen.m_BottomRight.y / 32) + 1;
		// Preserve the original far-zoom bound, including its shared-axis condition.
		if(Graphics()->ScreenWidth() / 12 < X1 - X0 || Graphics()->ScreenHeight() / 12 < Y1 - Y0)
		{
			const int DX = (X1 - Graphics()->ScreenWidth() / 12 - X0) / 2;
			const int DY = (Y1 - Graphics()->ScreenHeight() / 12 - Y0) / 2;
			X0 += DX;
			X1 -= DX;
			Y0 += DY;
			Y1 -= DY;
		}
		const float W = g_Config.m_ClOutlineWidth;
		const auto TileAt = [&](int X, int Y) {
			return pTiles[std::clamp(Y, 0, Height - 1) * Width + std::clamp(X, 0, Width - 1)].m_Index;
		};
		const auto Draw = [&](int X, int Y, const std::array<bool, 8> &aOpen) {
			// Neighbor order TL,T,TR,L,R,BL,B,BR. 0x14030a3d0/0x14030c110
			// emit inside corners first, then horizontal and trimmed vertical strips.
			const auto [TL, T, TR, L, R, BL, B, BR] = aOpen;
			const float PX = X * 32.0f, PY = Y * 32.0f;
			IGraphics::CQuadItem aQuads[8];
			int Count = 0;
			if(TL && !T && !L)
				aQuads[Count++] = IGraphics::CQuadItem(PX, PY, W, W);
			if(TR && !T && !R)
				aQuads[Count++] = IGraphics::CQuadItem(PX + 32 - W, PY, W, W);
			if(BL && !L && !B)
				aQuads[Count++] = IGraphics::CQuadItem(PX, PY + 32 - W, W, W);
			if(BR && !B && !R)
				aQuads[Count++] = IGraphics::CQuadItem(PX + 32 - W, PY + 32 - W, W, W);
			if(T)
				aQuads[Count++] = IGraphics::CQuadItem(PX, PY, 32, W);
			if(B)
				aQuads[Count++] = IGraphics::CQuadItem(PX, PY + 32 - W, 32, W);
			if(L)
				aQuads[Count++] = IGraphics::CQuadItem(PX, PY + (T ? W : 0), W, 32 - (T ? W : 0) - (B ? W : 0));
			if(R)
				aQuads[Count++] = IGraphics::CQuadItem(PX + 32 - W, PY + (T ? W : 0), W, 32 - (T ? W : 0) - (B ? W : 0));
			Graphics()->QuadsDrawTL(aQuads, Count);
		};
		struct CCategory
		{
			int m_Tile;
			int m_Enabled;
			unsigned m_Color;
			float m_Alpha;
		};
		const CCategory aCategories[] = {
			{TILE_UNFREEZE, g_Config.m_ClOutlineUnfreeze, g_Config.m_ClOutlineColorUnfreeze, g_Config.m_ClOutlineAlpha / 100.f},
			{TILE_FREEZE, g_Config.m_ClOutlineFreeze, g_Config.m_ClOutlineColorFreeze, g_Config.m_ClOutlineAlpha / 100.f},
			{TILE_SOLID, g_Config.m_ClOutlineSolid, g_Config.m_ClOutlineColorSolid, g_Config.m_ClOutlineAlphaSolid / 100.f},
			{TILE_DEATH, g_Config.m_ClOutlineKill, g_Config.m_ClOutlineColorKill, g_Config.m_ClOutlineAlpha / 100.f}};
		const int aDX[] = {-1, 0, 1, -1, 1, -1, 0, 1};
		const int aDY[] = {-1, -1, -1, 0, 0, 1, 1, 1};
		Graphics()->TextureClear();
		for(const auto &Category : aCategories)
		{
			if(!Category.m_Enabled)
				continue;
			Graphics()->QuadsBegin();
			ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(Category.m_Color));
			Color.a = Category.m_Alpha;
			Graphics()->SetColor(Color);
			for(int Y = Y0; Y < Y1; ++Y)
				for(int X = X0; X < X1; ++X)
				{
					const int Current = TileAt(X, Y);
					const bool Selected = Category.m_Tile == TILE_FREEZE   ? Current == TILE_FREEZE || Current == TILE_DFREEZE :
							      Category.m_Tile == TILE_UNFREEZE ? Current == TILE_UNFREEZE || Current == TILE_DUNFREEZE :
							      Category.m_Tile == TILE_SOLID    ? Current == TILE_SOLID || Current == TILE_NOHOOK :
												 Current == TILE_DEATH;
					if(!Selected)
						continue;
					std::array<bool, 8> aOpen;
					for(int N = 0; N < 8; ++N)
					{
						const int Neighbor = TileAt(X + aDX[N], Y + aDY[N]);
						aOpen[N] = Category.m_Tile == TILE_FREEZE   ? Neighbor == TILE_AIR || Neighbor == TILE_UNFREEZE || Neighbor == TILE_DUNFREEZE :
							   Category.m_Tile == TILE_UNFREEZE ? Neighbor != TILE_UNFREEZE && Neighbor != TILE_DUNFREEZE :
							   Category.m_Tile == TILE_SOLID    ? Neighbor != TILE_NOHOOK && Neighbor != Current :
											      Neighbor != TILE_DEATH;
					}
					Draw(X, Y, aOpen);
				}
			Graphics()->QuadsEnd();
		}
		if(g_Config.m_ClOutlineTele && Layers()->TeleLayer())
		{
			const auto *pTeleLayer = Layers()->TeleLayer();
			const auto *pTele = static_cast<const CTeleTile *>(Layers()->Map()->GetData(pTeleLayer->m_Tele));
			if(pTele && pTeleLayer->m_Width == Width && pTeleLayer->m_Height == Height &&
				Layers()->Map()->GetDataSize(pTeleLayer->m_Tele) >= (int64_t)Width * Height * (int)sizeof(CTeleTile))
			{
				Graphics()->QuadsBegin();
				ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClOutlineColorTele));
				Color.a = g_Config.m_ClOutlineAlpha / 100.f;
				Graphics()->SetColor(Color);
				for(int Y = Y0; Y < Y1; ++Y)
					for(int X = X0; X < X1; ++X)
					{
						if(X <= 0 || X >= Width - 1 || Y <= 0 || Y >= Height - 1)
							continue;
						const int Type = pTele[Y * Width + X].m_Type;
						if(Type != 10 && Type != 26 && Type != 63)
							continue;
						std::array<bool, 8> aOpen;
						for(int N = 0; N < 8; ++N)
						{
							const int Index = (Y + aDY[N]) * Width + X + aDX[N];
							// Original tests m_Number, not neighboring teleport type.
							aOpen[N] = pTiles[Index].m_Index == 0 && pTele[Index].m_Number == 0;
						}
						Draw(X, Y, aOpen);
					}
				Graphics()->QuadsEnd();
			}
		}
	}

	void CPreTasVisuals::RenderFrozenHud()
	{
		// Original 0x140277060 (the unrelated status messages at its start belong
		// to their own features). Team membership uses snapshot player-info slots.
		if(!g_Config.m_ClShowhud || !GameClient()->m_GameInfo.m_HudDDRace ||
			(!g_Config.m_ClFrozenTeesHud && !g_Config.m_ClFrozenTeesText && !g_Config.m_ClLastNotify))
			return;
		int ViewId = GameClient()->m_Snap.m_LocalClientId;
		if(GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
			ViewId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;
		if(ViewId < 0 || ViewId >= LEGACY_MAX_CLIENTS)
			return;
		const int Team = GameClient()->m_Teams.Team(ViewId);
		std::vector<int> vIds;
		int FrozenCount = 0;
		const auto Frozen = [&](int Id) { return GameClient()->m_aClients[Id].m_FreezeEnd > 0 || GameClient()->m_aClients[Id].m_DeepFrozen; };
		for(int Id = 0; Id < LEGACY_MAX_CLIENTS; ++Id)
			if(GameClient()->m_Snap.m_apPlayerInfos[Id] && GameClient()->m_Teams.Team(Id) == Team)
			{
				vIds.push_back(Id);
				FrozenCount += Frozen(Id);
			}
		const float Width = Graphics()->ScreenAspect() * 300;
		Graphics()->MapScreen(CScreenRect(0, 0, Width, 300));
		if(g_Config.m_ClLastNotify && vIds.size() > 1 && (int)vIds.size() - FrozenCount == 1)
		{
			TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClLastNotifyColor)));
			TextRender()->Text(170, 4, 14, g_Config.m_ClLastNotifyText);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		if(g_Config.m_ClFrozenTeesText > 0)
		{
			char aCount[64];
			str_format(aCount, sizeof(aCount), "%d / %d", g_Config.m_ClFrozenTeesText == 1 ? (int)vIds.size() - FrozenCount : FrozenCount, (int)vIds.size());
			TextRender()->Text((Width - TextRender()->TextWidth(10, aCount)) * .5f, 12, 10, aCount);
		}
		if(!g_Config.m_ClFrozenTeesHud || GameClient()->m_Scoreboard.IsActive() || (Team == 0 && g_Config.m_ClFrozenTeesOnlyInteam))
			return;
		const float Aspect = Width / 300;
		const float Size = g_Config.m_ClFrozenTeesSize;
		const int Columns = std::max(1, (int)((double)Aspect * (g_Config.m_ClShowfps || g_Config.m_ClShowpred ? 8.3 : 9.5) * 13 / Size));
		const int Rows = std::min(g_Config.m_ClFrozenTeesMaxRows, ((int)vIds.size() + Columns - 1) / Columns);
		const int Capacity = g_Config.m_ClFrozenTeesMaxRows * Columns;
		const float StartX = (float)((double)(Aspect * 38) / 1.78 + Width * .5f);
		CUIRect Background;
		Background.x = StartX - Size * .5f;
		Background.y = 0;
		Background.w = std::min(Columns, (int)vIds.size()) * Size;
		Background.h = Rows * Size + 3;
		Background.Draw(ColorRGBA(0, 0, 0, .4f), IGraphics::CORNER_B, 5);
		// Overflow prioritizes frozen tees, preserving id order in both passes.
		const int Passes = Capacity < (int)vIds.size() ? 2 : 1;
		int Drawn = 0;
		for(int Pass = 0; Pass < Passes; ++Pass)
			for(int Id : vIds)
			{
				if(Drawn >= Capacity)
					break;
				const bool IsFrozen = Frozen(Id);
				if(Passes == 2 && IsFrozen != (Pass == 0))
					continue;
				CTeeRenderInfo Info = GameClient()->m_aClients[Id].m_RenderInfo;
				float Alpha = 1;
				int Emote = GameClient()->m_aClients[Id].m_RenderCur.m_Emote;
				if(IsFrozen)
				{
					Emote = EMOTE_PAIN;
					if(g_Config.m_ClFrozenTeesHudSkins)
					{
						Info.m_ColorBody.r *= .4f;
						Info.m_ColorBody.g *= .4f;
						Info.m_ColorBody.b *= .4f;
						Info.m_ColorFeet.r *= .4f;
						Info.m_ColorFeet.g *= .4f;
						Info.m_ColorFeet.b *= .4f;
						Info.m_ColorBody.a = Info.m_ColorFeet.a = 1;
						Alpha = .6f;
					}
					else if(GameClient()->m_Players.NinjaTeeRenderInfo())
					{
						Info.ApplySkin(GameClient()->m_Players.NinjaTeeRenderInfo()->TeeRenderInfo());
						Info.m_ColorBody = Info.m_ColorFeet = ColorRGBA(1, 1, 1, 1);
						Info.m_CustomColoredSkin = false;
					}
				}
				Info.m_Size = Size;
				RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, Emote, vec2(1, 0),
					vec2(StartX + (Drawn % Columns) * Size, (Drawn / Columns) * Size + Size * .7f), Alpha);
				++Drawn;
			}
	}

	void CPreTasVisuals::OnRender()
	{
		if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
			return;
		const CScreenRect Old = Graphics()->GetScreen();
		const auto &Camera = GameClient()->m_Camera;
		Graphics()->MapScreen(Graphics()->MapScreenToWorld(Camera.m_Center.x, Camera.m_Center.y, 100, 100, 100, 0, 0, Graphics()->ScreenAspect(), Camera.m_Zoom));
		RenderTileOutlines();
		RenderTrajectory();
		RenderFrozenHud();
		Graphics()->MapScreen(Old);
	}
}
