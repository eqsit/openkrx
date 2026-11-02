#include "replay_fake_aim.h"

#include <engine/shared/config.h>

#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/gameworld.h>

#include <array>
#include <cmath>
#include <deque>
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
		void Tick(CGameWorld &World, const std::array<int, 2> &aIds, const std::array<CNetObj_PlayerInput, 2> &aInputs)
		{
			for(int Slot = 0; Slot < 2; ++Slot)
				if(auto *pCharacter = World.GetCharacterById(aIds[Slot]))
					pCharacter->OnDirectInput(&aInputs[Slot]);
			++World.m_GameTick;
			for(int Slot = 0; Slot < 2; ++Slot)
				if(auto *pCharacter = World.GetCharacterById(aIds[Slot]))
					pCharacter->OnPredictedInput(&aInputs[Slot]);
			World.Tick();
			World.m_PredictedEvents.clear();
		}
	}

	bool AddReplayFakeAim(CReplay &Replay, const CGameWorld &Initial, const std::array<int, 2> &aIds, int Mode, float MouseRadius)
	{
		if(Replay.m_aFrames[0].empty())
			return false;
		struct CCritical
		{
			CNetObj_PlayerInput m_Input;
			int m_Tick;
		};
		std::array<std::deque<CCritical>, 2> aCritical;
		CGameWorld World;
		World.CopyWorldIsolated(&Initial);
		World.m_WorldConfig.m_KrxPrediction = true;
		std::array<CNetObj_PlayerInput, 2> aInputs{};
		for(size_t I = 0; I < Replay.m_aFrames[0].size(); ++I)
		{
			for(int Slot = 0; Slot < 2; ++Slot)
			{
				if(I >= Replay.m_aFrames[Slot].size())
					continue;
				aInputs[Slot] = Replay.m_aFrames[Slot][I].m_Input;
				auto *pCharacter = World.GetCharacterById(aIds[Slot]);
				if(pCharacter && I > 0 &&
					((!pCharacter->DiagnosticLatestInput().m_Hook && aInputs[Slot].m_Hook == 1) || WillFire(*pCharacter, aInputs[Slot])))
					aCritical[Slot].push_back({aInputs[Slot], (int)I});
			}
			Tick(World, aIds, aInputs);
		}
		World.CopyWorldIsolated(&Initial);
		World.m_WorldConfig.m_KrxPrediction = true;
		std::array<int, 2> aNextTick{-1, -1};
		std::array<std::deque<vec2>, 2> aAims;
		static float s_SpinAngle = 0;
		for(size_t I = 0; I < Replay.m_aFrames[0].size(); ++I)
		{
			for(int Slot = 0; Slot < 2; ++Slot)
			{
				if(I >= Replay.m_aFrames[Slot].size())
					continue;
				aInputs[Slot] = Replay.m_aFrames[Slot][I].m_Input;
				auto *pCharacter = World.GetCharacterById(aIds[Slot]);
				if(!pCharacter || I == 0)
					continue;
				const bool Firing = WillFire(*pCharacter, aInputs[Slot]);
				if(aNextTick[Slot] < (int)I && !aCritical[Slot].empty())
				{
					const auto Next = aCritical[Slot].front();
					aCritical[Slot].pop_front();
					aNextTick[Slot] = Next.m_Tick;
					aAims[Slot].clear();
					if(Next.m_Tick > 1)
					{
						const int Gap = Next.m_Tick - (int)I;
						const int Count = Mode == 0 ? Next.m_Tick : Gap;
						for(int J = 0; J < Count; ++J)
						{
							vec2 Value(Next.m_Input.m_TargetX, Next.m_Input.m_TargetY);
							if(Mode == 1)
							{
								s_SpinAngle += .7861844f;
								if(s_SpinAngle < 0)
									s_SpinAngle += 2 * pi;
								else if(s_SpinAngle > 2 * pi)
									s_SpinAngle -= 2 * pi;
								Value = direction(s_SpinAngle);
							}
							else if(Mode == 2)
							{
								std::mt19937 Random(std::random_device{}());
								Value = direction((float)(Random() >> 8) * 0x1p-24f * 2 * pi);
							}
							else if(Mode == 3)
							{
								const float From = std::atan2((float)aInputs[Slot].m_TargetY, (float)aInputs[Slot].m_TargetX);
								const float To = std::atan2((float)Next.m_Input.m_TargetY, (float)Next.m_Input.m_TargetX);
								float Delta = To - From;
								if(Delta > pi)
									Delta -= 2 * pi;
								else if(Delta < -pi)
									Delta += 2 * pi;
								Value = direction(From + Delta * ((float)J / Gap));
							}
							aAims[Slot].push_back(Aim(Value, MouseRadius));
						}
					}
				}
				// A held hook's aim is free; its initial press must be preserved.
				if((pCharacter->DiagnosticLatestInput().m_Hook != 0 || aInputs[Slot].m_Hook != 1) && !Firing)
				{
					vec2 Value(0, 0);
					if(!aAims[Slot].empty())
					{
						Value = aAims[Slot].front();
						aAims[Slot].pop_front();
					}
					if(!pCharacter->m_FrozenLastTick && pCharacter->m_FreezeTime == 0)
					{
						aInputs[Slot].m_TargetX = (int)Value.x;
						aInputs[Slot].m_TargetY = (int)Value.y;
						Replay.m_aFrames[Slot][I].m_Input = aInputs[Slot];
					}
				}
			}
			Tick(World, aIds, aInputs);
			for(int Slot = 0; Slot < 2; ++Slot)
				if(I < Replay.m_aFrames[Slot].size())
				{
					auto *pCharacter = World.GetCharacterById(aIds[Slot]);
					if(pCharacter && pCharacter->Core()->m_Pos != Replay.m_aFrames[Slot][I].m_Position)
						return false;
				}
		}
		return true;
	}
}
