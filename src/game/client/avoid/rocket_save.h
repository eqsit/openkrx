#ifndef GAME_CLIENT_AVOID_ROCKET_SAVE_H
#define GAME_CLIENT_AVOID_ROCKET_SAVE_H

#include <generated/protocol.h>

class CCharacter;
class CGameClient;
class CGameWorld;

namespace OpenKrx
{
	// Recovered b4450 gate shared by Avoid and Rocket Save. This reads the
	// authoritative world and the server's snapshot freeze convention.
	bool IsKrxCharacterFrozen(CGameClient *pGameClient, int ClientId);
	// A delayed automatic press is not an immediately available rescue shot.
	bool RocketShotReady(const CCharacter *pPredicted, const CCharacter *pAuthoritative);

	// Two distinct worlds are intentional: KRX's danger probe removes existing
	// projectiles/other tees and advances once; rescue shots use the full snapshot.
	class IRocketSaveProbe
	{
	public:
		virtual ~IRocketSaveProbe() = default;
		virtual int DangerLife(const CNetObj_PlayerInput &CurrentInput, const CNetObj_PlayerInput &ShotInput) const = 0;
		virtual int ShotLife(const CNetObj_PlayerInput &ShotInput) const = 0;
		virtual int NoShotLife(const CNetObj_PlayerInput &Input) const { return 9999; }
	};

	struct CRocketSaveDecision
	{
		CNetObj_PlayerInput m_Input{};
		bool m_Triggered = false;
		bool m_Fired = false;
		bool m_KeptManualJump = false;
		int m_ManualJumpLife = -1;
		int m_BaselineLife = -1;
		int m_BestLife = -1;
	};

	// After a rocket save shot the client keeps the grenade selected, because
	// the saver only ever asks for the grenade. Remember the weapon the player
	// had and hand it back once the shot is out. This never touches the input
	// while the saver is arming: it only runs after a confirmed shot and only
	// while the grenade is actually the active weapon.
	// Values are protocol selections: weapon + 1, 0 = none.
	class CRocketWeaponRestore
	{
	public:
		static bool Eligible(int Selection)
		{
			return Selection > 0 && Selection <= NUM_WEAPONS && Selection != WEAPON_GRENADE + 1 &&
			       Selection != WEAPON_NINJA + 1;
		}
		void Reset()
		{
			m_Target = 0;
			m_ShotTick = -1;
		}
		// Called every tick before the saver runs; a non-grenade weapon is the
		// player's own selection and becomes the restore target.
		void Observe(int Wanted, int Active)
		{
			const int Seen = Eligible(Wanted) ? Wanted : Eligible(Active) ? Active : 0;
			if(Seen != 0)
				m_Target = Seen;
		}
		// A rescue shot was really fired this tick.
		void OnShot(int GameTick)
		{
			m_ShotTick = m_Target != 0 ? GameTick : -1;
		}
		// Returns the selection to write, 0 when the input must stay untouched.
		int Next(int GameTick, int Active)
		{
			if(m_ShotTick < 0 || m_Target == 0)
				return 0;
			if(Active == m_Target)
			{
				// Switch completed.
				m_ShotTick = -1;
				return 0;
			}
			if(GameTick <= m_ShotTick + 4)
				return 0; // the generated press is still settling
			if(GameTick > m_ShotTick + 150 || Active != WEAPON_GRENADE + 1)
			{
				// Expired, or the player already holds something else.
				m_ShotTick = -1;
				return 0;
			}
			return m_Target;
		}
		bool Pending() const { return m_ShotTick >= 0; }
		int Target() const { return m_Target; }

	private:
		int m_Target = 0;
		int m_ShotTick = -1;
	};

	class CRocketSave
	{
		int m_ReleaseModulo = 0;
		int m_GeneratedFire = -1;
		int m_GeneratedClientId = -1;
		int m_GeneratedSlot = -1;
		int m_AttackTickOnIssue = -1;
		CRocketWeaponRestore m_WeaponRestore;
		CNetObj_PlayerInput AdjustInputCore(CGameClient *pGameClient, CNetObj_PlayerInput Input, int ClientId, bool ManualJump);
		static CRocketSaveDecision SearchShots(const IRocketSaveProbe &Probe, CNetObj_PlayerInput Input, float MouseRadius, int BaselineLife);

	public:
		void OnReset()
		{
			m_ReleaseModulo = 0;
			m_GeneratedFire = m_GeneratedClientId = m_GeneratedSlot = m_AttackTickOnIssue = -1;
			m_WeaponRestore.Reset();
		}
		const CRocketWeaponRestore &WeaponRestore() const { return m_WeaponRestore; }
		bool OwnsPendingFire(int Fire, int ClientId, int Slot) const
		{
			return m_ReleaseModulo != 0 && (Fire & 1) && Fire == m_GeneratedFire &&
			       ClientId == m_GeneratedClientId && Slot == m_GeneratedSlot;
		}
		bool ReleasesPendingFireNow(int GameTick, int Fire) const
		{
			return ShouldReleaseFire(GameTick, m_ReleaseModulo, Fire);
		}
		// Run after Avoid in the prepared-input portion of CControls::SnapInput.
		CNetObj_PlayerInput AdjustInput(CGameClient *pGameClient, CNetObj_PlayerInput Input, int ClientId, bool ManualJump);

		// Recovered policy from 0x1402ba150; public for deterministic oracle tests.
		static CRocketSaveDecision RequireReadyShot(CRocketSaveDecision Decision, const CNetObj_PlayerInput &Original, bool Ready);
		static CRocketSaveDecision Search(const IRocketSaveProbe &Probe, CNetObj_PlayerInput Input, float MouseRadius);
		static CRocketSaveDecision SearchWithManualJump(const IRocketSaveProbe &Probe, CNetObj_PlayerInput Input, float MouseRadius, bool ManualJump);
		static CRocketSaveDecision SearchWorld(const CGameWorld &World, int ClientId, CNetObj_PlayerInput Input, float MouseRadius, bool ManualJump = false, bool CanFire = true);
		static bool FrozenGate(bool Fng, bool ServerNamedDDNet, int FreezeTime, bool DeepFrozen, bool LiveFrozen, int SnapshotWeapon)
		{
			if(!Fng && ServerNamedDDNet)
				return FreezeTime > 0 || DeepFrozen || LiveFrozen;
			return FreezeTime > 0 || SnapshotWeapon == WEAPON_NINJA;
		}
		static bool ShouldReleaseFire(int GameTick, int Modulo, int Fire)
		{
			return Modulo != 0 && GameTick % Modulo == 0 && (Fire & 1) != 0;
		}
	};
}

#endif
