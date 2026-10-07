#pragma once
#include <d3d11.h>
#include <wrl/client.h>
namespace preyvr::dll {
// The card's texture: the status strip's aspect (the flattened meters are ~2.5:1);
// InventorySwapchain needs at least 640x480.
inline constexpr unsigned NativeWristWidth=1216,NativeWristHeight=480;
// Render-thread only. Same-frame alpha bounds and aspect-preserving fit entirely
// on the GPU. No readback, guessed screen crop, or changes to native transforms.
//
// **The framing is held, not re-fitted every frame.** The bounds grow at once
// (a status effect appearing, a meter's damage flash) but only shrink after
// they have stayed smaller for ~0.7 s, so the meters do not breathe with every
// animation. `alpha` fades the whole card (premultiplied: all four channels).
class NativeWristTexture {
public:
    // `panel`: the hologram's glass behind the meters (dark, translucent, a
    // thin edge), so they read over any background; off for Funk's card.
    ID3D11Texture2D* Fit(ID3D11DeviceContext*,ID3D11Texture2D*,float alpha=1.f,bool panel=false);
    // The last source again (no new capture this frame): a new fade, same pixels.
    ID3D11Texture2D* Refit(ID3D11DeviceContext*,float alpha,bool panel=false);
private:
    ID3D11Texture2D* Run(ID3D11DeviceContext*,float alpha,bool panel);
    bool Prepare(ID3D11Device*,const D3D11_TEXTURE2D_DESC&);
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> boundsShader_,holdShader_,fitShader_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> source_,output_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sourceView_,boundsView_,heldView_;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> boundsWrite_,heldWrite_,outputWrite_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> bounds_,held_,constants_;
    D3D11_TEXTURE2D_DESC description_{};
    bool attempted_=false,reset_=true,retained_=false;
};
}
