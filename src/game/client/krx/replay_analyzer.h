#ifndef GAME_CLIENT_KRX_REPLAY_ANALYZER_H
#define GAME_CLIENT_KRX_REPLAY_ANALYZER_H

#include "replay.h"

#include <atomic>
#include <string>
#include <vector>

class CGameWorld;
class IEngine;
namespace OpenKrx
{
	struct CReplayStabilityRange { float m_Min, m_Max; };
	struct CReplayStabilityResult
	{
		std::vector<CReplayStabilityRange> m_vRanges;
		std::string m_Error;
		int m_Samples = 0;
		bool m_Cancelled = false;
	};
	// 363f00: replay only the primary tee, change its tune-zone hook fire speed.
	bool SimulateReplayAtHookSpeed(const CReplay &Replay, const CGameWorld &Initial, int Id, float Speed, vec2 &EndPosition, const std::atomic<bool> *pCancel = nullptr);
	// 363b40/3638f0: float-add sweep [79,81], step .01; distance < .01.
	CReplayStabilityResult AnalyzeReplayStability(const CReplay &Replay, const CGameWorld &Initial, int Id, float ReferenceSpeed, const std::atomic<bool> *pCancel = nullptr, IEngine *pEngine = nullptr);
}
#endif
