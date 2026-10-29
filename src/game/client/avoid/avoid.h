#ifndef GAME_CLIENT_AVOID_AVOID_H
#define GAME_CLIENT_AVOID_AVOID_H

#include "blatant.h"
#include "trace.h"

#include <generated/protocol.h>

class CGameClient;

namespace OpenKrx
{
	// Glue between DDNet's input/render loop and the recovered KRX planner.
	class CAvoid
	{
	public:
		struct CDiagnostic
		{
			const char *m_pGate = "not_evaluated";
			std::optional<CNetObj_PlayerInput> m_Proposed;
			bool m_ManualHook = false;
			bool m_OwnedHookBefore = false;
			bool m_OwnedHookAfter = false;
			bool m_KeptDirection = false;
			bool m_FilteredHook = false;
			bool m_PendingAutomaticFire = false;
			bool m_AllowImmediateShot = false;
			int m_Life = -1;
			int m_ElapsedUs = 0;
		};
		void OnReset();
		CAvoidTrace m_Trace;
		const CDiagnostic &Diagnostic() const { return m_Diagnostic; }
		// Called inside CControls::SnapInput after preparing player input and before
		// send detection/last-input bookkeeping, matching KRX's overridden SnapInput.
		CNetObj_PlayerInput AdjustInput(CGameClient *pGameClient, CNetObj_PlayerInput Input, int ClientId, vec2 MouseAim, bool ManualHook);
		bool Active() const;

		CBlatantPlanner &Planner() { return m_Planner; }
		const CBlatantPlanner &Planner() const { return m_Planner; }

	private:
		CBlatantPlanner m_Planner;
		CDiagnostic m_Diagnostic;
		bool m_WasActive = false;
		int m_LastClientId = -1;
		int m_LastSlot = -1;
		int m_DebugCounter = 0;
		bool m_OwnsHook = false;
	};

	CBlatantSettings SettingsFromConfig();
}

#endif
