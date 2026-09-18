#pragma once
#include <cstdint>
namespace preyvr {
enum class ReadbackResult { pending, verified, failed };
struct SettingReadback {
    int expected=0;
    std::uint64_t deadline=0;
    ReadbackResult Observe(bool readable,int actual,std::uint64_t now) const {
        if(readable && actual==expected) return ReadbackResult::verified;
        return now>=deadline ? ReadbackResult::failed : ReadbackResult::pending;
    }
};
}
