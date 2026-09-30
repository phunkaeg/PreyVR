#include "NativeWristTexture.h"
#include <d3dcompiler.h>
#include <array>
#include <cstring>
namespace preyvr::dll {
namespace {
using Microsoft::WRL::ComPtr;
constexpr char shaders[]=R"(
Texture2D<float4> sourceImage : register(t0);
Buffer<uint> boundsRead : register(t1);
#ifdef FIND_BOUNDS
RWBuffer<uint> boundsWrite : register(u0);
groupshared uint x0,y0,x1,y1;
[numthreads(16,16,1)]
void main(uint3 id:SV_DispatchThreadID,uint index:SV_GroupIndex) {
    if(index==0){x0=0xffffffff;y0=0xffffffff;x1=0;y1=0;}
    GroupMemoryBarrierWithGroupSync();
    uint w,h;sourceImage.GetDimensions(w,h);
    if(id.x<w&&id.y<h&&sourceImage.Load(int3(id.xy,0)).a>0.003) {
        InterlockedMin(x0,id.x);InterlockedMin(y0,id.y);
        InterlockedMax(x1,id.x+1);InterlockedMax(y1,id.y+1);
    }
    GroupMemoryBarrierWithGroupSync();
    if(index==0&&x1>x0&&y1>y0) {
        InterlockedMin(boundsWrite[0],x0);InterlockedMin(boundsWrite[1],y0);
        InterlockedMax(boundsWrite[2],x1);InterlockedMax(boundsWrite[3],y1);
    }
}
#else
RWTexture2D<float4> outputImage : register(u0);
float4 samplePixel(int2 p,uint w,uint h) {
    if(p.x<0||p.y<0||p.x>=int(w)||p.y>=int(h))return 0;
    return sourceImage.Load(int3(p,0));
}
[numthreads(16,16,1)]
void main(uint3 id:SV_DispatchThreadID) {
    uint ow,oh;outputImage.GetDimensions(ow,oh);
    if(id.x>=ow||id.y>=oh)return;
    uint4 b=uint4(boundsRead[0],boundsRead[1],boundsRead[2],boundsRead[3]);
    float4 colour=0;
    if(b.z>b.x&&b.w>b.y) {
        uint w,h;sourceImage.GetDimensions(w,h);
        float2 extent=float2(b.z-b.x,b.w-b.y);
        float scale=min(ow/(extent.x+16),oh/(extent.y+16))*.94;
        float2 p=(float2(id.xy)+.5-float2(ow,oh)*.5)/scale+(float2(b.xy)+float2(b.zw))*.5-.5;
        int2 low=int2(floor(p));float2 f=frac(p);
        colour=lerp(lerp(samplePixel(low,w,h),samplePixel(low+int2(1,0),w,h),f.x),
                    lerp(samplePixel(low+int2(0,1),w,h),samplePixel(low+int2(1,1),w,h),f.x),f.y);
    }
    outputImage[id.xy]=colour;
}
#endif
)";
bool Compile(ID3D11Device* device,bool bounds,ID3D11ComputeShader** result) {
    const D3D_SHADER_MACRO defines[]={{"FIND_BOUNDS","1"},{nullptr,nullptr}};
    ComPtr<ID3DBlob> code,error;
    return SUCCEEDED(D3DCompile(shaders,std::strlen(shaders),"NativeWristTexture",bounds?defines:nullptr,
        nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error))&&
        SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,result));
}
struct ComputeState {
    ID3D11DeviceContext* c;
    ComPtr<ID3D11ComputeShader> shader;
    std::array<ID3D11ClassInstance*,256> instances{};UINT count=256;
    std::array<ID3D11ShaderResourceView*,2> resources{};
    ComPtr<ID3D11UnorderedAccessView> output;
    ComPtr<ID3D11Predicate> predicate;BOOL predicateValue=FALSE;
    explicit ComputeState(ID3D11DeviceContext* context):c(context){
        c->CSGetShader(&shader,instances.data(),&count);
        c->CSGetShaderResources(0,2,resources.data());c->CSGetUnorderedAccessViews(0,1,&output);
        c->GetPredication(&predicate,&predicateValue);c->SetPredication(nullptr,FALSE);
    }
    ~ComputeState(){
        ID3D11UnorderedAccessView* nullOutput=nullptr;c->CSSetUnorderedAccessViews(0,1,&nullOutput,nullptr);
        c->CSSetShaderResources(0,2,resources.data());
        auto u=output.Get();const UINT retain=~0u;c->CSSetUnorderedAccessViews(0,1,&u,&retain);
        c->CSSetShader(shader.Get(),instances.data(),count);c->SetPredication(predicate.Get(),predicateValue);
        for(auto p:resources)if(p)p->Release();for(UINT i=0;i<count;++i)if(instances[i])instances[i]->Release();
    }
};
}
bool NativeWristTexture::Prepare(ID3D11Device* device,const D3D11_TEXTURE2D_DESC& d){
    if(device_.Get()!=device){
        device_=device;attempted_=false;source_.Reset();sourceView_.Reset();output_.Reset();outputWrite_.Reset();
        boundsShader_.Reset();fitShader_.Reset();bounds_.Reset();boundsView_.Reset();boundsWrite_.Reset();
    }
    if(!attempted_){
        attempted_=true;
        if(!Compile(device,true,boundsShader_.ReleaseAndGetAddressOf())||
           !Compile(device,false,fitShader_.ReleaseAndGetAddressOf()))return false;
        D3D11_BUFFER_DESC b{};b.ByteWidth=16;b.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        if(FAILED(device->CreateBuffer(&b,nullptr,bounds_.ReleaseAndGetAddressOf())))return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC s{};s.Format=DXGI_FORMAT_R32_UINT;s.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;s.Buffer.NumElements=4;
        D3D11_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=DXGI_FORMAT_R32_UINT;u.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;u.Buffer.NumElements=4;
        if(FAILED(device->CreateShaderResourceView(bounds_.Get(),&s,boundsView_.ReleaseAndGetAddressOf()))||
           FAILED(device->CreateUnorderedAccessView(bounds_.Get(),&u,boundsWrite_.ReleaseAndGetAddressOf())))return false;
    }
    if(!boundsShader_||!fitShader_||!boundsView_||!boundsWrite_)return false;
    if(source_&&sourceView_&&output_&&outputWrite_&&d.Width==description_.Width&&d.Height==description_.Height&&d.Format==description_.Format)return true;
    source_.Reset();sourceView_.Reset();output_.Reset();outputWrite_.Reset();description_=d;
    // Copy bytes to UNORM views. Do not decode gamma here: preserve the game's
    // current pixel encoding, inherited by the wrist XR swapchain at submission.
    auto input=d;input.MiscFlags=0;input.CPUAccessFlags=0;input.Usage=D3D11_USAGE_DEFAULT;
    input.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    input.Format=d.Format==DXGI_FORMAT_B8G8R8A8_UNORM||d.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB?
        DXGI_FORMAT_B8G8R8A8_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM;
    auto output=input;output.Width=NativeWristWidth;output.Height=NativeWristHeight;
    output.Format=DXGI_FORMAT_R8G8B8A8_UNORM;output.BindFlags=D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE;
    return SUCCEEDED(device->CreateTexture2D(&input,nullptr,&source_))&&
        SUCCEEDED(device->CreateShaderResourceView(source_.Get(),nullptr,&sourceView_))&&
        SUCCEEDED(device->CreateTexture2D(&output,nullptr,&output_))&&
        SUCCEEDED(device->CreateUnorderedAccessView(output_.Get(),nullptr,&outputWrite_));
}
ID3D11Texture2D* NativeWristTexture::Fit(ID3D11DeviceContext* context,ID3D11Texture2D* source){
    if(!context||!source||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return nullptr;
    D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
    if(d.Width==0||d.Height==0||d.Width>8192||d.Height>8192||d.ArraySize!=1||d.MipLevels!=1||
       d.SampleDesc.Count!=1||d.SampleDesc.Quality!=0)return nullptr;
    if(d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB&&
       d.Format!=DXGI_FORMAT_B8G8R8A8_UNORM&&d.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)return nullptr;
    ComPtr<ID3D11Device> device,owner;context->GetDevice(&device);source->GetDevice(&owner);
    if(device.Get()!=owner.Get()||!Prepare(device.Get(),d))return nullptr;
    ComputeState saved(context);
    ID3D11ShaderResourceView* nulls[2]{};ID3D11UnorderedAccessView* nullUav=nullptr;
    context->CSSetShaderResources(0,2,nulls);context->CSSetUnorderedAccessViews(0,1,&nullUav,nullptr);
    context->CopyResource(source_.Get(),source);
    const UINT initial[4]={~0u,~0u,0,0};context->UpdateSubresource(bounds_.Get(),0,nullptr,initial,0,0);
    auto srv=sourceView_.Get();auto uav=boundsWrite_.Get();
    context->CSSetShaderResources(0,1,&srv);context->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
    context->CSSetShader(boundsShader_.Get(),nullptr,0);context->Dispatch((d.Width+15)/16,(d.Height+15)/16,1);
    context->CSSetUnorderedAccessViews(0,1,&nullUav,nullptr);
    auto bounds=boundsView_.Get();context->CSSetShaderResources(1,1,&bounds);
    uav=outputWrite_.Get();context->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
    context->CSSetShader(fitShader_.Get(),nullptr,0);
    context->Dispatch((NativeWristWidth+15)/16,(NativeWristHeight+15)/16,1);
    context->CSSetShaderResources(0,2,nulls);
    return output_.Get();
}
}
