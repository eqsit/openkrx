#include "visuals.h"

#include "bots.h"

#include <base/math.h>
#include <base/secure.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/laser.h>
#include <game/client/projectile_data.h>
#include <game/client/ui_rect.h>
#include <game/collision.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace OpenKrx
{
	void CMediumVisuals::OnReset()
	{
		m_LastTrail = m_LastIdentity = 0;
		m_HudPhase = m_WatermarkPhase = 0;
	}

	// 307c10 and308a00 use accumulated float .01 increments, draw the segment
	// before testing tile/player collision, and ignore the owner for aim lines.
	std::vector<CPathSegment> CMediumVisuals::ProjectilePath(const CGameWorld &Source, int Owner, int Type, vec2 Pos, vec2 Dir, float Age, bool StopOnPlayers)
	{
		std::vector<CPathSegment> Result;
		auto &World = const_cast<CGameWorld &>(Source);
		const auto *pTune = Source.GlobalTuning();
		float Curvature, Speed, Lifetime;
		if(Type == WEAPON_GUN)
		{
			Curvature = pTune->m_GunCurvature;
			Speed = pTune->m_GunSpeed;
			Lifetime = pTune->m_GunLifetime;
		}
		else if(Type == WEAPON_GRENADE)
		{
			Curvature = pTune->m_GrenadeCurvature;
			Speed = pTune->m_GrenadeSpeed;
			Lifetime = pTune->m_GrenadeLifetime;
		}
		else if(Type == WEAPON_SHOTGUN)
		{
			Curvature = pTune->m_ShotgunCurvature;
			Speed = pTune->m_ShotgunSpeed;
			Lifetime = pTune->m_ShotgunLifetime;
		}
		else
			return Result;
		float t = 0;
		const int Steps = std::clamp((int)(Lifetime * 100.0f), 0, 10000);
		for(int i = 0; i < Steps; ++i)
		{
			t += .01f;
			const vec2 From = CalcPos(Pos, Dir, Curvature, Speed, t - .01f);
			const vec2 To = CalcPos(Pos, Dir, Curvature, Speed, t);
			if(t < Age)
				continue;
			Result.push_back({From, To});
			vec2 At;
			if(Source.Collision()->IntersectLine(From, To, &At, nullptr))
				break;
			if(StopOnPlayers && World.IntersectCharacter(From, To, 0, At, World.GetCharacterById(Owner)))
				break;
		}
		return Result;
	}

	std::vector<CPathSegment> CMediumVisuals::LaserPath(const CGameWorld &Source, int Owner, int Type, vec2 Pos, vec2 Dir)
	{
		std::vector<CPathSegment> Result;
		CGameWorld Copy;
		Copy.CopyWorldIsolated(&Source);
		const auto *pTune = Source.GlobalTuning();
		auto *pLaser = new CLaser(&Copy, Pos, Dir, pTune->m_LaserReach, Owner, Type);
		int LastEval = -1;
		for(int Step = 0; Step < 2048; ++Step)
		{
			bool Exists = false;
			for(auto *pEntity = Copy.FindFirst(CGameWorld::ENTTYPE_LASER); pEntity; pEntity = pEntity->TypeNext())
				if(pEntity == pLaser)
				{
					Exists = true;
					break;
				}
			if(!Exists)
				break;
			if(pLaser->GetEvalTick() != LastEval)
			{
				Result.push_back({pLaser->GetFrom(), pLaser->m_Pos});
				LastEval = pLaser->GetEvalTick();
			}
			++Copy.m_GameTick;
			Copy.Tick();
		}
		return Result;
	}

	void CMediumVisuals::WorldLine(vec2 From, vec2 To, ColorRGBA Color, float Width)
	{
		const vec2 Difference = To - From;
		if(length(Difference) < .001f)
			return;
		const vec2 Perp = vec2(-Difference.y, Difference.x) / length(Difference) * (Width * .5f);
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(Color);
		IGraphics::CFreeformItem Quad(From - Perp, From + Perp, To - Perp, To + Perp);
		Graphics()->QuadsDrawFreeform(&Quad, 1);
		Graphics()->QuadsEnd();
	}

	void CMediumVisuals::RenderBulletLines(const CProjectileData &Projectile, float Curvature, float Speed, float Age)
	{
		if(!g_Config.m_KrxDrawBulletLines)
			return;
		const int Own = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
		if(g_Config.m_KrxDrawBulletLinesSelfOnly && (!Projectile.m_ExtraInfo || Projectile.m_Owner != Own))
			return;
		const int Weapon = Projectile.m_Type;
		if((Weapon == WEAPON_GUN && !g_Config.m_KrxDrawBulletLinesGun) ||
			(Weapon == WEAPON_GRENADE && !g_Config.m_KrxDrawBulletLinesGrenade) ||
			(Weapon == WEAPON_SHOTGUN && !g_Config.m_KrxDrawBulletLinesShotgun))
			return;
		if(Weapon != WEAPON_GUN && Weapon != WEAPON_GRENADE && Weapon != WEAPON_SHOTGUN)
			return;
		ColorRGBA Color = Weapon == WEAPON_GUN ? ColorRGBA(.9f, .9f, .9f) : Weapon == WEAPON_SHOTGUN ? ColorRGBA(0, .8f, 0) :
													       ColorRGBA(1, .3f, .6f);
		Color.a = g_Config.m_KrxDrawBulletLinesAlpha / 100.f;
		if(Projectile.m_ExtraInfo && Projectile.m_Owner >= 0 && GameClient()->IsOtherTeam(Projectile.m_Owner))
			Color.a = g_Config.m_ClShowOthersAlpha / 100.f;
		// Keep the actual projectile's tune-zone curvature/speed, not global aim tuning.
		const auto *pTune = GameClient()->GetTuning(Projectile.m_TuneZone);
		float Lifetime = Weapon == WEAPON_GUN ? pTune->m_GunLifetime : Weapon == WEAPON_GRENADE ? pTune->m_GrenadeLifetime :
													  pTune->m_ShotgunLifetime;
		float t = 0;
		for(int i = 0; i < std::clamp((int)(Lifetime * 100.f), 0, 10000); ++i)
		{
			t += .01f;
			if(t < Age)
				continue;
			const vec2 From = CalcPos(Projectile.m_StartPos, Projectile.m_StartVel, Curvature, Speed, t - .01f);
			const vec2 To = CalcPos(Projectile.m_StartPos, Projectile.m_StartVel, Curvature, Speed, t);
			WorldLine(From, To, Color, 1);
			if(Collision()->IntersectLine(From, To, nullptr, nullptr))
				break;
		}
	}

	void CMediumVisuals::RenderAimLines()
	{
		if(!g_Config.m_KrxDrawAimLines)
			return;
		const int Own = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
		for(int Id = 0; Id < MAX_CLIENTS; ++Id)
		{
			if((g_Config.m_KrxDrawAimLinesSelfOnly && Id != Own) || !GameClient()->m_aClients[Id].m_Active)
				continue;
			auto *pCharacter = GameClient()->m_GameWorld.GetCharacterById(Id);
			if(!pCharacter)
				continue;
			const int Weapon = pCharacter->GetActiveWeapon();
			if((Weapon == WEAPON_GUN && !g_Config.m_KrxDrawAimLinesGun) || (Weapon == WEAPON_GRENADE && !g_Config.m_KrxDrawAimLinesGrenade) ||
				(Weapon == WEAPON_SHOTGUN && !g_Config.m_KrxDrawAimLinesShotgun) || (Weapon == WEAPON_LASER && !g_Config.m_KrxDrawAimLinesLaser))
				continue;
			if(Weapon < WEAPON_GUN || Weapon > WEAPON_LASER)
				continue;
			vec2 Dir = direction(GameClient()->m_aClients[Id].m_Angle);
			if(Id == Own)
				Dir = normalize(GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy]);
			if(Dir == vec2(0, 0))
				Dir = vec2(1, 0);
			const vec2 Pos = GameClient()->m_aClients[Id].m_RenderPos;
			std::vector<CPathSegment> Path;
			if(Weapon == WEAPON_LASER || (Weapon == WEAPON_SHOTGUN && GameClient()->m_GameWorld.m_WorldConfig.m_IsDDRace))
				Path = LaserPath(GameClient()->m_GameWorld, Id, Weapon, Pos, Dir);
			else
				Path = ProjectilePath(GameClient()->m_GameWorld, Id, Weapon, Pos + Dir * 21.f, Dir);
			ColorRGBA Color = Weapon == WEAPON_GUN ? ColorRGBA(.9f, .9f, .9f) : Weapon == WEAPON_GRENADE ? ColorRGBA(1, .3f, .6f) :
										    Weapon == WEAPON_SHOTGUN         ? ColorRGBA(0, .8f, 0) :
														       ColorRGBA(.3f, .7f, 1);
			Color.a = g_Config.m_KrxDrawAimLinesAlpha / 100.f;
			for(const auto &Segment : Path)
				WorldLine(Segment.m_From, Segment.m_To, Color);
		}
	}

	void CMediumVisuals::RenderTargetIndicators()
	{
		if(!g_Config.m_KrxAimbot)
			return;
		const int Id = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
		if(Id < 0)
			return;
		if(g_Config.m_KrxAimbotDrawFov)
		{
			const vec2 Pos = GameClient()->m_LocalCharacterPos;
			const float Base = angle(GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy]);
			for(int Sign : {-1, 1})
			{
				const vec2 End = Pos + direction(Base + Sign * g_Config.m_KrxAimbotFovHook * .01f) * (float)GameClient()->GetTuning(0)->m_HookLength;
				vec2 At = End;
				Collision()->IntersectLineTeleHook(Pos, End, &At, nullptr, nullptr);
				WorldLine(Pos, At, ColorRGBA(0, 0, 0, .5f), 1);
			}
		}
		if(!g_Config.m_KrxAimbotTargetBox)
			return;
		for(int Target : {GameClient()->m_Aimbot.HookTarget(), GameClient()->m_Aimbot.WeaponTarget()})
		{
			if(Target < 0 || Target >= MAX_CLIENTS || !GameClient()->m_aClients[Target].m_Active)
				continue;
			const vec2 Pos = GameClient()->m_aClients[Target].m_RenderPos;
			const ColorRGBA Color = Target == GameClient()->m_Aimbot.HookTarget() ? ColorRGBA(1, 0, 0, 1) : ColorRGBA(1, .65f, 0, 1);
			WorldLine(Pos + vec2(-21, -21), Pos + vec2(21, -21), Color, 3);
			WorldLine(Pos + vec2(21, -21), Pos + vec2(21, 21), Color, 3);
			WorldLine(Pos + vec2(21, 21), Pos + vec2(-21, 21), Color, 3);
			WorldLine(Pos + vec2(-21, 21), Pos + vec2(-21, -21), Color, 3);
		}
	}

	void CMediumVisuals::GradientText(float x, float y, float Size, const char *pText, unsigned Start, unsigned End, bool Rainbow, bool Segmented, int Speed, float &Phase)
	{
		Phase += Speed * .000005f;
		const auto A = color_cast<ColorRGBA>(ColorHSLA(Start)), B = color_cast<ColorRGBA>(ColorHSLA(End));
		const int Length = str_length(pText);
		const auto At = [&](float Offset) {
			float T = std::fmod(Phase + Offset, 1.f);
			if(Rainbow)
			{
				const float h = std::fmod(Phase * .1f + T, 1.f) * 2 * pi;
				return ColorRGBA(std::cos(h) * .5f + .5f, std::cos(h + 2.09439f) * .5f + .5f, std::cos(h + 4.18879f) * .5f + .5f, 1);
			}
			if(!Segmented)
				T = (std::sin(Phase) + 1.f) * .5f;
			return ColorRGBA(mix(A.r, B.r, T), mix(A.g, B.g, T), mix(A.b, B.b, T), 1);
		};
		if(!Segmented)
		{
			TextRender()->TextColor(At(0));
			TextRender()->Text(x, y, Size, pText);
		}
		else
			for(int i = 0; i < Length;)
			{
				const int Next = str_utf8_forward(pText, i);
				char aChar[8];
				str_copy(aChar, pText + i, std::min(Next - i + 1, (int)sizeof(aChar)));
				TextRender()->TextColor(At((float)i / std::max(1, Length)));
				TextRender()->Text(x, y, Size, aChar);
				x += TextRender()->TextWidth(Size, aChar) * .71f;
				i = Next;
			}
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	void CMediumVisuals::RenderHud()
	{
		const float Width = 300.f * Graphics()->ScreenAspect();
		Graphics()->MapScreen(CScreenRect(0, 0, Width, 300));
		float y = 35;
		if(g_Config.m_KrxFeaturesHud)
		{
			std::vector<const char *> Enabled;
			const auto Add = [&](int Value, const char *Name) {if(Value)Enabled.push_back(Name); };
			Add(g_Config.m_KrxAimbot, "Aimbot");
			Add(g_Config.m_AvoidFreeze, "Avoid Freeze");
			Add(g_Config.m_KrxAutoFire, "Auto Fire");
			Add(g_Config.m_KrxAutoRehook, "Auto Rehook");
			Add(g_Config.m_KrxBalanceBot, "Balance Bot");
			Add(g_Config.m_KrxHookRide, "Hook Ride");
			Add(g_Config.m_KrxJetRide, "Jet Ride");
			Add(g_Config.m_KrxAutoAled, "Auto Aled");
			Add(g_Config.m_KrxDummyFly, "Dummy Fly");
			Add(g_Config.m_KrxQuickStop, "Quick Stop");
			Add(g_Config.m_KrxPixelWalk, "Pixel Walk");
			Add(g_Config.m_KrxAutoShotgun, "Auto Shotgun");
			Add(g_Config.m_KrxUnfreezeBot, "Auto Unfreeze");
			Add(g_Config.m_KrxAutoJumpSave, "Auto Jump Save");
			Add(g_Config.m_KrxFakeAim, "Fake Aim");
			Add(g_Config.m_KrxMoonwalk, "Moonwalk");
			Add(g_Config.m_KrxHookSpam, "Hook Spam");
			Add(g_Config.m_KrxIdStealer, "ID Stealer");
			const float Size = g_Config.m_KrxFeaturesHudFontSize;
			std::stable_sort(Enabled.begin(), Enabled.end(), [&](const char *a, const char *b) { return TextRender()->TextWidth(Size, a) > TextRender()->TextWidth(Size, b); });
			for(const char *Name : Enabled)
			{
				const float w = TextRender()->TextWidth(Size, Name);
				CUIRect Rect{Width - w - 6, y - 1, w + 2, Size + 2};
				Rect.Draw(ColorRGBA(0, 0, 0, g_Config.m_KrxFeaturesHudOpacity / 100.f), IGraphics::CORNER_ALL, 1);
				GradientText(Width - w - 5, y, Size, Name, g_Config.m_KrxFeaturesHudGradientStartColor, g_Config.m_KrxFeaturesHudGradientEndColor, g_Config.m_KrxFeaturesHudGradientRainbow, g_Config.m_KrxFeaturesHudSegmented, g_Config.m_KrxFeaturesHudGradientSpeed, m_HudPhase);
				y += Size + 3;
			}
		}
		const char *pDetection = GameClient()->m_KrxMiscPreTas.ModeratorDetected()                                   ? "Mod Detected" :
					 g_Config.m_KrxModDetectorWarn && GameClient()->m_KrxMiscPreTas.SuspiciousDetected() ? "Warn Detected" :
															       nullptr;
		if(g_Config.m_KrxModDetector && pDetection)
		{
			const float Size = g_Config.m_KrxFeaturesHudFontSize;
			const float w = TextRender()->TextWidth(Size, pDetection);
			CUIRect Rect{Width - w - 6, y - 1, w + 2, Size + 2};
			Rect.Draw(ColorRGBA(0, 0, 0, g_Config.m_KrxFeaturesHudOpacity / 100.f), IGraphics::CORNER_ALL, 1);
			GradientText(Width - w - 5, y, Size, pDetection, g_Config.m_KrxFeaturesHudGradientStartColor, g_Config.m_KrxFeaturesHudGradientEndColor, g_Config.m_KrxFeaturesHudGradientRainbow, g_Config.m_KrxFeaturesHudSegmented, g_Config.m_KrxFeaturesHudGradientSpeed, m_HudPhase);
		}
		if(g_Config.m_KrxWatermark)
		{
			char aText[160];
			const int Ping = GameClient()->m_Snap.m_pLocalInfo ? GameClient()->m_Snap.m_pLocalInfo->m_Latency : 0;
			str_format(aText, sizeof(aText), "OpenKRX | %s | %d ms", g_Config.m_PlayerName, Ping);
			CUIRect Rect{5, 5, TextRender()->TextWidth(6, aText) + 8, 11};
			Rect.Draw(ColorRGBA(0, 0, 0, .5f), IGraphics::CORNER_ALL, 2.5f);
			GradientText(9, 7, 6, aText, g_Config.m_KrxWatermarkGradientStartColor, g_Config.m_KrxWatermarkGradientEndColor, g_Config.m_KrxWatermarkGradientRainbow, g_Config.m_KrxWatermarkSegmented, g_Config.m_KrxWatermarkGradientSpeed, m_WatermarkPhase);
		}
	}

	void CMediumVisuals::RenderTrail()
	{
		if(!g_Config.m_KrxTrail || GameClient()->m_Snap.m_SpecInfo.m_Active)
			return;
		const int64_t Now = time_get();
		const int Rate = g_Config.m_KrxTrailMode == 1 ? 100 : 50;
		if(m_LastTrail && Now - m_LastTrail < time_freq() / Rate)
			return;
		m_LastTrail = Now;
		CParticle p;
		p.SetDefault();
		const vec2 Pos = GameClient()->m_LocalCharacterPos;
		p.m_Color = g_Config.m_KrxTrailColorRainbow ? color_cast<ColorRGBA>(ColorHSLA(std::fmod(Client()->LocalTime() * .5f, 1.f), 1, .5f)) : color_cast<ColorRGBA>(ColorHSLA(g_Config.m_KrxTrailColor));
		p.m_Pos = Pos;
		p.m_Friction = .7f;
		p.m_EndSize = 0;
		int Group = CParticles::GROUP_PROJECTILE_TRAIL;
		switch(g_Config.m_KrxTrailMode)
		{
		case 0:
			p.m_Spr = SPRITE_PART_SMOKE;
			p.m_Vel = GameClient()->m_PredictedChar.m_Vel + random_direction() * 50.f;
			p.m_LifeSpan = random_float(.5f, 1.f);
			p.m_StartSize = random_float(12.f, 20.f);
			p.m_Gravity = random_float(-500.f);
			break;
		case 1:
			p.m_Spr = SPRITE_PART_BALL;
			p.m_LifeSpan = random_float(.25f, .5f);
			p.m_StartSize = 8;
			break;
		case 2:
			p.m_Spr = SPRITE_PART_SLICE;
			p.m_Pos = Pos + vec2(random_float(-.5f, .5f), random_float(-.5f, .5f)) * 10.f;
			p.m_LifeSpan = .5f;
			p.m_StartSize = 16;
			p.m_Rot = random_angle();
			p.m_Rotspeed = 2 * pi;
			p.m_Gravity = 500;
			p.m_Friction = .9f;
			Group = CParticles::GROUP_GENERAL;
			break;
		default:
			p.m_Spr = SPRITE_PART_SPARKLE;
			p.m_Pos = Pos + random_direction() * random_float(40.f);
			p.m_LifeSpan = .5f;
			p.m_StartSize = 0;
			p.m_EndSize = random_float(20.f, 30.f);
			p.m_UseAlphaFading = true;
			p.m_EndAlpha = .2f;
			p.m_Collides = false;
			Group = CParticles::GROUP_TRAIL_EXTRA;
			break;
		}
		GameClient()->m_Particles.Add(Group, &p);
	}

	void CMediumVisuals::UpdateIdentity()
	{
		if(!g_Config.m_KrxIdStealer || Client()->State() != IClient::STATE_ONLINE || !(GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy].m_PlayerFlags & PLAYERFLAG_PLAYING))
		{
			m_LastIdentity = 0;
			return;
		}
		const int64_t Now = time_get();
		if(!m_LastIdentity)
		{
			m_LastIdentity = Now;
			return;
		}
		if(Now - m_LastIdentity < (int64_t)g_Config.m_KrxIdStealerSpeed * time_freq())
			return;
		int Target = -1;
		const int Own = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
		if(g_Config.m_KrxIdStealerClosest)
			Target = CMediumBots::ClosestPlayer(GameClient());
		else
		{
			std::vector<int> Candidates;
			for(int i = 0; i < MAX_CLIENTS; ++i)
				if(i != Own && GameClient()->m_aClients[i].m_Active)
					Candidates.push_back(i);
			if(!Candidates.empty())
				Target = Candidates[secure_rand_below(Candidates.size())];
		}
		if(Target < 0 || Target == Own || Target >= MAX_CLIENTS || !GameClient()->m_aClients[Target].m_Active)
			return;
		const auto &Other = GameClient()->m_aClients[Target];
		if(g_Config.m_KrxIdStealerName)
			str_copy(g_Config.m_PlayerName, Other.m_aName);
		if(g_Config.m_KrxIdStealerClan)
			str_copy(g_Config.m_PlayerClan, Other.m_aClan);
		if(g_Config.m_KrxIdStealerFlag)
			g_Config.m_PlayerCountry = Other.m_Country;
		if(g_Config.m_KrxIdStealerSkin)
		{
			str_copy(g_Config.m_ClPlayerSkin, Other.m_aSkinName);
			g_Config.m_ClPlayerUseCustomColor = Other.m_UseCustomColor;
			g_Config.m_ClPlayerColorBody = Other.m_ColorBody;
			g_Config.m_ClPlayerColorFeet = Other.m_ColorFeet;
		}
		if(g_Config.m_KrxIdStealerEmote)
			GameClient()->m_Emoticon.EyeEmote(Other.m_RenderCur.m_Emote);
		GameClient()->SendInfo(false);
		m_LastIdentity = Now;
	}

	void CMediumVisuals::OnRender()
	{
		if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
			return;
		const CScreenRect Old = Graphics()->GetScreen();
		const auto &Cam = GameClient()->m_Camera;
		const auto World = Graphics()->MapScreenToWorld(Cam.m_Center.x, Cam.m_Center.y, 100, 100, 100, 0, 0, Graphics()->ScreenAspect(), Cam.m_Zoom);
		Graphics()->MapScreen(World);
		RenderAimLines();
		RenderTargetIndicators();
		RenderTrail();
		RenderHud();
		Graphics()->MapScreen(Old);
	}
}
