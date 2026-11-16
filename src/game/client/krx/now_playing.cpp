#include "now_playing.h"

#include <engine/http.h>

#if defined(CONF_KRX_ARTWORK)
#include <png.h>
#include <turbojpeg.h>
#endif

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
#if defined(CONF_FAMILY_WINDOWS)
	CNowPlaying::CTrack PollWindowsMedia();
#endif
	CNowPlaying::~CNowPlaying() { Stop(); }

	void CNowPlaying::Stop()
	{
		{
			std::lock_guard Lock(m_Mutex);
			m_Stop = true;
			if(m_pArtworkRequest)
				m_pArtworkRequest->Abort();
		}
		m_Wakeup.notify_all();
		if(m_Worker.joinable())
			m_Worker.join();
		std::lock_guard StartLock(m_Mutex);
		m_Track = {};
	}

	CNowPlaying::CTrack CNowPlaying::Track(bool Enabled, IHttp *pHttp)
	{
		std::lock_guard StartLock(m_Mutex);
		m_Enabled = Enabled;
		m_pHttp = pHttp;
		if(!Enabled && m_pArtworkRequest)
			m_pArtworkRequest->Abort();
		if(!Enabled)
			m_Track = {};
		if(Enabled && !m_Worker.joinable())
		{
			m_Stop = false;
			m_Worker = std::thread([this] {
				std::string CachedUrl;
				std::shared_ptr<const std::vector<uint8_t>> CachedArt;
				auto RetryAt = std::chrono::steady_clock::time_point{};
				for(;;)
				{
					{
						std::unique_lock Lock(m_Mutex);
						m_Wakeup.wait(Lock, [this] { return m_Stop || m_Enabled; });
						if(m_Stop)
							return;
					}
					auto Track = Poll();
					if(!Track.m_pArt && (Track.m_ArtUrl.rfind("https://", 0) == 0 || Track.m_ArtUrl.rfind("http://", 0) == 0))
					{
						const auto Now = std::chrono::steady_clock::now();
						if(Track.m_ArtUrl != CachedUrl || Now >= RetryAt)
						{
							CachedUrl = Track.m_ArtUrl;
							CachedArt.reset();
							RetryAt = Now + std::chrono::seconds(30);
							std::shared_ptr<IHttpRequest> Request;
							{
								std::lock_guard Lock(m_Mutex);
								if(m_Enabled && !m_Stop && m_pHttp)
								{
									Request = HttpGet(CachedUrl.c_str());
									Request->Timeout({1000, 2000, 1024, 1});
									Request->MaxResponseSize(2 * 1024 * 1024);
									Request->LogProgress(HTTPLOG::NONE);
									m_pArtworkRequest = Request;
									m_pHttp->Run(Request);
								}
							}
							if(Request)
							{
								Request->Wait();
								if(Request->State() == EHttpState::DONE)
								{
									unsigned char *pBytes = nullptr;
									size_t Size = 0;
									Request->Result(&pBytes, &Size);
									if(pBytes && Size <= 2 * 1024 * 1024)
										CachedArt = DecodeArtwork(std::vector<uint8_t>(pBytes, pBytes + Size));
									if(CachedArt)
										RetryAt = Now + std::chrono::minutes(5);
								}
								std::lock_guard Lock(m_Mutex);
								m_pArtworkRequest.reset();
							}
						}
						Track.m_pArt = CachedArt;
					}
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

	std::shared_ptr<const std::vector<uint8_t>> CNowPlaying::DecodeArtwork(const std::vector<uint8_t> &Bytes)
	{
		if(Bytes.size() < 24 || Bytes.size() > 2 * 1024 * 1024)
			return {};
		const auto Big = [&](size_t Offset) { return (uint32_t(Bytes[Offset]) << 24) | (uint32_t(Bytes[Offset + 1]) << 16) | (uint32_t(Bytes[Offset + 2]) << 8) | Bytes[Offset + 3]; };
		if(std::memcmp(Bytes.data(), "\x89PNG\r\n\x1a\n", 8) == 0)
		{
			if(Big(16) == 0 || Big(20) == 0 || Big(16) > 2048 || Big(20) > 2048)
				return {};
			return std::make_shared<const std::vector<uint8_t>>(Bytes);
		}
#if defined(CONF_KRX_ARTWORK)
		if(Bytes[0] != 0xff || Bytes[1] != 0xd8)
			return {};
		std::unique_ptr<void, decltype(&tjDestroy)> Decoder(tjInitDecompress(), tjDestroy);
		int Width, Height, Subsampling, ColorSpace;
		if(!Decoder || tjDecompressHeader3(Decoder.get(), Bytes.data(), Bytes.size(), &Width, &Height, &Subsampling, &ColorSpace) != 0 ||
			Width < 1 || Height < 1 || Width > 2048 || Height > 2048)
			return {};
		std::vector<uint8_t> Pixels(size_t(Width) * Height * 4);
		if(tjDecompress2(Decoder.get(), Bytes.data(), Bytes.size(), Pixels.data(), Width, 0, Height, TJPF_RGBA, TJFLAG_STOPONWARNING) != 0)
			return {};
		png_image Image{};
		Image.version = PNG_IMAGE_VERSION;
		Image.width = Width;
		Image.height = Height;
		Image.format = PNG_FORMAT_RGBA;
		png_alloc_size_t Size = 0;
		if(!png_image_write_to_memory(&Image, nullptr, &Size, 0, Pixels.data(), 0, nullptr))
			return {};
		auto Result = std::make_shared<std::vector<uint8_t>>(Size);
		if(!png_image_write_to_memory(&Image, Result->data(), &Size, 0, Pixels.data(), 0, nullptr))
			return {};
		Result->resize(Size);
		return Result;
#else
		return {};
#endif
	}

	CNowPlaying::CTrack CNowPlaying::Poll()
	{
#if defined(CONF_FAMILY_WINDOWS)
		return PollWindowsMedia();
#elif defined(CONF_KRX_MPRIS)
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
		const auto LoadArtwork = [](CTrack &Track) {
			// Local files are read without blocking on device/FIFO paths. Remote
			// artwork is handled by the worker through the shared HTTP service.
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
			if(Read == Art->size())
				Track.m_pArt = DecodeArtwork(*Art);
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
			if(Playing && !Track.Text().empty())
			{
				LoadArtwork(Track);
				return Track;
			}
		}
		return {};
#else
		return {};
#endif
	}
}
