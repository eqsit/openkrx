#include "replay_analyzer.h"

#include <engine/engine.h>
#include <engine/shared/jobs.h>

#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/gameworld.h>
#include <game/collision.h>

#include <algorithm>
#include <array>
#include <functional>
#include <future>
#include <memory>

namespace OpenKrx
{
	namespace
	{
		class CStabilityJob final : public IJob
		{
			std::function<bool()> m_Trial;
			std::promise<bool> m_Result;
			std::atomic<bool> m_Claimed{false};
			void Run() override { RunInline(); }

		public:
			explicit CStabilityJob(std::function<bool()> Trial) : m_Trial(std::move(Trial)) { Abortable(false); }
			std::future<bool> Result() { return m_Result.get_future(); }
			void RunInline()
			{
				if(m_Claimed.exchange(true, std::memory_order_acq_rel))
					return;
				m_Result.set_value(m_Trial());
			}
		};
	}

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

	CReplayStabilityResult AnalyzeReplayStability(const CReplay &Replay, const CGameWorld &Initial, int Id, float ReferenceSpeed, const std::atomic<bool> *pCancel, IEngine *pEngine)
	{
		CReplayStabilityResult Result;
		vec2 Reference;
		if(!SimulateReplayAtHookSpeed(Replay, Initial, Id, ReferenceSpeed, Reference, pCancel))
		{
			Result.m_Cancelled = pCancel && pCancel->load(std::memory_order_relaxed);
			Result.m_Error = "Could not simulate original replay to get a reference end state.";
			return Result;
		}
		std::vector<float> vSpeeds;
		for(float Speed = 79.0f; Speed <= 81.0f; Speed += .01f)
			vSpeeds.push_back(Speed);
		std::vector<std::shared_ptr<CStabilityJob>> vJobs;
		std::vector<std::future<bool>> vResults;
		if(pEngine && pEngine->JobThreadCount() > 0)
		{
			for(float Speed : vSpeeds)
			{
				auto pJob = std::make_shared<CStabilityJob>([&, Speed] {
					vec2 End;
					return !(pCancel && pCancel->load()) && SimulateReplayAtHookSpeed(Replay, Initial, Id, Speed, End, pCancel) && distance(End, Reference) < .01f;
				});
				vResults.push_back(pJob->Result());
				vJobs.push_back(pJob);
				pEngine->AddJob(pJob);
			}
			// Drain ALL jobs before cancellation or referenced snapshots can die.
			for(auto &pJob : vJobs)
				pJob->RunInline();
			for(auto &Future : vResults)
				Future.wait();
		}
		bool InRange = false;
		float RangeStart = -1.0f;
		for(float Speed : vSpeeds)
		{
			if(pCancel && pCancel->load(std::memory_order_relaxed))
			{
				Result.m_Cancelled = true;
				return Result;
			}
			vec2 End;
			const bool Stable = vResults.empty() ? SimulateReplayAtHookSpeed(Replay, Initial, Id, Speed, End, pCancel) && distance(End, Reference) < .01f : vResults[Result.m_Samples].get();
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
