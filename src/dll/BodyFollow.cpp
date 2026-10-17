#include "BodyFollow.h"

#include "AnimIkTakeover.h"
#include "HeadTrackingHook.h"
#include "HudBridge.h"
#include "InputPost.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "MoveLane.h"
#include "SceneQuery.h"
#include "WristLane.h"
#include "XrInput.h"
#include "preyvr/BodyFollow.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/StereoCamera.h"

#include <MinHook.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>

namespace preyvr::dll {
namespace {

void Log(const std::string& line) { lifecycle::Log("preyvr_body " + line); }

// Steam PreyDll (SHA 7d6e322f...). EGS names, found with build/jordi-mock/re/egs2steam.py:
//   ArkPlayerCamera::PrePhysicsUpdate(float)                 EGS 0x145D780
//   ArkPlayerMovementFSM::LimitRotation(Quat&) const         EGS 0x1570600
//   ArkPlayer::GetInstance()                                 EGS 0x154FA60
//   CEntity::SetPos(const Vec3&, int, bool, bool)            EGS 0x90B930
constexpr std::uintptr_t kPrePhysicsRva = 0x148A250;
constexpr std::array<std::uint8_t, 24> kPrePhysicsPrologue{
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x70, 0x20, 0x57, 0x48, 0x81, 0xEC, 0xF0,
    0x00, 0x00, 0x00, 0x44, 0x0F, 0x29, 0x58, 0x98, 0x48, 0x8B, 0xF1, 0x44};
constexpr std::uintptr_t kLimitRotationRva = 0x159D4C0;
constexpr std::array<std::uint8_t, 24> kLimitRotationPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x70, 0x8B, 0x01,
    0x48, 0x8B, 0xFA, 0xFF, 0xC8, 0x48, 0x8B, 0xD9, 0x83, 0xF8, 0x10, 0x77};
constexpr std::uintptr_t kGetPlayerRva = 0x157C990;
constexpr std::uintptr_t kSetPosRva = 0x90AF40;
constexpr std::array<std::uint8_t, 24> kSetPosPrologue{
    0xF3, 0x0F, 0x10, 0x02, 0x45, 0x8B, 0xD0, 0x0F, 0x2E, 0x41, 0x38, 0x75,
    0x16, 0xF3, 0x0F, 0x10, 0x42, 0x04, 0x0F, 0x2E, 0x41, 0x3C, 0x75, 0x0B};

// ArkPlayer: the camera at +0x12A0 (ArkPlayerCamera::UpdateView's this); its
// rotation at +0x150 is what ArkPlayer::GetViewRotation returns (+0x13F0) and what
// PrePhysicsUpdate integrates the look input into.
constexpr std::size_t kPlayerCamera = 0x12A0;
constexpr std::size_t kCameraRotation = 0x150;
constexpr std::size_t kPlayerEntity = 0x38;     // IEntity*
constexpr std::size_t kPlayerFsm = 0x58;        // ArkPlayerMovementFSM; its state id first
constexpr std::size_t kEntityPos = 0x38;        // CEntity::m_vPos (no parent: world)
// ArkPlayer::m_stance (EStance; ArkPlayer::GetStance, Steam 0x157CE70, reads it).
constexpr std::size_t kPlayerStance = 0x1838;
constexpr int kStanceStand = 0, kStanceSneak = 1;

// EArkPlayerMovementStateId. The body turns in the states where the player
// turns freely, and walks only standing on the ground.
constexpr int kStateGround = 1, kStateJump = 4, kStateFall = 5, kStateSlide = 11;

using PrePhysicsFn = void(__fastcall*)(std::uint8_t* camera, float frameTime);
using LimitRotationFn = void(__fastcall*)(const void* fsm, Quaternion* rotation);
using GetPlayerFn = std::uint8_t*(__fastcall*)();
using SetPosFn = void(__fastcall*)(void* entity, const Vec3* pos, int why, bool recalc, bool force);

std::uintptr_t gBase = 0;
std::atomic<PrePhysicsFn> gOriginal{nullptr};
bool gInstalled = false;
std::mutex gInstallMutex;

// --- options (any thread) ----------------------------------------------------
// The VR option (body_follow): on unless the player turns it off. Nothing runs
// before VrMode installs the hook, nor without a head-tracking reference.
std::atomic<bool> gYawOn{true}, gRoomOn{true};
std::atomic<int> gDeadzoneDeci{0};        // decidegrees
std::atomic<int> gRateDeg{0};             // degrees per second while catching up; 0 = at once
std::atomic<int> gNeckUpMm{75}, gNeckForwardMm{80};
std::atomic<int> gRadiusMm{250};
std::atomic<int> gStepMm{220};            // highest step up or down the body takes (a stair)
std::atomic<int> gRoomStartMm{10}, gRoomStopMm{3};
// The lean radius: the head may leave the body's neck by this much before the
// body walks after it (Quake VR's vr_lean_radius; the visible body bends to
// reach it). Inside it the body comes back under the head at the head's own
// speed (at least 0.5 m/s) unless the player is leaning.
std::atomic<int> gLeanRadiusMm{250}, gRecentreMmps{500};
// The physical crouch: the head this far below standing crouches the game,
// back above the second stands it (Meta's 0.30 m; hysteresis).
std::atomic<bool> gCrouchOn{true};
std::atomic<int> gCrouchDownMm{300}, gCrouchUpMm{200};
std::atomic<bool> gFadeOn{true};
std::atomic<int> gFadeMarginMm{100}, gFadeDepthMm{100};  // the head-in-a-wall fade: from 10 cm off the surface, black at it
std::atomic<float> gFadeTarget{0.0f};
std::atomic<std::uint64_t> gFadeNs{0};    // when the target was last computed: a stale one fades out

// --- state (game thread: PrePhysicsUpdate and UpdateView run on it in turn) --
struct Written { float delta = 0; float yaw = 0; unsigned long long generation = 0; bool active = false; };
Written gWritten;                          // the turn written this frame, verified next frame
float gAwaitingYaw = 0;                    // verified turns the view will show at its next update
unsigned long long gAwaitingGeneration = 0;
Vec3 gMoveWorld{};                         // the SetPos made this frame (as commanded), for this frame's view
unsigned long long gMoveGeneration = 0;
bool gMovePending = false;
int gMoveState = 0;                        // the movement state when it was made
std::uint64_t gLastMoveFrame = 0;          // the PrePhysicsUpdate of the last SetPos
Vec3 gPivotAtView{}; bool gHavePivotAtView = false;   // the pivot at the last view update
int gPushBackFrames = 0;                   // the physics refused our last move: hold off walking
Vec2 gPushBackDir{};
bool gCatching = false, gRoomMoving = false;
float gPlayYaw = 0; bool gHavePlayYaw = false;   // the view's play yaw, last update
Vec3 gEye{};                                     // the game's eye relative to the pivot, last update
bool gHaveEye = false;
bool gFollowing = false;                         // this frame's PrePhysicsUpdate had the body following
Vec3 gNeckBefore{}; bool gHaveNeckBefore = false;  // the player's neck last frame (tracking), for its speed
float gNeckSpeed = 0;                            // m/s, smoothed
Vec2 gNeckVelocity{};                            // m/s (tracking x, z), smoothed
// The physical crouch. Owned: the game is crouched (or standing back up)
// because of it, so the game's eye height change is ours to take out of the
// view -- the camera stays at the player's real head.
bool gCrouchOwned = false, gCrouchSeen = false;
std::uint64_t gCrouchAskedNs = 0;
// The view's eye height while the crouch is ours: the game's eye stands in for
// the head, so a crouch the player made for real must not lower it a second
// time. Learned standing (the game's eye above the pivot), applied as the
// difference (CrouchEyeLift).
float gStandingEye = 0; bool gHaveStandingEye = false;
std::atomic<float> gEyeLift{0.0f};
int gEyeStill = 0;                               // frames the game's eye height has held still

// --- report -----------------------------------------------------------------
std::atomic<unsigned long long> gCalls{0}, gYawWrites{0}, gYawAbsorbed{0}, gYawRejected{0}, gYawTakeover{0};
std::atomic<unsigned long long> gYawLimited{0}, gMoves{0}, gWalks{0}, gBlocked{0}, gSlid{0}, gNoGround{0}, gAbsorbFailed{0};
std::atomic<unsigned long long> gPushedBack{0}, gSettled{0}, gCrouchTaps{0}, gLeanHolds{0};
std::atomic<int> gYawErrorMilli{0}, gNeckOffsetMm{0}, gGroundMm{0}, gGates{0}, gState{0};
std::atomic<int> gStance{0}, gDropMm{0}, gCrouchOwnedFlag{0};

// --- trace (research and the mock tests) --------------------------------------
std::atomic<std::uint64_t> gFrame{0};
std::atomic<bool> gScanPending{false};
std::mutex gScanMutex;
std::string gScanResult;

struct TraceEvent {
    char kind = 0;                 // C: the composed camera, once per view update
    std::uint64_t frame = 0;
    Vec3 pivot{}, view{};
    float yawRotation = 0, yawView = 0;
    int state = 0;
    // C only: the play yaw the view used, the world anchor (camera minus the
    // head's own placement: constant while only the player moves) and the head.
    float playYaw = 0;
    Vec3 anchor{}, camera{}, head{}, gameEye{};
    Vec3 handR{}, handL{};             // the drawn wrists (tracking space), from the IK's own publication
    Vec3 gripR{}, gripL{};             // the controllers (tracking space)
    Quaternion gripRQ{}, gripLQ{};
    long long armTime = 0;
    Quaternion headRotation{};
    float headYaw = 0;
    Vec2 offset{}, walk{};             // the neck's offset from the body and the walk asked this frame (tracking)
    int lean = 0;
};
Vec2 gLastOffset{}, gLastWalk{};
int gLastLean = 0;
std::mutex gTraceMutex;
std::array<TraceEvent, 1500> gTrace{};
unsigned gTraceCount = 0;
std::atomic<bool> gTracing{false};

float YawOf(const Quaternion& q)
{
    const Vec3 forward = Rotate(Normalize(q), Vec3{0.0f, 1.0f, 0.0f});
    return std::atan2(-forward.x, forward.y);
}

bool ReadVec3(const std::uint8_t* at, Vec3& out)
{
    __try { std::memcpy(&out, at, sizeof(out)); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::uint8_t* CurrentPlayer()
{
    return gBase != 0 ? reinterpret_cast<GetPlayerFn>(gBase + kGetPlayerRva)() : nullptr;
}

void StartTrace()
{
    std::lock_guard lock(gTraceMutex);
    gTraceCount = 0;
    gTracing = true;
}

// --- the move ----------------------------------------------------------------
//
// Rays at three heights stand in for the capsule: from the pivot's axis out
// along the step for radius + step. A hit slides the step along the wall once;
// a slide that still hits is refused. The floor is then found under the
// destination and the pivot keeps its height above it, so a step up or down
// is taken and a drop or a wall-high ledge is not.
enum class Path { clear, blocked, unknown };

Path ClearAlong(std::uintptr_t player, const Vec3& pivot, float eyeHeight, Vec2 step, Vec2& normal)
{
    const float length = std::sqrt(step.x * step.x + step.y * step.y);
    if (!(length > 1e-5f)) { return Path::clear; }
    const float radius = gRadiusMm.load() / 1000.0f;
    const Vec2 dir{step.x / length, step.y / length};
    const Vec2 side{-dir.y, dir.x};
    struct Ray { float height, lateral, reach; };
    // The lowest ray just clears the highest step the body may take, so anything
    // taller is a wall and anything lower is floor to step onto. Then the waist
    // and the chest on the axis, and two rays at the capsule's flanks (0.8 r out,
    // where the circle's front is 0.6 r ahead) so a corner or a post beside the
    // path is not walked into between the axis rays.
    const float low = gStepMm.load() / 1000.0f + 0.03f;
    const float waist = std::fmax(low + 0.1f, eyeHeight * 0.55f), chest = std::fmax(waist + 0.1f, eyeHeight * 0.85f);
    const Ray rays[] = {{low, 0, radius}, {waist, 0, radius}, {chest, 0, radius},
                        {waist, 0.8f * radius, 0.6f * radius}, {waist, -0.8f * radius, 0.6f * radius}};
    Path path = Path::clear;
    float nearest = 1e9f;
    for (const Ray& r : rays) {
        const Vec3 a{pivot.x + side.x * r.lateral, pivot.y + side.y * r.lateral, pivot.z + r.height};
        const float reach = r.reach + length;
        const Vec3 b{a.x + dir.x * reach, a.y + dir.y * reach, a.z};
        scene::Hit hit{};
        if (!QueryBodySegment(player, a, b, hit)) { return Path::unknown; }
        if (hit.distance >= 0.0f && hit.distance < reach) {
            path = Path::blocked;
            if (hit.distance < nearest) {
                nearest = hit.distance;
                const float n = std::sqrt(hit.normal.x * hit.normal.x + hit.normal.y * hit.normal.y);
                normal = n > 0.2f ? Vec2{hit.normal.x / n, hit.normal.y / n} : Vec2{-dir.x, -dir.y};
            }
        }
    }
    return path;
}

bool GroundBelow(std::uintptr_t player, const Vec3& at, float span, float& ground)
{
    const Vec3 a{at.x, at.y, at.z + span};
    const Vec3 b{at.x, at.y, at.z - span};
    scene::Hit hit{};
    if (!QueryBodySegment(player, a, b, hit) || hit.distance < 0.0f) { return false; }
    ground = a.z - hit.distance;
    return true;
}

bool ResolveMove(std::uintptr_t player, const Vec3& pivot, float eyeHeight, Vec2 step, Vec3& to)
{
    Vec2 normal{};
    const Path path = ClearAlong(player, pivot, eyeHeight, step, normal);
    if (path == Path::unknown) { return false; }
    if (path == Path::blocked) {
        const Vec2 slid = body::Slide(step, normal);
        Vec2 again{};
        if (slid.x * slid.x + slid.y * slid.y < 1e-8f ||
            ClearAlong(player, pivot, eyeHeight, slid, again) != Path::clear) {
            gBlocked.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        gSlid.fetch_add(1, std::memory_order_relaxed);
        step = slid;
    }
    const float span = gStepMm.load() / 1000.0f + 0.05f;
    float here = 0, there = 0;
    const Vec3 dest{pivot.x + step.x, pivot.y + step.y, pivot.z};
    if (!GroundBelow(player, pivot, span, here) || !GroundBelow(player, dest, span, there)) {
        gNoGround.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    gGroundMm.store(static_cast<int>((pivot.z - here) * 1000.0f), std::memory_order_relaxed);
    // A real step is taken; a rug's edge or the floor's own unevenness is left to
    // the physics, which settles the capsule without the camera hopping.
    const float rise = there - here;
    if (std::fabs(rise) > gStepMm.load() / 1000.0f) {
        gNoGround.fetch_add(1, std::memory_order_relaxed);   // a ledge up or a drop down: not a step
        return false;
    }
    if (std::fabs(rise) > 0.03f) {
        // A step is a tread, not an edge: the floor half a radius further on must
        // be at the same height, or this is the lip of a box, a pipe, a bed's frame.
        const float length = std::sqrt(step.x * step.x + step.y * step.y);
        const float further = gRadiusMm.load() / 2000.0f;
        const Vec3 beyond{dest.x + step.x / length * further, dest.y + step.y / length * further, pivot.z};
        float tread = 0;
        if (!GroundBelow(player, beyond, span, tread) || std::fabs(tread - there) > 0.03f) {
            gNoGround.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }
    to = {dest.x, dest.y, std::fabs(rise) > 0.03f ? pivot.z + rise : pivot.z};
    return true;
}

// --- the frame ---------------------------------------------------------------
bool KeptWrite(float current, const Written& w)
{
    // Kept, reverted, or replaced: the yaw is nearest the written one, nearest
    // the one before the write, or far from both (the game set its own).
    const float kept = std::fabs(body::Wrap(current - w.yaw));
    const float reverted = std::fabs(body::Wrap(current - (w.yaw - w.delta)));
    if (kept > 0.0873f && reverted > 0.0873f) { gYawTakeover.fetch_add(1, std::memory_order_relaxed); return false; }
    if (kept > reverted) { gYawRejected.fetch_add(1, std::memory_order_relaxed); return false; }
    return true;
}

// The physical crouch: crouching for real crouches the game, standing up
// stands it, through the crouch button (the path the old Chairloader mod
// verified; ArkPlayerMovementFSM::TrySetStance does nothing), guarded by the
// stance so the button stays the player's too. While it owns the stance the
// view keeps the camera at the real head (NoteGameView).
void UpdateCrouch(const std::uint8_t* player, const Vec3& neck, const Vec3& origin, int state)
{
    const int stance = *reinterpret_cast<const int*>(player + kPlayerStance);
    gStance.store(stance, std::memory_order_relaxed);
    // How far the neck is below where it stands (the calibrated eye, level):
    // the neck, so looking down does not count as crouching.
    const float drop = origin.y - gNeckUpMm.load() / 1000.0f - neck.y;
    gDropMm.store(static_cast<int>(drop * 1000.0f), std::memory_order_relaxed);
    const std::uint64_t now = MonotonicNanoseconds();
    const bool on = gCrouchOn.load(std::memory_order_relaxed);
    const float down = gCrouchDownMm.load() / 1000.0f, up = gCrouchUpMm.load() / 1000.0f;
    if (stance == kStanceSneak) { gCrouchSeen = true; }
    if (!gCrouchOwned) {
        if (on && stance == kStanceStand && drop > down && state == kStateGround) {
            RequestCrouchTap();
            gCrouchTaps.fetch_add(1, std::memory_order_relaxed);
            gCrouchOwned = true;
            gCrouchSeen = false;
            gCrouchAskedNs = now;
        }
    } else if (drop < up || !on) {
        if (stance == kStanceSneak) {
            // Stand back up -- again every half second while something overhead stops it.
            if (now - gCrouchAskedNs > 500000000ull && state == kStateGround) {
                RequestCrouchTap();
                gCrouchTaps.fetch_add(1, std::memory_order_relaxed);
                gCrouchAskedNs = now;
            }
        } else if (stance == kStanceStand && gHaveEye && gEye.z > gStandingEye - 0.01f && gEyeStill > 10) {
            // Stood up, and the game's eye is back at standing height: the stance
            // flips a frame before the eye starts rising, so not before.
            gCrouchOwned = false;
        }
    } else if (stance == kStanceStand && !gCrouchSeen && now - gCrouchAskedNs > 700000000ull &&
               state == kStateGround) {
        // Still low and the tap was not taken (input blocked that frame): once more.
        RequestCrouchTap();
        gCrouchTaps.fetch_add(1, std::memory_order_relaxed);
        gCrouchAskedNs = now;
        gCrouchSeen = true;   // no third try; the player can press the button
    }
    gCrouchOwnedFlag.store(gCrouchOwned ? 1 : 0, std::memory_order_relaxed);
}

void FollowAfterGame(std::uint8_t* camera, std::uint8_t* player, float frameTime)
{
    auto* const entity = *reinterpret_cast<std::uint8_t**>(player + kPlayerEntity);
    if (entity == nullptr) { return; }
    const int state = *reinterpret_cast<const int*>(player + kPlayerFsm);
    gState.store(state, std::memory_order_relaxed);

    TrackingFrame tracking{};
    float roomYaw = 0;
    Vec3 origin{};
    unsigned long long generation = 0;
    const bool gameplay = HudGameplayInputAllowed() && MoveLaneCinematicGate() == 0;
    const bool head = TryGetTrackingFrame(tracking) && FreshSample(MonotonicNanoseconds(), tracking.publishedNs) &&
                      IsPoseUsable(tracking.head, tracking.headValidity, 200000000ull);
    const bool room = ReadHeadTrackingRoom(roomYaw, origin, generation);
    const bool turning = state == kStateGround || state == kStateJump || state == kStateFall || state == kStateSlide;
    gGates.store((gameplay ? 1 : 0) | (head ? 2 : 0) | (room ? 4 : 0) | (turning ? 8 : 0) |
                 (state == kStateGround ? 16 : 0) | (gHavePlayYaw ? 32 : 0), std::memory_order_relaxed);
    gFollowing = gameplay && head && room && turning && gYawOn.load(std::memory_order_acquire);
    if (!gameplay || !head || !room) { gCatching = false; gRoomMoving = false; return; }
    if (gAwaitingGeneration != generation) { gAwaitingYaw = 0; }
    UpdateCrouch(player, body::NeckPoint(tracking.head, gNeckUpMm.load() / 1000.0f, gNeckForwardMm.load() / 1000.0f),
                 origin, state);

    // Yaw: turn the body to the head, through the game's own rotation limits.
    const float bodyYawBefore = roomYaw + gAwaitingYaw;   // the body's heading in tracking space
    float bodyYaw = bodyYawBefore;
    const auto heading = body::HeadHeading(tracking.head.orientation);
    if (heading) {
        const float error = body::Wrap(*heading - bodyYaw);
        gYawErrorMilli.store(static_cast<int>(error * 57295.78f), std::memory_order_relaxed);
        if (gYawOn.load(std::memory_order_acquire) && turning) {
            body::YawPolicy policy{gDeadzoneDeci.load() * 0.00174532925f, gRateDeg.load() * 0.0174532925f};
            const float step = body::YawStep(error, frameTime, policy, gCatching);
            if (std::fabs(step) > 1e-5f) {
                Quaternion before{};
                std::memcpy(&before, camera + kCameraRotation, sizeof(before));
                Quaternion after = Normalize(Multiply(stereo::YawQuaternion(step), before));
                reinterpret_cast<LimitRotationFn>(gBase + kLimitRotationRva)(player + kPlayerFsm, &after);
                const float written = body::Wrap(YawOf(after) - YawOf(before));
                if (std::fabs(written - step) > 1e-4f) { gYawLimited.fetch_add(1, std::memory_order_relaxed); }
                if (std::fabs(written) > 1e-6f) {
                    std::memcpy(camera + kCameraRotation, &after, sizeof(after));
                    gWritten = {written, YawOf(after), generation, true};
                    gYawWrites.fetch_add(1, std::memory_order_relaxed);
                    bodyYaw += written;
                }
            }
        } else {
            gCatching = false;
        }
    }

    // Room: keep the body under the neck. The neck sits `forward` behind the eye
    // anchor along the body's heading, so a turn of the body swings it round the
    // anchor; the turn just written is paired with the move that undoes that swing
    // -- the body turns about its neck, as the player's does -- every frame and
    // without a deadzone. Walking the rest of the way is the deadzoned part.
    const float up = gNeckUpMm.load() / 1000.0f, forward = gNeckForwardMm.load() / 1000.0f;
    const Vec3 neck = body::NeckPoint(tracking.head, up, forward);
    const Vec3 before = body::NeckAnchor(origin, bodyYawBefore, forward);
    const Vec3 after = body::NeckAnchor(origin, bodyYaw, forward);
    const Vec2 swing{before.x - after.x, before.z - after.z};
    const Vec2 offset{neck.x - before.x, neck.z - before.z};
    gNeckOffsetMm.store(static_cast<int>(std::sqrt(offset.x * offset.x + offset.y * offset.y) * 1000.0f),
                        std::memory_order_relaxed);
    if (state != kStateGround || !gHavePlayYaw || gMovePending) {
        gRoomMoving = false;
        return;
    }
    Vec2 step = swing;
    if (gHaveNeckBefore && frameTime > 0.0f) {
        const float moved = std::sqrt((neck.x - gNeckBefore.x) * (neck.x - gNeckBefore.x) +
                                      (neck.z - gNeckBefore.z) * (neck.z - gNeckBefore.z));
        const float k = 1.0f - std::exp(-frameTime / 0.15f);
        gNeckSpeed += (std::min(moved / frameTime, 3.0f) - gNeckSpeed) * k;
        gNeckVelocity.x += ((neck.x - gNeckBefore.x) / frameTime - gNeckVelocity.x) * k;
        gNeckVelocity.y += ((neck.z - gNeckBefore.z) / frameTime - gNeckVelocity.y) * k;
    }
    gNeckBefore = neck;
    gHaveNeckBefore = true;
    if (gRoomOn.load(std::memory_order_acquire)) {
        body::LeanPolicy policy{};
        policy.radius = gLeanRadiusMm.load() / 1000.0f;
        policy.recentre = gRecentreMmps.load() / 1000.0f;
        policy.start = gRoomStartMm.load() / 1000.0f;
        policy.stop = gRoomStopMm.load() / 1000.0f;
        // The neck stands `up` below the calibrated eye (origin, less what a crouch took out).
        const float neckDrop = origin.y - up - neck.y;
        const bool leaning = body::Leaning(tracking.head.orientation,
            Vec2{offset.x + gNeckVelocity.x * 0.1f, offset.y + gNeckVelocity.y * 0.1f}, neckDrop);
        if (leaning) { gLeanHolds.fetch_add(1, std::memory_order_relaxed); }
        Vec2 walk = body::LeanStep(offset, gNeckSpeed, frameTime, leaning, policy, gRoomMoving);
        gLastOffset = offset;
        gLastWalk = walk;
        gLastLean = (leaning ? 1 : 0) | (gRoomMoving ? 2 : 0);
        if (gPushBackFrames > 0) {
            // The physics just refused a move this way: do not keep pushing into
            // it (the body would shiver against the obstacle), but a step away
            // from it is taken at once.
            --gPushBackFrames;
            const float l = std::sqrt(walk.x * walk.x + walk.y * walk.y);
            if (l > 0.0f && (walk.x * gPushBackDir.x + walk.y * gPushBackDir.y) / l > 0.3f) { walk = {}; }
        }
        if (walk.x != 0.0f || walk.y != 0.0f) { gWalks.fetch_add(1, std::memory_order_relaxed); }
        step = {step.x + walk.x, step.y + walk.y};
    } else {
        gRoomMoving = false;
    }
    if (step.x == 0.0f && step.y == 0.0f) { return; }
    const Vec3 world = body::TrackingToWorld(Vec3{step.x, 0.0f, step.y}, gPlayYaw);
    Vec3 pivot{};
    if (!ReadVec3(entity + kEntityPos, pivot)) { return; }
    const float eyeHeight = gEye.z > 0.5f && gEye.z < 2.5f ? gEye.z : 1.7f;
    Vec3 to{};
    if (!ResolveMove(reinterpret_cast<std::uintptr_t>(player), pivot, eyeHeight, Vec2{world.x, world.y}, to)) {
        return;
    }
    reinterpret_cast<SetPosFn>(gBase + kSetPosRva)(entity, &to, 0, false, false);
    Vec3 now{};
    if (!ReadVec3(entity + kEntityPos, now)) { return; }
    // Absorb what the entity actually did, horizontally; a step up or down is
    // the game's to show, like any change of floor height.
    gMoveWorld = {now.x - pivot.x, now.y - pivot.y, 0.0f};
    gMoveGeneration = generation;
    gMovePending = true;
    gMoveState = state;
    gLastMoveFrame = gFrame.load(std::memory_order_relaxed);
    gMoves.fetch_add(1, std::memory_order_relaxed);
}

void __fastcall PrePhysicsDetour(std::uint8_t* camera, float frameTime)
{
    std::uint8_t* const player = CurrentPlayer();
    const bool ours = camera != nullptr && player != nullptr && player + kPlayerCamera == camera;
    // The turn written last frame: kept by the game (the view shows it at its
    // next update, which absorbs it) or not (nothing to absorb).
    if (ours && gWritten.active) {
        Quaternion current{};
        std::memcpy(&current, camera + kCameraRotation, sizeof(current));
        if (KeptWrite(YawOf(current), gWritten)) {
            if (gAwaitingGeneration != gWritten.generation) { gAwaitingYaw = 0; }
            gAwaitingYaw += gWritten.delta;
            gAwaitingGeneration = gWritten.generation;
        }
    }
    gWritten.active = false;
    gFollowing = false;

    const PrePhysicsFn original = gOriginal.load(std::memory_order_acquire);
    if (original != nullptr) { original(camera, frameTime); }
    gCalls.fetch_add(1, std::memory_order_relaxed);
    gFrame.fetch_add(1, std::memory_order_relaxed);
    if (!ours) { return; }
    if (gScanPending.exchange(false, std::memory_order_acq_rel)) {
        // Distances from the pivot's axis at waist height to the nearest geometry,
        // every 45 degrees from the body's facing (counter-clockwise), and the floor.
        std::ostringstream out;
        out.setf(std::ios::fixed); out.precision(3);
        auto* const entity = *reinterpret_cast<std::uint8_t**>(player + kPlayerEntity);
        Vec3 pivot{};
        Quaternion rotation{};
        std::memcpy(&rotation, camera + kCameraRotation, sizeof(rotation));
        if (entity != nullptr && ReadVec3(entity + kEntityPos, pivot)) {
            const float yaw = YawOf(rotation);
            for (int i = 0; i < 8; ++i) {
                const float a = yaw + i * 0.785398163f;
                const Vec3 from{pivot.x, pivot.y, pivot.z + 0.9f};
                const Vec3 to{from.x - std::sin(a) * 2.9f, from.y + std::cos(a) * 2.9f, from.z};
                scene::Hit hit{};
                const bool ran = QueryBodySegment(reinterpret_cast<std::uintptr_t>(player), from, to, hit);
                out << " d" << i * 45 << '=' << (ran ? hit.distance : -9.0f);
            }
            float ground = 0;
            out << " ground=" << (GroundBelow(reinterpret_cast<std::uintptr_t>(player), pivot, 0.5f, ground) ? pivot.z - ground : -9.0f)
                << " pivot=" << pivot.x << ',' << pivot.y << ',' << pivot.z << " yaw=" << yaw * 57.29578f;
        }
        std::lock_guard lock(gScanMutex);
        gScanResult = out.str();
    }
    FollowAfterGame(camera, player, frameTime);
}

} // namespace

DWORD EnsureBodyFollowHook()
{
    std::lock_guard lock(gInstallMutex);
    if (gInstalled) { return 0; }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) { return 1; }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);
    const auto matches = [&](std::uintptr_t rva, const auto& bytes) {
        return std::memcmp(reinterpret_cast<const void*>(base + rva), bytes.data(), bytes.size()) == 0;
    };
    if (!matches(kPrePhysicsRva, kPrePhysicsPrologue) || !matches(kSetPosRva, kSetPosPrologue) ||
        !matches(kLimitRotationRva, kLimitRotationPrologue)) {
        Log("result=unavailable detail=prologue_mismatch");
        return 2;
    }
    gBase = base;
    EnsureMinHook();
    PrePhysicsFn original = nullptr;
    void* const target = reinterpret_cast<void*>(base + kPrePhysicsRva);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&PrePhysicsDetour),
                      reinterpret_cast<void**>(&original)) != MH_OK) {
        Log("result=failed detail=create_hook");
        return 3;
    }
    gOriginal.store(original, std::memory_order_release);
    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        Log("result=failed detail=enable_hook");
        return 4;
    }
    gInstalled = true;
    Log("result=0 detail=hook_installed target=ArkPlayerCamera::PrePhysicsUpdate");
    return 0;
}

void NoteGameView(const Vec3& eye, const Quaternion& rotation)
{
    // The game's eye sits ~1 cm ahead of the pivot in the body's frame, so a body
    // turning with the head swings it -- the camera would slide ~1 cm per 60
    // degrees of head turn. While the body follows, horizontal changes of the eye
    // are taken out of the reference like the body's own turn and move.
    Vec3 eyeShift{};
    if (gFollowing && gHaveEye) { eyeShift = {eye.x - gEye.x, eye.y - gEye.y, 0.0f}; }
    // While the physical crouch owns the stance, the view stands on the standing
    // eye height instead of the game's (crouching, standing back up, its bob):
    // the camera stays at the player's real head and only the body crouches.
    const float rise = gHaveEye ? eye.z - gEye.z : 0.0f;
    gEyeStill = std::fabs(rise) < 0.0005f ? std::min(gEyeStill + 1, 1000) : 0;
    if (!gCrouchOwned && gStance.load(std::memory_order_relaxed) == kStanceStand && gEyeStill > 10 &&
        eye.z > 1.0f && eye.z < 2.5f) {
        gStandingEye = eye.z;
        gHaveStandingEye = true;
    }
    gEyeLift.store(gCrouchOwned && gHaveStandingEye ? std::clamp(gStandingEye - eye.z, 0.0f, 1.5f) : 0.0f,
                   std::memory_order_relaxed);
    gEye = eye;
    gHaveEye = true;
    std::uint8_t* const player = gInstalled ? CurrentPlayer() : nullptr;
    // This view shows the turns verified at the last PrePhysicsUpdate and the
    // move made in it: take both out of the reference now, before it composes.
    float yaw = 0;
    Vec3 origin{};
    unsigned long long generation = 0;
    if (!ReadHeadTrackingRoom(yaw, origin, generation)) {
        gHavePlayYaw = false;
        return;
    }
    float absorbYaw = gAwaitingGeneration == generation ? gAwaitingYaw : 0.0f;
    const float playYaw = YawOf(rotation) - (yaw + absorbYaw);
    Vec3 shiftWorld = eyeShift;
    shiftWorld = {shiftWorld.x, shiftWorld.y, 0.0f};
    // The body's move, closed-loop: what the pivot actually did since the last
    // view, not what was commanded. The physics may refuse a move (an obstacle
    // the probe rays slipped between), deflect it, or settle the capsule a few
    // frames later; none of that may move the camera. So while our moves are
    // settling and the game is not walking the player itself, the pivot's whole
    // horizontal change is the body's and is taken out. Otherwise -- the stick
    // walking, a platform, a push -- only the part of this frame's move that was
    // actually achieved is, and the rest is the game's to show.
    Vec3 pivotNow{};
    auto* const entity = player != nullptr ? *reinterpret_cast<std::uint8_t**>(player + kPlayerEntity) : nullptr;
    const bool havePivot = entity != nullptr && ReadVec3(entity + kEntityPos, pivotNow);
    if (havePivot && gHavePivotAtView) {
        const Vec2 moved{pivotNow.x - gPivotAtView.x, pivotNow.y - gPivotAtView.y};
        const float movedLength = std::sqrt(moved.x * moved.x + moved.y * moved.y);
        const bool stickIdle = MoveLaneAxisMilli(0) == 0 && MoveLaneAxisMilli(1) == 0;
        const bool settling = gFrame.load(std::memory_order_relaxed) - gLastMoveFrame <= 3;
        const bool ownMove = gMovePending && gMoveGeneration == generation;
        if (settling && stickIdle && gMoveState == kStateGround && movedLength < 0.3f) {
            shiftWorld.x += moved.x;
            shiftWorld.y += moved.y;
            if (!ownMove && movedLength > 0.0005f) { gSettled.fetch_add(1, std::memory_order_relaxed); }
        } else if (ownMove) {
            const float commanded = std::sqrt(gMoveWorld.x * gMoveWorld.x + gMoveWorld.y * gMoveWorld.y);
            if (commanded > 0.0f) {
                const Vec2 dir{gMoveWorld.x / commanded, gMoveWorld.y / commanded};
                const float along = std::clamp(moved.x * dir.x + moved.y * dir.y, 0.0f, commanded);
                shiftWorld.x += dir.x * along;
                shiftWorld.y += dir.y * along;
            }
        }
        if (ownMove) {
            const float commanded = std::sqrt(gMoveWorld.x * gMoveWorld.x + gMoveWorld.y * gMoveWorld.y);
            const float along = commanded > 0.0f ? (moved.x * gMoveWorld.x + moved.y * gMoveWorld.y) / commanded : 0.0f;
            if (commanded > 0.002f && along < 0.4f * commanded) {
                gPushBackFrames = 12;
                gPushBackDir = {gMoveWorld.x / commanded, gMoveWorld.y / commanded};
                gPushedBack.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    gPivotAtView = pivotNow;
    gHavePivotAtView = havePivot;
    Vec3 absorbOrigin = body::WorldToTracking(shiftWorld, playYaw);
    absorbOrigin.y = 0.0f;
    const bool shifted = shiftWorld.x != 0.0f || shiftWorld.y != 0.0f;
    if (absorbYaw != 0.0f || shifted) {
        if (AdjustHeadTrackingRoom(absorbYaw, absorbOrigin, generation)) {
            if (absorbYaw != 0.0f) { gYawAbsorbed.fetch_add(1, std::memory_order_relaxed); }
        } else {
            gAbsorbFailed.fetch_add(1, std::memory_order_relaxed);
            absorbYaw = 0;
        }
    }
    gAwaitingYaw = 0;
    gMovePending = false;
    gPlayYaw = YawOf(rotation) - (yaw + absorbYaw);
    gHavePlayYaw = true;
}

// The head-in-a-wall fade. The body's eye is always somewhere the capsule could
// stand; the camera is where the player's head is. Geometry between the two
// means the head went where the body could not follow it -- into a wall, a
// door's frame, a locker -- and the view darkens with the depth instead of
// showing the inside of the wall.
void UpdateFade(std::uint8_t* player, const Vec3& position, const Vec3& gameEye)
{
    float target = 0.0f;
    const bool on = gFadeOn.load(std::memory_order_relaxed) &&
                    (gYawOn.load(std::memory_order_relaxed) || gRoomOn.load(std::memory_order_relaxed));
    auto* const entity = *reinterpret_cast<std::uint8_t* const*>(player + kPlayerEntity);
    Vec3 pivot{};
    if (on && gFollowing && entity != nullptr && ReadVec3(entity + kEntityPos, pivot)) {
        const Vec3 eye{pivot.x + gameEye.x, pivot.y + gameEye.y, pivot.z + gameEye.z};
        const Vec3 d{position.x - gameEye.x, position.y - gameEye.y, position.z - gameEye.z};
        const float distance = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        const float margin = gFadeMarginMm.load() / 1000.0f, depthToBlack = gFadeDepthMm.load() / 1000.0f;
        if (distance > 0.01f && distance < 2.5f) {
            const float reach = distance + margin;
            const Vec3 to{eye.x + d.x / distance * reach, eye.y + d.y / distance * reach, eye.z + d.z / distance * reach};
            scene::Hit hit{};
            if (QueryBodySegment(reinterpret_cast<std::uintptr_t>(player), eye, to, hit) && hit.distance >= 0.0f &&
                hit.distance < reach) {
                target = std::clamp((reach - hit.distance) / std::fmax(depthToBlack, 0.01f), 0.0f, 1.0f);
            }
        }
    }
    gFadeTarget.store(target, std::memory_order_relaxed);
    gFadeNs.store(MonotonicNanoseconds(), std::memory_order_relaxed);
}

void NoteComposedView(const Vec3& position, const Quaternion& rotation, float playYaw, const Vec3& gameEye,
                      const Vec3& origin, const Pose& head)
{
    (void)origin;
    std::uint8_t* const player = gInstalled ? CurrentPlayer() : nullptr;
    if (player == nullptr) { return; }
    UpdateFade(player, position, gameEye);
    if (!gTracing.load(std::memory_order_relaxed)) { return; }
    std::lock_guard lock(gTraceMutex);
    TraceEvent e{};
    e.kind = 'C';
    e.frame = gFrame.load(std::memory_order_relaxed);
    if (auto* const entity = *reinterpret_cast<std::uint8_t* const*>(player + kPlayerEntity)) {
        ReadVec3(entity + kEntityPos, e.pivot);
    }
    Quaternion bodyRotation{};
    std::memcpy(&bodyRotation, player + kPlayerCamera + kCameraRotation, sizeof(bodyRotation));
    e.yawRotation = YawOf(bodyRotation);
    e.view = position;
    e.yawView = YawOf(rotation);
    e.state = *reinterpret_cast<const int*>(player + kPlayerFsm);
    e.playYaw = playYaw;
    e.camera = {e.pivot.x + position.x, e.pivot.y + position.y, e.pivot.z + position.z};
    const Vec3 placed = body::TrackingToWorld(head.position, playYaw);
    e.anchor = {e.camera.x - placed.x, e.camera.y - placed.y, e.camera.z - placed.z};
    e.head = head.position;
    e.headRotation = head.orientation;
    e.gameEye = gameEye;
    TrackingFrame controllers{};
    if (TryGetTrackingFrame(controllers)) {
        e.gripR = controllers.hands[1].gripPose.position;
        e.gripL = controllers.hands[0].gripPose.position;
        e.gripRQ = controllers.hands[1].gripPose.orientation;
        e.gripLQ = controllers.hands[0].gripPose.orientation;
    }
    WristArmFrame arm{};
    if (NewestWristArm(arm)) {
        e.handR = arm.right ? arm.rightWrist : Vec3{};
        e.handL = arm.wrist;
        e.armTime = arm.displayTime;
    }
    const auto heading = body::HeadHeading(head.orientation);
    e.headYaw = heading ? *heading : 0.0f;
    e.offset = gLastOffset;
    e.walk = gLastWalk;
    e.lean = gLastLean;
    if (gTraceCount < gTrace.size()) { gTrace[gTraceCount++] = e; } else { gTracing.store(false); }
}

DWORD SetBodyYawFollow(unsigned enabled)
{
    if (enabled > 1) { return 1; }
    if (enabled && EnsureBodyFollowHook() != 0) { return 2; }
    gYawOn.store(enabled != 0, std::memory_order_release);
    Log(std::string("result=0 detail=yaw value=") + (enabled ? "1" : "0"));
    return 0;
}

DWORD SetBodyRoomScale(unsigned enabled)
{
    if (enabled > 1) { return 1; }
    if (enabled && EnsureBodyFollowHook() != 0) { return 2; }
    gRoomOn.store(enabled != 0, std::memory_order_release);
    Log(std::string("result=0 detail=room value=") + (enabled ? "1" : "0"));
    return 0;
}

float BodyFadeAlpha()
{
    return FreshSample(MonotonicNanoseconds(), gFadeNs.load(std::memory_order_relaxed), 100000000ull)
               ? gFadeTarget.load(std::memory_order_relaxed) : 0.0f;
}

unsigned BodyYawFollowEnabled() { return gYawOn.load() ? 1u : 0u; }
DWORD SetPhysicalCrouch(unsigned enabled)
{
    if (enabled > 1) { return 1; }
    gCrouchOn.store(enabled != 0, std::memory_order_release);
    Log(std::string("result=0 detail=crouch value=") + (enabled ? "1" : "0"));
    return 0;
}
unsigned PhysicalCrouchEnabled() { return gCrouchOn.load() ? 1u : 0u; }
float CrouchEyeLift() { return gEyeLift.load(std::memory_order_relaxed); }
DWORD SetHeadInWallsFade(unsigned enabled)
{
    if (enabled > 1) { return 1; }
    gFadeOn.store(enabled != 0, std::memory_order_release);
    Log(std::string("result=0 detail=fade value=") + (enabled ? "1" : "0"));
    return 0;
}
unsigned HeadInWallsFadeEnabled() { return gFadeOn.load() ? 1u : 0u; }
unsigned BodyRoomScaleEnabled() { return gRoomOn.load() ? 1u : 0u; }

std::string BodyTrace()
{
    std::lock_guard lock(gTraceMutex);
    std::ostringstream out;
    out.setf(std::ios::fixed); out.precision(4);
    for (unsigned i = 0; i < gTraceCount; ++i) {
        const auto& e = gTrace[i];
        out << '\n' << e.kind << " f=" << e.frame << " st=" << e.state
            << " pivot=" << e.pivot.x << ',' << e.pivot.y << ',' << e.pivot.z
            << " yawRot=" << e.yawRotation * 57.29578f;
        if (e.kind == 'C') {
            out << " yawView=" << e.yawView * 57.29578f << " play=" << e.playYaw * 57.29578f
                << " anchor=" << e.anchor.x << ',' << e.anchor.y << ',' << e.anchor.z
                << " cam=" << e.camera.x << ',' << e.camera.y << ',' << e.camera.z
                << " head=" << e.head.x << ',' << e.head.y << ',' << e.head.z
                << " headYaw=" << e.headYaw * 57.29578f
                << " headQ=" << e.headRotation.x << ',' << e.headRotation.y << ',' << e.headRotation.z << ','
                << e.headRotation.w << " eye=" << e.gameEye.x << ',' << e.gameEye.y << ',' << e.gameEye.z
                << " handR=" << e.handR.x << ',' << e.handR.y << ',' << e.handR.z
                << " handL=" << e.handL.x << ',' << e.handL.y << ',' << e.handL.z << " armT=" << e.armTime
                << " gripR=" << e.gripR.x << ',' << e.gripR.y << ',' << e.gripR.z
                << " gripL=" << e.gripL.x << ',' << e.gripL.y << ',' << e.gripL.z
                << " gripRQ=" << e.gripRQ.x << ',' << e.gripRQ.y << ',' << e.gripRQ.z << ',' << e.gripRQ.w
                << " gripLQ=" << e.gripLQ.x << ',' << e.gripLQ.y << ',' << e.gripLQ.z << ',' << e.gripLQ.w
                << " off=" << e.offset.x << ',' << e.offset.y << " walk=" << e.walk.x << ',' << e.walk.y
                << " lean=" << e.lean;
        }
    }
    return out.str();
}

std::string BodyFollowReport()
{
    std::ostringstream out;
    out << "bodyHook=" << (gInstalled ? 1 : 0) << " bodyCalls=" << gCalls.load()
        << " yaw=" << BodyYawFollowEnabled() << " room=" << BodyRoomScaleEnabled()
        << " deadzoneDeci=" << gDeadzoneDeci.load() << " rateDeg=" << gRateDeg.load()
        << " neckMm=" << gNeckUpMm.load() << ',' << gNeckForwardMm.load()
        << " radiusMm=" << gRadiusMm.load() << " stepMm=" << gStepMm.load()
        << " gates=" << gGates.load() << " state=" << gState.load()
        << " yawErrMdeg=" << gYawErrorMilli.load() << " yawWrites=" << gYawWrites.load()
        << " yawAbsorbed=" << gYawAbsorbed.load() << " yawRejected=" << gYawRejected.load()
        << " yawTakeover=" << gYawTakeover.load() << " yawLimited=" << gYawLimited.load()
        << " neckOffsetMm=" << gNeckOffsetMm.load() << " moves=" << gMoves.load() << " walks=" << gWalks.load()
        << " blocked=" << gBlocked.load() << " slid=" << gSlid.load() << " noGround=" << gNoGround.load()
        << " groundMm=" << gGroundMm.load() << " absorbFailed=" << gAbsorbFailed.load()
        << " pushedBack=" << gPushedBack.load() << " settled=" << gSettled.load()
        << " fadeMilli=" << static_cast<int>(gFadeTarget.load() * 1000.0f) << " fade=" << (gFadeOn.load() ? 1 : 0)
        << " leanMm=" << gLeanRadiusMm.load() << " leanHolds=" << gLeanHolds.load()
        << " crouch=" << (gCrouchOn.load() ? 1 : 0) << " stance=" << gStance.load() << " dropMm=" << gDropMm.load()
        << " crouchOwned=" << gCrouchOwnedFlag.load() << " crouchTaps=" << gCrouchTaps.load()
        << " eyeLiftMm=" << static_cast<int>(gEyeLift.load() * 1000.0f)
        << " standingEyeMm=" << static_cast<int>(gStandingEye * 1000.0f)
        << " nearMm=" << static_cast<int>(ViewHookLastNearPlane() * 1000.0f);
    return out.str();
}

bool ExecuteBodyCommand(const std::vector<std::string>& args, std::ostringstream& out)
{
    const std::string& verb = args[0];
    if (verb.rfind("body.", 0) != 0) { return false; }
    const auto arg = [&](std::size_t i, int fallback) {
        return i < args.size() ? static_cast<int>(std::strtol(args[i].c_str(), nullptr, 10)) : fallback;
    };
    const auto set = [&](std::atomic<int>& value, std::size_t i, int lo, int hi) {
        if (i >= args.size()) { return 0; }
        const int v = arg(i, value.load());
        if (v < lo || v > hi) { return 1; }
        value.store(v);
        return 0;
    };
    if (verb == "body.yaw") {
        out << "body.yaw result=" << (args.size() > 1 ? SetBodyYawFollow(static_cast<unsigned>(arg(1, 1))) : 0)
            << " enabled=" << BodyYawFollowEnabled();
    } else if (verb == "body.room") {
        out << "body.room result=" << (args.size() > 1 ? SetBodyRoomScale(static_cast<unsigned>(arg(1, 1))) : 0)
            << " enabled=" << BodyRoomScaleEnabled();
    } else if (verb == "body.deadzone") {
        out << "body.deadzone result=" << set(gDeadzoneDeci, 1, 0, 900) << " decidegrees=" << gDeadzoneDeci.load();
    } else if (verb == "body.rate") {
        out << "body.rate result=" << set(gRateDeg, 1, 0, 3600) << " degreesPerSecond=" << gRateDeg.load();
    } else if (verb == "body.neck") {
        out << "body.neck result=" << (set(gNeckUpMm, 1, 0, 200) | set(gNeckForwardMm, 2, 0, 200))
            << " upMm=" << gNeckUpMm.load() << " forwardMm=" << gNeckForwardMm.load();
    } else if (verb == "body.radius") {
        out << "body.radius result=" << set(gRadiusMm, 1, 50, 600) << " mm=" << gRadiusMm.load();
    } else if (verb == "body.step") {
        out << "body.step result=" << set(gStepMm, 1, 0, 600) << " mm=" << gStepMm.load();
    } else if (verb == "body.roomband") {
        out << "body.roomband result=" << (set(gRoomStartMm, 1, 1, 200) | set(gRoomStopMm, 2, 0, 200))
            << " startMm=" << gRoomStartMm.load() << " stopMm=" << gRoomStopMm.load();
    } else if (verb == "body.lean") {
        out << "body.lean result=" << (set(gLeanRadiusMm, 1, 0, 600) | set(gRecentreMmps, 2, 0, 3000))
            << " radiusMm=" << gLeanRadiusMm.load() << " recentreMmps=" << gRecentreMmps.load();
    } else if (verb == "body.crouch") {
        if (args.size() > 1) { SetPhysicalCrouch(static_cast<unsigned>(arg(1, 1)) != 0 ? 1u : 0u); }
        out << "body.crouch result=" << (set(gCrouchDownMm, 2, 50, 800) | set(gCrouchUpMm, 3, 0, 800))
            << " enabled=" << PhysicalCrouchEnabled() << " downMm=" << gCrouchDownMm.load()
            << " upMm=" << gCrouchUpMm.load();
    } else if (verb == "body.fadeon") {
        if (args.size() > 1) { SetHeadInWallsFade(static_cast<unsigned>(arg(1, 1)) != 0 ? 1u : 0u); }
        out << "body.fadeon result=0 enabled=" << HeadInWallsFadeEnabled();
    } else if (verb == "body.fade") {
        out << "body.fade result=" << (set(gFadeMarginMm, 1, 0, 300) | set(gFadeDepthMm, 2, 10, 500))
            << " marginMm=" << gFadeMarginMm.load() << " depthMm=" << gFadeDepthMm.load()
            << " alpha=" << gFadeTarget.load();
    } else if (verb == "body.scan") {
        {
            std::lock_guard lock(gScanMutex);
            gScanResult.clear();
        }
        gScanPending.store(true, std::memory_order_release);
        std::string result;
        for (int i = 0; i < 50 && result.empty(); ++i) {
            Sleep(20);
            std::lock_guard lock(gScanMutex);
            result = gScanResult;
        }
        out << "body.scan" << (result.empty() ? std::string(" result=timeout") : result);
    } else if (verb == "body.tracestart") {
        StartTrace();
        out << "body.tracestart result=0";
    } else if (verb == "body.trace") {
        out << "body.trace" << BodyTrace();
    } else if (verb == "body.pose") {
        out << "body.pose result=" << (args.size() > 1 ? SetBodyPose(static_cast<unsigned>(arg(1, 1))) : 0)
            << BodyPoseReport();
    } else if (verb == "body.feet") {
        out << "body.feet result=" << (args.size() > 1 ? SetBodyFeet(static_cast<unsigned>(arg(1, 1))) : 0)
            << BodyPoseReport();
    } else if (verb == "body.report") {
        out << "body.report " << BodyFollowReport();
    } else {
        out << verb << " result=unknown_verb";
    }
    return true;
}

} // namespace preyvr::dll
