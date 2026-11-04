#include "krx_tas.h"

#include <game/client/krx/tile_editor.h>

#include <base/str.h>

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
#include <numeric>
#include <random>

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
		TileEditor::ClearKind(m_vTunnels, TileEditor::FINISH);
		TileEditor::Paint(m_vTunnels, m_FlowField.Width(), m_FlowField.Height(), (int)(Goal.x / 32.0f), (int)(Goal.y / 32.0f), TileEditor::FINISH);
		m_PathDirty = true;
	}

	bool CKrxTas::PathTileBlocked(int X, int Y, const std::vector<unsigned char> &vLight) const
	{
		if(X < 0 || Y < 0 || X >= m_FlowField.Width() || Y >= m_FlowField.Height())
			return true;
		const int Index = Collision()->GetPureMapIndex(vec2(X * 32.0f, Y * 32.0f));
		const int Tile = Collision()->GetTileIndex(Index), Front = Collision()->GetFrontTileIndex(Index);
		(void)vLight;
		const bool Tele = Collision()->IsTeleport(Index) || Collision()->IsEvilTeleport(Index) ||
			Collision()->IsCheckTeleport(Index) || Collision()->IsCheckEvilTeleport(Index);
		// 3410e0: a tele-in explicitly selected as a finish is traversable.
		if(Tele && (m_vTunnels[(size_t)Y * m_FlowField.Width() + X] & TileEditor::FINISH))
			return false;
		// New KRX's light-tile branch calls both getters but does not consume
		// their results before returning blocked. Preserve the observed result.
		const bool Freeze = Tile == TILE_FREEZE || Front == TILE_FREEZE;
		const auto Blocked = [](int T) { return T == TILE_SOLID || T == TILE_NOHOOK || T == TILE_DEATH || T == TILE_DFREEZE; };
		return Freeze || Blocked(Tile) || Blocked(Front) || Tele || (m_vTunnels[(size_t)Y * m_FlowField.Width() + X] & TileEditor::TUNNEL);
	}

	void CKrxTas::UpdatePathFinding()
	{
		if(g_Config.m_AvoidAgentType != 3 && g_Config.m_AvoidAgentType != 4 && !g_Config.m_KrxPathFindingRenderFlowField &&
			!g_Config.m_KrxTileEditorEnable && !g_Config.m_KrxPathFinding && !g_Config.m_KrxPathFindingClear &&
			!g_Config.m_KrxTileEditorClear && !g_Config.m_KrxTileEditorAutoTunnel && !g_Config.m_KrxTileEditorAutoFinish &&
			!g_Config.m_KrxFlowFieldBenchmark && !g_Config.m_KrxPathFindingTunnelMode && !g_Config.m_KrxPathFindingRenderTunnels &&
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
		// 345bc0 handles one-shot editor requests independently of editor enable.
		if(g_Config.m_KrxTileEditorClear || g_Config.m_KrxPathFindingTunnelClear)
		{
			std::fill(m_vTunnels.begin(), m_vTunnels.end(), 0);
			g_Config.m_KrxTileEditorClear = g_Config.m_KrxPathFindingTunnelClear = 0;
		}
		if(g_Config.m_KrxTileEditorAutoFinish)
		{
			AutoFinish();
			g_Config.m_KrxTileEditorAutoFinish = 0;
		}
		if(g_Config.m_KrxTileEditorAutoTunnel || g_Config.m_KrxPathFindingAutoTunnel)
		{
			AutoTunnel();
			g_Config.m_KrxTileEditorAutoTunnel = g_Config.m_KrxPathFindingAutoTunnel = 0;
			m_PathDirty = true;
		}
		if(g_Config.m_KrxPathFinding || g_Config.m_KrxPathFindingClear)
		{
			// New KRX recalculates from editor finishes; this is no longer a mouse goal.
			m_PathDirty = true;
			m_NoFinish = false;
			g_Config.m_KrxPathFinding = g_Config.m_KrxPathFindingClear = 0;
		}
		if(g_Config.m_KrxTileEditorEnable || g_Config.m_KrxPathFindingTunnelMode)
		{
			const int Slot = g_Config.m_ClDummy;
			const vec2 Brush = TileCenter(GameClient()->m_Snap.m_SpecInfo.m_Active ? GameClient()->m_Snap.m_SpecInfo.m_Position : GameClient()->m_Controls.m_aTargetPos[Slot]);
			if(!m_EditorBrushValid)
			{
				m_LastBrush = Brush;
				m_EditorBrushValid = true;
			}
			const vec2 From((float)(int)(m_LastBrush.x / 32.0f), (float)(int)(m_LastBrush.y / 32.0f));
			const vec2 To((float)(int)(Brush.x / 32.0f), (float)(int)(Brush.y / 32.0f));
			if(Input()->KeyIsPressed(KEY_MOUSE_1) || Input()->KeyIsPressed(KEY_MOUSE_2))
			{
				const int Steps = std::max(1, (int)distance(From, To));
				for(int I = 0; I <= Steps; ++I)
				{
					const vec2 P = mix(From, To, (float)I / Steps);
					const unsigned char Kind = Input()->KeyIsPressed(KEY_MOUSE_1) ?
						(g_Config.m_KrxTileEditorType == 1 ? TileEditor::FINISH : TileEditor::TUNNEL) : 0;
					TileEditor::Paint(m_vTunnels, Width, Height, (int)P.x, (int)P.y, Kind);
				}
			}
			m_LastBrush = Brush;
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
				m_FlowField = std::move(Completed.m_Field);
			if(!Completed.m_vBenchmarkUs.empty())
			{
				const auto &vTimes = Completed.m_vBenchmarkUs;
				const double Mean = std::accumulate(vTimes.begin(), vTimes.end(), 0.0) / vTimes.size();
				double Variance = 0;
				for(double Value : vTimes)
					Variance += (Value - Mean) * (Value - Mean);
				const double Deviation = vTimes.size() > 1 ? std::sqrt(Variance / (vTimes.size() - 1)) : 0;
				char aMessage[256];
				str_format(aMessage, sizeof(aMessage), "FlowField: %d full map builds; average %.3f ms, standard deviation %.3f ms, min %.3f ms, max %.3f ms.", (int)vTimes.size(), Mean / 1000, Deviation / 1000, *std::min_element(vTimes.begin(), vTimes.end()) / 1000, *std::max_element(vTimes.begin(), vTimes.end()) / 1000);
				Message(aMessage);
			}
		}
		if(!m_PathDirty && !g_Config.m_KrxFlowFieldBenchmark)
			return;
		auto vGoals = TileEditor::Goals(m_vTunnels, Width, Height);
		if(vGoals.empty() && !m_NoFinish)
		{
			AutoFinish();
			vGoals = TileEditor::Goals(m_vTunnels, Width, Height);
			m_NoFinish = vGoals.empty();
		}
		if(vGoals.empty())
			return;
		m_Goal = vGoals.front();
		const std::vector<unsigned char> vLight; // New predicate does not consume a light mask.
		std::vector<unsigned char> vBlocked((size_t)Width * Height), vFinishes(vBlocked.size());
		for(int Y = 0; Y < Height; ++Y)
			for(int X = 0; X < Width; ++X)
			{
				vBlocked[(size_t)Y * Width + X] = PathTileBlocked(X, Y, vLight);
				// 341500 skips raw game-layer finish tiles in the Gaussian pass.
				vFinishes[(size_t)Y * Width + X] = Collision()->GetTileIndex(Collision()->GetPureMapIndex(vec2(X * 32.0f, Y * 32.0f))) == TILE_FINISH;
			}
		CFlowField Field;
		Field.Resize(Width, Height);
		std::vector<vec2> vMapFinishes;
		for(int Y = 0; Y < Height; ++Y)
			for(int X = 0; X < Width; ++X)
				if(Tile(X, Y, TILE_FINISH))
					vMapFinishes.emplace_back((float)X, (float)Y);
		m_pPathCancel = std::make_shared<std::atomic<bool>>(false);
		const int BenchmarkRuns = g_Config.m_KrxFlowFieldBenchmark ? std::clamp(g_Config.m_KrxFlowFieldBenchmarkRuns, 1, 100) : 0;
		g_Config.m_KrxFlowFieldBenchmark = 0;
		m_PathFuture = std::async(std::launch::async, [BenchmarkRuns, Field = std::move(Field), vGoals = std::move(vGoals), vMapFinishes = std::move(vMapFinishes), vBlocked = std::move(vBlocked), vFinishes = std::move(vFinishes), Cancel = m_pPathCancel]() mutable {
			CPathResult Result;
			std::mt19937 Random(std::random_device{}());
			// 3427e0 measures complete field builds rather than network traffic.
			for(int Run = 0; Run < BenchmarkRuns && !Cancel->load() && !vMapFinishes.empty(); ++Run)
			{
				CFlowField Probe;
				Probe.Resize(Field.Width(), Field.Height());
				const vec2 Goal = vMapFinishes[CFent::Uniform(Random, (uint32_t)vMapFinishes.size())];
				// 342350 passes the randomly selected map tile coordinate unchanged.
				const auto Start = std::chrono::steady_clock::now();
				Probe.BuildGoals({Goal}, vBlocked, vFinishes, Cancel.get());
				const auto End = std::chrono::steady_clock::now();
				Result.m_vBenchmarkUs.push_back((double)std::chrono::duration_cast<std::chrono::microseconds>(End - Start).count());
			}
			Field.BuildGoals(vGoals, vBlocked, vFinishes, Cancel.get());
			Result.m_Field = std::move(Field);
			return Result;
		});
		m_PathDirty = false;
	}

	void CKrxTas::AutoFinish()
	{
		TileEditor::ClearKind(m_vTunnels, TileEditor::FINISH);
		const int Width = m_FlowField.Width(), Height = m_FlowField.Height();
		for(int Y = 0; Y < Height; ++Y)
			for(int X = 0; X < Width; ++X)
			{
				const int Index = Collision()->GetPureMapIndex(vec2(X * 32.0f, Y * 32.0f));
				if(Collision()->GetTileIndex(Index) == TILE_FINISH || Collision()->GetFrontTileIndex(Index) == TILE_FINISH)
					TileEditor::Paint(m_vTunnels, Width, Height, X, Y, TileEditor::FINISH);
			}
		m_NoFinish = false;
		m_PathDirty = true;
	}

	void CKrxTas::AutoTunnel()
	{
		const int Width = m_FlowField.Width(), Height = m_FlowField.Height();
		if(!Width || !Height)
			return;
		if(m_Replay.m_aFrames[0].empty())
		{
			Message("Empty replay for automatic Tunnel.");
			return;
		}
		TileEditor::ClearKind(m_vTunnels, TileEditor::TUNNEL);
		const auto OpenTile = [&](int X, int Y) {
			const int Index = Collision()->GetPureMapIndex(vec2(X * 32.0f, Y * 32.0f));
			return !Collision()->IsSolid(X * 32, Y * 32) && Collision()->GetTileIndex(Index) != TILE_FREEZE && Collision()->GetFrontTileIndex(Index) != TILE_FREEZE &&
			       Collision()->GetTileIndex(Index) != TILE_FINISH && Collision()->GetFrontTileIndex(Index) != TILE_FINISH;
		};
		// 2e1860 first masks the connected open area, then carves both runs.
		std::queue<int> Queue;
		std::vector<bool> vVisited((size_t)Width * Height);
		CGameWorld &Source = Active() ? *m_pWorld : GameClient()->m_GameWorld;
		const auto *pCharacter = Source.GetCharacterById(GameClient()->m_aLocalIds[g_Config.m_ClDummy]);
		const vec2 Position = pCharacter ? pCharacter->Core()->m_Pos : vec2(-32, -32);
		const int X = (int)(Position.x / 32.0f), Y = (int)(Position.y / 32.0f);
		if(X >= 0 && Y >= 0 && X < Width && Y < Height)
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
				m_vTunnels[Index] &= ~TileEditor::TUNNEL;
				continue;
			}
			m_vTunnels[Index] = TileEditor::TUNNEL;
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
				const int HalfWidth = g_Config.m_KrxTileEditorAutoTunnelWidth * 16;
				for(float Along = 0; Along <= Length; Along += 8.0f)
					for(int Across = -HalfWidth; Across <= HalfWidth; Across += 8)
					{
						const vec2 P = From + Direction * Along + Normal * (float)Across;
						const int Tx = (int)(P.x / 32.0f), Ty = (int)(P.y / 32.0f);
						if(Tx >= 0 && Ty >= 0 && Tx < Width && Ty < Height)
							m_vTunnels[(size_t)Ty * Width + Tx] &= ~TileEditor::TUNNEL;
					}
			}
	}

	void CKrxTas::RenderFlowField()
	{
		const CScreenRect Screen = Graphics()->GetScreen();
		const float X0 = Screen.m_TopLeft.x, Y0 = Screen.m_TopLeft.y, X1 = Screen.m_BottomRight.x, Y1 = Screen.m_BottomRight.y;
		const int MinX = std::max(0, (int)(X0 / 32.0f)), MinY = std::max(0, (int)(Y0 / 32.0f));
		const int MaxX = std::min(m_FlowField.Width() - 1, (int)(X1 / 32.0f)), MaxY = std::min(m_FlowField.Height() - 1, (int)(Y1 / 32.0f));
		if(g_Config.m_KrxTileEditorEnable || g_Config.m_KrxPathFindingRenderTunnels || g_Config.m_KrxPathFindingTunnelMode)
		{
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
						for(int Y = MinY; Y <= MaxY; ++Y)
				for(int X = MinX; X <= MaxX; ++X)
					if(m_vTunnels[(size_t)Y * m_FlowField.Width() + X])
					{
						const bool Finish = (m_vTunnels[(size_t)Y * m_FlowField.Width() + X] & TileEditor::FINISH) != 0;
						Graphics()->SetColor(1, Finish ? 1.0f : .41f, Finish ? 0.0f : .7f, 1);
						IGraphics::CQuadItem Quad(X * 32.0f, Y * 32.0f, 32, 32);
						Graphics()->QuadsDrawTL(&Quad, 1);
					}
			Graphics()->QuadsEnd();
		}
		if(!m_FlowField.Ready())
			return;
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
