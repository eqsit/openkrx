#ifndef GAME_CLIENT_KRX_WORKER_POOL_H
#define GAME_CLIENT_KRX_WORKER_POOL_H

#include <engine/engine.h>
#include <engine/shared/jobs.h>

#include <future>
#include <functional>
#include <mutex>
#include <thread>

namespace OpenKrx
{

class CWorkerPool final : public IEngine
{
	CJobPool m_Pool;
	std::once_flag m_Initialize;
	int m_Threads = 0;

public:
	static unsigned ThreadCount(unsigned Reported) { return Reported ? Reported : 8; }
	void Init() override
	{
		std::call_once(m_Initialize, [this] {
			m_Threads = (int)ThreadCount(std::thread::hardware_concurrency());
			m_Pool.Init(m_Threads);
		});
	}
	int JobThreadCount() const override { return m_Threads; }
	void AddJob(std::shared_ptr<IJob> Job) override { m_Pool.Add(std::move(Job)); }
	void ShutdownJobs() override
	{
		if(m_Threads)
		{
			m_Pool.Shutdown();
			m_Threads = 0;
		}
	}
	void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
};

// Non-abortable jobs keep captured snapshots alive through completion.
class CWorkerTask final : public IJob
{
	std::function<void()> m_Function;
	std::promise<void> m_Completed;
	void Run() override
	{
		m_Function();
		m_Completed.set_value();
	}

public:
	explicit CWorkerTask(std::function<void()> Function) : m_Function(std::move(Function)) { Abortable(false); }
	std::future<void> Result() { return m_Completed.get_future(); }
	static std::future<void> Submit(IEngine *pEngine, std::function<void()> Function)
	{
		auto Job = std::make_shared<CWorkerTask>(std::move(Function));
		auto Result = Job->Result();
		pEngine->AddJob(std::move(Job));
		return Result;
	}
};

}
#endif
