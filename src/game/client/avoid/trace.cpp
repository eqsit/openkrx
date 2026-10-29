#include "trace.h"

#include "avoid.h"

#include <base/dbg.h>
#include <base/fs.h>
#include <base/io.h>
#include <base/process.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/jsonwriter.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/projectile.h>
#include <game/client/projectile_data.h>
#include <game/collision.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace OpenKrx
{
	namespace
	{
		void Int(CJsonWriter &J, const char *pKey, int Value)
		{
			J.WriteAttribute(pKey);
			J.WriteIntValue(Value);
		}
		void Str(CJsonWriter &J, const char *pKey, const char *pValue)
		{
			J.WriteAttribute(pKey);
			J.WriteStrValue(pValue);
		}
		void Point(CJsonWriter &J, const char *pKey, vec2 Value)
		{
			J.WriteAttribute(pKey);
			J.BeginArray();
			J.WriteIntValue(round_to_int(Value.x * 1000));
			J.WriteIntValue(round_to_int(Value.y * 1000));
			J.EndArray();
		}
		void Input(CJsonWriter &J, const char *pKey, const CNetObj_PlayerInput &In)
		{
			J.WriteAttribute(pKey);
			J.BeginObject();
			Int(J, "direction", In.m_Direction);
			Int(J, "jump", In.m_Jump);
			Int(J, "hook", In.m_Hook);
			Int(J, "target_x", In.m_TargetX);
			Int(J, "target_y", In.m_TargetY);
			Int(J, "fire", In.m_Fire);
			Int(J, "wanted_weapon", In.m_WantedWeapon);
			Int(J, "next_weapon", In.m_NextWeapon);
			Int(J, "prev_weapon", In.m_PrevWeapon);
			Int(J, "player_flags", In.m_PlayerFlags);
			J.EndObject();
		}
		// Preserve quoted whitespace and backslash escapes, remove formatting only.
		std::string Compact(CJsonStringWriter &Writer)
		{
			auto Json = Writer.GetOutputString();
			std::string Result;
			Result.reserve(Json.size());
			bool Quoted = false, Escaped = false;
			for(char Ch : Json)
			{
				if(Quoted)
				{
					Result += Ch;
					if(Escaped)
						Escaped = false;
					else if(Ch == '\\')
						Escaped = true;
					else if(Ch == '"')
						Quoted = false;
				}
				else if(Ch == '"')
				{
					Quoted = true;
					Result += Ch;
				}
				else if(Ch != ' ' && Ch != '\n' && Ch != '\r' && Ch != '\t')
					Result += Ch;
			}
			return Result;
		}
		void Character(CJsonWriter &J, const CCharacter *pCharacter)
		{
			if(!pCharacter)
			{
				J.WriteNullValue();
				return;
			}
			const auto *p = pCharacter->Core();
			J.BeginObject();
			Point(J, "pos", p->m_Pos);
			Point(J, "vel", p->m_Vel);
			Point(J, "hook_pos", p->m_HookPos);
			Point(J, "hook_dir", p->m_HookDir);
			Int(J, "hook_state", p->m_HookState);
			Int(J, "hook_tick", p->m_HookTick);
			Int(J, "hooked_player", p->HookedPlayer());
			Int(J, "jumped", p->m_Jumped);
			Int(J, "jumped_total", p->m_JumpedTotal);
			Int(J, "jumps", p->m_Jumps);
			Int(J, "direction", p->m_Direction);
			Int(J, "freeze_time", pCharacter->m_FreezeTime);
			Int(J, "freeze_start", p->m_FreezeStart);
			Int(J, "freeze_end", p->m_FreezeEnd);
			Int(J, "deep_frozen", p->m_DeepFrozen);
			Int(J, "live_frozen", p->m_LiveFrozen);
			Int(J, "local", pCharacter->m_IsLocal);
			Int(J, "in_freeze_tile", p->m_IsInFreeze);
			Int(J, "solo", p->m_Solo);
			Int(J, "collision_disabled", p->m_CollisionDisabled);
			Int(J, "hook_hit_disabled", p->m_HookHitDisabled);
			Int(J, "grenade_hit_disabled", p->m_GrenadeHitDisabled);
			Int(J, "endless_jump", p->m_EndlessJump);
			Int(J, "endless_hook", p->m_EndlessHook);
			Int(J, "super", p->m_Super);
			J.WriteAttribute("tuning_x100");
			J.BeginObject();
			for(int i = 0; i < CTuningParams::Num(); ++i)
				Int(J, CTuningParams::Name(i), p->m_Tuning.NetworkArray()[i]);
			J.EndObject();
			Int(J, "weapon", pCharacter->GetActiveWeapon());
			Int(J, "reload", pCharacter->DiagnosticReloadTimer());
			Int(J, "attack_tick", pCharacter->GetAttackTick());
			Int(J, "queued_weapon", pCharacter->DiagnosticQueuedWeapon());
			Int(J, "last_weapon", pCharacter->GetLastWeapon());
			Int(J, "num_inputs", pCharacter->DiagnosticNumInputs());
			Input(J, "current_input", pCharacter->DiagnosticCurrentInput());
			Input(J, "latest_previous_input", pCharacter->DiagnosticLatestPreviousInput());
			Int(J, "tile", pCharacter->m_TileIndex);
			Int(J, "front_tile", pCharacter->m_TileFIndex);
			Input(J, "saved_input", pCharacter->DiagnosticSavedInput());
			Input(J, "latest_input", pCharacter->DiagnosticLatestInput());
			J.WriteAttribute("weapons");
			J.BeginArray();
			for(const auto &Weapon : p->m_aWeapons)
			{
				J.BeginArray();
				J.WriteIntValue(Weapon.m_Got);
				J.WriteIntValue(Weapon.m_Ammo);
				J.EndArray();
			}
			J.EndArray();
			J.EndObject();
		}
	}

	CAvoidTrace::~CAvoidTrace() { Finish("shutdown"); }
	void CAvoidTrace::Write(const std::string &Line)
	{
		if(!m_File || m_WriteFailed)
			return;
		const auto Written = io_write(m_File, Line.data(), (unsigned)Line.size());
		if(Written != Line.size() || io_write(m_File, "\n", 1) != 1 || io_error(m_File))
		{
			dbg_msg("avoid-trace", "Report write failed (disk full or I/O error)");
			m_WriteFailed = true;
		}
	}
	void CAvoidTrace::Finish(const char *pReason)
	{
		if(!m_File)
			return;
		CJsonStringWriter J;
		J.BeginObject();
		Str(J, "type", "end");
		Str(J, "reason", pReason);
		J.EndObject();
		Write(Compact(J));
		if(io_close(m_File) != 0)
			dbg_msg("avoid-trace", "Report close failed");
		m_File = nullptr;
	}
	void CAvoidTrace::Reset()
	{
		Finish("context_reset");
		m_Frames.clear();
		m_WasFrozen = false;
		m_ClientId = m_Slot = -1;
	}
	void CAvoidTrace::Begin(const char *pReason, int64_t NowUs)
	{
		Finish("new_report");
		if(m_Frames.empty() || m_Directory.empty())
			return;
		char aStamp[64], aFile[IO_MAX_PATH_LENGTH];
		str_timestamp(aStamp, sizeof(aStamp));
		str_format(aFile, sizeof(aFile), "%s/avoid-%s-%d-%u-%s.jsonl", m_Directory.c_str(), aStamp, process_id(), ++m_Sequence, pReason);
		if(fs_makedir_rec_for(aFile) != 0 || !(m_File = io_open(aFile, IOFLAG_WRITE)))
		{
			dbg_msg("avoid-trace", "Could not create report: %s", aFile);
			return;
		}
		m_WriteFailed = false;
		CJsonStringWriter J;
		J.BeginObject();
		Str(J, "type", "header");
		Int(J, "schema", 1);
		Str(J, "event", pReason);
		Str(J, "timestamp", aStamp);
		Int(J, "coordinate_scale", 1000);
		Int(J, "history_limit_frames", MAX_FRAMES);
		Int(J, "history_limit_ms", HISTORY_US / 1000);
		Str(J, "notice", "Read-only observations; emitted input is not proof of network delivery; SAFE=9999 applies to the recorded branch horizon only; projectile lists are capped, not a replay.");
		J.EndObject();
		Write(Compact(J));
		for(const auto &Frame : m_Frames)
			Write(Frame.m_Json);
		if(io_flush(m_File) != 0)
			dbg_msg("avoid-trace", "Report flush failed");
		m_PostUntilUs = NowUs + 1000000;
		m_PostFrames = 0;
		dbg_msg("avoid-trace", "Saved %s report (%d history frames, collecting 1s after event): %s", pReason, (int)m_Frames.size(), aFile);
	}
	void CAvoidTrace::Dump()
	{
		if(m_Frames.empty())
			dbg_msg("avoid-trace", "No history: enable openkrx_avoid_debug 1 and enter the game first");
		else
			Begin("manual", m_Frames.back().m_TimeUs);
	}
	void CAvoidTrace::Observe(const std::string &Directory, std::string Frame, int64_t NowUs, bool Frozen)
	{
		m_Directory = Directory;
		m_Frames.push_back({NowUs, std::move(Frame)});
		while(m_Frames.size() > MAX_FRAMES || NowUs - m_Frames.front().m_TimeUs > HISTORY_US)
			m_Frames.pop_front();
		const bool NewFreeze = Frozen && !m_WasFrozen;
		m_WasFrozen = Frozen;
		if(m_File)
		{
			Write(m_Frames.back().m_Json);
			if(NowUs >= m_PostUntilUs || ++m_PostFrames >= 128)
				Finish("post_window_complete");
		}
		// If this transition overlaps a previous post-window, start a distinct event.
		if(NewFreeze)
			Begin("freeze", NowUs);
	}

	void CAvoidTrace::Record(CGameClient *pClient, const CAvoid &Avoid, const CNetObj_PlayerInput &Raw,
		const CNetObj_PlayerInput &AfterAvoid, const CNetObj_PlayerInput &Final, bool Emitted, bool PipelineRan, int PipelineUs)
	{
		const auto Start = std::chrono::steady_clock::now();
		const auto NowUs = std::chrono::duration_cast<std::chrono::microseconds>(Start.time_since_epoch()).count();
		const int Slot = g_Config.m_ClDummy, Cid = pClient->m_aLocalIds[Slot];
		if(Cid < 0 || Cid >= MAX_CLIENTS || pClient->Client()->State() != IClient::STATE_ONLINE || pClient->m_Snap.m_SpecInfo.m_Active)
		{
			Reset();
			return;
		}
		if(m_ClientId != Cid || m_Slot != Slot)
			Reset();
		m_ClientId = Cid;
		m_Slot = Slot;
		const auto &Snapshot = pClient->m_aClients[Cid];
		const auto *pAuth = pClient->m_GameWorld.GetCharacterById(Cid);
		const auto *pPred = pClient->m_PredictedWorld.GetCharacterById(Cid);
		const int Tick = pClient->Client()->GameTick(Slot);
		// Snapshot flags are independent of the KRX frozen gate (which can differ
		// on FNG or servers with unexpected names). Never trigger on prediction alone.
		const bool Frozen = Snapshot.m_DeepFrozen || Snapshot.m_LiveFrozen || Snapshot.m_FreezeEnd > Tick ||
				    (pAuth && (pAuth->m_FreezeTime != 0 || pAuth->Core()->m_DeepFrozen || pAuth->Core()->m_LiveFrozen));
		CJsonStringWriter J;
		J.BeginObject();
		Str(J, "type", "frame");
		char aTime[32];
		str_format(aTime, sizeof(aTime), "%lld", (long long)NowUs);
		Str(J, "monotonic_us", aTime);
		Int(J, "dt_us", m_Frames.empty() ? 0 : (int)std::min<int64_t>(NowUs - m_Frames.back().m_TimeUs, 2147483647));
		Int(J, "cid", Cid);
		Int(J, "slot", Slot);
		Int(J, "game_tick", Tick);
		Int(J, "pred_tick", pClient->Client()->PredGameTick(Slot));
		Int(J, "requested_input_tick", pClient->Client()->PredGameTick(Slot));
		Int(J, "prediction_time_ms", pClient->Client()->GetPredictionTime());
		Int(J, "configured_prediction_margin_ms", g_Config.m_ClPredictionMargin);
		const auto InputTiming = pClient->Client()->GetInputTiming(Slot);
		Int(J, "server_input_timing_valid", InputTiming.m_ReceiptAgeMs >= 0);
		Int(J, "server_input_timing_tick", InputTiming.m_IntendedTick);
		Int(J, "server_input_time_left_ms", InputTiming.m_ServerTimeLeftMs);
		Int(J, "server_input_timing_age_ms", InputTiming.m_ReceiptAgeMs);
		Int(J, "server_input_timing_local_match", InputTiming.m_MatchedLocalInput);
		Int(J, "game_tick_speed", pClient->Client()->GameTickSpeed());
		Int(J, "pred_intra_tick_x1000000", round_to_int(pClient->Client()->PredIntraGameTick(Slot) * 1000000));
		Int(J, "pipeline_us", PipelineUs);
		Int(J, "auth_world_tick", pClient->m_GameWorld.GameTick());
		Int(J, "pred_world_tick", pClient->m_PredictedWorld.GameTick());
		Int(J, "prediction_lead_ticks", pClient->Client()->PredGameTick(Slot) - Tick);
		Int(J, "latency_snapshot_ms", pClient->m_Snap.m_apPlayerInfos[Cid] ? pClient->m_Snap.m_apPlayerInfos[Cid]->m_Latency : -1);
		Int(J, "snapshot_freeze_end", Snapshot.m_FreezeEnd);
		Int(J, "observed_freeze", Frozen);
		Int(J, "krx_frozen_gate", IsKrxCharacterFrozen(pClient, Cid));
		Int(J, "snap_input_emitted", Emitted);
		Int(J, "input_pipeline_ran", PipelineRan);
		Int(J, "physical_jump", pClient->m_Controls.m_PhysicalJump);
		Input(J, "raw", Raw);
		Input(J, "after_avoid", AfterAvoid);
		Input(J, "after_rocket", Final);
		CAvoid::CDiagnostic Skipped;
		Skipped.m_pGate = "input_pipeline_skipped";
		const auto &D = PipelineRan ? Avoid.Diagnostic() : Skipped;
		const CBlatantDiagnostic EmptyDiagnostic;
		const auto &Plan = str_comp(D.m_pGate, "evaluated") == 0 ? Avoid.Planner().Diagnostic() : EmptyDiagnostic;
		Str(J, "gate", D.m_pGate);
		Str(J, "planner_reason", Plan.m_pReason);
		Int(J, "manual_hook", D.m_ManualHook);
		Int(J, "owned_hook_before", D.m_OwnedHookBefore);
		Int(J, "owned_hook_after", D.m_OwnedHookAfter);
		Int(J, "kept_direction", D.m_KeptDirection);
		Int(J, "filtered_hook", D.m_FilteredHook);
		Int(J, "pending_automatic_fire", D.m_PendingAutomaticFire);
		Int(J, "allow_immediate_auto_shot", D.m_AllowImmediateShot);
		Int(J, "decision_life", D.m_Life);
		Int(J, "planner_us", D.m_ElapsedUs);
		Int(J, "constant_life", Plan.m_ConstantLife);
		Int(J, "greedy_life", Plan.m_GreedyLife);
		Int(J, "nsif_tail_length", Plan.m_TailLength);
		Int(J, "nsif_tail_life", Plan.m_TailLife);
		Int(J, "stored_plan_length", Avoid.Planner().LastPlan().size());
		if(D.m_Proposed)
			Input(J, "planner_input", *D.m_Proposed);
		else
		{
			J.WriteAttribute("planner_input");
			J.WriteNullValue();
		}
		Int(J, "selected_candidate", Plan.m_Selected);
		Int(J, "candidate_count", Plan.m_vCandidates.size());
		J.WriteAttribute("candidates");
		J.BeginArray();
		for(size_t i = 0; i < Plan.m_vCandidates.size(); ++i)
		{
			if(i >= 128 && (int)i != Plan.m_Selected)
				continue;
			const auto &C = Plan.m_vCandidates[i];
			J.BeginObject();
			Int(J, "index", i);
			Input(J, "input", C.m_Input);
			Int(J, "life", C.m_Life);
			Int(J, "plan_length", C.m_Length);
			J.EndObject();
		}
		J.EndArray();
		Int(J, "candidate_capture_limit", 128);
		J.WriteAttribute("stored_plan_prefix");
		J.BeginArray();
		const auto &Saved = Avoid.Planner().LastPlan();
		for(size_t i = 0; i < Saved.size() && i < 64; ++i)
		{
			J.BeginObject();
			Input(J, "input", Saved[i]);
			J.EndObject();
		}
		J.EndArray();
		Int(J, "stored_plan_capture_limit", 64);
		if(Avoid.Planner().TrackPoint())
			Point(J, "track_point", *Avoid.Planner().TrackPoint());
		J.WriteAttribute("authoritative");
		Character(J, pAuth);
		J.WriteAttribute("predicted");
		Character(J, pPred);
		const auto Settings = SettingsFromConfig();
		J.WriteAttribute("settings");
		J.BeginObject();
		Int(J, "enabled", g_Config.m_AvoidFreeze);
		Int(J, "agent", g_Config.m_AvoidAgentType);
		Int(J, "check", Settings.m_CheckTicks);
		Int(J, "kick", Settings.m_KickInTicks);
		Int(J, "nsif", Settings.m_Nsif);
		Int(J, "player_prediction", Settings.m_PlayerPrediction);
		Int(J, "direction", Settings.m_Direction);
		Int(J, "hook", Settings.m_Hook);
		Int(J, "death", Settings.m_Death);
		Int(J, "teles", Settings.m_Teles);
		Int(J, "unfreeze", Settings.m_Unfreeze);
		Int(J, "unfreeze_ticks", Settings.m_UnfreezeTicks);
		Int(J, "track", Settings.m_TrackPoint);
		Int(J, "safe_track", Settings.m_SafeTracking);
		Int(J, "aimbot", Settings.m_Aimbot);
		Int(J, "fov", Settings.m_Fov);
		Int(J, "segments", Settings.m_Segments);
		Int(J, "aim_assist", Settings.m_AimAssist);
		Int(J, "auto_aim", Settings.m_AutoAim);
		Int(J, "auto_drag", Settings.m_AutoDrag);
		Int(J, "emergency_hook", g_Config.m_AvoidEmergencyHook);
		Int(J, "keep_direction", g_Config.m_AvoidKeepDirection);
		Int(J, "rocket_enabled", g_Config.m_KrxGrenadeSaver);
		Int(J, "rocket_preserve_jump", g_Config.m_RocketPreserveJump);
		Int(J, "rocket_pulse_prediction", g_Config.m_AvoidRocketPulsePrediction);
		const auto &W = pClient->m_PredictedWorld.m_WorldConfig;
		Int(J, "predict_tiles", W.m_PredictTiles);
		Int(J, "predict_freeze", W.m_PredictFreeze);
		Int(J, "predict_weapons", W.m_PredictWeapons);
		Int(J, "predict_ddrace", W.m_PredictDDRace);
		Int(J, "fng", W.m_IsFNG);
		Int(J, "ddrace", W.m_IsDDRace);
		Int(J, "solo", W.m_IsSolo);
		Int(J, "tune_zones", W.m_UseTuneZones);
		J.EndObject();
		const auto &Server = pClient->Client()->ServerInfo();
		Str(J, "map", Server.m_aMap);
		Str(J, "server_name", Server.m_aName);
		Str(J, "game_type", Server.m_aGameType);
		const vec2 Pos = pPred ? pPred->Core()->m_Pos : pAuth ? pAuth->Core()->m_Pos :
									vec2(0, 0);
		J.WriteAttribute("nearby_predicted_characters");
		J.BeginArray();
		int NearbyCount = 0, Captured = 0;
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			const auto *p = pClient->m_PredictedWorld.GetCharacterById(i);
			if(!p || i == Cid || (!p->m_IsLocal && distance(p->Core()->m_Pos, Pos) > 800))
				continue;
			++NearbyCount;
			// Always preserve the other local tee even if 16 remote tees precede it.
			if(Captured >= 16 && !p->m_IsLocal)
				continue;
			++Captured;
			J.BeginObject();
			Int(J, "cid", i);
			J.WriteAttribute("state");
			Character(J, p);
			J.EndObject();
		}
		J.EndArray();
		Int(J, "nearby_character_count", NearbyCount);
		Int(J, "nearby_character_captured", Captured);
		J.WriteAttribute("predicted_projectiles");
		J.BeginArray();
		int ProjectileCount = 0;
		for(auto *pEntity = pClient->m_PredictedWorld.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pEntity; pEntity = pEntity->TypeNext())
		{
			auto *pProjectile = static_cast<CProjectile *>(pEntity);
			++ProjectileCount;
			if(ProjectileCount > 32)
				continue;
			const auto Data = pProjectile->GetData();
			J.BeginObject();
			Int(J, "owner", pProjectile->GetOwner());
			Int(J, "type", Data.m_Type);
			Int(J, "start_tick", Data.m_StartTick);
			Int(J, "explosive", Data.m_Explosive);
			Int(J, "freeze", Data.m_Freeze);
			Int(J, "bouncing", Data.m_Bouncing);
			Int(J, "tune_zone", Data.m_TuneZone);
			Point(J, "start_pos", Data.m_StartPos);
			Point(J, "direction", pProjectile->GetDirection());
			Point(J, "pos", pProjectile->GetPos((pClient->m_PredictedWorld.GameTick() - Data.m_StartTick) / 50.0f));
			J.EndObject();
		}
		J.EndArray();
		Int(J, "projectile_count", ProjectileCount);
		Int(J, "projectile_capture_limit", 32);
		const int TileX = (int)std::floor(Pos.x / 32), TileY = (int)std::floor(Pos.y / 32);
		J.WriteAttribute("tiles_7x7");
		J.BeginObject();
		Int(J, "origin_x", TileX - 3);
		Int(J, "origin_y", TileY - 3);
		Str(J, "cell_format", "row-major [game,front,switch_type,switch_number,tune]; null=outside map");
		J.WriteAttribute("cells");
		J.BeginArray();
		const auto *pCollision = pClient->Collision();
		for(int y = TileY - 3; y <= TileY + 3; ++y)
			for(int x = TileX - 3; x <= TileX + 3; ++x)
			{
				if(x < 0 || y < 0 || x >= pCollision->GetWidth() || y >= pCollision->GetHeight())
				{
					J.WriteNullValue();
					continue;
				}
				const int Index = y * pCollision->GetWidth() + x;
				J.BeginArray();
				J.WriteIntValue(pCollision->GetTileIndex(Index));
				J.WriteIntValue(pCollision->GetFrontTileIndex(Index));
				J.WriteIntValue(pCollision->GetSwitchType(Index));
				J.WriteIntValue(pCollision->GetSwitchNumber(Index));
				J.WriteIntValue(pCollision->IsTune(Index));
				J.EndArray();
			}
		J.EndArray();
		J.EndObject();
		const int CaptureUs = (int)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - Start).count();
		Int(J, "capture_build_us", CaptureUs);
		J.EndObject();
		char aDirectory[IO_MAX_PATH_LENGTH];
		pClient->Storage()->GetCompletePath(IStorage::TYPE_SAVE, "dumps/openkrx-avoid", aDirectory, sizeof(aDirectory));
		Observe(aDirectory, Compact(J), NowUs, Frozen);
		const auto TotalUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - Start).count();
		if(TotalUs > 10000)
			dbg_msg("avoid-trace", "Slow capture: %lldus (freeze=%d, includes report I/O)", (long long)TotalUs, Frozen);
	}
}
