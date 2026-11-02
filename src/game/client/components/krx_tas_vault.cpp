#include "krx_tas.h"

#include <base/io.h>

#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>
#include <game/client/krx/replay_vault.h>

#include <chrono>
#include <cstdlib>

namespace OpenKrx
{
	void CKrxTas::SyncVault()
	{
		if(m_VaultStage != 0 || (m_pVaultRequest && !m_pVaultRequest->Done()))
			return;
		m_pVaultRequest = HttpGet("https://api.github.com/repos/krxclient/krx-replays/commits/main");
		m_pVaultRequest->HeaderString("Accept", "application/vnd.github+json");
		m_pVaultRequest->MaxResponseSize(4 * 1024 * 1024);
		m_pVaultRequest->Timeout({10000, 30000, 100, 10});
		Http()->Run(m_pVaultRequest);
		m_VaultStage = 1;
		m_VaultAutoChecked = true;
		Message("ReplayVault: Checking for updates...");
	}

	void CKrxTas::CheckAutoSyncVault()
	{
		// Original Auto Sync is triggered by the TAS page, once per client run.
		if(!m_VaultAutoChecked && g_Config.m_KrxTasReplayVaultAutoSync)
			SyncVault();
	}

	void CKrxTas::UpdateVault()
	{
		if(m_VaultStage == 3)
		{
			if(m_VaultFuture.valid())
			{
				if(m_VaultFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
					return;
				auto Result = m_VaultFuture.get();
				if(!Result.m_Error.empty())
				{
					Message(Result.m_Error.c_str());
					m_VaultStage = 0;
					return;
				}
				m_vVaultPending = std::move(Result.m_vFiles);
				m_VaultNextFile = 0;
			}
			InstallVaultFiles();
			return;
		}
		if(!m_pVaultRequest || !m_pVaultRequest->Done())
			return;
		if(m_pVaultRequest->State() != EHttpState::DONE)
		{
			Message("ReplayVault: Failed to download replay vault.");
			m_pVaultRequest.reset();
			m_VaultStage = 0;
			return;
		}
		unsigned char *pData = nullptr;
		size_t Size = 0;
		m_pVaultRequest->Result(&pData, &Size);
		if(m_VaultStage == 1)
		{
			std::unique_ptr<json_value, decltype(&json_value_free)> pRoot(JsonParse((const char *)pData, Size), json_value_free);
			if(!pRoot || (*pRoot)["sha"].type != json_string)
			{
				Message("ReplayVault: Invalid commit response.");
				m_pVaultRequest.reset();
				m_VaultStage = 0;
				return;
			}
			m_VaultSha = (const char *)(*pRoot)["sha"];
			char *pPrevious = Storage()->ReadFileStr("replays/.vault-sha", IStorage::TYPE_SAVE);
			const bool Unchanged = pPrevious && m_VaultSha == pPrevious;
			free(pPrevious);
			if(Unchanged)
			{
				Message("ReplayVault: Already synchronized.");
				m_pVaultRequest.reset();
				m_VaultStage = 0;
				return;
			}
			m_pVaultRequest = HttpGet("https://github.com/krxclient/krx-replays/archive/refs/heads/main.zip");
			m_pVaultRequest->MaxResponseSize(256 * 1024 * 1024);
			m_pVaultRequest->Timeout({10000, 180000, 100, 20});
			Http()->Run(m_pVaultRequest);
			m_VaultStage = 2;
			Message("ReplayVault: Downloading replay vault...");
			return;
		}
		std::vector<unsigned char> vArchive(pData, pData + Size);
		m_pVaultRequest.reset();
		m_VaultStage = 3;
		m_VaultFuture = std::async(std::launch::async, [vArchive = std::move(vArchive)] {
			CVaultResult Result;
			if(!DecodeReplayVault(vArchive.data(), vArchive.size(), Result.m_vFiles, Result.m_Error))
				return Result;
			for(const auto &Entry : Result.m_vFiles)
			{
				CReplay Replay;
				if(!DecodeReplay(Entry.m_vData, Replay, Result.m_Error))
				{
					Result.m_vFiles.clear();
					return Result;
				}
			}
			return Result;
		});
	}

	void CKrxTas::StopVaultWorker()
	{
		if(m_VaultFuture.valid())
			m_VaultFuture.get();
		m_vVaultPending.clear();
	}

	void CKrxTas::InstallVaultFiles()
	{
		// Validation is off the render/input thread. Publish at most eight
		// files or two milliseconds per frame to avoid a burst of disk I/O.
		const auto Started = std::chrono::steady_clock::now();
		bool Success = true;
		Storage()->CreateFolder("replays", IStorage::TYPE_SAVE);
		for(int Published = 0; Published < 8 && m_VaultNextFile < m_vVaultPending.size(); ++Published)
		{
			const auto &Entry = m_vVaultPending[m_VaultNextFile];
			const std::string Path = "replays/" + Entry.m_Name;
			for(size_t Slash = Path.find('/'); Slash != std::string::npos; Slash = Path.find('/', Slash + 1))
				Storage()->CreateFolder(Path.substr(0, Slash).c_str(), IStorage::TYPE_SAVE);
			const std::string Temporary = Path + ".tmp";
			IOHANDLE File = Storage()->OpenFile(Temporary.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
			if(!File)
			{
				Success = false;
				break;
			}
			const bool Written = io_write(File, Entry.m_vData.data(), Entry.m_vData.size()) == Entry.m_vData.size();
			const bool Closed = io_close(File) == 0;
			if(!Written || !Closed || !Storage()->RenameFile(Temporary.c_str(), Path.c_str(), IStorage::TYPE_SAVE))
			{
				Storage()->RemoveFile(Temporary.c_str(), IStorage::TYPE_SAVE);
				Success = false;
				break;
			}
			++m_VaultNextFile;
			if(std::chrono::steady_clock::now() - Started >= std::chrono::milliseconds(2))
				break;
		}
		if(Success && m_VaultNextFile < m_vVaultPending.size())
			return;
		if(Success)
		{
			IOHANDLE File = Storage()->OpenFile("replays/.vault-sha.tmp", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			if(File)
			{
				const bool Written = io_write(File, m_VaultSha.data(), m_VaultSha.size()) == m_VaultSha.size();
				const bool Closed = io_close(File) == 0;
				Success = Written && Closed && Storage()->RenameFile("replays/.vault-sha.tmp", "replays/.vault-sha", IStorage::TYPE_SAVE);
			}
			else
				Success = false;
		}
		RefreshFiles();
		Message(Success ? "ReplayVault: Replay vault downloaded successfully." : "ReplayVault: Failed to extract replay vault.");
		m_vVaultPending.clear();
		m_VaultStage = 0;
	}
}
