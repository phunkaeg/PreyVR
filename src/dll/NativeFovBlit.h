#pragma once
#include "preyvr/NativeFovPlacement.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace preyvr::dll {
// Render thread only. Own UNORM views preserve the encoding inherited by the XR
// swapchain. The deferred command list restores the game's entire pipeline state.
class NativeFovBlit {
public:
    ID3D11Texture2D* Remap(ID3D11DeviceContext*,ID3D11Texture2D*,const xr::Tangents& rendered,
                          const xr::Tangents& native,unsigned width,unsigned height);
private:
    bool Prepare(ID3D11Device*,const D3D11_TEXTURE2D_DESC&,unsigned,unsigned);
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deferred_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> source_,output_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> read_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> write_;
    D3D11_TEXTURE2D_DESC description_{};
    unsigned width_=0,height_=0;
};
}
