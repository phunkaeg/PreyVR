#include "NativeFovBlit.h"
#include <d3dcompiler.h>
#include <cstring>

namespace preyvr::dll {
namespace {
using Microsoft::WRL::ComPtr;
constexpr char shader[]=R"(
Texture2D<float4> image:register(t0);
SamplerState filter:register(s0);
cbuffer Mapping:register(b0){float2 scale;float2 offset;};
struct Vertex {float4 position:SV_Position;float2 uv:TEXCOORD0;};
Vertex vs(uint id:SV_VertexID){
    Vertex v;v.uv=float2((id<<1)&2,id&2);
    v.position=float4(v.uv*float2(2,-2)+float2(-1,1),0,1);return v;
}
float4 ps(Vertex v):SV_Target {
    float2 uv=v.uv*scale+offset;
    if(any(uv<0)||any(uv>1))return float4(0,0,0,1);
    return image.SampleLevel(filter,uv,0);
}
)";
DXGI_FORMAT RawFormat(DXGI_FORMAT f) {
    switch(f){
    case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_UNORM:case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    default:return DXGI_FORMAT_UNKNOWN;
    }
}
}
bool NativeFovBlit::Prepare(ID3D11Device* device,const D3D11_TEXTURE2D_DESC& d,unsigned w,unsigned h) {
    if(!device||!w||!h||w>16384||h>16384||!d.Width||!d.Height||d.ArraySize!=1||d.MipLevels!=1||
       d.SampleDesc.Count!=1||RawFormat(d.Format)==DXGI_FORMAT_UNKNOWN)return false;
    if(device_.Get()!=device){*this=NativeFovBlit{};device_=device;}
    if(!vertex_||!pixel_||!deferred_||!sampler_||!constants_||!raster_){
        ComPtr<ID3DBlob> vs,ps,error;
        if(FAILED(D3DCompile(shader,std::strlen(shader),"NativeFovBlit",nullptr,nullptr,"vs","vs_5_0",0,0,&vs,&error))||
           FAILED(D3DCompile(shader,std::strlen(shader),"NativeFovBlit",nullptr,nullptr,"ps","ps_5_0",0,0,&ps,&error))||
           FAILED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,vertex_.ReleaseAndGetAddressOf()))||
           FAILED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,pixel_.ReleaseAndGetAddressOf()))||
           FAILED(device->CreateDeferredContext(0,deferred_.ReleaseAndGetAddressOf())))return false;
        D3D11_BUFFER_DESC b{};b.ByteWidth=16;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
        if(FAILED(device->CreateBuffer(&b,nullptr,constants_.ReleaseAndGetAddressOf()))||
           FAILED(device->CreateSamplerState(&s,sampler_.ReleaseAndGetAddressOf()))||
           FAILED(device->CreateRasterizerState(&r,raster_.ReleaseAndGetAddressOf())))return false;
    }
    if(source_&&read_&&output_&&write_&&d.Width==description_.Width&&d.Height==description_.Height&&
       d.Format==description_.Format&&w==width_&&h==height_)return true;
    source_.Reset();read_.Reset();output_.Reset();write_.Reset();description_=d;width_=w;height_=h;
    auto input=d;input.Format=RawFormat(d.Format);input.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    input.MiscFlags=0;input.Usage=D3D11_USAGE_DEFAULT;input.CPUAccessFlags=0;
    auto output=input;output.Width=w;output.Height=h;output.BindFlags=D3D11_BIND_RENDER_TARGET;
    return SUCCEEDED(device->CreateTexture2D(&input,nullptr,&source_))&&
        SUCCEEDED(device->CreateShaderResourceView(source_.Get(),nullptr,&read_))&&
        SUCCEEDED(device->CreateTexture2D(&output,nullptr,&output_))&&
        SUCCEEDED(device->CreateRenderTargetView(output_.Get(),nullptr,&write_));
}
ID3D11Texture2D* NativeFovBlit::Remap(ID3D11DeviceContext* context,ID3D11Texture2D* source,
    const xr::Tangents& rendered,const xr::Tangents& native,unsigned w,unsigned h) {
    const auto mapping=xr::NativeFovUv(rendered,native);
    if(!context||!source||!mapping)return nullptr;
    ComPtr<ID3D11Device> device;context->GetDevice(&device);
    D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
    if(!Prepare(device.Get(),d,w,h))return nullptr;
    // A private SRV works even when the game's backbuffer has no SRV bind flag.
    // CopyResource preserves bytes across UNORM/sRGB spellings of the same family.
    context->CopyResource(source_.Get(),source);
    auto* c=deferred_.Get();c->ClearState();
    c->UpdateSubresource(constants_.Get(),0,nullptr,&*mapping,0,0);
    auto target=write_.Get();c->OMSetRenderTargets(1,&target,nullptr);
    D3D11_VIEWPORT viewport{0,0,static_cast<float>(w),static_cast<float>(h),0,1};
    c->RSSetViewports(1,&viewport);c->RSSetState(raster_.Get());
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(vertex_.Get(),nullptr,0);c->PSSetShader(pixel_.Get(),nullptr,0);
    auto read=read_.Get();auto sampler=sampler_.Get();auto buffer=constants_.Get();
    c->PSSetShaderResources(0,1,&read);c->PSSetSamplers(0,1,&sampler);c->PSSetConstantBuffers(0,1,&buffer);
    c->Draw(3,0);
    ComPtr<ID3D11CommandList> commands;
    if(FAILED(c->FinishCommandList(FALSE,&commands)))return nullptr;
    context->ExecuteCommandList(commands.Get(),TRUE);
    return output_.Get();
}
}
