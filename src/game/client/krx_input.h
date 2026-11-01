#ifndef GAME_CLIENT_KRX_INPUT_H
#define GAME_CLIENT_KRX_INPUT_H

#include <cstdint>

// The generated protocol transitively uses int32_t. Keep cstdint first.
#include <generated/protocol.h>

namespace OpenKrx
{
	// Leaf input consumers recovered from the supplied image: be580, beae0,
	// bffd0, c05b0, c0600 and bf310. State belongs to a connection, not a server.
	struct CSimpleInputState
	{
		int m_HookSpamCounter = -1;
		bool m_RehookPending = false;
		bool m_PixelWalkLatch = false;

		void ApplyMoonwalk(CNetObj_PlayerInput &Input, const CNetObj_PlayerInput &Last, bool Left, bool Right, bool Enabled)
		{
			if(Enabled && Left && Right)
				Input.m_Direction = Last.m_Direction ? -Last.m_Direction : 1;
		}
		void ApplyAutoFire(CNetObj_PlayerInput &Input, bool Enabled)
		{
			if(Enabled && (unsigned)Input.m_WantedWeapon < 3 && (Input.m_Fire & 1))
				Input.m_Fire = Input.m_Fire > 62 ? 1 : Input.m_Fire + 2;
		}
		void ApplyAutoRehook(CNetObj_PlayerInput &Input, int HookedPlayer, int HookTick, bool Enabled, bool CharacterAvailable)
		{
			if(Enabled && CharacterAvailable)
			{
				if(m_RehookPending)
				{
					m_RehookPending = false;
					Input.m_Hook = 1;
				}
				else if(HookedPlayer != -1 && HookTick > 58)
				{
					m_RehookPending = true;
					Input.m_Hook = 0;
				}
			}
		}
		void ApplyHookSpam(CNetObj_PlayerInput &Input, bool Enabled)
		{
			if(Enabled)
			{
				if(++m_HookSpamCounter > 3)
				{
					if(Input.m_Hook == 1)
						Input.m_Hook = 0;
					else
					{
						Input.m_Hook = 1;
						m_HookSpamCounter = 0;
					}
				}
			}
			else if(m_HookSpamCounter >= 0)
			{
				m_HookSpamCounter = -1;
				Input.m_Hook = 0;
			}
		}
		void ApplyEarly(CNetObj_PlayerInput &Input, const CNetObj_PlayerInput &Last,
			bool Left, bool Right, int HookedPlayer, int HookTick,
			bool Moonwalk, bool AutoFire, bool AutoRehook, bool HookSpam, bool CharacterAvailable = true)
		{
			ApplyMoonwalk(Input, Last, Left, Right, Moonwalk);
			ApplyAutoFire(Input, AutoFire);
			ApplyAutoRehook(Input, HookedPlayer, HookTick, AutoRehook, CharacterAvailable);
			ApplyHookSpam(Input, HookSpam);
		}

		void ApplyLate(CNetObj_PlayerInput &Input, float VelocityX, bool CanMove, bool Grounded,
			bool PixelWalk, bool QuickStop, bool GroundOnly)
		{
			if(PixelWalk)
			{
				if(Input.m_Direction)
				{
					if(m_PixelWalkLatch)
						Input.m_Direction = 0;
					else
						m_PixelWalkLatch = true;
				}
				else
					m_PixelWalkLatch = false;
			}
			if(QuickStop && CanMove && (!GroundOnly || Grounded) && Input.m_Direction == 0)
				Input.m_Direction = VelocityX > 1.0f ? -1 : (VelocityX < -1.0f ? 1 : 0);
		}
		// Standalone fixture adapter. Production keeps the recovered stages
		// separated by Avoid and the other original coordinator consumers.
		void Apply(CNetObj_PlayerInput &Input, const CNetObj_PlayerInput &Last,
			bool Left, bool Right, int HookedPlayer, int HookTick, float VelocityX,
			bool CanMove, bool Grounded, bool Moonwalk, bool AutoFire, bool AutoRehook,
			bool HookSpam, bool PixelWalk, bool QuickStop, bool GroundOnly)
		{
			ApplyEarly(Input, Last, Left, Right, HookedPlayer, HookTick, Moonwalk, AutoFire, AutoRehook, HookSpam);
			ApplyLate(Input, VelocityX, CanMove, Grounded, PixelWalk, QuickStop, GroundOnly);
		}
	};
}
#endif
