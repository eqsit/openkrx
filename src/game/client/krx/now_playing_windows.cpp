#include "now_playing.h"

#if defined(CONF_FAMILY_WINDOWS)
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <windows.h>
#include <wincodec.h>

#include <chrono>

namespace OpenKrx
{
namespace
{
template<typename T>
bool Complete(const T &Operation)
{
	if(Operation.wait_for(std::chrono::seconds(2)) == winrt::Windows::Foundation::AsyncStatus::Completed)
		return true;
	Operation.Cancel();
	return false;
}

std::shared_ptr<const std::vector<uint8_t>> Thumbnail(const winrt::Windows::Storage::Streams::IRandomAccessStreamReference &Reference)
{
	using namespace winrt::Windows::Storage::Streams;
	if(!Reference) return {};
	auto Open = Reference.OpenReadAsync();
	if(!Complete(Open)) return {};
	auto Stream = Open.GetResults();
	if(!Stream.Size() || Stream.Size() > 2 * 1024 * 1024) return {};
	DataReader Reader(Stream);
	auto Load = Reader.LoadAsync((uint32_t)Stream.Size());
	if(!Complete(Load)) return {};
	std::vector<uint8_t> Bytes(Load.GetResults());
	Reader.ReadBytes(Bytes);
	Reader.DetachStream();
	Stream.Close();
	if(auto Png = CNowPlaying::DecodeArtwork(Bytes)) return Png;
	winrt::com_ptr<IWICImagingFactory> Factory;
	winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory), Factory.put_void()));
	winrt::com_ptr<IWICStream> Input;
	winrt::check_hresult(Factory->CreateStream(Input.put()));
	winrt::check_hresult(Input->InitializeFromMemory(Bytes.data(), (DWORD)Bytes.size()));
	winrt::com_ptr<IWICBitmapDecoder> Decoder;
	winrt::check_hresult(Factory->CreateDecoderFromStream(Input.get(), nullptr, WICDecodeMetadataCacheOnLoad, Decoder.put()));
	winrt::com_ptr<IWICBitmapFrameDecode> Frame;
	winrt::check_hresult(Decoder->GetFrame(0, Frame.put()));
	UINT Width, Height;
	winrt::check_hresult(Frame->GetSize(&Width, &Height));
	if(!Width || !Height || Width > 2048 || Height > 2048) return {};
	winrt::com_ptr<IStream> Output;
	winrt::check_hresult(CreateStreamOnHGlobal(nullptr, TRUE, Output.put()));
	winrt::com_ptr<IWICBitmapEncoder> Encoder;
	winrt::check_hresult(Factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, Encoder.put()));
	winrt::check_hresult(Encoder->Initialize(Output.get(), WICBitmapEncoderNoCache));
	winrt::com_ptr<IWICBitmapFrameEncode> Encoded;
	winrt::check_hresult(Encoder->CreateNewFrame(Encoded.put(), nullptr));
	winrt::check_hresult(Encoded->Initialize(nullptr));
	winrt::check_hresult(Encoded->WriteSource(Frame.get(), nullptr));
	winrt::check_hresult(Encoded->Commit());
	winrt::check_hresult(Encoder->Commit());
	STATSTG Stat{};
	winrt::check_hresult(Output->Stat(&Stat, STATFLAG_NONAME));
	if(Stat.cbSize.QuadPart > 16 * 1024 * 1024) return {};
	LARGE_INTEGER Start{};
	winrt::check_hresult(Output->Seek(Start, STREAM_SEEK_SET, nullptr));
	auto Result = std::make_shared<std::vector<uint8_t>>((size_t)Stat.cbSize.QuadPart);
	ULONG Read = 0;
	winrt::check_hresult(Output->Read(Result->data(), (ULONG)Result->size(), &Read));
	return Read == Result->size() ? Result : nullptr;
}
}

CNowPlaying::CTrack PollWindowsMedia()
{
	struct CApartment
	{
		CApartment() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
		~CApartment() { winrt::uninit_apartment(); }
	};
	try
	{
		CApartment Apartment;
		using namespace winrt::Windows::Media::Control;
		auto Request = GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
		if(!Complete(Request)) return {};
		auto Manager = Request.GetResults();
		auto Session = Manager.GetCurrentSession();
		if(!Session || Session.GetPlaybackInfo().PlaybackStatus() != GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) return {};
		auto Get = Session.TryGetMediaPropertiesAsync();
		if(!Complete(Get)) return {};
		auto Properties = Get.GetResults();
		CNowPlaying::CTrack Track;
		Track.m_Artist = winrt::to_string(Properties.Artist());
		Track.m_Title = winrt::to_string(Properties.Title());
		try { Track.m_pArt = Thumbnail(Properties.Thumbnail()); }
		catch(const winrt::hresult_error &) { /* Text remains useful without artwork. */ }
		return Track;
	}
	catch(const winrt::hresult_error &) { return {}; }
}
}
#endif
