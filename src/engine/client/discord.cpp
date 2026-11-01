#include <base/dbg.h>
#include <base/mem.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/discord.h>

#include <string>
#if defined(CONF_DISCORD)
#include <discord_game_sdk.h>
#if defined(CONF_DISCORD_DYNAMIC)
#include <dlfcn.h>
#endif

// Original 1192b0 selects these once after config/autoexec, before CLI args.
// 11aa70 creates the core even when RPC display is disabled; 11a9a0/11ac50
// gate activity updates using that startup snapshot. Callbacks are unconditional.
class CDiscord : public IDiscord
{
	IDiscordCore *m_pCore = nullptr;
	IDiscordActivityEvents m_ActivityEvents{};
	IDiscordActivityManager *m_pActivityManager = nullptr;
	bool m_Enabled = false;
	bool m_Initialized = false;

public:
	~CDiscord() override
	{
		if(m_pCore)
			m_pCore->destroy(m_pCore);
	}
	void Initialize(bool Enabled, int ApplicationId, const char *pCustomId) override
	{
		if(m_Initialized)
			return;
		m_Initialized = true;
		m_Enabled = Enabled;
		static constexpr int64_t aIds[] = {0x1267790b24401065LL, 0x0a703aca7c800033LL, 0x1264a1386802001fLL};
		const int64_t Id = ApplicationId == 3 ? std::stoll(pCustomId) : aIds[ApplicationId];
		DiscordCreateParams Params;
		DiscordCreateParamsSetDefault(&Params);
		Params.client_id = Id;
		Params.flags = DiscordCreateFlags_NoRequireDiscord;
		Params.event_data = this;
		Params.activity_events = &m_ActivityEvents;
#if defined(CONF_DISCORD_DYNAMIC)
		auto *pLibrary = dlopen("discord_game_sdk.so", RTLD_NOW);
		if(!pLibrary)
			return;
		auto Create = reinterpret_cast<decltype(&DiscordCreate)>(dlsym(pLibrary, "DiscordCreate"));
		if(!Create)
			return;
		const int Error = Create(DISCORD_VERSION, &Params, &m_pCore);
#else
		const int Error = DiscordCreate(DISCORD_VERSION, &Params, &m_pCore);
#endif
		if(Error != DiscordResult_Ok)
		{
			dbg_msg("discord", "error initializing discord instance, error=%d", Error);
			m_pCore = nullptr;
			return;
		}
		m_pActivityManager = m_pCore->get_activity_manager(m_pCore);
		ClearGameInfo();
	}
	void Update() override
	{
		if(m_pCore)
			m_pCore->run_callbacks(m_pCore);
	}
	void Activity(const char *pDetails, const char *pMap)
	{
		if(!m_Enabled || !m_pActivityManager)
			return;
		DiscordActivity Activity{};
		str_copy(Activity.assets.large_image, "ddnet_logo");
		str_copy(Activity.assets.large_text, "DDNet logo");
		Activity.timestamps.start = time_timestamp();
		str_copy(Activity.details, pDetails);
		str_copy(Activity.state, pMap);
		m_pActivityManager->update_activity(m_pActivityManager, &Activity, nullptr, nullptr);
	}
	void ClearGameInfo() override { Activity("Offline", ""); }
	void SetGameInfo(const CServerInfo &ServerInfo, bool Registered) override { Activity("Online", ServerInfo.m_aMap); }
	void UpdateServerInfo(const CServerInfo &) override {}
	void UpdatePlayerCount(int) override {}
};
#else
class CDiscord : public IDiscord
{
	void Update() override {}
	void ClearGameInfo() override {}
	void SetGameInfo(const CServerInfo &, bool) override {}
	void UpdateServerInfo(const CServerInfo &) override {}
	void UpdatePlayerCount(int) override {}
};
#endif
IDiscord *CreateDiscord() { return new CDiscord(); }
