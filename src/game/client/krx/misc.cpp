#include "misc.h"

#include <base/str.h>
#include <base/time.h>

#include <engine/shared/config.h>

#include <game/client/avoid/rocket_save.h>
#include <game/client/gameclient.h>

#include <array>
#include <random>

namespace OpenKrx
{

	namespace
	{
		int RandomInclusive(int Min, int Max)
		{
			// The reference uses a uniform integer distribution. Its PRNG stream is
			// not shared with physics or the grenade saver's release timing.
			static std::mt19937 s_Random(std::random_device{}());
			return std::uniform_int_distribution<int>(Min, Max)(s_Random);
		}

	} // namespace

	void CMiscFeatures::OnReset()
	{
		m_aSlots = {};
	}

	CMiscFeatures::CSlot *CMiscFeatures::PrepareContext(CGameClient *pGameClient)
	{
		if(pGameClient->Client()->State() != IClient::STATE_ONLINE)
		{
			OnReset();
			return nullptr;
		}

		const int Slot = g_Config.m_ClDummy;
		const int ClientId = pGameClient->m_aLocalIds[Slot];
		CSlot &State = m_aSlots[Slot];
		if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pGameClient->m_aClients[ClientId].m_Active)
		{
			State = {};
			return nullptr;
		}
		if(State.m_ClientId != ClientId)
		{
			State = {};
			State.m_ClientId = ClientId;
		}
		return &State;
	}

	vec2 CMiscFeatures::QuantizedAim(vec2 Direction, float Radius)
	{
		// 0x1402ba970 normalizes to the full mouse maximum, rather than
		// clamping only vectors beyond it. Then MSVC cvttss2si truncates both
		// components to integers before returning them as floats.
		const float Length = length(Direction);
		if(Length == 0.0f)
			return vec2(0, 0);
		const vec2 Scaled = (Direction / Length) * Radius;
		return vec2((int)Scaled.x, (int)Scaled.y);
	}

	void CMiscFeatures::BeforeAvoid(CGameClient *pGameClient)
	{
		CSlot *pState = PrepareContext(pGameClient);
		if(!pState)
			return;
		CSlot &State = *pState;
		const int Slot = g_Config.m_ClDummy;
		// Shell-observed 0x1402bf890 sends a random emoticon each update. The
		// reference includes index 16, outside the modern protocol's valid range;
		// keep valid indices and one emission per game tick rather than per frame.
		const int GameTick = pGameClient->Client()->GameTick(Slot);
		if(!g_Config.m_KrxEmoteSpam)
			State.m_LastEmoteTick = -1;
		else if(GameTick >= 0 && GameTick != State.m_LastEmoteTick)
		{
			pGameClient->m_Emoticon.Emote(RandomInclusive(0, NUM_EMOTICONS - 1));
			State.m_LastEmoteTick = GameTick;
		}
	}

	void CMiscFeatures::AutoTeam(CGameClient *pGameClient)
	{
		CSlot *pState = PrepareContext(pGameClient);
		if(!pState)
			return;
		CSlot &State = *pState;
		const int Slot = g_Config.m_ClDummy;
		const int ClientId = pGameClient->m_aLocalIds[Slot];
		const int64_t Now = time_get();
		const int64_t Frequency = time_freq();
		// 0x1402beb90: only leave team 0, prefer the requested empty team,
		// otherwise choose uniformly among empty teams 1..63, then lock it.
		// A bounded retry avoids repeating chat commands every rendered frame while
		// the server has not acknowledged the team change (or refuses the command).
		if(!g_Config.m_KrxAutoTeam || pGameClient->m_Teams.Team(ClientId) != 0)
			State.m_LastTeamAttempt = 0;
		else if((pGameClient->m_Controls.m_aInputData[Slot].m_PlayerFlags & PLAYERFLAG_PLAYING) &&
			(State.m_LastTeamAttempt == 0 || Now - State.m_LastTeamAttempt >= 3 * Frequency))
		{
			std::array<bool, 64> aOccupied{};
			for(int Id = 0; Id < MAX_CLIENTS; ++Id)
			{
				if(Id == ClientId || !pGameClient->m_aClients[Id].m_Active)
					continue;
				const int Team = pGameClient->m_Teams.Team(Id);
				if(Team > 0 && Team < (int)aOccupied.size())
					aOccupied[Team] = true;
			}
			std::array<int, 63> aEmpty{};
			int NumEmpty = 0;
			for(int Team = 1; Team < (int)aOccupied.size(); ++Team)
				if(!aOccupied[Team])
					aEmpty[NumEmpty++] = Team;
			State.m_LastTeamAttempt = Now;
			if(!NumEmpty)
			{
				pGameClient->m_KrxMiscPreTas.Notify(1, "AutoTeam", "AutoTeam: No empty team found!");
				pGameClient->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "AutoTeam", "AutoTeam: No empty team found!");
			}
			if(NumEmpty)
			{
				const int Preferred = g_Config.m_KrxAutoTeamPreferred;
				const int Team = Preferred > 0 && Preferred < (int)aOccupied.size() && !aOccupied[Preferred] ?
							 Preferred :
							 aEmpty[RandomInclusive(0, NumEmpty - 1)];
				char aCommand[32];
				str_format(aCommand, sizeof(aCommand), "/team %d", Team);
				pGameClient->m_Chat.SendChat(0, aCommand);
				pGameClient->m_Chat.SendChat(0, "/lock");
			}
		}
	}

	void CMiscFeatures::AfterAvoid(CGameClient *pGameClient)
	{
		CSlot *pState = PrepareContext(pGameClient);
		if(!pState)
			return;
		CSlot &State = *pState;
		const int Slot = g_Config.m_ClDummy;
		const int ClientId = pGameClient->m_aLocalIds[Slot];
		const int64_t Now = time_get();
		const int64_t Frequency = time_freq();
		// 0x14026d2d0 calls the authoritative b4450 frozen predicate, not a
		// predicted collision. Send once per continuous frozen period.
		const bool Frozen = pGameClient->m_Snap.m_pLocalCharacter &&
				    !pGameClient->m_Snap.m_SpecInfo.m_Active && IsKrxCharacterFrozen(pGameClient, ClientId);
		if(g_Config.m_KrxKillOnFreeze && Frozen && !State.m_WasFrozen)
		{
			// Original 0x140210700 also sends to the other connection when
			// copy-moves is enabled; use the shared DDNet sender for both.
			pGameClient->SendKill();
		}
		State.m_WasFrozen = g_Config.m_KrxKillOnFreeze && Frozen;
		pGameClient->m_KrxMovementPreTas.FindTunes();

		// 0x1402bb4d0: move the mouse by independent uniform [1,50] steps,
		// alternating positive/negative after each configured time interval.
		// The timer is independent of physical mouse movement in the reference.
		if(!g_Config.m_KrxAntiAfk || !pGameClient->m_Snap.m_pLocalCharacter || pGameClient->m_Snap.m_SpecInfo.m_Active)
		{
			State.m_AntiAfkStarted = 0;
			State.m_NegativeMouseStep = false;
		}
		else if(State.m_AntiAfkStarted == 0)
			State.m_AntiAfkStarted = Now;
		else if(Now - State.m_AntiAfkStarted >= (int64_t)g_Config.m_KrxAntiAfkSeconds * Frequency)
		{
			const float Sign = State.m_NegativeMouseStep ? -1.0f : 1.0f;
			const float DeltaY = RandomInclusive(1, 50) * Sign;
			const float DeltaX = RandomInclusive(1, 50) * Sign;
			const vec2 Direction = pGameClient->m_Controls.m_aMousePos[Slot] + vec2(DeltaX, DeltaY);
			pGameClient->m_Controls.m_aMousePos[Slot] = QuantizedAim(Direction, pGameClient->m_Controls.GetMaxMouseDistance());
			// KRX writes the physical mouse vector here. The prepared input
			// retains its aim until the next SnapInput preparation.
			State.m_NegativeMouseStep = !State.m_NegativeMouseStep;
			State.m_AntiAfkStarted = Now;
		}
	}

} // namespace OpenKrx
