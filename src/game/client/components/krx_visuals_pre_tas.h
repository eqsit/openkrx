#ifndef GAME_CLIENT_COMPONENTS_KRX_VISUALS_PRE_TAS_H
#define GAME_CLIENT_COMPONENTS_KRX_VISUALS_PRE_TAS_H

#include <base/color.h>

#include <generated/protocol.h>

#include <game/client/component.h>

#include <cstdint>
#include <vector>

namespace OpenKrx
{
	class CPreTasVisuals : public CComponent
	{
		// Original 0x1402cc7a0 uses one timer/mouse pair across both local slots.
		int64_t m_AfkLastActivity = 0;
		vec2 m_AfkMouse = vec2(0, 0);
		struct CTrajectoryPoint
		{
			vec2 m_Position;
			bool m_Frozen;
		};
		struct CTrajectory
		{
			int m_ClientId;
			CNetObj_PlayerInput m_Input;
			std::vector<CTrajectoryPoint> m_vPoints;
		};
		std::vector<CTrajectory> m_vTrajectories;
		void WorldLine(vec2 From, vec2 To, float Width, ColorRGBA Color);
		void WorldCircle(vec2 Center, float Radius, ColorRGBA Color, float Width, bool Filled);
		void RenderTrajectory();
		void RenderTileOutlines();
		void RenderFrozenHud();

	public:
		int Sizeof() const override { return sizeof(*this); }
		bool ShouldSuspendAvoid();
		void PredictTrajectory();
		void OnReset() override { m_vTrajectories.clear(); }
		void OnRender() override;
	};
}
#endif
