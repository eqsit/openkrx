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

	// OpenKRX addition. A chosen fire counter is only a request; wait for an
	// authoritative grenade attack, or a safe cancellation of preparation,
	// before queuing the previous weapon.
	class CRocketWeaponRestore
	{
		int m_Weapon = -1;
		int m_AttackBefore = -1;
		bool m_AwaitingShot = false;
		bool m_Returning = false;

	public:
		void Reset() { *this = {}; }
		void Prepare(int Weapon)
		{
			if(m_Weapon < 0 && Weapon >= WEAPON_HAMMER && Weapon < WEAPON_NINJA && Weapon != WEAPON_GRENADE)
				m_Weapon = Weapon;
		}
		void OnIssued(int AttackTick)
		{
			if(m_Weapon < 0)
				return;
			m_AttackBefore = AttackTick;
			m_AwaitingShot = true;
			m_Returning = false;
		}
		void CancelUnfired() { m_AwaitingShot = false; }
		bool NeedsSafeExit() const { return m_Weapon >= 0 && !m_Returning; }
		void OnSafeWithoutShot()
		{
			m_AwaitingShot = false;
			m_Returning = m_Weapon >= 0;
		}
		void Observe(int ActiveWeapon, int AttackTick)
		{
			if(m_AwaitingShot && ActiveWeapon == WEAPON_GRENADE && AttackTick > m_AttackBefore)
			{
				m_AwaitingShot = false;
				m_Returning = true;
			}
			else if(m_Returning && ActiveWeapon == m_Weapon)
				Reset();
		}
		int WantedWeapon(bool NewShot, bool PendingRelease) const
		{
			return m_Returning && !NewShot && !PendingRelease ? m_Weapon + 1 : 0;
		}
		int Weapon() const { return m_Weapon; }
	};

	class CRocketSave
	{
		CRocketWeaponRestore m_WeaponRestore;
		int m_RestoreClientId = -1;
		int m_RestoreSlot = -1;
		int m_ReleaseModulo = 0;
		int m_GeneratedFire = -1;
		int m_GeneratedClientId = -1;
		int m_GeneratedSlot = -1;
		int m_AttackTickOnIssue = -1;
		static CRocketSaveDecision SearchShots(const IRocketSaveProbe &Probe, CNetObj_PlayerInput Input, float MouseRadius, int BaselineLife);
		CNetObj_PlayerInput AdjustInputCore(CGameClient *pGameClient, CNetObj_PlayerInput Input, int ClientId, bool ManualJump);

	public:
		void OnReset()
		{
			CancelWeaponRestore();
			m_ReleaseModulo = 0;
			m_GeneratedFire = m_GeneratedClientId = m_GeneratedSlot = m_AttackTickOnIssue = -1;
		}
		void CancelWeaponRestore()
		{
			m_WeaponRestore.Reset();
			m_RestoreClientId = m_RestoreSlot = -1;
		}
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
		static bool SafeWithoutShot(const CGameWorld &World, int ClientId, CNetObj_PlayerInput Input, bool ManualJump);
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
