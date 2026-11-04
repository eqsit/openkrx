#include "replay_fake_aim.h"

#include <engine/shared/config.h>

#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/gameworld.h>

#include <array>
#include <cmath>
#include <random>

namespace OpenKrx
{
	namespace
	{
		bool WillFire(const CCharacter &Character, const CNetObj_PlayerInput &Input)
		{
			const auto &Core = *Character.Core();
			const int Weapon = Core.m_ActiveWeapon;
			if(Weapon < 0 || Weapon >= NUM_WEAPONS || !Core.m_aWeapons[Weapon].m_Ammo || Character.m_FreezeTime || Character.DiagnosticReloadTimer() ||
				(!g_Config.m_SvDeepfly && Weapon == WEAPON_HAMMER && Core.m_DeepFrozen))
				return false;
			const bool FullAuto = Weapon == WEAPON_SHOTGUN || Weapon == WEAPON_GRENADE || Weapon == WEAPON_LASER ||
					      (Weapon == WEAPON_GUN && Core.m_Jetpack) || Character.m_FrozenLastTick;
			return (FullAuto && (Input.m_Fire & 1)) || CountInput(Character.DiagnosticLatestInput().m_Fire, Input.m_Fire).m_Presses;
		}
		vec2 Aim(vec2 Value, float Radius)
		{
			const float Length = length(Value);
			if(Length == 0)
				return vec2(0, 0);
			return vec2((float)(int)(Value.x / Length * Radius), (float)(int)(Value.y / Length * Radius));
		}
		void Tick(CGameWorld &World, const std::array<int, 2> &aIds, const CReplay &Replay, size_t Frame)
		{
			for(int Slot = 0; Slot < 2; ++Slot)
				if(Frame < Replay.m_aFrames[Slot].size())
					if(auto *pCharacter = World.GetCharacterById(aIds[Slot]))
						pCharacter->OnDirectInput(&Replay.m_aFrames[Slot][Frame].m_Input);
			++World.m_GameTick;
			for(int Slot = 0; Slot < 2; ++Slot)
				if(Frame < Replay.m_aFrames[Slot].size())
					if(auto *pCharacter = World.GetCharacterById(aIds[Slot]))
						pCharacter->OnPredictedInput(&Replay.m_aFrames[Slot][Frame].m_Input);
			World.Tick();
			World.m_PredictedEvents.clear();
		}
		// 35cd80: an edit is safe only if all remaining recorded positions match.
		bool ValidateSuffix(const CReplay &Replay, const CGameWorld &Initial, const std::array<int, 2> &aIds, size_t Begin, bool ReportMismatch = false)
		{
			CGameWorld World;
			World.CopyWorldIsolated(&Initial);
			World.m_WorldConfig.m_KrxPrediction = true;
			for(size_t I = Begin; I < Replay.m_aFrames[0].size(); ++I)
			{
				Tick(World, aIds, Replay, I);
				for(int Slot = 0; Slot < 2; ++Slot)
					if(I < Replay.m_aFrames[Slot].size())
					{
						const auto *pCharacter = World.GetCharacterById(aIds[Slot]);
						if(!pCharacter || pCharacter->Core()->m_Pos != Replay.m_aFrames[Slot][I].m_Position)
						{
							if(ReportMismatch && g_Config.m_AvoidDebug)
							{
								const vec2 Expected = Replay.m_aFrames[Slot][I].m_Position;
								const vec2 Actual = pCharacter ? pCharacter->Core()->m_Pos : vec2(0, 0);
								dbg_msg("tas-validate", "source mismatch frame=%zu slot=%d id=%d actual=%.6f,%.6f expected=%.6f,%.6f", I, Slot, aIds[Slot], Actual.x, Actual.y, Expected.x, Expected.y);
							}
							return false;
						}
					}
			}
			return true;
		}
	}

	void ResetReplayFakeAim(CReplay &Replay)
	{
		Replay.m_AimPrepared = false;
		for(auto &vFrames : Replay.m_aFrames)
			for(auto &Frame : vFrames)
				Frame.m_AimEditable = false;
	}

	bool AddReplayFakeAim(CReplay &Replay, const CGameWorld &Initial, const std::array<int, 2> &aIds, int Mode, float MouseRadius)
	{
		if(Replay.m_aFrames[0].empty())
			return false;
		if(!ValidateSuffix(Replay, Initial, aIds, 0, true))
			return false;
		// The native port retains atomic commit and isolated initial snapshots.
		CReplay Modified = Replay;
		CGameWorld World;
		World.CopyWorldIsolated(&Initial);
		World.m_WorldConfig.m_KrxPrediction = true;
		Tick(World, aIds, Modified, 0);
		// 3556e0 / 34fc40: Robot first collapses each safe aim change to the
		// previous frame's aim. A per-frame marker survives later mode changes.
		for(size_t I = 1; !Modified.m_AimPrepared && I < Modified.m_aFrames[0].size(); ++I)
		{
			for(int Slot = 0; Slot < 2; ++Slot)
			{
				if(I >= Modified.m_aFrames[Slot].size())
					continue;
				auto *pCharacter = World.GetCharacterById(aIds[Slot]);
				if(!pCharacter)
					return false;
				auto &Frame = Modified.m_aFrames[Slot][I];
				const auto &Previous = Modified.m_aFrames[Slot][I - 1].m_Input;
				if((Previous.m_Hook == 0 && Frame.m_Input.m_Hook == 1) || WillFire(*pCharacter, Frame.m_Input))
				{
					Frame.m_AimEditable = false;
					continue;
				}
				if(Frame.m_Input.m_TargetX == Previous.m_TargetX && Frame.m_Input.m_TargetY == Previous.m_TargetY)
					continue;
				const auto Before = Frame.m_Input;
				Frame.m_Input.m_TargetX = Previous.m_TargetX;
				Frame.m_Input.m_TargetY = Previous.m_TargetY;
				Frame.m_AimEditable = ValidateSuffix(Modified, World, aIds, I);
				if(!Frame.m_AimEditable)
					Frame.m_Input = Before;
			}
			Tick(World, aIds, Modified, I);
		}
		Modified.m_AimPrepared = true;
		static float s_SpinAngle = 0;
		for(auto &vFrames : Modified.m_aFrames)
		{
			if(Mode == 1 || Mode == 2)
			{
				for(auto &Frame : vFrames)
					if(Frame.m_AimEditable)
					{
						float Angle;
						if(Mode == 1)
						{
							s_SpinAngle += .7861844f;
							if(s_SpinAngle < 0)
								s_SpinAngle += 2 * pi;
							else if(s_SpinAngle > 2 * pi)
								s_SpinAngle -= 2 * pi;
							Angle = s_SpinAngle;
						}
						else
						{
							std::mt19937 Random(std::random_device{}());
							Angle = (float)(Random() >> 8) * 0x1p-24f * 2 * pi;
						}
						const vec2 Value = Aim(direction(Angle), MouseRadius);
						Frame.m_Input.m_TargetX = (int)Value.x;
						Frame.m_Input.m_TargetY = (int)Value.y;
					}
			}
			else if(Mode == 3)
			{
				for(size_t I = 0; I < vFrames.size();)
				{
					if(!vFrames[I].m_AimEditable)
					{
						++I;
						continue;
					}
					const size_t Begin = I;
					while(I < vFrames.size() && vFrames[I].m_AimEditable)
						++I;
					if(Begin == 0 || I == vFrames.size())
						continue;
					const auto &Before = vFrames[Begin - 1].m_Input;
					const auto &After = vFrames[I].m_Input;
					for(size_t J = Begin; J < I; ++J)
					{
						const float Alpha = (float)(J - (Begin - 1)) / (float)(I - (Begin - 1));
						const vec2 Raw((float)(int)(((float)After.m_TargetX - Before.m_TargetX) * Alpha + Before.m_TargetX),
							(float)(int)(((float)After.m_TargetY - Before.m_TargetY) * Alpha + Before.m_TargetY));
						const vec2 Value = Aim(Raw, MouseRadius);
						vFrames[J].m_Input.m_TargetX = (int)Value.x;
						vFrames[J].m_Input.m_TargetY = (int)Value.y;
					}
				}
			}
		}
		if(!ValidateSuffix(Modified, Initial, aIds, 0))
			return false;
		Replay = std::move(Modified);
		return true;
	}
}
