#include <algorithm>
#include <d3d10.h>
#include <mferror.h>
using std::max;
using std::min;
#include <audioclient.h>
#include <math.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmdeviceapi.h>
#include <shlobj.h>
#include <strsafe.h>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#define CHECK(expr)                                                            \
  do {                                                                         \
    HRESULT hr_ = (expr);                                                      \
    if (FAILED(hr_))                                                           \
      return hr_;                                                              \
  } while (0)
#include "desktop.h"
static HRESULT sample(IMFSinkWriter *writer, DWORD stream, const void *data,
                      DWORD bytes, LONGLONG t, LONGLONG duration) {
  ComPtr<IMFMediaBuffer> b;
  ComPtr<IMFSample> s;
  BYTE *dst;
  CHECK(MFCreateMemoryBuffer(bytes, &b));
  CHECK(b->Lock(&dst, nullptr, nullptr));
  if (data)
    memcpy(dst, data, bytes);
  else
    memset(dst, 0, bytes);
  CHECK(b->Unlock());
  CHECK(b->SetCurrentLength(bytes));
  CHECK(MFCreateSample(&s));
  CHECK(s->AddBuffer(b.Get()));
  CHECK(s->SetSampleTime(t));
  CHECK(s->SetSampleDuration(duration));
  return writer->WriteSample(stream, s.Get());
}
static HRESULT videoType(ComPtr<IMFMediaType> &t, const GUID &sub, int w, int h,
                         DXGI_RATIONAL rate) {
  CHECK(MFCreateMediaType(&t));
  CHECK(t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
  CHECK(t->SetGUID(MF_MT_SUBTYPE, sub));
  CHECK(MFSetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, w, h));
  CHECK(MFSetAttributeRatio(t.Get(), MF_MT_FRAME_RATE, rate.Numerator,
                            rate.Denominator));
  CHECK(MFSetAttributeRatio(t.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
  return t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
}
struct Audio {
  ComPtr<IAudioClient> client;
  ComPtr<IAudioCaptureClient> capture;
  DWORD stream = 0;
  LONGLONG next = 0;
  HRESULT open() {
    ComPtr<IMMDeviceEnumerator> e;
    ComPtr<IMMDevice> d;
    CHECK(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                           IID_PPV_ARGS(&e)));
    CHECK(e->GetDefaultAudioEndpoint(eRender, eConsole, &d));
    CHECK(d->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                      (void **)client.GetAddressOf()));
    WAVEFORMATEX fmt = {WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0};
    CHECK(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                             AUDCLNT_STREAMFLAGS_LOOPBACK |
                                 AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                 AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                             1000000, 0, &fmt, nullptr));
    return client->GetService(IID_PPV_ARGS(&capture));
  }
  HRESULT setup(IMFSinkWriter *writer) {
    ComPtr<IMFMediaType> out, in;
    CHECK(MFCreateMediaType(&out));
    CHECK(out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
    CHECK(out->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC));
    CHECK(out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2));
    CHECK(out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000));
    CHECK(out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
    CHECK(out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000));
    CHECK(writer->AddStream(out.Get(), &stream));
    CHECK(MFCreateMediaType(&in));
    WAVEFORMATEX fmt = {WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0};
    CHECK(MFInitMediaTypeFromWaveFormatEx(in.Get(), &fmt, sizeof(fmt)));
    return writer->SetInputMediaType(stream, in.Get(), nullptr);
  }
  HRESULT write(IMFSinkWriter *writer, const BYTE *p, UINT32 n) {
    auto t = next * 10000000 / 48000;
    next += n;
    return sample(writer, stream, p, n * 4, t, next * 10000000 / 48000 - t);
  }
  HRESULT fill(IMFSinkWriter *writer, LONGLONG until) {
    while (next < until) {
      CHECK(write(writer, nullptr, (UINT32)min(until - next, 4800LL)));
    }
    return S_OK;
  }
  HRESULT drain(IMFSinkWriter *writer, LONGLONG epoch, LONGLONG now) {
    UINT32 n;
    CHECK(capture->GetNextPacketSize(&n));
    while (n) {
      BYTE *p;
      DWORD flags;
      UINT64 stamp;
      CHECK(capture->GetBuffer(&p, &n, &flags, nullptr, &stamp));
      LONGLONG start = (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)
                           ? next
                           : ((LONGLONG)stamp - epoch) * 48000 / 10000000;
      HRESULT hr = fill(writer, start);
      UINT32 skip = (UINT32)min((LONGLONG)n, max(0LL, next - start));
      if (SUCCEEDED(hr) && skip < n)
        hr =
            write(writer,
                  (flags & AUDCLNT_BUFFERFLAGS_SILENT) ? nullptr : p + 4 * skip,
                  n - skip);
      HRESULT released = capture->ReleaseBuffer(n);
      CHECK(hr);
      CHECK(released);
      CHECK(capture->GetNextPacketSize(&n));
    }
    return fill(writer, max(0LL, (now - 2000000) * 48000 / 10000000));
  }
};
static LONGLONG clock100ns() {
  LARGE_INTEGER f, c;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&c);
  return c.QuadPart / f.QuadPart * 10000000 +
         c.QuadPart % f.QuadPart * 10000000 / f.QuadPart;
}
static HRESULT record(HWND window, HANDLE stop, const wchar_t *file,
                      const wchar_t **stage) {
  *stage = L"opening primary display";
  Desktop screen;
  CHECK(screen.open());
  Audio audio;
  bool sound = SUCCEEDED(audio.open());
  ComPtr<IMFSinkWriter> writer;
  *stage = L"opening MP4";
  ComPtr<IMFAttributes> attrs;
  CHECK(MFCreateAttributes(&attrs, 2));
  CHECK(attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));
  CHECK(attrs->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, screen.manager.Get()));
  CHECK(MFCreateSinkWriterFromURL(file, nullptr, attrs.Get(), &writer));
  *stage = L"setting up H.264";
  DWORD video;
  ComPtr<IMFMediaType> out, in;
  CHECK(videoType(out, MFVideoFormat_H264, screen.w, screen.h, screen.rate));
  CHECK(out->SetUINT32(
      MF_MT_AVG_BITRATE,
      (UINT32)std::clamp(screen.w * (double)screen.h * screen.rate.Numerator /
                             screen.rate.Denominator * 0.22,
                         8000000., 180000000.)));
  CHECK(writer->AddStream(out.Get(), &video));
  CHECK(videoType(in, MFVideoFormat_NV12, screen.w, screen.h, screen.rate));

  CHECK(writer->SetInputMediaType(video, in.Get(), nullptr));
  if (sound) {
    *stage = L"setting up game audio";
    CHECK(audio.setup(writer.Get()));
  }
  *stage = L"starting encoder";
  CHECK(writer->BeginWriting());
  *stage = L"receiving the first desktop frame";
  ULONGLONG limit = GetTickCount64() + 5000;
  while (!screen.last) {
    CHECK(screen.update(40));
    if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0)
      return HRESULT_FROM_WIN32(ERROR_CANCELLED);
    if (GetTickCount64() > limit)
      return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
  }
  struct Timer {
    HANDLE h = CreateWaitableTimerExW(nullptr, nullptr, 2, TIMER_ALL_ACCESS);
    ~Timer() {
      if (h)
        CloseHandle(h);
    }
  } timer;
  if (!timer.h)
    timer.h = CreateWaitableTimerW(nullptr, FALSE, nullptr);
  if (!timer.h)
    return HRESULT_FROM_WIN32(GetLastError());
  HANDLE waits[] = {stop, timer.h};
  if (sound)
    CHECK(audio.client->Start());
  LONGLONG epoch = clock100ns(), tick = 0;
  const LONGLONG units = 10000000LL * screen.rate.Denominator,
                 hz = screen.rate.Numerator;
  auto timestamp = [&](LONGLONG frame) {
    return (LONGLONG)(frame * (double)units / hz);
  };
  *stage = L"capturing screen";
  do {
    LONGLONG t = timestamp(tick), end = timestamp(tick + 1);
    if (tick)
      CHECK(screen.update(0));
    CHECK(screen.submit(writer.Get(), video, t, end - t));
    if (!tick)
      PostMessageW(window, WM_APP + 1, sound, 0);
    if (sound)
      CHECK(audio.drain(writer.Get(), epoch, clock100ns() - epoch));
    LONGLONG elapsed = clock100ns() - epoch;
    tick = max(tick + 1, (LONGLONG)(elapsed * (double)hz / units));
    LARGE_INTEGER due;
    due.QuadPart = -max(1LL, timestamp(tick) - elapsed);
    if (!SetWaitableTimer(timer.h, &due, 0, nullptr, nullptr, FALSE))
      return HRESULT_FROM_WIN32(GetLastError());
    DWORD event = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
    if (event == WAIT_OBJECT_0)
      break;
    if (event == WAIT_FAILED)
      return HRESULT_FROM_WIN32(GetLastError());
  } while (true);
  *stage = L"saving MP4";
  if (sound) {
    CHECK(audio.drain(writer.Get(), epoch, clock100ns() - epoch));
    CHECK(audio.fill(writer.Get(), (clock100ns() - epoch) * 48000 / 10000000));
    audio.client->Stop();
  }
  return writer->Finalize();
}
extern "C" HRESULT captureClip(HWND window, HANDLE stop, wchar_t *path,
                               size_t capacity, const wchar_t **stage) {
  *stage = L"initializing Windows capture";
  CHECK(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
  HRESULT hr = MFStartup(MF_VERSION);
  if (SUCCEEDED(hr)) {
    PWSTR dir = nullptr;
    *stage = L"opening Videos folder";
    hr = SHGetKnownFolderPath(FOLDERID_Videos, KF_FLAG_CREATE, nullptr, &dir);
    wchar_t folder[MAX_PATH];
    if (SUCCEEDED(hr))
      hr = StringCchPrintfW(folder, MAX_PATH, L"%ls\\PadClip", dir);
    CoTaskMemFree(dir);
    if (SUCCEEDED(hr) && !CreateDirectoryW(folder, nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
      hr = HRESULT_FROM_WIN32(GetLastError());
    SYSTEMTIME s;
    GetLocalTime(&s);
    if (SUCCEEDED(hr))
      hr = StringCchPrintfW(path, capacity,
                            L"%ls\\Clip-%04d%02d%02d-%02d%02d%02d-%03d.mp4",
                            folder, s.wYear, s.wMonth, s.wDay, s.wHour,
                            s.wMinute, s.wSecond, s.wMilliseconds);
    if (SUCCEEDED(hr))
      hr = record(window, stop, path, stage);
    MFShutdown();
  }
  CoUninitialize();
  return hr;
}
