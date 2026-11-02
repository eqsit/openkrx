#include "replay_vault.h"

#include <zlib.h>

#include <cstdint>
#include <cstring>
#include <set>

namespace OpenKrx
{
	bool DecodeReplayVault(const unsigned char *pData, size_t Size, std::vector<CVaultFile> &vFiles, std::string &Error)
	{
		const auto Fail = [&Error](const char *pReason) { Error = pReason; return false; };
		if(Size < 22 || Size > 256 * 1024 * 1024)
			return Fail("Invalid replay vault ZIP size.");
		const auto U16 = [pData](size_t Offset) { return (uint16_t)((uint16_t)pData[Offset] | (uint16_t)pData[Offset + 1] << 8); };
		const auto U32 = [pData](size_t Offset) { return (uint32_t)pData[Offset] | (uint32_t)pData[Offset + 1] << 8 | (uint32_t)pData[Offset + 2] << 16 | (uint32_t)pData[Offset + 3] << 24; };
		size_t End = Size - 22;
		const size_t MinEnd = Size > 65557 ? Size - 65557 : 0;
		while(U32(End) != 0x06054b50 || End + 22 + U16(End + 20) != Size)
		{
			if(End == MinEnd)
				return Fail("Replay vault ZIP directory is missing.");
			--End;
		}
		if(U16(End + 4) || U16(End + 6) || U16(End + 8) != U16(End + 10))
			return Fail("Unsupported split replay vault ZIP.");
		const uint16_t Count = U16(End + 10);
		size_t Offset = U32(End + 16);
		const size_t DirectoryEnd = Offset + U32(End + 12);
		if(DirectoryEnd > End || Offset > DirectoryEnd)
			return Fail("Invalid replay vault ZIP directory bounds.");
		std::vector<CVaultFile> vDecoded;
		std::set<std::string> Names;
		size_t Total = 0;
		for(unsigned I = 0; I < Count; ++I)
		{
			if(Offset > DirectoryEnd || DirectoryEnd - Offset < 46 || U32(Offset) != 0x02014b50)
				return Fail("Invalid replay vault ZIP entry.");
			const uint16_t Flags = U16(Offset + 8), Method = U16(Offset + 10);
			const uint32_t Crc = U32(Offset + 16), Compressed = U32(Offset + 20), Length = U32(Offset + 24);
			const size_t NameLength = U16(Offset + 28), ExtraLength = U16(Offset + 30), CommentLength = U16(Offset + 32);
			const size_t Local = U32(Offset + 42);
			const size_t Next = Offset + 46 + NameLength + ExtraLength + CommentLength;
			if(Next > DirectoryEnd || Next < Offset)
				return Fail("Invalid replay vault ZIP filename bounds.");
			std::string Name((const char *)pData + Offset + 46, NameLength);
			Offset = Next;
			if(Name.find('\0') != std::string::npos || Name.find('\\') != std::string::npos || Name.find(':') != std::string::npos || Name.find("..") != std::string::npos)
				return Fail("Invalid replay vault ZIP path.");
			const size_t Prefix = Name.find("/replays/");
			if(Prefix == std::string::npos || Name.size() < 4 || Name.substr(Name.size() - 4) != ".tas")
				continue;
			Name = Name.substr(Prefix + 9);
			if(Name.empty() || Name[0] == '/' || !Names.insert(Name).second || (Flags & 1) || (Method != 0 && Method != 8))
				return Fail("Unsupported replay vault ZIP entry.");
			if(Length > 128 * 1024 * 1024 || Compressed > 128 * 1024 * 1024 || Total + Length > 512 * 1024 * 1024)
				return Fail("Replay vault ZIP exceeds extraction limits.");
			if(Local > Size || Size - Local < 30 || U32(Local) != 0x04034b50)
				return Fail("Invalid replay vault ZIP local header.");
			const size_t Start = Local + 30 + U16(Local + 26) + U16(Local + 28);
			if(Start > Size || Compressed > Size - Start)
				return Fail("Invalid replay vault ZIP data bounds.");
			CVaultFile File{Name, std::vector<unsigned char>(Length)};
			if(Method == 0)
			{
				if(Compressed != Length)
					return Fail("Invalid stored replay vault entry length.");
				std::memcpy(File.m_vData.data(), pData + Start, Length);
			}
			else
			{
				z_stream Stream{};
				Stream.next_in = const_cast<Bytef *>(pData + Start);
				Stream.avail_in = Compressed;
				Stream.next_out = File.m_vData.data();
				Stream.avail_out = Length;
				if(inflateInit2(&Stream, -MAX_WBITS) != Z_OK)
					return Fail("Couldn't initialize replay vault decompressor.");
				const int Result = inflate(&Stream, Z_FINISH);
				const bool Valid = Result == Z_STREAM_END && Stream.total_in == Compressed && Stream.total_out == Length;
				inflateEnd(&Stream);
				if(!Valid)
					return Fail("Couldn't decompress replay vault entry.");
			}
			if(crc32(0, File.m_vData.data(), File.m_vData.size()) != Crc)
				return Fail("Replay vault entry checksum mismatch.");
			Total += Length;
			vDecoded.push_back(std::move(File));
		}
		if(vDecoded.empty())
			return Fail("Replay vault ZIP has no TAS replays.");
		vFiles = std::move(vDecoded);
		Error.clear();
		return true;
	}
}
