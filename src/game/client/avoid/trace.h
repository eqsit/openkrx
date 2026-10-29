#ifndef GAME_CLIENT_AVOID_TRACE_H
#define GAME_CLIENT_AVOID_TRACE_H

#include <base/types.h>

#include <generated/protocol.h>

#include <cstdint>
#include <deque>
#include <string>

class CGameClient;
namespace OpenKrx
{
	class CAvoid;

	// A bounded flight recorder. No simulation, input modification, or background
	// access to live game worlds. Files are opened only for freeze/manual reports.
	class CAvoidTrace
	{
		struct CFrame
		{
			int64_t m_TimeUs;
			std::string m_Json;
		};
		std::deque<CFrame> m_Frames;
		std::string m_Directory;
		IOHANDLE m_File = nullptr;
		int64_t m_PostUntilUs = 0;
		int m_PostFrames = 0;
		unsigned m_Sequence = 0;
		bool m_WasFrozen = false;
		bool m_WriteFailed = false;
		int m_ClientId = -1;
		int m_Slot = -1;
		void Begin(const char *pReason, int64_t NowUs);
		void Write(const std::string &Line);
		void Finish(const char *pReason);

	public:
		static constexpr size_t MAX_FRAMES = 384;
		static constexpr int64_t HISTORY_US = 6000000;
		~CAvoidTrace();
		void Reset();
		void Dump();
		// Kept separate from scene collection so event ordering/limits can be tested.
		void Observe(const std::string &Directory, std::string Frame, int64_t NowUs, bool Frozen);
		void Record(CGameClient *pClient, const CAvoid &Avoid, const CNetObj_PlayerInput &Raw,
			const CNetObj_PlayerInput &AfterAvoid, const CNetObj_PlayerInput &Final, bool Emitted, bool PipelineRan = true, int PipelineUs = 0);
	};
}
#endif
