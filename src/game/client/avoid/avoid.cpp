#include "avoid.h"

#include "simulation.h"

#include <base/dbg.h>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>

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
			if(m_OwnsHook && !ManualHook && g_Config.m_AvoidEmergencyHook)
				Input.m_Hook = 0;
			if(m_WasActive)
				OnReset();
			m_Diagnostic.m_pGate = !Active() ? "disabled" : ClientId < 0                                    ? "invalid_cid" :
								pGameClient->Client()->State() != IClient::STATE_ONLINE ? "offline" :
								pGameClient->m_Snap.m_SpecInfo.m_Active                 ? "spectating" :
								!pGameClient->m_Snap.m_pLocalCharacter                  ? "no_local_snapshot" :
															  "not_playing";
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
		const CBlatantSettings Settings = SettingsFromConfig();
		m_Diagnostic.m_PendingAutomaticFire = g_Config.m_AvoidRocketPulsePrediction &&
						      pGameClient->m_RocketSave.OwnsPendingFire(Input.m_Fire, ClientId, g_Config.m_ClDummy);
		const auto *pAuthoritative = pGameClient->m_GameWorld.GetCharacterById(ClientId);
		const auto *pPredictionSource = pGameClient->m_PredictedWorld.GetCharacterById(ClientId);
		if(!pPredictionSource)
			pPredictionSource = pGameClient->m_GameWorld.GetCharacterById(ClientId);
		m_Diagnostic.m_AllowImmediateShot = m_Diagnostic.m_PendingAutomaticFire &&
						    RocketShotReady(pPredictionSource, pAuthoritative) &&
						    !pGameClient->m_RocketSave.ReleasesPendingFireNow(pGameClient->m_GameWorld.GameTick(), Input.m_Fire);
		auto pSimulation = CreateSimulation(pGameClient, ClientId, Settings.m_PlayerPrediction,
			m_Diagnostic.m_PendingAutomaticFire, m_Diagnostic.m_AllowImmediateShot);
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
			m_Planner.Reset();
			m_Diagnostic.m_pGate = AuthoritativeFrozen ? "authoritative_frozen_gate" : "invalid_simulation";
			if(m_OwnsHook && !ManualHook && g_Config.m_AvoidEmergencyHook)
				Input.m_Hook = 0;
			m_OwnsHook = false;
			return Input;
		}
		m_Planner.SetSettings(Settings);
		m_Planner.SetTracing(g_Config.m_AvoidDebug != 0);
		m_Diagnostic.m_pGate = "evaluated";
		const bool WasFrozen = pSimulation->Frozen();
		const vec2 Pos = pSimulation->Position();
		const auto Start = std::chrono::steady_clock::now();
		auto Decision = m_Planner.Decide(*pSimulation, Input, MouseAim);
		m_Diagnostic.m_Proposed = Decision;
		int DecisionLife = m_Planner.LastDecisionLife();
		if(Decision && g_Config.m_AvoidKeepDirection && Decision->m_Direction != Input.m_Direction)
		{
			const auto Preferred = CBlatantPlanner::KeepDirectionIfSafe(*pSimulation, *Decision, Input.m_Direction, Settings.m_CheckTicks);
			if(Preferred.m_Direction == Input.m_Direction)
			{
				m_Diagnostic.m_KeptDirection = true;
				Decision = Preferred;
				DecisionLife = CBlatantPlan::SAFE;
			}
		}
		const bool MayFilterHook = !ManualHook && (Input.m_Hook == 0 || m_OwnsHook);
		if(g_Config.m_AvoidEmergencyHook && MayFilterHook)
		{
			// Also retire a previously injected hook when the search has no result.
			auto Proposed = Decision.value_or(Input);
			const auto Filtered = CBlatantPlanner::FilterEmergencyHook(*pSimulation, Proposed, Settings.m_CheckTicks, ManualHook);
			m_Diagnostic.m_FilteredHook = Filtered.m_Hook != Proposed.m_Hook;
			if(Decision || Filtered.m_Hook != Input.m_Hook)
			{
				if(!Decision && Filtered.m_Hook != Input.m_Hook)
					DecisionLife = CBlatantPlan::SAFE;
				Decision = Filtered;
			}
		}
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
			const auto *pLocal = pGameClient->m_PredictedWorld.GetCharacterById(ClientId);
			if(!pLocal)
				pLocal = pAuthoritative;
			const vec2 Velocity = pLocal ? pLocal->Core()->m_Vel : vec2(0, 0);
			const vec2 HookPos = pLocal ? pLocal->Core()->m_HookPos : vec2(0, 0);
			const int HookState = pLocal ? pLocal->Core()->m_HookState : -1;
			dbg_msg("avoid", "cid=%d pos=%.0f,%.0f frozen=%d decision=%d dir=%d hook=%d jump=%d before_dir=%d before_hook=%d manual_hook=%d plan=%d took=%lldus life=%d tick=%d vel=%.2f,%.2f hook_state=%d hook_pos=%.1f,%.1f aim=%d,%d->%d,%d check=%d kick=%d before_jump=%d manual_jump=%d",
				ClientId, Pos.x, Pos.y, WasFrozen ? 1 : 0, Decision.has_value() ? 1 : 0,
				Decision ? Decision->m_Direction : 0, Decision ? Decision->m_Hook : 0, Decision ? Decision->m_Jump : 0,
				Input.m_Direction, Input.m_Hook, ManualHook ? 1 : 0, (int)m_Planner.LastPlan().size(), (long long)Elapsed,
				DecisionLife, pGameClient->m_PredictedWorld.GameTick(), Velocity.x, Velocity.y, HookState, HookPos.x, HookPos.y,
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
