/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_PREDICTION_ENTITIES_LASER_H
#define GAME_CLIENT_PREDICTION_ENTITIES_LASER_H

#include <game/client/prediction/entity.h>

class CLaserData;

class CLaser : public CEntity
{
	friend class CGameWorld;

public:
	CLaser(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int Type);

	void Tick() override;

	const vec2 &GetFrom() const { return m_From; }
	const int &GetOwner() const { return m_Owner; }
	const int &GetEvalTick() const { return m_EvalTick; }
	int GetBounces() const { return m_Bounces; }
	float GetEnergy() const { return m_Energy; }
	int GetHitClientId() const { return m_HitClientId; }
	int GetWeaponType() const { return m_Type; }
	void SetHitClientIdObserver(int *pResult)
	{
		m_pHitClientIdObserver = pResult;
		if(pResult && m_HitClientId >= 0)
			*pResult = m_HitClientId;
	}
	CLaser(CGameWorld *pGameWorld, int Id, const CLaserData *pLaser);
	bool Match(CLaser *pLaser);
	CLaserData GetData() const;

protected:
	bool HitCharacter(vec2 From, vec2 To);
	void DoBounce();

private:
	vec2 m_From;
	vec2 m_Dir;
	float m_Energy;
	int m_Bounces;
	// Result metadata for isolated KRX weapon probes; this does not alter hits.
	int m_HitClientId = -1;
	int *m_pHitClientIdObserver = nullptr;
	int m_EvalTick;
	int m_Owner;
	bool m_ZeroEnergyBounceInLastTick;

	// DDRace

	vec2 m_PrevPos;
	int m_Type;
	int m_TuneZone;
};

#endif
