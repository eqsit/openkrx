#ifndef GAME_CLIENT_KRX_REPLAY_FAKE_AIM_H
#define GAME_CLIENT_KRX_REPLAY_FAKE_AIM_H

#include "replay.h"

class CGameWorld;
namespace OpenKrx
{
	// 2ebfd0: collect aim-critical inputs, then fill gaps before each one.
	bool AddReplayFakeAim(CReplay &Replay, const CGameWorld &Initial, const std::array<int, 2> &aIds, int Mode, float MouseRadius);
}
#endif
