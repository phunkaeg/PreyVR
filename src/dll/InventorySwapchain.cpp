#include "InventorySwapchain.h"
#include <algorithm>

namespace preyvr::dll {
namespace {
int Family(DXGI_FORMAT format) {
    switch(format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:return 1;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:return 2;
    default:return 0;
    }
}
bool Supported(const D3D11_TEXTURE2D_DESC& d) {
    return Family(d.Format)!=0 && d.Width>=640 && d.Height>=480 &&
        d.Width<=8192 && d.Height<=8192 && d.ArraySize==1 && d.MipLevels==1 &&
        d.SampleDesc.Count==1 && d.SampleDesc.Quality==0;
}
}
bool InventoryTextureCompatible(const D3D11_TEXTURE2D_DESC& a,const D3D11_TEXTURE2D_DESC& b) {
    return Supported(a)&&Supported(b)&&a.Width==b.Width&&a.Height==b.Height&&
        Family(a.Format)==Family(b.Format);
}
void InventorySwapchain::Reset() {
    if(chain_)api_.destroy(chain_);
    chain_=XR_NULL_HANDLE;session_=XR_NULL_HANDLE;images_.clear();desc_={};
    presentationFormat_=DXGI_FORMAT_UNKNOWN;
    attempted_=fault_=false;
}
bool InventorySwapchain::Prepare(XrSession session,const D3D11_TEXTURE2D_DESC& source,DXGI_FORMAT presentationFormat) {
    if(attempted_ && session_==session && presentationFormat_==presentationFormat && desc_.Format==source.Format &&
       InventoryTextureCompatible(desc_,source))return chain_!=XR_NULL_HANDLE&&!fault_;
    Reset();attempted_=true;session_=session;desc_=source;
    presentationFormat_=presentationFormat;
    if(!session || !Supported(source))return false;
    if((presentationFormat!=DXGI_FORMAT_R8G8B8A8_UNORM && presentationFormat!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
        presentationFormat!=DXGI_FORMAT_B8G8R8A8_UNORM && presentationFormat!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) ||
       Family(presentationFormat)!=Family(source.Format))return false;
    uint32_t count=0;
    if(api_.formats(session,0,&count,nullptr)!=XR_SUCCESS || !count || count>256)return false;
    std::vector<int64_t> formats(count);
    if(api_.formats(session,count,&count,formats.data())!=XR_SUCCESS || count>formats.size())return false;
    formats.resize(count);
    // Inherit the actual main/menu choice, not an independent sRGB preference.
    // CopyResource preserves bytes; it neither converts gamma nor swizzles.
    const int64_t format=presentationFormat;
    if(std::find(formats.begin(),formats.end(),format)==formats.end())return false;
    XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    create.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    create.format=format;create.sampleCount=1;create.width=source.Width;create.height=source.Height;
    create.faceCount=1;create.arraySize=1;create.mipCount=1;
    if(api_.create(session,&create,&chain_)!=XR_SUCCESS || !chain_) {chain_=XR_NULL_HANDLE;return false;}
    auto refuse=[&]{api_.destroy(chain_);chain_=XR_NULL_HANDLE;images_.clear();return false;};
    count=0;
    if(api_.images(chain_,0,&count,nullptr)!=XR_SUCCESS || !count || count>64)return refuse();
    images_.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if(api_.images(chain_,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images_.data()))!=XR_SUCCESS ||
       !count || count>images_.size())return refuse();
    images_.resize(count);
    for(const auto& image:images_) {
        if(!image.texture)return refuse();
        D3D11_TEXTURE2D_DESC d{};image.texture->GetDesc(&d);
        if(!InventoryTextureCompatible(source,d))return refuse();
    }
    return true;
}
InventorySwapchain::Upload InventorySwapchain::Copy(ID3D11DeviceContext* context,ID3D11Texture2D* source) {
    if(fault_)return Upload::sessionFault;
    if(!chain_ || !context || !source)return Upload::failed;
    D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
    if(!InventoryTextureCompatible(desc_,d))return Upload::failed;
    uint32_t index=0;
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(api_.acquire(chain_,&acquire,&index)!=XR_SUCCESS)return Upload::failed;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout=50000000; // 50 ms; no unbounded render-thread wait.
    if(api_.wait(chain_,&wait)!=XR_SUCCESS) {
        // XR_TIMEOUT_EXPIRED is positive, but it does NOT permit copy/release.
        fault_=true;return Upload::sessionFault;
    }
    const bool valid=index<images_.size();
    if(valid)context->CopyResource(images_[index].texture,source);
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if(api_.release(chain_,&release)!=XR_SUCCESS) {fault_=true;return Upload::sessionFault;}
    return valid?Upload::ready:Upload::failed;
}
}
