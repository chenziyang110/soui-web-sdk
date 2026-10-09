#include "native_media_encoder.h"
#include "native_media_preview.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace soui::demo {
namespace {

int base64_value(unsigned char value) noexcept {
  if (value >= 'A' && value <= 'Z') return static_cast<int>(value - 'A');
  if (value >= 'a' && value <= 'z') return static_cast<int>(value - 'a' + 26U);
  if (value >= '0' && value <= '9') return static_cast<int>(value - '0' + 52U);
  if (value == '+') return 62;
  if (value == '/') return 63;
  return -1;
}

std::vector<std::uint8_t> decode_base64(std::string_view value) {
  if (value.empty() || value.size() % 4U != 0U) return {};
  std::vector<std::uint8_t> result;
  result.reserve((value.size() / 4U) * 3U);
  for (std::size_t offset = 0; offset < value.size(); offset += 4U) {
    const auto c0 = static_cast<unsigned char>(value[offset]);
    const auto c1 = static_cast<unsigned char>(value[offset + 1U]);
    const auto c2 = static_cast<unsigned char>(value[offset + 2U]);
    const auto c3 = static_cast<unsigned char>(value[offset + 3U]);
    const int a = base64_value(c0);
    const int b = base64_value(c1);
    const bool pad2 = c2 == '=';
    const bool pad3 = c3 == '=';
    if (a < 0 || b < 0 || (pad2 && !pad3) ||
        (pad2 && offset + 4U != value.size()) ||
        (pad3 && offset + 4U != value.size())) return {};
    const int c = pad2 ? 0 : base64_value(c2);
    const int d = pad3 ? 0 : base64_value(c3);
    if (c < 0 || d < 0) return {};
    const unsigned packed = (static_cast<unsigned>(a) << 18U) |
                            (static_cast<unsigned>(b) << 12U) |
                            (static_cast<unsigned>(c) << 6U) |
                            static_cast<unsigned>(d);
    result.push_back(static_cast<std::uint8_t>((packed >> 16U) & 0xFFU));
    if (!pad2) result.push_back(static_cast<std::uint8_t>((packed >> 8U) & 0xFFU));
    if (!pad3) result.push_back(static_cast<std::uint8_t>(packed & 0xFFU));
  }
  return result;
}

media_encode_result encode_native_png_snapshot(const std::filesystem::path& source,
    const std::filesystem::path& output, const std::atomic<bool>& cancel,
    const std::function<void(unsigned)>& progress) {
  if (cancel.load(std::memory_order_relaxed)) return {false, true, "任务已取消。"};
  const auto preview = decode_media_preview(source);
  static constexpr std::string_view prefix = "data:image/png;base64,";
  if (!preview.supported || preview.frame_data.rfind(prefix, 0) != 0)
    return {false, false, preview.message.empty() ? "Media Foundation could not create a native PNG snapshot." : preview.message};
  progress(20U);
  const auto bytes = decode_base64(std::string_view(preview.frame_data).substr(prefix.size()));
  static constexpr std::uint8_t png_signature[] = {0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
  if (bytes.size() < sizeof(png_signature) || bytes.size() > 4U * 1024U * 1024U ||
      !std::equal(std::begin(png_signature), std::end(png_signature), bytes.begin()))
    return {false, false, "Native PNG snapshot failed signature validation."};
  if (cancel.load(std::memory_order_relaxed)) return {false, true, "任务已取消。"};
  std::ofstream stream(output, std::ios::binary | std::ios::trunc);
  if (!stream) return {false, false, "无法创建 native PNG 输出文件。"};
  constexpr std::size_t chunk = 8192U;
  for (std::size_t offset = 0; offset < bytes.size(); offset += chunk) {
    if (cancel.load(std::memory_order_relaxed)) return {false, true, "任务已取消。"};
    const auto count = std::min(chunk, bytes.size() - offset);
    stream.write(reinterpret_cast<const char*>(bytes.data() + offset), static_cast<std::streamsize>(count));
    if (!stream) return {false, false, "写入 native PNG 输出失败。"};
    progress(std::min<unsigned>(95U, 20U + static_cast<unsigned>((offset + count) * 75U / bytes.size())));
  }
  stream.flush();
  if (!stream) return {false, false, "刷新 native PNG 输出失败。"};
  return {true, false, "Media Foundation 解码首帧并由 WIC 生成 PNG 快照。"};
}

}  // namespace

bool valid_media_profile(const std::string& format, const std::string& codec, unsigned quality) {
  if (quality < 10 || quality > 95) return false;
  return ((format == "MP4" || format == "MOV") && codec == "H.264") ||
      (format == "WebM" && codec == "VP9") || (format == "MP3" && codec == "MP3") ||
      (format == "WAV" && codec == "PCM") || (format == "GIF" && codec == "GIF") ||
      (format == "PNG" && codec == "PNG") || (format == "WebP" && codec == "WebP");
}
bool valid_media_operation(std::string_view operation) {
  return operation.empty() || operation == "reverse" || operation == "rotate90" ||
      operation == "mirror" || operation == "speed2x" || operation == "crop16x9" ||
      operation == "trim10" || operation == "fade" || operation == "grayscale" ||
      operation == "watermark" || operation == "denoise" || operation == "normalize";
}
std::string media_output_extension(const std::string& format) {
  if (format == "WebM") return ".webm";
  if (format == "WebP") return ".webp";
  std::string result = "." + format;
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}
std::filesystem::path configured_media_encoder() {
#ifdef _WIN32
  wchar_t value[32768]{};
  const auto length = GetEnvironmentVariableW(L"SOUI_FFMPEG_PATH", value, 32768);
  if (length == 0 || length >= 32768) return {};
  std::filesystem::path path(value);
  std::error_code error;
  if (path.is_absolute() && path.extension() == L".exe" && std::filesystem::is_regular_file(path, error) && !error) return path;
#endif
  return {};
}
#ifdef _WIN32
namespace {
class handle {
 public:
  explicit handle(HANDLE value = nullptr) : value_(value) {}
  ~handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
  handle(const handle&) = delete;
  handle& operator=(const handle&) = delete;
  HANDLE get() const { return value_; }
 private:
  HANDLE value_;
};
// Windows argv quoting, including embedded quotes and trailing backslashes.
std::wstring quote(const std::wstring& value) {
  std::wstring result = L"\"";
  unsigned slashes = 0;
  for (wchar_t ch : value) {
    if (ch == L'\\') { ++slashes; continue; }
    result.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
    slashes = 0;
    result += ch;
  }
  result.append(slashes * 2, L'\\');
  return result + L'"';
}
class attributes {
 public:
  attributes() {
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    bytes_.resize(size);
    list_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(bytes_.data());
    ready_ = InitializeProcThreadAttributeList(list_, 1, 0, &size) != FALSE;
  }
  ~attributes() { if (ready_) DeleteProcThreadAttributeList(list_); }
  bool handles(HANDLE* values, SIZE_T count) {
    return ready_ && UpdateProcThreadAttribute(list_, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        values, count * sizeof(HANDLE), nullptr, nullptr);
  }
  LPPROC_THREAD_ATTRIBUTE_LIST get() const { return list_; }
 private:
  std::vector<unsigned char> bytes_;
  LPPROC_THREAD_ATTRIBUTE_LIST list_{};
  bool ready_{false};
};
}
#endif
media_encode_result save_media_output(const std::filesystem::path& source,
    const std::filesystem::path& staging, const std::filesystem::path& destination,
    const std::atomic<bool>& cancel, const std::function<void(unsigned)>& progress) {
  if (cancel.load()) return {false, true, "保存已取消。"};
  std::error_code error;
  if (source == destination || source == staging || destination == staging ||
      (std::filesystem::exists(destination, error) && std::filesystem::equivalent(source, destination, error))) return {false, false, "不能覆盖源文件。"};
#ifdef _WIN32
  struct copy_state { const std::atomic<bool>* cancel; const std::function<void(unsigned)>* progress; unsigned previous{0}; } state{&cancel, &progress};
  const auto callback = [](LARGE_INTEGER total, LARGE_INTEGER copied, LARGE_INTEGER, LARGE_INTEGER,
                           DWORD, DWORD, HANDLE, HANDLE, LPVOID data) -> DWORD {
    auto& state = *static_cast<copy_state*>(data);
    if (state.cancel->load()) return PROGRESS_CANCEL;
    const auto percent = total.QuadPart > 0 ? static_cast<unsigned>(std::min<LONGLONG>(99, copied.QuadPart * 100 / total.QuadPart)) : 0;
    if (percent > state.previous) { state.previous = percent; (*state.progress)(percent); }
    return PROGRESS_CONTINUE;
  };
  const bool copied = CopyFileExW(source.c_str(), staging.c_str(), callback, &state, nullptr, COPY_FILE_FAIL_IF_EXISTS) != FALSE;
  const bool cancelled = cancel.load();
  const bool saved = copied && !cancelled && MoveFileExW(staging.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
  return {saved, cancelled, saved ? "文件已保存到选择的位置。" : cancelled ? "保存已取消。" : "保存失败，请检查目标磁盘空间和写入权限。"};
#else
  return {false, false, "保存需要 Windows 宿主。"};
#endif
}
media_encode_result encode_media(const std::filesystem::path& executable,
    const std::filesystem::path& source, const std::filesystem::path& output,
    const std::string& format, const std::string& codec, unsigned quality,
    const std::atomic<bool>& cancel, const std::function<void(unsigned)>& progress,
    std::string_view operation) {
  if (!valid_media_profile(format, codec, quality)) return {false, false, "不支持的输出参数。"};
  if (!valid_media_operation(operation)) {
    return {false, false, "不支持的 native 媒体操作。"};
  }
  const bool video_output = format == "MP4" || format == "MOV" || format == "WebM";
  const bool image_output = format == "GIF" || format == "PNG" || format == "WebP";
  const bool audio_output = format == "MP3" || format == "WAV";
  if (!operation.empty() && ((operation == "rotate90" || operation == "mirror") && !video_output && !image_output)) {
    return {false, false, "旋转和镜像只适用于视频或图片。"};
  }
  if (!operation.empty() && operation == "speed2x" && !(video_output || audio_output)) {
    return {false, false, "变速操作不适用于当前输出格式。"};
  }
  if (!operation.empty() && operation == "reverse" && !video_output && !audio_output) {
    return {false, false, "倒放只适用于视频或音频。"};
  }
  if (!operation.empty() && operation == "crop16x9" && !video_output && !image_output) {
    return {false, false, "裁剪只适用于视频或图片。"};
  }
  if (!operation.empty() && operation == "trim10" && !video_output && !audio_output) {
    return {false, false, "片段截取只适用于视频或音频。"};
  }
  if (!operation.empty() && (operation == "fade" || operation == "grayscale" || operation == "watermark") &&
      !video_output && !image_output) {
    return {false, false, "当前视觉操作只适用于视频或图片。"};
  }
  if (!operation.empty() && (operation == "denoise" || operation == "normalize") &&
      !video_output && !audio_output) {
    return {false, false, "当前音频操作只适用于视频或音频。"};
  }
  if (!operation.empty() && format == "GIF") {
    return {false, false, "GIF 动图暂不支持该 native 变换。"};
  }
  if (cancel.load()) return {false, true, "任务已取消。"};
  if (executable.empty()) {
    if (operation.empty() && format == "PNG" && codec == "PNG")
      return encode_native_png_snapshot(source, output, cancel, progress);
    return {false, false, "未配置受信任的 FFmpeg；当前仅支持 Media Foundation PNG 快照。"};
  }
  std::error_code path_error;
  if (std::filesystem::exists(output, path_error) || path_error) return {false, false, "输出文件已存在，拒绝覆盖。"};
  if (!std::filesystem::is_regular_file(source, path_error) || path_error) return {false, false, "输入文件不存在。"};
#ifdef _WIN32
  std::vector<std::wstring> args{executable.wstring(), L"-hide_banner", L"-nostdin", L"-n",
      L"-nostats", L"-progress", L"pipe:1", L"-threads", L"2", L"-filter_threads", L"2",
      L"-protocol_whitelist", L"file,pipe", L"-format_whitelist",
      L"mov,matroska,webm,avi,mp3,wav,flac,ogg,png_pipe,jpeg_pipe,webp_pipe",
      L"-i", source.wstring(), L"-map_metadata", L"-1"};
  const auto add = [&args](std::initializer_list<std::wstring> values) { args.insert(args.end(), values); };
  if (format == "MP4" || format == "MOV" || format == "WebM") {
    std::wstring video_filter = L"scale=trunc(iw/2)*2:trunc(ih/2)*2";
    std::wstring audio_filter;
    if (operation == "reverse") {
      video_filter = L"reverse," + video_filter;
      audio_filter = L"areverse";
    } else if (operation == "rotate90") {
      video_filter = L"transpose=1," + video_filter;
    } else if (operation == "mirror") {
      video_filter = L"hflip," + video_filter;
    } else if (operation == "speed2x") {
      video_filter = L"setpts=0.5*PTS," + video_filter;
      audio_filter = L"atempo=2.0";
    } else if (operation == "crop16x9") {
      // Keep the crop deterministic and centered. The expression is fixed so
      // the Web layer cannot smuggle arbitrary filter syntax into the native
      // process.
      video_filter = L"crop=trunc(iw*0.8/2)*2:trunc(ih*0.8/2)*2," + video_filter;
    } else if (operation == "trim10") {
      add({L"-t", L"10"});
    } else if (operation == "fade") {
      // A bounded transition works for the bundled short clip and ordinary
      // user clips without requiring a second input or a font installation.
      video_filter = L"fade=t=in:st=0:d=0.5," + video_filter;
    } else if (operation == "grayscale") {
      video_filter = L"hue=s=0," + video_filter;
    } else if (operation == "watermark") {
      // A translucent native badge is reliable across FFmpeg builds and does
      // not depend on a machine-specific font installation.
      video_filter = L"drawbox=x=iw-180:y=ih-54:w=164:h=38:color=white@0.22:t=fill," + video_filter;
    }
    add({L"-map", L"0:v:0", L"-map", L"0:a:0?", L"-vf", video_filter, L"-pix_fmt", L"yuv420p"});
    if (operation == "denoise") audio_filter = L"afftdn=nf=-25";
    else if (operation == "normalize") audio_filter = L"loudnorm=I=-16:TP=-1.5:LRA=11";
    if (!audio_filter.empty()) add({L"-af", audio_filter});
    if (format == "WebM") add({L"-c:v", L"libvpx-vp9", L"-b:v", L"0", L"-crf", std::to_wstring(50 - quality * 35 / 100), L"-c:a", L"libopus"});
    else add({L"-c:v", L"libx264", L"-preset", L"veryfast", L"-crf", std::to_wstring(40 - quality * 25 / 100), L"-c:a", L"aac", L"-b:a", L"160k", L"-movflags", L"+faststart"});
  } else if (format == "MP3" || format == "WAV") {
    add({L"-map", L"0:a:0", L"-vn"});
    if (operation == "reverse") add({L"-af", L"areverse"});
    else if (operation == "speed2x") add({L"-af", L"atempo=2.0"});
    else if (operation == "trim10") add({L"-t", L"10"});
    else if (operation == "denoise") add({L"-af", L"afftdn=nf=-25"});
    else if (operation == "normalize") add({L"-af", L"loudnorm=I=-16:TP=-1.5:LRA=11"});
    if (format == "MP3") add({L"-c:a", L"libmp3lame", L"-b:a", std::to_wstring(64 + quality * 256 / 100) + L"k"});
    else add({L"-c:a", L"pcm_s16le"});
  } else if (format == "GIF") {
    add({L"-map", L"0:v:0", L"-an", L"-t", L"10", L"-vf", L"fps=12,scale=480:-1:flags=lanczos", L"-loop", L"0"});
  } else {
    add({L"-map", L"0:v:0", L"-an", L"-frames:v", L"1"});
    if (operation == "rotate90") add({L"-vf", L"transpose=1"});
    else if (operation == "mirror") add({L"-vf", L"hflip"});
    else if (operation == "crop16x9") add({L"-vf", L"crop=trunc(iw*0.8/2)*2:trunc(ih*0.8/2)*2"});
    else if (operation == "grayscale") add({L"-vf", L"hue=s=0"});
    else if (operation == "watermark") add({L"-vf", L"drawbox=x=iw-180:y=ih-54:w=164:h=38:color=white@0.22:t=fill"});
    if (format == "WebP") add({L"-c:v", L"libwebp", L"-quality", std::to_wstring(quality)});
    else add({L"-c:v", L"png"});
  }
  add({L"-threads", L"2", L"-fs", L"1073741824", output.wstring()});
  std::wstring command;
  for (const auto& arg : args) { if (!command.empty()) command += L' '; command += quote(arg); }
  SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE read_raw{}, write_raw{};
  if (!CreatePipe(&read_raw, &write_raw, &security, 0)) return {false, false, "无法创建编码进度管道。"};
  handle pipe_read(read_raw), pipe_write(write_raw);
  if (!SetHandleInformation(pipe_read.get(), HANDLE_FLAG_INHERIT, 0)) return {false, false, "无法隔离进程句柄。"};
  handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr));
  HANDLE inherited[]{pipe_write.get(), input.get()};
  attributes attribute_list;
  if (input.get() == INVALID_HANDLE_VALUE || !attribute_list.handles(inherited, 2)) return {false, false, "无法配置编码进程。"};
  handle job(CreateJobObjectW(nullptr, nullptr));
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
  limits.BasicLimitInformation.ActiveProcessLimit = 1;
  if (!job.get() || !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) return {false, false, "无法建立编码任务隔离。"};
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = input.get();
  startup.StartupInfo.hStdOutput = pipe_write.get();
  startup.StartupInfo.hStdError = pipe_write.get();
  startup.lpAttributeList = attribute_list.get();
  PROCESS_INFORMATION process_info{};
  if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
      CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | BELOW_NORMAL_PRIORITY_CLASS,
      nullptr, output.parent_path().c_str(), &startup.StartupInfo, &process_info)) {
    return {false, false, "无法启动 FFmpeg，请检查配置路径和系统兼容性。"};
  }
  handle process(process_info.hProcess), thread(process_info.hThread);
  if (!AssignProcessToJobObject(job.get(), process.get()) || ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
    TerminateProcess(process.get(), 1);
    WaitForSingleObject(process.get(), 5000);
    return {false, false, "系统无法托管编码进程；已安全停止。"};
  }
  const auto started = std::chrono::steady_clock::now();
  double duration_seconds = 0;
  unsigned last_progress = 0;
  std::string pending;
  for (;;) {
    if (cancel.load() || std::chrono::steady_clock::now() - started > std::chrono::minutes(30)) {
      TerminateJobObject(job.get(), 1);
      WaitForSingleObject(process.get(), 5000);
      return {false, cancel.load(), cancel.load() ? "任务已取消，未完成输出已删除。" : "超过 30 分钟任务上限，已停止。"};
    }
    DWORD available = 0;
    if (PeekNamedPipe(pipe_read.get(), nullptr, 0, nullptr, &available, nullptr) && available) {
      char buffer[4096]; DWORD count = 0;
      if (ReadFile(pipe_read.get(), buffer, std::min<DWORD>(available, sizeof(buffer)), &count, nullptr)) pending.append(buffer, count);
      std::size_t newline;
      while ((newline = pending.find('\n')) != std::string::npos) {
        const auto line = pending.substr(0, newline); pending.erase(0, newline + 1);
        const auto duration = line.find("Duration: ");
        if (duration != std::string::npos) {
          int hours = 0, minutes = 0; double seconds = 0;
          if (std::sscanf(line.c_str() + duration + 10, "%d:%d:%lf", &hours, &minutes, &seconds) == 3) duration_seconds = hours * 3600.0 + minutes * 60.0 + seconds;
          if (format == "GIF") duration_seconds = std::min(duration_seconds, 10.0);
          if (operation == "trim10") duration_seconds = std::min(duration_seconds, 10.0);
        }
        if (line.compare(0, 12, "out_time_us=") == 0 && duration_seconds > 0) {
          try {
            const auto percent = static_cast<unsigned>(std::clamp(std::stod(line.substr(12)) / (duration_seconds * 10000.0), 0.0, 99.0));
            if (percent > last_progress) { last_progress = percent; progress(percent); }
          } catch (const std::exception&) { /* Ignore malformed engine telemetry. */ }
        }
      }
      if (pending.size() > 8192) pending.erase(0, pending.size() - 8192);
      continue;
    }
    if (WaitForSingleObject(process.get(), 50) == WAIT_OBJECT_0) break;
  }
  DWORD exit_code = 1; GetExitCodeProcess(process.get(), &exit_code);
  std::error_code error;
  const auto bytes = std::filesystem::file_size(output, error);
  if (exit_code != 0 || error || bytes == 0 || bytes >= 1073741824ULL) return {false, false,
      "编码失败（退出码 " + std::to_string(exit_code) + "）。请检查文件、所需音视频轨道、编码器及 1 GiB 输出上限。"};
  return {true, false, "真实媒体处理完成，可另存为。"};
#else
  return {false, false, "媒体编码仅支持 Windows 宿主。"};
#endif
}
}
