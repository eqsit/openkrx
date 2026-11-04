#include "classic_agents.h"

#include "rocket_save.h"

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/components/krx_misc_pre_tas.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/gameworld.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace OpenKrx
{
	namespace
	{
		class CClassicWorld final : public IClassicSimulation
		{
			std::shared_ptr<CCollision> m_pCollision;
			std::shared_ptr<std::array<CTuningParams, 256>> m_pTuning;
			CGameWorld m_World;
			int m_ClientId = -1;

		public:
			CClassicWorld() = default;
			CClassicWorld(const CGameWorld &Source, int ClientId, bool Death, bool Teles, bool PredictPlayers, CGameClient *pClient, bool Prune)
			{
				m_pCollision = Source.Collision()->CreateIsolatedCopy();
				m_pTuning = std::make_shared<std::array<CTuningParams, 256>>();
				std::copy_n(Source.TuningList(), m_pTuning->size(), m_pTuning->begin());
				m_World.CopyWorldIsolated(&Source);
				m_World.RebindCollision(m_pCollision.get());
				m_World.RebindTuning(m_pTuning->data());
				m_World.m_WorldConfig.m_KrxPrediction = true;
				m_World.m_WorldConfig.m_KrxAvoidDeath = Death;
				m_World.m_WorldConfig.m_KrxAvoidTeles = Teles;
				m_ClientId = ClientId;
				if(pClient)
				{
					m_World.m_WorldConfig.m_KrxExplosionStrengthValid = true;
					m_World.m_WorldConfig.m_KrxExplosionStrength = pClient->m_aTuning[g_Config.m_ClDummy].m_ExplosionStrength;
				}
				const auto *pLocal = m_World.GetCharacterById(ClientId);
				if(!Prune || !pLocal)
					return;
				// Recovered328a30: preserved local/dummy characters, other-team/stale/
				// distant removal and then the separate player-prediction option.
				const float Radius = (pClient ? (float)pClient->m_aTuning[0].m_HookLength : (float)m_World.GlobalTuning()->m_HookLength) + 56.f;
				for(int Id = 0; Id < MAX_CLIENTS; ++Id)
				{
					auto *pOther = m_World.GetCharacterById(Id);
					if(!pOther || Id == ClientId || pOther->m_IsLocal)
						continue;
					if(!PredictPlayers || distance(pLocal->m_Pos, pOther->m_Pos) > Radius ||
						(pClient && ((!pClient->m_Snap.m_aCharacters[Id].m_Active && pOther->m_SnapTicks > 10) || pClient->IsOtherTeam(Id))))
						delete pOther;
				}
			}
			std::unique_ptr<IClassicSimulation> Clone() const override
			{
				auto Result = std::make_unique<CClassicWorld>();
				Result->m_ClientId = m_ClientId;
				Result->m_pCollision = m_pCollision;
				Result->m_pTuning = m_pTuning;
				Result->m_World.CopyWorldIsolated(&m_World);
				// Probes are strictly serial; they share only this isolated map copy.
				Result->m_World.RebindCollision(m_pCollision.get());
				Result->m_World.RebindTuning(m_pTuning->data());
				return Result;
			}
			bool Valid() const override { return const_cast<CGameWorld &>(m_World).GetCharacterById(m_ClientId) != nullptr; }
			void Tick(const CNetObj_PlayerInput &Input, bool Direct, bool Advance) override
			{
				auto *pCharacter = m_World.GetCharacterById(m_ClientId);
				if(pCharacter)
				{
					if(Direct)
						pCharacter->OnDirectInput(&Input);
					if(Advance)
						++m_World.m_GameTick;
					pCharacter->OnPredictedInput(&Input);
				}
				else if(Advance)
					++m_World.m_GameTick;
				m_World.Tick();
			}
			bool RawFrozen() const override
			{
				const auto *pCharacter = const_cast<CGameWorld &>(m_World).GetCharacterById(m_ClientId);
				return !pCharacter || pCharacter->m_FreezeTime > 0 || pCharacter->Core()->m_DeepFrozen || pCharacter->Core()->m_IsInFreeze;
			}
			bool OnUnfreezeTile() const override
			{
				const auto *pCharacter = const_cast<CGameWorld &>(m_World).GetCharacterById(m_ClientId);
				if(!pCharacter)
					return false;
				//328230 reads character+0xa4, the previous-position pair following
				// freeze fields (current core/entity position is+0x30).
				const vec2 Pos = pCharacter->m_PrevPos;
				const int Index = m_World.Collision()->GetPureMapIndex(vec2((int)Pos.x, (int)Pos.y));
				return m_World.Collision()->GetTileIndex(Index) == TILE_UNFREEZE || m_World.Collision()->GetFrontTileIndex(Index) == TILE_UNFREEZE;
			}
			CNetObj_PlayerInput CurrentInput() const override
			{
				const auto *pCharacter = const_cast<CGameWorld &>(m_World).GetCharacterById(m_ClientId);
				return pCharacter ? pCharacter->DiagnosticCurrentInput() : CNetObj_PlayerInput{};
			}
			vec2 Position() const override
			{
				const auto *pCharacter = const_cast<CGameWorld &>(m_World).GetCharacterById(m_ClientId);
				return pCharacter ? pCharacter->m_Pos : vec2(0, 0);
			}
		};

		struct CNode
		{
			CNode *m_pParent = nullptr;
			std::vector<std::unique_ptr<CNode>> m_vChildren;
			CNetObj_PlayerInput m_Input{};
			float m_Sum = 0;
			int m_Visits = 0, m_Life = 0;
			bool m_Dead = false;
		};

		void Propagate(CNode *pNode, float Life)
		{
			for(; pNode; pNode = pNode->m_pParent)
			{
				++pNode->m_Visits;
				pNode->m_Sum += Life;
				if(pNode->m_Dead)
					pNode->m_Sum = 0;
			}
		}

		CNode *Select(CNode &Parent, float Exploration, const CNetObj_PlayerInput &Raw, int PhysicalDirection, const CClassicSettings &Settings, const CClassicPlanner::CRandom &Random)
		{
			float Best = std::numeric_limits<float>::lowest();
			std::vector<CNode *> Ties;
			for(auto &pChild : Parent.m_vChildren)
			{
				auto &Child = *pChild;
				float Score = std::numeric_limits<float>::max();
				if(Child.m_Visits)
				{
					Score = Child.m_Sum / Child.m_Visits;
					if(Child.m_pParent)
						Score += std::sqrt(std::log((float)Child.m_pParent->m_Visits) / Child.m_Visits) * Exploration;
				}
				float Bias = 0;
				if(!Child.m_Dead)
				{
					Bias = std::abs(std::abs((float)Child.m_Input.m_Hook - Raw.m_Hook) - 1.f) * Settings.m_HookWeight * .01f +
					       Child.m_Life * (float)Settings.m_LifespanWeight * .01f +
					       std::abs(std::abs((float)Child.m_Input.m_Direction - PhysicalDirection) - 2.f) * Settings.m_DirectionWeight * .01f;
				}
				Score += Bias;
				if(Score > Best)
				{
					Best = Score;
					Ties.clear();
					Ties.push_back(&Child);
				}
				else if(Score == Best)
					Ties.push_back(&Child);
			}
			if(Ties.empty())
				return nullptr;
			return Ties[Ties.size() == 1 ? 0 : Random() % Ties.size()];
		}
	}

	std::unique_ptr<IClassicSimulation> CreateClassicSimulation(const CGameWorld &Source, int ClientId, bool Death, bool Teles, bool PlayerPrediction, CGameClient *pClient, bool Prune)
	{
		return std::make_unique<CClassicWorld>(Source, ClientId, Death, Teles, PlayerPrediction, pClient, Prune);
	}

	bool CClassicPlanner::Frozen(const IClassicSimulation &World, const CClassicSettings &Settings)
	{
		if(!World.Valid())
			return true;
		if(!World.RawFrozen())
			return false;
		if(!Settings.m_Unfreeze)
			return true;
		auto Lookahead = World.Clone();
		const auto Input = World.CurrentInput();
		bool SawFrozen = false, Thawed = false;
		for(int Tick = 0; Tick <= Settings.m_UnfreezeTicks; ++Tick)
		{
			// Original328230: no direct input; the first lookahead does not advance
			// the copied game tick. SawFrozen intentionally starts false.
			Lookahead->Tick(Input, false, Tick != 0);
			if(!Lookahead->Valid())
				return true;
			const bool IsFrozen = Lookahead->RawFrozen();
			if(IsFrozen)
			{
				SawFrozen = true;
				Thawed = false;
			}
			if((IsFrozen || (SawFrozen && !Thawed)) && Lookahead->OnUnfreezeTile() && !IsFrozen)
			{
				SawFrozen = false;
				Thawed = true;
			}
		}
		return SawFrozen && !Thawed;
	}

	int CClassicPlanner::ConstantLife(const IClassicSimulation &World, const CNetObj_PlayerInput &Input, int Ticks)
	{
		auto Probe = World.Clone();
		if(!Probe->Valid())
			return -1;
		for(int Tick = 0; Tick <= Ticks; ++Tick)
		{
			Probe->Tick(Input);
			if(Probe->RawFrozen())
				return Tick;
		}
		return SAFE;
	}

	CClassicPlanner::CEvaluation CClassicPlanner::Evaluate(const IClassicSimulation &World, const CNetObj_PlayerInput &Input, const CClassicSettings &Settings)
	{
		auto Probe = World.Clone();
		if(!Probe->Valid())
			return {-1, true};
		Probe->Tick(Input);
		if(Frozen(*Probe, Settings))
			return {0, true};
		for(int Tick = 0; Tick <= Settings.m_CheckTicks; ++Tick)
		{
			Probe->Tick(Input);
			if(Frozen(*Probe, Settings))
				return {Tick + 1, Tick == 0};
		}
		// 329250 holds the last prefix input for Check+1 extra ticks, but its
		// all-safe score is prefix_size+Check rather than the physical tick count.
		return {Settings.m_CheckTicks + 1, false};
	}

	std::vector<CNetObj_PlayerInput> CClassicPlanner::Actions(const CNetObj_PlayerInput &BaseInput, const CClassicSettings &Settings)
	{
		std::vector<CNetObj_PlayerInput> Result;
		const auto Add = [&](int Direction, int Hook) {
			auto Input = BaseInput;
			Input.m_Direction = Direction;
			Input.m_Hook = Hook;
			Input.m_Jump = 0;
			Result.push_back(Input);
		};
		if(Settings.m_Direction)
			for(int Direction : {-1, 0, 1})
				Add(Direction, Settings.m_Hook ? 0 : BaseInput.m_Hook);
		if(Settings.m_Hook)
		{
			if(!Settings.m_Direction && BaseInput.m_Direction != -2)
			{
				Add(BaseInput.m_Direction, 0);
				Add(BaseInput.m_Direction, 1);
			}
			else
				for(int Direction : {-1, 0, 1})
					Add(Direction, 1);
		}
		return Result;
	}

	std::optional<CNetObj_PlayerInput> CClassicPlanner::Basic(const IClassicSimulation &World, CNetObj_PlayerInput Input)
	{
		if(!World.Valid() || ConstantLife(World, Input, 6) == SAFE)
			return std::nullopt;
		int BestLife = std::numeric_limits<int>::min();
		std::optional<CNetObj_PlayerInput> Best;
		for(int Direction : {0, -1, 1})
		{
			auto Candidate = Input;
			Candidate.m_Direction = Direction;
			const int Life = ConstantLife(World, Candidate, 6);
			if(Life > BestLife)
			{
				BestLife = Life;
				Best = Candidate;
			}
		}
		return Best;
	}

	bool CClassicPlanner::UnnecessaryHook(const IClassicSimulation &World, CNetObj_PlayerInput Input, int Horizon, const CClassicSettings &Settings)
	{
		if(!World.Valid() || !Input.m_Hook)
			return false;
		auto Probe = World.Clone();
		std::optional<vec2> HookedFreeze;
		for(int Tick = 0; Tick <= Horizon; ++Tick)
		{
			Probe->Tick(Input);
			if(Frozen(*Probe, Settings))
			{
				HookedFreeze = Probe->Position();
				break;
			}
		}
		if(!HookedFreeze)
			return false;
		Probe = World.Clone();
		Input.m_Hook = 1;
		Probe->Tick(Input); // Initial press is not tested for freeze until the next tick.
		Input.m_Hook = 0;
		for(int Tick = 0; Tick <= Horizon; ++Tick)
		{
			Probe->Tick(Input);
			if(Frozen(*Probe, Settings))
			{
				const vec2 Other = Probe->Position();
				return !(std::abs(Other.y - HookedFreeze->y) > 16.f && distance(Other, *HookedFreeze) > 32.f);
			}
		}
		return false;
	}

	int CClassicPlanner::HookHorizon(vec2 Velocity, float Gravity, float GroundAcceleration, float AirAcceleration, bool Grounded)
	{
		const auto Round = [](float Value) {
			Value += Value <= 0 ? -.5f : .5f;
			// cvttss2si in the reference produces INT_MIN for NaN/infinity.
			if(!std::isfinite(Value) || Value >= (float)std::numeric_limits<int>::max() || Value < (float)std::numeric_limits<int>::min())
				return std::numeric_limits<int>::min();
			return (int)Value;
		};
		int Result = Round(std::abs(Velocity.y) / Gravity);
		const float Acceleration = Grounded ? GroundAcceleration : AirAcceleration;
		if(Result == 0 && Velocity.x != 0)
			Result = Round(std::abs(Velocity.x) / Acceleration);
		if(Result == 0)
			Result = Round(length(Velocity) / Acceleration);
		return Result <= std::numeric_limits<int>::max() - 5 ? Result + 5 : std::numeric_limits<int>::max();
	}

	std::optional<CNetObj_PlayerInput> CClassicPlanner::Legit(const IClassicSimulation &World, CNetObj_PlayerInput Input, int PhysicalDirection,
		int Horizon, const CClassicSettings &Settings, CRandom Random, CClassicDiagnostic *pDiagnostic)
	{
		if(!World.Valid())
			return std::nullopt;
		CNode Root;
		Root.m_Input = Input;
		if(pDiagnostic)
		{
			pDiagnostic->m_Nodes = 1;
			pDiagnostic->m_Evaluations = pDiagnostic->m_MaxDepth = 0;
			pDiagnostic->m_vRootChildren.clear();
			pDiagnostic->m_vEvaluatedInputs.clear();
		}
		const auto EvaluateNode = [&](CNode &Node) {
			float Life = 0;
			if(!Node.m_Dead)
			{
				const auto Evaluation = Evaluate(World, Node.m_Input, Settings);
				Node.m_Life = Evaluation.m_Life;
				Node.m_Dead = Evaluation.m_Dead || Evaluation.m_Life < 1;
				Life = (float)Evaluation.m_Life;
				if(pDiagnostic)
				{
					++pDiagnostic->m_Evaluations;
					pDiagnostic->m_vEvaluatedInputs.push_back(Node.m_Input);
				}
			}
			// A dead node zeroes its own sum, but the original life (including1)
			// is still propagated to its ancestors by338220.
			++Node.m_Visits;
			Node.m_Sum += Life;
			if(Node.m_Dead)
				Node.m_Sum = 0;
			Propagate(Node.m_pParent, Life);
		};
		for(int Iteration = 0; Iteration < Settings.m_Iterations; ++Iteration)
		{
			CNode *pLeaf = &Root;
			int Depth = 0;
			while(!pLeaf->m_vChildren.empty())
			{
				pLeaf = Select(*pLeaf, (float)Settings.m_ExplorationConstant, Input, PhysicalDirection, Settings, Random);
				++Depth;
			}
			if(!pLeaf->m_Dead && pLeaf->m_Visits)
			{
				// Assembly339169 retains the ORIGINAL prepared input for every
				// expansion. Neither parent actions nor root-to-node paths accumulate.
				const auto Candidates = Actions(Input, Settings);
				for(const auto &Candidate : Candidates)
				{
					auto Child = std::make_unique<CNode>();
					Child->m_pParent = pLeaf;
					Child->m_Input = Candidate;
					pLeaf->m_vChildren.push_back(std::move(Child));
				}
				if(pDiagnostic)
					pDiagnostic->m_Nodes += Candidates.size();
				if(!pLeaf->m_vChildren.empty())
				{
					// Even one expansion child consumes rand() in the original.
					EvaluateNode(*pLeaf->m_vChildren[Random() % pLeaf->m_vChildren.size()]);
					++Depth;
				}
			}
			else
				EvaluateNode(*pLeaf);
			if(pDiagnostic)
				pDiagnostic->m_MaxDepth = std::max(pDiagnostic->m_MaxDepth, Depth);
		}
		if(pDiagnostic)
			for(const auto &pChild : Root.m_vChildren)
				pDiagnostic->m_vRootChildren.push_back({pChild->m_Input, pChild->m_Visits, pChild->m_Life, pChild->m_Sum, pChild->m_Dead});
		CNode *pBest = Select(Root, 0.f, Input, PhysicalDirection, Settings, Random);
		if(!pBest)
			return std::nullopt;
		auto Result = pBest->m_Input;
		auto WithoutHook = Result;
		if(UnnecessaryHook(World, Result, Horizon, Settings))
			WithoutHook.m_Hook = 0;
		const int WithoutLife = std::min(ConstantLife(World, WithoutHook, 26), 26);
		const int WithLife = std::min(ConstantLife(World, Result, 26), 26);
		if(WithoutHook.m_Hook != Result.m_Hook && WithoutLife > WithLife)
			Result.m_Hook = WithoutHook.m_Hook;
		Result.m_Jump = Input.m_Jump;
		if(!Settings.m_Direction)
			Result.m_Direction = Input.m_Direction;
		if(!Settings.m_Hook)
			Result.m_Hook = Input.m_Hook;
		return Result;
	}

	CNetObj_PlayerInput CClassicAvoid::AdjustInput(CGameClient *pClient, CNetObj_PlayerInput Input, int ClientId)
	{
		m_Diagnostic = {};
		const int Agent = g_Config.m_AvoidAgentType;
		if(!g_Config.m_AvoidFreeze || (Agent != 0 && Agent != 1))
		{
			m_Diagnostic.m_pGate = "disabled";
			return Input;
		}
		if(ClientId < 0 || ClientId >= MAX_CLIENTS || pClient->Client()->State() != IClient::STATE_ONLINE ||
			pClient->m_Snap.m_SpecInfo.m_Active || !pClient->m_Snap.m_pLocalCharacter || !(Input.m_PlayerFlags & PLAYERFLAG_PLAYING))
		{
			m_Diagnostic.m_pGate = "not_playing";
			return Input;
		}
		if(pClient->m_PreTasVisuals.ShouldSuspendAvoid() || IsKrxCharacterFrozen(pClient, ClientId))
		{
			m_Diagnostic.m_pGate = "afk_or_authoritative_frozen";
			return Input;
		}
		const vec2 Pos = pClient->m_LocalCharacterPos;
		if(pClient->m_aClients[ClientId].m_Super && pClient->Collision()->GetTileIndex(pClient->Collision()->GetPureMapIndex(vec2((int)Pos.x, (int)Pos.y))) == TILE_FREEZE)
		{
			m_Diagnostic.m_pGate = "super_freeze_tile";
			return Input;
		}
		const auto &World = pClient->KrxPredictedWorld();
		auto Simulation = CreateClassicSimulation(World, ClientId, Agent == 1 && g_Config.m_AvoidLegitDeath,
			Agent == 1 && g_Config.m_AvoidLegitTeles, g_Config.m_AvoidPlayerPrediction != 0, pClient, Agent == 1);
		if(!Simulation->Valid())
		{
			m_Diagnostic.m_pGate = "invalid_simulation";
			return Input;
		}
		m_Diagnostic.m_pGate = "evaluated";
		if(Agent == 0)
			return CClassicPlanner::Basic(*Simulation, Input).value_or(Input);
		CClassicSettings Settings;
		Settings.m_CheckTicks = g_Config.m_AvoidLegitCheckTicks;
		Settings.m_Direction = g_Config.m_AvoidLegitDirection != 0;
		Settings.m_Hook = g_Config.m_AvoidLegitHook != 0;
		Settings.m_Unfreeze = g_Config.m_AvoidLegitUnfreeze != 0;
		Settings.m_UnfreezeTicks = g_Config.m_AvoidLegitUnfreezeTicks;
		Settings.m_Iterations = g_Config.m_ClAvoidNumIterations;
		Settings.m_DirectionWeight = g_Config.m_AvoidDirectionWeight;
		Settings.m_LifespanWeight = g_Config.m_AvoidLifespanWeight;
		Settings.m_HookWeight = g_Config.m_AvoidHookWeight;
		Settings.m_ExplorationConstant = g_Config.m_AvoidExplorationConstant;
		const auto &Controls = pClient->m_Controls;
		const int Slot = g_Config.m_ClDummy;
		const int PhysicalDirection = (Controls.m_aInputDirectionRight[Slot] != 0) - (Controls.m_aInputDirectionLeft[Slot] != 0);
		const auto *pTune = &pClient->m_aTuning[0];
		const vec2 RenderPos = pClient->m_aClients[ClientId].m_RenderPos;
		const bool Grounded = pClient->Collision()->CheckPoint(round_to_int(RenderPos.x + 14), round_to_int(RenderPos.y + 19)) ||
				      pClient->Collision()->CheckPoint(round_to_int(RenderPos.x - 14), round_to_int(RenderPos.y + 19)) ||
				      (pClient->Collision()->GetMoveRestrictions(RenderPos + vec2(0, 18), 0.f) & CANTMOVE_DOWN);
		const int Horizon = CClassicPlanner::HookHorizon(pClient->m_PredictedChar.m_Vel, pTune->m_Gravity, pTune->m_GroundControlAccel, pTune->m_AirControlAccel, Grounded);
		return CClassicPlanner::Legit(*Simulation, Input, PhysicalDirection, Horizon, Settings, [] { return CKrxMiscPreTas::ReferenceRand(); }, &m_Diagnostic).value_or(Input);
	}
}
