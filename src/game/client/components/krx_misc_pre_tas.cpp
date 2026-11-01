#include "krx_misc_pre_tas.h"

#include <base/dbg.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/console.h>
#include <engine/external/json-parser/json.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/client/ui_rect.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iterator>

namespace
{
#include "krx_name_data.h"
	// MSVC's rand state and algorithm (REA 3f35fc); using libc rand on Linux changes
	// the generator and its 15-bit range. The original stream starts with seed1.
	thread_local uint32_t s_ReferenceRand = 1;
	const char *const s_apDdnetMods[] = {
		"deen",
		"Learath2",
		"Robyt3",
		"murpi",
		"snailx3",
		"Cendren",
		"Insanity",
		"Iza",
		"Jaguar",
		"jana",
		"Lagar",
		"Neben",
		"OguzKK",
		"ReD",
		"Vy0x2.'",
		"Yubel",
		"-StormAx",
		":v",
		"Deeper",
		"Ham5terzilla",
		"Lain?",
		"<~{Barsik}~>",
		"Coca Cola",
		"commander",
		"Deëivid!",
		"El YeK :v",
		"Diamond",
		"JOYSOYUM",
		"Skaydox",
		"daria",
		"Hanz-ddnet",
		"noby",
		"Skeith",
		"tela",
		"Welshi",
		"YupHio",
		"|GH0sT|",
		"garry",
		"Mariac",
		"XxDarkNezZ",
		"cheeser0613",
		"Gloomy",
		"I'm suck",
		"k1r1ha",
		"kyokaiwei",
		"Shiriusu",
		"'^'",
		"Ark",
		"Dust",
		"fall",
		"FightFire",
		"KuNao",
		"owanoyano",
		"Rance",
		"Rice",
		"Schroedinger",
		"confetti",
		"Texas.C",
		"TsFreddie",
		"wuu",
		"xiaocan",
		"满月",
		"心灵",
		"十八",
		"摩西摩西",
		"0x314",
		"APFFF",
		"AvapiX",
		"Bota",
		"Devinci",
		"Draggory",
		"Exotik",
		"Fussel",
		"Gumba",
		"Gwendal",
		"Maya",
		"Mery",
		"Paralix",
		"Rheme",
		"Shadowsun",
		"Zeral",
		"zhn",
		"ƤØŢĀŤ0Co",
		"Anime.pdf",
		"tortilla",
		"limo",
		"Lukov",
		"strauberi",
		"Lem0n",
	};
	const char *const s_apKogMods[] = {
		"Avolicious",
		"Bʟυɛ ♡",
		"Gummibaer12345",
		"Jore",
		"QshaR",
		"feat",
		"çraçk",
		"Andrew~",
		"Coople",
		"HLLFW-響",
		"Kime1627",
		"KoG Verify Bot",
		"Quinn",
		"Risky",
		"TsFreddie",
		"se",
		"spiriT ♡",
		"~Nertato~",
		"Dave",
		"Phumfel",
		"RaeDil",
		"Sebas GD",
		"Smash",
		"Vida",
		"ZonsaSmiley Kiddah",
	};
	const char *const s_apSuspects[] = {
		"Arrow",
		"Nehr",
		"Cøke",
		"DarkOort",
		"Kicker",
		"louis",
		"Savalic",
		"VéNa",
		"Afterburn",
		"Bo₩ser",
		"FlipLipp",
		"Hawerer",
		"Krumplee",
		"elnoid",
		"ipoopi",
		"mind",
		"2b",
		"=) 'SaT -A",
		"=646838=",
		"=Denkst du=",
		"AMIR-GHIYAMAT",
		"ASideSilence",
		"Alan",
		"Alexis>:c",
		"Alina",
		"AllesKlar",
		"Apostle",
		"BigMaschine",
		"Bonny",
		"Bud-Tee",
		"Busted",
		"Butzkâ/nf",
		"Caluka",
		"Cecily",
		"Cejota",
		"Colgate.",
		"Corazon",
		"Crabsen",
		"De1ete!?",
		"Deploid",
		"Dezq",
		"Dom",
		"Drakenmake",
		"Evil_Chicken",
		"FAST^",
		"FUMOFFU",
		"Fexa",
		"FlameKing",
		"FloLike",
		"Fre0cky",
		"Frozen",
		"Fumm",
		"GaLaXy",
		"Gandalf",
		"HAZE",
		"Harvy",
		"Hawk",
		"Heyng",
		"Hiddentest",
		"IN$ANITY",
		"Jawsh",
		"Juicy",
		"Juppel",
		"JustSayAnni",
		"Kaelσσ",
		"Kaluza.*",
		"King",
		"Kingston",
		"Ky Ky",
		"Lans",
		"Larlok",
		"Lea",
		"Lemon :D",
		"M@TR!X",
		"Mamax_du_51",
		"Marco",
		"Marmare",
		"Meow~",
		"Meraki",
		"Migget",
		"Mizkibo",
		"Miễn phí",
		"MonkeyDream",
		"MyStery.Fox",
		"Myles",
		"Mytii",
		"Nacht",
		"Neox",
		"Neø",
		"Nizzza",
		"Noe",
		"Noez",
		"Nones",
		"OnePiece",
		"PaHDeRa",
		"Problemkind.",
		"Protokol T'nP",
		"Psycho®",
		"Qiωi",
		"QzBoY",
		"Ra!n",
		"Raypak",
		"Riod",
		"Rips",
		"Roadrunner",
		"Saki`",
		"Shane",
		"ShankGos",
		"Shiiir0",
		"Shoyyy",
		"Shyzo",
		"SickJokee",
		"SlyDzE",
		"SnipeZ²",
		"Soyler",
		"Spooky",
		"Syntax",
		"Techkill",
		"Treatment",
		"Vampy:3",
		"VéNa",
		"WATÓN CON ASMA",
		"WWWWW",
		"Weedbärchiii",
		"Would?",
		"Xin",
		"Yuslo",
		"ahl",
		"ayy dragan",
		"b0gman",
		"bzw",
		"cappo 'SaT -Å",
		"cooper",
		"gdin",
		"henki",
		"kamillentee",
		"krümelmonster",
		"miks.",
		"mpampas",
		"negraldo",
		"opto",
		"r4ptur3",
		"rqza",
		"siиσs",
		"tra!n",
		"zale",
		"~Erre",
		"»Ƥϵtϵr",
		"ȼařǿŁiή☆",
		"ʌωιν",
		"тokyo",
		"Ҡασz",
		"₱aulmast",
		"定℘Ƭ͢Ʀ",
		"狗狗Bala",
		"purp",
	};

	template<size_t N>
	bool HasName(const char *pName, const char *const (&apNames)[N])
	{
		return std::any_of(std::begin(apNames), std::end(apNames), [pName](const char *pValue) { return str_comp(pName, pValue) == 0; });
	}
} // namespace

unsigned CKrxMiscPreTas::ReferenceRand()
{
	s_ReferenceRand = s_ReferenceRand * 214013u + 2531011u;
	return (s_ReferenceRand >> 16) & 0x7fffu;
}

void CKrxMiscPreTas::ReferenceSrand(unsigned Seed)
{
	s_ReferenceRand = Seed;
}

uint32_t CKrxMiscPreTas::ReferenceUniform(std::mt19937 &Random, uint32_t Count)
{
	// 2b4880 uses a fresh MT engine and MSVC's multiply-high rejection sampler.
	uint64_t Product = uint64_t(Random()) * Count;
	while(uint32_t(Product) < uint32_t(-Count) % Count)
		Product = uint64_t(Random()) * Count;
	return uint32_t(Product >> 32);
}

void CKrxMiscPreTas::ShuffleReferenceIds(std::vector<int> &vIds, std::mt19937 &Random)
{
	// 272600 uses the older modulo/rejection shuffle adapter. Its low-bit
	// sampling is different from uniform_int_distribution used for the prefix.
	const uint64_t Max = std::mt19937::max();
	for(size_t i = 1; i < vIds.size(); ++i)
	{
		const uint64_t Count = i + 1;
		uint64_t Draw;
		do
			Draw = Random();
		while(Max / Count <= Draw / Count && Max % Count != i);
		std::swap(vIds[i], vIds[Draw % Count]);
	}
}

std::string CKrxMiscPreTas::FancyChat(const char *pText)
{
	// REA272090: preserve commands and every non-ASCII byte verbatim.
	if(pText[0] == '/')
		return pText;
	std::string Result;
	for(const unsigned char *p = reinterpret_cast<const unsigned char *>(pText); *p; ++p)
	{
		if((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))
		{
			Result.append("\xf0\x9d\x95", 3);
			Result.push_back(static_cast<char>((*p >= 'A' && *p <= 'Z' ? *p + 32 : *p) + 49));
		}
		else
			Result.push_back(static_cast<char>(*p));
	}
	return Result;
}

std::string CKrxMiscPreTas::RepeatedChat(const char *pText)
{
	// The source alternates by byte index, including spaces/punctuation.
	// Its C locale leaves non-ASCII bytes untouched.
	std::string Result(pText);
	for(size_t i = 0; i < Result.size(); ++i)
	{
		const unsigned char Byte = static_cast<unsigned char>(Result[i]);
		if(i & 1)
			Result[i] = Byte >= 'a' && Byte <= 'z' ? Byte - 32 : Byte;
		else
			Result[i] = Byte >= 'A' && Byte <= 'Z' ? Byte + 32 : Byte;
	}
	return Result;
}

void CKrxMiscPreTas::OnInit()
{
	m_vKillsays.clear();
	m_vDeathsays.clear();
	void *pData = nullptr;
	unsigned Length = 0;
	if(!Storage()->ReadFile("krx/killsays.json", IStorage::TYPE_ALL, &pData, &Length))
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "krx_killsays", "Error: File does not exist: data/krx/killsays.json");
		return;
	}
	json_value *pRoot = json_parse(static_cast<const json_char *>(pData), Length);
	free(pData);
	if(!pRoot)
		return;
	const auto ReadArray = [&](const char *pKey, std::vector<std::string> &vDestination) {
		const json_value &Values = (*pRoot)[pKey];
		const auto ReadString = [&](const json_value &Value) {
			if(Value.type == json_string)
				vDestination.emplace_back(Value.u.string.ptr, Value.u.string.length);
		};
		if(Values.type != json_array)
			dbg_msg("FileHelper", "File doesn't contain: `%s`.", pKey);
		if(Values.type == json_array)
			for(unsigned i = 0; i < Values.u.array.length; ++i)
				ReadString(Values[i]);
		else if(Values.type == json_object)
		{
			// 296a40 still iterates a non-array value after logging the type.
			// nlohmann's default object iterator visits keys in byte order.
			std::vector<const json_object_entry *> vEntries;
			for(const auto &Entry : Values.u.object)
				vEntries.push_back(&Entry);
			std::sort(vEntries.begin(), vEntries.end(), [](const auto *pLeft, const auto *pRight) { return str_comp(pLeft->name, pRight->name) < 0; });
			for(const auto *pEntry : vEntries)
				ReadString(*pEntry->value);
		}
		else
			ReadString(Values);
	};
	ReadArray("killsays", m_vKillsays);
	ReadArray("deathsays", m_vDeathsays);
	json_value_free(pRoot);
}

void CKrxMiscPreTas::OnReset()
{
	m_RepeatAfterTick = m_MentionAfterTick = 0;
	m_NearFinish = false;
	m_aMods.fill(false);
	m_aSuspects.fill(false);
	// AutoVerify's completion/failure latch is process-global in the reference.
	// Notifications and already-seen names likewise survive connection changes.
}

void CKrxMiscPreTas::OnShutdown()
{
	const std::lock_guard<std::mutex> Lock(m_NotificationsMutex);
	m_vNotifications.clear();
}

void CKrxMiscPreTas::OnMessage(int MsgType, void *pRawMsg)
{
	// The original chat and kill-message dispatchers suppress seek/reset events
	// before invoking 235ea0 and 26f0e0 respectively.
	if(GameClient()->m_SuppressEvents)
		return;
	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		const auto *pMsg = static_cast<const CNetMsg_Sv_Chat *>(pRawMsg);
		const int Id = pMsg->m_ClientId;
		const int Tick = Client()->GameTick(g_Config.m_ClDummy);
		if(g_Config.m_KrxChatRepeater && m_RepeatAfterTick < Tick && Id >= 0 && Id < MAX_CLIENTS &&
			Id != LocalId && GameClient()->m_aClients[Id].m_Active && str_length(pMsg->m_pMessage) >= g_Config.m_KrxChatRepeaterLength)
		{
			const std::string Repeated = RepeatedChat(pMsg->m_pMessage);
			GameClient()->m_Chat.SendChat(0, Repeated.c_str());
			m_RepeatAfterTick = Tick + 250;
		}
	}
	else if(MsgType == NETMSGTYPE_SV_KILLMSG && g_Config.m_KrxKillsay)
	{
		const auto *pMsg = static_cast<const CNetMsg_Sv_KillMsg *>(pRawMsg);
		if(pMsg->m_Killer == pMsg->m_Victim || ((GameClient()->m_GameWorld.m_WorldConfig.m_IsFNG || str_find(Client()->ServerInfo().m_aGameType, "fng")) && pMsg->m_Weapon != WEAPON_NINJA))
			return;
		const auto Say = [&](int OtherId, const std::vector<std::string> &vPhrases) {
			if(OtherId < 0 || OtherId >= MAX_CLIENTS || vPhrases.empty())
				return;
			const std::string &Phrase = vPhrases[ReferenceRand() % vPhrases.size()];
			if(Phrase.empty())
				return;
			char aText[255];
			str_format(aText, sizeof(aText), "%s: %s", GameClient()->m_aClients[OtherId].m_aName, Phrase.c_str());
			GameClient()->m_Chat.SendChat(0, aText);
		};
		if(pMsg->m_Killer == LocalId)
			Say(pMsg->m_Victim, m_vKillsays);
		if(pMsg->m_Victim == LocalId)
			Say(pMsg->m_Killer, m_vDeathsays);
	}
}

void CKrxMiscPreTas::ScanModerators()
{
	if(!g_Config.m_KrxModDetector)
	{
		m_aMods.fill(false);
		m_aSuspects.fill(false);
		m_SeenMods.clear();
		m_SeenSuspects.clear();
		return;
	}
	const auto &Server = Client()->ServerInfo();
	const bool Kog = str_find(Server.m_aName, "|*KoG*|") || str_find(Server.m_aGameType, "Gores");
	const bool Ddnet = str_find(Server.m_aName, "DDNet");
	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	std::vector<std::string> vMods, vSuspects;
	m_aMods.fill(false);
	m_aSuspects.fill(false);
	for(int Id = 0; Id < std::min<int>(MAX_CLIENTS, 64); ++Id)
	{
		const auto &Other = GameClient()->m_aClients[Id];
		if(Id == LocalId || !Other.m_Active)
			continue;
		const bool ModName = (Kog && HasName(Other.m_aName, s_apKogMods)) ||
				     (!Kog && Ddnet && HasName(Other.m_aName, s_apDdnetMods)) ||
				     (!Kog && !Ddnet && (HasName(Other.m_aName, s_apKogMods) || HasName(Other.m_aName, s_apDdnetMods)));
		m_aMods[Id] = Other.m_AuthLevel != 0 || (g_Config.m_KrxModDetectorNames && ModName);
		m_aSuspects[Id] = g_Config.m_KrxModDetectorWarn && HasName(Other.m_aName, s_apSuspects);
		if(m_aMods[Id])
			vMods.emplace_back(Other.m_aName);
		if(m_aSuspects[Id])
			vSuspects.emplace_back(Other.m_aName);
	}
	if(!vMods.empty() && g_Config.m_KrxModDetectorLeave)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "mod_detector", "Disconnecting due to mod detection...");
		Client()->Disconnect();
	}
	if(!vSuspects.empty() && g_Config.m_KrxModDetectorWarnLeave)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "mod_detector", "Disconnecting due to sus player detection...");
		Client()->Disconnect();
	}
	const auto Log = [&](const std::vector<std::string> &vNames, std::set<std::string> &Seen, const char *pKind) {
		bool New = false;
		for(const std::string &Name : vNames)
			New |= Seen.insert(Name).second;
		if(!New)
			return;
		char aText[256];
		str_format(aText, sizeof(aText), "Detected %d %s:", (int)vNames.size(), pKind);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "mod_detector", aText);
		for(const std::string &Name : vNames)
		{
			str_format(aText, sizeof(aText), "- %s", Name.c_str());
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "mod_detector", aText);
		}
	};
	Log(vMods, m_SeenMods, "mod(s)");
	if(g_Config.m_KrxModDetectorWarn)
		Log(vSuspects, m_SeenSuspects, "sus player(s)");
}

bool CKrxMiscPreTas::ModeratorDetected() const
{
	return std::any_of(m_aMods.begin(), m_aMods.end(), [](bool Value) { return Value; });
}

bool CKrxMiscPreTas::SuspiciousDetected() const
{
	return std::any_of(m_aSuspects.begin(), m_aSuspects.end(), [](bool Value) { return Value; });
}

bool CKrxMiscPreTas::IsModerator(int Id) const
{
	return Id >= 0 && Id < (int)m_aMods.size() && m_aMods[Id];
}

bool CKrxMiscPreTas::IsSuspicious(int Id) const
{
	return Id >= 0 && Id < (int)m_aSuspects.size() && m_aSuspects[Id];
}

void CKrxMiscPreTas::AutoVoteKick()
{
	if(!g_Config.m_KrxAutoVoteKick || GameClient()->m_Voting.SecondsLeft() >= 0)
		return;
	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	int Target = g_Config.m_KrxAutoVoteKickTarget;
	if(g_Config.m_KrxAutoVoteKickMode == 1)
	{
		if(Target == LocalId || Target < 0 || Target >= MAX_CLIENTS || !GameClient()->m_aClients[Target].m_Active)
			return;
	}
	else
	{
		std::vector<int> vIds;
		for(int Id = 0; Id < std::min<int>(MAX_CLIENTS, 64); ++Id)
			if(Id != LocalId && GameClient()->m_aClients[Id].m_Active && GameClient()->m_aClients[Id].m_Team != TEAM_SPECTATORS && GameClient()->m_Teams.SameTeam(Id, LocalId))
				vIds.push_back(Id);
		if(vIds.empty())
			return;
		Target = vIds[ReferenceRand() % vIds.size()];
	}
	char aCommand[128];
	str_format(aCommand, sizeof(aCommand), "callvote kick %d \"%s\"", Target, g_Config.m_KrxAutoVoteKickReason);
	Console()->ExecuteLine(aCommand, IConsole::CLIENT_ID_UNSPECIFIED);
}

void CKrxMiscPreTas::MassMention()
{
	const int Tick = Client()->GameTick(g_Config.m_ClDummy);
	if(!g_Config.m_KrxMassMentionSpam || m_MentionAfterTick >= Tick)
		return;
	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	std::vector<int> vIds;
	for(int Id = 0; Id < std::min<int>(MAX_CLIENTS, 64); ++Id)
		if(Id != LocalId && GameClient()->m_aClients[Id].m_Active)
			vIds.push_back(Id);
	if(vIds.empty())
		return;
	std::mt19937 Shuffle(std::random_device{}());
	ShuffleReferenceIds(vIds, Shuffle);
	const char *const apPrefixes[] = {"bro, check out this client: ", "Stop being a noob! Get good with ", "If I was cheating, I'd use ", "Think you could do better? Not without ", "Escape the Teeworlds matrix with "};
	// Original 2b4880 creates an independent random_device-seeded MT engine.
	std::mt19937 PrefixRandom(std::random_device{}());
	std::string Message = apPrefixes[ReferenceUniform(PrefixRandom, 5)];
	// REA272270: reseed rand(time) for each inserted zero-width character.
	std::string Advert = "krxclient·xyz";
	ReferenceSrand(static_cast<unsigned>(std::time(nullptr)));
	Advert.insert(ReferenceRand() % (Advert.size() - 1) + 1, "\xe2\x80\x8b");
	ReferenceSrand(static_cast<unsigned>(std::time(nullptr)));
	Advert.insert(ReferenceRand() % (Advert.size() - 1) + 1, "\xe2\x80\x8a");
	Message += Advert;
	for(int Id : vIds)
	{
		Message += ' ';
		Message += GameClient()->m_aClients[Id].m_aName;
	}
	GameClient()->m_Chat.SendChat(0, Message.c_str());
	m_MentionAfterTick = Tick + 250;
}

void CKrxMiscPreTas::ChangeNameNearFinish()
{
	if(!g_Config.m_KrxChangeNameOnFinish || !(GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy].m_PlayerFlags & PLAYERFLAG_PLAYING) || Client()->State() != IClient::STATE_ONLINE)
	{
		m_NearFinish = false;
		return;
	}
	std::vector<vec2> vFinish;
	for(int y = 0; y < Collision()->GetHeight() && vFinish.size() < 255; ++y)
		for(int x = 0; x < Collision()->GetWidth() && vFinish.size() < 255; ++x)
		{
			const int Index = Collision()->GetPureMapIndex(x * 32.0f, y * 32.0f);
			if(Collision()->GetTileIndex(Index) == TILE_FINISH || Collision()->GetFrontTileIndex(Index) == TILE_FINISH)
				vFinish.emplace_back(x * 32.0f, y * 32.0f);
		}
	if(vFinish.empty() || vFinish[vFinish.size() / 2] == vec2(0, 0))
	{
		m_NearFinish = false;
		return;
	}
	const std::string Name = g_Config.m_KrxChangeNameOnFinishRandom ? RandomName() : g_Config.m_KrxChangeNameOnFinishName;
	if(distance(GameClient()->m_LocalCharacterPos, vFinish[vFinish.size() / 2]) >= 512.0f)
		m_NearFinish = false;
	else if(!m_NearFinish)
	{
		m_NearFinish = true;
		if(!Name.empty())
		{
			if(g_Config.m_ClDummy)
				str_copy(g_Config.m_ClDummyName, Name.c_str());
			else
				str_copy(g_Config.m_PlayerName, Name.c_str());
			GameClient()->SendInfo(false);
		}
	}
}

void CKrxMiscPreTas::Verify()
{
	if(!g_Config.m_ClAutoVerify || m_Verified)
		return;
	std::shared_ptr<IHttpRequest> pRequest = HttpGet("https://ger10.ddnet.tw/");
	pRequest->Timeout(CTimeout{10000, 0, 8000, 10});
	Http()->Run(pRequest);
	const int64_t Started = time_get_impl();
	pRequest->Wait();
	if(pRequest->State() == EHttpState::DONE)
	{
		dbg_msg("verify", "Verified client! Took %d ms", (int)((time_get_impl() - Started) * 1000 / time_freq()));
		m_Verified = true;
	}
	else
	{
		dbg_msg("verify", "Failed to verify client");
		if(++m_VerifyFailures >= 3)
		{
			dbg_msg("verify", "Failed to verify client 3 times, disabling auto verify");
			m_Verified = true;
		}
	}
}

void CKrxMiscPreTas::AfterComponentsUpdate()
{
	// Original20e750 calls Verify after all component updates, even offline.
	Verify();
}

void CKrxMiscPreTas::VoteBeforeHookSpam()
{
	AutoVoteKick();
}

void CKrxMiscPreTas::BeforeInput()
{
	// Original235e90 has no advertising sender: it simply clears this flag.
	g_Config.m_KrxSendAds = 0;
	ScanModerators();
	MassMention();
}

void CKrxMiscPreTas::BeforeSnapInput()
{
	ChangeNameNearFinish();
}

void CKrxMiscPreTas::Notify(int Type, const char *pTitle, const char *pMessage, int64_t DurationMs)
{
	if(g_Config.m_KrxNotifications)
	{
		const std::lock_guard<std::mutex> Lock(m_NotificationsMutex);
		m_vNotifications.push_back({Type, pTitle, pMessage, time_get_impl(), DurationMs});
	}
}

void CKrxMiscPreTas::OnRender()
{
	if(!g_Config.m_KrxNotifications)
		return;
	const std::lock_guard<std::mutex> Lock(m_NotificationsMutex);
	// 30f680 reserves the kill-message corner while notifications are queued.
	if(m_vNotifications.empty())
	{
		if(m_RestoreKillMessages)
		{
			m_RestoreKillMessages = false;
			g_Config.m_ClShowKillMessages = 1;
		}
		return;
	}
	if(g_Config.m_ClShowKillMessages)
		m_RestoreKillMessages = true;
	g_Config.m_ClShowKillMessages = 0;
	const auto OldScreen = Graphics()->GetScreen();
	const float Width = 300.0f * Graphics()->ScreenAspect();
	Graphics()->MapScreen(CScreenRect(0, 0, Width, 300));
	const int64_t Now = time_get_impl();
	const float X = Width - 75.0f - 2.0f;
	float Y = 0;
	int Displayed = 0;
	std::vector<CNotification> vExpired;
	for(auto It = m_vNotifications.begin(); It != m_vNotifications.end() && Displayed < 3; ++It)
	{
		if((Now - It->m_Created) * 1000 >= It->m_DurationMs * time_freq())
		{
			vExpired.push_back(*It);
			continue;
		}
		const ColorRGBA Colors[] = {ColorRGBA(.498f, .780f, 1, 1), ColorRGBA(1, .757f, .027f, 1), ColorRGBA(1, .14f, .14f, 1)};
		const char *const apIcons[] = {"\xef\x81\x9a", "\xef\x81\xaa", "\xef\x81\x97"};
		const int Type = std::clamp(It->m_Type, 0, 2);
		const float IconWidth = TextRender()->TextWidth(7, apIcons[Type]);
		const float TextWidth = 75 - (TextRender()->TextWidth(4, apIcons[Type]) + 12);
		const float Height = TextRender()->TextBoundingBox(4, It->m_Message.c_str(), -1, TextWidth).m_H + 12;
		if(Y == 0)
			Y = Height;
		CUIRect Panel{X, Y, 75, Height};
		Panel.Draw(ColorRGBA(0, 0, 0, .6f), IGraphics::CORNER_ALL, 5);
		TextRender()->TextOutlineColor(0, 0, 0, .3f);
		TextRender()->TextColor(Colors[Type]);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(TEXT_RENDER_FLAG_NO_X_BEARING | TEXT_RENDER_FLAG_NO_Y_BEARING | TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH);
		TextRender()->Text(X + 4, Y + (Height - 7) / 2 + 1, 7, apIcons[Type]);
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->TextColor(1, 1, 1, 1);
		TextRender()->Text(X + 8 + IconWidth, Y + 2, 6, It->m_Title.c_str());
		CTextCursor Cursor;
		Cursor.SetPosition(vec2(X + 8 + IconWidth, Y + 9));
		Cursor.m_FontSize = 4;
		Cursor.m_Flags = TEXTFLAG_RENDER;
		Cursor.m_LineWidth = TextWidth;
		TextRender()->TextEx(&Cursor, It->m_Message.c_str(), -1);
		Y += Height + 4;
		++Displayed;
	}
	// The source erases every structurally equal expired record, including
	// duplicates beyond the three displayed entries, after the rendering walk.
	m_vNotifications.erase(std::remove_if(m_vNotifications.begin(), m_vNotifications.end(), [&](const CNotification &Entry) {
		return std::any_of(vExpired.begin(), vExpired.end(), [&](const CNotification &Expired) {
			return Entry.m_Type == Expired.m_Type && Entry.m_Title == Expired.m_Title && Entry.m_Message == Expired.m_Message && Entry.m_Created == Expired.m_Created && Entry.m_DurationMs == Expired.m_DurationMs;
		});
	}), m_vNotifications.end());
	TextRender()->TextColor(1, 1, 1, 1);
	TextRender()->TextOutlineColor(0, 0, 0, .3f);
	Graphics()->MapScreen(OldScreen);
}

std::string CKrxMiscPreTas::GenerateReferenceName(std::mt19937 &Random)
{
	// MSVC uniform_int_distribution: multiply-high with rejection. Using the
	// host's uniform_int_distribution changes random draws across C++ libraries.
	const auto Uniform = [&](uint32_t Count) {
		uint64_t Product = uint64_t(Random()) * Count;
		while(uint32_t(Product) < uint32_t(-Count) % Count)
			Product = uint64_t(Random()) * Count;
		return uint32_t(Product >> 32);
	};
	const auto Pick = [&](const auto &Values) -> const auto & { return Values[Uniform(std::size(Values))]; };
	const auto Bounded = [](std::string &Name, const std::string &Candidate) { if(Candidate.size() <= 15) Name=Candidate; };
	std::string Name;
	do
	{
		switch(Uniform(5))
		{
		case 0:
		case 2: Name = Pick(s_apNameNames); break;
		case 1:
			if(Random() & 0x80000000U)
				Name = Pick(s_apNameNames);
			else
			{
				// Original draws the name before the prefix.
				const std::string Base = Pick(s_apNameNames);
				Name = std::string(Pick(s_apNamePrefixes)) + Base;
			}
			break;
		case 3:
		{
			const std::string Base = Pick(s_apNameNames);
			Name = std::string(Pick(s_apNameWords)) + Base;
			break;
		}
		case 4:
			// The original appends to the previous attempt rather than clearing it.
			for(int i = 0; i < 3; ++i)
				Name += Pick(s_apNameNames)[0];
			break;
		}
	} while(Name.size() > 15);
	if(Uniform(2) == 1)
		for(const auto &Pair : s_aNameHomoglyphs)
		{
			const auto Pos = Name.find(Pair.first[0]);
			if(Pos != std::string::npos && (Random() & 0x80000000U))
			{
				std::string Candidate = Name;
				Candidate.replace(Pos, 1, Pair.second);
				Bounded(Name, Candidate);
			}
		}
	if(Random() & 0x80000000U)
		for(const auto &Pair : s_aNameDigits)
		{
			const auto Pos = Name.find(Pair.first[0]);
			if(Pos != std::string::npos && (Random() & 0x80000000U))
			{
				std::string Candidate = Name;
				Candidate.replace(Pos, 1, Pair.second);
				Bounded(Name, Candidate);
			}
		}
	// 2a1000 maps these ASCII letters with an independent 20% draw per letter.
	const std::string Letters = "A4B8E3G6I1L1O0S5T7Z2";
	for(char &Byte : Name)
	{
		const unsigned char U = static_cast<unsigned char>(Byte);
		const char Upper = U >= 'a' && U <= 'z' ? char(U - 'a' + 'A') : char(U);
		for(size_t i = 0; i < Letters.size(); i += 2)
			if(Upper == Letters[i])
			{
				if(Uniform(100) < 20)
					Byte = Letters[i + 1];
				break;
			}
	}
	if(Name.size() < 7)
	{
		if(Random() & 0x80000000U)
			Bounded(Name, Name + Pick(s_apNameSuffixes));
		if(Name.size() < 7 && Uniform(5) == 0)
			Bounded(Name, Name + std::to_string(Uniform(100)));
	}
	if(Name.size() < 7 && Name.find(' ') == std::string::npos && Uniform(3) == 0)
	{
		const auto &Pair = Pick(s_aNameBrackets);
		Bounded(Name, std::string(Pair.first) + Name + Pair.second);
		return Name;
	}
	if(Random() <= 0x3fffffffU)
		Bounded(Name, Name + Pick(s_apNameSymbols));
	return Name;
}

std::string CKrxMiscPreTas::RandomName()
{
	// 2a2e30 initializes its persistent engine on the first generator call.
	if(!m_NameRandomInitialized)
	{
		m_NameRandom.seed(std::random_device{}());
		m_NameRandomInitialized = true;
	}
	return GenerateReferenceName(m_NameRandom);
}
