#include "pilot_bot.h"
#include "rocket_save.h"

#include <engine/engine.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>

#include <game/client/gameclient.h>
#include <game/client/krx/flow_field.h>
#include <game/client/krx/msvc_sort.h>
#include <game/client/prediction/entities/character.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <future>
#include <thread>

namespace OpenKrx
{
namespace
{
int ClosestPilotTarget(CGameClient *pGameClient, int Id)
{
	const auto &Own = pGameClient->m_aClients[Id];
	if(Own.m_Solo || Own.m_Spec || Own.m_HookHitDisabled)
		return -1;
	int BestId = -1;
	float BestDistance = 0;
	for(auto *pCharacter = static_cast<CCharacter *>(pGameClient->KrxPredictedWorld().FindFirst(CGameWorld::ENTTYPE_CHARACTER)); pCharacter; pCharacter = static_cast<CCharacter *>(pCharacter->TypeNext()))
	{
		const int OtherId = pCharacter->GetId();
		if(OtherId < 0 || OtherId >= MAX_CLIENTS || OtherId == Id)
			continue;
		const auto &Other = pGameClient->m_aClients[OtherId];
		if(!Other.m_Active || Other.m_Solo || Other.m_Spec || !pGameClient->m_Teams.CanCollide(OtherId, Id))
			continue;
		const float Distance = distance(pGameClient->m_LocalCharacterPos, Other.m_Predicted.m_Pos);
		if(BestId < 0 || Distance < BestDistance)
		{
			BestId = OtherId;
			BestDistance = Distance;
		}
	}
	return BestId;
}

bool Step(CGameWorld &World, int Id, const CNetObj_PlayerInput &Input)
{
	auto *pLocal = World.GetCharacterById(Id);
	if(!pLocal)
		return false;
	pLocal->OnDirectInput(&Input);
	++World.m_GameTick;
	pLocal->OnPredictedInput(&Input);
	World.Tick();
	World.m_PredictedEvents.clear();
	return World.GetCharacterById(Id) != nullptr;
}

bool SafeStep(CGameWorld &World, int Id, const CNetObj_PlayerInput &Input, bool Edge)
{
	if(!Step(World, Id, Input))
		return false;
	const auto *pLocal = World.GetCharacterById(Id);
	// 317f80 reads persistent FreezeTime, not IsInFreeze; Pilot additionally
	// rejects raw game-layer death at the post-tick position (33ba40).
	return pLocal->m_FreezeTime <= 0 && (Edge || World.Collision()->GetTileIndex(World.Collision()->GetPureMapIndex(pLocal->m_Pos)) != TILE_DEATH);
}

vec2 GuidedDirection(const CMovementSequenceSearch::CContext &Context, vec2 Position, bool &Guided)
{
	if(Context.m_Mode == 1 || Context.m_Mode == 2)
	{
		const vec2 Delta = Context.m_Target - Position;
		return Delta == vec2(0, 0) ? vec2(0, 0) : normalize(Delta);
	}
	const auto *pCell = Context.m_pField ? Context.m_pField->At(Position) : nullptr;
	if(!pCell || !pCell->Reachable())
	{
		Guided = false;
		return vec2(0, 0);
	}
	return -pCell->m_Flow;
}

void BiasedJump(std::mt19937 &Random, CNetObj_PlayerInput &Input, float Dy)
{
	if(Dy < -0.5f && CMovementSequenceSearch::Unit(Random) < 0.5f)
		Input.m_Jump = 1;
	else if(CMovementSequenceSearch::Unit(Random) < 0.1f)
		Input.m_Jump = (int)(Random() >> 31);
	else
		Input.m_Jump = 0;
}

class CPopulationJob final : public IJob
{
	const CMovementSequenceSearch::CContext &m_Context;
	const CNetObj_PlayerInput &m_Base;
	std::vector<CMovementSequenceSearch::CCandidate> &m_vPopulation;
	int m_Begin, m_End, m_Depth;
	std::promise<void> m_Completed;
	std::atomic<bool> m_Claimed{false};
	void Run() override { RunInline(); }

public:
	CPopulationJob(const CMovementSequenceSearch::CContext &Context, const CNetObj_PlayerInput &Base, std::vector<CMovementSequenceSearch::CCandidate> &vPopulation, int Begin, int End, int Depth) :
		m_Context(Context), m_Base(Base), m_vPopulation(vPopulation), m_Begin(Begin), m_End(End), m_Depth(Depth)
	{
		Abortable(false);
	}
	std::future<void> Result() { return m_Completed.get_future(); }
	void RunInline()
	{
		if(m_Claimed.exchange(true, std::memory_order_acq_rel))
			return;
		// Each worker owns mutable collision layers and tuning. In particular,
		// laser bounce probes may temporarily edit collision tiles, so sharing
		// the live map forced the previous implementation onto one thread.
		auto pCollision = m_Context.m_pWorld->Collision()->CreateIsolatedCopy();
		std::array<CTuningParams, 256> aTuning;
		std::copy_n(m_Context.m_pWorld->TuningList(), aTuning.size(), aTuning.begin());
		CGameWorld Source;
		Source.CopyWorldIsolated(m_Context.m_pWorld);
		Source.RebindCollision(pCollision.get());
		Source.RebindTuning(aTuning.data());
		auto Context = m_Context;
		Context.m_pWorld = &Source;
		const auto *pLocal = Source.GetCharacterById(Context.m_Id);
		std::random_device Seed;
		for(int Index = m_Begin; Index < m_End; ++Index)
		{
			std::mt19937 Random(Seed());
			auto &Candidate = m_vPopulation[Index];
			Candidate.m_vInputs = m_Context.m_Edge ?
				CMovementSequenceSearch::GenerateEdge(Random, m_Base, pLocal->m_Pos, pLocal->Core()->m_Vel, m_Context.m_Target, m_Depth) :
				CMovementSequenceSearch::GeneratePilot(Random, m_Base, m_Context, pLocal->m_Pos, pLocal->Core()->m_Vel, m_Depth, (float)Index < (float)m_vPopulation.size() * 0.5f);
			Candidate.m_Score = CMovementSequenceSearch::Evaluate(Context, Candidate.m_vInputs);
		}
		m_Completed.set_value();
	}
};
}

uint32_t CMovementSequenceSearch::Uniform(std::mt19937 &Random, uint32_t Count)
{
	if(!Count)
		return 0;
	uint64_t Product = (uint64_t)Random() * Count;
	while((uint32_t)Product < (uint32_t)(-Count) % Count)
		Product = (uint64_t)Random() * Count;
	return (uint32_t)(Product >> 32);
}

float CMovementSequenceSearch::Unit(std::mt19937 &Random)
{
	return (float)(Random() >> 8) * 0x1p-24f;
}

std::vector<CNetObj_PlayerInput> CMovementSequenceSearch::GenerateEdge(std::mt19937 &Random, const CNetObj_PlayerInput &Base, vec2 Position, vec2 Velocity, vec2 Target, int Depth)
{
	std::vector<CNetObj_PlayerInput> vInputs;
	vInputs.reserve(Depth);
	auto Input = Base;
	for(int Tick = 0; Tick < Depth; ++Tick)
	{
		const vec2 Delta = Target - Position;
		const vec2 Direction = Delta == vec2(0, 0) ? vec2(0, 0) : normalize(Delta);
		if((Tick & 1) == 0)
			Input.m_Direction = Unit(Random) < 0.5f ? (Direction.x < -0.1f ? -1 : (int)(Direction.x > 0.1f)) : (int)Uniform(Random, 3) - 1;
		BiasedJump(Random, Input, Direction.y);
		Input.m_Fire = Input.m_Hook = 0;
		vInputs.push_back(Input);
		// Original generator extrapolates velocity by .02 and never simulates
		// this guide position; physical validation happens in a fresh world.
		Position += Velocity * 0.02f;
	}
	return vInputs;
}

std::vector<CNetObj_PlayerInput> CMovementSequenceSearch::GeneratePilot(std::mt19937 &Random, const CNetObj_PlayerInput &Base, const CContext &Context, vec2 Position, vec2 Velocity, int Depth, bool Guided)
{
	std::vector<CNetObj_PlayerInput> vInputs;
	vInputs.reserve(Depth);
	auto Input = Base;
	for(int Tick = 0; Tick < Depth; ++Tick)
	{
		vec2 Direction(0, 0);
		if(Guided)
			Direction = GuidedDirection(Context, Position, Guided);
		if(Guided)
		{
			Input.m_TargetX = (int)(Direction.x * 500.0f);
			Input.m_TargetY = (int)(Direction.y * 500.0f);
			Input.m_Direction = Unit(Random) < 0.8f ? (Direction.x < -0.1f ? -1 : (int)(Direction.x > 0.1f)) : (int)Uniform(Random, 3) - 1;
			BiasedJump(Random, Input, Direction.y);
			if(Unit(Random) < 0.5f)
				Input.m_Hook = (int)(Random() >> 31);
		}
		else if(Tick % 3 == 0)
		{
			Input.m_Direction = (int)Uniform(Random, 3) - 1;
			Input.m_Hook = (int)(Random() >> 31);
			Input.m_Jump = (int)(Random() >> 31);
			const float Fraction = Unit(Random);
			const float Angle = (Fraction + Fraction) * pi;
			Input.m_TargetX = (int)(std::cos(Angle) * 500.0f);
			Input.m_TargetY = (int)(std::sin(Angle) * 500.0f);
		}
		Input.m_Fire = 0;
		vInputs.push_back(Input);
		Position += Velocity * 0.02f;
	}
	return vInputs;
}

bool CMovementSequenceSearch::Validate(const CContext &Context, const std::vector<CNetObj_PlayerInput> &vInputs)
{
	CGameWorld Copy;
	Copy.CopyWorldIsolated(Context.m_pWorld);
	if(!Copy.GetCharacterById(Context.m_Id))
		return false;
	for(const auto &Input : vInputs)
		if(!SafeStep(Copy, Context.m_Id, Input, Context.m_Edge))
			return false;
	return true;
}

namespace
{
float ScoreSequence(const CMovementSequenceSearch::CContext &Context, const std::vector<CNetObj_PlayerInput> &vInputs, bool Validate)
{
	CGameWorld Copy;
	Copy.CopyWorldIsolated(Context.m_pWorld);
	float Penalty = 0;
	for(const auto &Input : vInputs)
	{
		if(!Step(Copy, Context.m_Id, Input))
			return -1;
		const auto *pLocal = Copy.GetCharacterById(Context.m_Id);
		if(Validate && (pLocal->m_FreezeTime > 0 || (!Context.m_Edge && Copy.Collision()->GetTileIndex(Copy.Collision()->GetPureMapIndex(pLocal->m_Pos)) == TILE_DEATH)))
			return -1;
		if(!Context.m_Edge)
		{
			if(Context.m_Mode == 1 || (Context.m_Mode == 2 && !Context.m_FollowPath))
			{
				if(Context.m_Mode == 2 && pLocal->Core()->HookedPlayer() == Context.m_TargetId)
					Penalty = 99999.0f;
			}
			else
			{
				const auto *pCell = Context.m_pField ? Context.m_pField->At(pLocal->m_Pos) : nullptr;
				if(!pCell || !pCell->Reachable())
					Penalty += 50.0f;
				else if(length(pLocal->Core()->m_Vel) > 1.0f)
					Penalty += 1.0f + dot(pCell->m_Flow, normalize(pLocal->Core()->m_Vel));
			}
		}
	}
	auto *pLocal = Copy.GetCharacterById(Context.m_Id);
	if(!pLocal)
		return -1;
	if(Context.m_Edge)
		return distance(pLocal->m_Pos, Context.m_Target) + (pLocal->IsGrounded() ? 0.0f : 5000.0f);
	if(Context.m_Mode == 1 || (Context.m_Mode == 2 && !Context.m_FollowPath))
		return distance(pLocal->m_Pos, Context.m_Target) + Penalty;
	const auto *pCell = Context.m_pField ? Context.m_pField->At(pLocal->m_Pos) : nullptr;
	return (pCell ? pCell->m_Cost : 999999.0f) + Penalty;
}
}

float CMovementSequenceSearch::Score(const CContext &Context, const std::vector<CNetObj_PlayerInput> &vInputs)
{
	return ScoreSequence(Context, vInputs, false);
}

float CMovementSequenceSearch::Evaluate(const CContext &Context, const std::vector<CNetObj_PlayerInput> &vInputs)
{
	return ScoreSequence(Context, vInputs, true);
}

std::vector<CMovementSequenceSearch::CCandidate> CMovementSequenceSearch::Search(const CContext &Context, const CNetObj_PlayerInput &Base, int Population, int Depth, IEngine *pEngine)
{
	std::vector<CCandidate> vPopulation(std::max(0, Population));
	if(!Context.m_pWorld || !const_cast<CGameWorld *>(Context.m_pWorld)->GetCharacterById(Context.m_Id))
		return {};
	const bool Parallel = pEngine && pEngine->JobThreadCount() > 0;
	// Original339360 partitions by the actual pool size, bounded by population.
	const int Workers = Parallel ? std::min(std::max(1, Population), pEngine->JobThreadCount()) : 1;
	std::vector<std::shared_ptr<CPopulationJob>> vJobs;
	std::vector<std::future<void>> vResults;
	for(int I = 0; I < Workers; ++I)
	{
		auto pJob = std::make_shared<CPopulationJob>(Context, Base, vPopulation, I * (Population / Workers) + std::min(I, Population % Workers), (I + 1) * (Population / Workers) + std::min(I + 1, Population % Workers), Depth);
		vResults.push_back(pJob->Result());
		vJobs.push_back(pJob);
		if(Parallel)
			pEngine->AddJob(pJob);
	}
	for(auto &pJob : vJobs)
		pJob->RunInline();
	for(auto &Result : vResults)
		Result.get();
	vPopulation.erase(std::remove_if(vPopulation.begin(), vPopulation.end(), [](const auto &Candidate) { return !(Candidate.m_Score >= 0); }), vPopulation.end());
	MsvcSort(vPopulation.begin(), vPopulation.end(), (ptrdiff_t)vPopulation.size(), [](const auto &A, const auto &B) { return A.m_Score < B.m_Score; });
	return vPopulation;
}

void CMovementSequenceSearch::PrepareWorld(CGameClient *pGameClient, CGameWorld &World, int Id)
{
	World.CopyWorldIsolated(&pGameClient->KrxPredictedWorld());
	World.m_WorldConfig.m_KrxPrediction = true;
	World.m_WorldConfig.m_KrxAvoidDeath = false;
	World.m_WorldConfig.m_KrxAvoidTeles = false;
	World.m_WorldConfig.m_KrxExplosionStrengthValid = true;
	World.m_WorldConfig.m_KrxExplosionStrength = pGameClient->m_aTuning[g_Config.m_ClDummy].m_ExplosionStrength;
	for(int Other = 0; Other < MAX_CLIENTS; ++Other)
	{
		auto *pCharacter = World.GetCharacterById(Other);
		if(!pCharacter || Other == Id)
			continue;
		if(!g_Config.m_AvoidPlayerPrediction || pGameClient->IsOtherTeam(Other) || (!pGameClient->m_Snap.m_aCharacters[Other].m_Active && pCharacter->m_SnapTicks > 10))
			delete pCharacter;
	}
}

void CPilotBot::OnReset()
{
	// Explicit host lifecycle/profile resets invalidate a plan for the old tee.
	// Ordinary temporary eligibility gates still preserve original agent state.
	*this = {};
}

CNetObj_PlayerInput CPilotBot::AdjustInput(CGameClient *pGameClient, CNetObj_PlayerInput Input, int ClientId)
{
	if(g_Config.m_AvoidAgentType != 4 || !g_Config.m_AvoidFreeze || ClientId < 0 || pGameClient->Client()->State() != IClient::STATE_ONLINE || !(Input.m_PlayerFlags & PLAYERFLAG_PLAYING) || pGameClient->m_Snap.m_SpecInfo.m_Active || !pGameClient->m_Snap.m_pLocalCharacter)
		return Input;
	if(pGameClient->m_PreTasVisuals.ShouldSuspendAvoid())
		return Input;
	if(pGameClient->m_aClients[ClientId].m_Super && pGameClient->Collision()->GetTileIndex(pGameClient->Collision()->GetPureMapIndex(pGameClient->m_LocalCharacterPos)) == TILE_FREEZE)
		return Input;
	if(IsKrxCharacterFrozen(pGameClient, ClientId))
		return Input;
	const int Mode = g_Config.m_AvoidPilotBotMode;
	if(Mode != m_Mode)
	{
		m_Mode = Mode;
		m_qPlan.clear();
		m_ReuseTicks = 0;
		m_PathTarget = vec2(-1, -1);
	}
	CGameWorld World;
	CMovementSequenceSearch::PrepareWorld(pGameClient, World, ClientId);
	auto *pLocal = World.GetCharacterById(ClientId);
	if(!pLocal)
		return Input;
	if(!m_HaveCachedInput)
	{
		// 33aff0 reads character+2d0 = core+e8, produced by PreTick25b8b0.
		m_CachedInput = pLocal->Core()->m_Input;
		m_HaveCachedInput = true;
	}
	if(m_ReuseTicks > 0 && !m_qPlan.empty())
	{
		m_CachedInput = m_qPlan.front();
		m_qPlan.pop_front();
		--m_ReuseTicks;
		return m_CachedInput;
	}
	CMovementSequenceSearch::CContext Context;
	Context.m_pWorld = &World;
	Context.m_Id = ClientId;
	Context.m_Mode = Mode;
	Context.m_Target = pLocal->m_Pos;
	Context.m_TargetId = ClosestPilotTarget(pGameClient, ClientId);
	if(Mode == 1)
		Context.m_Target += pGameClient->m_Controls.m_aMousePos[g_Config.m_ClDummy];
	else if(Mode == 2)
	{
		auto *pTarget = World.GetCharacterById(Context.m_TargetId);
		if(!pTarget)
			return Input;
		Context.m_Target = pTarget->m_Pos;
		const float NearDistance = (float)pGameClient->m_aTuning[g_Config.m_ClDummy].m_HookLength * 0.5f;
		if(distance(pLocal->m_Pos, Context.m_Target) < NearDistance)
			m_FollowState = m_StationaryTicks = 0;
		else
		{
			m_StationaryTicks = distance(pLocal->m_Pos, m_LastLocal) < 10.0f ? m_StationaryTicks + 1 : 0;
			if(m_StationaryTicks > 100)
				m_FollowState = 1;
			if(m_FollowState == 1 && distance(Context.m_Target, m_LastTarget) > 192.0f)
				m_PathTarget = vec2(-1, -1);
		}
		m_LastLocal = pLocal->m_Pos;
		m_LastTarget = Context.m_Target;
		Context.m_FollowPath = m_FollowState == 1;
		if(Context.m_FollowPath && distance(Context.m_Target, m_PathTarget) > 192.0f)
		{
			m_PathTarget = Context.m_Target;
			pGameClient->m_Tas.SetGoal(Context.m_Target);
		}
	}
	if(Mode == 0 || Context.m_FollowPath)
	{
		pGameClient->m_Tas.UpdatePathFinding();
		Context.m_pField = &pGameClient->m_Tas.FlowField();
		if(!Context.m_pField->Ready())
			return Input;
	}
	auto vPopulation = CMovementSequenceSearch::Search(Context, m_CachedInput, g_Config.m_AvoidPilotBotPopulationSize, g_Config.m_AvoidPilotBotExplorationDepth, pGameClient->Engine());
	if(vPopulation.empty())
		return Input;
	const int TopK = std::min((int)vPopulation.size(), g_Config.m_AvoidPilotBotTopKCandidates);
	for(int I = 0; I < TopK; ++I)
		if(CMovementSequenceSearch::Validate(Context, vPopulation[I].m_vInputs))
		{
			m_qPlan.assign(vPopulation[I].m_vInputs.begin(), vPopulation[I].m_vInputs.end());
			m_ReuseTicks = g_Config.m_AvoidPilotBotSequenceLength;
			m_CachedInput = m_qPlan.front();
			m_qPlan.pop_front();
			--m_ReuseTicks;
			return m_CachedInput;
		}
	m_CachedInput = vPopulation.front().m_vInputs.front();
	return m_CachedInput;
}
}
