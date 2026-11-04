#ifndef GAME_CLIENT_KRX_NOW_PLAYING_H
#define GAME_CLIENT_KRX_NOW_PLAYING_H

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace OpenKrx
{
	// Linux adaptation of KRX's Windows system-media provider. Only local session
	// bus metadata is read; no shell commands, URLs or player controls are executed.
	class CNowPlaying
	{
	public:
		struct CTrack
		{
			std::string m_Artist;
			std::string m_Title;
			std::string m_ArtUrl;
			std::shared_ptr<const std::vector<uint8_t>> m_pArt;
			bool operator==(const CTrack &Other) const { return m_Artist == Other.m_Artist && m_Title == Other.m_Title; }
			std::string Text() const { return m_Artist.empty() || m_Title.empty() ? std::string{} : m_Artist + " - " + m_Title; }
		};
		~CNowPlaying();
		CTrack Track(bool Enabled);
		void Stop();
		static CTrack Poll();

	private:
		std::mutex m_Mutex;
		std::condition_variable m_Wakeup;
		std::thread m_Worker;
		bool m_Stop = false;
		bool m_Enabled = false;
		CTrack m_Track;
	};
}
#endif
