#include "flow_field.h"

#include <algorithm>
#include <cmath>

namespace OpenKrx
{
	void CFlowField::Resize(int Width, int Height)
	{
		m_Width = std::max(0, Width);
		m_Height = std::max(0, Height);
		m_vCells.assign((size_t)m_Width * m_Height, {});
		m_vBlocked.assign(m_vCells.size(), 0);
		m_Goal = vec2(0, 0);
		m_vGoals.clear();
		m_Ready = false;
	}

	const CFlowField::CCell *CFlowField::At(int X, int Y) const
	{
		return Inside(X, Y) ? &m_vCells[(size_t)Y * m_Width + X] : nullptr;
	}

	float CFlowField::DistanceToGoal(vec2 Position) const
	{
		float Result = FLT_MAX;
		for(const vec2 Goal : m_vGoals)
			Result = std::min(Result, distance(Position, Goal));
		return Result;
	}

	float CFlowField::Distance(int A, int B) const
	{
		const float X = (float)(A % m_Width - B % m_Width);
		const float Y = (float)(A / m_Width - B / m_Width);
		return std::sqrt(X * X + Y * Y);
	}

	bool CFlowField::LineOfSight(int From, int To) const
	{
		if(From < 0 || To < 0 || (size_t)From >= m_vCells.size() || (size_t)To >= m_vCells.size())
			return false;
		int X = From % m_Width, Y = From / m_Width;
		const int EndX = To % m_Width, EndY = To / m_Width;
		const int Dx = std::abs(EndX - X), Dy = std::abs(EndY - Y);
		const int Sx = X < EndX ? 1 : -1, Sy = Y < EndY ? 1 : -1;
		int Error = 2 * (Dx - Dy);
		for(;;)
		{
			// Original excludes the endpoint from this walk; Build checks it.
			if(X == EndX && Y == EndY)
				return true;
			if(!Inside(X, Y) || m_vBlocked[(size_t)Y * m_Width + X])
				return false;
			const int PreviousError = Error;
			if(PreviousError > -Dy)
			{
				Error -= 2 * Dy;
				X += Sx;
			}
			if(PreviousError < Dx)
			{
				Error += 2 * Dx;
				Y += Sy;
			}
		}
	}

	bool CFlowField::Build(vec2 Goal, const std::vector<unsigned char> &vBlocked, const std::vector<unsigned char> &vFinishes, const std::atomic<bool> *pCancel)
	{
		if(Goal == vec2(0, 0))
			return m_Ready = false;
		return BuildGoals({Goal}, vBlocked, vFinishes, pCancel);
	}

	bool CFlowField::BuildGoals(const std::vector<vec2> &vGoals, const std::vector<unsigned char> &vBlocked, const std::vector<unsigned char> &vFinishes, const std::atomic<bool> *pCancel)
	{
		m_Ready = false;
		if(vGoals.empty() || vBlocked.size() != m_vCells.size() || m_vCells.empty())
			return false;
		m_Goal = vGoals.front();
		m_vGoals = vGoals;
		m_vBlocked = vBlocked;
		std::fill(m_vCells.begin(), m_vCells.end(), CCell{});
		struct COpen
		{
			int m_Index;
			float m_Cost;
		};
		const auto Compare = [](const COpen &A, const COpen &B) { return A.m_Cost > B.m_Cost; };
		std::vector<COpen> vOpen;
		for(const vec2 Goal : vGoals)
		{
			const int Gx = (int)(Goal.x / 32.0f), Gy = (int)(Goal.y / 32.0f);
			if(!Inside(Gx, Gy))
				continue;
			const int End = Gy * m_Width + Gx;
			m_vCells[End].m_Cost = 0;
			m_vCells[End].m_Parent = End;
			vOpen.push_back({End, 0});
			std::push_heap(vOpen.begin(), vOpen.end(), Compare);
		}
		if(vOpen.empty())
			return false;
		while(!vOpen.empty())
		{
			if(pCancel && pCancel->load(std::memory_order_relaxed))
				return false;
			const int Current = vOpen.front().m_Index;
			std::pop_heap(vOpen.begin(), vOpen.end(), Compare);
			vOpen.pop_back();
			auto &Cell = m_vCells[Current];
			if(Cell.m_Closed)
				continue;
			Cell.m_Closed = true;
			if(m_vBlocked[Current])
			{
				Cell.m_Cost = FLT_MAX;
				continue;
			}
			for(int Dx = -1; Dx <= 1; ++Dx)
				for(int Dy = -1; Dy <= 1; ++Dy)
				{
					if(Dx == 0 && Dy == 0)
						continue;
					const int X = Current % m_Width + Dx, Y = Current / m_Width + Dy;
					if(!Inside(X, Y))
						continue;
					const int Next = Y * m_Width + X;
					auto &Neighbor = m_vCells[Next];
					if(m_vBlocked[Next])
					{
						Neighbor.m_Cost = FLT_MAX;
						Neighbor.m_Closed = true;
						continue;
					}
					const int Parent = Cell.m_Parent == Current || LineOfSight(Cell.m_Parent, Next) ? Cell.m_Parent : Current;
					const float Cost = m_vCells[Parent].m_Cost + Distance(Parent, Next);
					if(Cost < Neighbor.m_Cost)
					{
						Neighbor.m_Cost = Cost;
						Neighbor.m_Parent = Parent;
						vOpen.push_back({Next, Cost});
						std::push_heap(vOpen.begin(), vOpen.end(), Compare);
					}
				}
		}
		for(size_t I = 0; I < m_vCells.size(); ++I)
		{
			auto &Cell = m_vCells[I];
			if(!Cell.Reachable() || Cell.m_Parent < 0)
				continue;
			const vec2 Delta((float)(Cell.m_Parent % m_Width - (int)I % m_Width), (float)(Cell.m_Parent / m_Width - (int)I / m_Width));
			Cell.m_Flow = Delta == vec2(0, 0) ? vec2(0, 0) : -normalize(Delta);
		}
		// 2df660 calls 2e0a20: one out-of-place Gaussian pass, followed by
		// normalization. Costs/parents stay untouched; Fent scores this field.
		const auto vRaw = m_vCells;
		const float aWeights[3][3] = {{.0625f, .125f, .0625f}, {.125f, .25f, .125f}, {.0625f, .125f, .0625f}};
		for(int Y = 0; Y < m_Height; ++Y)
			for(int X = 0; X < m_Width; ++X)
			{
				if(pCancel && pCancel->load(std::memory_order_relaxed))
					return false;
				auto &Cell = m_vCells[(size_t)Y * m_Width + X];
				if(!Cell.Reachable())
					continue;
				vec2 Sum(0, 0);
				float Weight = 0;
				for(int Dy = -1; Dy <= 1; ++Dy)
					for(int Dx = -1; Dx <= 1; ++Dx)
					{
						if(!Inside(X + Dx, Y + Dy))
							continue;
						const size_t Index = (size_t)(Y + Dy) * m_Width + X + Dx;
						if(!vRaw[Index].Reachable() || m_vBlocked[Index] || (Index < vFinishes.size() && vFinishes[Index]))
							continue;
						Sum += vRaw[Index].m_Flow * aWeights[Dy + 1][Dx + 1];
						Weight += aWeights[Dy + 1][Dx + 1];
					}
				if(Weight > 0)
				{
					Sum /= Weight;
					Cell.m_Flow = Sum == vec2(0, 0) ? Sum : normalize(Sum);
				}
			}
		m_Ready = true;
		return true;
	}
}
