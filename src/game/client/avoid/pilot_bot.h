#ifndef GAME_CLIENT_AVOID_PILOT_BOT_H
#define GAME_CLIENT_AVOID_PILOT_BOT_H

#include <base/vmath.h>
#include <generated/protocol.h>

#include <deque>
#include <random>
#include <vector>

class CGameClient;
class CGameWorld;
class IEngine;

namespace OpenKrx
{
class CFlowField;

// Shared sequence machinery recovered from BLAutoEdge and BLPilotBot. Neither
// planner uses the old three constant-direction AutoEdge probes.
class CMovementSequenceSearch
{
public:
	struct CCandidate
	{
		std::vector<CNetObj_PlayerInput> m_vInputs;
		float m_Score = -1;
	};
	struct CContext
	{
		const CGameWorld *m_pWorld = nullptr;
		const CFlowField *m_pField = nullptr;
		int m_Id = -1;
		int m_Mode = 0;
		int m_TargetId = -1;
		bool m_FollowPath = false;
		bool m_Edge = false;
		vec2 m_Target = vec2(0, 0);
	};
	static uint32_t Uniform(std::mt19937 &Random, uint32_t Count);
	static float Unit(std::mt19937 &Random);
	static std::vector<CNetObj_PlayerInput> GenerateEdge(std::mt19937 &Random, const CNetObj_PlayerInput &Base, vec2 Position, vec2 Velocity, vec2 Target, int Depth);
	static std::vector<CNetObj_PlayerInput> GeneratePilot(std::mt19937 &Random, const CNetObj_PlayerInput &Base, const CContext &Context, vec2 Position, vec2 Velocity, int Depth, bool Guided);
	static bool Validate(const CContext &Context, const std::vector<CNetObj_PlayerInput> &vInputs);
	static float Score(const CContext &Context, const std::vector<CNetObj_PlayerInput> &vInputs);
	// Same validity and score as the two independent reference passes; one
	// isolated trajectory avoids repeating every accepted candidate's physics.
	static float Evaluate(const CContext &Context, const std::vector<CNetObj_PlayerInput> &vInputs);
	static std::vector<CCandidate> Search(const CContext &Context, const CNetObj_PlayerInput &Base, int Population, int Depth, IEngine *pEngine, int BudgetMilliseconds = 0);
	static void PrepareWorld(CGameClient *pGameClient, CGameWorld &World, int Id);
};

class CPilotBot
{
	int m_Mode = -1;
	int m_FollowState = 0;
	int m_StationaryTicks = 0;
	int m_ReuseTicks = 0;
	vec2 m_LastLocal = vec2(0, 0);
	vec2 m_LastTarget = vec2(-1, -1);
	vec2 m_PathTarget = vec2(-1, -1);
	CNetObj_PlayerInput m_CachedInput = {};
	bool m_HaveCachedInput = false;
	std::deque<CNetObj_PlayerInput> m_qPlan;

public:
	void OnReset();
	CNetObj_PlayerInput AdjustInput(CGameClient *pGameClient, CNetObj_PlayerInput Input, int ClientId);
};
}

#endif
