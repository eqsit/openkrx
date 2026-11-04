#ifndef GAME_CLIENT_KRX_REPLAY_H
#define GAME_CLIENT_KRX_REPLAY_H

#include <base/vmath.h>

#include <generated/protocol.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace OpenKrx
{
	struct CReplayFrame
	{
		CNetObj_PlayerInput m_Input{};
		vec2 m_Position = vec2(0, 0);
		bool m_AimEditable = false; // Runtime marker; absent from the replay file format.
	};
	struct CReplay
	{
		std::array<std::vector<CReplayFrame>, 2> m_aFrames;
		std::string m_Author, m_Description, m_Map, m_Date;
		bool m_AimPrepared = false;
	};
	// 2f5b70 / 2f2cf0: little endian JSON size followed by zlib level 9.
	// Loading is transactional; a broken file cannot destroy the current run.
	bool EncodeReplay(const CReplay &Replay, std::vector<unsigned char> &vBytes, std::string &Error);
	bool DecodeReplay(const std::vector<unsigned char> &vBytes, CReplay &Replay, std::string &Error);
	bool DecodeReplayJson(std::string_view Text, CReplay &Replay, std::string &Error);
	uint32_t PackFentInput(const CNetObj_PlayerInput &Input);
	CNetObj_PlayerInput UnpackFentInput(uint32_t Packed);
}
#endif
