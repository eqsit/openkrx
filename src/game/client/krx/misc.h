#ifndef GAME_CLIENT_KRX_MISC_H
#define GAME_CLIENT_KRX_MISC_H

#include <base/vmath.h>

#include <engine/client.h>

#include <array>
#include <cstdint>

class CGameClient;

namespace OpenKrx
{

	class CMiscFeatures
	{
		struct CSlot
		{
			int m_ClientId = -1;
			int m_LastEmoteTick = -1;
			int64_t m_LastTeamAttempt = 0;
			int64_t m_AntiAfkStarted = 0;
			bool m_WasFrozen = false;
			bool m_NegativeMouseStep = false;
		};
		std::array<CSlot, NUM_DUMMIES> m_aSlots{};
		CSlot *PrepareContext(CGameClient *pGameClient);

	public:
		void OnReset();
		void AutoTeam(CGameClient *pGameClient);
		void BeforeAvoid(CGameClient *pGameClient);
		void AfterAvoid(CGameClient *pGameClient);
		static vec2 QuantizedAim(vec2 Direction, float Radius);
	};

} // namespace OpenKrx

#endif
