#ifndef GAME_CLIENT_KRX_VISUALS_H
#define GAME_CLIENT_KRX_VISUALS_H
#include "now_playing.h"

#include <base/color.h>

#include <engine/graphics.h>

#include <game/client/component.h>

#include <array>
#include <cstdint>
#include <vector>
class CGameWorld;
class CProjectileData;
namespace OpenKrx
{
	struct CPathSegment
	{
		vec2 m_From;
		vec2 m_To;
	};
	class CMediumVisuals : public CComponent
	{
		float m_HudPhase = 0, m_WatermarkPhase = 0;
		float m_NowPlayingPhase = 0;
		CNowPlaying m_NowPlaying;
		std::string m_NowPlayingText;
		std::shared_ptr<const std::vector<uint8_t>> m_pNowPlayingArt;
		IGraphics::CTextureHandle m_NowPlayingArt;
		int64_t m_NowPlayingSince = 0;
		struct CTrailPoint
		{
			vec2 m_Position = vec2(0, 0);
			int m_Tick = -1;
		};
		std::array<std::array<CTrailPoint, 200>, 128> m_aTeeTrail{};
		int64_t m_LastTrail = 0, m_LastIdentity = 0;
		void WorldLine(vec2 From, vec2 To, ColorRGBA Color, float Width = 2.0f);
		void GradientText(float x, float y, float Size, const char *pText, unsigned Start, unsigned End, bool Rainbow, bool Segmented, int Speed, float &Phase);
		void RenderAimLines();
		void RenderTargetIndicators();
		void RenderHud();
		void RenderTrail();
		void RenderTeeTrail();
		void RenderNowPlaying();

	public:
		int Sizeof() const override { return sizeof(*this); }
		void OnRender() override;
		void OnReset() override;
		void OnShutdown() override;
		void UpdateIdentity();
		void RenderBulletLines(const CProjectileData &Projectile, float Curvature, float Speed, float Age);
		static std::vector<CPathSegment> ProjectilePath(const CGameWorld &Source, int Owner, int Type, vec2 Position, vec2 Direction, float Age = 0, bool StopOnPlayers = true);
		static std::vector<CPathSegment> LaserPath(const CGameWorld &Source, int Owner, int Type, vec2 Position, vec2 Direction);
	};
}
#endif
