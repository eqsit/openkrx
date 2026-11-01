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

	// The saver selects the grenade to fire its rescue shot and, like the
	// original, left it selected afterwards. Remember the weapon the player had
	// before that switch and put it back once the generated shot is over.
	// Values are protocol weapon selections: weapon + 1, 0 = none.
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
			m_Pending = false;
			m_Deadline = 0;
		}
		// Called every tick before the saver may override the selection. A value
		// that is not the grenade is the player's own weapon, so it becomes the
		// restore target.
		void Observe(int Wanted, int Active)
		{
			const int Seen = Eligible(Wanted) ? Wanted : Eligible(Active) ? Active : 0;
			if(Seen != 0)
				m_Target = Seen;
		}
		// The saver just selected the grenade; its choice is temporary now.
		void Begin(int GameTick)
		{
			m_Pending = m_Target != 0;
			m_Deadline = GameTick + 200;
		}
		bool Pending() const { return m_Pending; }
		int Target() const { return m_Target; }
		// Returns true while the restore owns the selection. A grenade release
		// that is still queued keeps the grenade selected.
		bool Steals(int GameTick, int Active, bool ShotPending)
		{
			if(!m_Pending)
				return false;
			if(Active == m_Target || GameTick > m_Deadline)
			{
				m_Pending = false;
				return false;
			}
			return !ShotPending;
		}
		// The player picked another weapon himself; nothing to restore.
		void PlayerPicked(int Wanted)
		{
			if(m_Pending && Eligible(Wanted) && Wanted != m_Target)
				m_Pending = false;
		}

	private:
		int m_Target = 0;
		bool m_Pending = false;
		int m_Deadline = 0;
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
