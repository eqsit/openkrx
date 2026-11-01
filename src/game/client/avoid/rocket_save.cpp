#include "rocket_save.h"

#include <base/dbg.h>
#include <base/str.h>
#include <base/vmath.h>

#include <engine/serverbrowser.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/projectile.h>
#include <game/client/prediction/gameworld.h>
#include <game/client/projectile_data.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <vector>

namespace OpenKrx
{
	namespace
	{
		bool IsKrxFng(CGameClient *pGameClient)
		{
			return pGameClient->m_GameWorld.m_WorldConfig.m_IsFNG ||
			       str_find_nocase(pGameClient->Client()->ServerInfo().m_aGameType, "fng") != nullptr;
		}

		// Translate the reference's 44-byte scored-input sort (2b9390/2b8e20).
		// GCC's std::sort permutes equal lifetimes differently, changing shot angles.
		// The scan has at most 65 entries; the reference's median-of-nine partition
		// and stable insertion sort for <=32 entries cover this bounded input.
		template<typename T>
		void Median3(T &First, T &Middle, T &Last)
		{
			if(Middle.m_Life > First.m_Life)
				std::swap(Middle, First);
			if(Last.m_Life > Middle.m_Life)
			{
				std::swap(Last, Middle);
				if(Middle.m_Life > First.m_Life)
					std::swap(Middle, First);
			}
		}

		template<typename It>
		std::pair<It, It> Partition(It First, It Last)
		{
			It Middle = First + (Last - First) / 2;
			It Final = Last - 1;
			if(Last - First > 41)
			{
				const auto Step = (Last - First) / 8;
				Median3(*First, *(First + Step), *(First + 2 * Step));
				Median3(*(Middle - Step), *Middle, *(Middle + Step));
				Median3(*(Final - 2 * Step), *(Final - Step), *Final);
				Median3(*(First + Step), *Middle, *(Final - Step));
			}
			else
				Median3(*First, *Middle, *Final);
			It PivotFirst = Middle, PivotLast = Middle + 1;
			while(First < PivotFirst && (PivotFirst - 1)->m_Life == PivotFirst->m_Life)
				--PivotFirst;
			while(PivotLast < Last && PivotLast->m_Life == PivotFirst->m_Life)
				++PivotLast;
			It GreaterFirst = PivotLast, GreaterLast = PivotFirst;
			for(;;)
			{
				for(; GreaterFirst < Last; ++GreaterFirst)
				{
					if(PivotFirst->m_Life > GreaterFirst->m_Life)
						continue;
					if(GreaterFirst->m_Life > PivotFirst->m_Life)
						break;
					if(PivotLast != GreaterFirst)
						std::iter_swap(PivotLast, GreaterFirst);
					++PivotLast;
				}
				for(; First < GreaterLast; --GreaterLast)
				{
					const It Previous = GreaterLast - 1;
					if(Previous->m_Life > PivotFirst->m_Life)
						continue;
					if(PivotFirst->m_Life > Previous->m_Life)
						break;
					if(--PivotFirst != Previous)
						std::iter_swap(PivotFirst, Previous);
				}
				if(GreaterLast == First && GreaterFirst == Last)
					return {PivotFirst, PivotLast};
				if(GreaterLast == First)
				{
					if(PivotLast != GreaterFirst)
						std::iter_swap(PivotFirst, PivotLast);
					++PivotLast;
					std::iter_swap(PivotFirst, GreaterFirst);
					++PivotFirst;
					++GreaterFirst;
				}
				else if(GreaterFirst == Last)
				{
					if(--GreaterLast != --PivotFirst)
						std::iter_swap(GreaterLast, PivotFirst);
					std::iter_swap(PivotFirst, --PivotLast);
				}
				else
				{
					std::iter_swap(GreaterFirst, --GreaterLast);
					++GreaterFirst;
				}
			}
		}

		template<typename It>
		void ReferenceSort(It First, It Last)
		{
			while(Last - First > 32)
			{
				const auto [PivotFirst, PivotLast] = Partition(First, Last);
				if(PivotFirst - First < Last - PivotLast)
				{
					ReferenceSort(First, PivotFirst);
					First = PivotLast;
				}
				else
				{
					ReferenceSort(PivotLast, Last);
					Last = PivotFirst;
				}
			}
			if(First == Last)
				return;
			for(It Current = First + 1; Current != Last; ++Current)
			{
				auto Value = *Current;
				It Hole = Current;
				while(Hole != First && Value.m_Life > (Hole - 1)->m_Life)
				{
					*Hole = *(Hole - 1);
					--Hole;
				}
				*Hole = Value;
			}
		}

		bool Frozen(const CCharacter *pCharacter)
		{
			return !pCharacter || pCharacter->m_FreezeTime > 0 || pCharacter->Core()->m_DeepFrozen || pCharacter->Core()->m_LiveFrozen;
		}

		void Tick(CGameWorld &World, int ClientId, const CNetObj_PlayerInput &Input)
		{
			if(auto *pCharacter = World.GetCharacterById(ClientId))
			{
				pCharacter->OnDirectInput(&Input);
				++World.m_GameTick;
				pCharacter->OnPredictedInput(&Input);
				World.Tick();
			}
		}

		// 0x1402b6760 probes CheckTicks+1 steps and uses a different absent-character
		// sentinel (-1) from Blatant's constant-life helper (-5).
		int Life(const CGameWorld &Source, int ClientId, const CNetObj_PlayerInput &Input, int CheckTicks)
		{
			CGameWorld Copy;
			Copy.CopyWorldIsolated(&Source);
			Copy.m_WorldConfig.m_KrxPrediction = true;
			if(!Copy.GetCharacterById(ClientId))
				return -1;
			for(int Step = 0; Step <= CheckTicks; ++Step)
			{
				Tick(Copy, ClientId, Input);
				if(Frozen(Copy.GetCharacterById(ClientId)))
					return Step;
			}
			return 9999;
		}

		class CWorldRocketProbe final : public IRocketSaveProbe
		{
			const CGameWorld &m_Source;
			int m_ClientId;

		public:
			CWorldRocketProbe(const CGameWorld &Source, int ClientId) : m_Source(Source), m_ClientId(ClientId) {}

			int DangerLife(const CNetObj_PlayerInput &CurrentInput, const CNetObj_PlayerInput &ShotInput) const override
			{
				CGameWorld Danger;
				Danger.CopyWorldIsolated(&m_Source);
				Danger.m_WorldConfig.m_KrxPrediction = true;
				// Only this danger world is stripped. Rescue probes retain projectiles,
				// lasers and other tees because they can affect a grenade's outcome.
				for(CEntity *pEntity = Danger.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pEntity;)
				{
					CEntity *pNext = pEntity->TypeNext();
					delete pEntity;
					pEntity = pNext;
				}
				for(int Id = 0; Id < MAX_CLIENTS; ++Id)
				{
					auto *pCharacter = Danger.GetCharacterById(Id);
					// KRX also preserves the other local tee (predicted dummy):
					// 2ba473 skips character+0x98, the IsLocal field written by
					// CCharacter::Read at 2195fd. Removing it changes danger
					// prediction while the full rescue search still contains it.
					if(pCharacter && Id != m_ClientId && !pCharacter->m_IsLocal)
						delete pCharacter;
				}
				Tick(Danger, m_ClientId, CurrentInput);
				return Life(Danger, m_ClientId, ShotInput, 10);
			}

			int ShotLife(const CNetObj_PlayerInput &ShotInput) const override
			{
				return Life(m_Source, m_ClientId, ShotInput, 21);
			}

			int NoShotLife(const CNetObj_PlayerInput &Input) const override
			{
				CGameWorld ExistingOnly;
				ExistingOnly.CopyWorldIsolated(&m_Source);
				if(auto *pLocal = ExistingOnly.GetCharacterById(m_ClientId))
					pLocal->SetWeaponAmmo(WEAPON_GRENADE, 0);
				return Life(ExistingOnly, m_ClientId, Input, 21);
			}
		};
	}

	bool RocketShotReady(const CCharacter *pPredicted, const CCharacter *pAuthoritative)
	{
		for(const auto *pCharacter : {pPredicted, pAuthoritative})
		{
			if(!pCharacter || pCharacter->GetActiveWeapon() != WEAPON_GRENADE ||
				pCharacter->DiagnosticReloadTimer() != 0 || pCharacter->DiagnosticNumInputs() < 2 ||
				pCharacter->m_FreezeTime > 0 || pCharacter->Core()->m_DeepFrozen || pCharacter->Core()->m_LiveFrozen ||
				!pCharacter->Core()->m_aWeapons[WEAPON_GRENADE].m_Got ||
				pCharacter->Core()->m_aWeapons[WEAPON_GRENADE].m_Ammo == 0)
				return false;
		}
		return true;
	}

	CRocketSaveDecision CRocketSave::RequireReadyShot(CRocketSaveDecision Decision, const CNetObj_PlayerInput &Original, bool Ready)
	{
		if(Ready || !Decision.m_Fired)
			return Decision;
		// Keep the triggered weapon preparation, but never queue a shot (or a
		// restored jump) whose safety depended on keeping Fire through reload.
		Decision.m_Input = Original;
		Decision.m_Input.m_WantedWeapon = WEAPON_GRENADE + 1;
		Decision.m_Fired = false;
		Decision.m_KeptManualJump = false;
		return Decision;
	}

	bool IsKrxCharacterFrozen(CGameClient *pGameClient, int ClientId)
	{
		if(ClientId == -1)
			ClientId = pGameClient->m_aLocalIds[g_Config.m_ClDummy];
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			return false;
		const auto *pCharacter = pGameClient->m_GameWorld.GetCharacterById(ClientId);
		if(!pCharacter)
			return false;
		// 2b43a0 reads game type; 2b4310 reads the adjacent server *name*.
		// Their different fields are significant on FNG and non-DDNet servers.
		const bool NamedDDNet = str_find_nocase(pGameClient->Client()->ServerInfo().m_aName, "DDNet") != nullptr;
		return CRocketSave::FrozenGate(IsKrxFng(pGameClient), NamedDDNet, pCharacter->m_FreezeTime,
			pCharacter->Core()->m_DeepFrozen, pCharacter->Core()->m_LiveFrozen,
			pGameClient->m_Snap.m_aCharacters[ClientId].m_Cur.m_Weapon);
	}

	CRocketSaveDecision CRocketSave::Search(const IRocketSaveProbe &Probe, CNetObj_PlayerInput Input, float MouseRadius)
	{
		CRocketSaveDecision Result;
		Result.m_Input = Input;
		if((Input.m_Fire & 1) != 0)
			return Result;
		CNetObj_PlayerInput Shot = Input;
		++Shot.m_Fire;
		Result.m_BaselineLife = Probe.DangerLife(Input, Shot);
		if(Result.m_BaselineLife >= 7)
			return Result;
		return SearchShots(Probe, Input, MouseRadius, Result.m_BaselineLife);
	}

	CRocketSaveDecision CRocketSave::SearchShots(const IRocketSaveProbe &Probe, CNetObj_PlayerInput Input, float MouseRadius, int BaselineLife)
	{
		CRocketSaveDecision Result;
		Result.m_Input = Input;
		Result.m_BaselineLife = BaselineLife;
		auto Shot = Input;
		++Shot.m_Fire;
		Result.m_Triggered = true;
		Result.m_Input.m_WantedWeapon = WEAPON_GRENADE + 1;
		struct CScored
		{
			CNetObj_PlayerInput m_Input;
			int m_Life;
		};
		std::vector<CScored> vCandidates;
		// Keep floating-point accumulation and the inclusive final bound: the
		// original increments by float pi/32, rather than using an integer index.
		constexpr float STEP = 0.09817477315664291f;
		constexpr float END = 6.2831854820251465f;
		for(float Angle = 0.0f; Angle <= END; Angle += STEP)
		{
			const vec2 Direction = normalize(vec2(std::cos(Angle), std::sin(Angle)));
			Shot.m_TargetX = static_cast<int>(Direction.x * MouseRadius);
			Shot.m_TargetY = static_cast<int>(Direction.y * MouseRadius);
			vCandidates.push_back({Shot, Probe.ShotLife(Shot)});
		}
		ReferenceSort(vCandidates.begin(), vCandidates.end());
		if(!vCandidates.empty())
		{
			Result.m_BestLife = vCandidates.front().m_Life;
			if(Result.m_BestLife > 8)
			{
				// KRX writes only aim and increments the live fire counter. It does
				// not replace movement, hook, jump or flags with the simulated shot.
				Result.m_Input.m_TargetX = vCandidates.front().m_Input.m_TargetX;
				Result.m_Input.m_TargetY = vCandidates.front().m_Input.m_TargetY;
				++Result.m_Input.m_Fire;
				Result.m_Fired = true;
			}
		}
		return Result;
	}

	CRocketSaveDecision CRocketSave::SearchWithManualJump(const IRocketSaveProbe &Probe, CNetObj_PlayerInput Input, float MouseRadius, bool ManualJump)
	{
		// This is the user's requested manual-input extension. The original
		// Search and its thresholds remain unchanged, including the fallback.
		if(ManualJump && Input.m_Jump == 0 && !(Input.m_Fire & 1))
		{
			auto JumpInput = Input;
			JumpInput.m_Jump = 1;
			// The manual variant may start while the player has the gun selected.
			// Probe the same grenade switch that will be sent with the rescue.
			JumpInput.m_WantedWeapon = WEAPON_GRENADE + 1;
			auto WithJump = Search(Probe, JumpInput, MouseRadius);
			if(!WithJump.m_Fired && !WithJump.m_Triggered)
			{
				const int WithoutShot = Probe.NoShotLife(JumpInput);
				// The original danger probe itself fires a hypothetical grenade.
				// It can therefore say SAFE precisely when a rocket WOULD rescue
				// the jump. For this manual-only extension, also test the real
				// no-new-shot jump before deciding that no rescue is necessary.
				if(WithoutShot >= 0 && WithoutShot != 9999)
				{
					WithJump = SearchShots(Probe, JumpInput, MouseRadius, WithJump.m_BaselineLife);
					WithJump.m_ManualJumpLife = WithoutShot;
				}
			}
			if(WithJump.m_Fired)
			{
				WithJump.m_KeptManualJump = true;
				return WithJump;
			}
			auto Fallback = Search(Probe, Input, MouseRadius);
			if(!Fallback.m_Fired && WithJump.m_Triggered)
			{
				// Selecting a grenade can take another input tick. Prepare that
				// switch without firing or jumping early; a held manual jump is
				// reconsidered from the real state on the next tick.
				Fallback.m_Input.m_WantedWeapon = WEAPON_GRENADE + 1;
				Fallback.m_ManualJumpLife = WithJump.m_ManualJumpLife;
			}
			return Fallback;
		}
		return Search(Probe, Input, MouseRadius);
	}

	CRocketSaveDecision CRocketSave::SearchWorld(const CGameWorld &World, int ClientId, CNetObj_PlayerInput Input, float MouseRadius, bool ManualJump, bool CanFire)
	{
		const CWorldRocketProbe Probe(World, ClientId);
		// An already flying grenade can support the manual jump even while the
		// original release state prevents firing again. Do not invent a shot.
		if(ManualJump && Input.m_Jump == 0)
		{
			auto &Source = const_cast<CGameWorld &>(World);
			bool HasOwnGrenade = false;
			for(auto *pProjectile = static_cast<CProjectile *>(Source.FindFirst(CGameWorld::ENTTYPE_PROJECTILE)); pProjectile;
				pProjectile = static_cast<CProjectile *>(pProjectile->TypeNext()))
				HasOwnGrenade |= pProjectile->GetOwner() == ClientId && pProjectile->GetData().m_Type == WEAPON_GRENADE;
			if(HasOwnGrenade)
			{
				auto JumpInput = Input;
				JumpInput.m_Jump = 1;
				// Do not count another full-auto shot after reload as support from
				// the already flying grenade. Existing projectiles still explode.
				if(Probe.NoShotLife(JumpInput) == 9999)
				{
					CRocketSaveDecision Result;
					Result.m_Input = JumpInput;
					Result.m_KeptManualJump = true;
					return Result;
				}
			}
		}
		if(!CanFire)
		{
			CRocketSaveDecision Result;
			Result.m_Input = Input;
			return Result;
		}
		return SearchWithManualJump(Probe, Input, MouseRadius, ManualJump);
	}

	bool CRocketSave::SafeWithoutShot(const CGameWorld &World, int ClientId, CNetObj_PlayerInput Input, bool ManualJump)
	{
		// Preparing the grenade must not prevent abandoning an unnecessary or
		// failed save. Test the actual no-new-shot trajectory, not DangerLife's
		// hypothetical shot. A suppressed physical jump remains part of the test.
		if(ManualJump && Input.m_Jump == 0)
			Input.m_Jump = 1;
		return CWorldRocketProbe(World, ClientId).NoShotLife(Input) == 9999;
	}

	CNetObj_PlayerInput CRocketSave::AdjustInput(CGameClient *pGameClient, CNetObj_PlayerInput Input, int ClientId, bool ManualJump)
	{
		const auto *pCharacter = pGameClient->m_GameWorld.GetCharacterById(ClientId);
		const bool Valid = g_Config.m_RocketRestoreWeapon && g_Config.m_KrxGrenadeSaver && pCharacter &&
			pGameClient->Client()->State() == IClient::STATE_ONLINE &&
			!pGameClient->m_Snap.m_SpecInfo.m_Active && pGameClient->m_Snap.m_pLocalCharacter;
		if(!Valid || m_RestoreClientId != ClientId || m_RestoreSlot != g_Config.m_ClDummy)
			CancelWeaponRestore();
		if(Valid)
		{
			m_RestoreClientId = ClientId;
			m_RestoreSlot = g_Config.m_ClDummy;
			if(m_WeaponRestore.Weapon() >= 0 && !pCharacter->Core()->m_aWeapons[m_WeaponRestore.Weapon()].m_Got)
				m_WeaponRestore.Reset();
			m_WeaponRestore.Observe(pCharacter->GetActiveWeapon(), pCharacter->GetAttackTick());
		}
		const auto Output = AdjustInputCore(pGameClient, Input, ClientId, ManualJump);
		auto Restored = Output;
		if(Valid)
		{
			// Capture before the first automatic WantedWeapon=4 reaches controls.
			// Persistent WantedWeapon=4 on later ticks must never replace this slot.
			if(Output.m_WantedWeapon == WEAPON_GRENADE + 1 && Input.m_WantedWeapon != WEAPON_GRENADE + 1)
			{
				int Previous = Input.m_WantedWeapon ? Input.m_WantedWeapon - 1 : pCharacter->GetActiveWeapon();
				if(Previous == WEAPON_GRENADE)
					Previous = pCharacter->GetActiveWeapon();
				if(Previous >= 0 && Previous < WEAPON_NINJA && pCharacter->Core()->m_aWeapons[Previous].m_Got)
					m_WeaponRestore.Prepare(Previous);
			}
			const bool Issued = Output.m_Fire != Input.m_Fire && (Output.m_Fire & 1) &&
				OwnsPendingFire(Output.m_Fire, ClientId, g_Config.m_ClDummy);
			if(Issued)
				m_WeaponRestore.OnIssued(pCharacter->GetAttackTick());
			if(!Issued && m_ReleaseModulo == 0 && !(Output.m_Fire & 1) && m_WeaponRestore.NeedsSafeExit() &&
				!IsKrxCharacterFrozen(pGameClient, ClientId) && pGameClient->m_PredictedWorld.GetCharacterById(ClientId))
			{
				CGameWorld Snapshot;
				Snapshot.CopyWorldIsolated(&pGameClient->m_PredictedWorld);
				Snapshot.m_WorldConfig.m_IsFNG = IsKrxFng(pGameClient);
				Snapshot.m_WorldConfig.m_PredictTiles = true;
				Snapshot.m_WorldConfig.m_PredictFreeze = 1;
				Snapshot.m_WorldConfig.m_PredictWeapons = true;
				Snapshot.m_WorldConfig.m_PredictDDRace = true;
				Snapshot.m_WorldConfig.m_KrxPrediction = true;
				Snapshot.m_WorldConfig.m_KrxAvoidDeath = g_Config.m_AvoidFreeze && g_Config.m_AvoidAgentType == 2 && g_Config.m_AvoidBlatantDeath;
				Snapshot.m_WorldConfig.m_KrxAvoidTeles = g_Config.m_AvoidFreeze && g_Config.m_AvoidAgentType == 2 && g_Config.m_AvoidBlatantTeles;
				Snapshot.m_WorldConfig.m_KrxExplosionStrengthValid = true;
				Snapshot.m_WorldConfig.m_KrxExplosionStrength = pGameClient->m_aTuning[g_Config.m_ClDummy].m_ExplosionStrength;
				if(SafeWithoutShot(Snapshot, ClientId, Output, ManualJump && g_Config.m_RocketPreserveJump))
				{
					m_WeaponRestore.OnSafeWithoutShot();
					if(g_Config.m_AvoidDebug)
						dbg_msg("rocket-save", "restore after safe cancellation cid=%d attack_tick=%d", ClientId, pCharacter->GetAttackTick());
				}
			}
			const int Wanted = m_WeaponRestore.WantedWeapon(Issued, m_ReleaseModulo != 0);
			if(Wanted)
			{
				// DoWeaponSwitch queues this through the actual reload timer. Keep
				// requesting it until the authoritative weapon acknowledges the switch,
				// even if Search still emits WantedWeapon=4 while reloading.
				Restored.m_WantedWeapon = Wanted;
				if(g_Config.m_AvoidDebug && Input.m_WantedWeapon != Wanted)
					dbg_msg("rocket-save", "restore weapon=%d cid=%d attack_tick=%d reload=%d", Wanted - 1,
						ClientId, pCharacter->GetAttackTick(), pCharacter->DiagnosticReloadTimer());
			}
		}
		return Restored;
	}

	CNetObj_PlayerInput CRocketSave::AdjustInputCore(CGameClient *pGameClient, CNetObj_PlayerInput Input, int ClientId, bool ManualJump)
	{
		const auto *pAuthoritative = pGameClient->m_GameWorld.GetCharacterById(ClientId);
		// Reference fire pulses; the requested manual-jump extension is applied below.
		bool CanFire = true;
		// 0x1402ba150 releases a generated shot on a randomly selected tick
		// divisor (1..5), not after a fixed elapsed duration. Release happens even
		// if the feature has since been disabled.
		if(m_ReleaseModulo != 0)
		{
			if(!ShouldReleaseFire(pGameClient->m_GameWorld.GameTick(), m_ReleaseModulo, Input.m_Fire))
				CanFire = false;
			else
			{
				++Input.m_Fire;
				m_ReleaseModulo = 0;
			}
		}
		if(!g_Config.m_KrxGrenadeSaver || ClientId < 0 || pGameClient->Client()->State() != IClient::STATE_ONLINE ||
			pGameClient->m_Snap.m_SpecInfo.m_Active || !pGameClient->m_Snap.m_pLocalCharacter)
			return Input;
		if(!pAuthoritative || IsKrxCharacterFrozen(pGameClient, ClientId))
			return Input;
		ManualJump = ManualJump && g_Config.m_RocketPreserveJump && Input.m_Jump == 0;
		CanFire = CanFire && !(Input.m_Fire & 1) && pAuthoritative->Core()->m_aWeapons[WEAPON_GRENADE].m_Got;
		if(!CanFire && !ManualJump)
			return Input;
		CGameWorld &Source = pGameClient->m_PredictedWorld;
		if(!Source.GetCharacterById(ClientId))
			return Input;
		CGameWorld Snapshot;
		Snapshot.CopyWorldIsolated(&Source);
		// Recovered stores to world+0x4ea/0x4ec/0x4f0/0x4f4/0x4f5.
		Snapshot.m_WorldConfig.m_IsFNG = IsKrxFng(pGameClient);
		Snapshot.m_WorldConfig.m_PredictTiles = true;
		Snapshot.m_WorldConfig.m_PredictFreeze = 1;
		Snapshot.m_WorldConfig.m_PredictWeapons = true;
		Snapshot.m_WorldConfig.m_PredictDDRace = true;
		Snapshot.m_WorldConfig.m_KrxPrediction = true;
		Snapshot.m_WorldConfig.m_KrxAvoidDeath = g_Config.m_AvoidFreeze && g_Config.m_AvoidAgentType == 2 && g_Config.m_AvoidBlatantDeath;
		Snapshot.m_WorldConfig.m_KrxAvoidTeles = g_Config.m_AvoidFreeze && g_Config.m_AvoidAgentType == 2 && g_Config.m_AvoidBlatantTeles;
		Snapshot.m_WorldConfig.m_KrxExplosionStrengthValid = true;
		Snapshot.m_WorldConfig.m_KrxExplosionStrength = pGameClient->m_aTuning[g_Config.m_ClDummy].m_ExplosionStrength;
		const auto Start = std::chrono::steady_clock::now();
		auto Decision = SearchWorld(Snapshot, ClientId, Input, pGameClient->m_Controls.GetMaxMouseDistance(), ManualJump, CanFire);
		const auto Micros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - Start).count();
		if(g_Config.m_AvoidDebug && (Decision.m_Triggered || Decision.m_KeptManualJump || ManualJump || Micros > 20000))
		{
			const auto *pLocal = Source.GetCharacterById(ClientId);
			dbg_msg("rocket-save", "cid=%d tick=%d pos=%.0f,%.0f trigger=%d fired=%d baseline=%d best=%d hook=%d fire=%d->%d aim=%d,%d->%d,%d weapon=%d wanted=%d took=%lldus manual_jump=%d jump=%d->%d kept_jump=%d no_shot_jump_life=%d issued_press=%d reload=%d attack_tick=%d",
				ClientId, Source.GameTick(), pLocal->m_Pos.x, pLocal->m_Pos.y,
				Decision.m_Triggered, Decision.m_Fired, Decision.m_BaselineLife, Decision.m_BestLife,
				Input.m_Hook, Input.m_Fire, Decision.m_Input.m_Fire, Input.m_TargetX, Input.m_TargetY,
				Decision.m_Input.m_TargetX, Decision.m_Input.m_TargetY, pLocal->GetActiveWeapon(),
				Decision.m_Input.m_WantedWeapon, (long long)Micros, ManualJump,
				Input.m_Jump, Decision.m_Input.m_Jump, Decision.m_KeptManualJump, Decision.m_ManualJumpLife,
				Decision.m_Fired, pLocal->DiagnosticReloadTimer(), pLocal->GetAttackTick());
		}
		if(Decision.m_Fired)
		{
			m_GeneratedFire = Decision.m_Input.m_Fire;
			m_GeneratedClientId = ClientId;
			m_GeneratedSlot = g_Config.m_ClDummy;
			m_AttackTickOnIssue = Source.GetCharacterById(ClientId)->GetAttackTick();
			std::random_device Seed;
			std::mt19937 Generator(Seed());
			m_ReleaseModulo = std::uniform_int_distribution<int>(1, 5)(Generator);
		}
		return Decision.m_Input;
	}
}
