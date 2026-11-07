#include "avoid.h"

#include "simulation.h"

#include <base/dbg.h>

#include <engine/shared/config.h>
#include <engine/graphics.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <chrono>

namespace OpenKrx
{
	CBlatantSettings SettingsFromConfig()
	{
		CBlatantSettings Settings;
		Settings.m_CheckTicks = g_Config.m_AvoidBlatantCheckTicks;
		Settings.m_KickInTicks = g_Config.m_AvoidKickInTicks;
		Settings.m_Nsif = g_Config.m_AvoidNsif != 0;
		Settings.m_PlayerPrediction = g_Config.m_AvoidPlayerPrediction != 0;
		Settings.m_Direction = g_Config.m_AvoidBlatantDirection != 0;
		Settings.m_Hook = g_Config.m_AvoidBlatantHook != 0;
		Settings.m_Death = g_Config.m_AvoidBlatantDeath != 0;
		Settings.m_Teles = g_Config.m_AvoidBlatantTeles != 0;
		Settings.m_Unfreeze = g_Config.m_AvoidBlatantUnfreezeTile != 0;
		Settings.m_UnfreezeTicks = g_Config.m_AvoidBlatantUnfreezeTicks;
		Settings.m_TrackPoint = g_Config.m_AvoidTrackPoints != 0;
		Settings.m_SafeTracking = g_Config.m_AvoidSafeAimTracking != 0;
		Settings.m_Aimbot = g_Config.m_AvoidBlatantAimbot != 0;
		Settings.m_Fov = g_Config.m_AvoidBlatantFov;
		Settings.m_Segments = g_Config.m_AvoidAimbotSegments;
		Settings.m_AimAssist = g_Config.m_AvoidAimAssistAimbot != 0;
		Settings.m_AutoAim = g_Config.m_AvoidAutoAimAimbot != 0;
		Settings.m_AutoDrag = g_Config.m_AvoidAutoDrag != 0;
		return Settings;
	}

	bool CAvoid::Active() const
	{
		return g_Config.m_AvoidFreeze != 0 && g_Config.m_AvoidAgentType == 2;
	}

	void CAvoid::Render(CGameClient *pGameClient)
	{
		if(!Active() || pGameClient->Client()->State() != IClient::STATE_ONLINE ||
			(!g_Config.m_AvoidDrawAimbot && !g_Config.m_AvoidDrawTrackPoint))
			return;
		const int Slot = g_Config.m_ClDummy;
		const int Id = pGameClient->m_aLocalIds[Slot];
		const auto *pLocal = pGameClient->KrxGameWorld().GetCharacterById(Id);
		if(!pLocal)
			return;
		auto *pGraphics = pGameClient->Graphics();
		const auto OldScreen = pGraphics->GetScreen();
		const auto &Camera = pGameClient->m_Camera;
		pGraphics->MapScreen(pGraphics->MapScreenToWorld(Camera.m_Center.x, Camera.m_Center.y, 100, 100, 100, 0, 0, pGraphics->ScreenAspect(), Camera.m_Zoom));
		// TAS ApplyRenderState updates this interpolated local position too.
		const vec2 From = pGameClient->m_LocalCharacterPos;
		const float HookLength = pGameClient->m_aTuning[0].m_HookLength;
		const auto Draw = [&](vec2 Point, ColorRGBA Circle, ColorRGBA Line) {
			pGraphics->TextureClear();
			pGraphics->QuadsBegin();
			pGraphics->SetColor(Circle);
			pGraphics->DrawCircle(Point.x, Point.y, 3.25f, 64);
			// 32f7c0/32fbc0 pass width2: ((2-1)*.25+.5)*zoom half-width.
			if(distance(From, Point) >= 1.0f)
			{
				const vec2 Offset = normalize(vec2(Point.y - From.y, From.x - Point.x)) * (0.75f * Camera.m_Zoom);
				pGraphics->SetColor(Line);
				IGraphics::CFreeformItem Quad(Point - Offset, Point + Offset, From - Offset, From + Offset);
				pGraphics->QuadsDrawFreeform(&Quad, 1);
			}
			pGraphics->QuadsEnd();
		};
		// New32f7c0 finally consumes the aimbot ESP setting and cached points.
		if(g_Config.m_AvoidDrawAimbot && g_Config.m_AvoidBlatantAimbot)
		{
			m_Planner.ExpireAimPoints(pLocal->m_Pos, HookLength);
			if(g_Config.m_AvoidAimAssistAimbot && m_Planner.AssistPoint())
				Draw(*m_Planner.AssistPoint(), ColorRGBA(0, 0, 1, .5f), ColorRGBA(1, 1, 1, .5f));
			if(g_Config.m_AvoidAutoAimAimbot && m_Planner.AutoAimPoint())
				Draw(*m_Planner.AutoAimPoint(), ColorRGBA(1, 0, 0, .5f), ColorRGBA(1, 1, 1, .5f));
		}
		if(g_Config.m_AvoidDrawTrackPoint && g_Config.m_AvoidTrackPoints && m_Planner.TrackPoint())
		{
			const vec2 Mouse = pGameClient->m_Controls.m_aMousePos[Slot];
			const vec2 Quantized((int)Mouse.x, (int)Mouse.y);
			bool Hookable = false;
			if(Quantized != vec2(0, 0))
			{
				const vec2 To = pLocal->m_Pos + normalize(Quantized) * (HookLength - 28.0f);
				vec2 Hit = To;
				int Tele = 0;
				Hookable = pGameClient->IntersectCharacter(pLocal->m_Pos, To, Hit, Id) == -1 &&
					pGameClient->Collision()->IntersectLineTeleHook(pLocal->m_Pos, To, &Hit, nullptr, &Tele) == TILE_SOLID && Hit != vec2(0, 0);
			}
			Draw(*m_Planner.TrackPoint(), ColorRGBA(0, 1, 0, .7f), Hookable ? ColorRGBA(1, 1, 1, .7f) : ColorRGBA(1, 1, 0, 1));
		}
		pGraphics->MapScreen(OldScreen);
	}

	void CAvoid::OnReset()
	{
		m_Planner.Reset();
		m_WasActive = false;
		m_LastClientId = -1;
		m_LastSlot = -1;
		m_OwnsHook = false;
	}

	CNetObj_PlayerInput CAvoid::AdjustInput(CGameClient *pGameClient, CNetObj_PlayerInput Input, int ClientId, vec2 MouseAim, bool ManualHook)
	{
		m_Diagnostic = {};
		m_Diagnostic.m_ManualHook = ManualHook;
		m_Diagnostic.m_OwnedHookBefore = m_OwnsHook;
		if(!Active() || ClientId < 0 || pGameClient->Client()->State() != IClient::STATE_ONLINE ||
			pGameClient->m_Snap.m_SpecInfo.m_Active || !pGameClient->m_Snap.m_pLocalCharacter ||
			!(Input.m_PlayerFlags & PLAYERFLAG_PLAYING))
		{
			// Original cc620 skips transient gates without clearing its agent.
			// Connection/tee changes still discard plans through OnReset below.
			if(pGameClient->Client()->State() != IClient::STATE_ONLINE && m_WasActive)
				OnReset();
			m_Diagnostic.m_pGate = !Active() ? "disabled" : ClientId < 0                                    ? "invalid_cid" :
								pGameClient->Client()->State() != IClient::STATE_ONLINE ? "offline" :
								pGameClient->m_Snap.m_SpecInfo.m_Active                 ? "spectating" :
								!pGameClient->m_Snap.m_pLocalCharacter                  ? "no_local_snapshot" :
															  "not_playing";
			return Input;
		}
		// 2cc7a0 is an early gate; it preserves the saved planner state.
		if(pGameClient->m_PreTasVisuals.ShouldSuspendAvoid())
		{
			m_Diagnostic.m_pGate = "afk_protection";
			return Input;
		}
		// A respawn/dummy switch must not consume another tee's stored NSIF plan.
		if(m_LastClientId != ClientId || m_LastSlot != g_Config.m_ClDummy)
		{
			m_Planner.Reset();
			m_OwnsHook = false;
		}
		m_LastClientId = ClientId;
		m_LastSlot = g_Config.m_ClDummy;
		// New327ef0 checks tile9 at the predicted/TAS position, not the
		// interpolated render position (which can be on the other side of a tile).
		const auto *pPredicted = pGameClient->KrxPredictedWorld().GetCharacterById(ClientId);
		const vec2 LocalPos = pPredicted ? pPredicted->m_Pos : pGameClient->m_LocalCharacterPos;
		const vec2 TilePos(static_cast<int>(LocalPos.x), static_cast<int>(LocalPos.y));
		if(pPredicted && pGameClient->m_aClients[ClientId].m_Super &&
			pGameClient->Collision()->GetTileIndex(pGameClient->Collision()->GetPureMapIndex(TilePos)) == TILE_FREEZE)
		{
			m_Diagnostic.m_pGate = "super_freeze_tile";
			return Input;
		}
		const CBlatantSettings Settings = SettingsFromConfig();
		auto pSimulation = CreateSimulation(pGameClient, ClientId, Settings.m_PlayerPrediction);
		// KRX b4450 gates on the authoritative world, while cd0c0 searches the
		// predicted world. A predicted freeze must not clear NSIF/skip unfreeze.
		const bool AuthoritativeFrozen = IsKrxCharacterFrozen(pGameClient, ClientId);
		if(!pSimulation->Valid() || AuthoritativeFrozen)
		{
			if(g_Config.m_AvoidDebug && pSimulation->Valid() && (m_DebugCounter++ % 20) == 0)
			{
				const vec2 Pos = pSimulation->Position();
				dbg_msg("avoid", "cid=%d pos=%.0f,%.0f frozen=1 decision=0 before_dir=%d", ClientId, Pos.x, Pos.y, Input.m_Direction);
			}
			m_Diagnostic.m_pGate = AuthoritativeFrozen ? "authoritative_frozen_gate" : "invalid_simulation";
			m_OwnsHook = false;
			return Input;
		}
		m_Planner.SetSettings(Settings);
		m_Planner.SetTracing(g_Config.m_AvoidDebug != 0);
		m_Diagnostic.m_pGate = "evaluated";
		const bool WasFrozen = pSimulation->Frozen();
		const vec2 Pos = pSimulation->Position();
		const auto Start = std::chrono::steady_clock::now();
		// Rocket releases after Avoid. A later NSIF reuse must simulate and emit
		// the current fire counter, otherwise the old odd counter can latch fire
		// after Rocket has already retired its release timer.
		m_Planner.SynchronizeFireCounter(Input.m_Fire);
		auto Decision = m_Planner.Decide(*pSimulation, Input, MouseAim);
		m_Diagnostic.m_Proposed = Decision;
		int DecisionLife = m_Planner.LastDecisionLife();
		const auto Output = Decision.value_or(Input);
		m_OwnsHook = !ManualHook && Output.m_Hook != 0 && (m_OwnsHook || Input.m_Hook == 0);
		const auto Elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - Start).count();
		m_Diagnostic.m_OwnedHookAfter = m_OwnsHook;
		m_Diagnostic.m_Life = DecisionLife;
		m_Diagnostic.m_ElapsedUs = (int)Elapsed;
		const bool Changed = Decision && (Decision->m_Direction != Input.m_Direction || Decision->m_Hook != Input.m_Hook || Decision->m_Jump != Input.m_Jump ||
							 Decision->m_TargetX != Input.m_TargetX || Decision->m_TargetY != Input.m_TargetY);
		if(g_Config.m_AvoidDebug && (Changed || (m_DebugCounter++ % 20) == 0))
		{
			const auto *pLocal = pGameClient->KrxPredictedWorld().GetCharacterById(ClientId);
			if(!pLocal)
				pLocal = pGameClient->KrxGameWorld().GetCharacterById(ClientId);
			const vec2 Velocity = pLocal ? pLocal->Core()->m_Vel : vec2(0, 0);
			const vec2 HookPos = pLocal ? pLocal->Core()->m_HookPos : vec2(0, 0);
			const int HookState = pLocal ? pLocal->Core()->m_HookState : -1;
			dbg_msg("avoid", "cid=%d pos=%.0f,%.0f frozen=%d decision=%d dir=%d hook=%d jump=%d before_dir=%d before_hook=%d manual_hook=%d plan=%d took=%lldus life=%d tick=%d vel=%.2f,%.2f hook_state=%d hook_pos=%.1f,%.1f aim=%d,%d->%d,%d check=%d kick=%d before_jump=%d manual_jump=%d",
				ClientId, Pos.x, Pos.y, WasFrozen ? 1 : 0, Decision.has_value() ? 1 : 0,
				Decision ? Decision->m_Direction : 0, Decision ? Decision->m_Hook : 0, Decision ? Decision->m_Jump : 0,
				Input.m_Direction, Input.m_Hook, ManualHook ? 1 : 0, (int)m_Planner.LastPlan().size(), (long long)Elapsed,
				DecisionLife, pGameClient->KrxPredictedWorld().GameTick(), Velocity.x, Velocity.y, HookState, HookPos.x, HookPos.y,
				Input.m_TargetX, Input.m_TargetY, Output.m_TargetX, Output.m_TargetY, Settings.m_CheckTicks, Settings.m_KickInTicks,
				Input.m_Jump, pGameClient->m_Controls.m_PhysicalJump);
		}
		if(g_Config.m_AvoidDebug && Elapsed > 20000)
			dbg_msg("avoid", "WARNING slow decide: %lldus", (long long)Elapsed);
		m_WasActive = true;
		if(!Decision)
		{
			// KRX retries on the next input tick; a cooldown misses short rescue windows.
			return Input;
		}
		// KRX writes the whole 40-byte input slot: fire, weapons and flags come
		// from the snapshot the planner started with.
		Input = *Decision;
		return Input;
	}

}
