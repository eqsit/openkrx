#include "krx_tas.h"

#include <base/str.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/krx/replay_fake_aim.h>
#include <game/client/krx/tile_editor.h>
#include <game/client/prediction/entities/character.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <random>

namespace OpenKrx
{
	namespace
	{
		ColorRGBA PathColor(unsigned Packed)
		{
			auto Color = color_cast<ColorRGBA>(ColorHSLA(Packed, true));
			if(Color.a == 0)
				Color.a = .75f;
			return Color;
		}
	}

	void CKrxTas::Line(vec2 From, vec2 To, ColorRGBA Color, float Width)
	{
		const vec2 Delta = To - From;
		const float Length = length(Delta);
		if(Length < 1)
			return;
		const vec2 Perp = vec2(Delta.y, -Delta.x) / Length * GameClient()->m_Camera.m_Zoom * ((Width - 1) * .25f + .5f);
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(Color);
		IGraphics::CFreeformItem Quad(To - Perp, To + Perp, From - Perp, From + Perp);
		Graphics()->QuadsDrawFreeform(&Quad, 1);
		Graphics()->QuadsEnd();
	}

	void CKrxTas::Circle(vec2 Pos, float Radius, ColorRGBA Color)
	{
		vec2 Prev = Pos + direction(-.1f) * Radius;
		for(float Angle = 0; Angle < 2 * pi; Angle += .1f)
		{
			const vec2 Next = Pos + direction(Angle) * Radius;
			Line(Prev, Next, Color, 1);
			Prev = Next;
		}
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Color.a = .5f;
		Graphics()->SetColor(Color);
		Graphics()->DrawCircle(Pos.x, Pos.y, Radius, 32);
		Graphics()->QuadsEnd();
	}

	void CKrxTas::ApplyRenderState()
	{
		if(!Active())
			return;
		const float Intra = IntraTick();
		for(int Id = 0; Id < MAX_CLIENTS; ++Id)
		{
			auto *pCurrent = m_pWorld->GetCharacterById(Id);
			if(!pCurrent)
				continue;
			auto *pPrevious = m_pPreviousWorld->GetCharacterById(Id);
			auto &Data = GameClient()->m_aClients[Id];
			Data.m_Predicted = pCurrent->GetCore();
			Data.m_PrevPredicted = pPrevious ? pPrevious->GetCore() : Data.m_Predicted;
			Data.m_PrevPredicted.m_Pos = pCurrent->m_PrevPos;
			Data.m_Predicted.Write(&Data.m_RenderCur);
			Data.m_PrevPredicted.Write(&Data.m_RenderPrev);
			Data.m_RenderCur.m_Weapon = pCurrent->GetActiveWeapon();
			Data.m_RenderCur.m_AttackTick = pCurrent->GetAttackTick();
			Data.m_RenderPrev.m_Weapon = pPrevious ? pPrevious->GetActiveWeapon() : pCurrent->GetActiveWeapon();
			Data.m_RenderPrev.m_AttackTick = pPrevious ? pPrevious->GetAttackTick() : pCurrent->GetAttackTick();
			Data.m_IsPredicted = true;
			Data.m_IsPredictedLocal = Id == m_aIds[0] || Id == m_aIds[1];
			Data.m_RenderPos = mix(Data.m_PrevPredicted.m_Pos, Data.m_Predicted.m_Pos, Intra);
			if(Id == GameClient()->m_aLocalIds[g_Config.m_ClDummy])
			{
				GameClient()->m_LocalCharacterPos = Data.m_RenderPos;
				GameClient()->m_PredictedChar = Data.m_Predicted;
				GameClient()->m_PredictedPrevChar = Data.m_PrevPredicted;
			}
		}
	}

	void CKrxTas::HandleWorldEvents()
	{
		for(int Slot = 0; Slot < 2; ++Slot)
			if(auto *pCharacter = m_pWorld->GetCharacterById(m_aIds[Slot]))
			{
				const int Events = pCharacter->Core()->m_TriggeredEvents;
				const vec2 Pos = pCharacter->Core()->m_Pos;
				if(g_Config.m_KrxTasShowEffects && (Events & COREEVENT_AIR_JUMP))
					GameClient()->m_Effects.AirJump(Pos, 1, g_Config.m_KrxTasUseSound ? 1 : 0);
				if(g_Config.m_KrxTasUseSound)
				{
					if(Events & COREEVENT_GROUND_JUMP)
						GameClient()->m_Sounds.PlayAt(CSounds::CHN_WORLD, SOUND_PLAYER_JUMP, 1, Pos);
					if(Events & COREEVENT_HOOK_ATTACH_GROUND)
						GameClient()->m_Sounds.PlayAt(CSounds::CHN_WORLD, SOUND_HOOK_ATTACH_GROUND, 1, Pos);
					if(Events & COREEVENT_HOOK_ATTACH_PLAYER)
						GameClient()->m_Sounds.PlayAt(CSounds::CHN_WORLD, SOUND_HOOK_ATTACH_PLAYER, 1, Pos);
					if(Events & COREEVENT_HOOK_HIT_NOHOOK)
						GameClient()->m_Sounds.PlayAt(CSounds::CHN_WORLD, SOUND_HOOK_NOATTACH, 1, Pos);
				}
			}
		for(const auto &Event : m_pWorld->m_PredictedEvents)
		{
			if(Event.m_EventId == NETEVENTTYPE_SOUNDWORLD && g_Config.m_KrxTasUseSound)
				GameClient()->m_Sounds.PlayAt(CSounds::CHN_WORLD, Event.m_ExtraInfo, 1, Event.m_Pos);
			if(g_Config.m_KrxTasShowEffects)
			{
				if(Event.m_EventId == NETEVENTTYPE_EXPLOSION)
					GameClient()->m_Effects.Explosion(Event.m_Pos, 1);
				else if(Event.m_EventId == NETEVENTTYPE_HAMMERHIT)
					GameClient()->m_Effects.HammerHit(Event.m_Pos, 1, g_Config.m_KrxTasUseSound ? 1 : 0);
				else if(Event.m_EventId == NETEVENTTYPE_DAMAGEIND)
					GameClient()->m_Effects.DamageIndicator(Event.m_Pos, direction(Event.m_ExtraInfo / 256.0f), 1);
			}
		}
		m_pWorld->m_PredictedEvents.clear();
	}

	void CKrxTas::RenderPath(const std::vector<CReplayFrame> &vFrames, unsigned Packed, int Mode, bool Segmented)
	{
		if(vFrames.empty())
			return;
		const ColorRGBA Color = PathColor(Packed);
		const size_t Begin = Segmented ? std::min((size_t)std::max(0, m_PlaybackTick), vFrames.size() - 1) : 0;
		const size_t End = Segmented ? std::min(vFrames.size() - 1, Begin + 50) : vFrames.size();
		for(size_t I = Begin; I < End; ++I)
			if(Mode == 0)
				Line(vFrames[Segmented ? I : I ? I - 1 :
								 0]
						.m_Position,
					vFrames[Segmented ? I + 1 : I].m_Position, Color);
			else
				Circle(vFrames[I].m_Position, 2.5f, Color);
	}

	void CKrxTas::RenderPrediction()
	{
		if(!Active() || !g_Config.m_KrxTasDrawPredictionPath)
			return;
		CGameWorld World;
		World.CopyWorldIsolated(m_pWorld.get());
		std::array<vec2, MAX_CLIENTS> aPrev{};
		std::array<CNetObj_PlayerInput, MAX_CLIENTS> aInputs{};
		std::array<bool, MAX_CLIENTS> aPredict{};
		for(int Id = 0; Id < MAX_CLIENTS; ++Id)
			if(auto *pCharacter = World.GetCharacterById(Id))
			{
				aPrev[Id] = pCharacter->Core()->m_Pos;
				aInputs[Id] = Id == GameClient()->m_aLocalIds[g_Config.m_ClDummy] ? GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy] : pCharacter->DiagnosticCurrentInput();
				aPredict[Id] = pCharacter->Core()->m_Vel != vec2(0, 0);
			}
		for(int Tick = 0; Tick <= 50; ++Tick)
		{
			for(int Id = 0; Id < MAX_CLIENTS; ++Id)
				if(aPredict[Id])
					if(auto *pCharacter = World.GetCharacterById(Id))
						pCharacter->OnDirectInput(&aInputs[Id]);
			++World.m_GameTick;
			for(int Id = 0; Id < MAX_CLIENTS; ++Id)
				if(aPredict[Id])
					if(auto *pCharacter = World.GetCharacterById(Id))
						pCharacter->OnPredictedInput(&aInputs[Id]);
			World.Tick();
			for(int Id = 0; Id < MAX_CLIENTS; ++Id)
			{
				auto *pCharacter = World.GetCharacterById(Id);
				if(!pCharacter || !aPredict[Id])
					continue;
				const bool Own = Id == m_aIds[0] || Id == m_aIds[1];
				bool Frozen = pCharacter->m_FreezeTime > 0;
				for(int Index : Collision()->GetMapIndices(pCharacter->m_PrevPos, pCharacter->Core()->m_Pos))
				{
					const int Tile = Collision()->GetTileIndex(Index), Front = Collision()->GetFrontTileIndex(Index);
					for(int Kind : {9, 26, 10, 31, 63, 2})
						Frozen |= Tile == Kind || Front == Kind;
					Frozen |= Collision()->IsTeleport(Index) || Collision()->IsEvilTeleport(Index) || Collision()->IsCheckTeleport(Index) || Collision()->IsCheckEvilTeleport(Index);
				}
				auto Color = PathColor(Frozen ? g_Config.m_KrxTasDrawPredictionPathColorFrozen : Own ? g_Config.m_KrxTasDrawPredictionPathColor :
														       g_Config.m_KrxTasDrawPredictionPathColorOthers);
				const float Fade = 1.0f - Tick / 51.0f * .5f;
				Color.r *= Fade;
				Color.g *= Fade;
				Color.b *= Fade;
				const vec2 Pos = pCharacter->Core()->m_Pos;
				if(g_Config.m_KrxTasDrawPredictionPathMode == 0)
					Line(Tick ? aPrev[Id] : Pos, Pos, Color, 3.5f);
				else
					Circle(Pos, 3, Color);
				aPrev[Id] = Pos;
			}
		}
	}

	void CKrxTas::RenderHud()
	{
		if(!g_Config.m_KrxTasHud || !Active())
			return;
		const CScreenRect OldScreen = Graphics()->GetScreen();
		Graphics()->MapScreen(CScreenRect(0, 0, 300 * Graphics()->ScreenAspect(), 300));
		Graphics()->DrawRect(0, 128, 50, 78, ColorRGBA(0, 0, 0, .4f), IGraphics::CORNER_R, 5);
		const auto &Input = GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
		const int aValues[] = {Input.m_Direction, Input.m_Hook, Input.m_Jump, Input.m_Fire % 2, Input.m_TargetX, Input.m_TargetY};
		const char *apLabels[] = {"Direction: %d", "Hook: %d", "Jump: %d", "Fire: %d", "Target X: %d", "Target Y: %d"};
		char aText[128];
		float Y = 132;
		for(int I = 0; I < 6; ++I, Y += 8)
		{
			str_format(aText, sizeof(aText), apLabels[I], aValues[I]);
			TextRender()->Text(4, Y, 6, aText);
		}
		TextRender()->Text(4, Y, 6, "Reload:");
		if(auto *pCharacter = m_pWorld->GetCharacterById(GameClient()->m_aLocalIds[g_Config.m_ClDummy]))
		{
			static int s_Weapon = -1, s_ReloadTick = -1, s_Jumps = 0;
			const auto &Core = *pCharacter->Core();
			if(s_Weapon != pCharacter->GetActiveWeapon())
			{
				s_Weapon = pCharacter->GetActiveWeapon();
				s_ReloadTick = -1;
			}
			const float Maximum = Core.m_Tuning.GetWeaponFireDelay(s_Weapon) * 50;
			if(std::abs(pCharacter->DiagnosticReloadTimer() - Maximum) <= 1.5f)
				s_ReloadTick = m_pWorld->GameTick();
			const float Progress = Maximum > 0 ? std::clamp(Maximum - (m_pWorld->GameTick() - s_ReloadTick), 0.0f, Maximum) / Maximum : 0;
			const auto &Skin = GameClient()->m_HudSkin;
			const auto Piece = [&](IGraphics::CTextureHandle Texture, float X, float Width, float U0, float U1) {
				Graphics()->TextureSet(Texture);
				Graphics()->QuadsBegin();
				Graphics()->SetColor(1, 1, 1, 1);
				Graphics()->QuadsSetSubset(U0, 0, U1, 1);
				IGraphics::CQuadItem Quad(X, Y, Width, 6);
				Graphics()->QuadsDrawTL(&Quad, 1);
				Graphics()->QuadsEnd();
			};
			// 30e1b0: horizontal ninja bar, 6 + 10 + 6 pixels, end fill .1875.
			Graphics()->WrapClamp();
			const float Left = std::min(1.0f, Progress / .1875f);
			Piece(Skin.m_SpriteHudNinjaBarFullLeft, 26, 3 + Left * 3, 0, .5f + Left * .5f);
			if(Left < 1)
				Piece(Skin.m_SpriteHudNinjaBarEmptyRight, 29 + Left * 3, (1 - Left) * 3, 0, .5f - Left * .5f);
			const float Middle = std::clamp((Progress - .1875f) / .625f, 0.0f, 1.0f) * 10;
			Piece(Skin.m_SpriteHudNinjaBarFull, 32, Middle, 0, std::min(1.0f, Middle / 6));
			Piece(Skin.m_SpriteHudNinjaBarEmpty, 32 + Middle, 10 - Middle, 1 - std::min(1.0f, (10 - Middle) / 6), 1);
			const float Right = std::clamp((Progress - .8125f) / .1875f, 0.0f, 1.0f);
			if(Right > 0)
				Piece(Skin.m_SpriteHudNinjaBarFullLeft, 42, Right * 3, 1, 1 - Right * .5f);
			Piece(Skin.m_SpriteHudNinjaBarEmptyRight, 42 + Right * 3, 6 - Right * 3, Right * .5f, 1);
			Graphics()->QuadsSetSubset(0, 0, 1, 1);
			Graphics()->WrapNormal();
			if(pCharacter->IsGrounded())
				s_Jumps = 0;
			if(Core.m_TriggeredEvents & COREEVENT_GROUND_JUMP)
				s_Jumps = 1;
			if(s_Jumps == 1)
				s_Jumps = Core.m_JumpedTotal + 1;
			else if(Core.m_JumpedTotal > 1)
				s_Jumps = Core.m_JumpedTotal;
			if(Core.m_EndlessJump)
				str_format(aText, sizeof(aText), "Jumps: %d / inf", s_Jumps);
			else
				str_format(aText, sizeof(aText), "Jumps: %d / %d", s_Jumps, Core.m_Jumps);
			TextRender()->Text(4, Y + 8, 6, aText);
			str_format(aText, sizeof(aText), "Grounded: %d", pCharacter->IsGrounded());
			TextRender()->Text(4, Y + 16, 6, aText);
		}
		Graphics()->MapScreen(OldScreen);
	}

	void CKrxTas::OnRender()
	{
		UpdateVault();
		UpdateLegacyConversion();
		UpdateAnalysis();
		if(g_Config.m_KrxTasAnalyzerAnalyze)
			AnalyzeReplay();
		if(g_Config.m_KrxTasFakeAim)
			AddFakeAim();
		if(g_Config.m_KrxSaveReplayToFile)
		{
			g_Config.m_KrxSaveReplayToFile = 0;
			SaveReplay(g_Config.m_KrxReplayFile);
		}
		if(g_Config.m_KrxLoadReplayFromFile)
		{
			g_Config.m_KrxLoadReplayFromFile = 0;
			LoadReplay(g_Config.m_KrxReplayFile);
		}
		if(Client()->State() != IClient::STATE_ONLINE)
			return;
		UpdatePathFinding();
		const CScreenRect OldScreen = Graphics()->GetScreen();
		const auto &Camera = GameClient()->m_Camera;
		Graphics()->MapScreen(Graphics()->MapScreenToWorld(Camera.m_Center.x, Camera.m_Center.y, 100, 100, 100, 0, 0, Graphics()->ScreenAspect(), Camera.m_Zoom));
		RenderFlowField();
		if(g_Config.m_KrxLoadReplay && g_Config.m_KrxTasDrawPath)
			for(const auto &vFrames : m_Replay.m_aFrames)
				RenderPath(vFrames, g_Config.m_KrxTasDrawPathColor, g_Config.m_KrxTasDrawPathMode, g_Config.m_KrxTasDrawPathSegmented != 0);
		if(g_Config.m_KrxTasDrawStartEndPos && m_Replay.m_aFrames[0].size() > 50)
			for(int Slot = 0; Slot < 2; ++Slot)
			{
				const auto &vFrames = m_Replay.m_aFrames[Slot];
				if(!vFrames.empty())
				{
					const int Id = m_aIds[Slot];
					const auto Marker = [&](vec2 Pos, int Emote, vec2 Aim) {
						if(Pos == vec2(0, 0))
							return;
						if(Id >= 0 && Id < MAX_CLIENTS)
						{
							auto Info = GameClient()->m_aClients[Id].m_RenderInfo;
							RenderTools()->RenderTee(CAnimState::GetIdle(), &Info, Emote, Aim, Pos);
						}
						const vec2 aCorners[] = {Pos + vec2(-16, -16), Pos + vec2(16, -16), Pos + vec2(16, 16), Pos + vec2(-16, 16)};
						for(int I = 0; I < 4; ++I)
							Line(aCorners[I], aCorners[(I + 1) % 4], ColorRGBA(0, 1, 0, 1), 1);
					};
					Marker(vFrames.front().m_Position, 2, vec2(1, 0));
					if(!Active())
						Marker(vFrames.back().m_Position, 1, vec2(-1, 0));
				}
			}
		RenderPrediction();
		RenderHud();
		if(g_Config.m_KrxTileEditorEnable)
		{
			Graphics()->MapScreen(CScreenRect(0, 0, 300.0f * Graphics()->ScreenAspect(), 300.0f));
			const int Slot = g_Config.m_ClDummy;
			const vec2 Brush = GameClient()->m_Snap.m_SpecInfo.m_Active ? GameClient()->m_Snap.m_SpecInfo.m_Position : GameClient()->m_Controls.m_aTargetPos[Slot];
			const int X = (int)(Brush.x / 32.0f), Y = (int)(Brush.y / 32.0f);
			unsigned char Kind = 0;
			if(X >= 0 && Y >= 0 && X < m_FlowField.Width() && Y < m_FlowField.Height())
				Kind = m_vTunnels[(size_t)Y * m_FlowField.Width() + X];
			char aLine[128];
			str_format(aLine, sizeof(aLine), "Tile Editor Mode: %s", g_Config.m_KrxTileEditorType == 1 ? "Finish" : "Tunnel");
			TextRender()->Text(4, 270, 6, aLine);
			str_format(aLine, sizeof(aLine), "Tile Info: %s", Kind & TileEditor::FINISH ? "Finish" : Kind & TileEditor::TUNNEL ? "Tunnel" : "Empty");
			TextRender()->Text(4, 278, 6, aLine);
		}
		Graphics()->MapScreen(OldScreen);
	}

	bool CKrxTas::AddFakeAim()
	{
		g_Config.m_KrxTasFakeAim = 0;
		if(m_Replay.m_aFrames[0].empty())
		{
			Message("TAS: Record or load a replay before adding fake aim.");
			return false;
		}
		if(!m_pInitialWorld)
		{
			Message("TAS: Replay start state unavailable. Load a valid replay on its map first.");
			return false;
		}
		// The live world and the current TAS cursor can both have moved since
		// recording started. Only this snapshot precedes the first input frame.
		const bool Result = AddReplayFakeAim(m_Replay, *m_pInitialWorld, m_aIds, g_Config.m_KrxTasFakeAimMode, GameClient()->m_Controls.GetMaxMouseDistance());
		Message(Result ? "TAS: Fake aim added successfully." : "TAS: Fake aim would change the replay trajectory. Replay unchanged.");
		return Result;
	}
}
