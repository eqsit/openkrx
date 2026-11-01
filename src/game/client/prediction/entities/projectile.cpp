/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "projectile.h"

#include "character.h"

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/projectile_data.h>
#include <game/collision.h>
#include <game/mapitems.h>

CProjectile::CProjectile(
	CGameWorld *pGameWorld,
	int Type,
	int Owner,
	vec2 Pos,
	vec2 Dir,
	int Span,
	bool Freeze,
	bool Explosive,
	int SoundImpact,
	int Layer,
	int Number) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE)
{
	m_Type = Type;
	m_Pos = Pos;
	m_Direction = Dir;
	m_LifeSpan = Span;
	m_Owner = Owner;
	m_SoundImpact = SoundImpact;
	m_StartTick = GameWorld()->GameTick();
	m_Explosive = Explosive;

	m_Layer = Layer;
	m_Number = Number;
	m_Bouncing = 0;
	m_Freeze = Freeze;

	m_TuneZone = GameWorld()->m_WorldConfig.m_UseTuneZones ? Collision()->IsTune(Collision()->GetMapIndex(m_Pos)) : 0;

	GameWorld()->InsertEntity(this);
}

vec2 CProjectile::GetPos(float Time)
{
	float Curvature = 0;
	float Speed = 0;
	CTuningParams *pTuning = GetTuning(m_TuneZone);

	switch(m_Type)
	{
	case WEAPON_GRENADE:
		Curvature = pTuning->m_GrenadeCurvature;
		Speed = pTuning->m_GrenadeSpeed;
		break;

	case WEAPON_SHOTGUN:
		Curvature = pTuning->m_ShotgunCurvature;
		Speed = pTuning->m_ShotgunSpeed;
		break;

	case WEAPON_GUN:
		Curvature = pTuning->m_GunCurvature;
		Speed = pTuning->m_GunSpeed;
		break;
	}

	return CalcPos(m_Pos, m_Direction, Curvature, Speed, Time);
}

void CProjectile::Tick()
{
	const bool Krx = GameWorld()->m_WorldConfig.m_KrxPrediction;
	if(Krx && !m_KrxAlive)
	{
		m_MarkedForDestroy = true;
		return;
	}
	if(Krx)
		m_HitClientId = -1;
	float Pt = (GameWorld()->GameTick() - m_StartTick - 1) / (float)GameWorld()->GameTickSpeed();
	float Ct = (GameWorld()->GameTick() - m_StartTick) / (float)GameWorld()->GameTickSpeed();
	vec2 PrevPos = GetPos(Pt);
	vec2 CurPos = GetPos(Ct);
	vec2 ColPos;
	vec2 NewPos;
	int Collide = Collision()->IntersectLine(PrevPos, CurPos, &ColPos, &NewPos);
	CCharacter *pOwnerChar = GameWorld()->GetCharacterById(m_Owner);

	CCharacter *pTargetChr = nullptr;
	if(!GameWorld()->m_WorldConfig.m_IsSolo || (m_Type == WEAPON_SHOTGUN && GameWorld()->m_WorldConfig.m_IsDDRace))
		pTargetChr = GameWorld()->IntersectCharacter(PrevPos, ColPos, m_Freeze ? 1.0f : 6.0f, ColPos, pOwnerChar, m_Owner);

	if(m_LifeSpan > -1)
		m_LifeSpan--;

	bool IsWeaponCollide = false;
	if(
		pOwnerChar &&
		pTargetChr &&
		!pTargetChr->CanCollide(m_Owner))
	{
		IsWeaponCollide = true;
	}

	if(((pTargetChr && (pOwnerChar ? !pOwnerChar->GrenadeHitDisabled() : g_Config.m_SvHit || m_Owner == -1 || pTargetChr == pOwnerChar)) || Collide || GameLayerClipped(CurPos)) && !IsWeaponCollide)
	{
		if(pTargetChr)
		{
			m_HitClientId = pTargetChr->GetCid();
			if(m_pHitClientIdObserver)
				*m_pHitClientIdObserver = m_HitClientId;
		}
		if(Krx && pTargetChr && pOwnerChar)
			m_KrxAlive = false;
		if(m_Explosive && (!pTargetChr || (!m_Freeze || (m_Type == WEAPON_SHOTGUN && Collide))))
		{
			GameWorld()->CreateExplosion(ColPos, m_Owner, m_Type, m_Owner == -1, (!pTargetChr ? -1 : pTargetChr->Team()), CClientMask().set(), m_StartTick);
			if(GameWorld()->m_WorldConfig.m_IsDDRace && GameWorld()->m_WorldConfig.m_PredictDDRace)
			{
				// vanilla has different projectile physics
				GameWorld()->CreatePredictedSound(ColPos, m_SoundImpact, m_StartTick);
			}
		}
		else if(m_Freeze)
		{
			CEntity *apEnts[MAX_CLIENTS];
			int Num = GameWorld()->FindEntities(CurPos, 1.0f, apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
			for(int i = 0; i < Num; ++i)
			{
				auto *pChr = static_cast<CCharacter *>(apEnts[i]);
				if(pChr && (m_Layer != LAYER_SWITCH || (m_Layer == LAYER_SWITCH && m_Number > 0 && m_Number < (int)Switchers().size() && Switchers()[m_Number].m_aStatus[pChr->Team()])))
					pChr->Freeze();
			}
		}
		if(Collide && m_Bouncing != 0)
		{
			m_StartTick = GameWorld()->GameTick();
			m_Pos = NewPos + (-(m_Direction * 4));
			if(m_Bouncing == 1)
				m_Direction.x = -m_Direction.x;
			else if(m_Bouncing == 2)
				m_Direction.y = -m_Direction.y;
			if(absolute(m_Direction.x) < 1e-6f)
				m_Direction.x = 0;
			if(absolute(m_Direction.y) < 1e-6f)
				m_Direction.y = 0;
			m_Pos += m_Direction;
		}
		else if(m_Type == WEAPON_GUN)
		{
			if(GameWorld()->m_WorldConfig.m_IsDDRace && GameWorld()->m_WorldConfig.m_PredictDDRace)
				GameWorld()->CreatePredictedDamageIndEvent(CurPos, -std::atan2(m_Direction.x, m_Direction.y), 10, m_StartTick);
			if(Krx)
				m_KrxAlive = false;
			else
				m_MarkedForDestroy = true;
		}
		else if(!m_Freeze)
		{
			if(Krx)
				m_KrxAlive = false;
			else
				m_MarkedForDestroy = true;
		}
		if(Krx && pOwnerChar && !GameLayerClipped(ColPos) &&
			((m_Type == WEAPON_GRENADE && pOwnerChar->Core()->m_HasTelegunGrenade) ||
			 (m_Type == WEAPON_GUN && pOwnerChar->Core()->m_HasTelegunGun)))
		{
			const int MapIndex = Collision()->GetPureMapIndex(pTargetChr ? pTargetChr->m_Pos : ColPos);
			const int Front = Collision()->GetFrontTileIndex(MapIndex);
			bool Switch = Collision()->GetSwitchType(MapIndex) == TILE_ALLOW_TELE_GUN;
			bool BlueSwitch = Collision()->GetSwitchType(MapIndex) == TILE_ALLOW_BLUE_TELE_GUN;
			if(Switch || BlueSwitch)
			{
				const int Delay = Collision()->GetSwitchDelay(MapIndex);
				if((Delay == 1 && m_Type != WEAPON_GUN) ||
					(Delay == 2 && m_Type != WEAPON_GRENADE) ||
					(Delay == 3 && m_Type != WEAPON_LASER))
					Switch = BlueSwitch = false;
			}
			if(Front == TILE_ALLOW_TELE_GUN || Front == TILE_ALLOW_BLUE_TELE_GUN || Switch || BlueSwitch || pTargetChr)
			{
				vec2 PossiblePos;
				const bool Found = Collide ? KrxNearestAirPos(NewPos, CurPos, &PossiblePos) :
					KrxNearestAirPosPlayer(pTargetChr ? pTargetChr->m_Pos : ColPos, &PossiblePos);
				if(Found)
				{
					pOwnerChar->m_KrxTeleGunPos = PossiblePos;
					pOwnerChar->m_KrxTeleGunTeleport = true;
					pOwnerChar->m_KrxBlueTeleGunTeleport = Front == TILE_ALLOW_BLUE_TELE_GUN || BlueSwitch;
				}
			}
		}
	}
	if(m_LifeSpan == -1)
	{
		if(m_Explosive)
		{
			if(m_Owner >= 0)
				pOwnerChar = GameWorld()->GetCharacterById(m_Owner);

			GameWorld()->CreateExplosion(ColPos, m_Owner, m_Type, m_Owner == -1, (!pOwnerChar ? -1 : pOwnerChar->Team()), CClientMask().set(), m_StartTick);
			GameWorld()->CreatePredictedSound(ColPos, m_SoundImpact, m_StartTick);
		}
		if(Krx)
			m_KrxAlive = false;
		else
			m_MarkedForDestroy = true;
	}
	if(Krx)
	{
		// 24dc80 checks weapon teleporters even on the impact/expiry tick.
		const int Index = Collision()->GetIndex(PrevPos, CurPos);
		const int Number = g_Config.m_SvOldTeleportWeapons ? Collision()->IsTeleport(Index) : Collision()->IsTeleportWeapon(Index);
		if(Number && !Collision()->TeleOuts(Number - 1).empty())
		{
			const auto &vOuts = Collision()->TeleOuts(Number - 1);
			m_Pos = vOuts[GameWorld()->m_Core.RandomOr0(vOuts.size())];
			m_StartTick = GameWorld()->GameTick();
		}
	}
}

bool CProjectile::KrxNearestAirPos(vec2 Pos, vec2 PrevPos, vec2 *pOutPos)
{
	// 21c890: retreat up to 16 pixels, then test the three 28x28 corner fits.
	for(int Step = 0; Step < 16 && Collision()->CheckPoint(Pos); ++Step)
		Pos -= normalize(PrevPos - Pos);
	const vec2 InBlock(round_to_int(Pos.x) % 32, round_to_int(Pos.y) % 32);
	const vec2 Center = vec2(round_to_int(Pos.x), round_to_int(Pos.y)) - InBlock + vec2(16.0f, 16.0f);
	*pOutPos = vec2(Center.x + (InBlock.x < 16.0f ? -2.0f : 1.0f), Pos.y);
	if(!Collision()->TestBox(*pOutPos, CCharacterCore::PhysicalSizeVec2()))
		return true;
	*pOutPos = vec2(Pos.x, Center.y + (InBlock.y < 16.0f ? -2.0f : 1.0f));
	if(!Collision()->TestBox(*pOutPos, CCharacterCore::PhysicalSizeVec2()))
		return true;
	*pOutPos = vec2(Center.x + (InBlock.x < 16.0f ? -2.0f : 1.0f), Center.y + (InBlock.y < 16.0f ? -2.0f : 1.0f));
	return !Collision()->TestBox(*pOutPos, CCharacterCore::PhysicalSizeVec2());
}

bool CProjectile::KrxNearestAirPosPlayer(vec2 PlayerPos, vec2 *pOutPos)
{
	// 21cc50: top-down candidate order is part of the chosen destination.
	for(int Offset = 5; Offset >= -1; --Offset)
	{
		*pOutPos = vec2(PlayerPos.x, PlayerPos.y - Offset);
		if(!Collision()->TestBox(*pOutPos, CCharacterCore::PhysicalSizeVec2()))
			return true;
	}
	return false;
}

// DDRace

void CProjectile::SetBouncing(int Value)
{
	m_Bouncing = Value;
}

CProjectile::CProjectile(CGameWorld *pGameWorld, int Id, const CProjectileData *pProj) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE)
{
	m_Pos = pProj->m_StartPos;
	m_Direction = pProj->m_StartVel;
	if(pProj->m_ExtraInfo)
	{
		m_Owner = pProj->m_Owner;
		m_Explosive = pProj->m_Explosive;
		m_Bouncing = pProj->m_Bouncing;
		m_Freeze = pProj->m_Freeze;
	}
	else
	{
		m_Owner = -1;
		m_Bouncing = 0;
		m_Freeze = false;
		m_Explosive = (pProj->m_Type == WEAPON_GRENADE) && (absolute(1.0f - length(m_Direction)) < 0.015f);
	}
	m_Type = pProj->m_Type;
	m_StartTick = pProj->m_StartTick;
	m_TuneZone = pProj->m_TuneZone;

	int Lifetime = 20 * GameWorld()->GameTickSpeed();
	m_SoundImpact = -1;
	if(m_Type == WEAPON_GRENADE)
	{
		Lifetime = GetTuning(m_TuneZone)->m_GrenadeLifetime * GameWorld()->GameTickSpeed();
		m_SoundImpact = SOUND_GRENADE_EXPLODE;
	}
	else if(m_Type == WEAPON_GUN)
	{
		Lifetime = GetTuning(m_TuneZone)->m_GunLifetime * GameWorld()->GameTickSpeed();
	}
	else if(m_Type == WEAPON_SHOTGUN && !GameWorld()->m_WorldConfig.m_IsDDRace)
	{
		Lifetime = GetTuning(m_TuneZone)->m_ShotgunLifetime * GameWorld()->GameTickSpeed();
	}
	m_LifeSpan = Lifetime - (pGameWorld->GameTick() - m_StartTick);
	m_Id = Id;
	m_Number = pProj->m_SwitchNumber;
	m_Layer = m_Number > 0 ? LAYER_SWITCH : LAYER_GAME;
}

CProjectileData CProjectile::GetData() const
{
	CProjectileData Result;
	Result.m_StartPos = m_Pos;
	Result.m_StartVel = m_Direction;
	Result.m_Type = m_Type;
	Result.m_StartTick = m_StartTick;
	Result.m_ExtraInfo = true;
	Result.m_Owner = m_Owner;
	Result.m_Explosive = m_Explosive;
	Result.m_Bouncing = m_Bouncing;
	Result.m_Freeze = m_Freeze;
	Result.m_TuneZone = m_TuneZone;
	Result.m_SwitchNumber = m_Number;
	return Result;
}

bool CProjectile::Match(CProjectile *pProj)
{
	if(pProj->m_Type != m_Type)
		return false;
	if(pProj->m_StartTick != m_StartTick)
		return false;
	if(distance(pProj->m_Pos, m_Pos) > 2.f)
		return false;
	if(distance(pProj->m_Direction, m_Direction) > 2.f)
		return false;
	return true;
}
