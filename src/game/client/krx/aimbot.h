#ifndef GAME_CLIENT_KRX_AIMBOT_H
#define GAME_CLIENT_KRX_AIMBOT_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

class CGameClient;
class CGameWorld;

namespace OpenKrx
{
	struct CAimbotWeaponSettings
	{
		bool m_Enabled = false;
		int m_Fov = 30;
		int m_Priority = 0;
		bool m_Silent = true;
		bool m_IgnoreFriends = false;
	};

	struct CAimbotSettings
	{
		bool m_Enabled = false;
		bool m_AutoHook = false;
		bool m_AutoShoot = false;
		CAimbotWeaponSettings m_Hook{true, 30, 0, true, false};
		std::array<CAimbotWeaponSettings, NUM_WEAPONS> m_aWeapons{};
		bool m_Advanced = false;
		int m_Accuracy = 8;
		int m_ShotPercentage = 100;
		int m_MinShootLength = 1;
		int m_MaxShootLength = 8;
		bool m_GrenadeMovePrediction = false;
		// Set when this tick's hook was injected by other KRX automation (Avoid
		// rescue, Hook Nearest auto hook, hook spam/rehook) instead of the
		// player. Such a hook owns its direction and aim assistance must not
		// steer it.
		bool m_HookIsAutomated = false;
	};

	struct CAimbotDecision
	{
		CNetObj_PlayerInput m_Input{};
		bool m_ChangeVisibleAim = false;
		vec2 m_VisibleAim{};
		int m_HookTarget = -1;
		int m_WeaponTarget = -1;
	};

	class CAimbot
	{
	public:
		void OnReset();
		CAimbotDecision AdjustInput(CGameClient *pGameClient, CNetObj_PlayerInput Input,
			int ClientId, vec2 PhysicalMouse, const CAimbotSettings &Settings);
		CAimbotDecision SearchWorld(const CGameWorld &Source, CNetObj_PlayerInput Input,
			int ClientId, vec2 PhysicalMouse, const CAimbotSettings &Settings,
			float MouseRadius = 400.0f, bool TeamPlay = false,
			const std::array<bool, MAX_CLIENTS> *pFriends = nullptr);
		std::optional<vec2> FindDragAim(const CGameWorld &Source, int ClientId,
			CGameClient *pGameClient, vec2 PhysicalMouse) const;

		int HookTarget() const { return m_HookTarget; }
		int WeaponTarget() const { return m_WeaponTarget; }

	private:
		struct CTarget
		{
			int m_ClientId;
			bool m_Frozen;
			vec2 m_LocalPosition;
			vec2 m_Difference;
			bool m_Friend;
		};
		CAimbotDecision Search(const CGameWorld &Source, CGameClient *pGameClient,
			CNetObj_PlayerInput Input, int ClientId, vec2 PhysicalMouse,
			const CAimbotSettings &Settings, float MouseRadius, bool TeamPlay,
			const std::array<bool, MAX_CLIENTS> *pFriends);
		std::vector<CTarget> m_vCachedTargets;
		vec2 m_CachedLocalPosition{};
		float m_CachedHookLength = 380.0f;
		CAimbotSettings m_CachedSettings{};
		int m_HookTarget = -1;
		int m_WeaponTarget = -1;
		int m_ClientId = -1;
		int m_Slot = -1;
		vec2 m_HookAim{};
		bool m_HookAcquired = false;
		bool m_AutoHookIssued = false;
		int m_FireLength = 0;
		int m_FireCounter = -1;
	};

} // namespace OpenKrx

#endif
