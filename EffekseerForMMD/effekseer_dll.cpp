﻿#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <unordered_map>
#include <experimental/filesystem>
namespace filesystem = std::experimental::filesystem;
#include <effekseer/include/Effekseer.h>
#include <effekseer/include/EffekseerRendererDX9.h>

#include "effekseer_dll.h"

#ifndef NDEBUG
# pragma comment(lib,"effekseer/lib/VS2015/x64/EffekseerRendererDX9.Debug.lib")
# pragma comment(lib,"effekseer/lib/VS2015/x64/Effekseer.Debug.lib")
#else
# pragma comment(lib,"effekseer/lib/VS2015/x64/EffekseerRendererDX9.Release.lib")
# pragma comment(lib,"effekseer/lib/VS2015/x64/Effekseer.Release.lib")

# define printf(...) (void)0
# define puts(...) (void)0
# define _putws(...) (void)0
#endif // NDEBUG

#include <mmd/MMDExport.h>
#include "hook_api.h"

namespace efk
{
  namespace
  {
    constexpr auto eps = 1e-7f;

    Effekseer::Matrix44 toMatrix4x4(const D3DMATRIX& mat)
    {
      Effekseer::Matrix44 ans;
      for ( int i = 0; i < 4; i++ )
      {
        for ( int j = 0; j < 4; j++ )
        {
          ans.Values[i][j] = mat.m[i][j];
        }
      }
      return ans;
    }

    Effekseer::Matrix43 toMatrix4x3(const D3DMATRIX& mat)
    {
      Effekseer::Matrix43 ans;
      for ( int i = 0; i < 4; i++ )
      {
        for ( int j = 0; j < 3; j++ )
        {
          ans.Value[i][j] = mat.m[i][j];
        }
      }
      return ans;
    }
  }

  PMDResource::PMDResource(int i)
  {
    if ( i == -1 ) return;

    // モーフのIDを取得
    morph_id_.fill(0);
    for ( int j = 0, len = ExpGetPmdMorphNum(i); j < len; ++j )
    {
      const char* name = ExpGetPmdMorphName(i, j);
      for ( int k = 0; k < morph_resource_size; ++k )
      {
        if ( strcmp(getName(static_cast<MorphKind>(k)), name) == 0 )
        {
          morph_id_[k] = j;
          break;
        }
      }
    }
    bone_id_.fill(-1);
    for ( int j = 0, len = ExpGetPmdBoneNum(i); j < len; ++j )
    {
      const char* name = ExpGetPmdBoneName(i, j);
      for ( int k = 0; k < bone_resource_size; ++k )
      {
        if ( strcmp(getName(static_cast<BoneKind>(k)), name) == 0 )
        {
          bone_id_[k] = j;
          break;
        }
      }
    }
  }

  float PMDResource::triggerVal(int i) const { return getMorph(i, MorphKind::trigger_morph); }

  float PMDResource::autoPlayVal(int i) const { return getMorph(i, MorphKind::auto_play_morph); }

  float PMDResource::frameVal(int i) const { return getMorph(i, MorphKind::frame_morph); }

  float PMDResource::loopVal(int i) const { return getMorph(i, MorphKind::loop_morph); }

  float PMDResource::triggerEraseVal(int i) const { return getMorph(i, MorphKind::trigger_erase_morph); }

  float PMDResource::scaleUpVal(int i) const { return getMorph(i, MorphKind::scale_up_morph); }

  float PMDResource::scaleDownVal(int i) const { return getMorph(i, MorphKind::scale_down_morph); }

  float PMDResource::speedUpVal(int i) const { return getMorph(i, MorphKind::speed_up_morph); }

  float PMDResource::speedDownVal(int i) const { return getMorph(i, MorphKind::speed_down_morph); }

  float PMDResource::effectTestVal(int i) const { return getMorph(i, MorphKind::at_effect_test_morph); }

  float PMDResource::stopRootVal(int i) const { return getMorph(i, MorphKind::stop_root_morph); }


  D3DMATRIX PMDResource::playBone(int i) const { return getBone(i, BoneKind::play_bone); }

  D3DMATRIX PMDResource::centerBone(int i) const { return getBone(i, BoneKind::center_bone); }

  D3DMATRIX PMDResource::baseBone(int i) const { return getBone(i, BoneKind::base_bone); }




  void TriggerTypeEffect::draw() const
  {
    for ( auto& i : effects_ )
    {
      manager_->DrawHandle(i);
    }
  }

  void TriggerTypeEffect::push()
  {
    const auto handle = manager_->Play(effect_, 0.0f, 0.0f, 0.0f);
    new_effect_handle_.push_back(handle);
    manager_->Flip();
  }

  void TriggerTypeEffect::update(int i)
  {
    const auto is_trigger = std::abs(resource_->triggerVal(i) - 1.0f) <= eps;
    if ( is_trigger )
    {
      // クロックの立ち上がりもしくは0フレーム目のときに生成する
      if ( pre_triggerd_ == false || std::abs(ExpGetFrameTime() - 0.0f) <= eps && effects_.size() == 0 )
      {
        push();
      }
    }

    pre_triggerd_ = is_trigger;

    const int delta_frame = delta_time_->get();
    // 前のフレームに戻っている場合または、トリガー削除が1の場合は既存の再生ハンドルをすべて削除
    if ( delta_frame < 0 || resource_->triggerEraseVal(i) >= 1.0f - eps )
    {
      for ( auto& handle : effects_ )
      {
        manager_->StopEffect(handle);
      }
      effects_.clear();
    }

    // StopRootのときはすべてを止める
    if ( std::abs(resource_->stopRootVal(i) - 1.0f) <= eps )
    {
      for ( auto& handle : new_effect_handle_ )
      {
        manager_->StopRoot(handle);
      }

      for ( auto& handle : effects_ )
      {
        manager_->StopRoot(handle);
      }
    }

    // 再生が終了したものを削除
    auto e = std::remove_if(effects_.begin(), effects_.end(), [this](Effekseer::Handle h)
    {
      return !manager_->Exists(h);
    });
    effects_.erase(e, effects_.end());
  }

  void TriggerTypeEffect::updateHandle(int i, const std::function<void(Effekseer::Handle, float)>& update_func_)
  {
    const int delta_frame = delta_time_->get();
    // 初回はリソースのデータを使って制御するので大本でUpdateする
    for ( auto& handle : new_effect_handle_ )
    {
      update_func_(handle, 0.0f);
      effects_.push_back(handle);
    }

    // トリガーによって作られたハンドルは制御しないのでただUpdateする
    for ( auto& handle : effects_ )
    {
      if ( delta_frame == 0 )
      {
        manager_->UpdateHandle(handle, 0.0f);
        continue;
      }

      for ( int k = 0; k < delta_frame; ++k )
      {
        manager_->UpdateHandle(handle, getSpeed(i));
      }
    }
  }


  void AutoPlayTypeEffect::ifCreate()
  {
    if ( manager_->Exists(handle_) == false )
    {
      if ( manager_->Exists(handle_) )
      {
        manager_->StopEffect(handle_);
      }

      handle_ = manager_->Play(effect_, 0.0f, 0.0f, 0.0f);
      manager_->Flip();
    }
  }

  void AutoPlayTypeEffect::update(int i)
  {

    if ( std::abs(resource_->loopVal(i) - 1.0f) <= eps )
    {
      ifCreate();
    }

    const int delta_frame = delta_time_->get();
    if ( delta_frame < 0 )
    {
      if ( manager_->Exists(handle_) )
      {
        manager_->StopEffect(handle_);
      }

      handle_ = -1;
      if ( std::abs(ExpGetFrameTime() - 0.0f) <= eps )
      {
        ifCreate();
      }
    }

    if ( handle_ == -1 )
    {
      return;
    }

    if ( std::abs(resource_->stopRootVal(i) - 1.0f) <= eps )
    {
      manager_->StopRoot(handle_);
    }

  }

  void AutoPlayTypeEffect::updateHandle(int i, const std::function<void(Effekseer::Handle, float)>&update_func_)
  {
    const auto delta_frame = delta_time_->get();
    for ( int j = 0; j < delta_frame; ++j )
    {
      update_func_(handle_, getSpeed(i));
    }
  }

  void AutoPlayTypeEffect::draw() const
  {
    if ( handle_ != -1 )
    {
      manager_->DrawHandle(handle_);
    }
  }

  void FrameTypeEffect::ifCreate()
  {
    if ( handle_ != -1 && manager_->Exists(handle_) )
    {
      return;
    }
    manager_->StopEffect(handle_);
    now_frame_ = 0;

    handle_ = manager_->Play(effect_, 0.0f, 0.0f, 0.0f);
    manager_->Flip();
  }

  void FrameTypeEffect::update(int i)
  {
    // フレーム方式
    const auto play_mat = resource_->playBone(i);
    const float new_frame = (play_mat.m[3][1] - 0.5) + resource_->frameVal(i) * 100.0f;
    if ( handle_ != -1 && now_frame_ > new_frame + eps )
    {
      manager_->StopEffect(handle_);
      handle_ = -1;
      now_frame_ = 0;
      return;
    }

    if ( std::abs(resource_->stopRootVal(i) - 1.0f) <= eps )
    {
      if ( handle_ != -1 )
      {
        manager_->StopRoot(handle_);
      }
    }
    else
    {
      ifCreate();
    }

  }

  void FrameTypeEffect::updateHandle(int i, const std::function<void(Effekseer::Handle, float)>&update_func_)
  {

#if 1
    if ( handle_ == -1 )
    {
      return;
    }

    const auto play_mat = resource_->playBone(i);
    const float new_frame = (play_mat.m[3][1] - 0.5) + resource_->frameVal(i) * 100.0f;

    const int len = std::min(static_cast<int>(1e9), static_cast<int>(new_frame - now_frame_));
    for ( int j = 0; j < len; j++ )
    {
      update_func_(handle_, 1.0f);
    }
    now_frame_ += len;
    printf("%f %f %d\n", new_frame, now_frame_, len);
    //manager_->UpdateHandle(handle_, new_frame - now_frame_ - len);
    update_func_(handle_, 0.0f);

#else

    manager_->UpdateHandle(handle_, new_frame - now_frame_);
    now_frame_ = new_frame;

#endif

  }

  void FrameTypeEffect::draw() const
  {
    if ( handle_ != -1 )
    {
      manager_->DrawHandle(handle_);
    }
  }

  MyEffect::MyEffect() : manager_(nullptr), effect_(nullptr)
    , trigger_(nullptr, nullptr, nullptr, nullptr)
    , auto_paly_(nullptr, nullptr, nullptr, nullptr)
    , frame_(nullptr, nullptr, nullptr, nullptr), effect_test_handle_(-1)
  {
  }

  MyEffect::MyEffect(Effekseer::Manager* manager, Effekseer::Effect* effect, PMDResource resource)
    : resource_(std::make_shared<PMDResource>(resource)), manager_(manager), effect_(effect)
    , delta_time_(std::make_shared<DeltaTime>())
    , trigger_(manager, effect, resource_.get(), delta_time_.get())
    , auto_paly_(manager, effect, resource_.get(), delta_time_.get())
    , frame_(manager, effect, resource_.get(), delta_time_.get())
    , effect_test_handle_(-1)
  {
  }

  MyEffect::~MyEffect() {}

  void MyEffect::setMatrix(const D3DMATRIX& center, const D3DMATRIX& base)
  {
    Effekseer::Matrix44 tmp;
    Effekseer::Matrix44::Inverse(tmp, toMatrix4x4(base));
    base_matrix_ = toMatrix4x3(base);
    Effekseer::Matrix44::Mul(tmp, toMatrix4x4(center), tmp);
    for ( int i = 0; i < 4; ++i )
    {
      for ( int j = 0; j < 3; ++j )
      {
        matrix_.Value[i][j] = tmp.Values[i][j];
      }
    }
  }

  void MyEffect::setScale(float x, float y, float z)
  {
    scale_.X = x;
    scale_.Y = y;
    scale_.Z = z;
  }

  void MyEffect::draw(int i) const
  {
    if ( effect_test_handle_ != -1 )
    {
      manager_->DrawHandle(effect_test_handle_);
    }

    if ( std::abs(resource_->autoPlayVal(i) - 1.0f) <= eps )
    {
      auto_paly_.draw();
    }
    else
    {
      frame_.draw();
    }

    trigger_.draw();
  }

  void MyEffect::OnLostDevice() const
  {
    if ( effect_ )
    {
      effect_->UnloadResources();
    }
  }

  void MyEffect::OnResetDevice() const
  {
    if ( effect_ )
    {
      effect_->ReloadResources();
    }
  }

  float MyEffect::getSpeed(int i) const { return 1.0f + resource_->speedUpVal(i) - resource_->speedDownVal(i); }

  void MyEffect::update(int i)
  {
    auto &resource = *resource_;

    // 座標の処理
    const auto center = resource.centerBone(i);
    const auto base_center = resource.baseBone(i);
    setMatrix(center, base_center);

    // 拡縮処理
    const auto scale = resource.scaleUpVal(i) * 10 - resource.scaleDownVal(i) + 1.0f;
    setScale(scale, scale, scale);


    if ( std::abs(resource.autoPlayVal(i) - 1.0f) <= eps )
    {
      auto_paly_.update(i);
    }
    else
    {
      frame_.update(i);
    }

    // トリガー方式
    trigger_.update(i);

    // エフェクトテスト
    if ( std::abs(resource.effectTestVal(i) - 1.0f) <= eps )
    {
      if ( std::abs(resource.stopRootVal(i) - 1.0f) <= eps )
      {
        manager_->StopRoot(effect_test_handle_);
      }
      else if ( manager_->Exists(effect_test_handle_) == false )
      {
        effect_test_handle_ = manager_->Play(effect_, 0.0f, 0.0f, 0.0f);
      }
    }
    else
    {
      if ( manager_->Exists(effect_test_handle_) )
      {
        manager_->StopEffect(effect_test_handle_);
      }
      effect_test_handle_ = -1;
    }

    manager_->Flip();

    auto update_func = [this](auto h, auto delta)
    {
      manager_->BeginUpdate();
      this->UpdateHandle(h, delta);
      manager_->EndUpdate();
    };

    {
      frame_.updateHandle(i, update_func);

      trigger_.updateHandle(i, update_func);

      auto_paly_.updateHandle(i, update_func);

      if ( effect_test_handle_ != -1 )
      {
        UpdateHandle(effect_test_handle_, getSpeed(i));
      }
    }

    delta_time_->update();
  }

  void MyEffect::UpdateHandle(Effekseer::Handle h, float delta_time)
  {
    manager_->SetMatrix(h, matrix_);
    manager_->SetBaseMatrix(h, base_matrix_);
    manager_->SetScale(h, scale_.X, scale_.Y, scale_.Z);
    manager_->UpdateHandle(h, delta_time);
  }

  DistortingCallback::DistortingCallback(::EffekseerRendererDX9::Renderer* renderer, LPDIRECT3DDEVICE9 device,
                                         int texWidth, int texHeight) : renderer(renderer), device(device), width_(texWidth), height_(texHeight)
  {
    OnResetDevice();
  }

  DistortingCallback::~DistortingCallback()
  {
    ES_SAFE_RELEASE(texture);
  }

  bool DistortingCallback::OnDistorting()
  {
    if ( use_distoring_ == false )
    {
      return false;
    }

    Microsoft::WRL::ComPtr<IDirect3DSurface9> texSurface;

    HRESULT hr = texture->GetSurfaceLevel(0, texSurface.ReleaseAndGetAddressOf());
    if ( FAILED(hr) )
    {
      std::wstring error = __FUNCTIONW__" error : texture->GetSurfaceLevel : " + std::to_wstring(hr);
      MessageBoxW(nullptr, error.c_str(), L"error", MB_OK);
      use_distoring_ = false;
      return false;
    }

    Microsoft::WRL::ComPtr<IDirect3DSurface9> targetSurface;
    hr = device->GetRenderTarget(0, targetSurface.ReleaseAndGetAddressOf());
    if ( FAILED(hr) )
    {
      std::wstring error = __FUNCTIONW__" error : texture->GetSurfaceLevel : " + std::to_wstring(hr);
      MessageBoxW(nullptr, error.c_str(), L"error", MB_OK);
      use_distoring_ = false;
      return false;
    }

    D3DVIEWPORT9 viewport;
    device->GetViewport(&viewport);
    const RECT rect{
      static_cast<LONG>(viewport.X),
      static_cast<LONG>(viewport.Y),
      static_cast<LONG>(viewport.Width + viewport.X),
      static_cast<LONG>(viewport.Height + viewport.Y)
    };
    hr = device->StretchRect(targetSurface.Get(), &rect, texSurface.Get(), nullptr, D3DTEXF_NONE);
    if ( FAILED(hr) )
    {
      std::wstring error = __FUNCTIONW__" error : texture->GetSurfaceLevel : " + std::to_wstring(hr);
      MessageBoxW(nullptr, error.c_str(), L"error", MB_OK);
      use_distoring_ = false;
      return false;
    }

    renderer->SetBackground(texture);

    return true;
  }

  void DistortingCallback::OnLostDevice()
  {
    ES_SAFE_RELEASE(texture);
  }

  void DistortingCallback::OnResetDevice()
  {
    OnLostDevice();
    device->CreateTexture(width_, height_, 1, D3DUSAGE_RENDERTARGET,
                          D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &texture, nullptr);
  }

  D3D9DeviceEffekserr::D3D9DeviceEffekserr(IDirect3DDevice9* device) : now_present_(false), device_(device)
  {
    device_->AddRef();
    HookAPI();
    renderer_ = ::EffekseerRendererDX9::Renderer::Create(device, 10000);

    // エフェクト管理用インスタンスの生成
    manager_ = ::Effekseer::Manager::Create(10000);

    // 描画用インスタンスから描画機能を設定
    manager_->SetSpriteRenderer(renderer_->CreateSpriteRenderer());
    manager_->SetRibbonRenderer(renderer_->CreateRibbonRenderer());
    manager_->SetRingRenderer(renderer_->CreateRingRenderer());
    manager_->SetTrackRenderer(renderer_->CreateTrackRenderer());
    manager_->SetModelRenderer(renderer_->CreateModelRenderer());

    // 描画用インスタンスからテクスチャの読込機能を設定
    // 独自拡張可能、現在はファイルから読み込んでいる。
    manager_->SetTextureLoader(renderer_->CreateTextureLoader());
    manager_->SetModelLoader(renderer_->CreateModelLoader());

    // 投影行列を設定
    renderer_->SetProjectionMatrix(
      ::Effekseer::Matrix44().PerspectiveFovRH(90.0f / 180.0f * 3.14f, 1024.0f / 768.0f, 1.0f, 500000.0f));

    SetDistorting();
  }

  D3D9DeviceEffekserr::~D3D9DeviceEffekserr()
  {
    manager_->Destroy();
    renderer_->Destroy();
    RestoreHook();
    device_->Release();
  }

  void fps()
  {
    static int t = 0, ave = 0, f[60];
    static int count = 0;
    count++;
    f[count % 60] = GetTickCount() - t;
    t = GetTickCount();
    if ( count % 60 == 59 )
    {
      ave = 0;
      for ( int i = 0; i < 60; i++ )
      {
        ave += f[i];
      }

      ave /= 60;
    }

    if ( ave != 0 )
    {
      printf("%.1fFPS \t", 1000.0 / (double)ave);
      printf("%dms", ave);
    }
  }

  void D3D9DeviceEffekserr::DrawIndexedPrimitive(D3DPRIMITIVETYPE Type, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount)
  {
    const int pmd_num = ExpGetPmdNum();
    const int technic_type = ExpGetCurrentTechnic();
    const int now_render_object = ExpGetCurrentObject();
    const int now_render_material = ExpGetCurrentMaterial();
    UpdateProjection();
    if ( D3DPT_LINELIST == Type
        || now_render_material != 0
        || now_render_object == 0
        || now_present_
        || (technic_type != 1 && technic_type != 2) )
    {
      return;
    }

    for ( int i = 0; i < pmd_num; i++ )
    {
      if ( now_render_object != ExpGetPmdOrder(i) )
      {
        continue;
      }

      UpdateCamera();
      const int ID = ExpGetPmdID(i);
      auto it = effect_.find(ID);
      if ( it != effect_.end() )
      {
        auto& effect = it->second;

        effect.update(i);

        now_present_ = true;

        // Effekseerライブラリがリストアしてくれないので自前でバックアップしてる
        float constant_data[256 * 4];
        device_->GetVertexShaderConstantF(0, constant_data, sizeof(constant_data) / sizeof(float) / 4);

        UINT stride;
        IDirect3DVertexBuffer9* stream_data;
        UINT offset;
        device_->GetStreamSource(0, &stream_data, &offset, &stride);

        IDirect3DIndexBuffer9* index_data;
        device_->GetIndices(&index_data);

        // エフェクトの描画開始処理を行う。
        if ( renderer_->BeginRendering() )
        {
          // エフェクトの描画を行う。
          effect.draw(i);

          // エフェクトの描画終了処理を行う。
          renderer_->EndRendering();
        }

        device_->SetStreamSource(0, stream_data, offset, stride);
        if ( stream_data ) stream_data->Release();

        device_->SetIndices(index_data);
        if ( index_data ) index_data->Release();

        device_->SetVertexShaderConstantF(0, constant_data, sizeof(constant_data) / sizeof(float) / 4);

        now_present_ = false;
      }
    }
  }

  void D3D9DeviceEffekserr::BeginScene(void)
  {
    int len = ExpGetPmdNum();
    if ( len == effect_.size() )
    {
      return;
    }

    for ( int i = 0; i < len; i++ )
    {
      const int id = ExpGetPmdID(i);
      const auto file_name = ExpGetPmdFilename(i);
      filesystem::path path(file_name);
      if ( ".efk" != path.extension() )
      {
        continue;
      }

      auto it = effect_.insert({ id, MyEffect() });
      if ( !it.second )
      {
        continue;
      }

      // エフェクトの読込
      hook_rewrite::nowEFKLoading = true;
      const auto eff = Effekseer::Effect::Create(manager_, reinterpret_cast<const EFK_CHAR*>((path.remove_filename() / path.stem().stem()).c_str()));
      hook_rewrite::nowEFKLoading = false;

      if ( eff == nullptr )
      {
        std::wstring error = L"Failed to read the .efk file.\nfilename: " + (path.remove_filename() / path.stem().stem()).wstring();
        MessageBoxW(nullptr, error.c_str(), L"error", MB_OK);
      }
      it.first->second = MyEffect(manager_, eff, PMDResource(i));
    }
  }

  void D3D9DeviceEffekserr::EndScene(void)
  {
    printf("%f\n", ExpGetFrameTime());
  }


  void D3D9DeviceEffekserr::UpdateCamera() const
  {
    D3DMATRIX view, world;
    device_->GetTransform(D3DTS_WORLD, &world);
    device_->GetTransform(D3DTS_VIEW, &view);
    Effekseer::Matrix44 camera, eview = toMatrix4x4(view), eworld = toMatrix4x4(world);
    Effekseer::Matrix44::Mul(camera, eworld, eview);

    renderer_->SetCameraMatrix(camera);
  }

  void D3D9DeviceEffekserr::UpdateProjection() const
  {
    D3DMATRIX projection;
    device_->GetTransform(D3DTS_PROJECTION, &projection);
    Effekseer::Matrix44 mat = toMatrix4x4(projection);
    renderer_->SetProjectionMatrix(mat);
  }


  void D3D9DeviceEffekserr::HookAPI()
  {
    hook_rewrite::Rewrite_wsopen_s();
    hook_rewrite::Rewrite_read();
    hook_rewrite::Rewrite_close();
    hook_rewrite::RewriteSetFilePointer();
    hook_rewrite::RewriteDragFinish();
    hook_rewrite::RewriteDragQueryFileW();

    // メニューバーの追加
    auto hwnd = getHWND();
    auto hmenu = GetMenu(hwnd);
    AppendMenuA(hmenu, MF_RIGHTJUSTIFY, WM_APP + 1, "Effekseer");
    DrawMenuBar(hwnd);
  }

  void D3D9DeviceEffekserr::RestoreHook()
  {
    hook_rewrite::PF_wsopen_s.reset();
    hook_rewrite::PF_read.reset();
    hook_rewrite::PF_close.reset();
    hook_rewrite::PFSetFilePointer.reset();
    hook_rewrite::PFDragFinish.reset();
    hook_rewrite::PFDragQueryFileW.reset();
  }

  void D3D9DeviceEffekserr::Reset(D3DPRESENT_PARAMETERS* pPresentationParameters)
  {
    renderer_->OnLostDevice();
    for ( auto& i : effect_ )
    {
      i.second.OnLostDevice();
    }

    if ( distorting_callback_ )
    {
      distorting_callback_->OnLostDevice();
    }
  }

  void D3D9DeviceEffekserr::PostReset(D3DPRESENT_PARAMETERS* pPresentationParameters, HRESULT& res)
  {
    if ( distorting_callback_ )
    {
      distorting_callback_->OnResetDevice();
    }

    for ( auto& i : effect_ )
    {
      i.second.OnResetDevice();
    }
    renderer_->OnResetDevice();
  }

  void D3D9DeviceEffekserr::SetDistorting()
  {
    // テクスチャサイズの取得
    Microsoft::WRL::ComPtr<IDirect3DSurface9> tex;
    if ( FAILED(device_->GetRenderTarget(0, tex.ReleaseAndGetAddressOf())) )
    {
      MessageBoxW(nullptr, L"Failed to get the render target. \nDistortion (distortion) can not be used.", L"error", MB_OK);
      return;
    }
    D3DSURFACE_DESC desc;
    if ( SUCCEEDED(tex->GetDesc(&desc)) )
    {
      distorting_callback_ = new DistortingCallback(renderer_, device_, desc.Width, desc.Height);
      renderer_->SetDistortingCallback(distorting_callback_);
    }
    else
    {
      MessageBoxW(nullptr, L"Failed to get the screen size of the render target. \nDistortion (distortion) can not be used.", L"error", MB_OK);
    }
  }
}

int version() { return 2; }

MMDPluginDLL2* create2(IDirect3DDevice9* device) { return new efk::D3D9DeviceEffekserr(device); }

void destroy2(MMDPluginDLL2* p) { delete p; }
