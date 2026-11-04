#include "visuals.h"

#include "bots.h"

#include <base/math.h>
#include <base/secure.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/image.h>
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
		for(auto &Trail : m_aTeeTrail)
			Trail.fill({});
	}

	void CMediumVisuals::OnShutdown()
	{
		m_NowPlaying.Stop();
		Graphics()->UnloadTexture(&m_NowPlayingArt);
		m_pNowPlayingArt.reset();
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
			auto *pCharacter = GameClient()->KrxGameWorld().GetCharacterById(Id);
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
			if(Weapon == WEAPON_LASER || (Weapon == WEAPON_SHOTGUN && GameClient()->KrxGameWorld().m_WorldConfig.m_IsDDRace))
				Path = LaserPath(GameClient()->KrxGameWorld(), Id, Weapon, Pos, Dir);
			else
				Path = ProjectilePath(GameClient()->KrxGameWorld(), Id, Weapon, Pos + Dir * 21.f, Dir);
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
		if(Id < 0 || Id >= MAX_CLIENTS || !GameClient()->KrxPredictedWorld().GetCharacterById(Id))
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
		const int HookTarget = GameClient()->m_Aimbot.HookTarget(), WeaponTarget = GameClient()->m_Aimbot.WeaponTarget();
		const auto *pLocal = GameClient()->KrxPredictedWorld().GetCharacterById(Id);
		const int Weapon = pLocal->GetActiveWeapon();
		const int aWeaponEnabled[] = {g_Config.m_KrxAimbotWeaponHammer, g_Config.m_KrxAimbotWeaponGun, g_Config.m_KrxAimbotWeaponShotgun, g_Config.m_KrxAimbotWeaponGrenade, g_Config.m_KrxAimbotWeaponLaser};
		const bool WeaponEnabled = Weapon >= WEAPON_HAMMER && Weapon <= WEAPON_LASER && aWeaponEnabled[Weapon];
		for(int TargetIndex = 0; TargetIndex < 2; ++TargetIndex)
		{
			const int Target = TargetIndex == 0 ? HookTarget : WeaponTarget;
			if((TargetIndex == 0 && !g_Config.m_KrxAimbotWeaponHook) || (TargetIndex == 1 && (!WeaponEnabled || (Target == HookTarget && g_Config.m_KrxAimbotWeaponHook))))
				continue;
			if(Target < 0 || Target >= MAX_CLIENTS || !GameClient()->m_aClients[Target].m_Active)
				continue;
			const vec2 Pos = GameClient()->m_aClients[Target].m_RenderPos;
			const ColorRGBA Color = TargetIndex == 0 ? ColorRGBA(1, 0, 0, 1) : ColorRGBA(1, .65f, 0, 1);
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
			// Recovered 13610 feature list2cbaa0; stable length sort below.
			Add(g_Config.m_KrxMassMentionSpam, "Mass Mention Spam");
			Add(g_Config.m_KrxKillsay, "Killsay/Deathsay");
			Add(g_Config.m_KrxFancyChatFont, "Fancy Chat Font");
			Add(g_Config.m_KrxChatRepeater, "Chat Repeater");
			Add(g_Config.m_AvoidFreeze, "Avoid Freeze");
			Add(g_Config.m_KrxMoonwalk, "Moonwalk");
			Add(g_Config.m_KrxHookRide, "Hook Ride");
			Add(g_Config.m_KrxIdStealer, "Id Stealer");
			Add(g_Config.m_KrxAutoFire, "Auto Fire");
			Add(g_Config.m_KrxFakeAim, "Fake Aim");
			Add(g_Config.m_KrxAimbot, "Aimbot");
			Add(g_Config.m_KrxGhostMove, "Ghost Move");
			Add(g_Config.m_KrxQuickStop, "Quick Stop");
			Add(g_Config.m_KrxAutoJumpSave, "Auto Jump Save");
			Add(g_Config.m_KrxAutoVoteKick, "Auto Vote Kick");
			Add(g_Config.m_KrxAutoRehook, "Auto Rehook");
			Add(g_Config.m_KrxModDetector, "Mod Detector");
			Add(g_Config.m_KrxEmoteSpam, "Emote Spam");
			Add(g_Config.m_KrxGrenadeSaver, "Grenade Saver");
			Add(g_Config.m_KrxUnfreezeBot, "Auto Unfreeze");
			Add(g_Config.m_KrxAutoAled, "Auto Aled");
			Add(g_Config.m_KrxAutoShotgun, "Auto Shotgun");
			Add(g_Config.m_AvoidTrackPoints, "Track Point");
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
		if(g_Config.m_KrxTrailType == 4)
		{
			RenderTeeTrail();
			return;
		}
		const int64_t Now = time_get();
		const int Rate = g_Config.m_KrxTrailType == 1 ? 100 : 50;
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
		switch(g_Config.m_KrxTrailType)
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
			p.m_FlowAffected = 0;
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

	void CMediumVisuals::RenderTeeTrail()
	{
		// REA371b40: a 200-tick ring per tee, newest-first path, adjacent
		// duplicate removal, endpoint interpolation and per-point taper/fade.
		// The current DDNet render/prediction flags replace old private offsets.
		const int Slot = g_Config.m_ClDummy;
		for(int Id = 0; Id < 128; ++Id)
		{
			if(Id != GameClient()->m_Snap.m_LocalClientId)
				continue;
			auto &History = m_aTeeTrail[Id];
			const auto &ClientData = GameClient()->m_aClients[Id];
			if(!ClientData.m_Active || !GameClient()->m_Snap.m_aCharacters[Id].m_Active)
			{
				History.fill({});
				continue;
			}
			const int Tick = ClientData.m_IsPredicted ? Client()->PredGameTick(Slot) : Client()->GameTick(Slot);
			const float Intra = ClientData.m_IsPredicted ? Client()->PredIntraGameTick(Slot) : Client()->IntraGameTick(Slot);
			if(Tick < 0)
				continue;
			History[Tick % 200] = {ClientData.m_RenderPos, Tick};
			std::vector<CTrailPoint> Points;
			const int Length = std::clamp(g_Config.m_ClTeeTrailLength, 5, 200);
			const int Count = std::min(200, Length + (ClientData.m_IsPredicted ? 0 : 2));
			for(int Age = 0; Age < Count; ++Age)
			{
				const int Wanted = Tick - Age;
				if(Wanted < 0)
					break;
				const CTrailPoint Point = ClientData.m_IsPredicted ? CTrailPoint{ClientData.m_aPredPos[Wanted % 200], ClientData.m_aPredTick[Wanted % 200]} : History[Wanted % 200];
				if(Point.m_Tick != Wanted)
					break;
				if(Points.empty() || Point.m_Position != Points.back().m_Position)
					Points.push_back(Point);
			}
			if(Points.size() < 3)
				continue;
			Points.front().m_Position = ClientData.m_RenderPos;
			Points.back().m_Position = mix(Points.back().m_Position, Points[Points.size() - 2].m_Position, Intra);
			struct CVertex
			{
				vec2 m_Left, m_Right;
				ColorRGBA m_Color;
			};
			std::vector<CVertex> Vertices;
			for(size_t Index = 0; Index < Points.size(); ++Index)
			{
				const auto &Point = Points[Index];
				const float Fraction = Index == 0 ? 0.f : Index + 1 == Points.size() ? 1.f :
												       (Index + Intra - 1.f) / (Points.size() - 2.f);
				ColorRGBA Color(0, 0, 0, 1);
				switch(g_Config.m_ClTeeTrailColorMode)
				{
				case 0: Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClTeeTrailColor)); break;
				case 1: Color = ClientData.m_RenderInfo.m_ColorBody; break;
				case 2:
					Color = color_cast<ColorRGBA>(ColorHSLA(std::fmod((float)((Id * 6361 + Point.m_Tick) % 1000000) * (.5f / Length), 1.f), 1, .5f));
					break;
				case 3:
				{
					const size_t OtherIndex = Index < 2 ? std::min(Index + 2, Points.size() - 1) : Index - 2;
					const auto &Other = Points[OtherIndex];
					const float Speed = distance(Point.m_Position, Other.m_Position) / std::max(1, std::abs(Point.m_Tick - Other.m_Tick));
					const unsigned Packed = (1 - (int)(Speed * Speed / -12.5f)) * 0xff00u;
					Color = color_cast<ColorRGBA>(ColorHSLA(Packed));
					break;
				}
				// The shipped "Random" case4 has no RGB writer in371b40.
				default: break;
				}
				float Alpha = g_Config.m_ClTeeTrailAlpha / 100.f;
				if(GameClient()->IsOtherTeam(Id))
					Alpha *= g_Config.m_ClShowOthersAlpha / 100.f;
				Color.a = g_Config.m_ClTeeTrailFade ? Alpha * (1.f - Fraction) : Alpha;
				const float Width = g_Config.m_ClTeeTrailTaper ? g_Config.m_ClTeeTrailWidth * (1.f - Fraction) : (float)g_Config.m_ClTeeTrailWidth;
				const vec2 Pos = Point.m_Position;
				const vec2 Previous = Index == 0 ? Pos - normalize(Points[1].m_Position - Pos) : Points[Index - 1].m_Position;
				const vec2 Next = Index + 1 == Points.size() ? Pos + normalize(Pos - Previous) : Points[Index + 1].m_Position;
				const vec2 Before = normalize(Pos - Previous), After = normalize(Next - Pos);
				const vec2 Normal(-Before.y, Before.x);
				const vec2 Tangent = normalize(Before + After);
				vec2 Miter(-Tangent.y, Tangent.x);
				if(Miter == vec2(0, 0))
					Miter = Normal;
				const float Denom = dot(Miter, Normal);
				const float Extension = Denom > .001f ? std::min(Width / Denom, Width * 3.f) : Width;
				Vertices.push_back({Pos + Miter * Extension, Pos - Miter * Extension, Color});
			}
			Graphics()->TextureClear();
			if(g_Config.m_ClTeeTrailWidth == 0)
			{
				Graphics()->LinesBegin();
				for(size_t Index = 1; Index < Points.size(); ++Index)
				{
					Graphics()->SetColor(Vertices[Index].m_Color);
					IGraphics::CLineItem Line(Points[Index - 1].m_Position.x, Points[Index - 1].m_Position.y, Points[Index].m_Position.x, Points[Index].m_Position.y);
					Graphics()->LinesDraw(&Line, 1);
				}
				Graphics()->LinesEnd();
			}
			else
			{
				Graphics()->QuadsBegin();
				for(size_t Index = 1; Index < Vertices.size(); ++Index)
				{
					const auto &A = Vertices[Index - 1], &B = Vertices[Index];
					Graphics()->SetColor4(A.m_Color, A.m_Color, B.m_Color, B.m_Color);
					IGraphics::CFreeformItem Quad(A.m_Left, A.m_Right, B.m_Left, B.m_Right);
					Graphics()->QuadsDrawFreeform(&Quad, 1);
				}
				Graphics()->QuadsEnd();
			}
		}
	}

	void CMediumVisuals::RenderNowPlaying()
	{
		const auto Track = m_NowPlaying.Track(g_Config.m_KrxNowPlayingHud != 0);
		const std::string Text = Track.Text();
		if(Text.empty())
		{
			m_NowPlayingSince = 0;
			m_NowPlayingText.clear();
			Graphics()->UnloadTexture(&m_NowPlayingArt);
			m_pNowPlayingArt.reset();
			return;
		}
		if((bool)Track.m_pArt != (bool)m_pNowPlayingArt || (Track.m_pArt && *Track.m_pArt != *m_pNowPlayingArt))
		{
			Graphics()->UnloadTexture(&m_NowPlayingArt);
			m_pNowPlayingArt = Track.m_pArt;
			if(m_pNowPlayingArt)
			{
				CImageInfo Image;
				if(Graphics()->LoadPng(Image, m_pNowPlayingArt->data(), m_pNowPlayingArt->size(), "now-playing-art"))
					m_NowPlayingArt = Graphics()->LoadTextureRawMove(Image, 0, "now-playing-art");
			}
		}
		if(!m_NowPlayingSince)
			m_NowPlayingSince = time_get_impl();
		m_NowPlayingText = Text;
		const float Width = 300.f * Graphics()->ScreenAspect();
		Graphics()->MapScreen(CScreenRect(0, 0, Width, 300));
		const float Size = 6.f;
		std::string First = Text + ' ', Second;
		if(TextRender()->TextWidth(Size, First.c_str()) > 120.f)
		{
			const size_t Half = First.size() / 2;
			size_t Split = First.rfind(' ', Half);
			if(Split == std::string::npos)
				Split = First.find(' ', Half);
			if(Split == std::string::npos)
			{
				Split = Half;
				while(Split < First.size() && (First[Split] & 0xc0) == 0x80)
					++Split;
			}
			Second = First.substr(Split + (First[Split] == ' ' ? 1 : 0));
			First.resize(Split);
		}
		const float TextWidth = std::max(TextRender()->TextWidth(Size, First.c_str()), TextRender()->TextWidth(Size, Second.c_str()));
		const float ArtWidth = m_NowPlayingArt.IsValid() ? 14.f : 0.f;
		const float PanelWidth = TextWidth + 12.f + ArtWidth;
		const float Open = std::clamp((float)(time_get_impl() - m_NowPlayingSince) * 5.f / time_freq(), 0.f, 1.f);
		const float Height = mix(5.f, Second.empty() ? 16.f : 18.f, Open);
		const float X = (Width - PanelWidth) / 2.f;
		CUIRect Panel{X, 18.f, PanelWidth, Height};
		Panel.Draw(ColorRGBA(0, 0, 0, .5f), IGraphics::CORNER_ALL, 2.5f);
		if(Open < 1.f)
			return;
		if(m_NowPlayingArt.IsValid())
		{
			Graphics()->TextureSet(m_NowPlayingArt);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1, 1, 1, 1);
			IGraphics::CQuadItem Artwork(X + 2.f, 18.f + (Height - 12.f) / 2.f, 12.f, 12.f);
			Graphics()->QuadsDrawTL(&Artwork, 1);
			Graphics()->QuadsEnd();
		}
		const auto Draw = [&](float x, float y, const char *pText) {
			GradientText(x, y, Size, pText, g_Config.m_KrxNowPlayingGradientStartColor, g_Config.m_KrxNowPlayingGradientEndColor,
				g_Config.m_KrxNowPlayingGradientRainbow, g_Config.m_KrxNowPlayingSegmented, g_Config.m_KrxNowPlayingGradientSpeed, m_NowPlayingPhase);
		};
		Draw(X + 4.f + ArtWidth, 18.f + (Height - Size) / 2.f - (Second.empty() ? 0.f : 4.f), First.c_str());
		if(!Second.empty())
			Draw(X + 4.f + ArtWidth, 18.f + (Height - Size) / 2.f + 4.f, Second.c_str());
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
		{
			// New13610 31a6f0 uses "%s." before the sixteen-byte copy.
			char aName[128];
			str_format(aName, sizeof(aName), "%s.", Other.m_aName);
			str_copy(g_Config.m_PlayerName, aName);
		}
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
		if(Client()->State() == IClient::STATE_ONLINE)
		{
			GameClient()->m_RescueBots.RenderUnfreeze(GameClient());
			GameClient()->m_Avoid.Render(GameClient());
		}
		const CScreenRect Old = Graphics()->GetScreen();
		if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		{
			RenderNowPlaying();
			Graphics()->MapScreen(Old);
			return;
		}
		const auto &Cam = GameClient()->m_Camera;
		const auto World = Graphics()->MapScreenToWorld(Cam.m_Center.x, Cam.m_Center.y, 100, 100, 100, 0, 0, Graphics()->ScreenAspect(), Cam.m_Zoom);
		Graphics()->MapScreen(World);
		RenderAimLines();
		RenderTargetIndicators();
		RenderTrail();
		RenderHud();
		RenderNowPlaying();
		Graphics()->MapScreen(Old);
	}
}
