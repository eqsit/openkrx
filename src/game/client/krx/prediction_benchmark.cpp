#include "prediction_benchmark.h"

#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/gameworld.h>
#include <game/collision.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <vector>

namespace OpenKrx
{
	namespace
	{
		struct CSnapshot
		{
			std::unique_ptr<CCollision> m_pCollision;
			std::array<CTuningParams, 256> m_aTuning;
			CGameWorld m_World;
			explicit CSnapshot(const CGameWorld &Source)
			{
				m_pCollision = Source.Collision()->CreateIsolatedCopy();
				std::copy_n(Source.TuningList(), m_aTuning.size(), m_aTuning.begin());
				m_World.CopyWorldIsolated(&Source);
				m_World.RebindCollision(m_pCollision.get());
				m_World.RebindTuning(m_aTuning.data());
			}
		};

		int RunSample(const CGameWorld &Source, int ClientId, const std::atomic<bool> &Cancel)
		{
			CSnapshot Copy(Source);
			CNetObj_PlayerInput Input{};
			std::chrono::nanoseconds CpuTime{0};
			int Ticks = 0;
			// REA36bd40: add elapsed time around direct-input/predicted-input/world
			// tick until one second, or ten million iterations. Setup is not timed.
			while(CpuTime < std::chrono::seconds(1) && Ticks < 10000000 && !Cancel.load(std::memory_order_relaxed))
			{
				auto *pCharacter = Copy.m_World.GetCharacterById(ClientId);
				if(!pCharacter)
					break;
				const auto Begin = std::chrono::steady_clock::now();
				pCharacter->OnDirectInput(&Input);
				++Copy.m_World.m_GameTick;
				pCharacter->OnPredictedInput(&Input);
				Copy.m_World.Tick();
				CpuTime += std::chrono::steady_clock::now() - Begin;
				++Ticks;
			}
			return Ticks;
		}

		std::string Stats(const std::vector<int> &Values)
		{
			if(Values.empty())
				return "no valid character samples";
			const double Mean = std::accumulate(Values.begin(), Values.end(), 0.0) / Values.size();
			double Variance = 0;
			for(int Value : Values)
				Variance += (Value - Mean) * (Value - Mean);
			const double Stddev = Values.size() > 1 ? std::sqrt(Variance / (Values.size() - 1)) : 0;
			std::ostringstream Text;
			Text << std::fixed << std::setprecision(1) << "mean " << Mean << " ticks/s, stddev " << Stddev << ", min " << *std::min_element(Values.begin(), Values.end()) << ", max " << *std::max_element(Values.begin(), Values.end());
			return Text.str();
		}
	}

	CPredictionBenchmark::~CPredictionBenchmark() { Stop(); }

	void CPredictionBenchmark::Stop()
	{
		m_Cancel = true;
		if(m_Worker.joinable())
			m_Worker.join();
		m_Done = false;
	}

	bool CPredictionBenchmark::Start(const CGameWorld &Source, int ClientId, int Runs, int Threads)
	{
		if(m_Worker.joinable() || !Source.Collision() || !const_cast<CGameWorld &>(Source).GetCharacterById(ClientId))
			return false;
		auto Snapshot = std::make_shared<CSnapshot>(Source);
		Runs = std::clamp(Runs, 1, 20);
		Threads = std::clamp(Threads, 1, (int)std::max(1u, std::thread::hardware_concurrency()));
		m_Cancel = false;
		m_Done = false;
		m_Worker = std::thread([this, Snapshot, ClientId, Runs, Threads] {
			std::vector<int> Single, Parallel;
			for(int Run = 0; Run < Runs && !m_Cancel; ++Run)
				Single.push_back(RunSample(Snapshot->m_World, ClientId, m_Cancel));
			for(int Run = 0; Run < Runs && !m_Cancel; ++Run)
			{
				std::vector<int> Counts(Threads);
				std::vector<std::thread> Workers;
				for(int I = 0; I < Threads; ++I)
					Workers.emplace_back([&, I] { Counts[I] = RunSample(Snapshot->m_World, ClientId, m_Cancel); });
				for(auto &Worker : Workers)
					Worker.join();
				Parallel.push_back(std::accumulate(Counts.begin(), Counts.end(), 0));
			}
			m_Result = m_Cancel ? "Prediction benchmark cancelled" : "Single-threaded: " + Stats(Single) + "\nMulti-threaded (" + std::to_string(Threads) + " threads): " + Stats(Parallel);
			m_Done.store(true, std::memory_order_release);
		});
		return true;
	}

	bool CPredictionBenchmark::Poll(std::string &Result)
	{
		if(!m_Done.load(std::memory_order_acquire))
			return false;
		m_Worker.join();
		Result = std::move(m_Result);
		m_Done = false;
		return true;
	}
}
