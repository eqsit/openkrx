#include "blatant.h"

#include <algorithm>
#include <cmath>

namespace OpenKrx
{
	namespace
	{
		vec2 InputAim(const CNetObj_PlayerInput &Input)
		{
			return vec2(static_cast<float>(Input.m_TargetX), static_cast<float>(Input.m_TargetY));
		}

		void AimAt(CNetObj_PlayerInput &Input, vec2 Point, vec2 Position)
		{
			const vec2 Aim = Point - Position;
			Input.m_TargetX = static_cast<int>(Aim.x);
			Input.m_TargetY = static_cast<int>(Aim.y);
		}
	}

	CNetObj_PlayerInput CBlatantPlanner::Action(const CNetObj_PlayerInput &Input, int Direction, int Hook)
	{
		CNetObj_PlayerInput Result = Input;
		Result.m_Direction = Direction;
		Result.m_Hook = Hook;
		Result.m_Jump = 0;
		return Result;
	}

	std::vector<CBlatantPlan> IBlatantSimulation::SearchCandidates(const std::vector<CNetObj_PlayerInput> &vInputs, int CheckTicks) const
	{
		std::vector<CBlatantPlan> vResults;
		vResults.reserve(vInputs.size());
		for(const auto &Input : vInputs)
			vResults.push_back(CBlatantPlanner::Greedy(*this, Input, CheckTicks));
		return vResults;
	}

	CBlatantPlanner::CActionGroups CBlatantPlanner::ActionGroups(int Direction, bool Hook)
	{
		// 0x1402d2a90: sentinel-separated groups, not a Cartesian product sorted later.
		if(!Hook)
		{
			if(Direction == -1)
				return {{{{0, 0}, {1, 0}}, {{-1, 1}}, {{0, 1}, {1, 1}}}};
			if(Direction == 0)
				return {{{{-1, 0}, {1, 0}}, {{0, 1}}, {{-1, 1}, {1, 1}}}};
			if(Direction == 1)
				return {{{{0, 0}, {-1, 0}}, {{1, 1}}, {{0, 1}, {-1, 1}}}};
		}
		else
		{
			if(Direction == -1)
				return {{{{-1, 0}}, {{0, 1}, {0, 0}}, {{1, 1}, {1, 0}}}};
			if(Direction == 0)
				return {{{{0, 0}}, {{-1, 1}, {1, 1}}, {{-1, 0}, {1, 0}}}};
			if(Direction == 1)
				return {{{{1, 0}}, {{0, 1}, {0, 0}}, {{-1, 1}, {-1, 0}}}};
		}
		return {};
	}

	int CBlatantPlanner::ConstantLife(const IBlatantSimulation &World, const CNetObj_PlayerInput &Input, int CheckTicks)
	{
		auto pCopy = World.Clone();
		if(!pCopy->Valid())
			return -5;
		// 0x1402cd640 intentionally checks CheckTicks + 1 simulated ticks.
		for(int Tick = 0; Tick <= CheckTicks; ++Tick)
		{
			pCopy->Tick(Input);
			if(pCopy->Frozen())
				return Tick;
		}
		return CBlatantPlan::SAFE;
	}

	CNetObj_PlayerInput CBlatantPlanner::FilterEmergencyHook(const IBlatantSimulation &World, CNetObj_PlayerInput Proposed, int CheckTicks, bool ManualHook)
	{
		if(ManualHook || Proposed.m_Hook == 0)
			return Proposed;
		auto WithoutHook = Proposed;
		WithoutHook.m_Hook = 0;
		if(ConstantLife(World, WithoutHook, CheckTicks) == CBlatantPlan::SAFE)
			return WithoutHook;
		return Proposed;
	}

	CNetObj_PlayerInput CBlatantPlanner::KeepDirectionIfSafe(const IBlatantSimulation &World, CNetObj_PlayerInput Proposed, int RequestedDirection, int CheckTicks)
	{
		if(Proposed.m_Direction == RequestedDirection || RequestedDirection < -1 || RequestedDirection > 1)
			return Proposed;
		auto Preferred = Proposed;
		Preferred.m_Direction = RequestedDirection;
		if(ConstantLife(World, Preferred, CheckTicks) == CBlatantPlan::SAFE)
			return Preferred;
		return Proposed;
	}

	int CBlatantPlanner::SequenceLife(const IBlatantSimulation &World, const std::vector<CNetObj_PlayerInput> &vInputs)
	{
		auto pCopy = World.Clone();
		if(!pCopy->Valid())
			return -5;
		for(size_t Tick = 0; Tick < vInputs.size(); ++Tick)
		{
			pCopy->Tick(vInputs[Tick]);
			if(pCopy->Frozen())
				return static_cast<int>(Tick);
		}
		return static_cast<int>(vInputs.size());
	}

	CBlatantPlan CBlatantPlanner::Greedy(const IBlatantSimulation &World, CNetObj_PlayerInput Input,
		int CheckTicks, std::optional<vec2> Anchor, bool TrackFromFirstTick)
	{
		auto pCopy = World.Clone();
		CBlatantPlan Result;
		if(!pCopy->Valid())
			return Result;
		// The order is part of KRX's tie-breaking (0x1402d3d20).
		static constexpr CAction ACTIONS[] = {{0, 0}, {0, 1}, {1, 0}, {-1, 0}, {1, 1}, {-1, 1}};
		for(int Tick = 0, Remaining = CheckTicks; Tick <= CheckTicks; ++Tick, --Remaining)
		{
			if(Anchor && (TrackFromFirstTick || Tick > 0))
				AimAt(Input, *Anchor, pCopy->Position());
			if(Tick > 0)
			{
				int BestLife = -1;
				CNetObj_PlayerInput BestInput{};
				for(const auto &Next : ACTIONS)
				{
					const auto Candidate = Action(Input, Next[0], Next[1]);
					const int Life = ConstantLife(*pCopy, Candidate, Remaining);
					if(Life > BestLife)
					{
						BestInput = Candidate;
						BestLife = Life;
					}
					if(Life > Remaining)
						break;
				}
				Input = BestInput;
				if(BestLife > Remaining)
				{
					// The safety probe includes one extra step; the stored suffix does not.
					Result.m_vInputs.insert(Result.m_vInputs.end(), Remaining, Input);
					Result.m_Life = CBlatantPlan::SAFE;
					return Result;
				}
			}
			pCopy->Tick(Input);
			if(pCopy->Frozen())
			{
				Result.m_Life = Tick;
				return Result;
			}
			Result.m_vInputs.push_back(Input);
		}
		Result.m_Life = CBlatantPlan::SAFE;
		return Result;
	}

	std::vector<CNetObj_PlayerInput> CBlatantPlanner::AimCandidates(const IBlatantSimulation &World,
		const CNetObj_PlayerInput &Input, vec2 MouseAim) const
	{
		std::vector<CNetObj_PlayerInput> vResult;
		if(!m_Settings.m_Aimbot || !World.Valid() || Input.m_Hook == 1)
			return vResult;
		// 0x1402d115c and 0x1402d11a4: both stored outputs and every survival
		// probe use Hook=1, including aim assist. Scoring an unhooked input here
		// makes the scan miss precisely the anchors that could rescue the tee.
		const bool MayElide = World.CanElideIndependentProbes();
		if(MayElide && !m_Settings.m_AimAssist && !m_Settings.m_AutoAim)
			return vResult;
		CNetObj_PlayerInput BestAssist = Input;
		CNetObj_PlayerInput BestAuto = Input;
		BestAssist.m_Hook = 1;
		BestAuto.m_Hook = 1;
		int AssistLife = 0;
		int AutoLife = 0;
		const auto OutputsFixed = [&] {
			return (!m_Settings.m_AimAssist || AssistLife == CBlatantPlan::SAFE) &&
			       (!m_Settings.m_AutoAim || AutoLife == CBlatantPlan::SAFE);
		};
		float AssistDot = -20.0f;
		const vec2 Mouse = normalize(InputAim(Input));
		float Fov = static_cast<float>(m_Settings.m_Fov) * pi / 180.0f;
		if(m_Settings.m_Fov == 360)
			Fov = 2.0f * pi;
		const float Step = Fov / static_cast<float>(std::max(1, m_Settings.m_Segments));
		const float Step50 = Step / 50.0f;
		// The scan is centred on the physical mouse, while the assist similarity
		// score uses the input aim (which may already track an older anchor).
		const float Center = angle(MouseAim);
		const float End = Center + Fov * 0.5f;
		const float Radius = World.HookLength() - 28.0f;
		// SAFE is the maximum ConstantLife result. Both selectors require a
		// strictly greater lifespan, so once every enabled output is SAFE no
		// later candidate can replace it. Preserve scan order until that point.
		for(float Angle = Center - Fov * 0.5f; Angle < End && !(MayElide && OutputsFixed()); Angle += Step)
		{
			std::optional<vec2> Aim;
			vec2 Hit;
			for(int Probe = 0; Probe < 50; ++Probe)
			{
				const vec2 Candidate = normalize(direction(Angle + static_cast<float>(Probe) * Step50)) * Radius;
				const auto Point = World.HookPoint(Candidate);
				if(Point)
				{
					Hit = Candidate;
					Aim = Point;
					break;
				}
			}
			if(!Aim)
				continue;
			for(const int Direction : {-1, 0, 1})
			{
				if(MayElide && OutputsFixed())
					break;
				CNetObj_PlayerInput Candidate = Input;
				Candidate.m_Direction = Direction;
				Candidate.m_TargetX = static_cast<int>(Hit.x);
				Candidate.m_TargetY = static_cast<int>(Hit.y);
				Candidate.m_Hook = 1;
				// 0x1402d1060 uses a literal 26 here, independently of Check Ticks.
				const int Life = ConstantLife(World, Candidate, 26);
				const float Dot = dot(normalize(Hit), Mouse);
				if(Dot > AssistDot && Life > AssistLife)
				{
					AssistDot = Dot;
					AssistLife = Life;
					BestAssist = Candidate;
				}
				if(Life > AutoLife)
				{
					AutoLife = Life;
					BestAuto = Candidate;
				}
			}
		}
		if(m_Settings.m_AimAssist)
			vResult.push_back(BestAssist);
		if(m_Settings.m_AutoAim)
			vResult.push_back(BestAuto);
		return vResult;
	}

	std::vector<std::vector<CNetObj_PlayerInput>> CBlatantPlanner::CandidateGroups(const CNetObj_PlayerInput &Input) const
	{
		std::vector<std::vector<CNetObj_PlayerInput>> vGroups;
		for(const auto &Group : ActionGroups(Input.m_Direction, Input.m_Hook != 0))
		{
			std::vector<CNetObj_PlayerInput> vGroup;
			for(const auto &Next : Group)
			{
				if(!m_Settings.m_Direction && Next[0] != 0)
					continue;
				if(!m_Settings.m_Hook && Next[1] != 0)
					continue;
				vGroup.push_back(Action(Input, Next[0], Next[1]));
			}
			if(!vGroup.empty())
				vGroups.push_back(std::move(vGroup));
		}
		return vGroups;
	}

	std::vector<CNetObj_PlayerInput> CBlatantPlanner::Candidates(const IBlatantSimulation &World,
		const CNetObj_PlayerInput &Input, vec2 MouseAim) const
	{
		// 0x1402d2490 really performs two passes over the same sentinel groups:
		// pass 0 accumulates scored actions without clearing the buffer, pass 1
		// clears it after each sentinel. Both stop at the first lifespan 9999 and
		// the merged list is de-duplicated by (Direction, Hook). Reproducing this
		// bug-for-bug doubles the work; that is what the reference does.
		struct CScored
		{
			CNetObj_PlayerInput m_Input;
			int m_Life;
		};
		std::vector<CNetObj_PlayerInput> vResult;
		std::vector<CScored> vScored;
		const auto vGroups = CandidateGroups(Input);
		for(int Pass = 0; Pass < 2; ++Pass)
		{
			for(const auto &Group : vGroups)
			{
				for(const auto &Candidate : Group)
					vScored.push_back({Candidate, ConstantLife(World, Candidate, m_Settings.m_CheckTicks)});
				CScored Best{};
				Best.m_Life = -1;
				if(!vScored.empty())
				{
					// 0x1402b9390 sorts by the lifespan field, descending.
					std::sort(vScored.begin(), vScored.end(), [](const CScored &A, const CScored &B) {
						return A.m_Life > B.m_Life;
					});
					Best = vScored.front();
				}
				if(Best.m_Life >= 0)
				{
					const bool Duplicate = std::any_of(vResult.begin(), vResult.end(), [&](const auto &Existing) {
						return Existing.m_Direction == Best.m_Input.m_Direction && Existing.m_Hook == Best.m_Input.m_Hook;
					});
					if(!Duplicate)
						vResult.push_back(Best.m_Input);
				}
				if(Pass == 1)
					vScored.clear();
				if(Best.m_Life == CBlatantPlan::SAFE)
					break;
			}
		}
		const auto vAim = AimCandidates(World, Input, MouseAim);
		vResult.insert(vResult.end(), vAim.begin(), vAim.end());
		return vResult;
	}

	void CBlatantPlanner::UpdateTracking(IBlatantSimulation &World, CNetObj_PlayerInput &Input, vec2 MouseAim)
	{
		if(!m_Settings.m_TrackPoint || (m_TrackPoint && distance(World.Position(), *m_TrackPoint) >= World.HookLength()))
			m_TrackPoint.reset();
		if(!m_Settings.m_TrackPoint)
			return;
		if(m_TrackPoint)
			AimAt(Input, *m_TrackPoint, World.Position());
		const auto Point = World.HookPoint(MouseAim);
		if(!Point)
			return;
		auto Candidate = Input;
		AimAt(Candidate, *Point, World.Position());
		if(!m_Settings.m_SafeTracking || m_Settings.m_Unfreeze ||
			Greedy(World, Candidate, m_Settings.m_CheckTicks, Point).m_Life == CBlatantPlan::SAFE)
		{
			m_TrackPoint = Point;
			Input = Candidate;
		}
	}

	CNetObj_PlayerInput CBlatantPlanner::AutoDrag(const IBlatantSimulation &World,
		CNetObj_PlayerInput Input, vec2 MouseAim)
	{
		const auto Aim = World.DragAim();
		if(!Aim)
			return Input;
		auto Candidate = Input;
		Candidate.m_TargetX = static_cast<int>(Aim->x);
		Candidate.m_TargetY = static_cast<int>(Aim->y);
		Candidate.m_Hook = 1;
		const vec2 Anchor = m_TrackPoint ? *m_TrackPoint - World.Position() : MouseAim;
		if(Greedy(World, Candidate, m_Settings.m_CheckTicks, Anchor, false).m_Life != CBlatantPlan::SAFE)
			return Input;
		m_Dragged = true;
		return Candidate;
	}

	std::optional<CNetObj_PlayerInput> CBlatantPlanner::Decide(IBlatantSimulation &World,
		CNetObj_PlayerInput Input, vec2 MouseAim)
	{
		m_LastDecisionLife = -1;
		m_Diagnostic = {};
		if(!World.Valid())
		{
			m_Diagnostic.m_pReason = "invalid_world";
			return std::nullopt;
		}
		if(!m_Settings.m_AutoDrag && m_Dragged)
		{
			Input.m_Hook = 0;
			m_Dragged = false;
		}

		// 0x1402d2080 order: tracking, then the guard (Kick In Ticks), then the
		// unfreeze branch, then the search and finally NSIF. There is no plan
		// replay: the stored plan is only consumed by NSIF.
		UpdateTracking(World, Input, MouseAim);
		if(m_Settings.m_TrackPoint && m_TrackPoint)
			AimAt(Input, *m_TrackPoint, World.Position());

		// Guard, cheap half: KRX evaluates the full greedy first and then this
		// constant probe, but with Auto Drag disabled the greedy plan is discarded
		// on success, so checking the probe first has the same result for a
		// fraction of the cost.
		m_Diagnostic.m_ConstantLife = ConstantLife(World, Input, m_Settings.m_KickInTicks);
		if(m_Diagnostic.m_ConstantLife == CBlatantPlan::SAFE)
		{
			m_Diagnostic.m_pReason = "constant_safe";
			m_LastDecisionLife = CBlatantPlan::SAFE;
			return m_Settings.m_AutoDrag ? AutoDrag(World, Input, MouseAim) : Input;
		}

		// Guard, expensive half: a safe greedy plan is stored for NSIF and the
		// current input is returned unchanged.
		const auto Guard = Greedy(World, Input, m_Settings.m_CheckTicks, m_TrackPoint);
		m_Diagnostic.m_GreedyLife = Guard.m_Life;
		if(Guard.m_Life == CBlatantPlan::SAFE)
		{
			m_LastDecisionLife = CBlatantPlan::SAFE;
			m_Diagnostic.m_pReason = "greedy_safe";
			m_vLastPlan = Guard.m_vInputs;
			return m_Settings.m_AutoDrag ? AutoDrag(World, Input, MouseAim) : Input;
		}

		if(m_Settings.m_Unfreeze)
		{
			auto pUnfreeze = World.Clone();
			bool WasFrozen = pUnfreeze->Frozen();
			for(int Tick = 0; Tick <= m_Settings.m_UnfreezeTicks; ++Tick)
			{
				pUnfreeze->Tick(Input);
				if(!pUnfreeze->Frozen() && WasFrozen && pUnfreeze->OnUnfreezeTile())
				{
					if(Tick > 0)
					{
						const auto Plan = Greedy(*pUnfreeze, Input, m_Settings.m_CheckTicks, m_TrackPoint);
						if(Plan.m_Life == CBlatantPlan::SAFE && !Plan.m_vInputs.empty())
						{
							m_LastDecisionLife = CBlatantPlan::SAFE;
							m_Diagnostic.m_pReason = "unfreeze_safe";
							m_vLastPlan = Plan.m_vInputs;
							return Input;
						}
					}
					break;
				}
				WasFrozen |= pUnfreeze->Frozen();
			}
		}

		CBlatantPlan Best;
		const auto vCandidates = Candidates(World, Input, MouseAim);
		auto vResults = World.SearchCandidates(vCandidates, m_Settings.m_CheckTicks);
		if(m_Tracing)
		{
			m_Diagnostic.m_vCandidates.reserve(vResults.size());
			for(size_t i = 0; i < vResults.size(); ++i)
				m_Diagnostic.m_vCandidates.push_back({vCandidates[i], vResults[i].m_Life, (int)vResults[i].m_vInputs.size()});
		}
		int Index = -1;
		int BestIndex = -1;
		for(auto &Plan : vResults)
		{
			++Index;
			if(Plan.m_Life == CBlatantPlan::SAFE)
			{
				m_Diagnostic.m_pReason = "search_safe";
				m_Diagnostic.m_Selected = Index;
				m_vLastPlan = Plan.m_vInputs;
				if(m_vLastPlan.empty())
				{
					m_Diagnostic.m_pReason = "empty_safe_plan";
					return std::nullopt;
				}
				m_LastDecisionLife = CBlatantPlan::SAFE;
				return m_vLastPlan.front();
			}
			if(Plan.m_Life > Best.m_Life)
			{
				BestIndex = Index;
				Best = std::move(Plan);
			}
		}

		// No safe result. NSIF re-checks the whole stored tail except its first
		// element and consumes stored plan[1] on success.
		// 0x1402d2080 also requires a non-empty current search result. A stale
		// saved tail alone must not turn a completely failed search into a move.
		if(Best.m_vInputs.empty() || m_vLastPlan.size() < 2 || !m_Settings.m_Nsif)
		{
			m_Diagnostic.m_pReason = Best.m_vInputs.empty() ? "no_search_plan" : !m_Settings.m_Nsif ? "no_safe_nsif_disabled" :
														  "no_safe_no_tail";
			return std::nullopt;
		}
		const std::vector<CNetObj_PlayerInput> vTail(m_vLastPlan.begin() + 1, m_vLastPlan.end());
		m_Diagnostic.m_TailLength = (int)vTail.size();
		m_Diagnostic.m_TailLife = SequenceLife(World, vTail);
		if(m_Diagnostic.m_TailLife == static_cast<int>(vTail.size()))
		{
			m_Diagnostic.m_pReason = "nsif_tail_safe";
			// Safe over the checked tail; it can be shorter than CheckTicks.
			m_LastDecisionLife = CBlatantPlan::SAFE;
			const CNetObj_PlayerInput Output = m_vLastPlan[1];
			// KRX keeps plan[0] as an anchor and erases only the consumed element.
			m_vLastPlan.erase(m_vLastPlan.begin() + 1);
			return Output;
		}
		// NSIF failed: hand out the first input of the best partial plan.
		if(Best.m_Life > 0 && !Best.m_vInputs.empty())
		{
			m_Diagnostic.m_pReason = "best_partial";
			m_Diagnostic.m_Selected = BestIndex;
			m_LastDecisionLife = Best.m_Life;
			return Best.m_vInputs.front();
		}
		m_Diagnostic.m_pReason = "no_safe_tail_failed";
		return std::nullopt;
	}

	void CBlatantPlanner::Reset()
	{
		m_vLastPlan.clear();
		m_TrackPoint.reset();
		m_Dragged = false;
		m_Diagnostic = {};
		m_LastDecisionLife = -1;
	}
}
