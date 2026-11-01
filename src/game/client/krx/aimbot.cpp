#include "aimbot.h"

#include <base/str.h>

#include <engine/shared/config.h>

#include <game/client/avoid/rocket_save.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/laser.h>
#include <game/client/prediction/entities/projectile.h>
#include <game/client/prediction/gameworld.h>
#include <game/collision.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

namespace OpenKrx
{
	namespace
	{
		constexpr float PI = 3.1415927410125732f;
		constexpr float SHOT_STEP = 0.015707964077591896f;
		constexpr float HAMMER_STEP = 0.09817477315664291f;

		int RandomInt(int Min, int Max)
		{
			static std::mt19937 s_Rng(std::random_device{}());
			return std::uniform_int_distribution<int>(Min, Max)(s_Rng);
		}
		float RandomAngle()
		{
			static std::mt19937 s_Rng(std::random_device{}());
			return std::uniform_real_distribution<float>(0.0f, 2.0f * PI)(s_Rng);
		}
		vec2 QuantizedAim(vec2 Direction, float Radius)
		{
			const float Length = length(Direction);
			if(Length == 0.0f)
				return vec2(0, 0);
			Direction = Direction / Length * Radius;
			return vec2((int)Direction.x, (int)Direction.y);
		}
		CNetObj_PlayerInput WithAim(CNetObj_PlayerInput Input, vec2 Aim)
		{
			Input.m_TargetX = (int)Aim.x;
			Input.m_TargetY = (int)Aim.y;
			return Input;
		}
		// 2ba8f0 uses centiradians, although the settings UI calls this FOV.
		// Candidate angle scans separately interpret the same value as degrees.
		bool InSelectionFov(vec2 Direction, vec2 Mouse, int Fov)
		{
			const float Delta = std::atan2(Direction.y, Direction.x) - std::atan2(Mouse.y, Mouse.x);
			return std::abs(std::atan2(std::sin(Delta), std::cos(Delta))) * 100.0f < Fov;
		}
		bool Frozen(const CCharacter *pCharacter)
		{
			return pCharacter && (pCharacter->m_FreezeTime > 0 || pCharacter->Core()->m_DeepFrozen || pCharacter->Core()->m_LiveFrozen);
		}
		bool Active(CGameClient *pGameClient, int Id)
		{
			return Id >= 0 && Id < MAX_CLIENTS && (!pGameClient || pGameClient->m_aClients[Id].m_Active);
		}
		bool SameGameTeam(CGameClient *pGameClient, const CGameWorld &World, int A, int B)
		{
			return pGameClient ? pGameClient->m_aClients[A].m_Team == pGameClient->m_aClients[B].m_Team : World.m_Teams.Team(A) == World.m_Teams.Team(B);
		}
		void Strip(CGameWorld &World, int Type)
		{
			for(CEntity *pEntity = World.FindFirst(Type); pEntity;)
			{
				CEntity *pNext = pEntity->TypeNext();
				delete pEntity;
				pEntity = pNext;
			}
		}
		void Tick(CGameWorld &World, int ClientId, const CNetObj_PlayerInput &Input, int TargetId = -1, const CNetObj_PlayerInput *pTargetInput = nullptr)
		{
			auto *pLocal = World.GetCharacterById(ClientId);
			auto *pTarget = World.GetCharacterById(TargetId);
			if(!pLocal)
				return;
			pLocal->OnDirectInput(&Input);
			if(pTarget && pTargetInput)
				pTarget->OnDirectInput(pTargetInput);
			++World.m_GameTick;
			pLocal->OnPredictedInput(&Input);
			if(pTarget && pTargetInput)
				pTarget->OnPredictedInput(pTargetInput);
			World.Tick();
		}
		// 2b57c0: target latest input is explicitly applied, CheckTicks is inclusive.
		bool HookHits(const CGameWorld &Source, int ClientId, int TargetId, CNetObj_PlayerInput Input)
		{
			CGameWorld World;
			World.CopyWorldIsolated(&Source);
			auto *pTarget = World.GetCharacterById(TargetId);
			if(!World.GetCharacterById(ClientId) || !pTarget)
				return false;
			const auto TargetInput = pTarget->DiagnosticLatestInput();
			Input.m_Hook = 1;
			for(int Step = 0; Step <= 15; ++Step)
			{
				Tick(World, ClientId, Input, TargetId, &TargetInput);
				auto *pLocal = World.GetCharacterById(ClientId);
				if(!pLocal)
					return false;
				const auto *pCore = pLocal->Core();
				if(pCore->HookedPlayer() == TargetId && pCore->m_HookState == HOOK_GRABBED)
					return true;
				if(pCore->m_HookState == HOOK_RETRACT_START || (pCore->HookedPlayer() == -1 && pCore->m_HookState == HOOK_GRABBED))
					return false;
			}
			return false;
		}
		struct CProbeResult
		{
			int m_Result = -1;
			int m_HitTick = -1;
		};
		CProbeResult LaserHits(const CGameWorld &Source, int ClientId, int TargetId, CNetObj_PlayerInput Input, int Ticks, bool TeamPlay, CGameClient *pGameClient)
		{
			CGameWorld World;
			World.CopyWorldIsolated(&Source);
			Strip(World, CGameWorld::ENTTYPE_LASER);
			if(!World.GetCharacterById(ClientId))
				return {};
			Input.m_Fire = 1;
			for(int Step = 0; Step <= Ticks; ++Step)
			{
				auto *pLocal = World.GetCharacterById(ClientId);
				if(!pLocal)
					return {};
				pLocal->OnDirectInput(&Input);
				++World.m_GameTick;
				pLocal->OnPredictedInput(&Input);
				// CLaser's constructor may already have hit a tee. Inspect before Tick
				// because a spent laser can be deleted during that tick.
				for(CEntity *pEntity = World.FindFirst(CGameWorld::ENTTYPE_LASER); pEntity; pEntity = pEntity->TypeNext())
				{
					auto *pLaser = static_cast<CLaser *>(pEntity);
					if(pLaser->GetOwner() != ClientId)
						continue;
					const int Hit = pLaser->GetHitClientId();
					if(Active(pGameClient, Hit) && Hit != ClientId)
					{
						if((TargetId != -1 && Hit != TargetId) || (TeamPlay && SameGameTeam(pGameClient, World, ClientId, Hit)))
							return {};
						return {pLaser->GetBounces(), Step};
					}
				}
				World.Tick();
				for(CEntity *pEntity = World.FindFirst(CGameWorld::ENTTYPE_LASER); pEntity; pEntity = pEntity->TypeNext())
				{
					auto *pLaser = static_cast<CLaser *>(pEntity);
					if(pLaser->GetOwner() != ClientId)
						continue;
					const int Hit = pLaser->GetHitClientId();
					if(Active(pGameClient, Hit) && Hit != ClientId)
					{
						if((TargetId != -1 && Hit != TargetId) || (TeamPlay && SameGameTeam(pGameClient, World, ClientId, Hit)))
							return {};
						return {pLaser->GetBounces(), Step};
					}
				}
			}
			return {};
		}
		CProbeResult ProjectileHits(const CGameWorld &Source, int ClientId, int TargetId, CNetObj_PlayerInput Input, const std::vector<CNetObj_PlayerInput> &vTargetInputs, bool TeamPlay, CGameClient *pGameClient)
		{
			CGameWorld World;
			World.CopyWorldIsolated(&Source);
			Strip(World, CGameWorld::ENTTYPE_PROJECTILE);
			if(!World.GetCharacterById(ClientId) || !World.GetCharacterById(TargetId))
				return {};
			int Hit = -1;
			Input.m_Fire = 1;
			for(int Step = 0; Step <= 30; ++Step)
			{
				auto *pLocal = World.GetCharacterById(ClientId);
				auto *pTarget = World.GetCharacterById(TargetId);
				if(!pLocal || !pTarget)
					return {};
				pLocal->OnDirectInput(&Input);
				if(Step < (int)vTargetInputs.size())
					pTarget->OnDirectInput(&vTargetInputs[Step]);
				for(CEntity *pEntity = World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pEntity; pEntity = pEntity->TypeNext())
				{
					auto *pProjectile = static_cast<CProjectile *>(pEntity);
					if(pProjectile->GetOwner() == ClientId)
					{
						pProjectile->SetHitClientIdObserver(&Hit);
						Input.m_Fire = 0;
					}
				}
				++World.m_GameTick;
				pLocal->OnPredictedInput(&Input);
				if(Step < (int)vTargetInputs.size())
					pTarget->OnPredictedInput(&vTargetInputs[Step]);
				World.Tick();
				if(Active(pGameClient, Hit))
				{
					if(Hit != TargetId || (TeamPlay && SameGameTeam(pGameClient, World, ClientId, Hit)))
						return {};
					return {1, Step};
				}
			}
			return {};
		}
		bool HammerHits(const CGameWorld &Source, int ClientId, CNetObj_PlayerInput Input)
		{
			CGameWorld World;
			World.CopyWorldIsolated(&Source);
			auto *pLocal = World.GetCharacterById(ClientId);
			if(!pLocal)
				return false;
			pLocal->ClearLastHammerHitClientId();
			Input.m_Fire = 1;
			for(int Step = 0; Step <= 5; ++Step)
			{
				Tick(World, ClientId, Input);
				pLocal = World.GetCharacterById(ClientId);
				if(!pLocal)
					return false;
				if(pLocal->GetLastHammerHitClientId() != -1)
					return true;
			}
			return false;
		}
		struct CScored
		{
			CNetObj_PlayerInput m_Input{};
			int m_Result = -1;
			int m_HitTick = -1;
			int m_RobustHits = 0;
		};
		static_assert(sizeof(CScored) == 52);
		// MSVC's median/insertion/partition ordering matters for equal and incomparable
		// scores. The reference uses conjunctions, not lexicographic score ordering.
		template<typename T, typename Less>
		void Median3(T &A, T &B, T &C, Less LessThan)
		{
			if(LessThan(B, A))
				std::swap(A, B);
			if(LessThan(C, B))
			{
				std::swap(B, C);
				if(LessThan(B, A))
					std::swap(A, B);
			}
		}
		template<typename It, typename Less>
		std::pair<It, It> Partition(It First, It Last, Less LessThan)
		{
			It Middle = First + (Last - First) / 2, Final = Last - 1;
			if(Last - First > 41)
			{
				const auto Step = (Last - First) / 8;
				Median3(*First, *(First + Step), *(First + 2 * Step), LessThan);
				Median3(*(Middle - Step), *Middle, *(Middle + Step), LessThan);
				Median3(*(Final - 2 * Step), *(Final - Step), *Final, LessThan);
				Median3(*(First + Step), *Middle, *(Final - Step), LessThan);
			}
			else
				Median3(*First, *Middle, *Final, LessThan);
			auto Equivalent = [&](const auto &A, const auto &B) { return !LessThan(A, B) && !LessThan(B, A); };
			It PivotFirst = Middle, PivotLast = Middle + 1;
			while(First < PivotFirst && Equivalent(*(PivotFirst - 1), *PivotFirst))
				--PivotFirst;
			while(PivotLast < Last && Equivalent(*PivotLast, *PivotFirst))
				++PivotLast;
			It GreaterFirst = PivotLast, GreaterLast = PivotFirst;
			for(;;)
			{
				for(; GreaterFirst < Last; ++GreaterFirst)
				{
					if(LessThan(*PivotFirst, *GreaterFirst))
						continue;
					if(LessThan(*GreaterFirst, *PivotFirst))
						break;
					if(PivotLast != GreaterFirst)
						std::iter_swap(PivotLast, GreaterFirst);
					++PivotLast;
				}
				for(; First < GreaterLast; --GreaterLast)
				{
					const It Previous = GreaterLast - 1;
					if(LessThan(*Previous, *PivotFirst))
						continue;
					if(LessThan(*PivotFirst, *Previous))
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
		template<typename It, typename Less>
		void ReferenceSort(It First, It Last, Less LessThan)
		{
			while(Last - First > 32)
			{
				const auto [A, B] = Partition(First, Last, LessThan);
				if(A - First < Last - B)
				{
					ReferenceSort(First, A, LessThan);
					First = B;
				}
				else
				{
					ReferenceSort(B, Last, LessThan);
					Last = A;
				}
			}
			if(First == Last)
				return;
			for(It Current = First + 1; Current != Last; ++Current)
			{
				auto Value = *Current;
				It Hole = Current;
				while(Hole != First && LessThan(Value, *(Hole - 1)))
				{
					*Hole = *(Hole - 1);
					--Hole;
				}
				*Hole = Value;
			}
		}
		std::optional<vec2> HookAim(const CGameWorld &Source, int ClientId, int TargetId, CNetObj_PlayerInput Input, vec2 Mouse, const CAimbotSettings &Settings, float Radius)
		{
			auto &MutableSource = const_cast<CGameWorld &>(Source);
			const auto *pLocal = MutableSource.GetCharacterById(ClientId);
			const auto *pTarget = MutableSource.GetCharacterById(TargetId);
			if(!pLocal || !pTarget)
				return std::nullopt;
			if(HookHits(Source, ClientId, TargetId, Input))
				return vec2(Input.m_TargetX, Input.m_TargetY);
			std::vector<CScored> vCandidates;
			const float Begin = std::atan2(pTarget->m_Pos.y - pLocal->m_Pos.y, pTarget->m_Pos.x - pLocal->m_Pos.x) + PI / 2.0f;
			const float End = Begin + PI;
			const int Accuracy = Settings.m_Advanced ? std::clamp(Settings.m_Accuracy, 1, 200) : 200;
			for(float Angle = Begin; Angle < End; Angle += PI / Accuracy)
			{
				const vec2 Edge((int)(std::cos(Angle) * 28.0f + pTarget->m_Pos.x), (int)(std::sin(Angle) * 28.0f + pTarget->m_Pos.y));
				CScored Candidate;
				Candidate.m_Input = WithAim(Input, QuantizedAim(Edge - pLocal->m_Pos, Radius));
				Candidate.m_Input.m_Hook = 1;
				if(HookHits(Source, ClientId, TargetId, Candidate.m_Input))
					vCandidates.push_back(Candidate);
			}
			if(vCandidates.empty())
				return std::nullopt;
			ReferenceSort(vCandidates.begin(), vCandidates.end(), [&](const CScored &A, const CScored &B) {
				return distance_squared(vec2(A.m_Input.m_TargetX, A.m_Input.m_TargetY), Mouse) < distance_squared(vec2(B.m_Input.m_TargetX, B.m_Input.m_TargetY), Mouse);
			});
			return vec2(vCandidates.front().m_Input.m_TargetX, vCandidates.front().m_Input.m_TargetY);
		}
		std::optional<vec2> WeaponAim(const CGameWorld &Source, int ClientId, int TargetId, CNetObj_PlayerInput Input, vec2 Mouse, const CAimbotSettings &Settings, float Radius, bool TeamPlay, CGameClient *pGameClient)
		{
			auto &MutableSource = const_cast<CGameWorld &>(Source);
			const auto *pLocal = MutableSource.GetCharacterById(ClientId);
			const auto *pTarget = MutableSource.GetCharacterById(TargetId);
			if(!pLocal || !pTarget)
				return std::nullopt;
			const int Weapon = pLocal->GetActiveWeapon();
			if(Weapon < WEAPON_HAMMER || Weapon > WEAPON_LASER || !Settings.m_aWeapons[Weapon].m_Enabled)
				return std::nullopt;
			const int Fov = Settings.m_aWeapons[Weapon].m_Fov;
			const float Span = Fov >= 315 ? PI * 2.0f : Fov * PI / 180.0f;
			if(Weapon == WEAPON_HAMMER)
			{
				Input = WithAim(Input, vec2((int)Mouse.x, (int)Mouse.y));
				if(HammerHits(Source, ClientId, Input))
					return vec2(Input.m_TargetX, Input.m_TargetY);
				const float Center = std::atan2(Mouse.y, Mouse.x);
				for(float Angle = Center - Span * 0.5f; Angle < Center + Span * 0.5f; Angle += HAMMER_STEP)
				{
					const vec2 Aim = QuantizedAim(vec2(std::cos(Angle), std::sin(Angle)), Radius);
					if(HammerHits(Source, ClientId, WithAim(Input, Aim)))
						return Aim;
				}
				return std::nullopt;
			}
			const bool Laser = Weapon == WEAPON_LASER || (Weapon == WEAPON_SHOTGUN && Source.m_WorldConfig.m_IsDDRace);
			if(Laser && LaserHits(Source, ClientId, -1, Input, 1, TeamPlay, pGameClient).m_Result == 0)
				return vec2(Input.m_TargetX, Input.m_TargetY);
			std::vector<CScored> vCandidates;
			const float Center = std::atan2(Mouse.y, Mouse.x);
			for(float Angle = Center - Span * 0.5f; Angle < Center + Span * 0.5f; Angle += SHOT_STEP)
			{
				CScored Candidate;
				Candidate.m_Input = WithAim(Input, QuantizedAim(vec2(std::cos(Angle), std::sin(Angle)), Radius));
				CProbeResult Probe;
				if(Laser)
					Probe = LaserHits(Source, ClientId, TargetId, Candidate.m_Input, 14, TeamPlay, pGameClient);
				else
					Probe = ProjectileHits(Source, ClientId, TargetId, Candidate.m_Input, {pTarget->DiagnosticLatestInput()}, TeamPlay, pGameClient);
				Candidate.m_Result = Probe.m_Result;
				Candidate.m_HitTick = Probe.m_HitTick;
				if(!Laser && Settings.m_Advanced && Settings.m_GrenadeMovePrediction)
				{
					for(int Trial = 0; Trial < 4; ++Trial)
					{
						std::vector<CNetObj_PlayerInput> vInputs;
						auto TargetInput = pTarget->DiagnosticLatestInput();
						for(int Step = 0; Step < 30; ++Step)
						{
							if(Trial == 0)
								TargetInput.m_Direction = TargetInput.m_Direction == 0;
							else
							{
								TargetInput = {};
								TargetInput.m_Direction = RandomInt(-1, 1);
								const float Angle = RandomAngle();
								const vec2 Aim = QuantizedAim(vec2(std::cos(Angle), std::sin(Angle)), Radius);
								TargetInput.m_TargetX = (int)Aim.x;
								TargetInput.m_TargetY = (int)Aim.y;
								TargetInput.m_Jump = RandomInt(0, 1);
								TargetInput.m_Fire = RandomInt(0, 1);
								TargetInput.m_Hook = RandomInt(0, 1);
							}
							vInputs.push_back(TargetInput);
						}
						if(ProjectileHits(Source, ClientId, TargetId, Candidate.m_Input, vInputs, TeamPlay, pGameClient).m_Result > 0)
							++Candidate.m_RobustHits;
					}
				}
				if(Candidate.m_Result >= (Laser ? 0 : 1) && Candidate.m_HitTick >= 0)
					vCandidates.push_back(Candidate);
			}
			if(vCandidates.empty())
			{
				return std::nullopt;
			}
			ReferenceSort(vCandidates.begin(), vCandidates.end(), [Laser](const CScored &A, const CScored &B) {
				return A.m_HitTick < B.m_HitTick && (Laser ? A.m_Result < B.m_Result : A.m_RobustHits > B.m_RobustHits);
			});
			return vec2(vCandidates.front().m_Input.m_TargetX, vCandidates.front().m_Input.m_TargetY);
		}
	} // namespace

	void CAimbot::OnReset()
	{
		m_HookTarget = m_WeaponTarget = m_ClientId = m_Slot = -1;
		m_HookAim = {};
		m_HookAcquired = false;
		m_AutoHookIssued = false;
		m_FireLength = 0;
		m_FireCounter = -1;
		m_vCachedTargets.clear();
	}
	CAimbotDecision CAimbot::SearchWorld(const CGameWorld &Source, CNetObj_PlayerInput Input, int ClientId, vec2 PhysicalMouse, const CAimbotSettings &Settings, float MouseRadius, bool TeamPlay, const std::array<bool, MAX_CLIENTS> *pFriends)
	{
		return Search(Source, nullptr, Input, ClientId, PhysicalMouse, Settings, MouseRadius, TeamPlay, pFriends);
	}
	CAimbotDecision CAimbot::AdjustInput(CGameClient *pGameClient, CNetObj_PlayerInput Input, int ClientId, vec2 PhysicalMouse, const CAimbotSettings &Settings)
	{
		const auto &Source = pGameClient->m_PredictedWorld.GetCharacterById(ClientId) ? pGameClient->m_PredictedWorld : pGameClient->m_GameWorld;
		const bool TeamPlay = pGameClient->m_Snap.m_pGameInfoObj && (pGameClient->m_Snap.m_pGameInfoObj->m_GameFlags & GAMEFLAG_TEAMS);
		return Search(Source, pGameClient, Input, ClientId, PhysicalMouse, Settings, pGameClient->m_Controls.GetMaxMouseDistance(), TeamPlay, nullptr);
	}
	CAimbotDecision CAimbot::Search(const CGameWorld &Source, CGameClient *pGameClient, CNetObj_PlayerInput Input, int ClientId, vec2 Mouse, const CAimbotSettings &Settings, float Radius, bool TeamPlay, const std::array<bool, MAX_CLIENTS> *pFriends)
	{
		CAimbotDecision Decision;
		Decision.m_Input = Input;
		const int Slot = pGameClient ? g_Config.m_ClDummy : 0;
		if(m_ClientId != ClientId || m_Slot != Slot)
		{
			OnReset();
			m_ClientId = ClientId;
			m_Slot = Slot;
		}
		CGameWorld World;
		World.CopyWorldIsolated(&Source);
		World.m_WorldConfig.m_PredictTiles = true;
		World.m_WorldConfig.m_PredictFreeze = 1;
		World.m_WorldConfig.m_PredictWeapons = true;
		World.m_WorldConfig.m_PredictDDRace = true;
		auto *pLocal = World.GetCharacterById(ClientId);
		m_vCachedTargets.clear();
		m_HookTarget = m_WeaponTarget = -1;
		if(!pLocal)
			return Decision;
		const int Weapon = pLocal->GetActiveWeapon();
		const bool Fng = World.m_WorldConfig.m_IsFNG;
		const auto *pTuning = pLocal->GetTuning(pLocal->GetOverriddenTuneZone());
		float Range = pTuning->m_HookLength + 56.0f;
		const bool WeaponEnabled = Weapon >= WEAPON_HAMMER && Weapon <= WEAPON_LASER && Settings.m_aWeapons[Weapon].m_Enabled;
		if(WeaponEnabled)
			Range = Weapon == WEAPON_HAMMER ? pTuning->m_HookLength : pTuning->m_LaserReach + 30.0f;
		for(CEntity *pEntity = World.FindFirst(CGameWorld::ENTTYPE_CHARACTER); pEntity;)
		{
			CEntity *pNext = pEntity->TypeNext();
			auto *pCharacter = static_cast<CCharacter *>(pEntity);
			const int Id = pCharacter->GetCid();
			if(Id != ClientId && !pCharacter->m_IsLocal &&
				((pGameClient && ((!pGameClient->m_Snap.m_aCharacters[Id].m_Active && pCharacter->m_SnapTicks > 10) || pGameClient->IsOtherTeam(Id))) || distance(pCharacter->m_Pos, pLocal->m_Pos) > Range))
				delete pCharacter;
			pEntity = pNext;
		}
		for(CEntity *pEntity = World.FindFirst(CGameWorld::ENTTYPE_CHARACTER); pEntity; pEntity = pEntity->TypeNext())
		{
			auto *pTarget = static_cast<CCharacter *>(pEntity);
			const int Id = pTarget->GetCid();
			if(Id == ClientId || !Active(pGameClient, Id))
				continue;
			if(pGameClient && (pGameClient->m_aClients[Id].m_Spec || pGameClient->m_aClients[ClientId].m_Spec))
				continue;
			if(!(Fng && !TeamPlay) && (!pTarget->CanCollide(ClientId) || pLocal->Core()->m_HookHitDisabled || pTarget->Core()->m_Solo || pLocal->Core()->m_Solo))
				continue;
			const bool Friend = pFriends ? (*pFriends)[Id] : pGameClient && pGameClient->m_aClients[Id].m_Friend;
			const bool TargetFrozen = pGameClient ? IsKrxCharacterFrozen(pGameClient, Id) : Frozen(pTarget);
			// The carrier really stores the local position, not target position.
			// Consequently equal scores retain the first eligible entity order.
			m_vCachedTargets.push_back({Id, TargetFrozen, pLocal->m_Pos, pTarget->m_Pos - pLocal->m_Pos, Friend});
		}
		m_CachedLocalPosition = pLocal->m_Pos;
		m_CachedHookLength = pTuning->m_HookLength;
		m_CachedSettings = Settings;
		auto Select = [&](const CAimbotWeaponSettings &Setting, bool Hook) {
			int Best = -1;
			float Score = std::numeric_limits<float>::max();
			for(const auto &Target : m_vCachedTargets)
			{
				if((Setting.m_IgnoreFriends && Target.m_Friend) || !InSelectionFov(Target.m_Difference, Mouse, Setting.m_Fov))
					continue;
				const vec2 Reference = Setting.m_Priority == 0 ? Mouse + pLocal->m_Pos : Setting.m_Priority == 1 ? pLocal->m_Pos :
																   vec2(0, 0);
				const float Distance = distance(Reference, Target.m_LocalPosition);
				const float Max = Hook ? pTuning->m_HookLength + 30.0f : Weapon == WEAPON_HAMMER ? (float)pTuning->m_HookLength :
														   pTuning->m_LaserReach + 30.0f;
				if(Distance > Max || Distance >= Score)
					continue;
				if(!Hook && Fng && ((TeamPlay && SameGameTeam(pGameClient, World, ClientId, Target.m_ClientId)) || Target.m_Frozen))
					continue;
				Best = Target.m_ClientId;
				Score = Distance;
			}
			return Best;
		};
		m_HookTarget = Select(Settings.m_Hook, true);
		if(WeaponEnabled)
			m_WeaponTarget = Select(Settings.m_aWeapons[Weapon], false);
		Decision.m_HookTarget = m_HookTarget;
		Decision.m_WeaponTarget = m_WeaponTarget;
		// Original private world keeps only local, selected hook and selected weapon targets.
		if(m_HookTarget >= 0 || m_WeaponTarget >= 0)
			for(CEntity *pEntity = World.FindFirst(CGameWorld::ENTTYPE_CHARACTER); pEntity;)
			{
				CEntity *pNext = pEntity->TypeNext();
				const int Id = static_cast<CCharacter *>(pEntity)->GetCid();
				if(Id != ClientId && Id != m_HookTarget && Id != m_WeaponTarget && !static_cast<CCharacter *>(pEntity)->m_IsLocal)
					delete pEntity;
				pEntity = pNext;
			}
		const auto *pAuth = pGameClient ? pGameClient->m_GameWorld.GetCharacterById(ClientId) : pLocal;
		if(!Settings.m_Enabled || !(Input.m_PlayerFlags & PLAYERFLAG_PLAYING) || !pAuth ||
			(pGameClient ? IsKrxCharacterFrozen(pGameClient, ClientId) || pGameClient->m_Snap.m_SpecInfo.m_Active : Frozen(pAuth)))
			return Decision;
		auto ApplyAim = [&](vec2 Aim, bool Silent) {
			// A hook injected by other KRX automation owns the aim this tick.
			// Recovered order ran the aimbot before Avoid, so an automatic hook
			// always kept its own direction afterwards.
			if(Settings.m_HookIsAutomated && !m_AutoHookIssued)
				return;
			Decision.m_Input = WithAim(Decision.m_Input, Aim);
			if(!Silent)
			{
				Decision.m_ChangeVisibleAim = true;
				Decision.m_VisibleAim = Aim;
			}
		};
		if(!Settings.m_AutoHook && m_AutoHookIssued)
		{
			Decision.m_Input.m_Hook = 0;
			m_AutoHookIssued = false;
		}
		if(Settings.m_Hook.m_Enabled && !Settings.m_HookIsAutomated && (Decision.m_Input.m_Hook || Settings.m_AutoHook) && m_HookTarget >= 0)
		{
			const auto Aim = HookAim(World, ClientId, m_HookTarget, Decision.m_Input, Mouse, Settings, Radius);
			if(Aim && *Aim != vec2(0, 0))
			{
				m_HookAim = *Aim;
				if(Decision.m_Input.m_Hook == 1)
				{
					if(!m_AutoHookIssued && !Settings.m_AutoHook)
						ApplyAim(*Aim, Settings.m_Hook.m_Silent);
				}
				else
				{
					ApplyAim(*Aim, Settings.m_Hook.m_Silent);
					Decision.m_Input.m_Hook = 1;
					m_AutoHookIssued = true;
				}
			}
		}
		// 2bab10 runs before the fire/target gates and uses authoritative tick modulo.
		if(m_FireLength)
		{
			const int Tick = pGameClient ? pGameClient->m_GameWorld.GameTick() : Source.GameTick();
			if(Tick % m_FireLength == 0 && (Decision.m_Input.m_Fire & 1))
			{
				++Decision.m_Input.m_Fire;
				m_FireLength = 0;
			}
			else
				return Decision;
		}
		if(!(Decision.m_Input.m_Fire & 1) && !Settings.m_AutoShoot)
			return Decision;
		if(pAuth->DiagnosticReloadTimer() > 3 || m_WeaponTarget < 0 || RandomInt(1, 100) < 100 - Settings.m_ShotPercentage)
			return Decision;
		const auto Aim = WeaponAim(World, ClientId, m_WeaponTarget, Decision.m_Input, Mouse, Settings, Radius, TeamPlay, pGameClient);
		if(!Aim || *Aim == vec2(0, 0))
			return Decision;
		if(!(Decision.m_Input.m_Fire & 1) && pAuth->DiagnosticReloadTimer() <= 0)
		{
			ApplyAim(QuantizedAim(*Aim, Radius), Settings.m_aWeapons[Weapon].m_Silent);
			++Decision.m_Input.m_Fire;
			m_FireCounter = Decision.m_Input.m_Fire;
			int Min = Fng ? 3 : 1, Max = Fng ? 14 : 5;
			if(pGameClient && (str_find(pGameClient->Client()->ServerInfo().m_aName, "|*KoG*|") || str_find(pGameClient->Client()->ServerInfo().m_aGameType, "Gores")))
			{
				Min = 1;
				Max = 12;
			}
			if(Settings.m_Advanced)
			{
				Min = Settings.m_MinShootLength;
				Max = Settings.m_MaxShootLength;
			}
			m_FireLength = RandomInt(std::max(1, std::min(Min, Max)), std::max(1, std::max(Min, Max)));
		}
		if(!Settings.m_AutoShoot)
			ApplyAim(*Aim, Settings.m_aWeapons[Weapon].m_Silent);
		return Decision;
	}
	std::optional<vec2> CAimbot::FindDragAim(const CGameWorld &Source, int ClientId, CGameClient *pGameClient, vec2 Mouse) const
	{
		if(m_ClientId != ClientId || m_vCachedTargets.empty())
			return std::nullopt;
		int TargetId = -1;
		float Best = std::numeric_limits<float>::max();
		for(const auto &Target : m_vCachedTargets)
		{
			if((m_CachedSettings.m_Hook.m_IgnoreFriends && Target.m_Friend) || !InSelectionFov(Target.m_Difference, Mouse, 315))
				continue;
			const float Score = distance(Target.m_LocalPosition, m_CachedLocalPosition);
			if(Score <= m_CachedHookLength + 30.0f && Score < Best)
			{
				TargetId = Target.m_ClientId;
				Best = Score;
			}
		}
		if(TargetId < 0)
			return std::nullopt;
		CNetObj_PlayerInput Input{};
		if(pGameClient)
			Input = pGameClient->m_Controls.m_aInputData[g_Config.m_ClDummy];
		else if(auto *pLocal = const_cast<CGameWorld &>(Source).GetCharacterById(ClientId))
			Input = pLocal->DiagnosticLatestInput();
		const float Radius = pGameClient ? pGameClient->m_Controls.GetMaxMouseDistance() : 400.0f;
		return HookAim(Source, ClientId, TargetId, Input, Mouse, m_CachedSettings, Radius);
	}
} // namespace OpenKrx
