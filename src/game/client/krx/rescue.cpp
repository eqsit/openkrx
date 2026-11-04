#include "rescue.h"

#include "bots.h"
#include "aimbot.h"
#include "misc.h"

#include <engine/shared/config.h>
#include <engine/graphics.h>

#include <game/client/avoid/rocket_save.h>
#include <game/client/gameclient.h>
#include <game/client/animstate.h>
#include <game/client/render.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/laser.h>
#include <game/client/prediction/gameworld.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace OpenKrx
{
	namespace
	{
		void Tick(CGameWorld &World, int ClientId, const CNetObj_PlayerInput &Input)
		{
			if(CCharacter *pLocal = World.GetCharacterById(ClientId))
			{
				pLocal->OnDirectInput(&Input);
				++World.m_GameTick;
				pLocal->OnPredictedInput(&Input);
				World.Tick();
			}
		}

		bool Frozen(const CCharacter *pCharacter)
		{
			return !pCharacter || pCharacter->m_FreezeTime > 0 || pCharacter->Core()->m_DeepFrozen || pCharacter->Core()->m_IsInFreeze;
		}

		void RemoveLasers(CGameWorld &World)
		{
			for(CEntity *pEntity = World.FindFirst(CGameWorld::ENTTYPE_LASER); pEntity;)
			{
				CEntity *pNext = pEntity->TypeNext();
				delete pEntity;
				pEntity = pNext;
			}
		}

		void FilterPlayers(CGameWorld &World, int ClientId, float Radius, CGameClient *pGameClient)
		{
			const CCharacter *pLocal = World.GetCharacterById(ClientId);
			if(!pLocal)
				return;
			for(int Id = 0; Id < MAX_CLIENTS; ++Id)
			{
				CCharacter *pCharacter = World.GetCharacterById(Id);
				if(!pCharacter || Id == ClientId || pCharacter->m_IsLocal)
					continue;
				if(distance(pLocal->m_Pos, pCharacter->m_Pos) > Radius ||
					(pGameClient && ((!pGameClient->m_Snap.m_aCharacters[Id].m_Active && pCharacter->m_SnapTicks > 10) || pGameClient->IsOtherTeam(Id))))
					delete pCharacter;
			}
		}

		bool Playing(CGameClient *pGameClient, const CNetObj_PlayerInput &Input, int ClientId)
		{
			return ClientId >= 0 && ClientId < MAX_CLIENTS &&
			       pGameClient->Client()->State() == IClient::STATE_ONLINE &&
			       (Input.m_PlayerFlags & PLAYERFLAG_PLAYING) &&
			       !pGameClient->m_Snap.m_SpecInfo.m_Active;
		}

		CNetObj_PlayerInput MouseInput(CGameClient *pGameClient, CNetObj_PlayerInput Input)
		{
			Input.m_TargetX = (int)pGameClient->m_Controls.m_aMousePos[g_Config.m_ClDummy].x;
			Input.m_TargetY = (int)pGameClient->m_Controls.m_aMousePos[g_Config.m_ClDummy].y;
			return Input;
		}

		void Aim(CGameClient *pGameClient, CNetObj_PlayerInput &Input, vec2 Direction, bool Silent)
		{
			Input.m_TargetX = (int)Direction.x;
			Input.m_TargetY = (int)Direction.y;
			if(!Silent)
				pGameClient->m_Controls.m_aMousePos[g_Config.m_ClDummy] = Direction;
		}

		int Life(const CGameWorld &Source, int ClientId, const CNetObj_PlayerInput &Input, int Ticks)
		{
			CGameWorld World;
			World.CopyWorldIsolated(&Source);
			World.m_WorldConfig.m_PredictWeapons = true;
			World.m_WorldConfig.m_KrxPrediction = true;
			if(!World.GetCharacterById(ClientId))
				return -1;
			for(int Step = 0; Step <= Ticks; ++Step)
			{
				Tick(World, ClientId, Input);
				if(Frozen(World.GetCharacterById(ClientId)))
					return Step;
			}
			return 9999;
		}

		bool HasFreezeBetween(CCollision *pCollision, vec2 From, vec2 To)
		{
			const int End = (int)(distance(From, To) + 1.0f);
			for(int Step = 0; Step <= End; ++Step)
			{
				const vec2 Position = mix(From, To, End ? Step / (float)End : 0.0f);
				const vec2 Rounded(round_to_int(Position.x), round_to_int(Position.y));
				if(pCollision->GetTileIndex(pCollision->GetPureMapIndex(Rounded)) == TILE_FREEZE)
					return true;
			}
			return false;
		}

		// MSVC's bounded introsort is significant for equal-scoring angles.
		// std::sort under GCC permutes these ties differently.
		template<typename T, typename Better>
		void Median3(T &First, T &Middle, T &Last, Better Before)
		{
			if(Before(Middle, First))
				std::swap(Middle, First);
			if(Before(Last, Middle))
			{
				std::swap(Last, Middle);
				if(Before(Middle, First))
					std::swap(Middle, First);
			}
		}

		template<typename It, typename Better>
		std::pair<It, It> Partition(It First, It Last, Better Before)
		{
			const auto Equal = [&](const auto &A, const auto &B) { return !Before(A, B) && !Before(B, A); };
			It Middle = First + (Last - First) / 2, Final = Last - 1;
			if(Last - First > 41)
			{
				const auto Step = (Last - First) / 8;
				Median3(*First, *(First + Step), *(First + 2 * Step), Before);
				Median3(*(Middle - Step), *Middle, *(Middle + Step), Before);
				Median3(*(Final - 2 * Step), *(Final - Step), *Final, Before);
				Median3(*(First + Step), *Middle, *(Final - Step), Before);
			}
			else
				Median3(*First, *Middle, *Final, Before);
			It PivotFirst = Middle, PivotLast = Middle + 1;
			while(First < PivotFirst && Equal(*(PivotFirst - 1), *PivotFirst))
				--PivotFirst;
			while(PivotLast < Last && Equal(*PivotLast, *PivotFirst))
				++PivotLast;
			It GreaterFirst = PivotLast, GreaterLast = PivotFirst;
			for(;;)
			{
				for(; GreaterFirst < Last; ++GreaterFirst)
				{
					if(Before(*PivotFirst, *GreaterFirst))
						continue;
					if(Before(*GreaterFirst, *PivotFirst))
						break;
					if(PivotLast != GreaterFirst)
						std::iter_swap(PivotLast, GreaterFirst);
					++PivotLast;
				}
				for(; First < GreaterLast; --GreaterLast)
				{
					const It Previous = GreaterLast - 1;
					if(Before(*Previous, *PivotFirst))
						continue;
					if(Before(*PivotFirst, *Previous))
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

		template<typename It, typename Better>
		void ReferenceSort(It First, It Last, Better Before)
		{
			while(Last - First > 32)
			{
				const auto [PivotFirst, PivotLast] = Partition(First, Last, Before);
				if(PivotFirst - First < Last - PivotLast)
				{
					ReferenceSort(First, PivotFirst, Before);
					First = PivotLast;
				}
				else
				{
					ReferenceSort(PivotLast, Last, Before);
					Last = PivotFirst;
				}
			}
			if(First == Last)
				return;
			for(It Current = First + 1; Current != Last; ++Current)
			{
				auto Value = *Current;
				It Hole = Current;
				while(Hole != First && Before(Value, *(Hole - 1)))
				{
					*Hole = *(Hole - 1);
					--Hole;
				}
				*Hole = Value;
			}
		}
	}

	void CRescueBots::OnReset()
	{
		// Pulse counters are process statics; aim/path are component fields.
		// The native base reset does not clear either set of retained state.
	}

	CDummyFlyDecision CRescueBots::DummyFlyDecision(vec2 LocalPosition, vec2 DummyPosition,
		int DummyHookedPlayer, float MouseRadius, CNetObj_PlayerInput DummyInput)
	{
		CDummyFlyDecision Result;
		Result.m_Input = DummyInput;
		// KRX quantizes the displacement before normalizing it to mouse max.
		const vec2 Direction((int)(LocalPosition.x - DummyPosition.x), (int)(LocalPosition.y - DummyPosition.y));
		const vec2 Aim = CMiscFeatures::QuantizedAim(Direction, MouseRadius);
		Result.m_Input.m_TargetX = (int)Aim.x;
		Result.m_Input.m_TargetY = (int)Aim.y;
		const float Distance = distance(LocalPosition, DummyPosition);
		if(LocalPosition.y > DummyPosition.y && Distance > 16.0f)
		{
			Result.m_Input.m_Hook = 1;
			Result.m_Hammer = 0;
		}
		else
			Result.m_Input.m_Hook = 0;
		if(DummyPosition.y >= LocalPosition.y + 5.0f && Distance > 30.0f)
			Result.m_Hammer = 1;
		if(DummyHookedPlayer == -1 || Distance < 48.0f)
			Result.m_Input.m_Hook = 0;
		return Result;
	}

	void CRescueBots::AdjustDummyFly(CGameClient *pGameClient)
	{
		if(!g_Config.m_KrxDummyFly)
			return;
		const int Slot = g_Config.m_ClDummy;
		const int LocalId = pGameClient->m_aLocalIds[Slot];
		const int DummyId = pGameClient->m_aLocalIds[!Slot];
		CGameWorld Source;
		Source.CopyWorldIsolated(&pGameClient->KrxPredictedWorld());
		Source.m_WorldConfig.m_InfiniteAmmo = true;
		Source.m_WorldConfig.m_KrxPrediction = true;
		pGameClient->m_DummyInput = DummyFlyWorld(Source, LocalId, DummyId, pGameClient->m_DummyInput,
			pGameClient->m_Controls.m_aInputData[!Slot], pGameClient->m_Controls.m_aInputData[Slot].m_Direction,
			g_Config.m_KrxDummyFlyHook != 0, g_Config.m_KrxDummyFlyKinta != 0,
			pGameClient->m_Controls.GetMaxMouseDistance(), pGameClient);
	}

	namespace
	{
		void TickDummyPair(CGameWorld &World, int DummyId, int LocalId, CNetObj_PlayerInput DummyInput, CNetObj_PlayerInput LocalInput)
		{
			auto *pDummy = World.GetCharacterById(DummyId);
			auto *pLocal = World.GetCharacterById(LocalId);
			if(pDummy)
				pDummy->OnDirectInput(&DummyInput);
			if(pLocal)
				pLocal->OnDirectInput(&LocalInput);
			++World.m_GameTick;
			if(pDummy)
				pDummy->OnPredictedInput(&DummyInput);
			if(pLocal)
				pLocal->OnPredictedInput(&LocalInput);
			World.Tick();
			World.m_PredictedEvents.clear();
		}

		float ProbeDummyHammer(const CGameWorld &Source, int DummyId, int LocalId, CNetObj_PlayerInput Input)
		{
			CGameWorld World;
			World.CopyWorldIsolated(&Source);
			auto *pDummy = World.GetCharacterById(DummyId);
			auto *pLocal = World.GetCharacterById(LocalId);
			if(!pDummy || !pLocal)
				return -1;
			const vec2 Before = pLocal->Core()->m_Vel;
			Input.m_Fire = (pDummy->Core()->m_Input.m_Fire + 1) | 1;
			Input.m_WantedWeapon = WEAPON_HAMMER + 1;
			TickDummyPair(World, DummyId, LocalId, Input, pLocal->Core()->m_Input);
			pDummy = World.GetCharacterById(DummyId);
			pLocal = World.GetCharacterById(LocalId);
			if(!pDummy || !pLocal || pDummy->GetLastHammerHitClientId() != LocalId)
				return -1;
			const float Delta = length(pLocal->Core()->m_Vel - Before);
			const vec2 Begin = pLocal->m_Pos;
			for(int Tick = 0; Tick < 15; ++Tick)
			{
				TickDummyPair(World, DummyId, LocalId, pDummy->Core()->m_Input, pLocal->Core()->m_Input);
				pDummy = World.GetCharacterById(DummyId);
				pLocal = World.GetCharacterById(LocalId);
				if(!pLocal)
					return -1;
				if(pLocal->m_FreezeTime > 0 || pLocal->Core()->m_IsInFreeze)
					break;
				if(!pDummy)
					break;
			}
			return Delta + distance(Begin, pLocal->m_Pos) * 0.2f;
		}

		bool DummySolidRay(const CCollision *pCollision, vec2 From, vec2 To)
		{
			const int Steps = (int)(distance(From, To) + 1.0f);
			for(int Step = 0; Step <= Steps; ++Step)
			{
				const vec2 Point = mix(From, To, (float)Step / Steps);
				const int Tile = pCollision->GetTileIndex(pCollision->GetPureMapIndex(vec2(round_to_int(Point.x), round_to_int(Point.y))));
				if(Tile == TILE_SOLID || Tile == TILE_NOHOOK)
					return true;
			}
			return false;
		}
	}

	std::optional<CNetObj_PlayerInput> CRescueBots::SearchDummyHammerWorld(const CGameWorld &Source, int DummyId, int LocalId, float MouseRadius, int MaxDelay)
	{
		std::vector<float> vScores(MaxDelay + 1, 0);
		std::vector<bool> vValid(MaxDelay + 1, false);
		std::optional<CNetObj_PlayerInput> BestNow;
		for(int Delay = 0; Delay <= MaxDelay; ++Delay)
		{
			CGameWorld World;
			World.CopyWorldIsolated(&Source);
			for(int Tick = 0; Tick < Delay; ++Tick)
			{
				auto *pDummy = World.GetCharacterById(DummyId);
				auto *pLocal = World.GetCharacterById(LocalId);
				if(!pDummy || !pLocal)
					break;
				TickDummyPair(World, DummyId, LocalId, pDummy->Core()->m_Input, pLocal->Core()->m_Input);
			}
			auto *pDummy = World.GetCharacterById(DummyId);
			auto *pLocal = World.GetCharacterById(LocalId);
			if(!pDummy || !pLocal || pDummy->GetActiveWeapon() != WEAPON_HAMMER || pDummy->DiagnosticReloadTimer() > 0)
				continue;
			vValid[Delay] = true;
			const float Center = angle(pLocal->m_Pos - pDummy->m_Pos);
			float Best = -1;
			for(int Offset = -2; Offset <= 2; ++Offset)
			{
				auto Input = pDummy->Core()->m_Input;
				const float Angle = (float)Offset * 10.0f * (pi / 180.0f) + Center;
				const vec2 Aim = CMiscFeatures::QuantizedAim(direction(Angle), MouseRadius);
				Input.m_TargetX = (int)Aim.x;
				Input.m_TargetY = (int)Aim.y;
				Input.m_WantedWeapon = WEAPON_HAMMER + 1;
				const float Score = ProbeDummyHammer(World, DummyId, LocalId, Input);
				if(Score > Best)
				{
					Best = Score;
					if(Delay == 0)
						BestNow = Input;
				}
			}
			vScores[Delay] = std::max(0.0f, Best);
		}
		if(!vValid[0] || vScores[0] <= 10.0f)
			return std::nullopt;
		for(int Delay = 1; Delay <= MaxDelay; ++Delay)
			if(vValid[Delay] && vScores[Delay] > vScores[0] + 5.0f)
				return std::nullopt;
		return BestNow;
	}

	CNetObj_PlayerInput CRescueBots::DummyFlyWorld(const CGameWorld &Source, int LocalId, int DummyId, CNetObj_PlayerInput Input, CNetObj_PlayerInput OtherLiveInput, int ActiveDirection, bool AllowHook, bool Kinta, float MouseRadius, CGameClient *pGameClient)
	{
		auto *pLocal = const_cast<CGameWorld &>(Source).GetCharacterById(LocalId);
		auto *pDummy = const_cast<CGameWorld &>(Source).GetCharacterById(DummyId);
		const auto Release = [&] { Input.m_Fire = (OtherLiveInput.m_Fire + 1) & ~1; };
		if(!pLocal || !pDummy)
		{
			Input.m_Direction = Input.m_Hook = Input.m_WantedWeapon = 0;
			Release();
			return Input;
		}
		const vec2 Aim = CMiscFeatures::QuantizedAim(pLocal->m_Pos - pDummy->m_Pos, MouseRadius);
		Input.m_TargetX = (int)Aim.x;
		Input.m_TargetY = (int)Aim.y;
		const float Distance = distance(pLocal->m_Pos, pDummy->m_Pos);
		const float HookLength = pGameClient ? (float)pGameClient->m_aTuning[g_Config.m_ClDummy].m_HookLength : (float)pDummy->GetTuning(pDummy->GetOverriddenTuneZone())->m_HookLength;
		if(Distance > HookLength * 2.5f)
		{
			Input.m_Direction = Input.m_Hook = Input.m_WantedWeapon = 0;
			Release();
			return Input;
		}
		if(pDummy->m_FreezeTime > 0 || pDummy->Core()->m_IsInFreeze || pDummy->Core()->m_DeepFrozen)
		{
			Input.m_Direction = Input.m_Hook = 0;
			Input.m_WantedWeapon = 1;
			Input.m_Fire = (OtherLiveInput.m_Fire + 1) | 1;
			return Input;
		}
		bool HookAimFound = false;
		const int HookState = pDummy->Core()->m_HookState;
		if(AllowHook)
		{
			if(pDummy->m_Pos.y < pLocal->m_Pos.y - 14.0f && Distance > 42.0f)
			{
				if(Distance <= HookLength * 1.1f && Distance > 48.0f && HookState == HOOK_IDLE)
				{
					CAimbot Aimbot;
					CAimbotSettings Settings;
					Settings.m_Enabled = Settings.m_AutoHook = true;
					Settings.m_Hook = {true, 315, 1, true, false};
					Settings.m_Advanced = g_Config.m_KrxAimbotAdvancedSettings != 0;
					Settings.m_Accuracy = g_Config.m_KrxAimbotAccuracy;
					std::array<bool, MAX_CLIENTS> aFriends{};
					if(pGameClient)
						for(int Id = 0; Id < MAX_CLIENTS; ++Id)
							aFriends[Id] = pGameClient->m_aClients[Id].m_Friend;
					Settings.m_Hook.m_IgnoreFriends = g_Config.m_KrxAimbotIgnoreFriendsHook != 0;
					auto Base = pGameClient ? pGameClient->m_Controls.m_aInputData[g_Config.m_ClDummy] : OtherLiveInput;
					Base.m_PlayerFlags |= PLAYERFLAG_PLAYING;
					const vec2 Mouse = pGameClient ? pGameClient->m_Controls.m_aMousePos[g_Config.m_ClDummy] : vec2(Base.m_TargetX, Base.m_TargetY);
					const auto Decision = Aimbot.SearchWorld(Source, Base, DummyId, Mouse, Settings, MouseRadius, false, &aFriends);
					if(Decision.m_HookTarget >= 0 && Decision.m_Input.m_Hook && (Decision.m_Input.m_TargetX || Decision.m_Input.m_TargetY))
					{
						Input.m_TargetX = Decision.m_Input.m_TargetX;
						Input.m_TargetY = Decision.m_Input.m_TargetY;
						Input.m_Hook = 1;
						HookAimFound = true;
					}
				}
				if(!HookAimFound && HookState == HOOK_GRABBED && pDummy->Core()->HookedPlayer() == LocalId)
				{
					Input.m_Hook = 1;
					HookAimFound = true;
				}
				else if(!HookAimFound && (HookState == HOOK_IDLE || HookState == HOOK_RETRACTED))
					Input.m_Hook = 0;
			}
			else
				Input.m_Hook = 0;
		}
		const vec2 Position = pLocal->m_Pos;
		Input.m_Direction = Kinta && pDummy->m_Pos.y >= Position.y - 14 && std::abs(pDummy->m_Pos.x - Position.x) <= 42 &&
			DummySolidRay(Source.Collision(), Position, Position + vec2(0, -96)) && DummySolidRay(Source.Collision(), Position, Position + vec2(-96, -96)) && DummySolidRay(Source.Collision(), Position, Position + vec2(96, -96)) ? ActiveDirection : 0;
		if(HookAimFound && Input.m_Hook == 1)
		{
			Input.m_WantedWeapon = 0;
			Release();
			return Input;
		}
		if(HookState == HOOK_IDLE || HookState == HOOK_RETRACTED)
			Input.m_Hook = 0;
		Input.m_WantedWeapon = 1;
		if(const auto Hammer = SearchDummyHammerWorld(Source, DummyId, LocalId, MouseRadius))
		{
			Input.m_TargetX = Hammer->m_TargetX;
			Input.m_TargetY = Hammer->m_TargetY;
			Input.m_Fire = (OtherLiveInput.m_Fire + 1) | 1;
		}
		else
		{
			Input.m_TargetX = (int)Aim.x;
			Input.m_TargetY = (int)Aim.y;
			Release();
		}
		return Input;
	}

	bool CRescueBots::CPulse::Release(CNetObj_PlayerInput &Input, int GameTick)
	{
		if(m_Modulo == 0)
			return true;
		if(GameTick % m_Modulo == 0 && (Input.m_Fire & 1))
		{
			++Input.m_Fire;
			m_Modulo = 0;
			return true;
		}
		return false;
	}

	void CRescueBots::CPulse::Press(CNetObj_PlayerInput &Input)
	{
		++Input.m_Fire;
		static std::mt19937 s_Generator(std::random_device{}());
		m_Modulo = std::uniform_int_distribution<int>(1, 5)(s_Generator);
	}

	CRescueBots::CBeamResult CRescueBots::ProbeBeam(const CGameWorld &Source, int ClientId,
		CNetObj_PlayerInput Input, int Ticks, bool RequireUnfreeze, CGameClient *pGameClient, bool FullTrajectory)
	{
		CBeamResult Result;
		CGameWorld World;
		World.CopyWorldIsolated(&Source);
		World.m_WorldConfig.m_PredictWeapons = true;
		World.m_WorldConfig.m_KrxPrediction = true;
		World.m_WorldConfig.m_InfiniteAmmo = true;
		CCharacter *pLocal = World.GetCharacterById(ClientId);
		if(!pLocal)
			return Result;
		if(!RequireUnfreeze)
		{
			World.m_WorldConfig.m_PredictTiles = true;
			World.m_WorldConfig.m_PredictFreeze = 1;
			World.m_WorldConfig.m_PredictDDRace = true;
		}
		if(RequireUnfreeze)
			FilterPlayers(World, ClientId, (float)World.TuningList()[0].m_LaserReach + 30.0f, pGameClient);
		RemoveLasers(World);
		Input.m_Fire = 1; // Native b6040/b71a0 replace Fire; they do not add2.
		int LastBounce = -1;
		for(int Step = 0; Step <= Ticks; ++Step)
		{
			Tick(World, ClientId, Input);
			pLocal = World.GetCharacterById(ClientId);
			if(!pLocal)
				return Result;
			for(CEntity *pEntity = World.FindFirst(CGameWorld::ENTTYPE_LASER); pEntity; pEntity = pEntity->TypeNext())
			{
				const auto *pLaser = static_cast<CLaser *>(pEntity);
				if(pLaser->GetOwner() != ClientId)
					continue;
				Result.m_vPath.push_back(pLaser->m_Pos);
				if(pLaser->GetEnergy() < 0.0f)
					Input.m_Fire = 0;
				if(pLaser->GetHitClientId() == ClientId)
				{
					LastBounce = pLaser->GetBounces();
					if(!RequireUnfreeze)
					{
						Result.m_Found = true;
						Result.m_Bounces = LastBounce;
						Result.m_Speed = length(pLocal->Core()->m_Vel);
						return Result;
					}
				}
			}
			// b6040 retains a recorded self-hit while waiting for freeze to clear.
			Result.m_vTeePath.push_back(pLocal->m_Pos);
			if(RequireUnfreeze && LastBounce >= 0 && pLocal->m_FreezeTime < 1 && !pLocal->Core()->m_DeepFrozen)
			{
				Result.m_Found = true;
				Result.m_Bounces = LastBounce;
				Result.m_Speed = length(pLocal->Core()->m_Vel);
				if(!FullTrajectory)
					return Result;
			}
		}
		// 369e20 records all Ticks+1 steps, and publishes no laser path if
		// no recorded self hit ever leaves FreezeTime/DeepFrozen cleared.
		if(FullTrajectory && !Result.m_Found)
			Result.m_vPath.clear();
		return Result;
	}

	CRescueBots::CBeamResult CRescueBots::SearchShotgunWorld(const CGameWorld &Source, int ClientId,
		CNetObj_PlayerInput Input, vec2 Mouse, int Ticks, int Points, float Fov, bool MostBounces,
		bool HighestVelocity, float MouseRadius)
	{
		const CCharacter *pLocal = const_cast<CGameWorld &>(Source).GetCharacterById(ClientId);
		if(!pLocal || Points <= 0)
			return {};
		struct CScoredAim
		{
			vec2 m_Aim;
			float m_Speed;
			int m_Bounces;
		};
		std::vector<CScoredAim> vCandidates;
		const float Span = Fov < 315.0f ? Fov * pi / 180.0f : 2.0f * pi;
		const float Center = angle(Mouse);
		// New 324bb0 evaluates exactly Points directions, including both FOV
		// endpoints. The older sector/wall prefilter is no longer present.
		for(int Index = 0; Index < Points; ++Index)
		{
			const float Radians = Index * (Span / std::max(1, Points - 1)) + (Center - Span * 0.5f);
			const vec2 Direction = CMiscFeatures::QuantizedAim(direction(Radians), MouseRadius);
			CNetObj_PlayerInput Candidate = Input;
			Candidate.m_TargetX = (int)Direction.x;
			Candidate.m_TargetY = (int)Direction.y;
			Candidate.m_Fire = 0;
			const auto Probe = ProbeBeam(Source, ClientId, Candidate, Ticks, false);
			if(Probe.m_Found && Probe.m_Speed > 0)
				vCandidates.push_back({Direction, Probe.m_Speed, Probe.m_Bounces});
		}
		if(vCandidates.empty())
			return {};
		const auto Score = [&](const CScoredAim &Candidate) {
			return (HighestVelocity ? Candidate.m_Speed : -Candidate.m_Speed) -
			       (MostBounces ? -(float)Candidate.m_Bounces : (float)Candidate.m_Bounces);
		};
		ReferenceSort(vCandidates.begin(), vCandidates.end(), [&](const CScoredAim &A, const CScoredAim &B) { return Score(A) > Score(B); });
		CBeamResult Result;
		Result.m_Found = true;
		Result.m_Aim = vCandidates.front().m_Aim;
		Result.m_Bounces = vCandidates.front().m_Bounces;
		Result.m_Speed = vCandidates.front().m_Speed;
		return Result;
	}

	CRescueBots::CBeamResult CRescueBots::SearchUnfreezeWorld(const CGameWorld &Source, int ClientId,
		CNetObj_PlayerInput Input, vec2 Mouse, int Ticks, int Points, float Fov, bool MostBounces,
		float MouseRadius, CGameClient *pGameClient)
	{
		const CCharacter *pLocal = const_cast<CGameWorld &>(Source).GetCharacterById(ClientId);
		if(!pLocal || Points <= 0 || Fov <= 0)
			return {};
		struct CScoredAim
		{
			vec2 m_Aim;
			int m_Bounces;
		};
		std::vector<CScoredAim> vCandidates;
		const float Span = Fov < 315.0f ? Fov * pi / 180.0f : 2.0f * pi;
		const float Sector = Span / Points;
		const float Center = angle(Mouse);
		for(float SectorStart = Center - Span * 0.5f; SectorStart < Center + Span * 0.5f;)
		{
			// Native cb8f0 retains only the first wall direction of each sector.
			for(int Index = 0; Index < Points; ++Index)
			{
				const vec2 Direction = CMiscFeatures::QuantizedAim(direction(SectorStart + Index * (Sector / Points)), MouseRadius);
				if(!Source.Collision()->IntersectLine(pLocal->m_Pos, pLocal->m_Pos + Direction, nullptr, nullptr))
					continue;
				CNetObj_PlayerInput Candidate = Input;
				Candidate.m_TargetX = (int)Direction.x;
				Candidate.m_TargetY = (int)Direction.y;
				const auto Probe = ProbeBeam(Source, ClientId, Candidate, Ticks, true, pGameClient);
				if(Probe.m_Found)
					vCandidates.push_back({Direction, Probe.m_Bounces});
				break;
			}
			const float Next = SectorStart + Sector;
			if(Next <= SectorStart)
				break;
			SectorStart = Next;
		}
		if(vCandidates.empty())
			return {};
		ReferenceSort(vCandidates.begin(), vCandidates.end(), [&](const CScoredAim &A, const CScoredAim &B) {
			return MostBounces ? A.m_Bounces > B.m_Bounces : A.m_Bounces < B.m_Bounces;
		});
		CBeamResult Result;
		Result.m_Found = true;
		Result.m_Aim = vCandidates.front().m_Aim;
		Result.m_Bounces = vCandidates.front().m_Bounces;
		return Result;
	}

	CNetObj_PlayerInput CRescueBots::AdjustAutoShotgun(CGameClient *pGameClient, CNetObj_PlayerInput Input, int ClientId)
	{
		if(!g_Config.m_KrxAutoShotgun || !(Input.m_Fire & 1) || !Playing(pGameClient, Input, ClientId))
			return Input;
		const CCharacter *pAuthoritative = pGameClient->KrxGameWorld().GetCharacterById(ClientId);
		const CCharacter *pLocal = pGameClient->KrxPredictedWorld().GetCharacterById(ClientId);
		if(!pAuthoritative || !pLocal || !const_cast<CCharacter *>(pAuthoritative)->GetWeaponGot(WEAPON_SHOTGUN) ||
			const_cast<CCharacter *>(pLocal)->GetActiveWeapon() != WEAPON_SHOTGUN)
			return Input;
		const vec2 Mouse = pGameClient->m_Controls.m_aMousePos[g_Config.m_ClDummy];
		const auto Result = SearchShotgunWorld(pGameClient->KrxPredictedWorld(), ClientId, Input, Mouse,
			g_Config.m_KrxAutoShotgunTicks, g_Config.m_KrxAutoShotgunPoints, (float)g_Config.m_KrxAutoShotgunFov,
			g_Config.m_KrxAutoShotgunMostBounces, g_Config.m_KrxAutoShotgunHighestVel, pGameClient->m_Controls.GetMaxMouseDistance());
		if(Result.m_Found)
			Aim(pGameClient, Input, CMiscFeatures::QuantizedAim(Result.m_Aim, pGameClient->m_Controls.GetMaxMouseDistance()), g_Config.m_KrxAutoShotgunSilent != 0);
		return Input;
	}

	CNetObj_PlayerInput CRescueBots::AdjustAutoUnfreeze(CGameClient *pGameClient, CNetObj_PlayerInput Input, int ClientId)
	{
		if(!g_Config.m_KrxUnfreezeBotAdvanced)
		{
			g_Config.m_KrxUnfreezeBotCurDirTicks = 21;
			g_Config.m_KrxUnfreezeBotTicks = 35;
			g_Config.m_KrxUnfreezeBotPoints = 50;
			g_Config.m_KrxUnfreezeBotMostBounces = 0;
			g_Config.m_KrxUnfreezeBotFov = 315;
			g_Config.m_KrxUnfreezeBotSilent = 1;
		}
		const int Slot = g_Config.m_ClDummy;
		// 326690 restores its retained aim while a TAS world is active, until
		// that world's core input confirms the automatic fire press.
		if(pGameClient->m_Tas.Active() && m_LastUnfreezeAim != vec2(0, 0))
		{
			const auto *pLocal = pGameClient->KrxPredictedWorld().GetCharacterById(ClientId);
			if(pLocal && (pLocal->Core()->m_Input.m_Fire & 1))
			{
				m_LastUnfreezeAim = vec2(0, 0);
				m_UnfreezeAimReleased = true;
			}
			if(!m_UnfreezeAimReleased)
				Aim(pGameClient, Input, m_LastUnfreezeAim, g_Config.m_KrxUnfreezeBotSilent != 0);
		}
		if(!m_UnfreezePulse.Release(Input, pGameClient->KrxGameWorld().GameTick()))
			return Input;
		if(!g_Config.m_KrxUnfreezeBot || (Input.m_Fire & 1) || !Playing(pGameClient, Input, ClientId))
		{
			m_LastUnfreezeAim = vec2(0, 0);
			m_UnfreezeAimReleased = true;
			return Input;
		}
		CCharacter *pAuthoritative = pGameClient->KrxGameWorld().GetCharacterById(ClientId);
		if(!pAuthoritative || pAuthoritative->DiagnosticReloadTimer() > 0 ||
			IsKrxCharacterFrozen(pGameClient, ClientId) || !pAuthoritative->GetWeaponGot(WEAPON_LASER))
			return Input;
		const CGameWorld &Source = pGameClient->KrxPredictedWorld();
		if(!const_cast<CGameWorld &>(Source).GetCharacterById(ClientId))
			return Input;
		CNetObj_PlayerInput ProbeInput = MouseInput(pGameClient, Input);
		if(Life(Source, ClientId, ProbeInput, 3) > 2)
			return Input;
		// cc190 prepares the weapon globally but probes its already-copied input.
		Input.m_WantedWeapon = WEAPON_LASER + 1;
		CGameWorld Advanced;
		Advanced.CopyWorldIsolated(&Source);
		Advanced.m_WorldConfig.m_PredictWeapons = true;
		Advanced.m_WorldConfig.m_KrxPrediction = true;
		RemoveLasers(Advanced);
		for(int Id = 0; Id < MAX_CLIENTS; ++Id)
			if(CCharacter *pCharacter = Advanced.GetCharacterById(Id); pCharacter && Id != ClientId && !pCharacter->m_IsLocal)
				delete pCharacter;
		Tick(Advanced, ClientId, ProbeInput);
		Tick(Advanced, ClientId, ProbeInput);
		if(ProbeBeam(Advanced, ClientId, ProbeInput, g_Config.m_KrxUnfreezeBotCurDirTicks, true, pGameClient).m_Found)
			return Input;
		const float Radius = pGameClient->m_Controls.GetMaxMouseDistance();
		const vec2 Mouse = pGameClient->m_Controls.m_aMousePos[Slot];
		auto Result = ProbeBeam(Source, ClientId, ProbeInput, g_Config.m_KrxUnfreezeBotCurDirTicks, true, pGameClient);
		if(Result.m_Found)
			Result.m_Aim = vec2(ProbeInput.m_TargetX, ProbeInput.m_TargetY);
		else
			Result = SearchUnfreezeWorld(Source, ClientId, ProbeInput, Mouse, g_Config.m_KrxUnfreezeBotTicks,
				g_Config.m_KrxUnfreezeBotPoints, (float)g_Config.m_KrxUnfreezeBotFov,
				g_Config.m_KrxUnfreezeBotMostBounces, Radius, pGameClient);
		if(!Result.m_Found)
			return Input;
		m_LastUnfreezeAim = CMiscFeatures::QuantizedAim(Result.m_Aim, Radius);
		CNetObj_PlayerInput PathInput = ProbeInput;
		PathInput.m_TargetX = (int)m_LastUnfreezeAim.x;
		PathInput.m_TargetY = (int)m_LastUnfreezeAim.y;
		auto Path = ProbeBeam(Source, ClientId, PathInput, g_Config.m_KrxUnfreezeBotTicks, true, pGameClient, true);
		m_vUnfreezePath = std::move(Path.m_vPath);
		m_vUnfreezeTeePath = std::move(Path.m_vTeePath);
		m_UnfreezeAimReleased = false;
		Aim(pGameClient, Input, m_LastUnfreezeAim, g_Config.m_KrxUnfreezeBotSilent);
		m_UnfreezePulse.Press(Input);
		return Input;
	}

	void CRescueBots::RenderUnfreeze(CGameClient *pGameClient)
	{
		if(!g_Config.m_KrxUnfreezeBot || !g_Config.m_KrxUnfreezeBotEsp || m_vUnfreezePath.empty() || m_vUnfreezeTeePath.empty())
			return;
		auto *pGraphics = pGameClient->Graphics();
		const CScreenRect PreviousScreen = pGraphics->GetScreen();
		const auto &Camera = pGameClient->m_Camera;
		pGraphics->MapScreen(pGraphics->MapScreenToWorld(Camera.m_Center.x, Camera.m_Center.y, 100, 100, 100, 0, 0, pGraphics->ScreenAspect(), Camera.m_Zoom));
		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		pGraphics->SetColor(0, 0, 1, 1);
		vec2 From = m_vUnfreezeTeePath.front();
		for(const vec2 To : m_vUnfreezePath)
		{
			if(distance(From, To) > 0)
			{
				// 30fff0's centered thick quad uses width scaled by camera zoom.
				const vec2 Offset = normalize(vec2(To.y - From.y, From.x - To.x)) * (1.5f * Camera.m_Zoom);
				IGraphics::CFreeformItem Line(From.x - Offset.x, From.y - Offset.y,
					From.x + Offset.x, From.y + Offset.y, To.x - Offset.x, To.y - Offset.y, To.x + Offset.x, To.y + Offset.y);
				pGraphics->QuadsDrawFreeform(&Line, 1);
			}
			From = To;
		}
		pGraphics->QuadsEnd();
		const int Id = pGameClient->m_aLocalIds[g_Config.m_ClDummy];
		if(Id >= 0 && Id < MAX_CLIENTS)
		{
			auto RenderInfo = pGameClient->m_aClients[Id].m_RenderInfo;
			pGameClient->RenderTools()->RenderTee(CAnimState::GetIdle(), &RenderInfo, 1, vec2(1, 0), m_vUnfreezeTeePath.front());
			pGameClient->RenderTools()->RenderTee(CAnimState::GetIdle(), &RenderInfo, 2, vec2(1, 0), m_vUnfreezePath.back());
		}
		pGraphics->MapScreen(PreviousScreen);
	}

	CRescueBots::CAledResult CRescueBots::SearchAledWorld(const CGameWorld &Source, int ClientId, int TargetId,
		CNetObj_PlayerInput Input, vec2 Mouse, float MouseRadius)
	{
		CAledResult Result;
		const CCharacter *pLocal = const_cast<CGameWorld &>(Source).GetCharacterById(ClientId);
		const CCharacter *pTarget = const_cast<CGameWorld &>(Source).GetCharacterById(TargetId);
		if(!pLocal || !pTarget)
			return Result;
		const auto Success = [&](CNetObj_PlayerInput Candidate) {
			CGameWorld World;
			World.CopyWorldIsolated(&Source);
			World.m_WorldConfig.m_PredictWeapons = true;
			World.m_WorldConfig.m_KrxPrediction = true;
			World.m_WorldConfig.m_InfiniteAmmo = true;
			CCharacter *pCharacter = World.GetCharacterById(ClientId);
			Candidate.m_Fire = 1;
			for(int Step = 0; Step <= 10; ++Step)
			{
				Tick(World, ClientId, Candidate);
				pCharacter = World.GetCharacterById(ClientId);
				CCharacter *pOther = World.GetCharacterById(TargetId);
				if(!pCharacter || !pOther)
					return false;
				if(pCharacter->GetLastHammerHitClientId() != -1 && !Frozen(pOther))
					return true;
			}
			return false;
		};
		if(Success(Input))
		{
			Result.m_Found = true;
			Result.m_InitialHit = true;
			Result.m_Aim = vec2(Input.m_TargetX, Input.m_TargetY);
		}
		const float Center = angle(pTarget->m_Pos - pLocal->m_Pos) + pi * 0.5f;
		float BestDistance = std::numeric_limits<float>::max();
		for(float Radians = Center; Radians < Center + pi; Radians += 0.06283186f)
		{
			const vec2 TargetEdge = pTarget->m_Pos + direction(Radians) * 28.0f;
			const vec2 Direction = CMiscFeatures::QuantizedAim(vec2((int)TargetEdge.x, (int)TargetEdge.y) - pLocal->m_Pos, MouseRadius);
			CNetObj_PlayerInput Candidate = Input;
			Candidate.m_TargetX = (int)Direction.x;
			Candidate.m_TargetY = (int)Direction.y;
			if(Success(Candidate))
			{
				const float CursorDistance = distance(Mouse, Direction);
				if(CursorDistance < BestDistance)
				{
					Result.m_Found = true;
					Result.m_SearchedHit = true;
					Result.m_Aim = Direction;
					BestDistance = CursorDistance;
				}
			}
		}
		return Result;
	}

	CNetObj_PlayerInput CRescueBots::AdjustAutoAled(CGameClient *pGameClient, CNetObj_PlayerInput Input, int ClientId)
	{
		const int Slot = g_Config.m_ClDummy;
		if(!m_AledPulse.Release(Input, pGameClient->KrxGameWorld().GameTick()))
			return Input;
		if(!g_Config.m_KrxAutoAled || (Input.m_Fire & 1) || !Playing(pGameClient, Input, ClientId))
			return Input;
		CCharacter *pAuthoritative = pGameClient->KrxGameWorld().GetCharacterById(ClientId);
		if(!pAuthoritative || pAuthoritative->DiagnosticReloadTimer() > 0 || IsKrxCharacterFrozen(pGameClient, ClientId))
			return Input;
		const int TargetId = CMediumBots::ClosestPlayer(pGameClient);
		if(TargetId < 0)
			return Input;
		CGameWorld Source;
		Source.CopyWorldIsolated(&pGameClient->KrxPredictedWorld());
		Source.m_WorldConfig.m_PredictWeapons = true;
		Source.m_WorldConfig.m_KrxPrediction = true;
		Source.m_WorldConfig.m_PredictTiles = true;
		Source.m_WorldConfig.m_PredictFreeze = 1;
		Source.m_WorldConfig.m_PredictDDRace = true;
		CCharacter *pLocal = Source.GetCharacterById(ClientId);
		CCharacter *pTarget = Source.GetCharacterById(TargetId);
		if(!pLocal || !pTarget || pTarget->m_FreezeTime < 1 || pLocal->GetActiveWeapon() != WEAPON_HAMMER || distance(pLocal->m_Pos, pTarget->m_Pos) > 112.0f)
			return Input;
		FilterPlayers(Source, ClientId, (float)Source.TuningList()[0].m_HookLength + 56.0f, pGameClient);
		if(!HasFreezeBetween(Source.Collision(), pLocal->m_Pos, pTarget->m_Pos) || pTarget->Core()->m_DeepFrozen)
			return Input;
		const vec2 Mouse = pGameClient->m_Controls.m_aMousePos[Slot];
		const auto Result = SearchAledWorld(Source, ClientId, TargetId, MouseInput(pGameClient, Input), Mouse, pGameClient->m_Controls.GetMaxMouseDistance());
		// 3139d0 presses immediately for an existing physical-mouse hit,
		// then still scans and can press again. Do not collapse both events.
		if(Result.m_InitialHit)
			m_AledPulse.Press(Input);
		if(Result.m_SearchedHit)
		{
			Aim(pGameClient, Input, CMiscFeatures::QuantizedAim(Result.m_Aim, pGameClient->m_Controls.GetMaxMouseDistance()), true);
			m_AledPulse.Press(Input);
		}
		return Input;
	}

}
