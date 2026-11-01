#ifndef GAME_CLIENT_KRX_BOTS_H
#define GAME_CLIENT_KRX_BOTS_H

#include <base/vmath.h>

#include <engine/client.h>

#include <generated/protocol.h>

#include <array>

class CGameClient;
class CGameWorld;

namespace OpenKrx
{
	// Recovered medium-size coordinator consumers. Call individual stages in
	// their original order; they are intentionally not a single early hook.
	class CMediumBots
	{
		struct CSlot
		{
			int m_ClientId = -1;
			int m_BalanceTarget = -1;
			vec2 m_HookRidePosition = vec2(0, 0);
			vec2 m_JetRidePosition = vec2(0, 0);
			bool m_HookRideActive = false;
			bool m_JetRideActive = false;
			bool m_JumpPending = false;
			bool m_NearestAutoIssued = false;
			vec2 m_RobotAim = vec2(0, 0);
			float m_SpinAngle = 0.0f;
		};
		std::array<CSlot, NUM_DUMMIES> m_aSlots{};
		CSlot &State(CGameClient *pGameClient);

	public:
		void OnReset();
		CNetObj_PlayerInput HookRide(CGameClient *pGameClient, CNetObj_PlayerInput Input);
		CNetObj_PlayerInput HookNearest(CGameClient *pGameClient, CNetObj_PlayerInput Input);
		CNetObj_PlayerInput JetRide(CGameClient *pGameClient, CNetObj_PlayerInput Input);
		CNetObj_PlayerInput FakeAim(CGameClient *pGameClient, CNetObj_PlayerInput Input);
		CNetObj_PlayerInput Balance(CGameClient *pGameClient, CNetObj_PlayerInput Input);
		CNetObj_PlayerInput AutoJumpSave(CGameClient *pGameClient, CNetObj_PlayerInput Input);

		static int ClosestPlayer(CGameClient *pGameClient, vec2 Position = vec2(0, 0), int SourceId = -1);
		static int Life(const CGameWorld &World, int ClientId, const CNetObj_PlayerInput &Input, int Ticks);
		static float BalanceScore(const CGameWorld &World, int ClientId, int TargetId, const CNetObj_PlayerInput &Input, float StopDistance);
		static bool JumpSaveWorld(const CGameWorld &World, int ClientId, CNetObj_PlayerInput Input, int Mode, vec2 Velocity);
		static vec2 FakeAimVector(int Mode, int Speed, vec2 Mouse, const CNetObj_PlayerInput &Input,
			int PreviousHook, vec2 ClosestDirection, float MouseRadius,
			vec2 &RobotAim, float &SpinAngle, float RandomAngle);
	};
}

#endif
