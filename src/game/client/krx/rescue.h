#ifndef GAME_CLIENT_KRX_RESCUE_H
#define GAME_CLIENT_KRX_RESCUE_H

#include <cstdint>

// The generated protocol uses int32_t through its message dependencies.
#include <base/vmath.h>

#include <generated/protocol.h>

#include <vector>
#include <optional>

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
		CPulse m_AledPulse;
		CPulse m_UnfreezePulse;
		std::vector<vec2> m_vUnfreezePath;
		std::vector<vec2> m_vUnfreezeTeePath;
		vec2 m_LastUnfreezeAim = vec2(0, 0);
		bool m_UnfreezeAimReleased = true;

	public:
		struct CBeamResult
		{
			bool m_Found = false;
			vec2 m_Aim = vec2(0, 0);
			int m_Bounces = -1;
			float m_Speed = 0;
			std::vector<vec2> m_vPath;
			std::vector<vec2> m_vTeePath;
		};
		struct CAledResult
		{
			bool m_Found = false;
			bool m_InitialHit = false;
			bool m_SearchedHit = false;
			vec2 m_Aim = vec2(0, 0);
		};
		void OnReset();
		void AdjustDummyFly(CGameClient *pGameClient);
		CNetObj_PlayerInput AdjustAutoAled(CGameClient *pGameClient, CNetObj_PlayerInput Input, int ClientId);
		CNetObj_PlayerInput AdjustAutoShotgun(CGameClient *pGameClient, CNetObj_PlayerInput Input, int ClientId);
		CNetObj_PlayerInput AdjustAutoUnfreeze(CGameClient *pGameClient, CNetObj_PlayerInput Input, int ClientId);
		const std::vector<vec2> &UnfreezePath() const { return m_vUnfreezePath; }
		const std::vector<vec2> &UnfreezeTeePath() const { return m_vUnfreezeTeePath; }
		void RenderUnfreeze(CGameClient *pGameClient);
		static CBeamResult ProbeBeam(const CGameWorld &Source, int ClientId, CNetObj_PlayerInput Input, int Ticks,
			bool RequireUnfreeze, CGameClient *pGameClient = nullptr, bool FullTrajectory = false);
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
		// New 1.36.1 helper33c4f0: evaluate hammer now and at delays1..5.
		static std::optional<CNetObj_PlayerInput> SearchDummyHammerWorld(const CGameWorld &Source, int DummyId, int LocalId, float MouseRadius, int MaxDelay = 5);
		static CNetObj_PlayerInput DummyFlyWorld(const CGameWorld &Source, int LocalId, int DummyId,
			CNetObj_PlayerInput DummyInput, CNetObj_PlayerInput OtherLiveInput, int ActiveDirection,
			bool AllowHook, bool Kinta, float MouseRadius, CGameClient *pGameClient = nullptr);
	};
}

#endif
