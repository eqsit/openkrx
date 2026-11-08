#ifndef GAME_CLIENT_KRX_PARALLEL_PREDICTION_H
#define GAME_CLIENT_KRX_PARALLEL_PREDICTION_H
#include <engine/engine.h>
#include <engine/shared/jobs.h>

#include <game/client/prediction/gameworld.h>
#include <game/collision.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <vector>
namespace OpenKrx
{
	// Each range uses its own mutable map/tuning; outputs belong to distinct indices.
	class CPredictionRangeJob final : public IJob
	{
		const CGameWorld &m_Source;
		std::function<void(const CGameWorld &, int)> m_Evaluate;
		int m_Begin, m_End;
		std::promise<void> m_Completed;
		std::atomic<bool> m_Claimed{false};
		void Run() override { RunInline(); }

	public:
		CPredictionRangeJob(const CGameWorld &Source, std::function<void(const CGameWorld &, int)> Evaluate, int Begin, int End) : m_Source(Source), m_Evaluate(std::move(Evaluate)), m_Begin(Begin), m_End(End) { Abortable(false); }
		std::future<void> Result() { return m_Completed.get_future(); }
		void RunInline()
		{
			if(m_Claimed.exchange(true, std::memory_order_acq_rel))
				return;
			auto Collision = m_Source.Collision()->CreateIsolatedCopy();
			std::array<CTuningParams, 256> Tuning;
			std::copy_n(m_Source.TuningList(), Tuning.size(), Tuning.begin());
			CGameWorld World;
			World.CopyWorldIsolated(&m_Source);
			World.RebindCollision(Collision.get());
			World.RebindTuning(Tuning.data());
			for(int I = m_Begin; I < m_End; ++I)
				m_Evaluate(World, I);
			m_Completed.set_value();
		}
	};
	inline void ParallelPrediction(const CGameWorld &Source, IEngine *pEngine, int Count, std::function<void(const CGameWorld &, int)> Evaluate)
	{
		if(!pEngine || pEngine->JobThreadCount() <= 0 || Count < 2)
		{
			for(int I = 0; I < Count; ++I)
				Evaluate(Source, I);
			return;
		}
		const int Workers = std::min(Count, pEngine->JobThreadCount());
		std::vector<std::shared_ptr<CPredictionRangeJob>> Jobs;
		std::vector<std::future<void>> Results;
		for(int I = 0; I < Workers; ++I)
		{
			auto Job = std::make_shared<CPredictionRangeJob>(Source, Evaluate, I * (Count / Workers) + std::min(I, Count % Workers), (I + 1) * (Count / Workers) + std::min(I + 1, Count % Workers));
			Results.push_back(Job->Result());
			Jobs.push_back(Job);
			pEngine->AddJob(Job);
		}
		for(auto &Job : Jobs)
			Job->RunInline();
		for(auto &Result : Results)
			Result.get();
	}
}
#endif
