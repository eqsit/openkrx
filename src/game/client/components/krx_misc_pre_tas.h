#ifndef GAME_CLIENT_COMPONENTS_KRX_MISC_PRE_TAS_H
#define GAME_CLIENT_COMPONENTS_KRX_MISC_PRE_TAS_H

#include <game/client/component.h>

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <vector>

class CKrxMiscPreTas : public CComponent
{
	struct CNotification
	{
		int m_Type;
		std::string m_Title;
		std::string m_Message;
		int64_t m_Created;
		int64_t m_DurationMs;
	};
	int m_RepeatAfterTick = 0;
	int m_MentionAfterTick = 0;
	bool m_NearFinish = false;
	bool m_Verified = false;
	int m_VerifyFailures = 0;
	bool m_RestoreKillMessages = false;
	std::set<std::string> m_SeenMods, m_SeenSuspects;
	std::array<bool, 128> m_aMods{}, m_aSuspects{};
	std::vector<std::string> m_vKillsays, m_vDeathsays;
	std::vector<CNotification> m_vNotifications;
	std::mutex m_NotificationsMutex;
	std::mt19937 m_NameRandom;
	bool m_NameRandomInitialized = false;
	void ScanModerators();
	void MassMention();
	void AutoVoteKick();
	void ChangeNameNearFinish();
	std::string RandomName();
	void Verify();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnShutdown() override;
	void AfterComponentsUpdate();
	void OnRender() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	// Called inside the original input pipeline; this has no frame timer gate.
	void BeforeInput();
	void VoteBeforeHookSpam();
	// Original 23e670 calls finish-name logic before preparing the input.
	void BeforeSnapInput();
	void Notify(int Type, const char *pTitle, const char *pMessage, int64_t DurationMs = 5000);
	bool ModeratorDetected() const;
	bool SuspiciousDetected() const;
	bool IsModerator(int ClientId) const;
	bool IsSuspicious(int ClientId) const;
	static std::string GenerateReferenceName(std::mt19937 &Random);
	static std::string FancyChat(const char *pText);
	static std::string RepeatedChat(const char *pText);
	static uint32_t ReferenceUniform(std::mt19937 &Random, uint32_t Count);
	static void ShuffleReferenceIds(std::vector<int> &vIds, std::mt19937 &Random);
	static unsigned ReferenceRand();
	static void ReferenceSrand(unsigned Seed);
};

#endif
