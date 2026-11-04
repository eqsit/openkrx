#include "krx_autoedge_new.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/avoid/pilot_bot.h>
#include <game/client/avoid/rocket_save.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <queue>

namespace
{
bool TeleAt(const CCollision *pCollision, int Index)
{
	return pCollision->IsTeleport(Index) || pCollision->IsEvilTeleport(Index) || pCollision->IsCheckTeleport(Index) || pCollision->IsCheckEvilTeleport(Index);
}
}

std::vector<CKrxAutoEdgeNew::CEdge> CKrxAutoEdgeNew::FindEdges(const CCollision *pCollision, vec2 Position, int Range, bool Freeze, bool Death, bool Tele)
{
	const int Width = pCollision->GetWidth(), Height = pCollision->GetHeight();
	if(Width <= 0 || Height <= 0)
		return {};
	const int Start = pCollision->GetPureMapIndex(Position);
	std::vector<unsigned char> vVisited((size_t)Width * Height, 0);
	std::queue<int> Queue;
	Queue.push(Start);
	vVisited[Start] = 1;
	float Nearest = (float)Range + 1;
	CEdge Best;
	const auto Matches = [&](int Index, int Tile) {
		return Tile == TILE_TELEIN ? TeleAt(pCollision, Index) : pCollision->GetTileIndex(Index) == Tile;
	};
	const auto Solid = [&](vec2 Point) { return pCollision->IsSolid((int)Point.x, (int)Point.y); };
	while(!Queue.empty())
	{
		const int Index = Queue.front();
		Queue.pop();
		const vec2 Center = pCollision->GetPos(Index);
		for(const int Tile : {TILE_FREEZE, TILE_DEATH, TILE_TELEIN})
		{
			if((Tile == TILE_FREEZE && !Freeze) || (Tile == TILE_DEATH && !Death) || (Tile == TILE_TELEIN && !Tele) || !Matches(Index, Tile))
				continue;
			const float Distance = distance(Position, Center);
			if(Distance > Range || Position.y > Center.y)
				continue;
			const vec2 Edge = Center + vec2(Position.x < Center.x ? -25.0f : 25.0f, 0);
			if(Matches(pCollision->GetPureMapIndex(Edge), Tile) || Solid(Edge))
				continue;
			if(!Solid(Center + vec2(0, 32)) && !Solid(Edge + vec2(0, 32)))
				continue;
			if(Distance < Nearest)
			{
				Nearest = Distance;
				Best = {Edge, Tile, Distance};
			}
		}
		const int X = Index % Width, Y = Index / Width;
		// 316e90 enqueues all eight neighbors, Dx outer / Dy inner; no diagonal
		// corner-cut check. A solid cell is never enqueued.
		for(int Dx = -1; Dx <= 1; ++Dx)
			for(int Dy = -1; Dy <= 1; ++Dy)
			{
				if((Dx == 0 && Dy == 0) || X + Dx < 0 || Y + Dy < 0 || X + Dx >= Width || Y + Dy >= Height)
					continue;
				const int Next = (Y + Dy) * Width + X + Dx;
				if(vVisited[Next] || distance(Position, pCollision->GetPos(Next)) > Range)
					continue;
				const int Tile = pCollision->GetTileIndex(Next);
				if(Tile == TILE_SOLID || Tile == TILE_NOHOOK)
					continue;
				vVisited[Next] = 1;
				Queue.push(Next);
			}
	}
	// Despite the vector name the original BFS publishes only the nearest edge.
	return Best.m_Tile < 0 ? std::vector<CEdge>{} : std::vector<CEdge>{Best};
}

void CKrxAutoEdgeNew::OnReset()
{
	m_Locked = false;
	m_LockedEdge = {};
	m_GroundedTicks = 0;
	// The original main gate clears only this lock/record/counter. Its
	// vectors and function-static cached input survive reconnect/disable.
}

void CKrxAutoEdgeNew::AutoEdge(CNetObj_PlayerInput &Input)
{
	const int Id = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(!g_Config.m_KrxAutoEdge || Id < 0 || Id >= MAX_CLIENTS || !(Input.m_PlayerFlags & PLAYERFLAG_PLAYING) || Client()->State() != IClient::STATE_ONLINE || GameClient()->m_Snap.m_SpecInfo.m_Active || !GameClient()->m_Snap.m_pLocalCharacter || OpenKrx::IsKrxCharacterFrozen(GameClient(), Id))
	{
		// 3181e0's transient/off gate clears lock and edge record, not the
		// cached vector/input. A new lock may reuse its remaining plan.
		m_Locked = false;
		m_LockedEdge = {};
		m_GroundedTicks = 0;
		return;
	}
	auto *pLive = GameClient()->KrxPredictedWorld().GetCharacterById(Id);
	if(!pLive)
		return;
	if(pLive->IsGrounded() && pLive->Core()->m_Vel.y == 0.0f)
	{
		++m_GroundedTicks;
		m_Locked = false;
		m_qPlan.clear();
		m_ReuseTicks = 0;
		Input.m_Direction = 0;
		m_HaveCachedInput = false;
		return;
	}
	m_GroundedTicks = 0;
	if(!m_Locked)
	{
		m_vEdges = FindEdges(Collision(), pLive->m_Pos, g_Config.m_KrxAutoEdgeDetectionRange, g_Config.m_KrxAutoEdgeDetectFreeze != 0, g_Config.m_KrxAutoEdgeDetectDeath != 0, g_Config.m_KrxAutoEdgeDetectTele != 0);
		if(m_vEdges.empty())
			return;
		m_LockedEdge = m_vEdges.front();
		m_Locked = true;
	}
	CGameWorld World;
	OpenKrx::CMovementSequenceSearch::PrepareWorld(GameClient(), World, Id);
	if(!World.GetCharacterById(Id))
		return;
	if(!m_HaveCachedInput)
	{
		m_CachedInput = Input;
		m_HaveCachedInput = true;
	}
	if(m_ReuseTicks > 0 && !m_qPlan.empty())
	{
		Input = m_qPlan.front();
		m_qPlan.pop_front();
		--m_ReuseTicks;
		return;
	}
	OpenKrx::CMovementSequenceSearch::CContext Context;
	Context.m_pWorld = &World;
	Context.m_Id = Id;
	Context.m_Edge = true;
	Context.m_Target = m_LockedEdge.m_Position;
	auto vCandidates = OpenKrx::CMovementSequenceSearch::Search(Context, m_CachedInput, g_Config.m_KrxAutoEdgePopulationSize, g_Config.m_KrxAutoEdgeExplorationDepth, GameClient()->Engine());
	const int TopK = std::min((int)vCandidates.size(), g_Config.m_KrxAutoEdgeTopKCandidates);
	for(int I = 0; I < TopK; ++I)
		if(OpenKrx::CMovementSequenceSearch::Validate(Context, vCandidates[I].m_vInputs))
		{
			m_qPlan.assign(vCandidates[I].m_vInputs.begin(), vCandidates[I].m_vInputs.end());
			m_ReuseTicks = g_Config.m_KrxAutoEdgeSequenceLength;
			Input = m_qPlan.front();
			m_CachedInput = Input;
			m_qPlan.pop_front();
			--m_ReuseTicks;
			return;
		}
	if(!vCandidates.empty())
		m_CachedInput = Input = vCandidates.front().m_vInputs.front();
}

void CKrxAutoEdgeNew::Circle(vec2 Position, float Radius, float R, float G, float B, float A, bool Filled)
{
	Graphics()->TextureClear();
	vec2 Previous = Position + vec2(std::cos(-0.1f), std::sin(-0.1f)) * Radius;
	for(float Angle = 0; Angle < 2 * pi; Angle += 0.1f)
	{
		const vec2 Next = Position + vec2(std::cos(Angle), std::sin(Angle)) * Radius;
		const vec2 Delta = Next - Previous;
		if(length(Delta) >= 1)
		{
			const vec2 Perp = vec2(Delta.y, -Delta.x) / length(Delta) * GameClient()->m_Camera.m_Zoom * 0.75f;
			Graphics()->QuadsBegin();
			Graphics()->SetColor(R, G, B, A);
			IGraphics::CFreeformItem Quad(Next - Perp, Next + Perp, Previous - Perp, Previous + Perp);
			Graphics()->QuadsDrawFreeform(&Quad, 1);
			Graphics()->QuadsEnd();
		}
		Previous = Next;
	}
	if(Filled)
	{
		Graphics()->QuadsBegin();
		Graphics()->SetColor(R, G, B, 0.5f);
		Graphics()->DrawCircle(Position.x, Position.y, Radius, 32);
		Graphics()->QuadsEnd();
	}
}

void CKrxAutoEdgeNew::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	const CScreenRect Old = Graphics()->GetScreen();
	const auto &Camera = GameClient()->m_Camera;
	Graphics()->MapScreen(Graphics()->MapScreenToWorld(Camera.m_Center.x, Camera.m_Center.y, 100, 100, 100, 0, 0, Graphics()->ScreenAspect(), Camera.m_Zoom));
	if(g_Config.m_KrxAutoEdgeShowFoundEdges)
		for(const auto &Edge : m_vEdges)
			Circle(Edge.m_Position, 5, 0, 1, 0, 0.8f, true);
	if(g_Config.m_KrxAutoEdgeShowLockedEdge && m_Locked)
	{
		Circle(m_LockedEdge.m_Position, 8, 1, 0, 0, 1, true);
		Circle(m_LockedEdge.m_Position, 12, 1, 0, 0, 0.5f, false);
	}
	// krx_autoedge_show_visited_tiles has only registration xrefs in this
	// shipped binary. The original has no visited-tile rendering consumer.
	Graphics()->MapScreen(Old);
}
