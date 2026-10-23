#include "../src/dll/NativeFovBlit.h"
#include <wrl/client.h>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace preyvr;using namespace preyvr::dll;using Microsoft::WRL::ComPtr;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main(){try{
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> c;
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&c)));
 NativeFovBlit blit;
 for(const auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB}){
  constexpr unsigned w=64,h=64;
  D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.ArraySize=d.MipLevels=1;d.SampleDesc.Count=1;
  d.Format=format;d.BindFlags=D3D11_BIND_RENDER_TARGET;
  std::vector<std::uint32_t> pixels(w*h);for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)
    pixels[y*w+x]=0xff400000u|(y*4<<8)|(x*4);
  D3D11_SUBRESOURCE_DATA data{pixels.data(),w*4,0};ComPtr<ID3D11Texture2D> source;
  CHECK(SUCCEEDED(device->CreateTexture2D(&d,&data,&source)));
  // Install hostile game state; ExecuteCommandList(TRUE) must restore it.
  ComPtr<ID3D11RenderTargetView> gameTarget;CHECK(SUCCEEDED(device->CreateRenderTargetView(source.Get(),nullptr,&gameTarget)));
  auto target=gameTarget.Get();c->OMSetRenderTargets(1,&target,nullptr);
  c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
  D3D11_VIEWPORT viewport{3,7,31,29,.2f,.8f};c->RSSetViewports(1,&viewport);
  const xr::Tangents rendered{-1,1,1,-1},native{-1.5f,1.25f,1.4f,-1.3f};
  auto image=blit.Remap(c.Get(),source.Get(),rendered,native,w,h);CHECK(image);
  ComPtr<ID3D11RenderTargetView> after;c->OMGetRenderTargets(1,&after,nullptr);CHECK(after.Get()==gameTarget.Get());
  D3D11_PRIMITIVE_TOPOLOGY topology{};c->IAGetPrimitiveTopology(&topology);CHECK(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
  UINT count=1;D3D11_VIEWPORT afterViewport{};c->RSGetViewports(&count,&afterViewport);
  CHECK(afterViewport.TopLeftX==3&&afterViewport.MinDepth==.2f);
  D3D11_TEXTURE2D_DESC out{};image->GetDesc(&out);out.BindFlags=0;out.Usage=D3D11_USAGE_STAGING;out.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;CHECK(SUCCEEDED(device->CreateTexture2D(&out,nullptr,&staging)));
  c->CopyResource(staging.Get(),image);D3D11_MAPPED_SUBRESOURCE map{};CHECK(SUCCEEDED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)));
  auto pixel=[&](unsigned x,unsigned y){return reinterpret_cast<const std::uint32_t*>(static_cast<const char*>(map.pData)+y*map.RowPitch)[x];};
  CHECK(pixel(0,0)==0xff000000u);CHECK(pixel(63,63)==0xff000000u);
  // Independent tangent reconstruction: inspect interior pixels against the
  // source's coordinate ramp. sRGB and BGRA must keep the same encoded bytes.
  for(unsigned y:{20u,32u,42u})for(unsigned x:{20u,32u,42u}){
   const float tx=native.left+(x+.5f)/w*(native.right-native.left);
   const float ty=native.up-(y+.5f)/h*(native.up-native.down);
   const float sx=(tx+1)*.5f*w-.5f,sy=(1-ty)*.5f*h-.5f;
   const auto p=pixel(x,y);CHECK(std::fabs(static_cast<float>(p&255)-sx*4)<=1);
   CHECK(std::fabs(static_cast<float>((p>>8)&255)-sy*4)<=1);CHECK((p>>16)==0xff40);
  }
  c->Unmap(staging.Get(),0);
  CHECK(!blit.Remap(c.Get(),source.Get(),{1,-1,1,-1},native,w,h));
  c->OMSetRenderTargets(0,nullptr,nullptr);
 }
 std::cout<<"native FOV WARP: narrow view angular mapping, black coverage, RGBA/BGRA linear/sRGB bytes and pipeline restoration PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
