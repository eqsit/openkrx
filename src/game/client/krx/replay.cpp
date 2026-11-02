#include "replay.h"

#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>

#include <zlib.h>

#include <climits>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>

namespace OpenKrx
{
	namespace
	{
		constexpr size_t MAX_JSON_BYTES = 128 * 1024 * 1024;
		constexpr size_t MAX_FRAMES = 1000000;
		const char *const INPUT_KEYS[] = {"m_Direction", "m_TargetX", "m_TargetY", "m_Jump", "m_Fire", "m_Hook", "m_PlayerFlags", "m_WantedWeapon", "m_NextWeapon", "m_PrevWeapon"};
		std::array<int, 10> InputValues(const CNetObj_PlayerInput &I)
		{
			return {I.m_Direction, I.m_TargetX, I.m_TargetY, I.m_Jump, I.m_Fire, I.m_Hook, I.m_PlayerFlags, I.m_WantedWeapon, I.m_NextWeapon, I.m_PrevWeapon};
		}
		CNetObj_PlayerInput MakeInput(const std::array<int, 10> &V)
		{
			return {V[0], V[1], V[2], V[3], V[4], V[5], V[6], V[7], V[8], V[9]};
		}
		std::string Quote(const std::string &S)
		{
			CJsonStringWriter Writer;
			Writer.WriteStrValue(S.c_str());
			return Writer.GetOutputString();
		}
		bool ReadNumber(const json_value &Value, float &Out)
		{
			if(Value.type != json_integer && Value.type != json_double)
				return false;
			const double D = Value.type == json_integer ? (double)Value.u.integer : Value.u.dbl;
			Out = (float)D;
			return std::isfinite(Out);
		}
	}

	bool EncodeReplay(const CReplay &Replay, std::vector<unsigned char> &vBytes, std::string &Error)
	{
		Error.clear();
		if(Replay.m_aFrames[0].empty() || Replay.m_aFrames[0].size() > MAX_FRAMES || Replay.m_aFrames[1].size() > MAX_FRAMES)
		{
			Error = "TAS: Can't save an empty or oversized replay!";
			return false;
		}
		std::ostringstream Json;
		Json.imbue(std::locale::classic());
		Json << std::setprecision(std::numeric_limits<float>::max_digits10);
		Json << "{\"info\":{\"author\":" << Quote(Replay.m_Author) << ",\"description\":" << Quote(Replay.m_Description)
		     << ",\"date_modified\":" << Quote(Replay.m_Date) << ",\"map\":" << Quote(Replay.m_Map) << "}";
		for(int Slot = 0; Slot < 2; ++Slot)
		{
			if(Slot && Replay.m_aFrames[Slot].empty())
				continue;
			Json << ",\"" << (Slot ? "replay_dummy" : "replay_local") << "\":[";
			bool First = true;
			for(const auto &Frame : Replay.m_aFrames[Slot])
			{
				if(!std::isfinite(Frame.m_Position.x) || !std::isfinite(Frame.m_Position.y))
				{
					Error = "TAS: Invalid replay position.";
					return false;
				}
				if(!First)
					Json << ',';
				First = false;
				Json << "{\"input\":{";
				const auto Values = InputValues(Frame.m_Input);
				for(size_t I = 0; I < Values.size(); ++I)
					Json << (I ? "," : "") << '"' << INPUT_KEYS[I] << "\":" << Values[I];
				Json << "},\"pos\":{\"x\":" << Frame.m_Position.x << ",\"y\":" << Frame.m_Position.y << "}}";
			}
			Json << ']';
		}
		Json << '}';
		const std::string Text = Json.str();
		if(Text.size() > MAX_JSON_BYTES)
		{
			Error = "TAS: Replay JSON is too large.";
			return false;
		}
		uLongf Size = compressBound(Text.size());
		std::vector<unsigned char> vEncoded(Size + 4);
		const uint32_t Length = (uint32_t)Text.size();
		for(int I = 0; I < 4; ++I)
			vEncoded[I] = (unsigned char)(Length >> (8 * I));
		if(compress2(vEncoded.data() + 4, &Size, (const Bytef *)Text.data(), Text.size(), 9) != Z_OK)
		{
			Error = "TAS: Couldn't compress replay JSON.";
			return false;
		}
		vEncoded.resize(Size + 4);
		vBytes = std::move(vEncoded);
		return true;
	}

	bool DecodeReplay(const std::vector<unsigned char> &vBytes, CReplay &Replay, std::string &Error)
	{
		Error.clear();
		const auto Fail = [&Error](const char *pMessage) { Error = pMessage; return false; };
		if(vBytes.size() < 5 || vBytes.size() > MAX_JSON_BYTES)
			return Fail("TAS: Replay file is corrupted or too small.");
		uint32_t Length = 0;
		for(int I = 0; I < 4; ++I)
			Length |= (uint32_t)vBytes[I] << (8 * I);
		if(!Length || Length > MAX_JSON_BYTES)
			return Fail("TAS: Invalid replay JSON size.");
		std::string Text(Length, '\0');
		uLongf Size = Length;
		if(uncompress((Bytef *)Text.data(), &Size, vBytes.data() + 4, vBytes.size() - 4) != Z_OK || Size != Length)
			return Fail("TAS: Couldn't decompress replay JSON.");
		return DecodeReplayJson(Text, Replay, Error);
	}

	bool DecodeReplayJson(std::string_view Text, CReplay &Replay, std::string &Error)
	{
		Error.clear();
		const auto Fail = [&Error](const char *pMessage) { Error = pMessage; return false; };
		if(Text.empty() || Text.size() > MAX_JSON_BYTES)
			return Fail("TAS: Invalid replay JSON size.");
		std::unique_ptr<json_value, decltype(&json_value_free)> pRoot(JsonParse(Text.data(), Text.size()), json_value_free);
		if(!pRoot || pRoot->type != json_object)
			return Fail("TAS: Invalid replay JSON.");
		CReplay Loaded;
		const auto &Info = (*pRoot)["info"];
		Loaded.m_Author = Info["author"].type == json_string ? (const char *)Info["author"] : "unknown";
		Loaded.m_Description = (const char *)Info["description"];
		Loaded.m_Date = Info["date_modified"].type == json_string ? (const char *)Info["date_modified"] : "unknown";
		Loaded.m_Map = Info["map"].type == json_string ? (const char *)Info["map"] : "unknown";
		for(int Slot = 0; Slot < 2; ++Slot)
		{
			const auto &Frames = (*pRoot)[Slot ? "replay_dummy" : "replay_local"];
			if(Slot && Frames.type == json_none)
				continue;
			if(Frames.type != json_array || Frames.u.array.length > MAX_FRAMES)
				return Fail("TAS: Invalid replay frame array.");
			auto &vFrames = Loaded.m_aFrames[Slot];
			vFrames.reserve(Frames.u.array.length);
			for(unsigned I = 0; I < Frames.u.array.length; ++I)
			{
				const auto &Frame = Frames[I];
				const auto &Input = Frame["input"];
				if(Frame.type != json_object || Input.type != json_object)
					return Fail("TAS: Invalid replay input.");
				std::array<int, 10> Values{};
				for(size_t J = 0; J < Values.size(); ++J)
				{
					const auto &V = Input[INPUT_KEYS[J]];
					if(V.type != json_integer || V.u.integer < INT_MIN || V.u.integer > INT_MAX)
						return Fail("TAS: Invalid replay input field.");
					Values[J] = (int)V.u.integer;
				}
				vec2 Position;
				if(!ReadNumber(Frame["pos"]["x"], Position.x) || !ReadNumber(Frame["pos"]["y"], Position.y))
					return Fail("TAS: Invalid replay position.");
				vFrames.push_back({MakeInput(Values), Position});
			}
		}
		if(Loaded.m_aFrames[0].empty())
			return Fail("TAS: Empty replay.");
		Replay = std::move(Loaded);
		return true;
	}

	uint32_t PackFentInput(const CNetObj_PlayerInput &Input)
	{
		return (Input.m_Direction < 0 ? 1u : Input.m_Direction > 0 ? 2u :
									     0u) |
		       (Input.m_Jump ? 4u : 0u) | (Input.m_Fire ? 8u : 0u) | (Input.m_Hook ? 16u : 0u) |
		       ((uint32_t)Input.m_WantedWeapon & 7u) << 5 |
		       ((uint32_t)Input.m_TargetX & 0xfffu) << 8 | ((uint32_t)Input.m_TargetY & 0xfffu) << 20;
	}

	CNetObj_PlayerInput UnpackFentInput(uint32_t Packed)
	{
		CNetObj_PlayerInput Input{};
		Input.m_Direction = Packed & 1 ? -1 : (Packed >> 1 & 1);
		Input.m_Jump = Packed >> 2 & 1;
		Input.m_Fire = Packed >> 3 & 1;
		Input.m_Hook = Packed >> 4 & 1;
		Input.m_WantedWeapon = Packed >> 5 & 7;
		const auto Signed12 = [](uint32_t V) { return (int)(V & 0xfff) - (V & 0x800 ? 4096 : 0); };
		Input.m_TargetX = Signed12(Packed >> 8);
		Input.m_TargetY = Signed12(Packed >> 20);
		return Input;
	}
}
