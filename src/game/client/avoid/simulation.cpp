#include "simulation.h"

#include <engine/engine.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/pickup.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <future>
#include <atomic>
#include <chrono>

namespace OpenKrx
{
namespace
{
class CGreedyCandidateJob final : public IJob
{
	const IBlatantSimulation &m_World;
	CNetObj_PlayerInput m_Input;
	int m_CheckTicks;
	std::promise<CBlatantPlan> m_Result;
	std::atomic<bool> m_Claimed{false};

	void Run() override
	{
		RunInline();
	}

public:
	CGreedyCandidateJob(const IBlatantSimulation &World, CNetObj_PlayerInput Input, int CheckTicks) :
		m_World(World), m_Input(Input), m_CheckTicks(CheckTicks)
	{
		Abortable(false);
	}
	std::future<CBlatantPlan> Result() { return m_Result.get_future(); }
	void RunInline()
	{
		// A queued job may be claimed by the input thread when the shared
		// engine pool is busy with unrelated work. A late worker then returns
		// without ever dereferencing the (possibly already destroyed) world.
		if(m_Claimed.exchange(true, std::memory_order_acq_rel))
			return;
		m_Result.set_value(CBlatantPlanner::Greedy(m_World, m_Input, m_CheckTicks));
	}
};
}

bool CGameWorldSimulation::CanElideIndependentProbes() const
{
	// Each Clone owns its events/switchers and rebinds character callbacks.
	// CopyWorldIsolated does not copy PRNG pointers; tele-hook RandomOr0 thus
	// consumes no external stream. Unknown/nonisolated adapters do not opt in.
	// Laser::DoBounce's only collision write requires Res==-1, unreachable
	// through this adapter's IntersectLineTeleWeapon (returns tile IDs or0).
	// This serial probe-elision contract is separate from parallel job safety.
	return !m_World.m_pParent && !m_World.m_pChild && !m_World.m_Core.m_pPrng;
}

std::vector<CBlatantPlan> CGameWorldSimulation::SearchCandidates(const std::vector<CNetObj_PlayerInput> &vInputs, int CheckTicks) const
{
	bool ParallelSafe = m_pJobEngine != nullptr && vInputs.size() > 1;
	// Laser bounces temporarily patch the shared collision grid. Neither existing
	// lasers nor weapons/pickups that can spawn them may run in parallel.
	auto &World = const_cast<CGameWorld &>(m_World);
	if(World.FindFirst(CGameWorld::ENTTYPE_LASER))
		ParallelSafe = false;
	if(ParallelSafe && World.m_WorldConfig.m_PredictWeapons)
	{
		for(auto *pCharacter = static_cast<CCharacter *>(World.FindFirst(CGameWorld::ENTTYPE_CHARACTER)); pCharacter;
			pCharacter = static_cast<CCharacter *>(pCharacter->TypeNext()))
			if(pCharacter->GetWeaponGot(WEAPON_LASER) || pCharacter->GetWeaponGot(WEAPON_SHOTGUN))
				ParallelSafe = false;
		for(auto *pPickup = static_cast<CPickup *>(World.FindFirst(CGameWorld::ENTTYPE_PICKUP)); pPickup;
			pPickup = static_cast<CPickup *>(pPickup->TypeNext()))
			if(pPickup->Type() == POWERUP_WEAPON && (pPickup->Subtype() == WEAPON_LASER || pPickup->Subtype() == WEAPON_SHOTGUN))
				ParallelSafe = false;
	}
	if(!ParallelSafe)
		return IBlatantSimulation::SearchCandidates(vInputs, CheckTicks);

	// 0x1402d30b0 waits for all independent greedy jobs, then selects by input
	// index. Futures are stored in that order, never by completion time.
	std::vector<std::future<CBlatantPlan>> vPending;
	std::vector<std::shared_ptr<CGreedyCandidateJob>> vJobs;
	vPending.reserve(vInputs.size());
	vJobs.reserve(vInputs.size());
	for(const auto &Input : vInputs)
	{
		auto pJob = std::make_shared<CGreedyCandidateJob>(*this, Input, CheckTicks);
		vPending.push_back(pJob->Result());
		vJobs.push_back(pJob);
		m_pJobEngine->AddJob(std::move(pJob));
	}
	for(size_t Index = 0; Index < vPending.size(); ++Index)
	{
		if(vPending[Index].wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			vJobs[Index]->RunInline();
		vPending[Index].wait();
	}
	std::vector<CBlatantPlan> vResults;
	vResults.reserve(vPending.size());
	for(auto &Pending : vPending)
		vResults.push_back(Pending.get());
	return vResults;
}

std::unique_ptr<IBlatantSimulation> CGameWorldSimulation::Clone() const
{
	auto pResult = std::make_unique<CGameWorldSimulation>();
	pResult->CopyFrom(m_World, m_LocalClientId);
	pResult->m_pGameClient = m_pGameClient;
	pResult->m_pJobEngine = m_pJobEngine;
	pResult->m_LimitAutomaticFire = m_LimitAutomaticFire;
	pResult->m_AllowImmediateShot = m_AllowImmediateShot;
	pResult->m_AutomaticFireStep = m_AutomaticFireStep;
	return pResult;
}

void CGameWorldSimulation::CopyFrom(const CGameWorld &SourceWorld, int LocalClientId)
{
	m_World.CopyWorldIsolated(&SourceWorld);
	m_LocalClientId = LocalClientId;
	m_pLocal = LocalClientId >= 0 ? m_World.GetCharacterById(LocalClientId) : nullptr;
	m_pGameClient = nullptr;
	m_pJobEngine = nullptr;
	m_LimitAutomaticFire = m_AllowImmediateShot = false;
	m_AutomaticFireStep = 0;
}

void CGameWorldSimulation::PreparePlayers(bool PredictPlayers, CGameClient *pGameClient)
{
	m_pGameClient = pGameClient;
	m_pJobEngine = pGameClient ? pGameClient->Engine() : nullptr;
	if(!m_pLocal)
		return;
	// 0x1402cd0c0: discard inactive/other-team/distant tees, then remove all
	// remaining non-local tees when krx_avoidtileplayerprediction is disabled.
	const float Radius = HookLength() + 56.0f;
	for(int Id = 0; Id < MAX_CLIENTS; ++Id)
	{
		auto *pCharacter = m_World.GetCharacterById(Id);
		if(!pCharacter || Id == m_LocalClientId || pCharacter->m_IsLocal)
			continue;
		if(!PredictPlayers || distance(m_pLocal->m_Pos, pCharacter->m_Pos) > Radius ||
			(pGameClient && ((!pGameClient->m_Snap.m_aCharacters[Id].m_Active && pCharacter->m_SnapTicks > 10) || pGameClient->IsOtherTeam(Id))))
			delete pCharacter;
	}
}

void CGameWorldSimulation::Tick(const CNetObj_PlayerInput &Input)
{
	if(!m_pLocal)
		return;
	CNetObj_PlayerInput Copy = Input;
	if(m_LimitAutomaticFire && (!m_AllowImmediateShot || m_AutomaticFireStep > 0))
	{
		// Use the world's current counter, not the candidate's possibly stale
		// NSIF counter: jumping between even counters can still create presses.
		Copy.m_Fire = m_pLocal->DiagnosticLatestInput().m_Fire;
		if(Copy.m_Fire & 1)
			Copy.m_Fire = (Copy.m_Fire + 1) & INPUT_STATE_MASK;
	}
	++m_AutomaticFireStep;
	m_pLocal->OnDirectInput(&Copy);
	m_World.m_GameTick++;
	m_pLocal->OnPredictedInput(&Copy);
	m_World.Tick();
	m_pLocal = m_World.GetCharacterById(m_LocalClientId);
}

bool CGameWorldSimulation::Frozen() const
{
	// 0x1402cd750 treats the tee as frozen while the freeze counter or any of
	// the deep/live freeze flags is set; a destroyed character cannot move
	// either, so death ends the search just like a freeze.
	if(!m_pLocal)
		return true;
	return m_pLocal->m_FreezeTime > 0 || m_pLocal->Core()->m_DeepFrozen || m_pLocal->Core()->m_LiveFrozen;
}

bool CGameWorldSimulation::OnUnfreezeTile() const
{
	if(!m_pLocal)
		return false;
	// KRX's custom 0x14023cd10 returns the raw game-layer tile. DDNet's
	// GetCollisionAt filters out tile 11 entirely, so it cannot be used here.
	const vec2 Pos(static_cast<int>(m_pLocal->m_Pos.x), static_cast<int>(m_pLocal->m_Pos.y));
	return m_World.Collision()->GetTileIndex(m_World.Collision()->GetPureMapIndex(Pos)) == TILE_UNFREEZE;
}

vec2 CGameWorldSimulation::Position() const
{
	return m_pLocal ? m_pLocal->m_Pos : vec2(0.0f, 0.0f);
}

float CGameWorldSimulation::HookLength() const
{
	return m_pLocal ? m_pLocal->GlobalTuning()->m_HookLength : 380.0f;
}

std::optional<vec2> CGameWorldSimulation::HookPoint(vec2 Aim) const
{
	if(!m_pLocal)
		return std::nullopt;
	// 0x1402b3980 first quantizes the aim as an input, then probes the full
	// hook radius irrespective of the mouse's distance from the tee.
	const vec2 QuantizedAim(static_cast<int>(Aim.x), static_cast<int>(Aim.y));
	if(QuantizedAim == vec2(0.0f, 0.0f))
		return std::nullopt;
	const vec2 To = m_pLocal->m_Pos + normalize(QuantizedAim) * (HookLength() - 28.0f);
	vec2 HitPos = To;
	// KRX tests against interpolated live character positions before tracing
	// the map. Keep that test independent of the speculative movement world.
	if(m_pGameClient && m_pGameClient->IntersectCharacter(m_pLocal->m_Pos, To, HitPos, m_LocalClientId) != -1)
		return std::nullopt;
	const int Hit = m_World.Collision()->IntersectLineTeleHook(m_pLocal->m_Pos, To, &HitPos, nullptr, nullptr);
	if(Hit == TILE_SOLID)
		return HitPos;
	return std::nullopt;
}

std::unique_ptr<IBlatantSimulation> CreateSimulation(CGameClient *pGameClient, int ClientId, bool PredictPlayers, bool PendingAutomaticFire, bool AllowImmediateShot)
{
	auto pResult = std::make_unique<CGameWorldSimulation>();
	// 0x1402cd0c0 always starts from the predicted world (client+0x5b3f38).
	// Player prediction only decides whether the snapshot contains other tees.
	const CGameWorld *pSource = &pGameClient->m_GameWorld;
	if(pGameClient->m_PredictedWorld.GetCharacterById(ClientId))
		pSource = &pGameClient->m_PredictedWorld;
	pResult->CopyFrom(*pSource, ClientId);
	pResult->PreparePlayers(PredictPlayers, pGameClient);
	if(PendingAutomaticFire)
		pResult->LimitPendingAutomaticFire(AllowImmediateShot);
	return pResult;
}
}

std::optional<vec2> OpenKrx::CGameWorldSimulation::DragAim() const
{
 if(!m_pGameClient || !m_pLocal) return std::nullopt;
 return m_pGameClient->m_Aimbot.FindDragAim(m_World, m_LocalClientId, m_pGameClient,
  m_pGameClient->m_Controls.m_aMousePos[g_Config.m_ClDummy]);
}
