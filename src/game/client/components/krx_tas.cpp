#include "krx_tas.h"

#include <base/io.h>
#include <base/log.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/console.h>
#include <engine/http.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>

namespace OpenKrx
{
	namespace
	{
		bool Frozen(const CCharacter *pCharacter)
		{
			return pCharacter && (pCharacter->m_FreezeTime > 0 || pCharacter->Core()->m_DeepFrozen);
		}
		std::shared_ptr<CGameWorld> Snapshot(const CGameWorld &World)
		{
			auto pCopy = std::make_shared<CGameWorld>();
			pCopy->CopyWorldIsolated(&World);
			return pCopy;
		}
	}

	bool CKrxTas::Active() const { return g_Config.m_KrxRecordReplay && m_pWorld != nullptr; }
	CKrxTas::~CKrxTas()
	{
		CancelPathFinding();
		StopVaultWorker();
	}
	CGameWorld *CKrxTas::RenderWorld() { return Active() ? m_pWorld.get() : &GameClient()->m_PredictedWorld; }
	CGameWorld *CKrxTas::PreviousRenderWorld() { return Active() ? m_pPreviousWorld.get() : &GameClient()->m_PrevPredictedWorld; }

	float CKrxTas::IntraTick() const
	{
		const int Tps = g_Config.m_KrxTasRewind || g_Config.m_KrxTasForward ? 50 : g_Config.m_KrxTasTps;
		const float Interval = (float)(int)(1000.0f / Tps);
		const float Milliseconds = (float)((time_get_nanoseconds().count() - m_LastStep) / 1000000);
		const float Intra = std::clamp(Milliseconds, 0.0f, Interval) / Interval;
		return g_Config.m_KrxTasRewind ? -Intra : Intra;
	}

	void CKrxTas::OnShutdown()
	{
		CancelPathFinding();
		StopVaultWorker();
		if(m_pVaultRequest && !m_pVaultRequest->Done())
			m_pVaultRequest->Abort();
	}

	void CKrxTas::Message(const char *pMessage)
	{
		m_Status = pMessage;
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "TAS", pMessage);
		if(!str_startswith(pMessage, "record="))
			GameClient()->m_KrxMiscPreTas.Notify(0, "TAS", pMessage);
	}

	void CKrxTas::OnConsoleInit()
	{
		Console()->Register("openkrx_tas_status", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) {
		auto *pSelf = static_cast<CKrxTas *>(pUser);
		auto *pGameClient = pSelf->GameClient();
		const int Slot = g_Config.m_ClDummy;
		const int Id = pGameClient->m_aLocalIds[Slot];
		auto *pLive = pGameClient->m_GameWorld.GetCharacterById(Id);
		auto *pTas = pSelf->m_pWorld ? pSelf->m_pWorld->GetCharacterById(Id) : nullptr;
		const vec2 Live = pLive ? pLive->Core()->m_Pos : vec2(0, 0), Tas = pTas ? pTas->Core()->m_Pos : vec2(0, 0);
		char aLine[1024];
		str_format(aLine, sizeof(aLine), "record=%d pause=%d frames=%d dummy_frames=%d playback=%d tas_tick=%d tas_pos=%.6f,%.6f live_pos=%.6f,%.6f flow=%d fent=%d", g_Config.m_KrxRecordReplay, g_Config.m_KrxTasPause, (int)pSelf->m_Replay.m_aFrames[0].size(), (int)pSelf->m_Replay.m_aFrames[1].size(), pSelf->m_PlaybackTick, pSelf->m_pWorld ? pSelf->m_pWorld->GameTick() : -1, Tas.x, Tas.y, Live.x, Live.y, pSelf->m_FlowField.Ready(), pGameClient->m_Fent.Running());
		pSelf->Message(aLine);
			int Tunnels = 0;
			for(auto Value : pSelf->m_vTunnels) Tunnels += Value != 0;
			str_format(aLine, sizeof(aLine), "tas_detail frozen=%d deep=%d id0=%d id1=%d goal=%.1f,%.1f tunnels=%d intra=%.4f", pTas ? pTas->m_FreezeTime : 0, pTas ? pTas->Core()->m_DeepFrozen : 0, pSelf->m_aIds[0], pSelf->m_aIds[1], pSelf->m_Goal.x, pSelf->m_Goal.y, Tunnels, pSelf->IntraTick());
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "TAS", aLine); }, this, "Inspect current TAS and Fent state");
		Console()->Register("krx_clearreplay", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) { static_cast<CKrxTas *>(pUser)->ClearReplay(); }, this, "Clear current TAS replay");
		Console()->Register("krx_deletereplay", "s[name]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUser) { static_cast<CKrxTas *>(pUser)->DeleteReplay(pResult->GetString(0)); }, this, "Delete selected local replay");
		Console()->Register("krx_validatereplay", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) { static_cast<CKrxTas *>(pUser)->ValidateReplay(); }, this, "Validate replay positions with prediction physics");
		Console()->Register("krx_tasaddfakeaim", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) { static_cast<CKrxTas *>(pUser)->AddFakeAim(); }, this, "Add fake aim to current replay");
		Console()->Register("krx_getreplaytime", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) { static_cast<CKrxTas *>(pUser)->GetReplayTime(); }, this, "Compute race finish time of current replay");
		Console()->Register("krx_convertreplays", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) { static_cast<CKrxTas *>(pUser)->ConvertLegacyReplays(); }, this, "Convert legacy JSON replays to TAS");
		Console()->Register("krx_syncvault", "", CFGFLAG_CLIENT, [](IConsole::IResult *, void *pUser) { static_cast<CKrxTas *>(pUser)->SyncVault(); }, this, "Synchronize the original KRX replay vault");
	}

	void CKrxTas::OnReset()
	{
		CancelPathFinding();
		ClearReplay();
		m_pWorld.reset();
		m_pPreviousWorld.reset();
		m_pInitialWorld.reset();
		m_WasRecording = m_PlaybackWasActive = false;
		m_RewindHeld = m_ForwardHeld = false;
		m_aIds = {-1, -1};
		m_aReplayInputSlots = {0, 1};
		m_LastReplayTimeTicks = 0;
		g_Config.m_KrxRecordReplay = g_Config.m_KrxLoadReplay = 0;
		g_Config.m_KrxTasRewind = g_Config.m_KrxTasForward = g_Config.m_KrxTasRespawn = 0;
		m_LastPlaybackGameTick = -1;
	}

	void CKrxTas::OnMapLoad()
	{
		CancelPathFinding();
		m_FlowField.Resize(Collision()->GetWidth(), Collision()->GetHeight());
		m_vTunnels.assign((size_t)Collision()->GetWidth() * Collision()->GetHeight(), 0);
		m_Goal = m_LastBrush = vec2(0, 0);
		m_NoFinish = false;
		m_PathDirty = true;
		RefreshFiles();
	}

	void CKrxTas::ClearReplay()
	{
		m_Replay = {};
		m_vWorlds.clear();
		m_PlaybackTick = 0;
		m_StartTick = m_FinishTick = 0;
		m_ReplayFinishTicks = 0;
		m_OverwriteName.clear();
		g_Config.m_KrxLoadReplay = 0;
	}

	void CKrxTas::InitializeWorld(const CGameWorld &Source, bool PredictPlayers)
	{
		m_pWorld = std::make_unique<CGameWorld>();
		m_pPreviousWorld = std::make_unique<CGameWorld>();
		m_pInitialWorld = std::make_unique<CGameWorld>();
		m_pWorld->CopyWorldIsolated(&Source);
		m_pWorld->m_WorldConfig.m_KrxPrediction = true;
		m_pWorld->m_WorldConfig.m_KrxExplosionStrengthValid = true;
		m_pWorld->m_WorldConfig.m_KrxExplosionStrength = GameClient()->m_aTuning[g_Config.m_ClDummy].m_ExplosionStrength;
		if(!PredictPlayers)
			for(int Id = 0; Id < LEGACY_MAX_CLIENTS; ++Id)
				if(Id != m_aIds[0] && Id != m_aIds[1])
					if(auto *pCharacter = m_pWorld->GetCharacterById(Id))
						m_pWorld->RemoveCharacter(pCharacter);
		m_pPreviousWorld->CopyWorldIsolated(m_pWorld.get());
		m_pInitialWorld->CopyWorldIsolated(m_pWorld.get());
		m_LastStep = 0;
		m_LastAutoSave = time_get();
	}

	void CKrxTas::AppendFrame(const std::array<CNetObj_PlayerInput, 2> &aInputs)
	{
		auto pWorld = Snapshot(*m_pWorld);
		for(int Slot = 0; Slot < 2; ++Slot)
			if(auto *pCharacter = m_pWorld->GetCharacterById(m_aIds[Slot]))
				if(pCharacter->Core()->m_Pos != vec2(0, 0))
					m_Replay.m_aFrames[Slot].push_back({aInputs[m_aReplayInputSlots[Slot]], pCharacter->Core()->m_Pos});
		m_vWorlds.push_back(std::move(pWorld));
	}

	void CKrxTas::Step(const std::array<CNetObj_PlayerInput, 2> &aInputs, bool Record, bool Events, int InputCount, unsigned InputMask)
	{
		if(!m_pWorld)
			return;
		m_pPreviousWorld->CopyWorldIsolated(m_pWorld.get());
		for(int Slot = 0; Slot < 2; ++Slot)
			if(m_aReplayInputSlots[Slot] < InputCount && (InputMask & (1u << m_aReplayInputSlots[Slot])))
				if(auto *pCharacter = m_pWorld->GetCharacterById(m_aIds[Slot]))
					pCharacter->OnDirectInput(&aInputs[m_aReplayInputSlots[Slot]]);
		++m_pWorld->m_GameTick;
		for(int Slot = 0; Slot < 2; ++Slot)
			if(m_aReplayInputSlots[Slot] < InputCount && (InputMask & (1u << m_aReplayInputSlots[Slot])))
				if(auto *pCharacter = m_pWorld->GetCharacterById(m_aIds[Slot]))
					pCharacter->OnPredictedInput(&aInputs[m_aReplayInputSlots[Slot]]);
		m_pWorld->Tick();
		for(int Slot = 0; Slot < 2; ++Slot)
		{
			if(m_aReplayInputSlots[Slot] >= InputCount || !(InputMask & (1u << m_aReplayInputSlots[Slot])))
				continue;
			auto *pCharacter = m_pWorld->GetCharacterById(m_aIds[Slot]);
			if(!pCharacter)
				continue;
			auto vIndices = Collision()->GetMapIndices(pCharacter->m_PrevPos, pCharacter->Core()->m_Pos);
			vIndices.push_back(Collision()->GetPureMapIndex(pCharacter->Core()->m_Pos));
			for(int Index : vIndices)
			{
				const int Tile = Collision()->GetTileIndex(Index), Front = Collision()->GetFrontTileIndex(Index);
				if(Tile == TILE_START || Front == TILE_START)
					m_StartTick = m_pWorld->GameTick();
				if((Tile == TILE_FINISH || Front == TILE_FINISH) && m_StartTick && !m_FinishTick)
				{
					m_FinishTick = m_pWorld->GameTick();
					char aMessage[128];
					const float Seconds = (m_FinishTick - m_StartTick) / 50.0f;
					str_format(aMessage, sizeof(aMessage), "TAS: Finished in %d minute(s) %5.2f second(s).", (int)Seconds / 60, std::fmod(Seconds, 60.0f));
					Message(aMessage);
				}
			}
		}
		if(Record)
			AppendFrame(aInputs);
		if(Events)
			HandleWorldEvents();
		else
			m_pWorld->m_PredictedEvents.clear();
	}

	bool CKrxTas::ProcessInput()
	{
		if(Client()->State() != IClient::STATE_ONLINE)
			return false;
		const int Slot = g_Config.m_ClDummy;
		auto &Controls = GameClient()->m_Controls;
		const bool Recording = g_Config.m_KrxRecordReplay != 0;
		if(Recording && g_Config.m_KrxLoadReplay)
		{
			Message("TAS: Can't load, still recording!");
			g_Config.m_KrxLoadReplay = 0;
		}
		if(!Recording)
		{
			if(m_WasRecording)
			{
				Controls.m_aInputData[Slot].m_TargetX = m_NetworkBefore.m_TargetX;
				Controls.m_aInputData[Slot].m_TargetY = m_NetworkBefore.m_TargetY;
				Controls.m_aMousePos[Slot] = vec2(m_NetworkBefore.m_TargetX, m_NetworkBefore.m_TargetY);
			}
			m_WasRecording = false;
			m_NetworkBefore = Controls.m_aInputData[Slot];
			if(!g_Config.m_KrxLoadReplay && g_Config.m_KrxTasAutoReplay && !m_Replay.m_aFrames[0].empty() &&
				((g_Config.m_KrxTasAutoReplayFreeze && Frozen(GameClient()->m_GameWorld.GetCharacterById(GameClient()->m_aLocalIds[Slot]))) || m_PlaybackTick >= (int)m_Replay.m_aFrames[0].size()))
			{
				m_PlaybackTick = 0;
				g_Config.m_KrxLoadReplay = 1;
				if(g_Config.m_KrxTasAutoReplayKill)
					GameClient()->SendKill();
				Message("TAS: Auto replay restarting!");
			}
			Playback();
			return false;
		}
		g_Config.m_KrxTasAutoReplay = 0;
		// SnapInput's recording branch mirrors the inactive dummy's input
		// before the TAS world receives both slots (23ed20 onwards).
		Controls.m_aInputData[!Slot] = GameClient()->m_DummyInput;
		if(g_Config.m_ClDummyHammer)
		{
			auto &Hammer = GameClient()->m_HammerInput;
			if(GameClient()->m_DummyFire % 25 != 0)
				++GameClient()->m_DummyFire;
			++GameClient()->m_DummyFire;
			Hammer.m_Fire = (Hammer.m_Fire + 1) | 1;
			Hammer.m_WantedWeapon = WEAPON_HAMMER + 1;
			if(!g_Config.m_ClDummyRestoreWeapon)
				GameClient()->m_DummyInput.m_WantedWeapon = WEAPON_HAMMER + 1;
			const auto *pSource = m_pWorld ? m_pWorld.get() : &GameClient()->m_PredictedWorld;
			auto *pMain = const_cast<CGameWorld *>(pSource)->GetCharacterById(GameClient()->m_aLocalIds[Slot]);
			auto *pDummy = const_cast<CGameWorld *>(pSource)->GetCharacterById(GameClient()->m_aLocalIds[!Slot]);
			if(pMain && pDummy)
			{
				const vec2 Aim = pMain->Core()->m_Pos - pDummy->Core()->m_Pos;
				Hammer.m_TargetX = (int)Aim.x;
				Hammer.m_TargetY = (int)Aim.y;
				Controls.m_aInputData[!Slot] = Hammer;
			}
		}
		if(!m_pWorld || m_Replay.m_aFrames[0].size() < 51 || g_Config.m_KrxTasRespawn)
		{
			m_aIds = {GameClient()->m_aLocalIds[0], GameClient()->m_aLocalIds[1]};
			m_aReplayInputSlots = {0, 1};
			if(!GameClient()->m_GameWorld.GetCharacterById(m_aIds[Slot]))
				return false;
			if(!m_WasRecording)
				m_NetworkBefore = Controls.m_aInputData[Slot];
			ClearReplay();
			// Both reset entry points pass prediction=true in the original.
			InitializeWorld(GameClient()->m_GameWorld, true);
			m_Replay.m_Map = Client()->ServerInfo().m_aMap;
			m_Replay.m_Author = g_Config.m_PlayerName;
			m_Replay.m_Description = g_Config.m_KrxReplayFileDescription;
			for(auto &Input : Controls.m_aInputData)
				Input.m_Direction = Input.m_Jump = Input.m_Hook = 0;
			m_aInputs = {Controls.m_aInputData[0], Controls.m_aInputData[1]};
			for(int I = 0; I < 50; ++I)
				Step(m_aInputs, true, false);
			g_Config.m_KrxTasPause = 0;
			g_Config.m_KrxTasRespawn = 0;
			m_WasRecording = true;
		}
		m_WasRecording = true;
		m_aInputs = {Controls.m_aInputData[0], Controls.m_aInputData[1]};
		if(g_Config.m_KrxTasForward && (!m_ForwardHeld || !g_Config.m_KrxTickControlStep))
			Forward(g_Config.m_KrxTickControlTicks);
		if(g_Config.m_KrxTasRewind && (!m_RewindHeld || !g_Config.m_KrxTickControlStep))
			Rewind(g_Config.m_KrxTickControlTicks);
		m_RewindHeld = g_Config.m_KrxTasRewind != 0;
		m_ForwardHeld = g_Config.m_KrxTasForward != 0;
		if(g_Config.m_KrxTasAutoForward && !m_RewindHeld)
		{
			auto *pCharacter = m_pWorld->GetCharacterById(GameClient()->m_aLocalIds[Slot]);
			if(pCharacter && pCharacter->m_FreezeTime > 0)
			{
				CGameWorld Probe;
				Probe.CopyWorldIsolated(m_pWorld.get());
				const int MaxTicks = pCharacter->m_FreezeTime;
				int Ticks = 0;
				for(int I = 0; I < MaxTicks; ++I)
				{
					auto *pProbe = Probe.GetCharacterById(GameClient()->m_aLocalIds[Slot]);
					if(!pProbe)
						break;
					pProbe->OnDirectInput(&m_aInputs[Slot]);
					pProbe->OnPredictedInput(&m_aInputs[Slot]);
					Probe.Tick();
					if(pProbe->m_FreezeTime < 1 && !pProbe->Core()->m_DeepFrozen)
					{
						Ticks = I;
						break;
					}
				}
				Forward(Ticks);
			}
		}
		AutoRewind();
		const int64_t Now = time_get_nanoseconds().count();
		const bool Due = g_Config.m_KrxTasTps == 50 || (Now - m_LastStep) / 1000000 >= (int)(1000.0f / g_Config.m_KrxTasTps);
		if(Due && !g_Config.m_KrxTasPause && !g_Config.m_KrxTasRewind && !g_Config.m_KrxTasForward)
		{
			Step(m_aInputs, true, true);
			m_LastStep = Now;
		}
		if(g_Config.m_KrxAutoSaveReplayEnabled && time_get() - m_LastAutoSave >= (int64_t)g_Config.m_KrxAutoSaveReplayInterval * time_freq())
		{
			char aName[64];
			str_timestamp(aName, sizeof(aName));
			SaveReplay(aName, true);
			m_LastAutoSave = time_get();
		}
		return true;
	}

	CNetObj_PlayerInput CKrxTas::NetworkInput() const
	{
		auto Input = m_NetworkBefore;
		Input.m_Direction = Input.m_Jump = Input.m_Hook = 0;
		return Input;
	}

	void CKrxTas::Rewind(int Ticks)
	{
		const int Count = (int)m_vWorlds.size();
		if(!Active() || Ticks < 0 || Ticks >= Count)
			return;
		Ticks = std::min(Ticks, Count - 1);
		std::array<int, 2> aFire{};
		std::array<bool, 2> aRestoreFire{};
		if(Ticks > 0)
			for(int Slot = 0; Slot < 2; ++Slot)
				if(auto *pCharacter = m_vWorlds[Count - Ticks]->GetCharacterById(m_aIds[Slot]))
				{
					// 2f5890 reads the first removed snapshot before popping it,
					// using LatestInput.Fire (character+154), not target CurrentInput.
					aFire[Slot] = pCharacter->DiagnosticLatestInput().m_Fire;
					aRestoreFire[Slot] = true;
				}
		m_pWorld->CopyWorldIsolated(m_vWorlds[Count - Ticks - 1].get());
		m_pPreviousWorld->CopyWorldIsolated(m_pWorld.get());
		m_vWorlds.resize(Count - Ticks);
		for(auto &vFrames : m_Replay.m_aFrames)
			if(vFrames.size() >= (size_t)Ticks)
				vFrames.resize(vFrames.size() - Ticks);
		for(int Slot = 0; Slot < 2; ++Slot)
			if(aRestoreFire[Slot])
			{
				const int InputSlot = m_aReplayInputSlots[Slot];
				GameClient()->m_Controls.m_aInputData[InputSlot].m_Fire = aFire[Slot];
				if(InputSlot != g_Config.m_ClDummy)
				{
					GameClient()->m_DummyInput.m_Fire = aFire[Slot];
					GameClient()->m_HammerInput.m_Fire = aFire[Slot];
				}
			}
		m_FinishTick = 0;
		if(Ticks > 0)
			m_LastStep = time_get_nanoseconds().count();
		if(g_Config.m_KrxTickControlPause && Ticks > 0)
			g_Config.m_KrxTasPause = 1;
	}

	void CKrxTas::Forward(int Ticks)
	{
		if(!Active())
			return;
		for(int I = 0; I < Ticks; ++I)
		{
			// 2f1700 explicitly supplies one input, then records both characters.
			Step(m_aInputs, true, false, 1);
			m_LastStep = time_get_nanoseconds().count();
		}
		if(g_Config.m_KrxTickControlPause && Ticks > 0)
			g_Config.m_KrxTasPause = 1;
	}

	void CKrxTas::AutoRewind()
	{
		if(!Active() || !g_Config.m_KrxTasAutoRewind || g_Config.m_KrxTasRewind || m_vWorlds.size() <= 10)
			return;
		const int Id = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
		if(!Frozen(m_pWorld->GetCharacterById(Id)) || Frozen(m_pPreviousWorld->GetCharacterById(Id)))
			return;
		int BestLife = 0, BestRewind = 10;
		for(int Back = 4; Back < 10; ++Back)
			for(int Slot = 0; Slot < 2; ++Slot)
			{
				CGameWorld Probe;
				Probe.CopyWorldIsolated(m_vWorlds[m_vWorlds.size() - Back - 1].get());
				auto *pInputCharacter = Probe.GetCharacterById(m_aIds[Slot]);
				if(!pInputCharacter)
					return;
				const auto Input = pInputCharacter->DiagnosticCurrentInput();
				// 2b6760 always simulates the active client, with either slot's
				// saved input. Safe is 9999; test after each of 21 physics ticks.
				int Life = 9999;
				for(int Tick = 0; Tick <= 20; ++Tick)
				{
					auto *pCharacter = Probe.GetCharacterById(Id);
					if(!pCharacter)
					{
						Life = -1;
						break;
					}
					pCharacter->OnDirectInput(&Input);
					++Probe.m_GameTick;
					pCharacter->OnPredictedInput(&Input);
					Probe.Tick();
					pCharacter = Probe.GetCharacterById(Id);
					if(!pCharacter || Frozen(pCharacter) || pCharacter->Core()->m_LiveFrozen)
					{
						Life = Tick;
						break;
					}
				}
				if(Life > BestLife)
				{
					BestLife = Life;
					BestRewind = Back;
				}
			}
		Rewind(BestRewind);
	}

	void CKrxTas::Playback()
	{
		if(g_Config.m_KrxLoadReplay && GameClient()->m_Fent.Busy())
		{
			Message("FentBot: Still searching. Wait for Replay transferred to TAS, then start playback.");
			g_Config.m_KrxLoadReplay = 0;
		}
		if(!g_Config.m_KrxLoadReplay)
		{
			if(m_PlaybackWasActive)
				m_PlaybackTick = 0;
			m_PlaybackWasActive = false;
			return;
		}
		const auto &vMain = m_Replay.m_aFrames[0];
		if(vMain.empty())
		{
			Message("TAS: Empty replay.");
			g_Config.m_KrxLoadReplay = 0;
			return;
		}
		const int GameTick = Client()->PredGameTick(g_Config.m_ClDummy);
		if(m_LastPlaybackGameTick == GameTick)
			return;
		m_LastPlaybackGameTick = GameTick;
		if(m_PlaybackTick == 0)
			for(int Slot = 0; Slot < 2; ++Slot)
				if(!m_Replay.m_aFrames[Slot].empty())
					if(auto *pCharacter = GameClient()->m_GameWorld.GetCharacterById(m_aIds[Slot]))
						if(pCharacter->Core()->m_Pos != m_Replay.m_aFrames[Slot][0].m_Position)
						{
							char aMessage[128];
							str_format(aMessage, sizeof(aMessage), "TAS: Go to proper position (distance = %2.f).", distance(pCharacter->Core()->m_Pos, m_Replay.m_aFrames[Slot][0].m_Position));
							Message(aMessage);
							g_Config.m_KrxLoadReplay = 0;
							return;
						}
		++m_PlaybackTick;
		m_PlaybackWasActive = true;
		if(m_PlaybackTick >= (int)vMain.size())
		{
			Message("TAS: Finished replaying!");
			g_Config.m_KrxLoadReplay = 0;
			if(!g_Config.m_KrxTasAutoReplay)
				m_PlaybackTick = 0;
			return;
		}
		for(int Slot = 0; Slot < 2; ++Slot)
			if((size_t)m_PlaybackTick < m_Replay.m_aFrames[Slot].size())
			{
				const int InputSlot = m_aReplayInputSlots[Slot];
				GameClient()->m_Controls.m_aInputData[InputSlot] = m_Replay.m_aFrames[Slot][m_PlaybackTick].m_Input;
				if(InputSlot != g_Config.m_ClDummy)
					GameClient()->m_DummyInput = m_Replay.m_aFrames[Slot][m_PlaybackTick].m_Input;
			}
	}

	bool CKrxTas::MakePath(const char *pName, std::string &Path, bool Auto) const
	{
		if(!pName || !*pName || str_length(pName) > 240 || pName[0] == '/' || str_find(pName, "..") || str_find(pName, "\\") || str_find(pName, ":"))
			return false;
		Path = Auto ? "replays/auto/" : "replays/";
		Path += pName;
		if(!str_endswith(pName, ".tas") && !str_endswith(pName, ".json"))
			Path += ".tas";
		return true;
	}

	bool CKrxTas::SaveReplay(const char *pName, bool Auto, bool Overwrite)
	{
		std::string Path;
		if(!MakePath(pName, Path, Auto))
			return false;
		if(str_endswith(Path.c_str(), ".json"))
			Path = Path.substr(0, Path.size() - 5) + ".tas";
		if(!Auto && !Overwrite && Storage()->FileExists(Path.c_str(), IStorage::TYPE_SAVE) && m_OverwriteName != Path)
		{
			m_OverwriteName = Path;
			Message("TAS: Replay already exists. Save again to overwrite.");
			return false;
		}
		Storage()->CreateFolder("replays", IStorage::TYPE_SAVE);
		Storage()->CreateFolder("replays/auto", IStorage::TYPE_SAVE);
		m_Replay.m_Description = g_Config.m_KrxReplayFileDescription;
		char aDate[64];
		str_timestamp_format(aDate, sizeof(aDate), "%d.%m %H:%M");
		m_Replay.m_Date = aDate;
		std::vector<unsigned char> vData;
		std::string Error;
		if(!EncodeReplay(m_Replay, vData, Error))
		{
			Message(Error.c_str());
			return false;
		}
		const std::string Temporary = Path + ".tmp";
		IOHANDLE File = Storage()->OpenFile(Temporary.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File)
			return false;
		const bool Wrote = io_write(File, vData.data(), vData.size()) == vData.size();
		const bool Closed = io_close(File) == 0;
		if(!Wrote || !Closed || !Storage()->RenameFile(Temporary.c_str(), Path.c_str(), IStorage::TYPE_SAVE))
		{
			Storage()->RemoveFile(Temporary.c_str(), IStorage::TYPE_SAVE);
			Message("TAS: Couldn't save replay to file.");
			return false;
		}
		m_OverwriteName.clear();
		Message("TAS: Successfully saved replay to file.");
		RefreshFiles();
		return true;
	}

	bool CKrxTas::LoadReplay(const char *pName)
	{
		std::string Path;
		if(!MakePath(pName, Path, false))
			return false;
		IOHANDLE File = Storage()->OpenFile(Path.c_str(), IOFLAG_READ, IStorage::TYPE_ALL);
		if(!File)
		{
			Message("TAS: Replay file doesn't exist.");
			return false;
		}
		const auto Size = io_length(File);
		if(Size < 5 || Size > 128 * 1024 * 1024)
		{
			io_close(File);
			Message("TAS: Replay file is corrupted or too small.");
			return false;
		}
		std::vector<unsigned char> vData(Size);
		const bool Read = io_read(File, vData.data(), vData.size()) == vData.size();
		io_close(File);
		CReplay Loaded;
		std::string Error;
		const bool Decoded = Read && (str_endswith(Path.c_str(), ".json") ? DecodeReplayJson(std::string_view((const char *)vData.data(), vData.size()), Loaded, Error) : DecodeReplay(vData, Loaded, Error));
		if(!Decoded)
		{
			Message(Error.empty() ? "TAS: Couldn't read file." : Error.c_str());
			return false;
		}
		// Validate and rebuild snapshots from the same source as 2f2cf0. An
		// offline browser can load data, but cannot reuse a previous TAS world.
		CGameWorld Source;
		const bool Online = Client()->State() == IClient::STATE_ONLINE;
		if(Online)
			Source.CopyWorldIsolated(Active() ? m_pWorld.get() : &GameClient()->m_GameWorld);
		ClearReplay();
		m_Replay = std::move(Loaded);
		m_pWorld.reset();
		m_pPreviousWorld.reset();
		m_pInitialWorld.reset();
		m_aReplayInputSlots = {g_Config.m_ClDummy, !g_Config.m_ClDummy};
		m_aIds = {GameClient()->m_aLocalIds[m_aReplayInputSlots[0]], GameClient()->m_aLocalIds[m_aReplayInputSlots[1]]};
		str_copy(g_Config.m_KrxReplayFileDescription, m_Replay.m_Description.c_str());
		Message("TAS: Successfully loaded replay from file.");
		if(Online && !m_Replay.m_aFrames[0].empty())
		{
			InitializeWorld(Source, true);
			bool Valid = m_pWorld->GetCharacterById(m_aIds[0]) != nullptr;
			for(size_t I = 0; Valid && I < m_Replay.m_aFrames[0].size(); ++I)
			{
				std::array<CNetObj_PlayerInput, 2> aInputs{};
				unsigned InputMask = 0;
				for(int Slot = 0; Slot < 2; ++Slot)
					if(I < m_Replay.m_aFrames[Slot].size())
					{
						aInputs[m_aReplayInputSlots[Slot]] = m_Replay.m_aFrames[Slot][I].m_Input;
						InputMask |= 1u << m_aReplayInputSlots[Slot];
					}
				Step(aInputs, false, false, 2, InputMask);
				for(int Slot = 0; Slot < 2; ++Slot)
					if(I < m_Replay.m_aFrames[Slot].size())
					{
						const auto *pCharacter = m_pWorld->GetCharacterById(m_aIds[Slot]);
						if(!pCharacter || pCharacter->Core()->m_Pos != m_Replay.m_aFrames[Slot][I].m_Position)
							Valid = false;
					}
				m_vWorlds.push_back(Snapshot(*m_pWorld));
			}
			if(Valid)
			{
				m_ReplayFinishTicks = m_StartTick && m_FinishTick ? m_FinishTick - m_StartTick : 0;
				g_Config.m_KrxTasPause = 1;
			}
			else
			{
				m_vWorlds.clear();
				m_pWorld.reset();
				m_pPreviousWorld.reset();
				m_pInitialWorld.reset();
				m_StartTick = m_FinishTick = 0;
				Message("TAS: Couldn't validate run information (invalid run).");
			}
		}
		return true;
	}

	bool CKrxTas::DeleteReplay(const char *pName)
	{
		std::string Path;
		if(!MakePath(pName, Path, false))
			return false;
		const bool Removed = Storage()->RemoveFile(Path.c_str(), IStorage::TYPE_SAVE);
		RefreshFiles();
		return Removed;
	}

	void CKrxTas::RefreshFiles()
	{
		m_vReplayFiles.clear();
		struct CScan
		{
			IStorage *m_pStorage;
			std::vector<std::string> *m_pFiles;
			std::string m_Path;
			int m_Depth;
			static int Callback(const char *pName, int IsDir, int Type, void *pUser)
			{
				auto &Scan = *static_cast<CScan *>(pUser);
				if(pName[0] == '.')
					return 0;
				const std::string Name = Scan.m_Path.empty() ? pName : Scan.m_Path + "/" + pName;
				if(IsDir && Scan.m_Depth < 16)
				{
					CScan Child{Scan.m_pStorage, Scan.m_pFiles, Name, Scan.m_Depth + 1};
					Scan.m_pStorage->ListDirectory(Type, ("replays/" + Name).c_str(), Callback, &Child);
				}
				else if(!IsDir && (str_endswith(pName, ".tas") || str_endswith(pName, ".json")))
					Scan.m_pFiles->push_back(Name);
				return 0;
			}
		} Scan{Storage(), &m_vReplayFiles, "", 0};
		Storage()->ListDirectory(IStorage::TYPE_ALL, "replays", CScan::Callback, &Scan);
		std::sort(m_vReplayFiles.begin(), m_vReplayFiles.end());
		m_vReplayFiles.erase(std::unique(m_vReplayFiles.begin(), m_vReplayFiles.end()), m_vReplayFiles.end());
	}

	bool CKrxTas::ValidateReplay()
	{
		if(m_Replay.m_aFrames[0].empty())
			return false;
		CGameWorld Probe;
		Probe.CopyWorldIsolated(Active() ? m_pWorld.get() : &GameClient()->m_GameWorld);
		Probe.m_WorldConfig.m_KrxPrediction = true;
		for(size_t I = 0; I < m_Replay.m_aFrames[0].size(); ++I)
		{
			for(int Slot = 0; Slot < 2; ++Slot)
				if(I < m_Replay.m_aFrames[Slot].size())
					if(auto *pCharacter = Probe.GetCharacterById(m_aIds[Slot]))
						pCharacter->OnDirectInput(&m_Replay.m_aFrames[Slot][I].m_Input);
			++Probe.m_GameTick;
			for(int Slot = 0; Slot < 2; ++Slot)
				if(I < m_Replay.m_aFrames[Slot].size())
					if(auto *pCharacter = Probe.GetCharacterById(m_aIds[Slot]))
						pCharacter->OnPredictedInput(&m_Replay.m_aFrames[Slot][I].m_Input);
			Probe.Tick();
			for(int Slot = 0; Slot < 2; ++Slot)
				if(I < m_Replay.m_aFrames[Slot].size())
				{
					auto *pCharacter = Probe.GetCharacterById(m_aIds[Slot]);
					if(!pCharacter || pCharacter->Core()->m_Pos != m_Replay.m_aFrames[Slot][I].m_Position)
					{
						Message("TAS: Invalid run!");
						return false;
					}
				}
		}
		Message("TAS: Valid run!");
		return true;
	}

	int CKrxTas::GetReplayTime()
	{
		CGameWorld Probe;
		Probe.CopyWorldIsolated(&GameClient()->m_GameWorld);
		Probe.m_WorldConfig.m_KrxPrediction = true;
		int Start = 0, Finish = 0;
		for(size_t I = 0; I < m_Replay.m_aFrames[0].size(); ++I)
		{
			for(int Slot = 0; Slot < 2; ++Slot)
				if(I < m_Replay.m_aFrames[Slot].size())
					if(auto *pCharacter = Probe.GetCharacterById(m_aIds[Slot]))
						pCharacter->OnDirectInput(&m_Replay.m_aFrames[Slot][I].m_Input);
			++Probe.m_GameTick;
			for(int Slot = 0; Slot < 2; ++Slot)
				if(I < m_Replay.m_aFrames[Slot].size())
					if(auto *pCharacter = Probe.GetCharacterById(m_aIds[Slot]))
						pCharacter->OnPredictedInput(&m_Replay.m_aFrames[Slot][I].m_Input);
			Probe.Tick();
			for(int Slot = 0; Slot < 2; ++Slot)
				if(I < m_Replay.m_aFrames[Slot].size())
					if(auto *pCharacter = Probe.GetCharacterById(m_aIds[Slot]))
					{
						auto vIndices = Collision()->GetMapIndices(pCharacter->m_PrevPos, pCharacter->Core()->m_Pos);
						vIndices.push_back(Collision()->GetPureMapIndex(pCharacter->Core()->m_Pos));
						for(int Index : vIndices)
						{
							const int Tile = Collision()->GetTileIndex(Index), Front = Collision()->GetFrontTileIndex(Index);
							if(Tile == TILE_START || Front == TILE_START)
								Start = Probe.GameTick();
							if((Tile == TILE_FINISH || Front == TILE_FINISH) && Start && !Finish)
								Finish = Probe.GameTick();
						}
					}
		}
		const int Ticks = Start && Finish ? Finish - Start : 0;
		// 2f24b0 writes the last measured race time only for a completed run.
		// Replay metadata Finish Time is populated separately by 2f2cf0.
		if(Ticks)
			m_LastReplayTimeTicks = Ticks;
		return Ticks;
	}

	void CKrxTas::ConvertLegacyReplays()
	{
		const auto vFiles = m_vReplayFiles;
		int Converted = 0;
		for(const auto &Name : vFiles)
		{
			if(!str_endswith(Name.c_str(), ".json"))
				continue;
			const std::string Target = "replays/" + Name.substr(0, Name.size() - 5) + ".tas";
			if(Storage()->FileExists(Target.c_str(), IStorage::TYPE_ALL))
				continue;
			char *pJson = Storage()->ReadFileStr(("replays/" + Name).c_str(), IStorage::TYPE_ALL);
			if(!pJson)
				continue;
			CReplay Replay;
			std::string Error;
			std::vector<unsigned char> vData;
			const bool Valid = DecodeReplayJson(pJson, Replay, Error) && EncodeReplay(Replay, vData, Error);
			free(pJson);
			if(!Valid)
				continue;
			for(size_t Slash = Target.find('/'); Slash != std::string::npos; Slash = Target.find('/', Slash + 1))
				Storage()->CreateFolder(Target.substr(0, Slash).c_str(), IStorage::TYPE_SAVE);
			const std::string Temporary = Target + ".tmp";
			IOHANDLE File = Storage()->OpenFile(Temporary.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
			if(!File)
				continue;
			const bool Wrote = io_write(File, vData.data(), vData.size()) == vData.size();
			const bool Closed = io_close(File) == 0;
			if(Wrote && Closed && Storage()->RenameFile(Temporary.c_str(), Target.c_str(), IStorage::TYPE_SAVE))
				++Converted;
			else
				Storage()->RemoveFile(Temporary.c_str(), IStorage::TYPE_SAVE);
		}
		RefreshFiles();
		char aMessage[128];
		str_format(aMessage, sizeof(aMessage), "TAS: Converted %d replays to new format.", Converted);
		Message(aMessage);
	}

	void CKrxTas::PrepareForFent()
	{
		ClearReplay();
		m_aIds = {GameClient()->m_aLocalIds[0], GameClient()->m_aLocalIds[1]};
		m_aReplayInputSlots = {0, 1};
		InitializeWorld(GameClient()->m_GameWorld, true);
		m_Replay.m_Map = Client()->ServerInfo().m_aMap;
		m_Replay.m_Author = g_Config.m_PlayerName;
		auto &Controls = GameClient()->m_Controls;
		for(auto &Input : Controls.m_aInputData)
			Input.m_Direction = Input.m_Jump = Input.m_Hook = 0;
		m_aInputs = {Controls.m_aInputData[0], Controls.m_aInputData[1]};
		for(int I = 0; I < 50; ++I)
			Step(m_aInputs, true, false);
	}

	void CKrxTas::ImportInputs(const CGameWorld &Initial, int Id, const std::vector<CNetObj_PlayerInput> &vInputs)
	{
		if(vInputs.empty())
		{
			ClearReplay();
			Message("FentBot: No replay found. Choose a reachable goal and try again.");
			return;
		}
		// 2d9b00 appends to the 50-frame prefix, always into replay_local,
		// even when the active character is the dummy. Preserve that ordering.
		m_pWorld->CopyWorldIsolated(&Initial);
		std::vector<CNetObj_PlayerInput> vAllInputs;
		vAllInputs.reserve(vInputs.size() + 1);
		vAllInputs.push_back(GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy]);
		vAllInputs.insert(vAllInputs.end(), vInputs.begin(), vInputs.end());
		for(const auto &Input : vAllInputs)
		{
			m_pPreviousWorld->CopyWorldIsolated(m_pWorld.get());
			auto *pCharacter = m_pWorld->GetCharacterById(Id);
			if(!pCharacter)
				break;
			pCharacter->OnDirectInput(&Input);
			++m_pWorld->m_GameTick;
			pCharacter->OnPredictedInput(&Input);
			m_pWorld->Tick();
			pCharacter = m_pWorld->GetCharacterById(Id);
			if(!pCharacter)
				break;
			m_Replay.m_aFrames[0].push_back({Input, pCharacter->Core()->m_Pos});
			m_vWorlds.push_back(Snapshot(*m_pWorld));
		}
		char aMessage[160];
		str_format(aMessage, sizeof(aMessage), "FentBot: Replay transferred to TAS (%d frames). Ready for playback.", (int)m_Replay.m_aFrames[0].size());
		Message(aMessage);
	}
}
