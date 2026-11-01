#include "bots.h"

#include "misc.h"

#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/client/avoid/rocket_save.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
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
		bool Playing(CGameClient *pGameClient, const CNetObj_PlayerInput &Input)
		{
			return (Input.m_PlayerFlags & PLAYERFLAG_PLAYING) && pGameClient->Client()->State() == IClient::STATE_ONLINE;
		}

		bool Spectating(CGameClient *pGameClient)
		{
			const int Id = pGameClient->m_aLocalIds[g_Config.m_ClDummy];
			return Id < 0 || Id >= MAX_CLIENTS || !pGameClient->m_aClients[Id].m_Active ||
			       pGameClient->m_aClients[Id].m_Team == TEAM_SPECTATORS || pGameClient->m_Snap.m_SpecInfo.m_Active;
		}

		void Aim(CNetObj_PlayerInput &Input, vec2 Value)
		{
			// Original 0x1402ba070 mode1: write prepared aim only.
			Input.m_TargetX = (int)Value.x;
			Input.m_TargetY = (int)Value.y;
		}

		float RandomRadians()
		{
			static std::mt19937 s_Generator(std::random_device{}());
			return std::uniform_real_distribution<float>(0.0f, 2.0f * pi)(s_Generator);
		}

		void ConfigureProbe(CGameWorld &World)
		{
			World.m_WorldConfig.m_PredictTiles = true;
			World.m_WorldConfig.m_PredictFreeze = 1;
			World.m_WorldConfig.m_PredictWeapons = true;
			World.m_WorldConfig.m_PredictDDRace = true;
		}

		void TickWorld(CGameWorld &World, int Id, const CNetObj_PlayerInput &Input, bool Direct = true)
		{
			auto *pLocal = World.GetCharacterById(Id);
			if(Direct && pLocal)
				pLocal->OnDirectInput(&Input);
			++World.m_GameTick;
			if(pLocal)
				pLocal->OnPredictedInput(&Input);
			World.Tick();
		}

		bool Frozen(const CCharacter *pCharacter)
		{
			return !pCharacter || pCharacter->m_FreezeTime > 0 || pCharacter->Core()->m_DeepFrozen || pCharacter->Core()->m_LiveFrozen;
		}

		void FilterHookPlayers(CGameClient *pGameClient, CGameWorld &World, int LocalId)
		{
			auto *pLocal = World.GetCharacterById(LocalId);
			if(!pLocal)
				return;
			const float Radius = pGameClient->m_aTuning[g_Config.m_ClDummy].m_HookLength + 56.0f;
			for(int Id = 0; Id < MAX_CLIENTS; ++Id)
			{
				auto *pCharacter = World.GetCharacterById(Id);
				if(!pCharacter || Id == LocalId || pCharacter->m_IsLocal)
					continue;
				if((!pGameClient->m_Snap.m_aCharacters[Id].m_Active && pCharacter->m_SnapTicks > 10) ||
					pGameClient->IsOtherTeam(Id) || distance(pLocal->m_Pos, pCharacter->m_Pos) > Radius)
					delete pCharacter;
			}
		}

		bool GroundHook(const CGameWorld &Source, int Id, CNetObj_PlayerInput Input)
		{
			CGameWorld Copy;
			Copy.CopyWorldIsolated(&Source);
			if(!Copy.GetCharacterById(Id))
				return false;
			Input.m_Hook = 1;
			for(int Step = 0; Step <= 15; ++Step)
			{
				TickWorld(Copy, Id, Input);
				const auto *pLocal = Copy.GetCharacterById(Id);
				if(!pLocal)
					return false;
				if(pLocal->Core()->HookedPlayer() == -1 && pLocal->Core()->m_HookState == HOOK_GRABBED &&
					(pLocal->Core()->m_TriggeredEvents & COREEVENT_HOOK_ATTACH_GROUND))
					return true;
			}
			return false;
		}

		int HookPlayerBlocker(CGameClient *pGameClient, int OwnId, vec2 Start, vec2 End)
		{
			// Recovered 0x140207010 uses interpolated *snapshots*, unlike the
			// generic nearest-player helper which reads predicted-core positions.
			const auto &Own = pGameClient->m_aClients[OwnId];
			const float Intra = pGameClient->Client()->IntraGameTick(g_Config.m_ClDummy);
			int BestId = -1;
			float BestDistance = 0.0f;
			for(int Id = 0; Id < MAX_CLIENTS; ++Id)
			{
				const auto &Other = pGameClient->m_aClients[Id];
				if(Id == OwnId || !Other.m_Active)
					continue;
				if(!(Own.m_Super || Other.m_Super) &&
					(!pGameClient->m_Teams.CanCollide(Id, OwnId) || Own.m_Solo || Other.m_Solo || Own.m_HookHitDisabled))
					continue;
				const auto &Snap = pGameClient->m_Snap.m_aCharacters[Id];
				const vec2 Pos = mix(vec2(Snap.m_Prev.m_X, Snap.m_Prev.m_Y), vec2(Snap.m_Cur.m_X, Snap.m_Cur.m_Y), Intra);
				vec2 Point;
				if(closest_point_on_line(Start, End, Pos, Point) && distance(Point, Pos) < 30.0f)
				{
					const float Dist = distance(Start, Pos);
					if(BestId == -1 || Dist < BestDistance)
					{
						BestId = Id;
						BestDistance = Dist;
					}
				}
			}
			return BestId;
		}
	}

	void CMediumBots::OnReset()
	{
		m_aSlots = {};
	}

	CMediumBots::CSlot &CMediumBots::State(CGameClient *pGameClient)
	{
		const int Slot = g_Config.m_ClDummy;
		CSlot &Result = m_aSlots[Slot];
		const int Id = pGameClient->m_aLocalIds[Slot];
		if(Result.m_ClientId != Id)
		{
			Result = {};
			Result.m_ClientId = Id;
		}
		return Result;
	}

	int CMediumBots::ClosestPlayer(CGameClient *pGameClient, vec2 Position, int SourceId)
	{
		if(SourceId == -1)
			SourceId = pGameClient->m_aLocalIds[g_Config.m_ClDummy];
		if(SourceId < 0 || SourceId >= MAX_CLIENTS)
			return -1;
		if(Position == vec2(0, 0))
			Position = pGameClient->m_LocalCharacterPos;
		const auto &Own = pGameClient->m_aClients[SourceId];
		if(Own.m_Solo || Own.m_Spec || Own.m_HookHitDisabled)
			return -1;
		float Best = 0.0f;
		int BestId = -1;
		// Entity order matters for ties. The original iterates the authoritative
		// world's character list, not client IDs in ascending order.
		for(auto *pCharacter = static_cast<CCharacter *>(pGameClient->m_GameWorld.FindFirst(CGameWorld::ENTTYPE_CHARACTER));
			pCharacter; pCharacter = static_cast<CCharacter *>(pCharacter->TypeNext()))
		{
			const int Id = pCharacter->GetId();
			if(Id < 0 || Id >= MAX_CLIENTS || Id == SourceId)
				continue;
			const auto &Other = pGameClient->m_aClients[Id];
			if(!Other.m_Active || Other.m_Solo || Other.m_Spec || !pGameClient->m_Teams.CanCollide(Id, SourceId))
				continue;
			const float Dist = distance(Position, Other.m_Predicted.m_Pos);
			if(BestId == -1 || Dist < Best)
			{
				BestId = Id;
				Best = Dist;
			}
		}
		return BestId;
	}

	int CMediumBots::Life(const CGameWorld &World, int ClientId, const CNetObj_PlayerInput &Input, int Ticks)
	{
		CGameWorld Copy;
		Copy.CopyWorldIsolated(&World);
		if(!Copy.GetCharacterById(ClientId))
			return -1;
		for(int Step = 0; Step <= Ticks; ++Step)
		{
			TickWorld(Copy, ClientId, Input);
			if(Frozen(Copy.GetCharacterById(ClientId)))
				return Step;
		}
		return 9999;
	}

	float CMediumBots::BalanceScore(const CGameWorld &World, int ClientId, int TargetId, const CNetObj_PlayerInput &Input, float StopDistance)
	{
		CGameWorld Copy;
		Copy.CopyWorldIsolated(&World);
		if(!Copy.GetCharacterById(ClientId))
			return -1.0f;
		if(!Copy.GetCharacterById(TargetId))
			return -2.0f;
		float Best = 9999.0f;
		for(int Step = 0; Step <= 25; ++Step)
		{
			TickWorld(Copy, ClientId, Input, false);
			const auto *pLocal = Copy.GetCharacterById(ClientId);
			const auto *pTarget = Copy.GetCharacterById(TargetId);
			if(!pLocal || !pTarget)
				break;
			const float Delta = std::abs(pTarget->m_Pos.x - pLocal->m_Pos.x);
			Best = std::min(Best, Delta);
			if(Delta <= StopDistance)
				break;
		}
		return Best;
	}

	CNetObj_PlayerInput CMediumBots::Balance(CGameClient *pGameClient, CNetObj_PlayerInput Input)
	{
		CSlot &Current = State(pGameClient);
		const int Id = pGameClient->m_aLocalIds[g_Config.m_ClDummy];
		if(!pGameClient->m_GameWorld.GetCharacterById(Id))
			return Input;
		if(!g_Config.m_KrxBalanceBot)
		{
			Current.m_BalanceTarget = -1;
			return Input;
		}
		if(Current.m_BalanceTarget == -1)
			Current.m_BalanceTarget = ClosestPlayer(pGameClient);
		if(Current.m_BalanceTarget < 0 || Current.m_BalanceTarget >= MAX_CLIENTS)
			return Input;
		if(pGameClient->m_PredictedChar.m_Pos.y == pGameClient->m_aClients[Current.m_BalanceTarget].m_Predicted.m_Pos.y)
			return Input;
		const float Offset = g_Config.m_KrxBalanceBotOffset * 0.1f;
		const int Directions[] = {0, -1, 1};
		float Scores[3];
		for(int Index = 0; Index < 3; ++Index)
		{
			auto Candidate = Input;
			Candidate.m_Direction = Directions[Index];
			Scores[Index] = BalanceScore(pGameClient->m_PredictedWorld, Id, Current.m_BalanceTarget, Candidate, Offset);
		}
		float Best = 9999.0f;
		for(int Index = 0; Index < 3; ++Index)
		{
			if(Scores[Index] < 0)
				continue;
			if(Scores[Index] <= Offset)
				break; // Original early break retains the preceding input.
			if(Scores[Index] < Best)
			{
				Best = Scores[Index];
				Input.m_Direction = Directions[Index];
			}
		}
		return Input;
	}

	CNetObj_PlayerInput CMediumBots::HookNearest(CGameClient *pGameClient, CNetObj_PlayerInput Input)
	{
		CSlot &Current = State(pGameClient);
		if(Current.m_NearestAutoIssued && !g_Config.m_KrxHookNearestAutoHook)
		{
			Input.m_Hook = 0;
			Current.m_NearestAutoIssued = false;
		}
		if(!g_Config.m_KrxHookNearestCollision && !g_Config.m_KrxHookNearestAutoHook)
			return Input;
		const int Slot = g_Config.m_ClDummy;
		const int Id = pGameClient->m_aLocalIds[Slot];
		CGameWorld Source;
		Source.CopyWorldIsolated(&pGameClient->m_PredictedWorld);
		ConfigureProbe(Source);
		const auto *pLocal = Source.GetCharacterById(Id);
		if(!pLocal)
			return Input;
		FilterHookPlayers(pGameClient, Source, Id);
		const float Fov = g_Config.m_KrxHookNearestFov * pi / 180.0f;
		const vec2 Mouse = pGameClient->m_Controls.m_aMousePos[Slot];
		const float StartAngle = std::atan2(Mouse.y, Mouse.x) - Fov * 0.5f;
		const float Radius = pGameClient->m_Controls.GetMaxMouseDistance();
		const float HookRange = pGameClient->m_aTuning[Slot].m_HookLength - 28.0f;
		float BestLength = std::numeric_limits<float>::max();
		vec2 Best(0, 0);
		for(int Index = 0; Index < 150; ++Index)
		{
			const float Angle = ((float)Index / 149.0f) * Fov + StartAngle;
			const vec2 Candidate = CMiscFeatures::QuantizedAim(direction(Angle), Radius);
			auto Probe = Input;
			Aim(Probe, Candidate);
			// Preserve the supplied helper's endpoint construction: this is
			// quantized aim times hook range, not normalized aim plus position.
			if(HookPlayerBlocker(pGameClient, Id, pLocal->m_Pos, Candidate * HookRange) != -1)
				continue;
			if(GroundHook(Source, Id, Probe))
			{
				const float CandidateLength = length(Candidate);
				if(CandidateLength < BestLength)
				{
					BestLength = CandidateLength;
					Best = Candidate;
				}
			}
		}
		if(Best == vec2(0, 0))
			return Input;
		if(g_Config.m_KrxHookNearestAutoHook)
		{
			Input.m_Hook = 1;
			Aim(Input, Best);
			Current.m_NearestAutoIssued = true;
		}
		else if(Input.m_Hook == 1 && !GroundHook(Source, Id, Input))
			Aim(Input, Best);
		return Input;
	}

	CNetObj_PlayerInput CMediumBots::HookRide(CGameClient *pGameClient, CNetObj_PlayerInput Input)
	{
		CSlot &Current = State(pGameClient);
		const int Slot = g_Config.m_ClDummy;
		const int Id = pGameClient->m_aLocalIds[Slot];
		if(Id < 0 || Id >= MAX_CLIENTS)
			return Input;
		const vec2 Pos = pGameClient->m_aClients[Id].m_Predicted.m_Pos;
		const vec2 Velocity = pGameClient->m_aClients[Id].m_Predicted.m_Vel;
		if(!g_Config.m_KrxHookRide || Spectating(pGameClient) || !Playing(pGameClient, Input))
		{
			if(Current.m_HookRideActive)
			{
				Current.m_HookRideActive = false;
				Input.m_Hook = 0;
			}
			Current.m_HookRidePosition = Pos;
			return Input;
		}
		if(!IsKrxCharacterFrozen(pGameClient, Id) && Current.m_HookRidePosition != vec2(0, 0) &&
			std::abs(Pos.x - Current.m_HookRidePosition.x) <= 10.0f)
		{
			Current.m_HookRideActive = true;
			if(pGameClient->Input()->KeyIsPressed(KEY_D))
				Current.m_HookRidePosition.x += 5.5f;
			if(pGameClient->Input()->KeyIsPressed(KEY_A))
				Current.m_HookRidePosition.x -= 5.5f;
			if(pGameClient->Input()->KeyIsPressed(KEY_W))
				Current.m_HookRidePosition.y -= 3.5f;
			if(pGameClient->Input()->KeyIsPressed(KEY_S))
				Current.m_HookRidePosition.y += 3.5f;
			CCollision *pCollision = pGameClient->Collision();
			float Range = pGameClient->m_aTuning[Slot].m_HookLength;
			const int LastIndex = pCollision->GetPureMapIndex(Pos + vec2(Range, Range));
			vec2 Best(0, 0);
			for(int Index = 0; Index < LastIndex; ++Index)
			{
				if(pCollision->GetTileIndex(Index) != TILE_SOLID)
					continue;
				const vec2 Point = pCollision->GetPos(Index);
				if(Point.y > Pos.y)
					continue;
				const float Dist = distance(Point, Pos);
				if(Dist > Range)
					continue;
				const vec2 Delta = Point - Pos;
				const float Degrees = std::atan2(Delta.y, Delta.x) * 180.0f / pi + 90.0f;
				if(Degrees > 45.0f && Degrees < 315.0f)
					continue;
				if(pCollision->IntersectLine(Pos, Point, nullptr, nullptr) == TILE_NOHOOK)
					continue;
				Range = Dist;
				Best = Point;
			}
			if(Best != vec2(0, 0))
			{
				if(Pos.x + Velocity.x - Current.m_HookRidePosition.x > 1.0f)
					Input.m_Direction = -1;
				else if(Current.m_HookRidePosition.x - Pos.x + Velocity.x > 1.0f)
					Input.m_Direction = 1;
				float TargetVelocity = std::max(std::abs(Pos.y - Current.m_HookRidePosition.y) / 6.685f, 1.0f);
				if(Pos.y > Current.m_HookRidePosition.y)
					TargetVelocity *= -0.55f;
				else if(Pos.y < Current.m_HookRidePosition.y)
					TargetVelocity *= 0.55f;
				else
					TargetVelocity = 0.0f;
				if(TargetVelocity < Velocity.y)
				{
					Aim(Input, CMiscFeatures::QuantizedAim(Best - Pos, pGameClient->m_Controls.GetMaxMouseDistance()));
					Input.m_Hook = 1;
					return Input;
				}
			}
			Input.m_Hook = 0;
		}
		else
			Current.m_HookRidePosition = Pos;
		return Input;
	}

	CNetObj_PlayerInput CMediumBots::JetRide(CGameClient *pGameClient, CNetObj_PlayerInput Input)
	{
		CSlot &Current = State(pGameClient);
		const int Slot = g_Config.m_ClDummy;
		const int Id = pGameClient->m_aLocalIds[Slot];
		const vec2 Pos = pGameClient->m_LocalCharacterPos;
		if(!g_Config.m_KrxJetRide || Spectating(pGameClient) || !Playing(pGameClient, Input))
		{
			if(Current.m_JetRideActive)
			{
				Current.m_JetRideActive = false;
				Input.m_Fire = 0;
			}
			Current.m_JetRidePosition = Pos;
			return Input;
		}
		if(IsKrxCharacterFrozen(pGameClient, Id) || Current.m_JetRidePosition == vec2(0, 0) ||
			distance(Pos, Current.m_JetRidePosition) > 210.0f)
		{
			Current.m_JetRidePosition = Pos;
			return Input;
		}
		auto *pLocal = pGameClient->m_GameWorld.GetCharacterById(Id);
		if(!pLocal)
			return Input;
		if(pLocal->GetWeaponAmmo(pLocal->GetActiveWeapon()) == 0 || !pLocal->Core()->m_Jetpack)
		{
			Current.m_JetRidePosition = Pos;
			return Input;
		}
		if(pGameClient->m_aClients[Id].m_RenderCur.m_Weapon != WEAPON_GUN || !pLocal->GetWeaponGot(WEAPON_GUN))
			return Input;
		Aim(Input, CMiscFeatures::QuantizedAim(direction(pi / 2.0f), pGameClient->m_Controls.GetMaxMouseDistance()));
		Current.m_JetRideActive = true;
		if(pGameClient->Input()->KeyIsPressed(KEY_W))
			Current.m_JetRidePosition.y -= 5.0f;
		if(pGameClient->Input()->KeyIsPressed(KEY_S))
			Current.m_JetRidePosition.y += 5.0f;
		if(pGameClient->Input()->KeyIsPressed(KEY_D))
			Current.m_JetRidePosition.x += 5.0f;
		if(pGameClient->Input()->KeyIsPressed(KEY_A))
			Current.m_JetRidePosition.x -= 5.0f;
		const vec2 MouseDirection = normalize(pGameClient->m_Controls.m_aMousePos[Slot]);
		const float Force = (pGameClient->m_aTuning[Slot].m_JetpackStrength / 100.0f) / 6.11f;
		vec2 Velocity = pGameClient->m_aClients[Id].m_Predicted.m_Vel;
		Velocity.x -= MouseDirection.x * Force;
		if(Velocity.x + Pos.x - Current.m_JetRidePosition.x > 1.0f)
			Input.m_Direction = -1;
		else
			Input.m_Direction = Current.m_JetRidePosition.x - Pos.x - Velocity.x > 1.0f ? 1 : 0;
		float TargetVelocity = std::max(std::abs(Pos.y - Current.m_JetRidePosition.y) / 6.685f, 1.0f);
		if(Pos.y > Current.m_JetRidePosition.y)
			TargetVelocity *= -0.55f;
		else if(Pos.y < Current.m_JetRidePosition.y)
			TargetVelocity *= 0.55f;
		else
			TargetVelocity = 0.0f;
		Input.m_Fire = Velocity.y + MouseDirection.y * Force <= TargetVelocity ? 0 : 1;
		return Input;
	}

	CNetObj_PlayerInput CMediumBots::AutoJumpSave(CGameClient *pGameClient, CNetObj_PlayerInput Input)
	{
		CSlot &Current = State(pGameClient);
		if(Current.m_JumpPending)
		{
			Current.m_JumpPending = false;
			Input.m_Jump = 0;
		}
		if(!g_Config.m_KrxAutoJumpSave || !Playing(pGameClient, Input) || Spectating(pGameClient))
			return Input;
		const int Slot = g_Config.m_ClDummy;
		const int Id = pGameClient->m_aLocalIds[Slot];
		const CGameWorld &Source = pGameClient->m_PredictedWorld;
		// GetCharacterById is a non-const lookup over the character array; the
		// prediction world is only read here.
		if(!const_cast<CGameWorld &>(Source).GetCharacterById(Id))
			return Input;
		auto Probe = Input;
		// Jump save restores physical aim for its probes even after preceding
		// automation changed the prepared aim.
		Aim(Probe, pGameClient->m_Controls.m_aMousePos[Slot]);
		if(JumpSaveWorld(Source, Id, Probe, g_Config.m_KrxAutoJumpSaveMode, pGameClient->m_PredictedChar.m_Vel))
		{
			Input.m_Jump = 1;
			Current.m_JumpPending = true;
		}
		return Input;
	}

	bool CMediumBots::JumpSaveWorld(const CGameWorld &Source, int Id, CNetObj_PlayerInput Probe, int Mode, vec2 Velocity)
	{
		bool Save = false;
		if(Mode == 0)
		{
			if(Life(Source, Id, Probe, 2) != 9999)
			{
				Probe.m_Jump = 1;
				Save = Life(Source, Id, Probe, 16) == 9999;
			}
		}
		else if(Mode == 1 || (Mode == 2 && std::abs(Velocity.x) < std::abs(Velocity.y)))
		{
			CGameWorld Snapshot;
			Snapshot.CopyWorldIsolated(&Source);
			ConfigureProbe(Snapshot);
			if(Life(Snapshot, Id, Probe, 16) < 16)
			{
				CGameWorld Delayed;
				Delayed.CopyWorldIsolated(&Snapshot);
				for(int OtherId = 0; OtherId < MAX_CLIENTS; ++OtherId)
				{
					auto *pOther = Delayed.GetCharacterById(OtherId);
					if(pOther && OtherId != Id && !pOther->m_IsLocal)
						delete pOther;
				}
				TickWorld(Delayed, Id, Probe);
				TickWorld(Delayed, Id, Probe);
				auto Jump = Probe;
				Jump.m_Jump = 1;
				if(Life(Delayed, Id, Jump, 27) != 9999 && Life(Snapshot, Id, Jump, 16) == 9999)
					Save = true;
			}
		}
		return Save;
	}

	vec2 CMediumBots::FakeAimVector(int Mode, int Speed, vec2 Mouse, const CNetObj_PlayerInput &Input,
		int PreviousHook, vec2 ClosestDirection, float MouseRadius, vec2 &RobotAim, float &SpinAngle, float RandomAngle)
	{
		// Recovered 0x1402dcdd0: all six modes, including zero-speed spin and
		// the asymmetric robot latch, use truncation rather than rounding.
		switch(Mode)
		{
		case 0:
			return vec2((int)Mouse.x, (int)Mouse.y);
		case 1:
			if((Input.m_Hook != 1 || PreviousHook != 0) && !(Input.m_Fire & 1))
				return RobotAim;
			RobotAim = vec2(Input.m_TargetX, Input.m_TargetY);
			return RobotAim;
		case 2:
			if(Speed == 0)
				return vec2(0, 0);
			SpinAngle += (Speed * pi) / 99.9f;
			if(SpinAngle < 0.0f)
				SpinAngle += 2.0f * pi;
			else if(SpinAngle > 2.0f * pi)
				SpinAngle -= 2.0f * pi;
			return CMiscFeatures::QuantizedAim(direction(SpinAngle), MouseRadius);
		case 3:
			return CMiscFeatures::QuantizedAim(direction(RandomAngle), MouseRadius);
		case 4:
			return vec2((int)-Mouse.x, (int)-Mouse.y);
		case 5:
			return CMiscFeatures::QuantizedAim(ClosestDirection, MouseRadius);
		default:
			return vec2(0, 0);
		}
	}

	CNetObj_PlayerInput CMediumBots::FakeAim(CGameClient *pGameClient, CNetObj_PlayerInput Input)
	{
		CSlot &Current = State(pGameClient);
		if(!g_Config.m_KrxFakeAim || Spectating(pGameClient) || !Playing(pGameClient, Input))
			return Input;
		const int Slot = g_Config.m_ClDummy;
		const int PreviousHook = pGameClient->m_Controls.m_aLastData[Slot].m_Hook;
		if(((Input.m_Hook != 1 || PreviousHook != 0) && !(Input.m_Fire & 1)) || g_Config.m_KrxFakeAimMode == 1)
		{
			vec2 ClosestDirection(0, 0);
			if(g_Config.m_KrxFakeAimMode == 5)
			{
				const int Target = ClosestPlayer(pGameClient);
				if(Target >= 0)
					ClosestDirection = pGameClient->m_aClients[Target].m_Predicted.m_Pos - pGameClient->m_LocalCharacterPos;
			}
			Aim(Input, FakeAimVector(g_Config.m_KrxFakeAimMode, g_Config.m_KrxFakeAimSpeed,
					   pGameClient->m_Controls.m_aMousePos[Slot], Input, PreviousHook, ClosestDirection,
					   pGameClient->m_Controls.GetMaxMouseDistance(), Current.m_RobotAim, Current.m_SpinAngle,
					   g_Config.m_KrxFakeAimMode == 3 ? RandomRadians() : 0.0f));
		}
		return Input;
	}
}
