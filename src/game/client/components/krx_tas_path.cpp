#include "krx_tas.h"

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <queue>

namespace OpenKrx
{
	void CKrxTas::CancelPathFinding()
	{
		if(m_pPathCancel)
			m_pPathCancel->store(true);
		if(m_PathFuture.valid())
			m_PathFuture.get();
		m_pPathCancel.reset();
		m_PathDirty = true;
	}
	namespace
	{
		vec2 TileCenter(vec2 Position)
		{
			return vec2((int)(Position.x / 32.0f) * 32.0f + 16, (int)(Position.y / 32.0f) * 32.0f + 16);
		}
	}

	void CKrxTas::SetGoal(vec2 Goal)
	{
		m_Goal = TileCenter(Goal);
		m_PathDirty = true;
	}

	bool CKrxTas::PathTileBlocked(int X, int Y, const std::vector<unsigned char> &vLight) const
	{
		if(X < 0 || Y < 0 || X >= m_FlowField.Width() || Y >= m_FlowField.Height())
			return true;
		const int Index = Collision()->GetPureMapIndex(vec2(X * 32.0f, Y * 32.0f));
		const int Tile = Collision()->GetTileIndex(Index), Front = Collision()->GetFrontTileIndex(Index);
		const bool Freeze = (Tile == TILE_FREEZE || Front == TILE_FREEZE) &&
				    (!g_Config.m_KrxFentLightTile || !vLight[(size_t)Y * m_FlowField.Width() + X]);
		const auto Blocked = [](int T) { return T == TILE_SOLID || T == TILE_NOHOOK || T == TILE_DEATH || T == TILE_DFREEZE; };
		return Freeze || Blocked(Tile) || Blocked(Front) || Collision()->IsTeleport(Index) || Collision()->IsEvilTeleport(Index) ||
		       Collision()->IsCheckTeleport(Index) || Collision()->IsCheckEvilTeleport(Index) || m_vTunnels[(size_t)Y * m_FlowField.Width() + X];
	}

	void CKrxTas::UpdatePathFinding()
	{
		if(g_Config.m_AvoidAgentType != 3 && !g_Config.m_KrxPathFindingRenderFlowField && !g_Config.m_KrxPathFindingRenderTunnels &&
			!g_Config.m_KrxPathFindingTunnelMode && !g_Config.m_KrxPathFinding && !g_Config.m_KrxPathFindingClear &&
			!g_Config.m_KrxPathFindingTunnelClear && !g_Config.m_KrxPathFindingAutoTunnel)
			return;
		if(Client()->State() != IClient::STATE_ONLINE || !Collision()->GetWidth() || !Collision()->GetHeight())
			return;
		const int Width = Collision()->GetWidth(), Height = Collision()->GetHeight();
		if(m_LastLightTiles != g_Config.m_KrxFentLightTile || m_LastLightRadius != g_Config.m_KrxFentLightTileRadius)
		{
			m_PathDirty = true;
			m_LastLightTiles = g_Config.m_KrxFentLightTile;
			m_LastLightRadius = g_Config.m_KrxFentLightTileRadius;
		}
		if(Width != m_FlowField.Width() || Height != m_FlowField.Height())
			OnMapLoad();
		const auto Tile = [&](int X, int Y, int Kind) {
			const int Index = Collision()->GetPureMapIndex(vec2(X * 32.0f, Y * 32.0f));
			return Collision()->GetTileIndex(Index) == Kind || Collision()->GetFrontTileIndex(Index) == Kind;
		};
		if(g_Config.m_KrxPathFindingClear)
		{
			m_Goal = vec2(0, 0);
			m_NoFinish = false;
			m_PathDirty = true;
			g_Config.m_KrxPathFindingClear = 0;
		}
		if(g_Config.m_KrxPathFindingTunnelClear)
		{
			std::fill(m_vTunnels.begin(), m_vTunnels.end(), 0);
			m_PathDirty = true;
			g_Config.m_KrxPathFindingTunnelClear = 0;
		}
		if(g_Config.m_KrxPathFinding)
		{
			const int Slot = g_Config.m_ClDummy;
			const bool Spectating = GameClient()->m_Snap.m_SpecInfo.m_Active;
			const vec2 Pos = Spectating ? GameClient()->m_Snap.m_SpecInfo.m_Position : GameClient()->m_Controls.m_aTargetPos[Slot];
			SetGoal(Pos);
			g_Config.m_KrxPathFinding = 0;
		}
		if(g_Config.m_KrxPathFindingTunnelMode)
		{
			const int Slot = g_Config.m_ClDummy;
			const vec2 Brush = TileCenter(GameClient()->m_Snap.m_SpecInfo.m_Active ? GameClient()->m_Snap.m_SpecInfo.m_Position : GameClient()->m_Controls.m_aTargetPos[Slot]);
			const vec2 From((float)(int)(m_LastBrush.x / 32.0f), (float)(int)(m_LastBrush.y / 32.0f));
			const vec2 To((float)(int)(Brush.x / 32.0f), (float)(int)(Brush.y / 32.0f));
			const int Steps = std::max(1, (int)distance(From, To));
			for(int I = 0; I <= Steps; ++I)
			{
				const vec2 P = mix(From, To, (float)I / Steps);
				const int X = (int)P.x, Y = (int)P.y;
				if(X < 0 || Y < 0 || X >= Width || Y >= Height)
					continue;
				auto &Value = m_vTunnels[(size_t)Y * Width + X];
				if(Input()->KeyIsPressed(KEY_MOUSE_1) && !Collision()->IsSolid(X * 32, Y * 32) && !Tile(X, Y, TILE_FREEZE) && !Tile(X, Y, TILE_FINISH) && !Value)
				{
					Value = 1;
					m_PathDirty = true;
				}
				if(Input()->KeyIsPressed(KEY_MOUSE_2) && Value)
				{
					Value = 0;
					m_PathDirty = true;
				}
			}
			m_LastBrush = Brush;
		}
		if(g_Config.m_KrxPathFindingAutoTunnel && !m_Replay.m_aFrames[0].empty())
		{
			AutoTunnel();
			g_Config.m_KrxPathFindingAutoTunnel = 0;
			m_PathDirty = true;
		}
		if(m_Goal == vec2(0, 0))
		{
			if(m_NoFinish)
				return;
			// 2b3f30: row-major scan, at most 255 finishes, middle element.
			std::vector<vec2> vFinishes;
			for(int Y = 0; Y < Height && vFinishes.size() < 255; ++Y)
				for(int X = 0; X < Width && vFinishes.size() < 255; ++X)
					if(Tile(X, Y, TILE_FINISH))
						vFinishes.emplace_back(X * 32.0f, Y * 32.0f);
			if(vFinishes.empty())
			{
				m_NoFinish = true;
				return;
			}
			m_Goal = vFinishes[vFinishes.size() / 2];
		}
		if(m_PathFuture.valid())
		{
			if(m_PathFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			{
				if(m_PathDirty)
					m_pPathCancel->store(true);
				return;
			}
			auto Completed = m_PathFuture.get();
			if(!m_PathDirty)
				m_FlowField = std::move(Completed);
		}
		if(!m_PathDirty)
			return;
		std::vector<unsigned char> vLight((size_t)Width * Height, 0);
		if(g_Config.m_KrxFentLightTile)
			for(int Y = 0; Y < Height; ++Y)
				for(int X = 0; X < Width; ++X)
					if(Tile(X, Y, TILE_UNFREEZE))
						for(int Dy = -g_Config.m_KrxFentLightTileRadius; Dy <= g_Config.m_KrxFentLightTileRadius; ++Dy)
							for(int Dx = -g_Config.m_KrxFentLightTileRadius; Dx <= g_Config.m_KrxFentLightTileRadius; ++Dx)
								if(X + Dx >= 0 && Y + Dy >= 0 && X + Dx < Width && Y + Dy < Height)
									vLight[(size_t)(Y + Dy) * Width + X + Dx] = 1;
		std::vector<unsigned char> vBlocked((size_t)Width * Height), vFinishes(vBlocked.size());
		for(int Y = 0; Y < Height; ++Y)
			for(int X = 0; X < Width; ++X)
			{
				vBlocked[(size_t)Y * Width + X] = PathTileBlocked(X, Y, vLight);
				vFinishes[(size_t)Y * Width + X] = Collision()->GetTile(X * 32, Y * 32) == TILE_FINISH;
			}
		CFlowField Field;
		Field.Resize(Width, Height);
		m_pPathCancel = std::make_shared<std::atomic<bool>>(false);
		m_PathFuture = std::async(std::launch::async, [Field = std::move(Field), Goal = m_Goal, vBlocked = std::move(vBlocked), vFinishes = std::move(vFinishes), Cancel = m_pPathCancel]() mutable {
			Field.Build(Goal, vBlocked, vFinishes, Cancel.get());
			return std::move(Field);
		});
		m_PathDirty = false;
	}

	void CKrxTas::AutoTunnel()
	{
		const int Width = m_FlowField.Width(), Height = m_FlowField.Height();
		if(!Width || !Height)
			return;
		const auto OpenTile = [&](int X, int Y) {
			const int Index = Collision()->GetPureMapIndex(vec2(X * 32.0f, Y * 32.0f));
			return !Collision()->IsSolid(X * 32, Y * 32) && Collision()->GetTileIndex(Index) != TILE_FREEZE && Collision()->GetFrontTileIndex(Index) != TILE_FREEZE &&
			       Collision()->GetTileIndex(Index) != TILE_FINISH && Collision()->GetFrontTileIndex(Index) != TILE_FINISH;
		};
		// 2e1860 first masks the connected open area, then carves both runs.
		std::queue<int> Queue;
		std::vector<bool> vVisited((size_t)Width * Height);
		const vec2 Position = GameClient()->m_LocalCharacterPos;
		const int X = (int)(Position.x / 32.0f), Y = (int)(Position.y / 32.0f);
		if(X < 0 || Y < 0 || X >= Width || Y >= Height)
			return;
		Queue.push(Y * Width + X);
		while(!Queue.empty())
		{
			const int Index = Queue.front();
			Queue.pop();
			if(vVisited[Index])
				continue;
			vVisited[Index] = true;
			const int Cx = Index % Width, Cy = Index / Width;
			if(!OpenTile(Cx, Cy))
			{
				m_vTunnels[Index] = 0;
				continue;
			}
			m_vTunnels[Index] = 1;
			for(int Dx = -1; Dx <= 1; ++Dx)
				for(int Dy = -1; Dy <= 1; ++Dy)
					if((Dx || Dy) && Cx + Dx >= 0 && Cy + Dy >= 0 && Cx + Dx < Width && Cy + Dy < Height && !vVisited[(size_t)(Cy + Dy) * Width + Cx + Dx])
						Queue.push((Cy + Dy) * Width + Cx + Dx);
		}
		for(const auto &vFrames : m_Replay.m_aFrames)
			for(size_t I = 1; I < vFrames.size(); ++I)
			{
				const vec2 From = vFrames[I - 1].m_Position, To = vFrames[I].m_Position;
				const float Length = distance(From, To);
				if(Length == 0)
					continue;
				const vec2 Direction = (To - From) / Length;
				const vec2 Normal(-Direction.y, Direction.x);
				const int HalfWidth = g_Config.m_KrxPathFindingAutoTunnelWidth * 16;
				for(float Along = 0; Along <= Length; Along += 1.0f)
					for(int Across = -HalfWidth; Across <= HalfWidth; ++Across)
					{
						const vec2 P = From + Direction * Along + Normal * (float)Across;
						const int Tx = (int)(P.x / 32.0f), Ty = (int)(P.y / 32.0f);
						if(Tx >= 0 && Ty >= 0 && Tx < Width && Ty < Height && !Collision()->IsSolid((int)P.x, (int)P.y) && Collision()->GetTile((int)P.x, (int)P.y) != TILE_FREEZE)
							m_vTunnels[(size_t)Ty * Width + Tx] = 0;
					}
			}
	}

	void CKrxTas::RenderFlowField()
	{
		if(!m_FlowField.Ready())
			return;
		const CScreenRect Screen = Graphics()->GetScreen();
		const float X0 = Screen.m_TopLeft.x, Y0 = Screen.m_TopLeft.y, X1 = Screen.m_BottomRight.x, Y1 = Screen.m_BottomRight.y;
		const int MinX = std::max(0, (int)(X0 / 32.0f)), MinY = std::max(0, (int)(Y0 / 32.0f));
		const int MaxX = std::min(m_FlowField.Width() - 1, (int)(X1 / 32.0f)), MaxY = std::min(m_FlowField.Height() - 1, (int)(Y1 / 32.0f));
		if(g_Config.m_KrxPathFindingRenderTunnels || g_Config.m_KrxPathFindingTunnelMode)
		{
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1, .41f, .7f, 1);
			for(int Y = MinY; Y <= MaxY; ++Y)
				for(int X = MinX; X <= MaxX; ++X)
					if(m_vTunnels[(size_t)Y * m_FlowField.Width() + X])
					{
						IGraphics::CQuadItem Quad(X * 32.0f, Y * 32.0f, 32, 32);
						Graphics()->QuadsDrawTL(&Quad, 1);
					}
			Graphics()->QuadsEnd();
		}
		if(g_Config.m_KrxPathFindingRenderFlowField)
			for(int Y = MinY; Y <= MaxY; ++Y)
				for(int X = MinX; X <= MaxX; ++X)
				{
					const auto *pCell = m_FlowField.At(X, Y);
					if(!pCell || !pCell->Reachable())
						continue;
					const vec2 Center(X * 32.0f + 16, Y * 32.0f + 16);
					const vec2 Vector = pCell->m_Flow * 16.0f;
					const ColorRGBA White(1, 1, 1, 1);
					Line(Center, Center + Vector, White, 1);
					// 2dffd0 draws its two 20% side bars at +/- pi/4.
					for(const float Angle : {0.7853981852531433f, -0.7853981852531433f})
					{
						const float Sine = std::sin(Angle), Cosine = std::cos(Angle);
						Line(Center, Center + vec2(Cosine * Vector.x - Sine * Vector.y, Cosine * Vector.y + Sine * Vector.x) * .2f, White, 1);
					}
				}
		if(g_Config.m_KrxPathFindingTunnelMode)
			Circle(m_LastBrush, 5, ColorRGBA(1, 1, 1, 1));
	}
}
