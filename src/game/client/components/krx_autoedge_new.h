#ifndef GAME_CLIENT_COMPONENTS_KRX_AUTOEDGE_NEW_H
#define GAME_CLIENT_COMPONENTS_KRX_AUTOEDGE_NEW_H

#include <game/client/component.h>
#include <generated/protocol.h>

#include <deque>
#include <vector>

class CCollision;

class CKrxAutoEdgeNew : public CComponent
{
public:
	struct CEdge
	{
		vec2 m_Position = vec2(0, 0);
		int m_Tile = -1;
		float m_Distance = 0;
	};
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnRender() override;
	void AutoEdge(CNetObj_PlayerInput &Input);
	static std::vector<CEdge> FindEdges(const CCollision *pCollision, vec2 Position, int Range, bool Freeze, bool Death, bool Tele);
	bool Locked() const { return m_Locked; }
	const CEdge &LockedEdge() const { return m_LockedEdge; }

private:
	std::vector<CEdge> m_vEdges;
	CEdge m_LockedEdge;
	bool m_Locked = false;
	bool m_HaveCachedInput = false;
	CNetObj_PlayerInput m_CachedInput = {};
	std::deque<CNetObj_PlayerInput> m_qPlan;
	int m_ReuseTicks = 0;
	int m_GroundedTicks = 0;
	void Circle(vec2 Position, float Radius, float R, float G, float B, float A, bool Filled);
};

#endif
