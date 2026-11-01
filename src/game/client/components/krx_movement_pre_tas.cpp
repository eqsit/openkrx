#include "krx_movement_pre_tas.h"

#include <base/str.h>
#include <base/time.h>

#include <engine/shared/config.h>

#include <game/client/avoid/rocket_save.h>
#include <game/client/gameclient.h>
#include <game/client/krx/bots.h>
#include <game/client/krx/misc.h>
#include <game/client/prediction/entities/character.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>

void CKrxMovementPreTas::OnReset()
{
	m_FastInput = {};
	// The movement/tune state is stored in process globals by the original.
	// Handler-owned off/reset branches clear it; reconnect must not invent
	// another reset (REA lifecycle references045..047).
}

bool CKrxMovementPreTas::UpdateFastInput()
{
	// REA 23e070 copies all ten input words, derives direction from physical
	// keys, and compares the seven non-aim action fields with the old cache.
	const int Slot = g_Config.m_ClDummy;
	const auto &Controls = GameClient()->m_Controls;
	CNetObj_PlayerInput Input = Controls.m_aInputData[Slot];
	Input.m_Direction = (Controls.m_aInputDirectionRight[Slot] != 0) - (Controls.m_aInputDirectionLeft[Slot] != 0);
	if(g_Config.m_ClSubTickAiming)
	{
		Input.m_TargetX = (int)Controls.m_aMousePos[Slot].x;
		Input.m_TargetY = (int)Controls.m_aMousePos[Slot].y;
	}
	const bool Changed = Input.m_Direction != m_FastInput.m_Direction ||
			     Input.m_Jump != m_FastInput.m_Jump || Input.m_Fire != m_FastInput.m_Fire ||
			     Input.m_Hook != m_FastInput.m_Hook || Input.m_WantedWeapon != m_FastInput.m_WantedWeapon ||
			     Input.m_NextWeapon != m_FastInput.m_NextWeapon || Input.m_PrevWeapon != m_FastInput.m_PrevWeapon;
	m_FastInput = Input;
	return Changed;
}

void CKrxMovementPreTas::GhostMove(CNetObj_PlayerInput &Input)
{
	// REA 2c0650: the cached world-tick parity gates the alternating phase.
	// Deliberately do not turn this into a simple current-tick modulo toggle.
	if(!g_Config.m_KrxGhostMove)
	{
		m_GhostSavedAim = vec2(0, 0);
		m_GhostLastTick = 1;
		m_GhostToggle = true;
		return;
	}
	const int WorldTick = GameClient()->m_PredictedWorld.GameTick();
	if(m_GhostLastTick % 2 == 0)
	{
		if(!m_GhostToggle)
		{
			m_GhostToggle = true;
			m_GhostSavedAim = vec2((float)Input.m_TargetX, (float)Input.m_TargetY);
			if(g_Config.m_KrxGhostMoveDirection)
			{
				const auto &Controls = GameClient()->m_Controls;
				Input.m_Direction = (Controls.m_aInputDirectionRight[g_Config.m_ClDummy] != 0) -
						    (Controls.m_aInputDirectionLeft[g_Config.m_ClDummy] != 0);
			}
			if(g_Config.m_KrxGhostMoveHookClosest)
			{
				const int Id = OpenKrx::CMediumBots::ClosestPlayer(GameClient());
				if(Id != -1)
				{
					const vec2 Aim = GameClient()->m_aClients[Id].m_Predicted.m_Pos - GameClient()->m_LocalCharacterPos;
					Input.m_TargetX = (int)Aim.x;
					Input.m_TargetY = (int)Aim.y;
					Input.m_Hook = 1;
				}
			}
			if(g_Config.m_KrxGhostMoveHook)
				Input.m_Hook = 1;
		}
		else
		{
			m_GhostToggle = false;
			if(g_Config.m_KrxGhostMoveDirection)
				Input.m_Direction = 0;
			if(g_Config.m_KrxGhostMoveJump)
				Input.m_Jump = 0;
			if(g_Config.m_KrxGhostMoveHookClosest)
			{
				Input.m_TargetX = (int)m_GhostSavedAim.x;
				Input.m_TargetY = (int)m_GhostSavedAim.y;
				Input.m_Hook = 0;
			}
			if(g_Config.m_KrxGhostMoveHook)
				Input.m_Hook = 0;
		}
		if(WorldTick % 2 == 0)
			return;
		const bool HasAction = (g_Config.m_KrxGhostMoveDirection && Input.m_Direction != 0) ||
				       (g_Config.m_KrxGhostMoveJump && Input.m_Jump != 0) ||
				       ((g_Config.m_KrxGhostMoveHookClosest || g_Config.m_KrxGhostMoveHook) && Input.m_Hook != 0);
		if(!HasAction)
			return;
	}
	m_GhostLastTick = WorldTick;
}

namespace
{
	// GoodCrack collision23cd10 truncates world coordinates then divides by 32
	// with arithmetic right shift. GetPureMapIndex instead rounds; keep both.
	int RawTileAt(const CCollision *pCollision, float X, float Y)
	{
		const int TileX = std::clamp((int)X >> 5, 0, pCollision->GetWidth() - 1);
		const int TileY = std::clamp((int)Y >> 5, 0, pCollision->GetHeight() - 1);
		return pCollision->GetTileIndex(TileX + pCollision->GetWidth() * TileY);
	}

	bool SolidAt(const CCollision *pCollision, float X, float Y)
	{
		const int Tile = RawTileAt(pCollision, X, Y);
		return Tile == TILE_SOLID || Tile == TILE_NOHOOK;
	}

	bool EdgeGrounded(CGameClient *pGameClient, int Id)
	{
		const vec2 Pos = pGameClient->m_aClients[Id].m_Predicted.m_Pos;
		const auto *pCollision = pGameClient->Collision();
		return SolidAt(pCollision, round_to_int(Pos.x + 14.0f), round_to_int(Pos.y + 19.0f)) ||
		       SolidAt(pCollision, round_to_int(Pos.x - 14.0f), round_to_int(Pos.y + 19.0f)) ||
		       (pCollision->GetMoveRestrictions(Pos + vec2(0, 18.0f), 0.0f) & CANTMOVE_DOWN);
	}

	float EdgeDistance(const CGameWorld &Source, int Id, CNetObj_PlayerInput Input, float TargetX)
	{
		// Original2b65e0 takes predicted input only (no OnDirectInput), 0..25
		// inclusive, and stops at distance<=0.2 while retaining the minimum.
		CGameWorld Copy;
		Copy.CopyWorldIsolated(&Source);
		auto *pLocal = Copy.GetCharacterById(Id);
		if(!pLocal)
			return -1.0f;
		float Minimum = 9999.0f;
		for(int Step = 0; Step <= 25; ++Step)
		{
			++Copy.m_GameTick;
			pLocal->OnPredictedInput(&Input);
			Copy.Tick();
			const float Dist = std::abs(TargetX - pLocal->m_Pos.x);
			Minimum = std::min(Minimum, Dist);
			if(Dist <= 0.2f)
				return Dist;
		}
		return Minimum;
	}
}

void CKrxMovementPreTas::AutoEdge(CNetObj_PlayerInput &Input)
{
	const int Id = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(!g_Config.m_KrxAutoEdge || !(Input.m_PlayerFlags & PLAYERFLAG_PLAYING) || Id < 0 || Id >= MAX_CLIENTS ||
		OpenKrx::IsKrxCharacterFrozen(GameClient(), Id) || Client()->State() != IClient::STATE_ONLINE ||
		GameClient()->m_Snap.m_SpecInfo.m_Active || GameClient()->m_aClients[Id].m_Team == TEAM_SPECTATORS)
	{
		m_EdgeTarget = vec2(0, 0);
		m_EdgeTile = -1;
		return;
	}
	const auto &Core = GameClient()->m_PredictedChar;
	const vec2 Probe = Core.m_Pos + Core.m_Vel * (length(Core.m_Vel) * 0.5f);
	const bool Grounded = EdgeGrounded(GameClient(), Id);
	const auto *pCollision = Collision();
	const int aTiles[] = {TILE_DEATH, TILE_FREEZE};
	for(const int Tile : aTiles)
	{
		if(Grounded && (RawTileAt(pCollision, Probe.x + 32.0f, Probe.y) == Tile ||
				       RawTileAt(pCollision, Probe.x - 32.0f, Probe.y) == Tile))
		{
			Input.m_Direction = 0;
			return;
		}
	}
	for(const int Tile : aTiles)
	{
		float Nearest = 200.0f;
		const float Offset = Tile == TILE_DEATH ? 27.75f : 24.5f;
		// This scan begins at map index0 in the original. Do not replace it
		// with a radius box; the ordering and the per-tile nearest reset matter.
		const int End = pCollision->GetPureMapIndex(Probe + vec2(200, 200));
		for(int Index = 0; Index < End; ++Index)
		{
			if(pCollision->GetTileIndex(Index) != Tile)
				continue;
			const vec2 Pos = pCollision->GetPos(Index);
			const float Side = Probe.x < Pos.x ? -Offset : Offset;
			const float Dist = distance(Probe, Pos);
			if(Dist > Nearest || Probe.y > Pos.y)
				continue;
			if(RawTileAt(pCollision, Pos.x + Side, Pos.y) == Tile && SolidAt(pCollision, Pos.x + Side, Pos.y))
				continue;
			if(!SolidAt(pCollision, Pos.x, Pos.y + 32) && !SolidAt(pCollision, Pos.x + Side, Pos.y + 32))
				continue;
			m_EdgeTarget = Pos + vec2(Side, 0);
			m_EdgeTile = Tile;
			Nearest = Dist;
		}
	}
	if(m_EdgeTarget == vec2(0, 0) || m_EdgeTile == -1)
		return;
	if(std::abs(m_EdgeTarget.x - Probe.x) >= 2.0f && !Grounded)
	{
		float BestScore = 9999.0f;
		int BestDirection = Input.m_Direction;
		const int aDirections[] = {0, -1, 1};
		for(const int Direction : aDirections)
		{
			auto Candidate = Input;
			Candidate.m_Direction = Direction;
			const float Score = EdgeDistance(GameClient()->m_PredictedWorld, Id, Candidate, m_EdgeTarget.x);
			if(Score >= 0)
			{
				if(Score <= 0.2f)
				{
					Input.m_Direction = Direction;
					return;
				}
				if(Score < BestScore)
				{
					BestScore = Score;
					BestDirection = Direction;
				}
				Input.m_Direction = BestDirection;
			}
		}
		return;
	}
	if(!Grounded)
		return;
	m_EdgeTarget = vec2(0, 0);
	m_EdgeTile = -1;
}

void CKrxMovementPreTas::ObserveProjectile(int StartTick)
{
	// No enable/owner/type gate exists at original Items181820a8.
	if(m_TunesStartTick == 0)
		m_TunesStartTick = StartTick;
}

void CKrxMovementPreTas::ObserveExplosion(vec2 Position, int WorldTick)
{
	m_TunesExplosion = Position;
	m_TunesEndTick = WorldTick;
}

namespace
{
	struct CTuneProbe
	{
		int m_Hundredths = 0;
		vec2 m_Collision = vec2(0, 0);
		vec2 m_BeforeCollision = vec2(0, 0);
	};

	vec2 TunePosition(vec2 Start, vec2 Direction, float Curvature, float Speed, float Time)
	{
		// Preserve original2aae70 addition order; an ULP can change which
		// curvature first passes the component-wise tolerance comparison.
		const float Distance = Time * Speed;
		return vec2(Direction.x * Distance + Start.x,
			Distance * Distance * (Curvature / 10000.0f) + Direction.y * Distance + Start.y);
	}

	CTuneProbe ProbeGrenade(CCollision *pCollision, vec2 Start, vec2 Direction, float Curvature, float Speed, int MaxHundredths)
	{
		CTuneProbe Result;
		float Time = 0.0f;
		for(int Step = 0; Step < MaxHundredths; ++Step)
		{
			Time += 0.01f;
			const vec2 End = TunePosition(Start, Direction, Curvature, Speed, Time);
			const vec2 Previous = TunePosition(Start, Direction, Curvature, Speed, Time - 0.01f);
			if(pCollision->IntersectLine(Previous, End, &Result.m_Collision, &Result.m_BeforeCollision))
			{
				Result.m_Hundredths = Step;
				return Result;
			}
		}
		Result.m_Hundredths = MaxHundredths;
		return Result;
	}
}

void CKrxMovementPreTas::FindTunes()
{
	if(!g_Config.m_KrxFindTunes)
		return;
	constexpr float Angle = 5.4977874755859375f; // Original float bits40afede0.
	const vec2 UnitDirection = normalize(direction(Angle));
	if(!m_TunesStarted)
	{
		m_TunesStartTick = m_TunesEndTick = 0;
		m_TunesExplosion = vec2(0, 0);
		m_TunesStarted = true;
		auto &Controls = GameClient()->m_Controls;
		const vec2 Aim = UnitDirection * 25.0f;
		Controls.m_aMousePos[g_Config.m_ClDummy] = Aim;
		auto &Input = Controls.m_aInputData[g_Config.m_ClDummy];
		Input.m_TargetX = (int)Aim.x;
		Input.m_TargetY = (int)Aim.y;
		Input.m_Fire += 2;
	}
	if(m_TunesExplosion == vec2(0, 0) || m_TunesEndTick == 0 || m_TunesStartTick == 0)
		return;

	const vec2 Observed = m_TunesExplosion;
	const float ObservedLifetime = (float)(m_TunesEndTick - m_TunesStartTick);
	// Original 2ab1b0 directly reads tuning0, including its fixed point lifetime
	// as a count of 0.01-second probe steps. It does not use dummy tuning here.
	const float Speed = GameClient()->m_aTuning[0].m_GrenadeSpeed;
	const int MaxHundredths = GameClient()->m_aTuning[0].m_GrenadeLifetime.Get();
	const vec2 Start = GameClient()->m_LocalCharacterPos + UnitDirection * 28.0f * 0.75f;
	const int64_t Started = time_get_nanoseconds().count();
	bool Found = false;
	for(float Delta = 1.0f; Delta <= 2.0f && !Found; Delta += 0.05f)
	{
		for(float Curvature = 0.0f; Curvature <= 10.0f; Curvature += 0.001f)
		{
			const CTuneProbe Probe = ProbeGrenade(Collision(), Start, UnitDirection, Curvature, Speed, MaxHundredths);
			const vec2 Hit = Probe.m_Collision == vec2(0, 0) ? Probe.m_BeforeCollision : Probe.m_Collision;
			if(std::abs(Hit.x - Observed.x) <= Delta && std::abs(Hit.y - Observed.y) <= Delta)
			{
				const double Milliseconds = (double)(time_get_nanoseconds().count() - Started) / 1000000.0;
				const int SimulatedLifetime = (int)((float)Probe.m_Hundredths * 0.5f);
				char aMessage[256];
				str_format(aMessage, sizeof(aMessage), "BTunes: Found grenade tunes(%f ms)! | Delta: %f | Lifetimes %d / %d",
					Milliseconds, (double)Delta, SimulatedLifetime, (int)ObservedLifetime);
				GameClient()->Echo(aMessage);
				str_format(aMessage, sizeof(aMessage), "BTunes: Grenade curvature: %f", (double)Curvature);
				GameClient()->Echo(aMessage);
				str_format(aMessage, sizeof(aMessage), "BTunes: Estimated grenade speed: %f", (double)(((float)SimulatedLifetime / ObservedLifetime) * Speed));
				GameClient()->Echo(aMessage);
				Found = true;
				break;
			}
		}
	}
	if(!Found)
		GameClient()->Echo("BTunes: Couldn't bruteforce tunes");
	m_TunesExplosion = vec2(0, 0);
	m_TunesStartTick = m_TunesEndTick = 0;
	g_Config.m_KrxFindTunes = 0;
	m_TunesStarted = false;
}
