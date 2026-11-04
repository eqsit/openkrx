#include "now_playing.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#if defined(CONF_KRX_MPRIS)
#include <dbus/dbus.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace OpenKrx
{
	CNowPlaying::~CNowPlaying() { Stop(); }

	void CNowPlaying::Stop()
	{
		{
			std::lock_guard Lock(m_Mutex);
			m_Stop = true;
		}
		m_Wakeup.notify_all();
		if(m_Worker.joinable())
			m_Worker.join();
		std::lock_guard StartLock(m_Mutex);
		m_Track = {};
	}

	CNowPlaying::CTrack CNowPlaying::Track(bool Enabled)
	{
		std::lock_guard StartLock(m_Mutex);
		m_Enabled = Enabled;
		if(!Enabled)
			m_Track = {};
		if(Enabled && !m_Worker.joinable())
		{
			m_Stop = false;
			m_Worker = std::thread([this] {
				for(;;)
				{
					{
						std::unique_lock Lock(m_Mutex);
						m_Wakeup.wait(Lock, [this] { return m_Stop || m_Enabled; });
						if(m_Stop)
							return;
					}
					auto Track = Poll();
					std::unique_lock Lock(m_Mutex);
					if(m_Stop)
						return;
					if(m_Enabled)
						m_Track = std::move(Track);
					if(m_Wakeup.wait_for(Lock, std::chrono::seconds(1), [this] { return m_Stop; }))
						return;
				}
			});
		}
		m_Wakeup.notify_all();
		return m_Track;
	}

	CNowPlaying::CTrack CNowPlaying::Poll()
	{
#if defined(CONF_KRX_MPRIS)
		if(!std::getenv("DBUS_SESSION_BUS_ADDRESS"))
			return {};
		// A private connection belongs exclusively to this worker. Fixed call
		// deadlines bound shutdown; a session player cannot stall the render thread.
		static const bool ThreadsReady = dbus_threads_init_default();
		if(!ThreadsReady)
			return {};
		DBusError Error;
		dbus_error_init(&Error);
		DBusConnection *pBus = dbus_bus_get_private(DBUS_BUS_SESSION, &Error);
		if(dbus_error_is_set(&Error))
			dbus_error_free(&Error);
		if(!pBus)
			return {};
		dbus_connection_set_exit_on_disconnect(pBus, false);
		struct CClose
		{
			void operator()(DBusConnection *p) const
			{
				dbus_connection_close(p);
				dbus_connection_unref(p);
			}
		};
		std::unique_ptr<DBusConnection, CClose> Bus(pBus);
		using CMessage = std::unique_ptr<DBusMessage, decltype(&dbus_message_unref)>;
		const auto Call = [&](const char *pDestination, const char *pPath, const char *pInterface, const char *pMethod, const char *pArg = nullptr) {
			CMessage Request(dbus_message_new_method_call(pDestination, pPath, pInterface, pMethod), dbus_message_unref);
			if(!Request || (pArg && !dbus_message_append_args(Request.get(), DBUS_TYPE_STRING, &pArg, DBUS_TYPE_INVALID)))
				return CMessage(nullptr, dbus_message_unref);
			dbus_error_init(&Error);
			CMessage Reply(dbus_connection_send_with_reply_and_block(pBus, Request.get(), 250, &Error), dbus_message_unref);
			if(dbus_error_is_set(&Error))
				dbus_error_free(&Error);
			return Reply;
		};
		auto Names = Call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, "ListNames");
		DBusMessageIter NamesIter, Array;
		if(!Names || !dbus_message_iter_init(Names.get(), &NamesIter) || dbus_message_iter_get_arg_type(&NamesIter) != DBUS_TYPE_ARRAY)
			return {};
		dbus_message_iter_recurse(&NamesIter, &Array);
		std::vector<std::string> Players;
		while(dbus_message_iter_get_arg_type(&Array) == DBUS_TYPE_STRING)
		{
			const char *pName = nullptr;
			dbus_message_iter_get_basic(&Array, &pName);
			if(pName && std::strncmp(pName, "org.mpris.MediaPlayer2.", 22) == 0 && Players.size() < 16)
				Players.emplace_back(pName);
			dbus_message_iter_next(&Array);
		}
		CTrack Fallback;
		const auto LoadArtwork = [](CTrack &Track) {
			// Linux MPRIS exposes a URL rather than Windows thumbnail bytes. Read
			// local regular PNG files only; never fetch remote art URLs here.
			if(Track.m_ArtUrl.compare(0, 7, "file://") != 0)
				return;
			std::string Path;
			for(size_t I = 7; I < Track.m_ArtUrl.size(); ++I)
			{
				unsigned char C = Track.m_ArtUrl[I];
				if(C == '%' && I + 2 < Track.m_ArtUrl.size())
				{
					const auto Hex = [](char V) { return V >= '0' && V <= '9' ? V - '0' : V >= 'a' && V <= 'f' ? V - 'a' + 10 :
												      V >= 'A' && V <= 'F'         ? V - 'A' + 10 :
																     -1; };
					const int High = Hex(Track.m_ArtUrl[I + 1]), Low = Hex(Track.m_ArtUrl[I + 2]);
					if(High < 0 || Low < 0)
						return;
					C = (High << 4) | Low;
					I += 2;
				}
				if(C == 0)
					return;
				Path.push_back(C);
			}
			if(Path.empty() || Path[0] != '/')
				return;
			const int Fd = open(Path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
			if(Fd < 0)
				return;
			struct stat Info;
			if(fstat(Fd, &Info) != 0 || !S_ISREG(Info.st_mode) || Info.st_size < 24 || Info.st_size > 2 * 1024 * 1024)
			{
				close(Fd);
				return;
			}
			auto Art = std::make_shared<std::vector<uint8_t>>(Info.st_size);
			size_t Read = 0;
			while(Read < Art->size())
			{
				const ssize_t Got = read(Fd, Art->data() + Read, Art->size() - Read);
				if(Got <= 0)
					break;
				Read += Got;
			}
			close(Fd);
			const auto Big = [&](size_t Offset) { return (uint32_t((*Art)[Offset]) << 24) | (uint32_t((*Art)[Offset + 1]) << 16) | (uint32_t((*Art)[Offset + 2]) << 8) | (*Art)[Offset + 3]; };
			if(Read == Art->size() && std::memcmp(Art->data(), "\x89PNG\r\n\x1a\n", 8) == 0 && Big(16) <= 2048 && Big(20) <= 2048)
				Track.m_pArt = std::move(Art);
		};
		for(const auto &Player : Players)
		{
			auto Properties = Call(Player.c_str(), "/org/mpris/MediaPlayer2", DBUS_INTERFACE_PROPERTIES, "GetAll", "org.mpris.MediaPlayer2.Player");
			DBusMessageIter Iter, Entries;
			if(!Properties || !dbus_message_iter_init(Properties.get(), &Iter) || dbus_message_iter_get_arg_type(&Iter) != DBUS_TYPE_ARRAY)
				continue;
			dbus_message_iter_recurse(&Iter, &Entries);
			CTrack Track;
			bool Playing = false;
			while(dbus_message_iter_get_arg_type(&Entries) == DBUS_TYPE_DICT_ENTRY)
			{
				DBusMessageIter Entry, Value;
				dbus_message_iter_recurse(&Entries, &Entry);
				if(dbus_message_iter_get_arg_type(&Entry) != DBUS_TYPE_STRING)
					break;
				const char *pKey = nullptr;
				dbus_message_iter_get_basic(&Entry, &pKey);
				if(!dbus_message_iter_next(&Entry) || dbus_message_iter_get_arg_type(&Entry) != DBUS_TYPE_VARIANT)
					break;
				dbus_message_iter_recurse(&Entry, &Value);
				if(std::strcmp(pKey, "PlaybackStatus") == 0 && dbus_message_iter_get_arg_type(&Value) == DBUS_TYPE_STRING)
				{
					const char *pState = nullptr;
					dbus_message_iter_get_basic(&Value, &pState);
					Playing = pState && std::strcmp(pState, "Playing") == 0;
				}
				else if(std::strcmp(pKey, "Metadata") == 0 && dbus_message_iter_get_arg_type(&Value) == DBUS_TYPE_ARRAY)
				{
					DBusMessageIter Metadata;
					dbus_message_iter_recurse(&Value, &Metadata);
					while(dbus_message_iter_get_arg_type(&Metadata) == DBUS_TYPE_DICT_ENTRY)
					{
						DBusMessageIter Field, Data;
						dbus_message_iter_recurse(&Metadata, &Field);
						if(dbus_message_iter_get_arg_type(&Field) != DBUS_TYPE_STRING)
							break;
						const char *pField = nullptr;
						dbus_message_iter_get_basic(&Field, &pField);
						if(!dbus_message_iter_next(&Field) || dbus_message_iter_get_arg_type(&Field) != DBUS_TYPE_VARIANT)
							break;
						dbus_message_iter_recurse(&Field, &Data);
						if(std::strcmp(pField, "xesam:artist") == 0 && dbus_message_iter_get_arg_type(&Data) == DBUS_TYPE_ARRAY)
						{
							DBusMessageIter Artist;
							dbus_message_iter_recurse(&Data, &Artist);
							Data = Artist;
						}
						if(dbus_message_iter_get_arg_type(&Data) == DBUS_TYPE_STRING)
						{
							const char *pText = nullptr;
							dbus_message_iter_get_basic(&Data, &pText);
							if(pText && std::strlen(pText) <= 4096)
							{
								if(std::strcmp(pField, "xesam:title") == 0)
									Track.m_Title = pText;
								else if(std::strcmp(pField, "xesam:artist") == 0)
									Track.m_Artist = pText;
								else if(std::strcmp(pField, "mpris:artUrl") == 0)
									Track.m_ArtUrl = pText;
							}
						}
						dbus_message_iter_next(&Metadata);
					}
				}
				dbus_message_iter_next(&Entries);
			}
			if(!Track.Text().empty())
			{
				if(Playing)
				{
					LoadArtwork(Track);
					return Track;
				}
				if(Fallback.Text().empty())
					Fallback = std::move(Track);
			}
		}
		LoadArtwork(Fallback);
		return Fallback;
#else
		return {};
#endif
	}
}
