#ifndef GAME_CLIENT_AVOID_BLATANT_H
#define GAME_CLIENT_AVOID_BLATANT_H

#include <base/vmath.h>

#include <generated/protocol.h>

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace OpenKrx
{
	// Coordinates and control flow were recovered from the supplied KRX image.
	// See research/rea/028-blatant-greedy-search.json, research/rea/avoid-range and
	// research/deepseek/REVERSE-NOTES.md.
	struct CBlatantSettings
	{
		int m_CheckTicks = 26;
		int m_KickInTicks = 26;
		bool m_Nsif = true;
		bool m_PlayerPrediction = true;
		bool m_Direction = true;
		bool m_Hook = true;
		bool m_Death = false;
		bool m_Teles = false;
		bool m_Unfreeze = false;
		int m_UnfreezeTicks = 26;
		bool m_TrackPoint = false;
		bool m_SafeTracking = false;
		bool m_Aimbot = false;
		int m_Fov = 90;
		int m_Segments = 5;
		bool m_AimAssist = true;
		bool m_AutoAim = false;
		bool m_AutoDrag = false;
	};

	struct CBlatantPlan;

	class IBlatantSimulation
	{
	public:
		virtual ~IBlatantSimulation() = default;
		virtual std::unique_ptr<IBlatantSimulation> Clone() const = 0;
		// Results stay in candidate order, independently of worker completion order.
		// Standalone simulations use the sequential default.
		virtual std::vector<CBlatantPlan> SearchCandidates(const std::vector<CNetObj_PlayerInput> &vInputs, int CheckTicks) const;
		// Opt in only when omitted HookPoint/Clone/Tick probes cannot mutate
		// external state or consume a shared random stream. Unknown adapters keep
		// the complete reference scan, including discarded probes.
		virtual bool CanElideIndependentProbes() const { return false; }
		virtual bool Valid() const = 0;
		virtual void Tick(const CNetObj_PlayerInput &Input) = 0;
		// KRX freezes the search when the character is frozen, dead or otherwise
		// unable to keep the tee alive.
		virtual bool Frozen() const = 0;
		virtual bool Dead() const { return false; }
		virtual bool OnUnfreezeTile() const = 0;
		virtual vec2 Position() const = 0;
		virtual float HookLength() const = 0;
		// Aim is a mouse offset relative to the character. The result is the world
		// space hook attach point, or nullopt when the hook would not attach.
		virtual std::optional<vec2> HookPoint(vec2 Aim) const = 0;
		virtual std::optional<vec2> DragAim() const { return std::nullopt; }
	};

	struct CBlatantPlan
	{
		static constexpr int SAFE = 9999;
		std::vector<CNetObj_PlayerInput> m_vInputs;
		int m_Life = -1;
	};

	// Diagnostic observations only: never run extra probes or change candidate order.
	struct CBlatantDiagnostic
	{
		const char *m_pReason = "not_evaluated";
		int m_ConstantLife = -1;
		int m_GreedyLife = -1;
		int m_TailLife = -1;
		int m_TailLength = 0;
		int m_Selected = -1;
		struct CCandidate
		{
			CNetObj_PlayerInput m_Input;
			int m_Life;
			int m_Length;
		};
		std::vector<CCandidate> m_vCandidates;
	};

	class CBlatantPlanner
	{
		CBlatantSettings m_Settings;
		std::vector<CNetObj_PlayerInput> m_vLastPlan;
		std::optional<vec2> m_TrackPoint;
		bool m_Dragged = false;
		int m_LastDecisionLife = -1;
		bool m_Tracing = false;
		CBlatantDiagnostic m_Diagnostic;

		using CAction = std::array<int, 2>; // Direction, Hook
		using CActionGroups = std::array<std::vector<CAction>, 3>;
		static CNetObj_PlayerInput Action(const CNetObj_PlayerInput &Input, int Direction, int Hook);
		static int SequenceLife(const IBlatantSimulation &World, const std::vector<CNetObj_PlayerInput> &vInputs);
		void UpdateTracking(IBlatantSimulation &World, CNetObj_PlayerInput &Input, vec2 MouseAim);
		std::vector<CNetObj_PlayerInput> AimCandidates(const IBlatantSimulation &World, const CNetObj_PlayerInput &Input, vec2 MouseAim) const;
		std::vector<std::vector<CNetObj_PlayerInput>> CandidateGroups(const CNetObj_PlayerInput &Input) const;
		std::vector<CNetObj_PlayerInput> Candidates(const IBlatantSimulation &World, const CNetObj_PlayerInput &Input, vec2 MouseAim) const;
		CNetObj_PlayerInput AutoDrag(const IBlatantSimulation &World, CNetObj_PlayerInput Input, vec2 MouseAim);

	public:
		explicit CBlatantPlanner(CBlatantSettings Settings = {}) : m_Settings(Settings) {}
		void SetSettings(const CBlatantSettings &Settings) { m_Settings = Settings; }
		const CBlatantSettings &Settings() const { return m_Settings; }
		void Reset();
		void SetTracing(bool Enabled) { m_Tracing = Enabled; }
		const CBlatantDiagnostic &Diagnostic() const { return m_Diagnostic; }
		std::optional<CNetObj_PlayerInput> Decide(IBlatantSimulation &World, CNetObj_PlayerInput Input, vec2 MouseAim);
		std::optional<vec2> TrackPoint() const { return m_TrackPoint; }
		const std::vector<CNetObj_PlayerInput> &LastPlan() const { return m_vLastPlan; }
		int LastDecisionLife() const { return m_LastDecisionLife; }

		// Kept accessible for boundary tests and comparisons against recorded KRX decisions.
		static CActionGroups ActionGroups(int Direction, bool Hook);
		static int ConstantLife(const IBlatantSimulation &World, const CNetObj_PlayerInput &Input, int CheckTicks);
		// Optional user-requested policy; the recovered KRX planner remains intact.
		static CNetObj_PlayerInput FilterEmergencyHook(const IBlatantSimulation &World, CNetObj_PlayerInput Proposed, int CheckTicks, bool ManualHook);
		static CNetObj_PlayerInput KeepDirectionIfSafe(const IBlatantSimulation &World, CNetObj_PlayerInput Proposed, int RequestedDirection, int CheckTicks);
		static CBlatantPlan Greedy(const IBlatantSimulation &World, CNetObj_PlayerInput Input, int CheckTicks,
			std::optional<vec2> Anchor = std::nullopt, bool TrackFromFirstTick = true);
	};
}

#endif
