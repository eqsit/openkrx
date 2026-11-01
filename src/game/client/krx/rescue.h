#ifndef GAME_CLIENT_KRX_RESCUE_H
#define GAME_CLIENT_KRX_RESCUE_H

#include <cstdint>

// The generated protocol uses int32_t through its message dependencies.
#include <base/vmath.h>

#include <generated/protocol.h>

#include <vector>

class CGameClient;
class CGameWorld;

namespace OpenKrx
{
	struct CDummyFlyDecision
	{
		CNetObj_PlayerInput m_Input{};
		// -1 means preserve the current setting, exactly as the native branch.
		int m_Hammer = -1;
	};

	class CRescueBots
	{
		struct CPulse
		{
			int m_Modulo = 0;
			bool Release(CNetObj_PlayerInput &Input, int GameTick);
			void Press(CNetObj_PlayerInput &Input);
		};
		CPulse m_aAledPulse[2];
		CPulse m_aUnfreezePulse[2];
		std::vector<vec2> m_vUnfreezePath;
		vec2 m_LastUnfreezeAim = vec2(0, 0);

	public:
		struct CBeamResult
		{
			bool m_Found = false;
			vec2 m_Aim = vec2(0, 0);
			int m_Bounces = -1;
			float m_Speed = 0;
			std::vector<vec2> m_vPath;
		};
		struct CAledResult
		{
			bool m_Found = false;
			vec2 m_Aim = vec2(0, 0);
		};
		void OnReset();
		void AdjustDummyFly(CGameClient *pGameClient);
		CNetObj_PlayerInput AdjustAutoAled(CGameClient *pGameClient, CNetObj_PlayerInput Input, int ClientId);
		CNetObj_PlayerInput AdjustAutoShotgun(CGameClient *pGameClient, CNetObj_PlayerInput Input, int ClientId);
		CNetObj_PlayerInput AdjustAutoUnfreeze(CGameClient *pGameClient, CNetObj_PlayerInput Input, int ClientId);
		const std::vector<vec2> &UnfreezePath() const { return m_vUnfreezePath; }
		static CBeamResult ProbeBeam(const CGameWorld &Source, int ClientId, CNetObj_PlayerInput Input, int Ticks,
			bool RequireUnfreeze, CGameClient *pGameClient = nullptr);
		static CBeamResult SearchShotgunWorld(const CGameWorld &Source, int ClientId, CNetObj_PlayerInput Input,
			vec2 Mouse, int Ticks, int Points, float Fov, bool MostBounces, bool HighestVelocity, float MouseRadius);
		static CBeamResult SearchUnfreezeWorld(const CGameWorld &Source, int ClientId, CNetObj_PlayerInput Input,
			vec2 Mouse, int Ticks, int Points, float Fov, bool MostBounces, float MouseRadius, CGameClient *pGameClient = nullptr);
		static CAledResult SearchAledWorld(const CGameWorld &Source, int ClientId, int TargetId,
			CNetObj_PlayerInput Input, vec2 Mouse, float MouseRadius);

		// Original non-TAS dummy-fly controller 0x1402bf440. Exposed for a
		// deterministic geometry/input test independent of network state.
		static CDummyFlyDecision DummyFlyDecision(vec2 LocalPosition, vec2 DummyPosition,
			int DummyHookedPlayer, float MouseRadius, CNetObj_PlayerInput DummyInput);
	};
}

#endif
