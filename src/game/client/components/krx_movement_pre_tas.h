#ifndef GAME_CLIENT_COMPONENTS_KRX_MOVEMENT_PRE_TAS_H
#define GAME_CLIENT_COMPONENTS_KRX_MOVEMENT_PRE_TAS_H

#include <base/vmath.h>

#include <generated/protocol.h>

#include <game/client/component.h>

class CKrxMovementPreTas : public CComponent
{
	CNetObj_PlayerInput m_FastInput = {};
	int m_GhostLastTick = 1;
	bool m_GhostToggle = true;
	vec2 m_GhostSavedAim = vec2(0, 0);
	vec2 m_EdgeTarget = vec2(0, 0);
	int m_EdgeTile = -1;
	bool m_TunesStarted = false;
	vec2 m_TunesExplosion = vec2(0, 0);
	int m_TunesStartTick = 0;
	int m_TunesEndTick = 0;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	// Original controls23e070: refreshes raw local input between network ticks.
	bool UpdateFastInput();
	const CNetObj_PlayerInput &FastInput() const { return m_FastInput; }
	void AutoEdge(CNetObj_PlayerInput &Input);
	void GhostMove(CNetObj_PlayerInput &Input);
	void FindTunes();
	// Original Items snapshot loop records the first visible network projectile
	// of any weapon; ProcessEvents records every authoritative explosion.
	void ObserveProjectile(int StartTick);
	void ObserveExplosion(vec2 Position, int WorldTick);
};

#endif
