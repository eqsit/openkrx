#ifndef GAME_CLIENT_KRX_REPLAY_FAKE_AIM_H
#define GAME_CLIENT_KRX_REPLAY_FAKE_AIM_H

#include "replay.h"

class CGameWorld;
namespace OpenKrx
{
	// New KRX 3556e0/34fc40/350560: validate Robot edits, then style editable aims.
	void ResetReplayFakeAim(CReplay &Replay);
	bool AddReplayFakeAim(CReplay &Replay, const CGameWorld &Initial, const std::array<int, 2> &aIds, int Mode, float MouseRadius);
}
#endif
