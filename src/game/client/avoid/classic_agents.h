#ifndef GAME_CLIENT_AVOID_CLASSIC_AGENTS_H
#define GAME_CLIENT_AVOID_CLASSIC_AGENTS_H

#include <base/vmath.h>

#include <generated/protocol.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class CGameClient;
class CGameWorld;
namespace OpenKrx
{
	// Explicit raw-physics interface: Legit thaw lookahead deliberately differs
	// from an ordinary direct+predicted input tick. Pure fixtures exercise both.
	class IClassicSimulation
	{
	public:
		virtual ~IClassicSimulation() = default;
		virtual std::unique_ptr<IClassicSimulation> Clone() const = 0;
		virtual bool Valid() const = 0;
		virtual void Tick(const CNetObj_PlayerInput &Input, bool Direct = true, bool Advance = true) = 0;
		virtual bool RawFrozen() const = 0;
		virtual bool OnUnfreezeTile() const = 0;
		virtual CNetObj_PlayerInput CurrentInput() const = 0;
		virtual vec2 Position() const = 0;
	};

	struct CClassicSettings
	{
		int m_CheckTicks = 6;
		bool m_Direction = true, m_Hook = true;
		bool m_Unfreeze = false;
		int m_UnfreezeTicks = 5;
		int m_Iterations = 100;
		int m_DirectionWeight = 170, m_LifespanWeight = 160, m_HookWeight = 260;
		int m_ExplorationConstant = 4;
	};

	struct CClassicDiagnostic
	{
		const char *m_pGate = "not_evaluated";
		int m_Nodes = 0, m_Evaluations = 0, m_MaxDepth = 0;
		struct CNode
		{
			CNetObj_PlayerInput m_Input{};
			int m_Visits = 0, m_Life = 0;
			float m_Sum = 0;
			bool m_Dead = false;
		};
		std::vector<CNode> m_vRootChildren;
		std::vector<CNetObj_PlayerInput> m_vEvaluatedInputs;
	};

	class CClassicPlanner
	{
	public:
		static constexpr int SAFE = 9999;
		struct CEvaluation
		{
			int m_Life = -1;
			bool m_Dead = false;
		};
		using CRandom = std::function<unsigned()>;
		static bool Frozen(const IClassicSimulation &World, const CClassicSettings &Settings);
		static int ConstantLife(const IClassicSimulation &World, const CNetObj_PlayerInput &Input, int Ticks);
		static CEvaluation Evaluate(const IClassicSimulation &World, const CNetObj_PlayerInput &Input, const CClassicSettings &Settings);
		static std::vector<CNetObj_PlayerInput> Actions(const CNetObj_PlayerInput &BaseInput, const CClassicSettings &Settings);
		static std::optional<CNetObj_PlayerInput> Basic(const IClassicSimulation &World, CNetObj_PlayerInput Input);
		static std::optional<CNetObj_PlayerInput> Legit(const IClassicSimulation &World, CNetObj_PlayerInput Input, int PhysicalDirection,
			int HookHorizon, const CClassicSettings &Settings, CRandom Random, CClassicDiagnostic *pDiagnostic = nullptr);
		static bool UnnecessaryHook(const IClassicSimulation &World, CNetObj_PlayerInput Input, int Horizon, const CClassicSettings &Settings);
		static int HookHorizon(vec2 Velocity, float Gravity, float GroundAcceleration, float AirAcceleration, bool Grounded);
	};

	class CClassicAvoid
	{
		CClassicDiagnostic m_Diagnostic;

	public:
		void OnReset() { m_Diagnostic = {}; }
		const CClassicDiagnostic &Diagnostic() const { return m_Diagnostic; }
		CNetObj_PlayerInput AdjustInput(CGameClient *pClient, CNetObj_PlayerInput Input, int ClientId);
	};

	// A separate factory lets native fixtures exercise actual prediction physics
	// without input/network access. Prune=false is the Basic full-world path.
	std::unique_ptr<IClassicSimulation> CreateClassicSimulation(const CGameWorld &Source, int ClientId,
		bool Death, bool Teles, bool PlayerPrediction = true, CGameClient *pClient = nullptr, bool Prune = true);
}
#endif
