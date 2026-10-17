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
const char* InventorySwapchain::RefusalName(Refusal refusal) {
    switch(refusal) {
    case Refusal::none:return "none";
    case Refusal::session:return "no_session";
    case Refusal::source:return "source_texture";
    case Refusal::presentationFormat:return "presentation_format";
    case Refusal::enumerateFormats:return "enumerate_formats";
    case Refusal::formatNotListed:return "format_not_listed";
    case Refusal::create:return "create_swapchain";
    case Refusal::images:return "enumerate_images";
    case Refusal::image:return "image_incompatible";
    }
    return "unknown";
}
void InventorySwapchain::Reset() {
    if(chain_)api_.destroy(chain_);
    chain_=XR_NULL_HANDLE;session_=XR_NULL_HANDLE;images_.clear();desc_={};
    presentationFormat_=DXGI_FORMAT_UNKNOWN;
    attempted_=fault_=acquired_=false;
    refusal_=Refusal::none;result_=XR_SUCCESS;
}
bool InventorySwapchain::Prepare(XrSession session,const D3D11_TEXTURE2D_DESC& source,DXGI_FORMAT presentationFormat) {
    // A retired chain (a failed wait or release) is built again rather than kept dark.
    if(attempted_ && !fault_ && session_==session && presentationFormat_==presentationFormat && desc_.Format==source.Format &&
       InventoryTextureCompatible(desc_,source))return chain_!=XR_NULL_HANDLE;
    Reset();attempted_=true;session_=session;desc_=source;
    presentationFormat_=presentationFormat;
    auto refuse=[&](Refusal why,XrResult result=XR_SUCCESS){refusal_=why;result_=result;return false;};
    if(!session)return refuse(Refusal::session);
    if(!Supported(source))return refuse(Refusal::source);
    if((presentationFormat!=DXGI_FORMAT_R8G8B8A8_UNORM && presentationFormat!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
        presentationFormat!=DXGI_FORMAT_B8G8R8A8_UNORM && presentationFormat!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) ||
       Family(presentationFormat)!=Family(source.Format))return refuse(Refusal::presentationFormat);
    uint32_t count=0;
    XrResult result=api_.formats(session,0,&count,nullptr);
    if(result!=XR_SUCCESS || !count || count>256)return refuse(Refusal::enumerateFormats,result);
    std::vector<int64_t> formats(count);
    result=api_.formats(session,count,&count,formats.data());
    if(result!=XR_SUCCESS || count>formats.size())return refuse(Refusal::enumerateFormats,result);
    formats.resize(count);
    // Inherit the actual main/menu choice, not an independent sRGB preference.
    // CopyResource preserves bytes; it neither converts gamma nor swizzles.
    const int64_t format=presentationFormat;
    if(std::find(formats.begin(),formats.end(),format)==formats.end())return refuse(Refusal::formatNotListed);
    XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    create.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    create.format=format;create.sampleCount=1;create.width=source.Width;create.height=source.Height;
    create.faceCount=1;create.arraySize=1;create.mipCount=1;
    result=api_.create(session,&create,&chain_);
    if(result!=XR_SUCCESS || !chain_) {chain_=XR_NULL_HANDLE;return refuse(Refusal::create,result);}
    auto discard=[&](Refusal why,XrResult failed=XR_SUCCESS){
        api_.destroy(chain_);chain_=XR_NULL_HANDLE;images_.clear();return refuse(why,failed);};
    count=0;
    result=api_.images(chain_,0,&count,nullptr);
    if(result!=XR_SUCCESS || !count || count>64)return discard(Refusal::images,result);
    images_.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    result=api_.images(chain_,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images_.data()));
    if(result!=XR_SUCCESS || !count || count>images_.size())return discard(Refusal::images,result);
    images_.resize(count);
    for(const auto& image:images_) {
        if(!image.texture)return discard(Refusal::image);
        D3D11_TEXTURE2D_DESC d{};image.texture->GetDesc(&d);
        if(!InventoryTextureCompatible(source,d))return discard(Refusal::image);
    }
    return true;
}
InventorySwapchain::Upload InventorySwapchain::Copy(ID3D11DeviceContext* context,ID3D11Texture2D* source) {
    if(fault_)return Upload::failed;   // retired until Prepare builds it again
    if(!chain_ || !context || !source)return Upload::failed;
    D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
    if(!InventoryTextureCompatible(desc_,d))return Upload::failed;
    // Only a session or instance the runtime calls gone ends VR; any other
    // failure retires this layer alone.
    const auto fail=[&](XrResult result){
        fault_=true;
        return result==XR_ERROR_SESSION_LOST||result==XR_ERROR_INSTANCE_LOST?Upload::sessionFault:Upload::failed;};
    if(!acquired_) {
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if(const XrResult result=api_.acquire(chain_,&acquire,&index_);result!=XR_SUCCESS)
            return result==XR_ERROR_SESSION_LOST||result==XR_ERROR_INSTANCE_LOST?fail(result):Upload::failed;
        acquired_=true;
    }
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout=50000000; // 50 ms; no unbounded render-thread wait.
    if(const XrResult result=api_.wait(chain_,&wait);result!=XR_SUCCESS) {
        // XR_TIMEOUT_EXPIRED is positive, but it does NOT permit copy/release:
        // this frame goes without the layer and the next Copy waits again.
        return XR_FAILED(result)?fail(result):Upload::failed;
    }
    acquired_=false;
    const bool valid=index_<images_.size();
    if(valid)context->CopyResource(images_[index_].texture,source);
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if(const XrResult result=api_.release(chain_,&release);result!=XR_SUCCESS)return fail(result);
    return valid?Upload::ready:Upload::failed;
}
}
