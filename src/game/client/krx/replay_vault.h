#ifndef GAME_CLIENT_KRX_REPLAY_VAULT_H
#define GAME_CLIENT_KRX_REPLAY_VAULT_H

#include <string>
#include <vector>

namespace OpenKrx
{
	struct CVaultFile
	{
		std::string m_Name;
		std::vector<unsigned char> m_vData;
	};
	bool DecodeReplayVault(const unsigned char *pData, size_t Size, std::vector<CVaultFile> &vFiles, std::string &Error);
}
#endif
