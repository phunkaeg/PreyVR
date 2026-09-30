#include "preyvr/NativeHudIsolation.h"
#include "../src/dll/NativeWristTexture.h"
#include "../src/dll/NativeHudValue.h"
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace preyvr;
using Microsoft::WRL::ComPtr;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
namespace {
struct Api {
    using Value=int;
    static constexpr int count=static_cast<int>(hud::ExcludedFromStatus.size()+hud::StatusMeters.size());
    std::array<int,count> objects{};
    int resolved=0,released=0,live=0,failResolve=-1,failRelease=-1;
    bool duplicate=false;
    bool Resolve(const char*,Value& value,void*& object){
        value=++resolved;++live;
        object=&objects[duplicate?0:resolved-1];
        return resolved-1!=failResolve;
    }
    bool Release(Value& value){
        if(!value)return true;
        value=0;--live;return released++!=failRelease;
    }
};
void Transactions(){
    for(int missing=0;missing<Api::count;++missing){
        Api api;api.failResolve=missing;
        {hud::Isolation guard(api);CHECK(!guard.Begin());CHECK(!guard.Excludes(&api.objects[0]));}
        CHECK(api.live==0&&api.released==missing+1);
    }
    Api api;
    {hud::Isolation guard(api);CHECK(guard.Begin());CHECK(!guard.Begin());CHECK(!guard.Complete());
        for(std::size_t i=0;i<hud::ExcludedFromStatus.size();++i)CHECK(guard.Excludes(&api.objects[i]));
        for(std::size_t i=hud::ExcludedFromStatus.size();i<api.objects.size();++i){
            CHECK(!guard.Excludes(&api.objects[i]));guard.Observe(&api.objects[i]);
            CHECK(guard.Complete()); // Other meters may be hidden (e.g. locked psi).
        }
        int unrelated=0;
        CHECK(guard.Complete());CHECK(!guard.Excludes(&unrelated));CHECK(guard.Release());CHECK(!guard.Complete());
        CHECK(!guard.Excludes(&api.objects[0]));
    }
    CHECK(api.live==0&&api.released==Api::count);
    api={};api.duplicate=true;
    {hud::Isolation guard(api);CHECK(!guard.Begin());}CHECK(api.live==0&&api.released==2);
    for(int failure=0;failure<Api::count;++failure){
        api={};api.failRelease=failure;
        {hud::Isolation guard(api);CHECK(guard.Begin());CHECK(!guard.Release());CHECK(api.live==0);}
        CHECK(api.live==0&&api.released==Api::count);
    }
}
struct Graphics {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<ID3D11InfoQueue> debug;
    Graphics(){
        auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_DEBUG,
            nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
        if(FAILED(hr))hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&device,nullptr,&context);
        CHECK(SUCCEEDED(hr));device.As(&debug);
    }
    ComPtr<ID3D11Texture2D> Source(unsigned w,unsigned h,DXGI_FORMAT format,bool blank=false){
        std::vector<std::uint32_t> pixels(w*h);
        // Coloured premultiplied rectangle touching the bottom-left input edge.
        const bool bgra=format==DXGI_FORMAT_B8G8R8A8_UNORM||format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        if(!blank)for(unsigned y=h-50;y<h;++y)for(unsigned x=0;x<100;++x)
            pixels[y*w+x]=bgra?0x80643219u:0x80193264u;
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.Format=format;d.ArraySize=d.MipLevels=1;
        d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{pixels.data(),w*4,0};ComPtr<ID3D11Texture2D> texture;
        CHECK(SUCCEEDED(device->CreateTexture2D(&d,&data,&texture)));return texture;
    }
    void Inspect(ID3D11Texture2D* texture,bool blank){
        CHECK(texture);D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
        CHECK(d.Width==dll::NativeWristWidth&&d.Height==dll::NativeWristHeight&&d.Format==DXGI_FORMAT_R8G8B8A8_UNORM);
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&staging)));
        context->CopyResource(staging.Get(),texture);D3D11_MAPPED_SUBRESOURCE m{};
        CHECK(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m)));
        unsigned x0=d.Width,y0=d.Height,x1=0,y1=0;
        bool allZero=true;std::uint32_t center=0;
        for(unsigned y=0;y<d.Height;++y){
            const auto row=reinterpret_cast<const std::uint32_t*>(static_cast<const char*>(m.pData)+y*m.RowPitch);
            if(y==d.Height/2)center=row[d.Width/2];
            for(unsigned x=0;x<d.Width;++x){
                allZero=allZero&&row[x]==0;
                if((row[x]>>24)>16){x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x);y1=std::max(y1,y);}
            }
        }
        context->Unmap(staging.Get(),0);
        if(blank)CHECK(allZero);
        else {
            CHECK(center==0x80193264u); // Exact swizzle, alpha and gamma-byte preservation.
            CHECK(x0>10&&y0>10&&x1<d.Width-10&&y1<d.Height-10);
            CHECK(std::abs(static_cast<int>(x0+x1)-static_cast<int>(d.Width-1))<=2);
            CHECK(std::abs(static_cast<int>(y0+y1)-static_cast<int>(d.Height-1))<=2);
            const float ratio=float(x1-x0+1)/float(y1-y0+1);CHECK(ratio>1.97f&&ratio<2.03f);
        }
    }
    void NoErrors(){
        if(!debug)return;
        for(UINT64 i=0;i<debug->GetNumStoredMessages();++i){
            SIZE_T n=0;debug->GetMessage(i,nullptr,&n);std::vector<char> storage(n);
            auto message=reinterpret_cast<D3D11_MESSAGE*>(storage.data());debug->GetMessage(i,message,&n);
            if(message->Severity<=D3D11_MESSAGE_SEVERITY_WARNING)throw std::runtime_error(message->pDescription);
        }
    }
};
void Pixels(){
    Graphics g;dll::NativeWristTexture fitter;
    auto sentinel=g.Source(640,480,DXGI_FORMAT_R8G8B8A8_UNORM);
    ComPtr<ID3D11ShaderResourceView> view;CHECK(SUCCEEDED(g.device->CreateShaderResourceView(sentinel.Get(),nullptr,&view)));
    auto v=view.Get();g.context->CSSetShaderResources(0,1,&v);g.context->CSSetShaderResources(1,1,&v);
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    ComPtr<ID3D11Buffer> buffer;CHECK(SUCCEEDED(g.device->CreateBuffer(&bd,nullptr,&buffer)));
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=DXGI_FORMAT_R32_UINT;ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;ud.Buffer.NumElements=4;
    ComPtr<ID3D11UnorderedAccessView> uav;CHECK(SUCCEEDED(g.device->CreateUnorderedAccessView(buffer.Get(),&ud,&uav)));
    auto u=uav.Get();g.context->CSSetUnorderedAccessViews(0,1,&u,nullptr);
    constexpr char code[]="[numthreads(1,1,1)] void main(){}";
    ComPtr<ID3DBlob> bytes;CHECK(SUCCEEDED(D3DCompile(code,sizeof(code)-1,nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&bytes,nullptr)));
    ComPtr<ID3D11ComputeShader> shader;CHECK(SUCCEEDED(g.device->CreateComputeShader(bytes->GetBufferPointer(),bytes->GetBufferSize(),nullptr,&shader)));
    g.context->CSSetShader(shader.Get(),nullptr,0);
    D3D11_QUERY_DESC qd{D3D11_QUERY_OCCLUSION_PREDICATE,0};ComPtr<ID3D11Predicate> predicate;
    CHECK(SUCCEEDED(g.device->CreatePredicate(&qd,&predicate)));
    g.context->Begin(predicate.Get());g.context->End(predicate.Get());g.context->SetPredication(predicate.Get(),TRUE);
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                     DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB}){
        auto input=g.Source(640,480,format);g.Inspect(fitter.Fit(g.context.Get(),input.Get()),false);
        ComPtr<ID3D11ShaderResourceView> before0,before1;
        g.context->CSGetShaderResources(0,1,&before0);g.context->CSGetShaderResources(1,1,&before1);
        CHECK(before0.Get()==view.Get()&&before1.Get()==view.Get());
        ComPtr<ID3D11ComputeShader> previousShader;ComPtr<ID3D11UnorderedAccessView> previousUav;
        ComPtr<ID3D11Predicate> previousPredicate;BOOL condition=FALSE;
        g.context->CSGetShader(&previousShader,nullptr,nullptr);g.context->CSGetUnorderedAccessViews(0,1,&previousUav);
        g.context->GetPredication(&previousPredicate,&condition);
        CHECK(previousShader.Get()==shader.Get()&&previousUav.Get()==uav.Get());
        CHECK(previousPredicate.Get()==predicate.Get()&&condition==TRUE);
    }
    g.context->SetPredication(nullptr,FALSE);
    auto bigger=g.Source(1280,720,DXGI_FORMAT_R8G8B8A8_UNORM);g.Inspect(fitter.Fit(g.context.Get(),bigger.Get()),false);
    auto clear=g.Source(1280,720,DXGI_FORMAT_R8G8B8A8_UNORM,true);g.Inspect(fitter.Fit(g.context.Get(),clear.Get()),true);
    CHECK(!fitter.Fit(nullptr,clear.Get())&&!fitter.Fit(g.context.Get(),nullptr));
    Graphics other;auto otherInput=other.Source(640,480,DXGI_FORMAT_R8G8B8A8_UNORM);
    CHECK(!fitter.Fit(g.context.Get(),otherInput.Get()));
    other.Inspect(fitter.Fit(other.context.Get(),otherInput.Get()),false);
    g.Inspect(fitter.Fit(g.context.Get(),bigger.Get()),false); // Recreate on device change.
    other.NoErrors();
    g.NoErrors();
}
}
int main(){try{Transactions();Pixels();std::cout<<"native wrist isolation and GPU pixels PASS\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
