#include "pch.h"

#include "CustomJacketInternal.h"

#include "HookUtils.h"

#include <algorithm>
#include <sstream>
#include <wincodec.h>
#include <wrl/client.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace
{
using Microsoft::WRL::ComPtr;

class ComInit
{
public:
    ComInit() : hr_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComInit()
    {
        if (hr_ == S_OK || hr_ == S_FALSE) CoUninitialize();
    }
    bool Ok() const
    {
        return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE;
    }
    HRESULT Hr() const { return hr_; }

private:
    HRESULT hr_;
};

std::string H(HRESULT hr)
{
    return HookUtils::HexU64(static_cast<uint32_t>(hr));
}

void LogDecodeFail(const char* step, HRESULT hr, const Logger& logger)
{
    std::ostringstream oss;
    oss << "jacket image decode " << step << " failed hr=" << H(hr);
    logger.Log(oss.str());
}

uint32_t ScaleCrop(uint32_t value, uint32_t source, uint32_t target)
{
    const uint64_t scaled = (uint64_t(value) * target + source / 2) / source;
    return std::max<uint32_t>(1, static_cast<uint32_t>(scaled));
}

WICRect CoverCropRect(uint32_t sourceW, uint32_t sourceH,
    uint32_t targetW, uint32_t targetH)
{
    WICRect rect = {};
    uint32_t cropW = sourceW;
    uint32_t cropH = sourceH;
    if (uint64_t(sourceW) * targetH > uint64_t(sourceH) * targetW)
    {
        cropW = ScaleCrop(sourceH, targetH, targetW);
    }
    else if (uint64_t(sourceW) * targetH < uint64_t(sourceH) * targetW)
    {
        cropH = ScaleCrop(sourceW, targetW, targetH);
    }
    cropW = cropW < sourceW ? cropW : sourceW;
    cropH = cropH < sourceH ? cropH : sourceH;
    rect.X = static_cast<INT>((sourceW - cropW) / 2);
    rect.Y = static_cast<INT>((sourceH - cropH) / 2);
    rect.Width = static_cast<INT>(cropW);
    rect.Height = static_cast<INT>(cropH);
    return rect;
}

void LogDecodeOk(uint32_t encodedBytes, uint32_t sourceW, uint32_t sourceH,
    uint32_t drawW, uint32_t drawH, uint32_t targetW, uint32_t targetH,
    const Logger& logger)
{
    std::ostringstream oss;
    oss << "jacket image decode ok bytes=" << encodedBytes
        << " source=" << sourceW << "x" << sourceH
        << " draw=" << drawW << "x" << drawH
        << " target=" << targetW << "x" << targetH;
    logger.Log(oss.str());
}
}

namespace CustomJacketInternal
{
bool TryDecodeCustomJacketImageToRgba(const uint8_t* encoded, uint32_t encodedBytes,
    uint32_t targetW, uint32_t targetH, std::vector<uint8_t>& rgba,
    uint32_t& sourceW, uint32_t& sourceH, uint32_t& drawW, uint32_t& drawH,
    const Logger& logger)
{
    rgba.clear();
    sourceW = 0; sourceH = 0; drawW = 0; drawH = 0;
    if (!encoded || !encodedBytes || !targetW || !targetH) return false;

    ComInit com;
    if (!com.Ok())
    {
        LogDecodeFail("coinitialize", com.Hr(), logger);
        return false;
    }

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) { LogDecodeFail("factory", hr, logger); return false; }

    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(stream.GetAddressOf());
    if (FAILED(hr)) { LogDecodeFail("stream", hr, logger); return false; }
    hr = stream->InitializeFromMemory(const_cast<BYTE*>(encoded), encodedBytes);
    if (FAILED(hr)) { LogDecodeFail("memory", hr, logger); return false; }

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromStream(stream.Get(), nullptr,
        WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf());
    if (FAILED(hr)) { LogDecodeFail("decoder", hr, logger); return false; }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr)) { LogDecodeFail("frame", hr, logger); return false; }
    hr = frame->GetSize(&sourceW, &sourceH);
    if (FAILED(hr) || !sourceW || !sourceH)
    {
        LogDecodeFail("size", FAILED(hr) ? hr : E_INVALIDARG, logger);
        return false;
    }

    const WICRect cropRect = CoverCropRect(sourceW, sourceH, targetW, targetH);
    drawW = targetW;
    drawH = targetH;
    ComPtr<IWICBitmapClipper> clipper;
    hr = factory->CreateBitmapClipper(clipper.GetAddressOf());
    if (FAILED(hr)) { LogDecodeFail("clipper", hr, logger); return false; }
    hr = clipper->Initialize(frame.Get(), &cropRect);
    if (FAILED(hr)) { LogDecodeFail("crop", hr, logger); return false; }

    ComPtr<IWICBitmapScaler> scaler;
    hr = factory->CreateBitmapScaler(scaler.GetAddressOf());
    if (FAILED(hr)) { LogDecodeFail("scaler", hr, logger); return false; }
    hr = scaler->Initialize(clipper.Get(), drawW, drawH, WICBitmapInterpolationModeFant);
    if (FAILED(hr)) { LogDecodeFail("scale", hr, logger); return false; }

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) { LogDecodeFail("converter", hr, logger); return false; }
    hr = converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) { LogDecodeFail("format", hr, logger); return false; }

    rgba.resize(uint64_t(targetW) * targetH * 4);
    hr = converter->CopyPixels(nullptr, drawW * 4,
        static_cast<UINT>(rgba.size()), rgba.data());
    if (FAILED(hr)) { LogDecodeFail("copy", hr, logger); return false; }

    LogDecodeOk(encodedBytes, sourceW, sourceH, drawW, drawH, targetW, targetH, logger);
    return true;
}
}
