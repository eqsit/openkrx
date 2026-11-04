#ifndef GAME_CLIENT_KRX_PREDICTION_BENCHMARK_H
#define GAME_CLIENT_KRX_PREDICTION_BENCHMARK_H

#include <atomic>
#include <memory>
#include <string>
#include <thread>

class CGameWorld;
namespace OpenKrx
{
	class CPredictionBenchmark
	{
	public:
		~CPredictionBenchmark();
		bool Start(const CGameWorld &Source, int ClientId, int Runs, int Threads);
		bool Poll(std::string &Result);
		void Stop();

	private:
		std::thread m_Worker;
		std::atomic<bool> m_Cancel{false}, m_Done{false};
		std::string m_Result;
	};
}
#endif
