#ifndef GAME_CLIENT_KRX_VISUALS_H
#define GAME_CLIENT_KRX_VISUALS_H
#include <base/color.h>

#include <game/client/component.h>

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
		int64_t m_LastTrail = 0, m_LastIdentity = 0;
		void WorldLine(vec2 From, vec2 To, ColorRGBA Color, float Width = 2.0f);
		void GradientText(float x, float y, float Size, const char *pText, unsigned Start, unsigned End, bool Rainbow, bool Segmented, int Speed, float &Phase);
		void RenderAimLines();
		void RenderTargetIndicators();
		void RenderHud();
		void RenderTrail();

	public:
		int Sizeof() const override { return sizeof(*this); }
		void OnRender() override;
		void OnReset() override;
		void UpdateIdentity();
		void RenderBulletLines(const CProjectileData &Projectile, float Curvature, float Speed, float Age);
		static std::vector<CPathSegment> ProjectilePath(const CGameWorld &Source, int Owner, int Type, vec2 Position, vec2 Direction, float Age = 0, bool StopOnPlayers = true);
		static std::vector<CPathSegment> LaserPath(const CGameWorld &Source, int Owner, int Type, vec2 Position, vec2 Direction);
	};
}
#endif
