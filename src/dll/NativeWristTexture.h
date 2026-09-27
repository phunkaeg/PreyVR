#pragma once
#include <d3d11.h>
#include <wrl/client.h>
namespace preyvr::dll {
inline constexpr unsigned NativeWristWidth=768,NativeWristHeight=512;
// Render-thread only. Same-frame alpha bounds and aspect-preserving fit entirely
// on the GPU. No readback, guessed screen crop, or changes to native transforms.
class NativeWristTexture {
public:
    ID3D11Texture2D* Fit(ID3D11DeviceContext*,ID3D11Texture2D*);
private:
    bool Prepare(ID3D11Device*,const D3D11_TEXTURE2D_DESC&);
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> boundsShader_,fitShader_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> source_,output_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sourceView_,boundsView_;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> boundsWrite_,outputWrite_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> bounds_;
    D3D11_TEXTURE2D_DESC description_{};
    bool attempted_=false;
};
}
