#pragma once
namespace preyvr {
// Stored with retained eye images: a fresh separate HUD must not be composed
// over an older eye image that already contains the native HUD.
class HudCapturePair {
public:
    void RecordEye(int eye,bool removed) {if(eye>=0 && eye<2)removed_[eye]=removed;}
    bool CanOverlay(bool stereo,bool capturedNow) const {
        return capturedNow && (!stereo || (removed_[0] && removed_[1]));
    }
private:
    bool removed_[2]={false,false};
};
}
