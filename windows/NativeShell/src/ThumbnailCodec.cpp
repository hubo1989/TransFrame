// ThumbnailCodec.cpp — WIC in-memory encode/decode (see header). All paths
// stay in WIC land: scaling goes through IWICBitmapScaler and container
// transcodes through IWICFormatConverter, so nothing is scaled by hand.
#include "pch.h"

#include "tf/ThumbnailCodec.hpp"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <limits>

#pragma comment(lib, "windowscodecs.lib")

namespace tf {

namespace {

using Microsoft::WRL::ComPtr;

ComPtr<IWICImagingFactory> wicFactory() {
    ComPtr<IWICImagingFactory> f;
    ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                       IID_PPV_ARGS(&f));
    return f;
}

/// Scaled dimensions preserving aspect; never upscales.
void scaledSize(int srcW, int srcH, int maxEdge, int& outW, int& outH) {
    const double fit = std::min(1.0, static_cast<double>(maxEdge) /
                                         std::max(1, std::max(srcW, srcH)));
    outW = std::max(1, static_cast<int>(srcW * fit));
    outH = std::max(1, static_cast<int>(srcH * fit));
}

/// Decode any container to BGRA, optionally downscaled. Invalid buffer on
/// failure.
PixelBuffer decodeInternal(const std::uint8_t* data, std::size_t size, int maxEdge) {
    PixelBuffer out;
    if (!data || size == 0) return out;
    ComPtr<IWICImagingFactory> wic = wicFactory();
    if (!wic) return out;

    ComPtr<IWICStream> stream;
    if (FAILED(wic->CreateStream(&stream))) return out;
    // InitializeFromMemory keeps a read-only view over `data`; the view only
    // needs to outlive this synchronous call.
    if (FAILED(stream->InitializeFromMemory(const_cast<WICInProcPointer>(data),
                                            static_cast<DWORD>(size)))) {
        return out;
    }
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand,
                                            &decoder))) {
        return out;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)) || !frame) return out;

    UINT srcW = 0, srcH = 0;
    if (FAILED(frame->GetSize(&srcW, &srcH)) || srcW == 0 || srcH == 0) return out;

    ComPtr<IWICBitmapSource> source = frame;
    int dstW = 0, dstH = 0;
    scaledSize(static_cast<int>(srcW), static_cast<int>(srcH), maxEdge, dstW, dstH);
    if (dstW != static_cast<int>(srcW) || dstH != static_cast<int>(srcH)) {
        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(wic->CreateBitmapScaler(&scaler))) return out;
        if (FAILED(scaler->Initialize(frame.Get(), static_cast<UINT>(dstW),
                                      static_cast<UINT>(dstH),
                                      WICBitmapInterpolationModeFant))) {
            return out;
        }
        source = scaler;
    }
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wic->CreateFormatConverter(&converter))) return out;
    if (FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeMedianCut))) {
        return out;
    }

    const std::size_t rowBytes = static_cast<std::size_t>(dstW) * 4;
    out.width = dstW;
    out.height = dstH;
    out.stride = static_cast<std::int32_t>(rowBytes);
    out.dpiX = 96;
    out.dpiY = 96;
    out.data = std::shared_ptr<std::uint8_t[]>(
        new std::uint8_t[rowBytes * static_cast<std::size_t>(dstH)]{});
    if (FAILED(converter->CopyPixels(nullptr, static_cast<UINT>(rowBytes),
                                     static_cast<UINT>(rowBytes * static_cast<std::size_t>(dstH)),
                                     out.data.get()))) {
        return PixelBuffer{};
    }
    return out;
}

}  // namespace

std::vector<std::uint8_t> encodeJpegThumbnail(const PixelBuffer& pixels, int maxEdge,
                                              int qualityPercent) {
    std::vector<std::uint8_t> out;
    if (!pixels.valid()) return out;
    ComPtr<IWICImagingFactory> wic = wicFactory();
    if (!wic) return out;

    ComPtr<IWICBitmap> bitmap;
    if (FAILED(wic->CreateBitmapFromMemory(
            static_cast<UINT>(pixels.width), static_cast<UINT>(pixels.height),
            GUID_WICPixelFormat32bppBGRA, static_cast<UINT>(pixels.stride),
            static_cast<UINT>(pixels.byteSize()), pixels.data.get(), &bitmap))) {
        return out;
    }

    int dstW = 0, dstH = 0;
    scaledSize(pixels.width, pixels.height, maxEdge, dstW, dstH);
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(wic->CreateBitmapScaler(&scaler))) return out;
    if (FAILED(scaler->Initialize(bitmap.Get(), static_cast<UINT>(dstW),
                                  static_cast<UINT>(dstH),
                                  WICBitmapInterpolationModeFant))) {
        return out;
    }
    // JPEG only takes YCbCr-family pixel formats; convert from the scaler.
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wic->CreateFormatConverter(&converter))) return out;
    if (FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat24bppBGR,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeMedianCut))) {
        return out;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(wic->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder))) return out;
    ComPtr<IStream> stream;
    HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, 0);
    if (!mem) return out;
    if (FAILED(::CreateStreamOnHGlobal(mem, /*deleteOnRelease=*/TRUE, &stream))) {
        ::GlobalFree(mem);
        return out;
    }
    if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return out;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> bag;
    if (FAILED(encoder->CreateNewFrame(&frame, &bag))) return out;
    if (bag) {
        // ImageQuality must ride the bag BEFORE frame->Initialize (see
        // ImageExporter.cpp for the same dance).
        PROPBAG2 option{};
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        option.vt = VT_R4;
        VARIANT value{};
        value.vt = VT_R4;
        value.fltVal = std::clamp(qualityPercent, 0, 100) / 100.0f;
        bag->Write(1, &option, &value);
    }
    if (FAILED(frame->Initialize(bag.Get()))) return out;
    if (FAILED(frame->SetSize(static_cast<UINT>(dstW), static_cast<UINT>(dstH)))) return out;
    GUID pixelFormat = GUID_WICPixelFormat24bppBGR;
    if (FAILED(frame->SetPixelFormat(&pixelFormat))) return out;
    if (FAILED(frame->WriteSource(converter.Get(), nullptr))) return out;
    if (FAILED(frame->Commit()) || FAILED(encoder->Commit())) return out;

    STATSTG stat{};
    LARGE_INTEGER seekBack{};
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) ||
        FAILED(stream->Seek(seekBack, STREAM_SEEK_SET, nullptr))) {
        return out;
    }
    out.resize(static_cast<std::size_t>(stat.cbSize.QuadPart));
    ULONG read = 0;
    if (FAILED(stream->Read(out.data(), static_cast<ULONG>(out.size()), &read)) ||
        read != out.size()) {
        return std::vector<std::uint8_t>{};
    }
    return out;
}

PixelBuffer decodeImageMemory(const std::uint8_t* data, std::size_t size) {
    return decodeInternal(data, size, std::numeric_limits<int>::max());
}

PixelBuffer decodeImageScaled(const std::uint8_t* data, std::size_t size, int maxEdge) {
    return decodeInternal(data, size, maxEdge);
}

}  // namespace tf
