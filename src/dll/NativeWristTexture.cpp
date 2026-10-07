#include "NativeWristTexture.h"
#include <d3dcompiler.h>
#include <array>
#include <cstring>
#include <algorithm>
#include <cmath>
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
#elif defined(HOLD_BOUNDS)
// One thread. held = x0,y0,x1,y1,frames-smaller. Grow at once, shrink only
// after the new bounds have stayed inside for 60 frames (~0.7 s).
RWBuffer<uint> held : register(u0);
[numthreads(1,1,1)]
void main() {
    uint4 n=uint4(boundsRead[0],boundsRead[1],boundsRead[2],boundsRead[3]);
    uint4 h=uint4(held[0],held[1],held[2],held[3]);uint c=held[4];
    if(n.z>n.x&&n.w>n.y) {
        if(!(h.z>h.x&&h.w>h.y)) {h=n;c=0;}
        else if(n.x<h.x||n.y<h.y||n.z>h.z||n.w>h.w) {h=uint4(min(h.xy,n.xy),max(h.zw,n.zw));c=0;}
        else if(any(n!=h)) {c=c+1;if(c>60){h=n;c=0;}}
        else c=0;
    }
    held[0]=h.x;held[1]=h.y;held[2]=h.z;held[3]=h.w;held[4]=c;
}
#else
cbuffer Presentation : register(b0) {float fade;float panel;float2 unused;};
RWTexture2D<float4> outputImage : register(u0);
float4 samplePixel(int2 p,uint w,uint h) {
    if(p.x<0||p.y<0||p.x>=int(w)||p.y>=int(h))return 0;
    return sourceImage.Load(int3(p,0));
}
float4 bilinear(float2 p,uint w,uint h) {
    int2 low=int2(floor(p));float2 f=frac(p);
    return lerp(lerp(samplePixel(low,w,h),samplePixel(low+int2(1,0),w,h),f.x),
                lerp(samplePixel(low+int2(0,1),w,h),samplePixel(low+int2(1,1),w,h),f.x),f.y);
}
// Signed distance to a rounded rectangle centred at 0 (negative inside).
float roundedBox(float2 p,float2 extent,float radius) {
    float2 q=abs(p)-extent+radius;
    return length(max(q,0))+min(max(q.x,q.y),0)-radius;
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
        // With the panel, the meters sit inside the glass with a fixed margin
        // (output pixels), whatever their size in the eye target.
        float scale=panel>0?min((ow-120)/extent.x,(oh-84)/extent.y):
                            min(ow/(extent.x+16),oh/(extent.y+16))*.94;
        float2 p=(float2(id.xy)+.5-float2(ow,oh)*.5)/scale+(float2(b.xy)+float2(b.zw))*.5-.5;
        if(scale<.9) {
            // Minifying (a large eye target): four taps, no shimmer.
            float d=.25/scale;
            colour=(bilinear(p+float2(-d,-d),w,h)+bilinear(p+float2(d,-d),w,h)+
                    bilinear(p+float2(-d,d),w,h)+bilinear(p+float2(d,d),w,h))*.25;
        } else colour=bilinear(p,w,h);
    }
    if(panel>0) {
        // The hologram's glass: dark, translucent, a thin bright edge and a
        // faint scan, under the meters (premultiplied "over").
        float2 c=float2(id.xy)+.5-float2(ow,oh)*.5;
        float2 halfSize=float2(ow,oh)*.5-6;
        float dist=roundedBox(c,halfSize,30);
        float inside=saturate(.5-dist);
        float edge=saturate(1-abs(dist+1.5)/1.6)*.55+saturate(1-abs(dist+1.5)/9)*.12;
        float t=float(id.y)/float(oh);
        float scan=.93+.07*sin(float(id.y)*1.0472);
        float glassA=inside*lerp(.62,.50,t)*scan;
        float4 glass=float4(float3(.012,.030,.042)*glassA,glassA);
        float4 rim=float4(float3(.60,.86,.96)*edge,edge*.9)*inside;
        float4 back=rim+glass*(1-rim.a);
        colour=colour+back*(1-colour.a);
    }
    outputImage[id.xy]=colour*fade;
}
#endif
)";
bool Compile(ID3D11Device* device,int pass,ID3D11ComputeShader** result) {
    const D3D_SHADER_MACRO bounds[]={{"FIND_BOUNDS","1"},{nullptr,nullptr}};
    const D3D_SHADER_MACRO hold[]={{"HOLD_BOUNDS","1"},{nullptr,nullptr}};
    ComPtr<ID3DBlob> code,error;
    return SUCCEEDED(D3DCompile(shaders,std::strlen(shaders),"NativeWristTexture",pass==0?bounds:pass==1?hold:nullptr,
        nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error))&&
        SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,result));
}
struct ComputeState {
    ID3D11DeviceContext* c;
    ComPtr<ID3D11ComputeShader> shader;
    std::array<ID3D11ClassInstance*,256> instances{};UINT count=256;
    std::array<ID3D11ShaderResourceView*,2> resources{};
    ComPtr<ID3D11UnorderedAccessView> output;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11Predicate> predicate;BOOL predicateValue=FALSE;
    explicit ComputeState(ID3D11DeviceContext* context):c(context){
        c->CSGetShader(&shader,instances.data(),&count);c->CSGetConstantBuffers(0,1,&constants);
        c->CSGetShaderResources(0,2,resources.data());c->CSGetUnorderedAccessViews(0,1,&output);
        c->GetPredication(&predicate,&predicateValue);c->SetPredication(nullptr,FALSE);
    }
    ~ComputeState(){
        ID3D11UnorderedAccessView* nullOutput=nullptr;c->CSSetUnorderedAccessViews(0,1,&nullOutput,nullptr);
        c->CSSetShaderResources(0,2,resources.data());
        auto u=output.Get();const UINT retain=~0u;c->CSSetUnorderedAccessViews(0,1,&u,&retain);
        c->CSSetShader(shader.Get(),instances.data(),count);c->SetPredication(predicate.Get(),predicateValue);
        auto b=constants.Get();c->CSSetConstantBuffers(0,1,&b);
        for(auto p:resources)if(p)p->Release();for(UINT i=0;i<count;++i)if(instances[i])instances[i]->Release();
    }
};
}
bool NativeWristTexture::Prepare(ID3D11Device* device,const D3D11_TEXTURE2D_DESC& d){
    if(device_.Get()!=device){
        device_=device;attempted_=false;source_.Reset();sourceView_.Reset();output_.Reset();outputWrite_.Reset();
        boundsShader_.Reset();holdShader_.Reset();fitShader_.Reset();bounds_.Reset();boundsView_.Reset();boundsWrite_.Reset();
        held_.Reset();heldView_.Reset();heldWrite_.Reset();constants_.Reset();
    }
    if(!attempted_){
        attempted_=true;
        if(!Compile(device,0,boundsShader_.ReleaseAndGetAddressOf())||
           !Compile(device,1,holdShader_.ReleaseAndGetAddressOf())||
           !Compile(device,2,fitShader_.ReleaseAndGetAddressOf()))return false;
        D3D11_BUFFER_DESC b{};b.ByteWidth=16;b.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        if(FAILED(device->CreateBuffer(&b,nullptr,bounds_.ReleaseAndGetAddressOf())))return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC s{};s.Format=DXGI_FORMAT_R32_UINT;s.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;s.Buffer.NumElements=4;
        D3D11_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=DXGI_FORMAT_R32_UINT;u.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;u.Buffer.NumElements=4;
        if(FAILED(device->CreateShaderResourceView(bounds_.Get(),&s,boundsView_.ReleaseAndGetAddressOf()))||
           FAILED(device->CreateUnorderedAccessView(bounds_.Get(),&u,boundsWrite_.ReleaseAndGetAddressOf())))return false;
        D3D11_BUFFER_DESC hb=b;hb.ByteWidth=32;
        if(FAILED(device->CreateBuffer(&hb,nullptr,held_.ReleaseAndGetAddressOf())))return false;
        D3D11_UNORDERED_ACCESS_VIEW_DESC hu=u;hu.Buffer.NumElements=8;
        if(FAILED(device->CreateShaderResourceView(held_.Get(),&s,heldView_.ReleaseAndGetAddressOf()))||
           FAILED(device->CreateUnorderedAccessView(held_.Get(),&hu,heldWrite_.ReleaseAndGetAddressOf())))return false;
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=16;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if(FAILED(device->CreateBuffer(&cb,nullptr,constants_.ReleaseAndGetAddressOf())))return false;
        reset_=true;
    }
    if(!boundsShader_||!holdShader_||!fitShader_||!boundsView_||!boundsWrite_||!heldView_||!heldWrite_||!constants_)return false;
    if(source_&&sourceView_&&output_&&outputWrite_&&d.Width==description_.Width&&d.Height==description_.Height&&d.Format==description_.Format)return true;
    source_.Reset();sourceView_.Reset();output_.Reset();outputWrite_.Reset();description_=d;retained_=false;
    // New source dimensions: the held framing (source pixels) no longer applies.
    reset_=true;
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
ID3D11Texture2D* NativeWristTexture::Fit(ID3D11DeviceContext* context,ID3D11Texture2D* source,float alpha,bool panel){
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
    retained_=true;
    return Run(context,alpha,panel);
}
ID3D11Texture2D* NativeWristTexture::Refit(ID3D11DeviceContext* context,float alpha,bool panel){
    if(!context||!retained_||!source_||!output_||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return nullptr;
    ComPtr<ID3D11Device> device;context->GetDevice(&device);
    if(device.Get()!=device_.Get())return nullptr;
    ComputeState saved(context);
    return Run(context,alpha,panel);
}
ID3D11Texture2D* NativeWristTexture::Run(ID3D11DeviceContext* context,float alpha,bool panel){
    const auto& d=description_;
    ID3D11ShaderResourceView* nulls[2]{};ID3D11UnorderedAccessView* nullUav=nullptr;
    context->CSSetShaderResources(0,2,nulls);context->CSSetUnorderedAccessViews(0,1,&nullUav,nullptr);
    const UINT initial[4]={~0u,~0u,0,0};context->UpdateSubresource(bounds_.Get(),0,nullptr,initial,0,0);
    auto srv=sourceView_.Get();auto uav=boundsWrite_.Get();
    context->CSSetShaderResources(0,1,&srv);context->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
    context->CSSetShader(boundsShader_.Get(),nullptr,0);context->Dispatch((d.Width+15)/16,(d.Height+15)/16,1);
    context->CSSetUnorderedAccessViews(0,1,&nullUav,nullptr);
    if(reset_){const UINT none[8]{};context->UpdateSubresource(held_.Get(),0,nullptr,none,0,0);reset_=false;}
    auto bounds=boundsView_.Get();context->CSSetShaderResources(1,1,&bounds);
    auto hold=heldWrite_.Get();context->CSSetUnorderedAccessViews(0,1,&hold,nullptr);
    context->CSSetShader(holdShader_.Get(),nullptr,0);context->Dispatch(1,1,1);
    context->CSSetUnorderedAccessViews(0,1,&nullUav,nullptr);
    context->CSSetShaderResources(1,1,nulls);
    bounds=heldView_.Get();context->CSSetShaderResources(1,1,&bounds);
    const float fade[4]{std::clamp(std::isfinite(alpha)?alpha:0.f,0.f,1.f),panel?1.f:0.f,0,0};
    context->UpdateSubresource(constants_.Get(),0,nullptr,fade,0,0);
    auto cbuffer=constants_.Get();context->CSSetConstantBuffers(0,1,&cbuffer);
    uav=outputWrite_.Get();context->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
    context->CSSetShader(fitShader_.Get(),nullptr,0);
    context->Dispatch((NativeWristWidth+15)/16,(NativeWristHeight+15)/16,1);
    context->CSSetShaderResources(0,2,nulls);
    return output_.Get();
}
}
