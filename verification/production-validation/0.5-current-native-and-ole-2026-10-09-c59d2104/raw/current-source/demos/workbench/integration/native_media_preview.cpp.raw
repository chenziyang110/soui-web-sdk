#include "native_media_preview.h"

#ifdef _WIN32
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wincodec.h>
#include <objidl.h>
#include <propvarutil.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace soui::demo {
namespace {

#ifdef _WIN32

template <typename T>
class com_ptr final {
 public:
  com_ptr() = default;
  ~com_ptr() { reset(); }
  com_ptr(const com_ptr&) = delete;
  com_ptr& operator=(const com_ptr&) = delete;
  [[nodiscard]] T* get() const noexcept { return value_; }
  [[nodiscard]] T** put() noexcept { reset(); return &value_; }
  void reset(T* value = nullptr) noexcept {
    if (value_) value_->Release();
    value_ = value;
  }
  T* operator->() const noexcept { return value_; }
 private:
  T* value_{nullptr};
};

struct mf_api final {
  using startup_fn = HRESULT(WINAPI*)(ULONG, DWORD);
  using shutdown_fn = HRESULT(WINAPI*)();
  using create_attributes_fn = HRESULT(WINAPI*)(IMFAttributes**, UINT32);
  using create_media_type_fn = HRESULT(WINAPI*)(IMFMediaType**);
  using create_reader_fn = HRESULT(WINAPI*)(LPCWSTR, IMFAttributes*, IMFSourceReader**);

  HMODULE plat{nullptr};
  HMODULE readwrite{nullptr};
  startup_fn startup{nullptr};
  shutdown_fn shutdown{nullptr};
  create_attributes_fn create_attributes{nullptr};
  create_media_type_fn create_media_type{nullptr};
  create_reader_fn create_reader{nullptr};

  ~mf_api() {
    if (readwrite) FreeLibrary(readwrite);
    if (plat) FreeLibrary(plat);
  }

  bool load() {
    plat = LoadLibraryW(L"mfplat.dll");
    readwrite = LoadLibraryW(L"mfreadwrite.dll");
    if (!plat || !readwrite) return false;
    startup = reinterpret_cast<startup_fn>(GetProcAddress(plat, "MFStartup"));
    shutdown = reinterpret_cast<shutdown_fn>(GetProcAddress(plat, "MFShutdown"));
    create_attributes = reinterpret_cast<create_attributes_fn>(GetProcAddress(plat, "MFCreateAttributes"));
    create_media_type = reinterpret_cast<create_media_type_fn>(GetProcAddress(plat, "MFCreateMediaType"));
    create_reader = reinterpret_cast<create_reader_fn>(GetProcAddress(readwrite, "MFCreateSourceReaderFromURL"));
    return startup && shutdown && create_attributes && create_media_type && create_reader;
  }
};

std::string base64_encode(const std::uint8_t* bytes, std::size_t size) {
  static constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  result.reserve(((size + 2U) / 3U) * 4U);
  for (std::size_t index = 0; index < size; index += 3U) {
    const std::uint32_t a = bytes[index];
    const std::uint32_t b = index + 1U < size ? bytes[index + 1U] : 0U;
    const std::uint32_t c = index + 2U < size ? bytes[index + 2U] : 0U;
    const std::uint32_t value = (a << 16U) | (b << 8U) | c;
    result.push_back(alphabet[(value >> 18U) & 0x3FU]);
    result.push_back(alphabet[(value >> 12U) & 0x3FU]);
    result.push_back(index + 1U < size ? alphabet[(value >> 6U) & 0x3FU] : '=');
    result.push_back(index + 2U < size ? alphabet[value & 0x3FU] : '=');
  }
  return result;
}

bool encode_png(const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height,
                std::string& data_uri) {
  if (!pixels || width == 0 || height == 0 || width > 3840U || height > 2160U)
    return false;
  com_ptr<IWICImagingFactory> factory;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(factory.put())))) return false;
  com_ptr<IWICBitmap> bitmap;
  const UINT stride = width * 4U;
  if (FAILED(factory->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppBGRA,
                                             stride, stride * height,
                                             const_cast<BYTE*>(pixels), bitmap.put()))) return false;
  const std::uint32_t target_width = std::min<std::uint32_t>(width, 320U);
  const std::uint32_t target_height = std::max<std::uint32_t>(1U,
      static_cast<std::uint32_t>((static_cast<std::uint64_t>(height) * target_width) / width));
  com_ptr<IWICBitmapScaler> scaler;
  if (FAILED(factory->CreateBitmapScaler(scaler.put())) ||
      FAILED(scaler->Initialize(bitmap.get(), target_width, target_height,
                                WICBitmapInterpolationModeFant))) return false;
  com_ptr<IStream> output_stream;
  if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, output_stream.put()))) return false;
  com_ptr<IWICStream> wic_stream;
  if (FAILED(factory->CreateStream(wic_stream.put())) ||
      FAILED(wic_stream->InitializeFromIStream(output_stream.get()))) return false;
  com_ptr<IWICBitmapEncoder> encoder;
  if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put())) ||
      FAILED(encoder->Initialize(wic_stream.get(), WICBitmapEncoderNoCache))) return false;
  com_ptr<IWICBitmapFrameEncode> frame;
  com_ptr<IPropertyBag2> properties;
  if (FAILED(encoder->CreateNewFrame(frame.put(), properties.put())) ||
      FAILED(frame->Initialize(nullptr)) ||
      FAILED(frame->SetSize(target_width, target_height))) return false;
  GUID pixel_format = GUID_WICPixelFormat32bppBGRA;
  if (FAILED(frame->SetPixelFormat(&pixel_format)) ||
      FAILED(frame->WriteSource(scaler.get(), nullptr)) ||
      FAILED(frame->Commit()) || FAILED(encoder->Commit())) return false;
  HGLOBAL global = nullptr;
  if (FAILED(GetHGlobalFromStream(output_stream.get(), &global)) || !global) return false;
  STATSTG stream_stat{};
  if (FAILED(output_stream->Stat(&stream_stat, STATFLAG_NONAME)) ||
      stream_stat.cbSize.QuadPart == 0 || stream_stat.cbSize.QuadPart > 48U * 1024U)
    return false;
  const SIZE_T size = static_cast<SIZE_T>(stream_stat.cbSize.QuadPart);
  void* locked = GlobalLock(global);
  if (!locked) return false;
  const auto encoded = base64_encode(static_cast<const std::uint8_t*>(locked), size);
  char dump_path[MAX_PATH] = {};
  const DWORD dump_length = GetEnvironmentVariableA("SOUI_MEDIA_PREVIEW_DUMP", dump_path, sizeof(dump_path));
  if (dump_length > 0 && dump_length < sizeof(dump_path)) {
    std::ofstream dump(dump_path, std::ios::binary | std::ios::trunc);
    if (dump) dump.write(static_cast<const char*>(locked), static_cast<std::streamsize>(size));
  }
  GlobalUnlock(global);
  data_uri = "data:image/png;base64," + encoded;
  return true;
}

#endif

}  // namespace

bool media_preview_available() noexcept {
#ifdef _WIN32
  mf_api api;
  return api.load();
#else
  return false;
#endif
}

media_preview_result decode_media_preview(const std::filesystem::path& path) {
  media_preview_result result;
#ifdef _WIN32
  if (path.empty() || !std::filesystem::is_regular_file(path)) {
    result.message = "The media file is missing.";
    return result;
  }
  mf_api api;
  if (!api.load()) {
    result.message = "Media Foundation is not available on this Windows host.";
    return result;
  }
  const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool uninitialize_com = SUCCEEDED(com_result);
  HRESULT hr = api.startup(MF_VERSION, MFSTARTUP_FULL);
  if (FAILED(hr)) {
    if (uninitialize_com) CoUninitialize();
    result.message = "Media Foundation could not start.";
    return result;
  }
  com_ptr<IMFAttributes> attributes;
  com_ptr<IMFSourceReader> reader;
  com_ptr<IMFMediaType> native_type;
  com_ptr<IMFMediaType> output_type;
  UINT32 width = 0;
  UINT32 height = 0;
  bool decoded = false;
  constexpr DWORD first_video_stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
  constexpr DWORD media_source = static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE);
  do {
    if (FAILED(api.create_attributes(attributes.put(), 2U))) break;
    attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    if (FAILED(api.create_reader(path.c_str(), attributes.get(), reader.put()))) break;
    if (FAILED(reader->GetNativeMediaType(first_video_stream, 0,
                                          native_type.put()))) break;
    UINT64 packed_size = 0;
    if (FAILED(native_type->GetUINT64(MF_MT_FRAME_SIZE, &packed_size))) break;
    width = static_cast<UINT32>(packed_size >> 32U);
    height = static_cast<UINT32>(packed_size & 0xffffffffU);
    if (width == 0 || height == 0 || width > 3840U || height > 2160U) break;
    if (FAILED(api.create_media_type(output_type.put()))) break;
    if (FAILED(output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(output_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) ||
        FAILED(output_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)) ||
        FAILED(output_type->SetUINT64(MF_MT_FRAME_SIZE,
                                      (static_cast<UINT64>(width) << 32U) | height))) break;
    if (FAILED(reader->SetCurrentMediaType(first_video_stream, nullptr,
                                           output_type.get()))) break;
    LONG default_stride = static_cast<LONG>(width * 4U);
    UINT32 stride_value = 0;
    if (SUCCEEDED(output_type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride_value)) && stride_value != 0)
      default_stride = static_cast<LONG>(stride_value);
    PROPVARIANT duration;
    PropVariantInit(&duration);
    if (SUCCEEDED(reader->GetPresentationAttribute(media_source, MF_PD_DURATION,
                                                   &duration))) {
      if (duration.vt == VT_UI8) result.duration_ms = duration.uhVal.QuadPart / 10000U;
      else if (duration.vt == VT_I8) result.duration_ms = duration.hVal.QuadPart / 10000U;
    }
    PropVariantClear(&duration);
    // The bundled product sample starts with a short fade-to-black. Seeking
    // to an early visible point keeps the native preview useful while still
    // decoding the frame through Media Foundation. For very short clips the
    // quarter-duration clamp stays inside the stream.
    if (result.duration_ms > 0U) {
      const std::uint64_t seek_ms = std::min<std::uint64_t>(1000U,
          std::max<std::uint64_t>(1U, result.duration_ms / 4U));
      PROPVARIANT position;
      PropVariantInit(&position);
      position.vt = VT_I8;
      position.hVal.QuadPart = static_cast<LONGLONG>(seek_ms) * 10000LL;
      (void)reader->SetCurrentPosition(GUID_NULL, position);
      PropVariantClear(&position);
    }
    std::vector<std::uint8_t> pending_frame;
    for (unsigned attempt = 0; attempt < 120U && !decoded; ++attempt) {
      DWORD stream = 0;
      DWORD flags = 0;
      LONGLONG timestamp = 0;
      com_ptr<IMFSample> sample;
      if (FAILED(reader->ReadSample(first_video_stream, 0, &stream,
                                    &flags, &timestamp, sample.put()))) break;
      if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
      if (!sample.get()) continue;
      com_ptr<IMFMediaBuffer> buffer;
      if (FAILED(sample->ConvertToContiguousBuffer(buffer.put()))) continue;
      const auto accept_frame = [&](const BYTE* source, LONG source_pitch) {
        if (!source) return false;
        const std::size_t row_bytes = static_cast<std::size_t>(width) * 4U;
        const std::size_t pitch = source_pitch < 0
            ? static_cast<std::size_t>(-(static_cast<std::int64_t>(source_pitch)))
            : static_cast<std::size_t>(source_pitch);
        if (pitch < row_bytes) return false;
        std::vector<std::uint8_t> packed(row_bytes * height);
        for (std::uint32_t row = 0; row < height; ++row) {
          const auto source_row = source_pitch < 0
              ? source + static_cast<std::size_t>(height - 1U - row) * pitch
              : source + static_cast<std::size_t>(row) * pitch;
          auto* target_row = packed.data() + static_cast<std::size_t>(row) * row_bytes;
          std::memcpy(target_row, source_row, row_bytes);
          // MFVideoFormat_RGB32 commonly leaves the unused X/alpha byte at
          // zero. WIC treats the same memory as BGRA, so zero alpha would
          // encode the real decoded pixels as transparent black.
          for (std::size_t channel = 3U; channel < row_bytes; channel += 4U)
            target_row[channel] = 0xFFU;
        }
        bool visible = false;
        unsigned max_channel = 0;
        std::size_t non_zero_pixels = 0;
        const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
        const std::size_t sample_stride = std::max<std::size_t>(1U, pixel_count / 2048U);
        for (std::size_t pixel = 0; pixel < pixel_count; pixel += sample_stride) {
          const auto* pixel_data = packed.data() + pixel * 4U;
          const unsigned channel_sum = static_cast<unsigned>(pixel_data[0]) + pixel_data[1] + pixel_data[2];
          max_channel = std::max(max_channel, std::max<unsigned>(pixel_data[0], std::max<unsigned>(pixel_data[1], pixel_data[2])));
          if (channel_sum > 24U) {
            visible = true;
            ++non_zero_pixels;
          }
        }
        char trace_path[MAX_PATH] = {};
        const DWORD trace_length = GetEnvironmentVariableA("SOUI_MEDIA_PREVIEW_TRACE", trace_path, sizeof(trace_path));
        if (trace_length > 0 && trace_length < sizeof(trace_path)) {
          std::ofstream trace(trace_path, std::ios::app);
          if (trace) {
            trace << "attempt=" << attempt << " pitch=" << source_pitch
                  << " maxChannel=" << max_channel << " visible=" << (visible ? 1 : 0)
                  << " sampledNonZero=" << non_zero_pixels << "\n";
          }
        }
        const bool accepted = visible || attempt >= 30U;
        if (accepted) pending_frame = std::move(packed);
        return accepted;
      };
      com_ptr<IMF2DBuffer> buffer_2d;
      if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(buffer_2d.put())))) {
        BYTE* scan = nullptr;
        LONG pitch = 0;
        if (SUCCEEDED(buffer_2d->Lock2D(&scan, &pitch))) {
          decoded = accept_frame(scan, pitch);
          buffer_2d->Unlock2D();
        }
      } else {
        BYTE* bytes = nullptr;
        DWORD max_length = 0;
        DWORD current_length = 0;
        if (SUCCEEDED(buffer->Lock(&bytes, &max_length, &current_length))) {
          const std::uint64_t expected = static_cast<std::uint64_t>(width) * height * 4U;
          if (current_length >= expected) decoded = accept_frame(bytes, default_stride);
          buffer->Unlock();
        }
      }
      if (decoded) {
        decoded = encode_png(pending_frame.data(), width, height, result.frame_data);
      }
    }
  } while (false);
  // Release every Media Foundation interface while COM is still initialized.
  // The worker may call this adapter on a fresh MTA thread; destroying an
  // interface after CoUninitialize is undefined and can crash the host.
  reader.reset();
  output_type.reset();
  native_type.reset();
  attributes.reset();
  api.shutdown();
  if (uninitialize_com) CoUninitialize();
  if (decoded) {
    result.supported = true;
    result.width = std::min<std::uint32_t>(width, 320U);
    result.height = std::max<std::uint32_t>(1U,
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(height) * result.width) / width));
    result.decoder = "Media Foundation / RGB32 / PNG thumbnail";
    result.message = "Native first-frame preview decoded.";
  } else if (result.message.empty()) {
    result.message = "Media Foundation could not decode a preview frame.";
  }
#else
  (void)path;
  result.message = "Native media preview is only available on Windows hosts.";
#endif
  return result;
}

media_decode_result measure_media_decode(const std::filesystem::path& path, std::uint32_t max_frames) {
  media_decode_result result;
#ifdef _WIN32
  if (path.empty() || !std::filesystem::is_regular_file(path) || max_frames == 0U) {
    result.message = "The bounded decode source is missing.";
    return result;
  }
  mf_api api;
  if (!api.load()) { result.message = "Media Foundation is not available."; return result; }
  const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool uninitialize_com = SUCCEEDED(com_result);
  if (FAILED(api.startup(MF_VERSION, MFSTARTUP_FULL))) {
    if (uninitialize_com) CoUninitialize();
    result.message = "Media Foundation could not start.";
    return result;
  }
  com_ptr<IMFAttributes> attributes;
  com_ptr<IMFSourceReader> reader;
  com_ptr<IMFMediaType> native_type;
  com_ptr<IMFMediaType> output_type;
  constexpr DWORD first_video_stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
  do {
    if (FAILED(api.create_attributes(attributes.put(), 2U))) break;
    attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    if (FAILED(api.create_reader(path.c_str(), attributes.get(), reader.put()))) break;
    if (FAILED(reader->GetNativeMediaType(first_video_stream, 0, native_type.put()))) break;
    UINT64 packed_size = 0;
    if (FAILED(native_type->GetUINT64(MF_MT_FRAME_SIZE, &packed_size))) break;
    result.width = static_cast<UINT32>(packed_size >> 32U);
    result.height = static_cast<UINT32>(packed_size & 0xffffffffU);
    if (result.width == 0 || result.height == 0 || result.width > 3840U || result.height > 2160U) break;
    if (FAILED(api.create_media_type(output_type.put()))) break;
    if (FAILED(output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(output_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) ||
        FAILED(output_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)) ||
        FAILED(output_type->SetUINT64(MF_MT_FRAME_SIZE,
                                      (static_cast<UINT64>(result.width) << 32U) | result.height))) break;
    if (FAILED(reader->SetCurrentMediaType(first_video_stream, nullptr, output_type.get()))) break;
    const auto started = std::chrono::steady_clock::now();
    for (std::uint32_t attempt = 0; result.frames < max_frames && attempt < max_frames * 8U; ++attempt) {
      DWORD stream = 0, flags = 0;
      LONGLONG timestamp = 0;
      com_ptr<IMFSample> sample;
      if (FAILED(reader->ReadSample(first_video_stream, 0, &stream, &flags, &timestamp, sample.put()))) break;
      if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
      if (!sample.get()) continue;
      com_ptr<IMFMediaBuffer> buffer;
      if (FAILED(sample->ConvertToContiguousBuffer(buffer.put()))) continue;
      com_ptr<IMF2DBuffer> buffer_2d;
      BYTE* bytes = nullptr;
      LONG pitch = 0;
      bool has_2d = false;
      bool locked_2d = false;
      if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(buffer_2d.put())))) {
        has_2d = true;
        locked_2d = SUCCEEDED(buffer_2d->Lock2D(&bytes, &pitch));
      } else {
        DWORD maximum = 0, current = 0;
        if (SUCCEEDED(buffer->Lock(&bytes, &maximum, &current))) {
          pitch = static_cast<LONG>(result.width * 4U);
          locked_2d = current >= static_cast<DWORD>(result.width * result.height * 4U);
          if (!locked_2d) buffer->Unlock();
        }
      }
      if (!locked_2d || !bytes) {
        if (has_2d && locked_2d) buffer_2d->Unlock2D();
        continue;
      }
      const std::size_t row_bytes = static_cast<std::size_t>(result.width) * 4U;
      const std::size_t pitch_abs = pitch < 0 ? static_cast<std::size_t>(-static_cast<std::int64_t>(pitch))
                                             : static_cast<std::size_t>(pitch);
      if (pitch_abs >= row_bytes) {
        const std::size_t sample_stride = std::max<std::size_t>(1U,
            (static_cast<std::size_t>(result.width) * result.height) / 2048U);
        for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(result.width) * result.height; pixel += sample_stride) {
          const std::size_t row = pixel / result.width;
          const std::size_t column = pixel % result.width;
          const auto* source_row = pitch < 0 ? bytes + (static_cast<std::size_t>(result.height) - 1U - row) * pitch_abs
                                             : bytes + row * pitch_abs;
          const auto* pixel_data = source_row + column * 4U;
          result.checksum = (result.checksum * 1315423911ULL) ^
                            (static_cast<std::uint64_t>(pixel_data[0]) |
                             (static_cast<std::uint64_t>(pixel_data[1]) << 8U) |
                             (static_cast<std::uint64_t>(pixel_data[2]) << 16U));
        }
        ++result.frames;
      }
      if (has_2d) buffer_2d->Unlock2D(); else buffer->Unlock();
    }
    const double seconds = std::max(0.001, std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
    result.decode_fps = static_cast<double>(result.frames) / seconds;
  } while (false);
  // COM interfaces must be released before the apartment is torn down.
  reader.reset();
  output_type.reset();
  native_type.reset();
  attributes.reset();
  api.shutdown();
  if (uninitialize_com) CoUninitialize();
  result.supported = result.frames > 0;
  result.decoder = result.supported ? "Media Foundation / RGB32 bounded decode" : "";
  result.message = result.supported ? "Bounded native frames decoded." : "Media Foundation returned no decodable frames.";
#else
  (void)path; (void)max_frames;
  result.message = "Native decode is only available on Windows.";
#endif
  return result;
}

}  // namespace soui::demo
