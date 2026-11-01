#ifndef GAME_CLIENT_AVOID_SIMULATION_H
#define GAME_CLIENT_AVOID_SIMULATION_H

#include "blatant.h"

#include <game/client/prediction/gameworld.h>

#include <memory>

class CGameClient;
class IEngine;

namespace OpenKrx
{
// Simulation backed by real DDNet prediction physics. Each instance owns an
// isolated copy of CGameWorld, so speculative ticks never touch the live world.
class CGameWorldSimulation final : public IBlatantSimulation
{
public:
	CGameWorldSimulation() = default;

	std::unique_ptr<IBlatantSimulation> Clone() const override;
	std::vector<CBlatantPlan> SearchCandidates(const std::vector<CNetObj_PlayerInput> &vInputs, int CheckTicks) const override;
	bool CanElideIndependentProbes() const override;
	bool Valid() const override { return m_pLocal != nullptr; }
	void Tick(const CNetObj_PlayerInput &Input) override;
	bool Frozen() const override;
	bool Dead() const override { return m_pLocal == nullptr; }
	bool OnUnfreezeTile() const override;
	vec2 Position() const override;
	float HookLength() const override;
	std::optional<vec2> HookPoint(vec2 Aim) const override;
	std::optional<vec2> DragAim() const override;

	void CopyFrom(const CGameWorld &SourceWorld, int LocalClientId);
	// User-authorized coupling fix: a pending Rocket press is not a promise
	// to hold Fire/aim through reload. Existing projectiles remain simulated.
	void LimitPendingAutomaticFire(bool AllowImmediateShot)
	{
		m_LimitAutomaticFire = true;
		m_AllowImmediateShot = AllowImmediateShot;
		m_AutomaticFireStep = 0;
	}
	// The KRX player-prediction setting controls other tees, not the time of
	// the world snapshot. Apply this only to the initial snapshot, not clones.
	void PreparePlayers(bool PredictPlayers, CGameClient *pGameClient = nullptr);
	// The standalone physics fixture can supply the same existing engine pool.
	void SetJobEngine(IEngine *pEngine) { m_pJobEngine = pEngine; }
	int LocalClientId() const { return m_LocalClientId; }

private:
	CGameWorld m_World;
	CCharacter *m_pLocal = nullptr;
	int m_LocalClientId = -1;
	CGameClient *m_pGameClient = nullptr;
	IEngine *m_pJobEngine = nullptr;
	bool m_LimitAutomaticFire = false;
	bool m_AllowImmediateShot = false;
	int m_AutomaticFireStep = 0;
};

// Snapshot of the live game world for the given client id (main or dummy).
std::unique_ptr<IBlatantSimulation> CreateSimulation(CGameClient *pGameClient, int ClientId, bool PredictPlayers, bool PendingAutomaticFire = false, bool AllowImmediateShot = false);
}

#endif
