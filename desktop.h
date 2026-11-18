// GPU capture of the primary Windows output. Resources live for one clip.
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <vector>
struct Desktop {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<IDXGIOutputDuplication> duplication;
  ComPtr<ID3D11VideoDevice> video;
  ComPtr<ID3D11VideoContext> processorContext;
  ComPtr<ID3D11VideoProcessorEnumerator> formats;
  ComPtr<ID3D11VideoProcessor> processor;
  ComPtr<ID3D11Texture2D> source, last;
  ComPtr<ID3D11VideoProcessorInputView> sourceView;
  ComPtr<IMFDXGIDeviceManager> manager;
  UINT w = 0, h = 0;
  DXGI_RATIONAL rate = {};
  HRESULT open() {
    ComPtr<IDXGIFactory1> factory;
    CHECK(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    HMONITOR primary = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    for (UINT a = 0;; a++) {
      ComPtr<IDXGIAdapter1> adapter;
      HRESULT hr = factory->EnumAdapters1(a, &adapter);
      CHECK(hr);
      for (UINT o = 0;; o++) {
        ComPtr<IDXGIOutput> output;
        hr = adapter->EnumOutputs(o, &output);
        if (hr == DXGI_ERROR_NOT_FOUND)
          break;
        CHECK(hr);
        DXGI_OUTPUT_DESC info;
        CHECK(output->GetDesc(&info));
        if (info.Monitor != primary)
          continue;
        CHECK(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                D3D11_CREATE_DEVICE_VIDEO_SUPPORT |
                                    D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                nullptr, 0, D3D11_SDK_VERSION, &device, nullptr,
                                &context));
        ComPtr<ID3D10Multithread> locking;
        if (FAILED(device.As(&locking)))
          CHECK(context.As(&locking));
        locking->SetMultithreadProtected(TRUE);
        ComPtr<IDXGIOutput1> output1;
        CHECK(output.As(&output1));
        CHECK(output1->DuplicateOutput(device.Get(), &duplication));
        DXGI_OUTDUPL_DESC d;
        duplication->GetDesc(&d);
        rate = d.ModeDesc.RefreshRate;
        // The display configuration also carries fractional refresh rates.
        UINT paths = 0, modes = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &paths,
                                        &modes) == ERROR_SUCCESS) {
          std::vector<DISPLAYCONFIG_PATH_INFO> p(paths);
          std::vector<DISPLAYCONFIG_MODE_INFO> m(modes);
          if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &paths, p.data(),
                                 &modes, m.data(), nullptr) == ERROR_SUCCESS)
            for (UINT i = 0; i < paths; i++) {
              DISPLAYCONFIG_SOURCE_DEVICE_NAME name = {};
              name.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,
                             sizeof(name), p[i].sourceInfo.adapterId,
                             p[i].sourceInfo.id};
              if (DisplayConfigGetDeviceInfo(&name.header) == ERROR_SUCCESS &&
                  !lstrcmpiW(name.viewGdiDeviceName, info.DeviceName)) {
                auto r = p[i].targetInfo.refreshRate;
                if (r.Numerator && r.Denominator)
                  rate = {r.Numerator, r.Denominator};
                break;
              }
            }
        }
        if (!rate.Numerator || !rate.Denominator) {
          DEVMODEW mode = {};
          mode.dmSize = sizeof(mode);
          if (!EnumDisplaySettingsW(info.DeviceName, ENUM_CURRENT_SETTINGS,
                                    &mode) ||
              mode.dmDisplayFrequency < 2)
            return E_FAIL;
          rate = {mode.dmDisplayFrequency, 1};
        }
        w = info.DesktopCoordinates.right - info.DesktopCoordinates.left;
        h = info.DesktopCoordinates.bottom - info.DesktopCoordinates.top;
        if ((w | h) & 1)
          return MF_E_INVALIDMEDIATYPE;
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc = {};
        desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        desc.InputWidth = d.ModeDesc.Width;
        desc.InputHeight = d.ModeDesc.Height;
        desc.OutputWidth = w;
        desc.OutputHeight = h;
        desc.InputFrameRate = desc.OutputFrameRate = rate;
        desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        CHECK(device.As(&video));
        CHECK(context.As(&processorContext));
        CHECK(video->CreateVideoProcessorEnumerator(&desc, &formats));
        CHECK(video->CreateVideoProcessor(formats.Get(), 0, &processor));
        if (d.Rotation > DXGI_MODE_ROTATION_IDENTITY) {
          ComPtr<ID3D11VideoContext1> v1;
          CHECK(processorContext.As(&v1));
          D3D11_VIDEO_PROCESSOR_CAPS caps = {};
          CHECK(formats->GetVideoProcessorCaps(&caps));
          if (!(caps.FeatureCaps & D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_ROTATION))
            return E_NOTIMPL;
          v1->VideoProcessorSetStreamRotation(
              processor.Get(), 0, TRUE,
              (D3D11_VIDEO_PROCESSOR_ROTATION)(d.Rotation -
                                               DXGI_MODE_ROTATION_IDENTITY));
        }
        processorContext->VideoProcessorSetStreamFrameFormat(
            processor.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE rgb = {}, yuv = {};
        rgb.YCbCr_Matrix = yuv.YCbCr_Matrix = 1;
        rgb.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
        yuv.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
        processorContext->VideoProcessorSetStreamColorSpace(processor.Get(), 0,
                                                            &rgb);
        processorContext->VideoProcessorSetOutputColorSpace(processor.Get(),
                                                            &yuv);
        UINT token;
        CHECK(MFCreateDXGIDeviceManager(&token, &manager));
        return manager->ResetDevice(device.Get(), token);
      }
    }
  }
  HRESULT update(DWORD timeout) {
    DXGI_OUTDUPL_FRAME_INFO info = {};
    ComPtr<IDXGIResource> resource;
    HRESULT hr = duplication->AcquireNextFrame(timeout, &info, &resource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT)
      return S_FALSE;
    CHECK(hr);
    struct Release {
      IDXGIOutputDuplication *d;
      ~Release() { d->ReleaseFrame(); }
    } release{duplication.Get()};
    if (last && !info.LastPresentTime.QuadPart)
      return S_OK;
    ComPtr<ID3D11Texture2D> tex;
    CHECK(resource.As(&tex));
    if (!source) {
      D3D11_TEXTURE2D_DESC desc;
      tex->GetDesc(&desc);
      desc.BindFlags = D3D11_BIND_RENDER_TARGET;
      desc.MiscFlags = 0;
      CHECK(device->CreateTexture2D(&desc, nullptr, &source));
      D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC v = {};
      v.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
      CHECK(video->CreateVideoProcessorInputView(source.Get(), formats.Get(),
                                                 &v, &sourceView));
    }
    context->CopyResource(source.Get(), tex.Get());
    D3D11_TEXTURE2D_DESC dest = {};
    dest.Width = w;
    dest.Height = h;
    dest.Format = DXGI_FORMAT_NV12;
    dest.SampleDesc.Count = dest.ArraySize = dest.MipLevels = 1;
    dest.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> next;
    CHECK(device->CreateTexture2D(&dest, nullptr, &next));
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC v = {};
    v.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11VideoProcessorOutputView> view;
    CHECK(video->CreateVideoProcessorOutputView(next.Get(), formats.Get(), &v,
                                                &view));
    D3D11_VIDEO_PROCESSOR_STREAM stream = {};
    stream.Enable = TRUE;
    stream.pInputSurface = sourceView.Get();
    CHECK(processorContext->VideoProcessorBlt(processor.Get(), view.Get(), 0, 1,
                                              &stream));
    last = next;
    return S_OK;
  }
  HRESULT submit(IMFSinkWriter *writer, DWORD index, LONGLONG t,
                 LONGLONG duration) {
    ComPtr<IMFMediaBuffer> b;
    ComPtr<IMFSample> s;
    CHECK(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), last.Get(), 0,
                                    FALSE, &b));
    DWORD length;
    ComPtr<IMF2DBuffer> layout;
    CHECK(b.As(&layout));
    CHECK(layout->GetContiguousLength(&length));
    CHECK(b->SetCurrentLength(length));
    CHECK(MFCreateSample(&s));
    CHECK(s->AddBuffer(b.Get()));
    CHECK(s->SetSampleTime(t));
    CHECK(s->SetSampleDuration(duration));
    return writer->WriteSample(index, s.Get());
  }
};
