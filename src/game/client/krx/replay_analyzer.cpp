#include "replay_analyzer.h"

#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/gameworld.h>
#include <game/collision.h>

#include <array>
#include <algorithm>

namespace OpenKrx
{
	bool SimulateReplayAtHookSpeed(const CReplay &Replay, const CGameWorld &Initial, int Id, float Speed, vec2 &EndPosition, const std::atomic<bool> *pCancel)
	{
		if(Replay.m_aFrames[0].empty() || !Initial.Collision() || !Initial.TuningList())
			return false;
		// Trials must not mutate the live tuning table or one another's doors.
		auto pCollision = Initial.Collision()->CreateIsolatedCopy();
		std::array<CTuningParams, 256> aTuning;
		std::copy_n(Initial.TuningList(), aTuning.size(), aTuning.begin());
		CGameWorld World;
		World.CopyWorldIsolated(&Initial);
		World.RebindCollision(pCollision.get());
		World.RebindTuning(aTuning.data());
		auto *pCharacter = World.GetCharacterById(Id);
		if(!pCharacter)
			return false;
		const int Zone = pCharacter->GetOverriddenTuneZone();
		if(Zone >= 0 && Zone < (int)aTuning.size())
			aTuning[Zone].m_HookFireSpeed = Speed;
		for(const auto &Frame : Replay.m_aFrames[0])
		{
			if(pCancel && pCancel->load(std::memory_order_relaxed))
				return false;
			pCharacter = World.GetCharacterById(Id);
			if(!pCharacter)
				return false;
			pCharacter->OnDirectInput(&Frame.m_Input);
			++World.m_GameTick;
			pCharacter->OnPredictedInput(&Frame.m_Input);
			World.Tick();
			World.m_PredictedEvents.clear();
			pCharacter = World.GetCharacterById(Id);
			if(!pCharacter || pCharacter->m_FreezeTime > 0)
				return false;
		}
		EndPosition = pCharacter->Core()->m_Pos;
		return true;
	}

	CReplayStabilityResult AnalyzeReplayStability(const CReplay &Replay, const CGameWorld &Initial, int Id, float ReferenceSpeed, const std::atomic<bool> *pCancel)
	{
		CReplayStabilityResult Result;
		vec2 Reference;
		if(!SimulateReplayAtHookSpeed(Replay, Initial, Id, ReferenceSpeed, Reference, pCancel))
		{
			Result.m_Cancelled = pCancel && pCancel->load(std::memory_order_relaxed);
			Result.m_Error = "Could not simulate original replay to get a reference end state.";
			return Result;
		}
		bool InRange = false;
		float RangeStart = -1.0f;
		for(float Speed = 79.0f; Speed <= 81.0f; Speed += .01f)
		{
			if(pCancel && pCancel->load(std::memory_order_relaxed))
			{
				Result.m_Cancelled = true;
				return Result;
			}
			vec2 End;
			const bool Stable = SimulateReplayAtHookSpeed(Replay, Initial, Id, Speed, End, pCancel) && distance(End, Reference) < .01f;
			++Result.m_Samples;
			if(Stable && !InRange)
			{
				InRange = true;
				RangeStart = Speed;
			}
			else if(!Stable && InRange)
			{
				Result.m_vRanges.push_back({RangeStart, Speed - .01f});
				InRange = false;
			}
		}
		if(InRange)
			Result.m_vRanges.push_back({RangeStart, 81.0f});
		return Result;
	}
}
