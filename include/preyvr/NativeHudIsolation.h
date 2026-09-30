#pragma once
#include <array>
#include <cstddef>
namespace preyvr::hud {
// Named, non-mask siblings in the patched Steam movie. No native visibility,
// transforms, ActionScript variables or animation playlists are modified.
inline constexpr std::array ExcludedFromStatus{
    "_root.damage", "_root.ar_mc", "_root.safe.damage_mc", "_root.safe.awareness_mc",
    "_root.safe.reticle_mc", "_root.safe.quadrant_W", "_root.safe.quadrant_N",
    "_root.safe.quadrant_NE", "_root.safe.quadrant_E", "_root.safe.wheel_mc",
    "_root.safe.quadrant_NW", "_root.safe.quadrant_SE", "_root.safe.quadrant_S",
    "_root.safe.quadrant_C", "_root.safe.quadrant_N_2", "_root.safe.quadrant_SW.power_mc",
    "_root.safe.quadrant_SW.climb_mc", "_root.safe.quadrant_SW.flashlight_mc",
    "_root.safe.quadrant_SW.stealth", "_root.safe.quadrant_SW.pickup"};
inline constexpr std::array StatusMeters{"_root.safe.quadrant_SW.health_mc",
    "_root.safe.quadrant_SW.psi_mc", "_root.safe.quadrant_SW.armor_mc"};

// Retained handles live for exactly one replay, inside the native render locks.
// All references, including a partial failed lookup, are released on failure.
template<class Api> class Isolation {
public:
    explicit Isolation(Api& api):api_(api){}
    Isolation(const Isolation&)=delete;
    Isolation& operator=(const Isolation&)=delete;
    bool Begin(){
        if(attempted_)return false;
        attempted_=true;
        for(std::size_t i=0;i<objects_.size();++i){
            const auto path=i<ExcludedFromStatus.size()?ExcludedFromStatus[i]:StatusMeters[i-ExcludedFromStatus.size()];
            if(!api_.Resolve(path,values_[i],objects_[i])||!objects_[i])return false;
            for(std::size_t j=0;j<i;++j)if(objects_[j]==objects_[i])return false;
        }
        ready_=true;return true;
    }
    bool Excludes(const void* object)const{
        if(!ready_)return false;
        for(std::size_t i=0;i<ExcludedFromStatus.size();++i)if(objects_[i]==object)return true;
        return false;
    }
    void Observe(const void* object){
        if(!ready_)return;
        for(std::size_t i=0;i<StatusMeters.size();++i)
            if(objects_[ExcludedFromStatus.size()+i]==object)observed_[i]=true;
    }
    bool Complete()const{
        if(!ready_)return false;
        // One meter proves traversal below the common SW ancestor. Requiring
        // all three would reject legitimate native visibility (e.g. locked psi).
        for(bool seen:observed_)if(seen)return true;
        return false;
    }
    bool Release(){
        ready_=false;bool ok=true;
        for(auto& value:values_)ok=api_.Release(value)&&ok;
        return ok;
    }
    ~Isolation(){Release();}
private:
    Api& api_;
    std::array<typename Api::Value,ExcludedFromStatus.size()+StatusMeters.size()> values_{};
    std::array<void*,ExcludedFromStatus.size()+StatusMeters.size()> objects_{};
    std::array<bool,StatusMeters.size()> observed_{};
    bool attempted_=false,ready_=false;
};
}
