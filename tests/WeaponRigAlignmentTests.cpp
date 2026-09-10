#include "preyvr/WeaponRigAlignment.h"
#include "preyvr/AnimIk.h"
#include "preyvr/MotionController.h"
#include "preyvr/StereoCamera.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

using namespace preyvr;
using namespace preyvr::weaponrig;
namespace {
void Check(bool ok, const char* why)
{
    if (!ok) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
Quaternion Q(float x, float y, float z, float angle)
{
    const float s = std::sin(angle * .5f) / std::sqrt(x*x + y*y + z*z);
    return {x*s, y*s, z*s, std::cos(angle*.5f)};
}
Quaternion Inv(Quaternion q) { return {-q.x, -q.y, -q.z, q.w}; }
float Distance(Vec3 a, Vec3 b)
{
    return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));
}
bool SameRotation(Quaternion a, Quaternion b)
{
    return Distance(Rotate(a, {1,0,0}), Rotate(b, {1,0,0})) < 1e-4f &&
        Distance(Rotate(a, {0,1,0}), Rotate(b, {0,1,0})) < 1e-4f &&
        Distance(Rotate(a, {0,0,1}), Rotate(b, {0,0,1})) < 1e-4f;
}

struct Fixture {
    static constexpr std::uintptr_t base = 0x180000000ull, dataBase = 0x200000000ull;
    std::array<std::byte, 32768> bytes{};
    std::size_t used = 8;
    std::uintptr_t weapon, mount, binding, arms, skel, joints, bindPose, pose;
    std::uintptr_t weaponChar, weaponSkel, weaponJoints, weaponBind, helpers, helper;
    RigIdentity owner{};
    Quaternion nativeWrist = Q(1,2,3,.7f), socketRelative = Q(3,1,2,-.3f);
    Quaternion bind = Q(1,0,2,.4f), authored = Q(2,1,0,-.8f), extra = Q(0,2,1,.2f);
    Quaternion barrel = Q(1,3,2,.6f), helperExtra = Q(2,0,1,-.2f);
    unsigned reads = 0;
    unsigned poseCount=6;

    std::uintptr_t Alloc(std::size_t size) {
        used = (used+7) & ~std::size_t(7);
        const auto p = dataBase + used;
        used += size;
        Check(used <= bytes.size(), "fixture allocation");
        return p;
    }
    template<class T> void Put(std::uintptr_t p, std::size_t offset, const T& v) {
        Check(p + offset >= dataBase && p + offset + sizeof(v) <= dataBase + bytes.size(), "fixture write range");
        std::memcpy(bytes.data() + p + offset - dataBase, &v, sizeof(v));
    }
    std::uintptr_t Array(unsigned count, unsigned stride) {
        const auto p = Alloc(8 + count*stride) + 8;
        Put(p-4, 0, std::uint32_t(count));
        return p;
    }
    std::uintptr_t Name(const char* name) {
        const auto p = Alloc(std::strlen(name)+1);
        std::memcpy(bytes.data()+p-dataBase, name, std::strlen(name)+1);
        return p;
    }
    static bool Read(void* context, std::uintptr_t p, void* out, std::size_t size) {
        auto& f = *static_cast<Fixture*>(context);
        ++f.reads;
        if (p < dataBase || p > dataBase + f.bytes.size() || size > dataBase + f.bytes.size() - p) { return false; }
        std::memcpy(out, f.bytes.data()+p-dataBase, size);
        return true;
    }
    Status Resolve(Basis& b) { return ReadBasis({this, Read}, base, owner, pose, poseCount, 2, b); }
    Fixture() {
        weapon = Alloc(0x420); mount = Alloc(0x170); binding = Alloc(0x20);
        arms = Alloc(0x80); skel = Alloc(0x80); joints = Array(6, 0xA8);
        bindPose = Array(6, 0x1C); pose = Array(6, 0x1C);
        weaponChar = Alloc(0x80); weaponSkel = Alloc(0x80); weaponJoints = Array(3, 0xA8);
        weaponBind = Array(3, 0x1C); helpers = Array(1, 8); helper = Alloc(0x170);
        owner = {weapon, mount, binding, arms, 1, 42};
        Put(weapon, 0x2B0, mount); Put(weapon, 0x2F0, Name("AMMO"));
        Put(mount, 0, base + 0x1D212B8); Put(mount, 0x20, binding); Put(mount, 0x28, arms+0x18);
        Put(mount, 0x15C, 3); Put(mount, 0x114, authored); Put(mount, 0x14C, extra);
        Put(binding, 0, base+0x1CB1328); Put(binding, 8, weaponChar);
        Put(arms, 0, base+0x1D22200); Put(arms, 0x10, skel);
        Put(arms, 0x18, base+0x1D22110); Put(arms, 0x30, arms);
        Put(skel, 0, base+0x1D2A3E8); Put(skel, 8, joints); Put(skel, 0x30, bindPose);
        Put(skel,0x20,std::uint32_t(6));
        // Live bind-pose slices are raw spans inside one allocation. The word
        // before this slice is the previous pose's float, not an array count.
        Put(bindPose-4,0,std::uint32_t(0xB9E50000));
        Put(pose-4,0,std::uint32_t(0xB9E57000));
        for (unsigned i = 0; i < 6; ++i) {
            Put(joints, i*0xA8+0x18, std::int16_t(-1));
            Put(bindPose, i*0x1C, Quaternion{}); Put(pose, i*0x1C, Quaternion{});
        }
        Put(joints, 3*0xA8+0x18, std::int16_t(2));
        Put(pose, 2*0x1C, nativeWrist); Put(pose, 3*0x1C, Multiply(nativeWrist, socketRelative));
        Put(bindPose, 3*0x1C, bind);
        Put(weaponChar, 0, base+0x1D22200); Put(weaponChar, 0x10, weaponSkel);
        Put(weaponChar, 0x18, base+0x1D22110); Put(weaponChar, 0x30, weaponChar);
        Put(weaponChar, 0x38, helpers);
        Put(weaponSkel, 0, base+0x1D2A3E8); Put(weaponSkel, 8, weaponJoints); Put(weaponSkel, 0x30, weaponBind);
        Put(weaponSkel,0x20,std::uint32_t(3));Put(weaponBind-4,0,std::uint32_t(0));
        for (unsigned i = 0; i < 3; ++i) {
            Put(weaponJoints, i*0xA8, Name(i == 1 ? "Ammo" : "root"));
            Put(weaponBind, i*0x1C, Q(1,0,0,-.9f)); // intentionally differs from attachment
        }
        Put(helpers, 0, helper); Put(helper, 0, base+0x1D212B8);
        Put(helper, 0x10, Name("aMmO")); Put(helper, 0x28, weaponChar+0x18);
        Put(helper, 0x114, barrel); Put(helper, 0x14C, helperExtra);
    }
};

void GeometryAndSwitches()
{
    unsigned cases = 0;
    // Different assets, arbitrary animated wrist/equip angles, rolled character
    // roots and XR aim poses. Check all three axes, not just yaw or forward.
    for (int asset = 0; asset < 3; ++asset) {
        Fixture f;
        f.authored = Q(2,1,3,.2f + asset*.6f);
        f.barrel = Q(1,4,2,-.7f + asset*.5f);
        f.Put(f.mount, 0x114, f.authored); f.Put(f.helper, 0x114, f.barrel);
        for (int equip = 0; equip < 4; ++equip) {
            f.owner.generation++;
            f.nativeWrist = Q(3,1,2,-1.1f+equip*.7f);
            f.Put(f.pose, 2*0x1C, f.nativeWrist);
            f.Put(f.pose, 3*0x1C, Multiply(f.nativeWrist, f.socketRelative));
            for (int sample = 0; sample < 9; ++sample) {
                const auto before = f.bytes;
                Basis b{};
                Check(f.Resolve(b) == Status::ready, "supported character binding resolves");
                Check(b.source == Source::attachmentDefault, "attachment wins over identically named joint");
                Check(f.bytes == before, "basis lookup is read-only");
                const Quaternion root = Q(3,1,4,.7f);
                Pose xrAim{Q(1,2,3,-1.5f+sample*.4f), {1,.4f,-.2f}};
                stereo::ReferenceFrame reference{};
                reference.yawRadians = -.6f;
                const Pose world = stereo::EyePoseInWorld(reference, xrAim);
                Quaternion solved{};
                Check(SolveWrist(root, world.orientation, b, solved), "solve valid basis");
                // Native forward reconstruction after the engine propagates
                // descendants: root * newSocket * inv(bind) * A * K * barrel.
                const auto socketAfter = Multiply(solved, f.socketRelative);
                const auto mountAfter = Multiply(Multiply(Multiply(socketAfter, Inv(f.bind)), f.authored), f.extra);
                const auto helperAfter = Multiply(Multiply(root, mountAfter), Multiply(f.barrel, f.helperExtra));
                Check(SameRotation(helperAfter, world.orientation), "full barrel frame matches aim after every equip angle");
                const PoseValidity valid{true,true,true,true,0};
                const auto ray = controller::AimFromController(reference, xrAim, valid, 200000000);
                Check(ray.has_value(), "reticle producer positive control");
                Check(Distance(Rotate(helperAfter, {0,1,0}), ray->direction) < 1e-4f,
                      "native helper +Y agrees with actual reticle producer's XR -Z");
                ++cases;
            }
        }
    }
    // Positive control for the reported bug: the old capture preserves an
    // arbitrary 45-degree equip angle after the controller returns to neutral.
    const auto equip = Q(0,0,1,.78539816f);
    const auto offset = animik::CalibrateRotationOffset(equip, Quaternion{});
    Check(Distance(Rotate(animik::ApplyRotationOffset(Quaternion{}, offset), {0,1,0}), {0,1,0}) > .7f,
          "old equip-angle defect is observable in this harness");
    std::cout << cases << " native-chain/reticle comparisons passed\n";
}

void LookupAndRefusals()
{
    Basis b{};
    Fixture f;
    Check(f.Resolve(b) == Status::ready, "baseline positive control");
    f.Put(f.helpers-4, 0, std::uint32_t(0));
    Check(f.Resolve(b) == Status::ready && b.source == Source::jointBind, "no matching attachment uses bind joint");
    Check(SameRotation(b.barrelInWeapon, Q(1,0,0,-.9f)), "joint fallback uses weapon bind pose");
    {
        Fixture melee;
        melee.Put(melee.weapon,0,Fixture::base+0x1E92F00);
        melee.Put(melee.weapon,0x2F0,melee.Name(""));
        Check(melee.Resolve(b)==Status::ready && b.source==Source::wrenchModel,
              "verified muzzle-free wrench uses authored model frame");
        Quaternion solved{};
        const auto aim=Q(1,2,3,.8f);
        Check(SolveWrist({},aim,b,solved) && SameRotation(Multiply(solved,b.weaponInWrist),aim),
              "melee frame follows controller without equip-angle capture");
        melee.Put(melee.weapon,0,Fixture::base+0x123);
        Check(melee.Resolve(b)==Status::missingHelper,"unknown empty-helper asset cannot become wrench policy");
    }
    {
        Fixture skins;
        const auto list=skins.Array(2,8), skin=skins.Alloc(0x40);
        skins.Put(skins.weaponChar,0x38,list);skins.Put(list,0,skin);skins.Put(list,8,skins.helper);
        skins.Put(skin,0,Fixture::base+0x1D1EDA8);skins.Put(skin,0x18,skins.Name("body"));
        Check(skins.Resolve(b)==Status::ready && b.source==Source::attachmentDefault,
              "unrelated skin uses its concrete name getter before muzzle lookup");
        skins.Put(skin,0x18,skins.Name("ammo"));
        Check(skins.Resolve(b)==Status::unsupportedHelper,"matching skin retains native lookup precedence");
    }
    {
        Fixture disruptor;
        disruptor.Put(disruptor.weapon,0,Fixture::base+0x1E31290);
        disruptor.Put(disruptor.helper,0x10,disruptor.Name("fx_muzzle"));
        Check(disruptor.Resolve(b)==Status::ready && std::strcmp(b.helper,"fx_muzzle")==0,
              "Disruptor uses its measured barrel-facing effects frame");
    }
    f.Put(f.mount, 0x15C, 2);
    Check(f.Resolve(b) == Status::ready, "direct wrist mount supported");
    f.Put(f.mount, 8, std::uint32_t(0x4000));
    const auto adjustedRelative = Q(1,2,1,-1.1f);
    f.Put(f.mount, 0xF8, adjustedRelative);
    Check(f.Resolve(b) == Status::ready && SameRotation(b.weaponInWrist, Multiply(adjustedRelative, f.extra)),
          "projected mount uses actual relative field, including later native adjustment");

    auto refusal = [&](auto mutate, Status expected, const char* why) {
        Fixture bad;
        Check(bad.Resolve(b) == Status::ready, "negative case positive control");
        mutate(bad);
        const auto before = bad.bytes;
        Check(bad.Resolve(b) == expected, why);
        Check(b.source == Source::none && b.helper[0] == 0, "refusal clears previous basis");
        Check(bad.bytes == before, "refusal never writes engine memory");
    };
    refusal([](Fixture& x) { x.owner.generation = 0; }, Status::invalidOwner, "missing owner generation");
    refusal([](Fixture& x) { x.Put(x.weapon, 0x2B0, std::uintptr_t(0)); }, Status::invalidOwner, "weapon switched attachment");
    refusal([](Fixture& x) { x.Put(x.mount, 0x20, x.binding+8); }, Status::invalidOwner, "binding replaced during equip");
    refusal([](Fixture& x) { x.Put(x.arms, 0x30, x.weaponChar); }, Status::invalidOwner, "foreign attachment manager");
    refusal([](Fixture& x) { x.Put(x.binding, 0, Fixture::base+0x1CB12A0); }, Status::unsupportedBinding, "static binding requires its own proof");
    refusal([](Fixture& x) { x.Put(x.skel, 0, Fixture::base+0x10); }, Status::invalidSkeleton, "wrong concrete skeleton");
    refusal([](Fixture& x) { x.Put(x.mount, 0x30, std::uint8_t(1)); }, Status::simulatedMount, "simulation changes mount formula");
    refusal([](Fixture& x) { x.Put(x.mount, 0x33, std::uint8_t(1)); }, Status::simulatedMount, "redirect changes mount formula");
    refusal([](Fixture& x) { x.Put(x.joints, 3*0xA8+0x18, std::int16_t(0)); }, Status::unrelatedSocket, "other limb socket");
    refusal([](Fixture& x) { x.Put(x.joints, 3*0xA8+0x18, std::int16_t(3)); }, Status::unrelatedSocket, "cyclic hierarchy bounded");
    refusal([](Fixture& x) { x.Put(x.mount, 0x15C, 769); }, Status::unrelatedSocket, "out-of-bounds socket");
    refusal([](Fixture& x) { x.poseCount=2; }, Status::invalidMount, "truncated current pose");
    refusal([](Fixture& x) { x.Put(x.skel,0x20,std::uint32_t(2)); }, Status::invalidMount, "bind span is bounded by its actual owning count");
    refusal([](Fixture& x) { x.Put(x.mount, 0x14C, Quaternion{0,0,0,0}); }, Status::invalidMount, "zero extra rotation rejected before normalization");
    refusal([](Fixture& x) { x.Put(x.helper, 0x114, Quaternion{0,0,0,std::numeric_limits<float>::quiet_NaN()}); }, Status::invalidBasis, "NaN basis rejected");
    refusal([](Fixture& x) { x.Put(x.weapon, 0x2F0, x.Name("absent")); }, Status::missingHelper, "missing name cannot turn into identity");
    refusal([](Fixture& x) { x.Put(x.helper, 0, Fixture::base+0x10); }, Status::unsupportedHelper, "unknown attachment cannot fall through to matching joint");
    refusal([](Fixture& x) { x.Put(x.helper, 0x30, std::uint8_t(1)); }, Status::unsupportedHelper, "simulated helper unsupported");
    refusal([](Fixture& x) { x.Put(x.helper, 0x15C, -1); }, Status::unsupportedHelper, "unresolved helper joint cannot supply a barrel frame");
    refusal([](Fixture& x) { x.Put(x.mount, 8, std::uint32_t(0x4000)); }, Status::invalidMount, "projected mount with invalid stored relative rotation");
    refusal([](Fixture& x) { x.Put(x.helpers-4, 0, std::uint32_t(257)); }, Status::missingHelper, "attachment scan bounded");
    refusal([](Fixture& x) { x.Put(x.weapon, 0x2F0, std::uintptr_t(1)); }, Status::missingHelper, "inaccessible string guarded");
    f = Fixture{};
    f.Put(f.joints-4, 0, std::uint32_t(0x80000006));
    Check(f.Resolve(b) == Status::ready, "DynArray capacity flag is masked like native getter");
    Quaternion untouched = Q(1,0,0,.2f), out = untouched;
    Check(!SolveWrist({}, {0,0,0,0}, b, out) && SameRotation(out, untouched), "invalid aim leaves output untouched");
    Check(ReadBasis({}, Fixture::base, f.owner, f.pose, f.poseCount, 2, b) == Status::invalidOwner,
          "missing reader refuses safely");
}
} // namespace

int main()
{
    GeometryAndSwitches();
    LookupAndRefusals();
    std::cout << "weapon rig alignment fixtures passed (static/offline; no game)\n";
}
