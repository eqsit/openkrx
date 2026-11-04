#ifndef GAME_CLIENT_KRX_FLOW_FIELD_H
#define GAME_CLIENT_KRX_FLOW_FIELD_H

#include <base/vmath.h>

#include <atomic>
#include <cfloat>
#include <vector>

namespace OpenKrx
{
	// GoodCrack 2dec40: reverse Theta* with Euclidean edge costs, not BFS.
	// Flow points away from the parent; Fent minimizes its dot with velocity.
	class CFlowField
	{
	public:
		struct CCell
		{
			float m_Cost = FLT_MAX;
			int m_Parent = -1;
			bool m_Closed = false;
			vec2 m_Flow = vec2(0, 0);
			bool Reachable() const { return m_Cost != FLT_MAX; }
		};
		void Resize(int Width, int Height);
		bool Build(vec2 Goal, const std::vector<unsigned char> &vBlocked, const std::vector<unsigned char> &vFinishes = {}, const std::atomic<bool> *pCancel = nullptr);
		// KRX 1.36.1 33f3f0 seeds the same reverse Theta* from every editor finish.
		bool BuildGoals(const std::vector<vec2> &vGoals, const std::vector<unsigned char> &vBlocked, const std::vector<unsigned char> &vFinishes = {}, const std::atomic<bool> *pCancel = nullptr);
		bool LineOfSight(int From, int To) const;
		const CCell *At(int X, int Y) const;
		const CCell *At(vec2 Position) const { return At((int)(Position.x / 32.0f), (int)(Position.y / 32.0f)); }
		int Width() const { return m_Width; }
		int Height() const { return m_Height; }
		vec2 Goal() const { return m_Goal; }
		const std::vector<vec2> &Goals() const { return m_vGoals; }
		float DistanceToGoal(vec2 Position) const;
		bool Ready() const { return m_Ready; }
		void Invalidate() { m_Ready = false; }

	private:
		int m_Width = 0, m_Height = 0;
		vec2 m_Goal = vec2(0, 0);
		std::vector<vec2> m_vGoals;
		bool m_Ready = false;
		std::vector<CCell> m_vCells;
		std::vector<unsigned char> m_vBlocked;
		bool Inside(int X, int Y) const { return X >= 0 && Y >= 0 && X < m_Width && Y < m_Height; }
		float Distance(int A, int B) const;
	};
}
#endif
