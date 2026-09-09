// Vee.ViewmodelTweaks - Prey (2017) Chairloader mod
//
// Lets the player offset the first-person weapon viewmodel (position + rotation) with global standing /
// crouch offsets plus per-weapon hip offsets, a proper ironsight "aim lock" on a dedicated key (per-weapon
// poses), and a weapon FOV override - all live, from an ImGui window (F1 -> "Viewmodel Tweaks") or the
// console (vm_* cvars).
//
// How it works (see README.md for the full reverse-engineering notes):
//
//  * Prey still ships Crysis 3's "procedural weapon animation" system. Every frame
//    CProceduralWeaponAnimationContext::Update() calls CProceduralWeaponAnimation::Update(),
//    which blends the designer-authored pose offsets, look/strafe sway, recoil, bumps, etc.
//    into two view-space QuatTs: m_rightOffset (weapon hand) and m_leftOffset (support hand).
//    The context then rotates those into the aim frame and pushes them *additively* onto the
//    hand IK joints of the first-person arms through an IAnimationOperatorQueue pose modifier.
//      - Hip / crouch offsets: we post-hook CProceduralWeaponAnimation::Update() and fold the
//        user's offset into the QuatT(s) before the context consumes them.
//      - Aim lock, skeleton side: additive offsets inherit everything the body animation does with
//        camera pitch and look-sway, which ruins ironsights. So while aiming we post-hook the *context*
//        update and push an eOp_Override for the weapon-hand IK joint: camera(model space) * userPose *
//        (weaponBone relative to IK joint)^-1, so hands, weapon bone and effects end up near the pose.
//        The camera is not known yet at that point (the skeleton is evaluated BEFORE the camera is
//        built from it), so this uses last frame's exact camera + this frame's mouse delta.
//      - Aim lock, render side: ArkPlayerCamera::UpdateView builds the camera from the evaluated
//        skeleton (camera attachment position, view angles, smoothed lean roll) - after that the
//        camera is final and nothing moves the skeleton any more before rendering. We post-hook it and
//        write the weapon's attachment transform (CAttachmentBONE::m_AttModelRelative, what
//        DrawAttachment multiplies with the character matrix) to exactly camera * aimPose, and move the
//        hand joints (final absolute pose, what the skinning reads) by the same rigid delta. No
//        prediction, no feedback: the weapon is placed where it has to be, every frame, from exact data.
//
//  * The weapon is rendered in a separate "nearest" pass with its own FOV (r_DrawNearFoV).
//    ArkPlayerZoomManager hard-codes it to 55 degrees on Reset()/SetNearFOVUnlocked() and
//    rescales it proportionally to the HFOV while zooming. We re-apply the user's FOV every frame
//    relative to cl_hfov, so zooming (including our own aim camera zoom) keeps working.

#include "ModMain.h"
#include <Prey/CryRenderer/IRenderer.h>
#include <Prey/CrySystem/IConsole.h>
#include <Prey/CryInput/IHardwareMouse.h>
#include <Prey/CrySystem/HardwareMouse.h>
#include <Prey/CryEntitySystem/IEntity.h>
#include <Prey/CryEntitySystem/IEntityClass.h>
#include <Prey/GameDll/ark/player/ArkPlayer.h>
#include <Prey/GameDll/ark/player/ArkPlayerCamera.h>
#include <Prey/GameDll/ark/player/ArkPlayerZoomManager.h>
#include <Prey/GameDll/ark/player/ArkPlayerInput.h>
#include <Prey/GameDll/ark/player/ArkPlayerWeaponComponent.h>
#include <Prey/GameDll/ark/player/ArkPlayerComponent.h>
#include <Prey/GameDll/ark/player/ArkPlayerHealthComponent.h>
#include <Prey/GameDll/ark/weapons/arkweapon.h>
#include <Prey/GameDll/ark/weapons/arkweaponshotgun.h>
#include <Prey/GameDll/GameCVars.h>
#include <Prey/GameDll/weaponlookoffset.h>
#include <Prey/GameDll/weaponstrafeoffset.h>
#include <Prey/GameDll/weaponrecoiloffset.h>
#include <Prey/GameDll/weaponbumpoffset.h>
#include <Prey/CryAction/IViewSystem.h>
#include <Prey/CryPhysics/physinterface.h>
#include <pugixml.hpp>

ModMain* gMod = nullptr;

//---------------------------------------------------------------------------------
// Reverse-engineered game internals (PreyDll.dll, Chairloader-supported build)
//---------------------------------------------------------------------------------
namespace PreyInternals
{
    // CProceduralWeaponAnimation (GameDll/ProceduralWeaponAnimation.h in Crysis 3 GameSDK).
    // Not part of the Chairloader SDK headers, so we only describe the members we touch.
    namespace CProceduralWeaponAnimation
    {
        constexpr size_t OFFSET_RIGHT_OFFSET = 0x2D8;       // QuatT m_rightOffset (view space)
        constexpr size_t OFFSET_LEFT_OFFSET = 0x2F4;        // QuatT m_leftOffset  (view space)
        constexpr size_t OFFSET_DEBUG_INPUT_ENABLED = 0x320;// bool  m_debugInputEnabled (g_debugWeaponOffset == 2)

        // void CProceduralWeaponAnimation::Update(float deltaTime)
        inline auto FUpdate = PreyFunction<void(void* _this, float deltaTime)>(0x17D31E0);
    }

    // CProceduralWeaponAnimationContext (mannequin procedural context owning the object above at +0x40).
    namespace CProceduralWeaponAnimationContext
    {
        constexpr size_t OFFSET_RIGHT_IK_JOINT = 0x018;     // int   m_params.rightBlendIkIdx (weapon hand IK target joint)
        constexpr size_t OFFSET_LEFT_IK_JOINT = 0x01C;      // int   m_params.leftBlendIkIdx
        constexpr size_t OFFSET_WEAPON_TARGET_JOINT = 0x020;// int   m_params.weaponTargetIdx
        constexpr size_t OFFSET_POSE_MODIFIER = 0x028;      // IAnimationOperatorQueue* m_pPoseModifier
        constexpr size_t OFFSET_SCOPE = 0x038;              // IScope* m_pScope
        constexpr size_t OFFSET_WEAPON_SWAY = 0x040;        // CProceduralWeaponAnimation m_weaponSway
        constexpr size_t OFFSET_AIM_DIRECTION = 0x368;      // Vec3  m_aimDirection (model space)
        constexpr size_t OFFSET_INSTANCE_COUNT = 0x374;     // int   m_instanceCount (active procedural clips)

        // void CProceduralWeaponAnimationContext::Update(float timePassed)
        // Early-outs when m_instanceCount < 1, i.e. whenever the current mannequin fragment has no
        // weapon procedural clip (some jump/land fragments) -> no offsets at all get applied.
        inline auto FUpdate = PreyFunction<void(void* _this, float timePassed)>(0x17D4D50);
    }

    // Virtual table slots (verified against the game code, they differ from public CRYENGINE).
    constexpr size_t VT_IScope_GetCharInst = 0x18 / 8;
    constexpr size_t VT_ICharacterInstance_GetISkeletonPose = 0x38 / 8;
    constexpr size_t VT_ICharacterInstance_GetIDefaultSkeleton = 0x58 / 8; // [ArkPlayerCamera::GetJointId]
    constexpr size_t VT_ISkeletonPose_GetAbsJointByID = 0xC0 / 8;    // const QuatT& (int jointId)   [ArkPlayer::GetBoneTransform]
    constexpr size_t VT_IAnimationOperatorQueue_PushPosition = 0x40 / 8;    // (int joint, int op, const Vec3&)
    constexpr size_t VT_IAnimationOperatorQueue_PushOrientation = 0x48 / 8; // (int joint, int op, const Quat&)
    constexpr int OP_OVERRIDE = 0;      // IAnimationOperatorQueue::eOp_Override (model space)
    constexpr int OP_ADDITIVE = 3;      // eOp_Additive (what the game uses)

    // IDefaultSkeleton [ArkPlayerCamera::GetJointId, CAttachmentBONE::ProjectAttachment]
    constexpr size_t VT_IDefaultSkeleton_GetJointCount = 0x08 / 8;
    constexpr size_t VT_IDefaultSkeleton_GetJointParentIDByID = 0x10 / 8;
    constexpr size_t VT_IDefaultSkeleton_GetJointNameByID = 0x30 / 8;
    constexpr size_t VT_IDefaultSkeleton_GetJointIDByName = 0x38 / 8;

    // IAttachment (CAttachmentBONE vtable at .rdata 0x181d185c8). The weapon is bound to the
    // first-person arms as a skeleton attachment (CArkWeapon::AttachToHand -> AddBinding of a
    // CSKELAttachment), so it is rendered as part of the arms character at
    //     characterWorldMatrix * m_AttModelRelative(* m_addTransformation)
    // and NOT from its own entity transform.
    constexpr size_t VT_IAttachment_GetAttRelativeDefault = 0x60 / 8; // const QuatT& -> this+0xF8
    constexpr size_t VT_IAttachment_GetAttModelRelative = 0x68 / 8;   // const QuatT& -> this+0x130 [ArkPlayerCamera::GetDesiredPositionAndRotation]
    constexpr size_t VT_IAttachment_HideAttachment = 0x88 / 8;        // [CArkWeapon::AttachToHand]
    constexpr size_t OFFSET_CAttachmentBONE_JOINT_ID = 0x15C;         // int m_nJointID [CAttachmentBONE::UpdateAttModelRelative]

    // ArkPlayer::m_zoomManager::m_nearFOVLockedCount is read by the cl_hfov callback at
    // ArkPlayer + 0x18C0, which pins the zoom manager at ArkPlayer + 0x1898.
    constexpr size_t ARKPLAYER_ZOOMMANAGER_OFFSET = 0x1898;
    // ArkPlayerZoomManager::Update fetches the equipped weapon through ArkPlayer + 0x14B8.
    constexpr size_t ARKPLAYER_WEAPONCOMPONENT_OFFSET = 0x14B8;
    constexpr float STOCK_WEAPON_FOV = 55.0f;

    constexpr int BONE_WEAPON = (int)ArkPlayerCamera::EArkBoneList::BONE_WEAPON;
    constexpr int BONE_CAMERA = (int)ArkPlayerCamera::EArkBoneList::BONE_CAMERA;
}

static_assert(sizeof(QuatT) == 28, "QuatT layout mismatch");
static_assert(offsetof(ArkPlayerZoomManager, m_nearFOVLockedCount) == 0x28, "ArkPlayerZoomManager layout mismatch");
static_assert(offsetof(ArkPlayer, m_zoomManager) == PreyInternals::ARKPLAYER_ZOOMMANAGER_OFFSET, "ArkPlayer layout mismatch");
static_assert(offsetof(ArkPlayer, m_weaponComponent) == PreyInternals::ARKPLAYER_WEAPONCOMPONENT_OFFSET, "ArkPlayer layout mismatch");
static_assert(offsetof(ArkPlayer, m_camera) == 0x12A0, "ArkPlayer layout mismatch");
static_assert(offsetof(CArkWeapon, m_pAttachment) == 0x2B0, "CArkWeapon layout mismatch");
static_assert(offsetof(ArkPlayerCamera, m_rotation) == 0x150, "ArkPlayerCamera layout mismatch");
static_assert(offsetof(ArkPlayerInput, m_sprintCameraRotationRateScale) == 0x98, "ArkPlayerInput layout mismatch");
static_assert(offsetof(SViewParams, rotation) == 0x0C, "SViewParams layout mismatch");

// --- Structures the GAME fills in for us ---------------------------------------------------------
// Prey's physics is not stock CryEngine, and the SDK's physinterface.h is. pe_status_living in Prey is
// 152 bytes (an extra Vec3 after velGround: groundSurfaceIdx sits at +0x64 where the header says +0x58);
// the header's 136-byte version therefore made CLivingEntity::GetStatus write 16 bytes past our local -
// straight over the saved xmm6 in the caller's frame, i.e. over MainUpdate's `dt`. That garbage step was
// what made every smoothed value (convergence, wall pull-back) either freeze or snap. So: our own layout
// for the fields we read, and generous padding after everything the game writes into.
struct PreyStatusLiving : pe_status
{
    enum entype { type_id = ePE_status_living };
    PreyStatusLiving() { type = type_id; }
    int              bFlying;            // +0x04
    float            timeFlying;         // +0x08
    Vec3             camOffset;          // +0x0C
    Vec3             vel;                // +0x18 (confirmed against the game's readers)
    Vec3             velUnconstrained;   // +0x24
    Vec3             velRequested;       // +0x30
    Vec3             velGround;          // +0x3C
    Vec3             preyExtra;          // +0x48 Prey-only field
    float            groundHeight;       // +0x54
    Vec3             groundSlope;        // +0x58
    int              groundSurfaceIdx;   // +0x64 (confirmed)
    int              groundSurfaceIdxAux;// +0x68
    IPhysicalEntity* pGroundCollider;    // +0x70
    int              iGroundColliderPart;// +0x78
    float            timeSinceStanceChange; // +0x7C
    int              bStuck;             // +0x80
    volatile int*    pLockStep;          // +0x88
    int              iCurTime;           // +0x90
    int              bSquashed;          // +0x94
    char             pad[128];           // the game may write further still; nothing of ours lives here
};
static_assert(offsetof(PreyStatusLiving, vel) == 0x18 && offsetof(PreyStatusLiving, groundSurfaceIdx) == 0x64, "PreyStatusLiving layout");
struct PaddedRayHit : ray_hit
{
    char pad[128];
};

template <typename R, typename... Args>
static inline R VCall(void* pObj, size_t slot, Args... args)
{
    using Fn = R(*)(void*, Args...);
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(pObj))[slot])(pObj, args...);
}

template <typename T>
static inline T& Member(void* pObj, size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<char*>(pObj) + offset);
}

//---------------------------------------------------------------------------------
// Hooks
//---------------------------------------------------------------------------------
static auto s_hookPwaUpdate = PreyInternals::CProceduralWeaponAnimation::FUpdate.MakeHook();
static auto s_hookPwaContextUpdate = PreyInternals::CProceduralWeaponAnimationContext::FUpdate.MakeHook();

static void CProceduralWeaponAnimationContext_Update_Hook(void* _this, float timePassed)
{
    using namespace PreyInternals::CProceduralWeaponAnimationContext;
    int& instanceCount = Member<int>(_this, OFFSET_INSTANCE_COUNT);
    const int rawCount = instanceCount;
    const bool modifierNull = Member<void*>(_this, OFFSET_POSE_MODIFIER) == nullptr;

    // Keep the weapon offset pipeline running while no procedural clip is active, so the
    // user offset does not pop off during jump/land animations. The pose stacks are simply
    // empty in that case, so the game contributes nothing extra.
    bool forced = false;
    if (gMod && gMod->Active() && instanceCount < 1 && !modifierNull)
    {
        instanceCount = 1;
        forced = true;
    }

    if (gMod)
        gMod->OnProceduralContextSeen(_this, rawCount, forced, modifierNull);

    const bool ran = instanceCount >= 1;
    s_hookPwaContextUpdate.InvokeOrig(_this, timePassed);

    if (forced && instanceCount == 1)
        instanceCount = 0;

    if (gMod && ran)
        gMod->OnProceduralContextUpdated(_this);
}

static void CProceduralWeaponAnimation_Update_Hook(void* _this, float deltaTime)
{
    s_hookPwaUpdate.InvokeOrig(_this, deltaTime);

    if (!gMod)
        return;

    using namespace PreyInternals::CProceduralWeaponAnimation;
    gMod->GetDiag().pwaUpdatesThisFrame++;

    const ViewmodelSettings& s = gMod->GetSettings();
    if (!gMod->Active())
        return;

    // Leave the designer debug tool (g_debugWeaponOffset 2) alone.
    if (Member<bool>(_this, OFFSET_DEBUG_INPUT_ENABLED))
        return;

    // Only the weapon hand: the support hand follows the weapon through the rig already.
    QuatT& rightOffset = Member<QuatT>(_this, OFFSET_RIGHT_OFFSET);
    gMod->ApplyOffset(rightOffset);
}

// The game's procedural weapon offsets (view space), captured so the aim lock can keep recoil/bumps.
// NOTE: these are MEMBER functions returning a 28-byte QuatT, so the MSVC x64 convention is
// (this, hidden return pointer, args...). The SDK headers declare them as free functions returning
// QuatT, which would put the return pointer first - do not use those declarations for hooking.
#define VM_OFFSET_COMPUTE_HOOK(NAME, CLASS, ADDR, SLOT)                                              \
    static auto s_fn##NAME = PreyFunction<QuatT*(CLASS* _this, QuatT* _ret, float frameTime)>(ADDR);   \
    static auto s_hook##NAME = s_fn##NAME.MakeHook();                                                 \
    static QuatT* NAME##_Hook(CLASS* _this, QuatT* _ret, float frameTime)                             \
    {                                                                                                 \
        QuatT* r = s_hook##NAME.InvokeOrig(_this, _ret, frameTime);                                   \
        if (gMod && r)                                                                                \
            gMod->SetGameOffset(SLOT, *r);                                                            \
        return r;                                                                                     \
    }
VM_OFFSET_COMPUTE_HOOK(LookCompute, CLookOffset, 0x1801770, 0)
VM_OFFSET_COMPUTE_HOOK(StrafeCompute, CStrafeOffset, 0x18039C0, 1)
VM_OFFSET_COMPUTE_HOOK(RecoilCompute, CRecoilOffset, 0x1803390, 2)
VM_OFFSET_COMPUTE_HOOK(BumpCompute, CBumpOffset, 0x1801250, 3)

// The player camera. Called by the view system once per frame after the first-person skeleton has been
// evaluated (the camera position IS the evaluated camera attachment) and before anything is rendered.
static auto s_hookUpdateView = ArkPlayerCamera::FUpdateView.MakeHook();
static void ArkPlayerCamera_UpdateView_Hook(ArkPlayerCamera* const _this, SViewParams& _params)
{
    s_hookUpdateView.InvokeOrig(_this, _params);
    if (gMod)
        gMod->OnCameraUpdated(_this, _params);
}

// The game interpolates a weapon's camera-speed (and walk-speed) multiplier towards its "zoomed"
// value based on the zoom manager's HFOV progress. Our aim camera zoom triggers that too, which is
// where the aggressive ADS sensitivity drop comes from. Substitute the zoomed camera multiplier.
static auto s_hookHfovMultiplier = ArkPlayerZoomManager::FGetHFOVDependentMultiplier.MakeHook();
static float ArkPlayerZoomManager_GetHFOVDependentMultiplier_Hook(const ArkPlayerZoomManager* const _this, float currentMultiplier, float zoomedMultiplier)
{
    if (gMod)
    {
        float replacement = gMod->GetAimSensitivityMultiplier(currentMultiplier, zoomedMultiplier);
        if (replacement > 0.0f)
            zoomedMultiplier = replacement;
    }
    return s_hookHfovMultiplier.InvokeOrig(_this, currentMultiplier, zoomedMultiplier);
}

// Shotgun spread: the game interpolates the current dispersion between a minimum and a maximum that come
// from stats (chipsets scale them the same way). Scaling both ends scales the whole cone.
static auto s_hookDispMin = CArkWeaponShotgun::FGetDispersionMinimum.MakeHook();
static auto s_hookDispMax = CArkWeaponShotgun::FGetDispersionMaximum.MakeHook();
static float CArkWeaponShotgun_GetDispersionMinimum_Hook(CArkWeaponShotgun const* const _this)
{
    float v = s_hookDispMin.InvokeOrig(_this);
    if (gMod)
        v *= gMod->GetSpreadMultiplier(_this);
    return v;
}
static float CArkWeaponShotgun_GetDispersionMaximum_Hook(CArkWeaponShotgun const* const _this)
{
    float v = s_hookDispMax.InvokeOrig(_this);
    if (gMod)
        v *= gMod->GetSpreadMultiplier(_this);
    return v;
}
// The shotgun's pellets are laid out in a cone whose angle is the weapon stat "ShotgunSpreadConeDegrees",
// read through CArkWeapon::GetStatFloat in SpawnPellets (the dispersion above only wobbles the cone's
// centre). Scale that stat too.
static auto s_hookGetStatFloat = CArkWeapon::FGetStatFloat.MakeHook();
static float CArkWeapon_GetStatFloat_Hook(const CArkWeapon* const _this, const CCryName& _statName)
{
    float v = s_hookGetStatFloat.InvokeOrig(_this, _statName);
    if (gMod && _statName.c_str() && strcmp(_statName.c_str(), "ShotgunSpreadConeDegrees") == 0)
        v *= gMod->GetSpreadMultiplier(_this);
    return v;
}

// Every shot of every weapon goes through CArkWeapon::FireWeapon - the reliable shot event (the pistol's
// procedural recoil is too small for the offset-jump heuristic below).
static auto s_hookFireWeapon = CArkWeapon::FFireWeapon.MakeHook();
static void CArkWeapon_FireWeapon_Hook(CArkWeapon* const _this)
{
    s_hookFireWeapon.InvokeOrig(_this);
    if (gMod)
        gMod->OnWeaponFired(_this);
}

//---------------------------------------------------------------------------------
// Helpers
//---------------------------------------------------------------------------------
static inline float SmoothStep01(float t)
{
    t = clamp_tpl(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// --- Numeric sanity -------------------------------------------------------------------------------
// Cheap checks used at the boundaries of the pipeline (what we read from the game, what we write back)
// and on every accumulated state. A NaN compares false with everything, so "!(x < limit)" catches it.
static inline bool Finite(float v) { return fabsf(v) < 1e30f; } // false for NaN and inf
static inline bool Finite(const Vec3& v) { return Finite(v.x) && Finite(v.y) && Finite(v.z); }
static inline bool SaneQuat(const Quat& q)
{
    const float n2 = q.w * q.w + q.v.x * q.v.x + q.v.y * q.v.y + q.v.z * q.v.z;
    return n2 > 0.25f && n2 < 4.0f; // false for NaN
}
static inline bool SaneQuatT(const QuatT& t, float maxPos = 100.0f)
{
    return SaneQuat(t.q) && Finite(t.t) && t.t.GetLengthSquared() < maxPos * maxPos;
}
static inline Quat SafeNormalized(const Quat& q)
{
    return SaneQuat(q) ? q.GetNormalized() : Quat(IDENTITY);
}
//! Euler angles of a (possibly slightly denormalized) quaternion without the asin() domain hazard.
static Ang3 SafeAng3(const Quat& qIn)
{
    const Quat q = SafeNormalized(qIn);
    Ang3 a;
    const float sy = clamp_tpl(-(q.v.x * q.v.z - q.w * q.v.y) * 2.0f, -1.0f, 1.0f);
    a.y = asinf(sy);
    if (fabsf(fabsf(a.y) - gf_PI * 0.5f) < 0.01f)
    {
        a.x = 0.0f;
        a.z = atan2f(-2.0f * (q.v.x * q.v.y - q.w * q.v.z), 1.0f - (q.v.x * q.v.x + q.v.z * q.v.z) * 2.0f);
    }
    else
    {
        a.x = atan2f((q.v.y * q.v.z + q.w * q.v.x) * 2.0f, 1.0f - (q.v.x * q.v.x + q.v.y * q.v.y) * 2.0f);
        a.z = atan2f((q.v.x * q.v.y + q.w * q.v.z) * 2.0f, 1.0f - (q.v.z * q.v.z + q.v.y * q.v.y) * 2.0f);
    }
    return a;
}
static inline bool SanePose(const PoseOffset& p)
{
    return Finite(p.posX) && Finite(p.posY) && Finite(p.posZ) && Finite(p.pitch) && Finite(p.yaw) && Finite(p.roll)
        && fabsf(p.posX) < 10.0f && fabsf(p.posY) < 10.0f && fabsf(p.posZ) < 10.0f;
}

//! Moves `value` towards `target` so that a full 0->1 trip takes `time` seconds.
static inline void MoveTowards(float& value, float target, float time, float dt)
{
    if (time <= 0.0001f)
    {
        value = target;
        return;
    }
    float step = dt / time;
    if (value < target)
        value = min(value + step, target);
    else if (value > target)
        value = max(value - step, target);
}

static bool IsHardwareCursorVisible()
{
    if (!gEnv || !gEnv->pHardwareMouse)
        return false;
    return static_cast<CHardwareMouse*>(gEnv->pHardwareMouse)->m_iReferenceCounter > 0;
}

static const char* GetWeaponClassName(ArkPlayer* pPlayer)
{
    if (!pPlayer)
        return "";
    CArkWeapon* pWeapon = pPlayer->m_weaponComponent.GetEquippedWeapon();
    if (!pWeapon)
        return "";
    IEntity* pEntity = pWeapon->GetEntity();
    if (!pEntity || !pEntity->GetClass())
        return "";
    const char* name = pEntity->GetClass()->GetName();
    return name ? name : "";
}

static QuatT BlendQuatT(const QuatT& a, const QuatT& b, float t)
{
    t = clamp_tpl(t, 0.0f, 1.0f);
    QuatT r;
    r.t = LERP(a.t, b.t, t);
    r.q = SafeNormalized(Quat::CreateNlerp(a.q, b.q, t));
    return r;
}

float ModMain::Now() const
{
    return (gEnv && gEnv->pTimer) ? gEnv->pTimer->GetCurrTime() : 0.0f;
}

//---------------------------------------------------------------------------------
// Additive offsets (global standing / crouch + per-weapon hip + legacy aim)
//---------------------------------------------------------------------------------
void ModMain::SetGameOffset(int which, const QuatT& q)
{
    if (which < 0 || which > 3)
        return;
    if (SaneQuatT(q, 5.0f))
        m_gameOffsets[which] = q;
    else
    {
        m_gameOffsets[which] = QuatT(IDENTITY);
        m_nanRecoveries++;
    }
}

void ModMain::ApplyOffset(QuatT& offset) const
{
    const ViewmodelSettings& s = m_settings;
    if (!SaneQuatT(offset, 10.0f))
        return; // the game's own offset is bad this frame: leave it alone
    const QuatT original = offset;

    const float ab = s.aimEnabled ? SmoothStep01(m_aimBlend) : 0.0f;
    float cb = s.crouchEnabled ? SmoothStep01(m_crouchBlend) : 0.0f;
    if (s.aimIgnoresCrouch)
        cb *= (1.0f - ab); // the aim pose must line the sights up regardless of stance

    PoseOffset total = s.base;
    if (cb > 0.0f)
        total.AddScaled(s.crouch, cb);
    if (const WeaponSettings* pW = FindCurrentWeapon())
        total.AddScaled(pW->hip, 1.0f);
    // Feel layer (hip side): sprint pose + sprint sway, view drag. Both fade out as the sights come up;
    // the aim side has its own versions.
    if (SanePose(m_feel.sprintOut))
        total.AddScaled(m_feel.sprintOut, 1.0f - ab);   // already faded to zero when the feature is off
    if (SanePose(m_feel.dragHipOut))
        total.AddScaled(m_feel.dragHipOut, 1.0f - ab);

    // Reloads: the support hand is animated in place (shells, magazines) against where the weapon is in
    // the stock pose, so everything that moves the weapon fades out for the duration and comes back after.
    const float keep = 1.0f - SmoothStep01(m_reloadFade);
    if (keep < 1.0f)
    {
        PoseOffset scaled;
        scaled.AddScaled(total, keep);
        total = scaled;
    }

    const Quat userRot = total.Rot();
    const Vec3 userPos = total.Pos();

    // Rotate the weapon in place (around its hand pivot) and translate in view space.
    offset.t += userPos;
    offset.q = userRot * offset.q;

    // Wall pull-back: slide the weapon towards the camera along the view axis. While aiming the lock
    // takes over (see ComputeAimExtra), so fade this copy out with the aim blend.
    if (s.wallPushEnabled && m_wallPush != 0.0f)
        offset.t.y -= m_wallPush * (1.0f - ab) * keep;

    // Hip-fire convergence: rotate in place so the barrel points at the crosshair's impact point.
    // Fades out with the aim blend (the ironsight lock is on the camera ray by definition).
    if (s.convergeEnabled && (m_convergeYaw != 0.0f || m_convergePitch != 0.0f) && keep > 0.0f)
    {
        const float k = (1.0f - ab) * keep;
        const Quat conv = Quat::CreateRotationXYZ(Ang3(DEG2RAD(m_convergePitch * k), 0.0f, DEG2RAD(m_convergeYaw * k)));
        offset.q = conv * offset.q;
    }
    offset.q = SafeNormalized(offset.q);
    if (!SaneQuatT(offset, 10.0f))
    {
        offset = original; // never hand the game a bad transform
        m_nanRecoveries++;
    }
}

//---------------------------------------------------------------------------------
// Diagnostics
//---------------------------------------------------------------------------------
void ModMain::OnProceduralContextSeen(void* pContext, int instanceCount, bool forced, bool modifierNull)
{
    using namespace PreyInternals::CProceduralWeaponAnimationContext;
    PipelineDiag& d = m_diag;
    d.ctxUpdatesThisFrame++;

    const float t = Now();
    if (pContext != d.lastContext)
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "context changed %p -> %p", d.lastContext, pContext);
        d.Add(t, buf);
        d.lastContext = pContext;
    }
    if ((instanceCount < 1) != (d.lastInstanceCount < 1))
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "instanceCount %d -> %d (%s)", d.lastInstanceCount, instanceCount, forced ? "forced alive" : "not forced");
        d.Add(t, buf);
    }
    d.lastInstanceCount = instanceCount;
    d.lastForced = forced;
    if (modifierNull != d.lastModifierNull)
    {
        d.Add(t, modifierNull ? "pose modifier is NULL" : "pose modifier present");
        d.lastModifierNull = modifierNull;
    }
    const int ik = Member<int>(pContext, OFFSET_RIGHT_IK_JOINT);
    if (ik != d.lastRightIk)
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "right IK joint %d -> %d", d.lastRightIk, ik);
        d.Add(t, buf);
        d.lastRightIk = ik;
    }
}

//---------------------------------------------------------------------------------
// Ironsight lock
//---------------------------------------------------------------------------------
static QuatT ScaleQuatT(const QuatT& q, float k)
{
    if (!(k > 0.0f) || !SaneQuatT(q)) return QuatT(IDENTITY);
    if (fabsf(k - 1.0f) < 1e-4f) return q;
    QuatT r;
    const Ang3 a = SafeAng3(q.q);
    r.q = Quat::CreateRotationXYZ(Ang3(a.x * k, a.y * k, a.z * k));
    r.t = q.t * k;
    return r;
}

float ModMain::GetLockBlend() const
{
    const ViewmodelSettings& s = m_settings;
    if (!Active() || !s.aimEnabled)
        return 0.0f;
    if (m_playerDead || m_reviveGuard > 0.0f)
        return 0.0f; // death model / model reload: attachments and joints are being recreated
    return SmoothStep01(m_aimBlend);
}

QuatT ModMain::ComputeAimExtra() const
{
    const ViewmodelSettings& s = m_settings;
    // Keep (part of) the game's procedural offsets so the locked weapon still kicks and bumps
    // (the camera itself already carries the game's camera recoil), and optionally couple the
    // weapon to the head bob: the camera bobs with the animated head, so a weapon that is glued to
    // the camera looks perfectly still on screen; subtracting a fraction of the camera's own bob makes
    // it lag behind the head like a real ADS viewmodel. All terms are known exactly this frame.
    QuatT extra(IDENTITY);
    const WeaponSettings* pW = FindCurrentWeapon();
    const float recoilScale = pW ? pW->aimRecoilScale : 1.0f;
    if (s.aimAnimSway > 0.0f)
        extra = extra * ScaleQuatT(m_gameOffsets[0], s.aimAnimSway) * ScaleQuatT(m_gameOffsets[1], s.aimAnimSway);
    if (s.aimAnimRecoil * recoilScale > 0.0f)
        extra = extra * ScaleQuatT(m_gameOffsets[2], s.aimAnimRecoil * recoilScale) * ScaleQuatT(m_gameOffsets[3], s.aimAnimRecoil * recoilScale);
    // Head-bob coupling, boosted for a moment after each shot so the fire kick (which reaches the camera
    // through the body animation) shows up as weapon motion relative to the sights.
    float coupling = clamp_tpl(s.aimBobAmount, -1.0f, 1.0f);
    if (pW && m_fireTimer > 0.0f && pW->fireCouplingTime > 0.001f)
        coupling = max(coupling, pW->fireCoupling * clamp_tpl(m_fireTimer / pW->fireCouplingTime, 0.0f, 1.0f));
    if (coupling != 0.0f && m_render.bobValid)
        extra.t -= m_render.bobCam * coupling;
    // Wall pull-back along the view axis keeps the sights on the crosshair, so it can stay on while aiming.
    if (s.wallPushEnabled && s.wallPushWhileAiming && m_wallPush != 0.0f)
        extra.t.y -= m_wallPush;
    return extra;
}

//---------------------------------------------------------------------------------
// Skeleton side (before the animation is evaluated)
//---------------------------------------------------------------------------------
void ModMain::OnProceduralContextUpdated(void* pContext)
{
    using namespace PreyInternals;
    using namespace PreyInternals::CProceduralWeaponAnimationContext;

    AimLockState& L = m_lock;
    const RenderLockState& R = m_render;

    ArkPlayer* pPlayer = ArkPlayer::GetInstancePtr();
    void* pModifier = Member<void*>(pContext, OFFSET_POSE_MODIFIER);
    void* pScope = Member<void*>(pContext, OFFSET_SCOPE);
    if (!pPlayer || !pModifier || !pScope || !pPlayer->GetEntity())
    {
        L.active = false;
        L.captured = false;
        L.addValid = false;
        L.kick = QuatT(IDENTITY);
        return;
    }

    L.rightIkJoint = Member<int>(pContext, OFFSET_RIGHT_IK_JOINT);
    L.leftIkJoint = Member<int>(pContext, OFFSET_LEFT_IK_JOINT);
    L.weaponJoint = Member<int>(pContext, OFFSET_WEAPON_TARGET_JOINT);

    void* pCharInst = VCall<void*>(pScope, VT_IScope_GetCharInst);
    ICharacterInstance* pPlayerChar = pPlayer->GetEntity()->GetCharacter(0);
    L.charMatches = pCharInst && pCharInst == static_cast<void*>(pPlayerChar);
    if (!pCharInst || L.rightIkJoint < 0)
    {
        L.active = false;
        L.captured = false;
        L.addValid = false;
        L.kick = QuatT(IDENTITY);
        return;
    }

    // Skeleton readings are the *previous* frame's final pose (model space).
    void* pSkelPose = VCall<void*>(pCharInst, VT_ICharacterInstance_GetISkeletonPose);
    if (!pSkelPose)
        return;
    const QuatT* pIkAbs = VCall<const QuatT*>(pSkelPose, VT_ISkeletonPose_GetAbsJointByID, L.rightIkJoint);
    if (!pIkAbs || !SaneQuatT(*pIkAbs))
    {
        // Skeleton not evaluated yet (first frames after a load) or garbage: do nothing this frame.
        L.active = false;
        L.captured = false;
        L.addValid = false;
        L.kick = QuatT(IDENTITY);
        m_nanRecoveries += pIkAbs ? 1 : 0;
        return;
    }
    L.ikAbs = *pIkAbs;
    // "Weapon" here means the weapon attachment (what gets rendered and what the render side places),
    // read from the game's values of the previous frame (before the render side moved the hands).
    if (R.attOffsetValid && SaneQuatT(R.weaponBoneGame) && SaneQuatT(R.attOffset))
        L.weaponAbs = R.weaponBoneGame * R.attOffset;
    else
        L.weaponAbs = pPlayer->GetBoneTransform(BONE_WEAPON);
    if (!SaneQuatT(L.weaponAbs))
    {
        L.active = false;
        L.captured = false;
        L.addValid = false;
        L.kick = QuatT(IDENTITY);
        m_nanRecoveries++;
        return;
    }
    L.ikErr = (L.ikAbs.t - L.lastTarget.t).GetLength();

    const float ab = L.charMatches ? GetLockBlend() : 0.0f;

    // --- Fire animation pass-through -------------------------------------------------------------
    // The animated (pre-modifier) hand of last frame = last frame's final hand minus what we added to it.
    {
        const QuatT finalIk = (R.ikGameValid && SaneQuatT(R.ikGame)) ? R.ikGame : L.ikAbs;
        QuatT anim = finalIk;
        if (L.addValid)
        {
            // Only subtract our add if the skeleton actually applied it: the animation update can be skipped
            // (pause menu, character not updated) while this hook still runs, and subtracting a push that never
            // happened would make the next push grow without bound. Test: the final hand must be near
            // "last animated hand + our add".
            // Two hypotheses for last frame's final hand: "animated + our add" (applied) or "animated only"
            // (the queue did not run). Pick the closer one; the error is then at most the smaller of the two.
            const Vec3 expected = L.animIk.t + L.lastAdd.t;
            const float dApplied = (finalIk.t - expected).GetLengthSquared();
            const float dSkipped = (finalIk.t - L.animIk.t).GetLengthSquared();
            if (dApplied <= dSkipped && dApplied < 0.15f * 0.15f)
            {
                anim.t -= L.lastAdd.t;
                anim.q = SafeNormalized((!L.lastAdd.q) * anim.q);
            }
            else
                L.pushesNotApplied++;
        }
        L.animIk = anim;
        // Relative to the camera of that frame, so head motion does not count as hand motion.
        const QuatT animRelCam = SaneQuatT(L.camAbs) ? (L.camAbs.GetInverted() * anim) : QuatT(IDENTITY);
        L.animRelCam = animRelCam;
        L.animRelCamValid = R.camValid && L.addValid && SaneQuatT(L.camAbs) && SaneQuatT(animRelCam, 5.0f);
        const float dt = clamp_tpl((gEnv && gEnv->pTimer) ? gEnv->pTimer->GetFrameTime() : 0.016f, 0.0f, 0.1f);
        const bool restBad = !L.animRestValid || !SaneQuatT(L.animRestRelCam, 5.0f) || !SaneQuatT(animRelCam, 5.0f)
            || !((animRelCam.t - L.animRestRelCam.t).GetLengthSquared() < 0.5f * 0.5f) || !R.camValid;
        if (restBad)
        {
            L.animRestRelCam = animRelCam;
            L.animRestValid = R.camValid && SaneQuatT(animRelCam, 5.0f);
        }
        else if (m_fireTimer <= 0.0f)
        {
            // Slow reference; frozen while a shot plays out so the kick is measured against the pose before it.
            const float k = 1.0f - expf(-dt / 0.35f);
            L.animRestRelCam = BlendQuatT(L.animRestRelCam, animRelCam, k);
        }
        QuatT dev = L.animRestValid ? (L.animRestRelCam.GetInverted() * animRelCam) : QuatT(IDENTITY); // hand-local deviation
        dev.q = SafeNormalized(dev.q);
        L.kickPos = Finite(dev.t) ? dev.t.GetLength() : 1e9f;
        L.kickRot = 2.0f * acosf(clamp_tpl(fabsf(dev.q.w), 0.0f, 1.0f));
        const WeaponSettings* pW = FindCurrentWeapon();
        const float T = (pW && pW->fireCouplingTime > 0.05f) ? pW->fireCouplingTime : 0.3f;
        // Gate: only around shots (idle breathing / walk bob must not leak into the sights).
        const float gate = (m_fireTimer > 0.0f) ? SmoothStep01(m_fireTimer / (0.3f * T)) : 0.0f;
        const float scale = (pW ? pW->aimKickScale : 1.0f) * gate;
        if (scale > 0.0f && ab > 0.0f && L.animRestValid && L.captured && L.kickPos < 0.25f && L.kickRot < DEG2RAD(45.0f))
        {
            // dev is the hand's motion in the hand's own axes. The weapon rides rigidly on the hand
            // (weapon = hand * weaponRel), so the same motion in the weapon's axes is weaponRel^-1 * dev * weaponRel.
            QuatT kickW = L.weaponRel.GetInverted() * dev * L.weaponRel;
            kickW.q = SafeNormalized(kickW.q);
            L.kick = SaneQuatT(kickW, 1.0f) ? ScaleQuatT(kickW, scale) : QuatT(IDENTITY);
        }
        else
            L.kick = QuatT(IDENTITY);
    }

    IEntity* pEnt = pPlayer->GetEntity();
    const Quat entRot = SafeNormalized(pEnt->GetWorldRotation());
    const Quat freshRot = SafeNormalized(pPlayer->m_camera.m_rotation); // this frame's view rotation (updated in PrePhysicsUpdate)

    // Best available camera for this frame: last frame's exact camera (position in model space is
    // frame-independent; a frame of head motion is a millimetre) with this frame's mouse look applied.
    // Nothing here is filtered or integrated; the render-side step below fixes whatever is left.
    QuatT camAbs(IDENTITY);
    if (R.camValid && SaneQuat(R.freshRotAtCam) && SaneQuat(R.camWorldRot) && Finite(R.camModelPos))
    {
        const Quat delta = SafeNormalized(freshRot * (!R.freshRotAtCam));
        const Quat camRotWorld = SafeNormalized(delta * R.camWorldRot);
        camAbs.q = SafeNormalized((!entRot) * camRotWorld);
        camAbs.t = R.camModelPos;
    }
    else
    {
        const QuatT camBone = pPlayer->GetBoneTransform(BONE_CAMERA);
        camAbs.q = SafeNormalized((!entRot) * freshRot);
        camAbs.t = Finite(camBone.t) ? camBone.t : Vec3(0.0f, 0.0f, 1.6f);
    }

    PushAimLock(pModifier, camAbs, L.ikAbs, L.weaponAbs, ab);
}

void ModMain::PushAimLock(void* pModifier, const QuatT& camAbs, const QuatT& ikAbs, const QuatT& weaponBone, float ab)
{
    using namespace PreyInternals;
    AimLockState& L = m_lock;

    L.camAbs = camAbs;

    if (ab <= 0.0f || !pModifier)
    {
        L.active = false;
        L.captured = false;
        L.addValid = false;
        L.kick = QuatT(IDENTITY);
        return;
    }

    if (!L.captured || L.weaponClass != m_currentWeaponClass)
    {
        const QuatT hipRel = camAbs.GetInverted() * ikAbs;       // where the hand is now, relative to the camera
        const QuatT weaponRel = ikAbs.GetInverted() * weaponBone; // hand -> weapon bone, assumed rigid
        if (!SaneQuatT(hipRel, 5.0f) || !SaneQuatT(weaponRel, 5.0f))
        {
            // Not a usable pose (skeleton still settling): try again next frame, push nothing.
            L.active = false;
            L.addValid = false;
            L.kick = QuatT(IDENTITY);
            m_nanRecoveries++;
            return;
        }
        if (!L.active)
            L.hipRel = hipRel;
        L.weaponRel = weaponRel;
        L.weaponClass = m_currentWeaponClass;
        L.captured = true;
        L.animRestValid = false;
    }
    L.active = true;

    const WeaponSettings& w = GetCurrentWeapon();
    const QuatT desiredRelCam = ComputeAimExtra() * w.aim.AsQuatT() * ComputeAimLocal() * L.kick;

    const QuatT aimWeaponAbs = camAbs * desiredRelCam;                  // where the weapon bone should be
    const QuatT aimIkAbs = aimWeaponAbs * L.weaponRel.GetInverted();    // -> where the hand IK target must be
    // Hip side of the blend: the LIVE animated hand (last frame's, following the camera), not the pose
    // captured when aiming started - so whatever the hip path does meanwhile (sprint pose, view drag,
    // crouch) is what the weapon returns to, without a pop when the lock releases.
    const QuatT hipAbs = camAbs * (L.animRelCamValid ? L.animRelCam : L.hipRel);
    QuatT target = BlendQuatT(hipAbs, aimIkAbs, ab);
    target.q.Normalize();

    // Additive, not override: the fire animation keeps moving the hand (the deviation is measured above
    // and fed back into the aim pose). The add is relative to last frame's animated hand; the frame of
    // animation velocity that leaves is fixed exactly on the render side.
    QuatT add;
    add.t = target.t - L.animIk.t;
    add.q = SafeNormalized(target.q * (!L.animIk.q));
    // Never push anything unreasonable into the skeleton: a hand more than ~a metre from where the
    // animation put it means our bookkeeping is off - drop this frame and start the additive chain afresh.
    if (!SaneQuatT(target, 50.0f) || !Finite(add.t) || add.t.GetLengthSquared() > 1.0f * 1.0f)
    {
        L.addValid = false;
        L.kick = QuatT(IDENTITY);
        m_nanRecoveries++;
        return;
    }
    VCall<void>(pModifier, VT_IAnimationOperatorQueue_PushPosition, L.rightIkJoint, OP_ADDITIVE, &add.t);
    VCall<void>(pModifier, VT_IAnimationOperatorQueue_PushOrientation, L.rightIkJoint, OP_ADDITIVE, &add.q);
    L.lastAdd = add;
    L.addValid = true;
    L.lastTarget = target;
}

//---------------------------------------------------------------------------------
// Render side (after the camera is final)
//---------------------------------------------------------------------------------
void ModMain::UpdateSkeletonCache(void* pCharInst, void* pAttachment)
{
    using namespace PreyInternals;
    RenderLockState& R = m_render;

    void* pSkel = VCall<void*>(pCharInst, VT_ICharacterInstance_GetIDefaultSkeleton);
    if (!pSkel)
        return;
    const int count = (int)VCall<unsigned>(pSkel, VT_IDefaultSkeleton_GetJointCount);
    const int attachJoint = Member<int>(pAttachment, OFFSET_CAttachmentBONE_JOINT_ID);
    if (pSkel == R.skeleton && count == R.jointCount && attachJoint == R.attachJoint)
        return;

    R.skeleton = pSkel;
    R.jointCount = count;
    R.attachJoint = attachJoint;
    R.rightSubtree.clear();
    R.leftSubtree.clear();
    R.rightHand = R.leftHand = -1;
    R.attachJointName.clear();
    R.rightHandName.clear();
    R.leftHandName.clear();
    if (count <= 0 || count > 4096)
        return;

    auto jointName = [&](int id) -> const char* {
        if (id < 0 || id >= count) return "";
        const char* n = VCall<const char*>(pSkel, VT_IDefaultSkeleton_GetJointNameByID, id);
        return n ? n : "";
    };
    auto parentOf = [&](int id) -> int {
        if (id < 0 || id >= count) return -1;
        return VCall<int>(pSkel, VT_IDefaultSkeleton_GetJointParentIDByID, id);
    };
    // The weapon hangs off a "prop" joint that is a child of the hand joint. Move the whole hand.
    auto handOf = [&](int propJoint) -> int {
        if (propJoint < 0 || propJoint >= count) return -1;
        const char* n = jointName(propJoint);
        if (strstr(n, "Prop") || strstr(n, "prop") || strstr(n, "weapon"))
        {
            const int p = parentOf(propJoint);
            return p >= 0 ? p : propJoint;
        }
        return propJoint;
    };

    R.attachJointName = jointName(attachJoint);
    R.rightHand = handOf(attachJoint);
    ArkPlayer* pPlayer = ArkPlayer::GetInstancePtr();
    const int leftProp = pPlayer ? pPlayer->m_camera.GetJointId((int)ArkPlayerCamera::EArkBoneList::BONE_WEAPON2) : -1;
    R.leftHand = handOf(leftProp);
    R.rightHandName = jointName(R.rightHand);
    R.leftHandName = jointName(R.leftHand);

    // Subtrees (CryEngine guarantees parent index < child index).
    std::vector<char> inRight(count, 0), inLeft(count, 0);
    for (int i = 0; i < count; i++)
    {
        const int p = parentOf(i);
        inRight[i] = (i == R.rightHand) || (p >= 0 && inRight[p]);
        inLeft[i] = (i == R.leftHand) || (p >= 0 && inLeft[p]);
        if (inRight[i]) R.rightSubtree.push_back(i);
        if (inLeft[i] && !inRight[i]) R.leftSubtree.push_back(i);
    }
}

void ModMain::OnWeaponFired(CArkWeapon* pWeapon)
{
    ArkPlayer* pPlayer = ArkPlayer::GetInstancePtr();
    if (!pPlayer || !pPlayer->GetEntity() || !pWeapon)
        return;
    if (pWeapon->GetOwnerId() != pPlayer->GetEntity()->GetId())
        return;
    m_shotPending = true;
}

void ModMain::OnCameraUpdated(ArkPlayerCamera* pCamera, SViewParams& params)
{
    using namespace PreyInternals;
    RenderLockState& R = m_render;
    const ViewmodelSettings& s = m_settings;

    if (R.frameOfLastCall != m_frameIndex)
    {
        R.callsLastFrame = R.callsThisFrame;
        R.callsThisFrame = 0;
        R.frameOfLastCall = m_frameIndex;
    }
    R.callsThisFrame++;
    R.ikGameValid = false; // re-captured below when everything checks out; stale values must never survive an early return

    ArkPlayer* pPlayer = ArkPlayer::GetInstancePtr();
    if (!pPlayer || pCamera != &pPlayer->m_camera || !pPlayer->GetEntity())
        return;
    IEntity* pEnt = pPlayer->GetEntity();

    // --- Exact camera of this frame, in model (entity) space -------------------------------------
    // With camera-space rendering (ENTITY_SLOT_RENDER_NEAREST on the arms) UpdateView returns the
    // position relative to the entity; CView adds the entity position afterwards. Detect the other case.
    // Anything that is not a number (first frames of a load, a camera that has not been set up yet) means
    // this frame is skipped and every accumulated state is invalidated rather than poisoned.
    if (!SaneQuat(params.rotation) || !Finite(params.position) || !SaneQuat(pEnt->GetWorldRotation()) || !Finite(pEnt->GetWorldPos()))
    {
        R.camValid = false;
        R.attachValid = false;
        R.bobValid = false;
        m_nanRecoveries++;
        return;
    }
    const Quat entRot = SafeNormalized(pEnt->GetWorldRotation());
    Vec3 camPosRel = params.position;
    R.positionWasWorld = camPosRel.GetLengthSquared() > 20.0f * 20.0f;
    if (R.positionWasWorld)
        camPosRel -= pEnt->GetWorldPos();
    const Quat camRotWorld = SafeNormalized(params.rotation);

    QuatT camModel;
    camModel.q = SafeNormalized((!entRot) * camRotWorld);
    camModel.t = (!entRot) * camPosRel;
    if (!SaneQuatT(camModel, 50.0f))
    {
        R.camValid = false;
        R.attachValid = false;
        R.bobValid = false;
        m_nanRecoveries++;
        return;
    }
    R.camModel = camModel;

    R.camWorldRot = camRotWorld;
    R.camModelPos = camModel.t;
    R.freshRotAtCam = pPlayer->m_camera.m_rotation;
    R.camValid = true;

    const float dt = (gEnv && gEnv->pTimer) ? gEnv->pTimer->GetFrameTime() : 0.016f;

    // Head bob = camera motion minus its slow component (posture, crouch, lean, eye height).
    {
        if (!R.bobValid || !Finite(R.camLowPass) || !((camModel.t - R.camLowPass).GetLengthSquared() < 1.0f))
        {
            R.camLowPass = camModel.t;
            R.bobValid = true;
        }
        const float tau = max(s.aimBobTau, 0.02f);
        const float k = 1.0f - expf(-dt / tau);
        R.camLowPass += (camModel.t - R.camLowPass) * k;
        Vec3 bob = camModel.t - R.camLowPass;
        const float maxBob = 0.05f;
        if (bob.GetLength() > maxBob)
            bob = bob.GetNormalized() * maxBob;
        R.bobCam = (!camModel.q) * bob;
    }

    // Shot detection: the camera recoil timer is re-armed by every shot, and the procedural recoil
    // offset jumps. Either one starts the post-shot coupling boost.
    {
        const float camRecoil = pPlayer->m_camera.m_recoilTimeRemaining;
        const float recoilMag = m_gameOffsets[2].t.GetLength();
        const bool shot = m_shotPending || (camRecoil > m_prevCamRecoilTime + 1e-4f) || (recoilMag > m_prevRecoilMag + 0.002f && recoilMag > 0.003f);
        m_shotPending = false;
        m_prevCamRecoilTime = camRecoil;
        m_prevRecoilMag = recoilMag;
        if (shot)
        {
            m_shotsSeen++;
            const WeaponSettings* pW = FindCurrentWeapon();
            m_fireTimer = max(m_fireTimer, pW ? pW->fireCouplingTime : 0.3f);
        }
        else if (m_fireTimer > 0.0f)
            m_fireTimer = max(0.0f, m_fireTimer - dt);
    }

    // --- Weapon attachment ------------------------------------------------------------------------
    if (m_playerDead || m_reviveGuard > 0.0f)
    {
        R.attachValid = false;
        return;
    }
    CArkWeapon* pWeapon = pPlayer->m_weaponComponent.GetEquippedWeapon();
    // Only a weapon the player actually owns has a live attachment on the arms.
    if (pWeapon && pWeapon->GetOwnerId() != pEnt->GetId())
        pWeapon = nullptr;
    void* pAttachment = pWeapon ? static_cast<void*>(pWeapon->m_pAttachment) : nullptr;
    ICharacterInstance* pCharInst = pEnt->GetCharacter(0);
    if (!pAttachment || !pCharInst)
    {
        R.attachValid = false;
        return;
    }
    UpdateSkeletonCache(pCharInst, pAttachment);

    QuatT* pModelRel = const_cast<QuatT*>(VCall<const QuatT*>(pAttachment, VT_IAttachment_GetAttModelRelative));
    if (!pModelRel)
    {
        R.attachValid = false;
        return;
    }
    const QuatT gameModel = *pModelRel;
    const QuatT weaponBone = pPlayer->GetBoneTransform(BONE_WEAPON);
    if (!SaneQuatT(gameModel, 50.0f) || !SaneQuatT(weaponBone, 50.0f))
    {
        // The game's own values are not usable this frame (attachment being recreated, model reload):
        // leave everything as the game has it and forget our references.
        R.attachValid = false;
        R.attOffsetValid = false;
        R.hipValid = false;
        R.ikGameValid = false;
        m_nanRecoveries++;
        return;
    }
    R.attachValid = true;
    R.weaponModelGame = gameModel;
    R.weaponRelCam = camModel.GetInverted() * gameModel;
    // Final (unmodified) weapon bone of this frame and its constant offset to the attachment, so the
    // skeleton-side push next frame targets the very same thing we place here.
    R.weaponBoneGame = weaponBone;
    R.attOffset = R.weaponBoneGame.GetInverted() * gameModel;
    R.attOffsetValid = true;
    R.ikGameValid = false;
    if (m_lock.rightIkJoint >= 0)
    {
        if (void* pSkelPose0 = VCall<void*>(pCharInst, VT_ICharacterInstance_GetISkeletonPose))
            if (const QuatT* pIk = VCall<const QuatT*>(pSkelPose0, VT_ISkeletonPose_GetAbsJointByID, m_lock.rightIkJoint))
            {
                R.ikGame = *pIk;
                R.ikGameValid = SaneQuatT(R.ikGame, 50.0f);
            }
    }

    const float ab = (m_lock.charMatches || !m_lock.active) ? GetLockBlend() : 0.0f;
    if (ab <= 0.0f)
    {
        // Not aiming: this is the exact hip pose of the weapon relative to the camera.
        R.hipRelCam = R.weaponRelCam;
        R.hipValid = true;
        R.residualPos = 0.0f;
        R.residualRot = 0.0f;
    }

    // --- Desired transform ------------------------------------------------------------------------
    QuatT desiredModel = gameModel;
    bool changed = false;
    if (ab > 0.0f && s.aimRenderLock)
    {
        const WeaponSettings& w = GetCurrentWeapon();
        const QuatT aimRelCam = ComputeAimExtra() * w.aim.AsQuatT() * ComputeAimLocal() * m_lock.kick;
        QuatT desiredRelCam = BlendQuatT(R.weaponRelCam, aimRelCam, ab);
        desiredRelCam.q.Normalize();
        desiredModel = camModel * desiredRelCam;
        changed = true;
    }
    if (Active() && s.testOffsetUp != 0.0f)
    {
        desiredModel.t += (camModel.q * Vec3(0.0f, 0.0f, s.testOffsetUp));
        changed = true;
    }
    if (!changed)
        return;
    desiredModel.q = SafeNormalized(desiredModel.q);
    if (!SaneQuatT(desiredModel, 50.0f) || !((desiredModel.t - gameModel.t).GetLengthSquared() < 2.0f * 2.0f))
    {
        // Our own numbers went bad (or absurd): leave the game's pose for this frame and restart the
        // lock's accumulated state so the next frame begins from clean values.
        m_lock.addValid = false;
        m_lock.animRestValid = false;
        m_lock.kick = QuatT(IDENTITY);
        R.bobValid = false;
        m_nanRecoveries++;
        return;
    }
    R.desiredModel = desiredModel;

    // Rigid delta that takes the game's weapon transform to ours (model space).
    QuatT D = desiredModel * gameModel.GetInverted();
    D.q = SafeNormalized(D.q);
    R.residualPos = D.t.GetLength();
    {
        Quat dq = desiredModel.q * (!gameModel.q);
        dq.Normalize();
        R.residualRot = 2.0f * acosf(clamp_tpl(fabsf(dq.w), 0.0f, 1.0f));
    }
    if (ab > 0.0f && s.aimRenderLock)
    {
        R.residualPosAcc = max(R.residualPosAcc, R.residualPos);
        R.residualTimer += dt;
        if (R.residualTimer > 1.0f)
        {
            R.residualPosMax1s = R.residualPosAcc;
            R.residualPosAcc = 0.0f;
            R.residualTimer = 0.0f;
        }
    }

    *pModelRel = desiredModel;

    // --- Hands: move the final hand poses by the same rigid delta so they stay on the grip. ----------
    const bool wantHands = (ab > 0.0f && s.aimRenderLock) ? (s.aimHandsFollow != 0) : (s.testHands != 0);
    if (!wantHands)
        return;
    void* pSkelPose = VCall<void*>(pCharInst, VT_ICharacterInstance_GetISkeletonPose);
    if (!pSkelPose)
        return;
    auto jointRef = [&](int id) -> QuatT* {
        return const_cast<QuatT*>(VCall<const QuatT*>(pSkelPose, VT_ISkeletonPose_GetAbsJointByID, id));
    };
    for (int j : R.rightSubtree)
        if (QuatT* p = jointRef(j))
            if (SaneQuatT(*p, 50.0f))
                *p = D * (*p);

    R.leftOnWeapon = false;
    if (R.leftHand >= 0 && (s.aimLeftHandFollow || ab <= 0.0f))
    {
        if (QuatT* pl = jointRef(R.leftHand))
        {
            R.leftHandDist = Finite(pl->t) ? (pl->t - gameModel.t).GetLength() : 1e9f;
            R.leftOnWeapon = R.leftHandDist < 0.35f;
        }
        if (R.leftOnWeapon)
            for (int j : R.leftSubtree)
                if (QuatT* p = jointRef(j))
                    if (SaneQuatT(*p, 50.0f))
                        *p = D * (*p);
    }
}

void ModMain::UpdateConvergence(float dt)
{
    const ViewmodelSettings& s = m_settings;

    const bool wantConverge = s.convergeEnabled != 0;
    const bool wantWall = s.wallPushEnabled != 0;
    const bool wantBlock = s.aimWallBlockEnabled != 0 && s.aimEnabled != 0;
    if (!Active() || (!wantConverge && !wantWall && !wantBlock) || !gEnv || !gEnv->pPhysicalWorld || !gEnv->pSystem)
    {
        m_convergeTargetYaw = m_convergeTargetPitch = 0.0f;
        m_convergeYaw = m_convergePitch = 0.0f;
        m_wallPush = m_wallPushTarget = 0.0f;
        m_convergeHit = false;
        m_aimBlockedByWall = false;
        return;
    }

    ArkPlayer* pPlayer = ArkPlayer::GetInstancePtr();
    if (!pPlayer)
        return;
    IPhysicalEntity* pSkip = pPlayer->GetEntity() ? pPlayer->GetEntity()->GetPhysics() : nullptr;

    // Previous frame's rendered camera is plenty accurate for a smoothed convergence target.
    const Matrix34 cam = gEnv->pSystem->GetViewCamera().GetMatrix();
    const Vec3 camPos = cam.GetTranslation();
    const Vec3 dir = cam.GetColumn1().GetNormalized(); // CryEngine forward = +Y

    const float maxDist = clamp_tpl(s.convergeMaxDist, 1.0f, 200.0f);
    PaddedRayHit hit; // padded: the game's ray_hit may be larger than the SDK's
    const int n = gEnv->pPhysicalWorld->RayWorldIntersection(camPos, dir * maxDist, ent_all,
        rwi_stop_at_pierceable | rwi_colltype_any(geom_colltype_ray | geom_colltype0 | geom_colltype_player), &hit, 1, pSkip);
    m_convergeHit = (n > 0);
    m_convergeDist = (m_convergeHit && Finite(hit.dist)) ? hit.dist : maxDist;

    // --- Convergence: point the barrel at the impact point --------------------------------------
    float yaw = 0.0f, pitch = 0.0f;
    if (wantConverge)
    {
        // Weapon offset from the camera in view space (exact, measured at render time while not aiming).
        const Vec3 w = (m_render.hipValid && Finite(m_render.hipRelCam.t)) ? m_render.hipRelCam.t : Vec3(0.0f, 0.25f, -0.1f);
        const float d = max((Finite(m_convergeDist) ? m_convergeDist : 100.0f) - w.y, 0.15f); // distance from roughly the weapon to the target
        yaw = RAD2DEG(atan2f(w.x, d));   // weapon right of the eye -> turn left (+yaw)
        pitch = RAD2DEG(atan2f(-w.z, d)); // weapon below the eye -> tilt up (+pitch)
        const float maxA = clamp_tpl(s.convergeMaxAngle, 0.0f, 45.0f);
        yaw = clamp_tpl(yaw, -maxA, maxA) * clamp_tpl(s.convergeStrength, 0.0f, 1.0f);
        pitch = clamp_tpl(pitch, -maxA, maxA) * clamp_tpl(s.convergeStrength, 0.0f, 1.0f);
    }
    m_convergeTargetYaw = yaw;
    m_convergeTargetPitch = pitch;
    {
        const float tau = max(s.convergeSmoothTime, 0.0f);
        const float k = (tau > 0.0005f) ? (1.0f - expf(-dt / tau)) : 1.0f;
        m_convergeYaw += (yaw - m_convergeYaw) * k;
        m_convergePitch += (pitch - m_convergePitch) * k;
        if (!Finite(m_convergeYaw) || !Finite(m_convergePitch))
        {
            m_convergeYaw = m_convergePitch = 0.0f;
            m_nanRecoveries++;
        }
    }

    // --- Wall pull-back: move the weapon towards the camera when something is close ------------
    float push = 0.0f;
    if (wantWall && m_convergeHit)
    {
        const WeaponSettings* pW = FindCurrentWeapon();
        const float amount = pW ? pW->wallPush : WeaponSettings().wallPush;
        const float startD = max(s.wallPushStartDist, 0.05f);
        const float fullD = clamp_tpl(s.wallPushFullDist, 0.0f, startD - 0.01f);
        // 0 at startD, 1 at fullD (and closer), eased so it does not kick in abruptly.
        const float t = SmoothStep01((startD - m_convergeDist) / (startD - fullD));
        push = clamp_tpl(amount, 0.0f, 0.5f) * t;
    }
    m_wallPushTarget = push;
    {
        const float tau = max(s.wallPushSmoothTime, 0.0f);
        const float k = (tau > 0.0005f) ? (1.0f - expf(-dt / tau)) : 1.0f;
        m_wallPush += (push - m_wallPush) * k;
        if (!Finite(m_wallPush))
        {
            m_wallPush = 0.0f;
            m_nanRecoveries++;
        }
    }

    // --- Aim block: no ironsights while the weapon would be poking into the wall -----------------
    // Reach estimate = how far the weapon sits in front of the eye + its wall pull-back (longer
    // weapons have more), times the user's tolerance. Small hysteresis so it cannot flicker.
    if (wantBlock)
    {
        const WeaponSettings* pW = FindCurrentWeapon();
        const float reach = max(m_render.hipRelCam.t.y, 0.15f) + (pW ? pW->wallPush : WeaponSettings().wallPush);
        m_aimBlockDist = reach * clamp_tpl(s.aimWallBlockScale, 0.1f, 5.0f);
        if (!m_convergeHit)
            m_aimBlockedByWall = false;
        else if (m_aimBlockedByWall)
            m_aimBlockedByWall = m_convergeDist < m_aimBlockDist * 1.1f;
        else
            m_aimBlockedByWall = m_convergeDist < m_aimBlockDist;
    }
    else
    {
        m_aimBlockedByWall = false;
    }
}

float ModMain::GetAimSensitivityMultiplier(float currentMultiplier, float zoomedMultiplier)
{
    // Only while our own aim zoom is active, and only for the camera-speed call (the same function
    // also blends the walk-speed multiplier; we tell the two apart by the weapon's zoomed camera stat).
    if (m_cameraZoomHandle == 0 || m_settings.aimSensMode == 0)
        return 0.0f;
    ArkPlayer* pPlayer = ArkPlayer::GetInstancePtr();
    CArkWeapon* pWeapon = pPlayer ? pPlayer->m_weaponComponent.GetEquippedWeapon() : nullptr;
    if (!pWeapon)
        return 0.0f;
    if (fabsf(pWeapon->GetZoomedCameraSpeedStat() - zoomedMultiplier) > 1e-6f)
        return 0.0f; // walk speed call
    m_sensHookCalls++;
    const float scale = (m_settings.aimSensMode == 1) ? clamp_tpl(m_settings.aimCameraZoomFactor, 0.2f, 1.5f)
                                                       : clamp_tpl(m_settings.aimSensScale, 0.05f, 3.0f);
    return currentMultiplier * scale;
}

float ModMain::GetSpreadMultiplier(const CArkItem* pWeapon)
{
    if (!Active() || !pWeapon)
        return 1.0f;
    ArkPlayer* pPlayer = ArkPlayer::GetInstancePtr();
    if (!pPlayer || !pPlayer->GetEntity() || pWeapon->GetOwnerId() != pPlayer->GetEntity()->GetId())
        return 1.0f; // not the player's weapon
    m_spreadHookCalls++;
    // Per-weapon factors (the pistol and the shotgun share the game's "shotgun" weapon class).
    IEntity* pEnt = pWeapon->GetEntity();
    if (!pEnt || !pEnt->GetClass())
        return 1.0f;
    const WeaponSettings* pW = FindWeapon(pEnt->GetClass()->GetName());
    if (!pW)
        return 1.0f;
    const float ab = m_settings.aimEnabled ? SmoothStep01(m_aimBlend) : 0.0f;
    const float hip = clamp_tpl(pW->hipSpreadMult, 0.0f, 5.0f);
    const float ads = clamp_tpl(pW->aimSpreadMult, 0.0f, 5.0f);
    const float m = LERP(hip, ads, ab);
    return Finite(m) ? clamp_tpl(m, 0.0f, 5.0f) : 1.0f;
}

//---------------------------------------------------------------------------------
// State
//---------------------------------------------------------------------------------
void ModMain::UpdateBlendStates(float dt)
{
    ArkPlayer* pPlayer = ArkPlayer::GetInstancePtr();

    bool crouching = false;
    bool aiming = false;

    if (pPlayer)
    {
        // Dead (ragdoll / death model) or just revived (model reloaded, attachments recreated):
        // keep our hands off the skeleton and the weapon attachment for a moment.
        const bool dead = pPlayer->m_playerComponent.GetHealthComponent().IsDead();
        if (m_playerDead && !dead)
            m_reviveGuard = 0.75f;
        m_playerDead = dead;
        if (m_reviveGuard > 0.0f)
            m_reviveGuard -= dt;
        if (dead)
            m_aimKeyHeld = m_settings.aimToggle ? false : m_aimKeyHeld;

        const EStance stance = pPlayer->m_stance;
        crouching = (stance == EStance::STANCE_SNEAK || stance == EStance::STANCE_CRAWL);
        // Zero-G: the thruster boost reports as sprinting, but there is no running body to lower the weapon
        // for, so it only counts as a sprint if the user asks for it.
        m_feel.zeroG = pPlayer->m_movementFSM.IsInZeroG();
        m_feel.sprinting = !dead && pPlayer->m_movementFSM.IsSprinting() && (!m_feel.zeroG || m_settings.sprintInZeroG);

        m_currentWeaponClass = GetWeaponClassName(pPlayer);

        // Weapon state: reloading, and "switching" = holster / draw in progress or any weapon action that
        // leaves the weapon not ready (select, reload action, charge release).
        {
            const ArkPlayerWeaponComponent& wc = pPlayer->m_weaponComponent;
            CArkWeapon* pWeapon = wc.GetEquippedWeapon();
            if (pWeapon && pWeapon->GetOwnerId() != pPlayer->GetEntity()->GetId())
                pWeapon = nullptr;
            m_wsReloading = pWeapon && pWeapon->m_bIsReloading;
            // "Ready to attack" is cleared for select, reload AND every fire action (pump / bolt / beam cycle),
            // so it cannot be used on its own; it only tells us when a freshly drawn weapon has finished coming up.
            m_wsReady = !pWeapon || pWeapon->m_bIsReadyToAttack;
            m_wsUnequipping = (pWeapon && pWeapon->m_bIsUnequipping) || wc.m_bIsUnequipping
                || (wc.m_toBeEquippedWeaponId != 0 && wc.m_toBeEquippedWeaponId != wc.m_equippedWeaponId);
            const unsigned curId = pWeapon ? wc.m_equippedWeaponId : 0u;
            if (curId != m_wsLastWeaponId)
            {
                m_wsLastWeaponId = curId;
                m_wsDrawing = curId != 0; // a new weapon: its draw animation is playing until it reports ready
                m_wsDrawTimer = 0.0f;
            }
            if (m_wsDrawing)
            {
                // The ready flag comes back at equip time, before the raise animation ends, so it is only a lower
                // bound: also wait out the configured draw time (and never more than a few seconds in any case).
                m_wsDrawTimer += dt;
                const float minHold = clamp_tpl(m_settings.aimSwitchDelay, 0.0f, 3.0f);
                if ((m_wsReady && m_wsDrawTimer >= minHold) || m_wsDrawTimer > max(3.0f, minHold))
                    m_wsDrawing = false;
            }
            m_wsSwitching = m_wsUnequipping || m_wsDrawing;
        }

        // Aim key held/toggled, weapon allows aiming, and the player is actually controlling the
        // character (no cursor on screen: menus, inventory, our own settings window...).
        const WeaponSettings* pW = FindCurrentWeapon();
        const bool weaponAllows = !m_currentWeaponClass.empty() && (!pW || pW->aimAllowed);
        aiming = Active() && m_settings.aimEnabled && m_aimKeyHeld && weaponAllows && !m_mouseCaptured && !IsHardwareCursorVisible()
                 && !(m_settings.aimWallBlockEnabled && m_aimBlockedByWall) && !dead && m_reviveGuard <= 0.0f
                 && !(m_settings.sprintPoseEnabled && m_settings.sprintBlocksAim && m_feel.sprinting)
                 && !(m_settings.aimBlockReload && m_wsReloading)
                 && !(m_settings.aimBlockSwitch && m_wsSwitching);
    }
    else
    {
        m_aimKeyHeld = false;
        m_currentWeaponClass.clear();
        m_feel.sprinting = false;
        m_feel.zeroG = false;
        m_wsReloading = m_wsUnequipping = m_wsSwitching = m_wsDrawing = false;
        m_wsReady = true;
        m_wsLastWeaponId = 0;
    }

    if (!aiming && m_settings.aimToggle && (IsHardwareCursorVisible() || !pPlayer))
        m_aimKeyHeld = false; // drop a stale toggle when a menu opens

    m_isCrouching = crouching;
    m_isAiming = aiming;

    UpdateCameraZoom(aiming);

    MoveTowards(m_crouchBlend, crouching ? 1.0f : 0.0f, m_settings.crouchTime, dt);
    MoveTowards(m_aimBlend, aiming ? 1.0f : 0.0f, m_settings.aimTime, dt);
    MoveTowards(m_reloadFade, (m_settings.reloadFadesOffsets && m_wsReloading && Active()) ? 1.0f : 0.0f, m_settings.reloadFadeTime, dt);
    if (!Finite(m_crouchBlend)) { m_crouchBlend = 0.0f; m_nanRecoveries++; }
    if (!Finite(m_aimBlend)) { m_aimBlend = 0.0f; m_nanRecoveries++; }
    if (!Finite(m_reloadFade)) { m_reloadFade = 0.0f; m_nanRecoveries++; }
}

//---------------------------------------------------------------------------------
// Feel: sprint pose, aim sway & settle, view drag. Pure state, evaluated once per frame; the outputs
// are added in ApplyOffset (hip path) and ComputeAimLocal (aim path).
//---------------------------------------------------------------------------------
static inline float WrapAngle(float a)
{
    while (a > gf_PI) a -= 2.0f * gf_PI;
    while (a < -gf_PI) a += 2.0f * gf_PI;
    return a;
}

void ModMain::UpdateFeel(float dt, ArkPlayer* pPlayer)
{
    const ViewmodelSettings& s = m_settings;
    FeelState& f = m_feel;
    dt = clamp_tpl(dt, 0.0f, 0.1f);
    if (!f.lastLookValid)
        f.steadyLeft = s.steadyDuration; // first frame (or after a reset): a full breath

    IEntity* pEnt = pPlayer ? pPlayer->GetEntity() : nullptr;
    if (!pEnt)
    {
        f = FeelState();
        f.steadyLeft = s.steadyDuration;
        return;
    }

    // --- Player speed (horizontal) ---
    f.speed = 0.0f;
    if (IPhysicalEntity* pPhys = pEnt->GetPhysics())
    {
        PreyStatusLiving living; // NOT pe_status_living: see the struct's comment
        if (pPhys->GetStatus(&living) && Finite(living.vel))
            f.speed = Vec2(living.vel.x, living.vel.y).GetLength();
    }
    f.speedNorm = clamp_tpl(f.speed / 3.0f, 0.0f, 1.5f);

    // --- Sprint blend and phase ---
    if (f.sprinting)
        f.timeSinceSprint = 0.0f;
    else
        f.timeSinceSprint += dt;
    const bool wantSprintPose = s.sprintPoseEnabled && f.sprinting;
    MoveTowards(f.sprintBlend, wantSprintPose ? 1.0f : 0.0f, wantSprintPose ? s.sprintBlendIn : s.sprintBlendOut, dt);
    f.sprintOut = PoseOffset();
    if (f.sprintBlend > 0.0f)
    {
        f.sprintOut.AddScaled(s.sprint, f.sprintBlend);
        if (s.sprintSwayEnabled)
        {
            f.sprintPhase = fmodf(f.sprintPhase + 2.0f * gf_PI * max(s.sprintSwayFreq, 0.01f) * dt, 2.0f * gf_PI);
            const float k = f.sprintBlend;
            PoseOffset sw;
            sw.posX = s.sprintSwayPos * sinf(f.sprintPhase);
            sw.posZ = s.sprintSwayPos * 0.6f * sinf(2.0f * f.sprintPhase);
            sw.roll = s.sprintSwayRot * sinf(f.sprintPhase);
            sw.pitch = s.sprintSwayRot * 0.4f * sinf(2.0f * f.sprintPhase + 0.5f);
            f.sprintOut.AddScaled(sw, k);
        }
    }
    else
        f.sprintPhase = 0.0f;

    // --- Aim sway & settle ---
    f.swayOut = PoseOffset();
    if (m_aimBlend > 0.0f)
        f.aimTime += dt;
    else
        f.aimTime = 0.0f;
    // Breath: held only while aiming and with breath left; recovers otherwise.
    const bool wantSteady = s.steadyEnabled && f.steadyHeld && m_isAiming;
    if (wantSteady && f.steadyLeft > 0.0f)
    {
        f.steadyActive = true;
        f.steadyLeft = max(0.0f, f.steadyLeft - dt);
    }
    else
    {
        f.steadyActive = false;
        if (s.steadyRecover > 0.01f)
            f.steadyLeft = min(s.steadyDuration, f.steadyLeft + dt * s.steadyDuration / s.steadyRecover);
        else
            f.steadyLeft = s.steadyDuration;
    }
    if (s.aimSwayEnabled && m_aimBlend > 0.0f)
    {
        f.swayPhase = fmodf(f.swayPhase + 2.0f * gf_PI * max(s.aimSwayFreq, 0.01f) * dt, 2.0f * gf_PI);
        float amp = 1.0f;
        if (s.aimSwaySettleTime > 0.01f)
            amp *= 1.0f + (max(s.aimSwayInitial, 1.0f) - 1.0f) * expf(-f.aimTime / s.aimSwaySettleTime);
        amp *= 1.0f + s.aimSwayMoveMult * f.speedNorm;
        if (s.aimSwaySprintRecover > 0.01f)
            amp *= 1.0f + s.aimSwaySprintPenalty * expf(-f.timeSinceSprint / s.aimSwaySprintRecover);
        if (f.steadyActive)
            amp *= 1.0f - clamp_tpl(s.steadyReduce, 0.0f, 1.0f);
        f.swayAmplitude = amp;
        const float ph = f.swayPhase;
        // Figure-eight: slow axis sideways, twice the rate vertically; the rotation is what moves the sights.
        f.swayOut.posX = s.aimSwayPos * amp * sinf(ph);
        f.swayOut.posZ = s.aimSwayPos * amp * 0.5f * sinf(2.0f * ph + 1.57f);
        f.swayOut.yaw = s.aimSwayRot * amp * sinf(ph + 0.3f);
        f.swayOut.pitch = s.aimSwayRot * amp * 0.6f * sinf(2.0f * ph);
    }
    else
    {
        f.swayAmplitude = 0.0f;
        if (m_aimBlend <= 0.0f)
            f.swayPhase = 0.0f;
    }

    // --- View drag (spring on the turn rate) ---
    f.dragHipOut = PoseOffset();
    f.dragAimOut = PoseOffset();
    {
        const Quat camQ = pPlayer->m_camera.m_rotation;
        const bool lookOk = camQ.IsValid();
        const Ang3 look = lookOk ? SafeAng3(camQ) : Ang3(ZERO);
        const float yaw = look.z, pitch = look.x;
        float dYaw = 0.0f, dPitch = 0.0f;
        if (lookOk && f.lastLookValid && dt > 0.0f)
        {
            dYaw = WrapAngle(yaw - f.lastYaw);
            dPitch = WrapAngle(pitch - f.lastPitch);
        }
        f.lastYaw = yaw; f.lastPitch = pitch; f.lastLookValid = lookOk;
        // Ignore the one-frame jumps of teleports / loads (and anything that is not a number).
        if (!(fabsf(dYaw) < DEG2RAD(60.0f)) || !(fabsf(dPitch) < DEG2RAD(60.0f)) || dt <= 0.0f)
            f.yawRate = f.pitchRate = 0.0f;
        else
        {
            f.yawRate = dYaw / dt;
            f.pitchRate = dPitch / dt;
        }
        if (!(fabsf(f.dragX) < 1e3f) || !(fabsf(f.dragY) < 1e3f) || !(fabsf(f.dragVX) < 1e5f) || !(fabsf(f.dragVY) < 1e5f))
            f.dragX = f.dragY = f.dragVX = f.dragVY = 0.0f; // never let a bad frame poison the spring

        // The spring always runs (towards zero when disabled) so toggling the feature fades instead of snapping.
        {
            const float w = clamp_tpl(s.dragStiffness, 0.5f, 60.0f);
            const float c = 2.0f * clamp_tpl(s.dragDamping, 0.1f, 2.0f) * w;
            // Sub-step for stability at low frame rates.
            const int n = max(1, (int)ceilf(dt / 0.008f));
            const float h = dt / n;
            const float tx = s.dragEnabled ? f.yawRate : 0.0f;
            const float ty = s.dragEnabled ? f.pitchRate : 0.0f;
            for (int i = 0; i < n; i++)
            {
                const float a1 = w * w * (tx - f.dragX) - c * f.dragVX;
                f.dragVX += a1 * h; f.dragX += f.dragVX * h;
                const float a2 = w * w * (ty - f.dragY) - c * f.dragVY;
                f.dragVY += a2 * h; f.dragY += f.dragVY * h;
            }
            const float sign = s.dragLead ? 1.0f : -1.0f;
            // Turning left = positive yaw rate. Leading: the weapon moves/turns left with it.
            PoseOffset d;
            d.posX = clamp_tpl(-sign * f.dragX * s.dragPos, -s.dragMaxPos, s.dragMaxPos);
            d.posZ = clamp_tpl(sign * f.dragY * s.dragPos * s.dragPitchScale, -s.dragMaxPos, s.dragMaxPos);
            d.yaw = clamp_tpl(sign * f.dragX * s.dragRot, -s.dragMaxRot, s.dragMaxRot);
            d.pitch = clamp_tpl(sign * f.dragY * s.dragRot * s.dragPitchScale, -s.dragMaxRot, s.dragMaxRot);
            f.dragHipOut = d;
            f.dragAimOut = PoseOffset();
            f.dragAimOut.AddScaled(d, clamp_tpl(s.dragAimScale, 0.0f, 1.0f));
        }
    }
}

void ModMain::SanitizeSettings()
{
    // Persisted numbers (cvars, weapons.xml) are the one place a bad value could come back session after
    // session; anything that is not a number goes back to its default.
    ViewmodelSettings& s = m_settings;
    const ViewmodelSettings def;
    int fixed = 0;
    auto fixF = [&](float& v, float d) { if (!Finite(v)) { v = d; fixed++; } };
    fixF(s.crouchTime, def.crouchTime); fixF(s.reloadFadeTime, def.reloadFadeTime); fixF(s.aimSwitchDelay, def.aimSwitchDelay); fixF(s.aimBobAmount, def.aimBobAmount); fixF(s.aimBobTau, def.aimBobTau);
    fixF(s.aimAnimRecoil, def.aimAnimRecoil); fixF(s.aimAnimSway, def.aimAnimSway); fixF(s.aimSensScale, def.aimSensScale);
    fixF(s.aimTime, def.aimTime); fixF(s.aimFov, def.aimFov); fixF(s.aimCameraZoomFactor, def.aimCameraZoomFactor);
    fixF(s.nudgePosSpeed, def.nudgePosSpeed); fixF(s.nudgeRotSpeed, def.nudgeRotSpeed); fixF(s.reticleY, def.reticleY);
    fixF(s.convergeStrength, def.convergeStrength); fixF(s.convergeMaxAngle, def.convergeMaxAngle);
    fixF(s.convergeSmoothTime, def.convergeSmoothTime); fixF(s.convergeMaxDist, def.convergeMaxDist);
    fixF(s.aimWallBlockScale, def.aimWallBlockScale); fixF(s.wallPushStartDist, def.wallPushStartDist);
    fixF(s.wallPushFullDist, def.wallPushFullDist); fixF(s.wallPushSmoothTime, def.wallPushSmoothTime);
    fixF(s.sprintBlendIn, def.sprintBlendIn); fixF(s.sprintBlendOut, def.sprintBlendOut);
    fixF(s.sprintSwayPos, def.sprintSwayPos); fixF(s.sprintSwayRot, def.sprintSwayRot); fixF(s.sprintSwayFreq, def.sprintSwayFreq);
    fixF(s.aimSwayPos, def.aimSwayPos); fixF(s.aimSwayRot, def.aimSwayRot); fixF(s.aimSwayFreq, def.aimSwayFreq);
    fixF(s.aimSwayInitial, def.aimSwayInitial); fixF(s.aimSwaySettleTime, def.aimSwaySettleTime); fixF(s.aimSwayMoveMult, def.aimSwayMoveMult);
    fixF(s.aimSwaySprintPenalty, def.aimSwaySprintPenalty); fixF(s.aimSwaySprintRecover, def.aimSwaySprintRecover);
    fixF(s.steadyReduce, def.steadyReduce); fixF(s.steadyDuration, def.steadyDuration); fixF(s.steadyRecover, def.steadyRecover);
    fixF(s.dragPos, def.dragPos); fixF(s.dragRot, def.dragRot); fixF(s.dragStiffness, def.dragStiffness); fixF(s.dragDamping, def.dragDamping);
    fixF(s.dragMaxPos, def.dragMaxPos); fixF(s.dragMaxRot, def.dragMaxRot); fixF(s.dragAimScale, def.dragAimScale); fixF(s.dragPitchScale, def.dragPitchScale);
    fixF(s.worldFov, def.worldFov); fixF(s.sprintSensScale, def.sprintSensScale); fixF(s.fov, def.fov);
    if (s.base.Sanitize(def.base)) fixed++;
    if (s.crouch.Sanitize(def.crouch)) fixed++;
    if (s.sprint.Sanitize(def.sprint)) fixed++;
    for (auto& kv : m_weapons)
    {
        WeaponSettings& w = kv.second;
        const WeaponSettings* pB = WeaponSettings::BuiltIn(kv.first.c_str());
        WeaponSettings d;
        if (pB) d = *pB; else d.aim = WeaponSettings::DefaultAim();
        if (w.hip.Sanitize(d.hip)) fixed++;
        if (w.aim.Sanitize(d.aim)) fixed++;
        fixF(w.wallPush, d.wallPush); fixF(w.fireCoupling, d.fireCoupling); fixF(w.fireCouplingTime, d.fireCouplingTime);
        fixF(w.aimRecoilScale, d.aimRecoilScale); fixF(w.aimKickScale, d.aimKickScale); fixF(w.aimSpreadMult, d.aimSpreadMult); fixF(w.hipSpreadMult, d.hipSpreadMult);
    }
    if (fixed > 0)
    {
        CryLog("ViewmodelTweaks: {} persisted setting(s) were not numbers and were reset to defaults", fixed);
        m_nanRecoveries += fixed;
    }
}

void ModMain::SanitizeFeel()
{
    FeelState& f = m_feel;
    const bool ok = SanePose(f.sprintOut) && SanePose(f.swayOut) && SanePose(f.dragHipOut) && SanePose(f.dragAimOut)
        && Finite(f.sprintBlend) && Finite(f.sprintPhase) && Finite(f.swayPhase) && Finite(f.aimTime) && Finite(f.steadyLeft)
        && Finite(f.dragX) && Finite(f.dragY) && Finite(f.dragVX) && Finite(f.dragVY) && Finite(f.speed);
    if (!ok)
    {
        const float breath = Finite(f.steadyLeft) ? f.steadyLeft : m_settings.steadyDuration;
        f = FeelState();
        f.steadyLeft = breath;
        m_nanRecoveries++;
    }
}

QuatT ModMain::ComputeAimLocal() const
{
    // Weapon-local (post-multiplied) extras: rotate in place around the weapon's pivot so the sights move
    // off the crosshair, translate along the weapon's own axes.
    PoseOffset total;
    if (SanePose(m_feel.swayOut))
        total.AddScaled(m_feel.swayOut, 1.0f);      // both are zero when their feature is off
    if (SanePose(m_feel.dragAimOut))
        total.AddScaled(m_feel.dragAimOut, 1.0f);
    if (total.IsZero())
        return QuatT(IDENTITY);
    return total.AsQuatT();
}

void ModMain::UpdateCameraZoom(bool aiming)
{
    ArkPlayer* pPlayer = ArkPlayer::GetInstancePtr();
    if (!pPlayer)
    {
        m_cameraZoomHandle = 0; // the zoom manager is gone with the player
        return;
    }

    ArkPlayerZoomManager& zoom = pPlayer->m_zoomManager;
    const bool want = aiming && m_settings.aimCameraZoom != 0;
    SCVars* pCVars = *g_pGameCVars;
    const float baseHfov = (pCVars && pCVars->cl_hfov > 1.0f) ? pCVars->cl_hfov : 90.0f;
    const float factor = clamp_tpl(m_settings.aimCameraZoomFactor, 0.2f, 1.5f);
    const float hfov = baseHfov * factor;

    if (want && m_cameraZoomHandle == 0)
    {
        m_cameraZoomHandle = zoom.SetDesiredHFOV(hfov, m_settings.aimTime, false, EArkZoomPriority::high);
        m_cameraZoomHfov = hfov;
    }
    else if (want && fabsf(hfov - m_cameraZoomHfov) > 0.01f)
    {
        zoom.UpdateDesiredHFOV(m_cameraZoomHandle, hfov, 0.05f, false);
        m_cameraZoomHfov = hfov;
    }
    else if (!want && m_cameraZoomHandle != 0)
    {
        zoom.ClearDesiredHFOV(m_cameraZoomHandle, m_settings.aimTime, false);
        m_cameraZoomHandle = 0;
    }
}

//---------------------------------------------------------------------------------
// Built-in per-weapon defaults (tuned on the stock weapons; the user's weapons.xml overrides them)
//---------------------------------------------------------------------------------
namespace
{
    struct BuiltInWeapon
    {
        const char* cls;
        bool aimAllowed;
        float wallPush;
        float fireCoupling, fireCouplingTime, aimRecoilScale, aimKickScale;
        float aimSpread, hipSpread;
        PoseOffset hip;
        PoseOffset aim;
    };
    PoseOffset P(float x, float y, float z, float pitch = 0.0f, float yaw = 0.0f, float roll = 0.0f)
    {
        PoseOffset p;
        p.posX = x; p.posY = y; p.posZ = z; p.pitch = pitch; p.yaw = yaw; p.roll = roll;
        return p;
    }
    const BuiltInWeapon* BuiltInTable(size_t& count)
    {
        // Aim pose = weapon attachment relative to the camera (m / deg), x = 0 means centered on the crosshair.
        static const BuiltInWeapon table[] = {
            //  class                          aim    wall   fireC fireT recoil kick  aimSp hipSp  hip offset                                   aim pose
            { "ArkWeaponPistol",               true,  0.066f, 1.0f, 0.40f, 3.0f, 0.30f, 0.45f, 1.0f, P(0, 0, 0),                                  P(0.0f, 0.250f, -0.1327f, 0.04f) },
            { "ArkWeaponShotgun",              true,  0.134f, 1.0f, 0.75f, 1.0f, 0.15f, 0.45f, 1.0f, P(0, 0, -0.0323f, 3.23f),                    P(0.0f, 0.0575f, -0.0956f, 0.73f) },
            { "ArkWeaponGooGun",               true,  0.157f, 0.35f, 0.30f, 1.0f, 0.20f, 1.0f, 1.0f, P(0, 0, 0),                                  P(0.0f, 0.250f, -0.1707f) },
            { "ArkWeaponStunGun",              true,  0.060f, 0.35f, 0.30f, 1.0f, 0.25f, 1.0f, 1.0f, P(0, 0, 0),                                  P(0.0f, 0.250f, -0.1487f) },
            { "ArkWeaponToyGun",               true,  0.129f, 0.35f, 0.30f, 1.0f, 0.30f, 1.0f, 1.0f, P(0, -0.014f, -0.0403f, 4.84f, 5.44f),       P(0.0f, 0.0564f, -0.0954f) },
            { "ArkWeaponInstalaser",           true,  0.216f, 0.35f, 0.30f, 1.0f, 0.35f, 1.0f, 1.0f, P(0, 0, -0.0362f, 5.13f, 2.58f),             P(0.165f, -0.2295f, 0.0511f, 0.0f, 0.01f) },
            { "ArkWeaponWrench",               false, 0.026f, 0.35f, 0.30f, 1.0f, 1.0f, 1.0f, 1.0f, P(-0.0177f, 0.0242f, 0.0398f, 4.83f, 0.0f, -5.99f), P(0, 0.25f, -0.06f) },
            { "ArkWeaponEMPGrenade",           false, 0.060f, 0.35f, 0.30f, 1.0f, 1.0f, 1.0f, 1.0f, P(0, 0, 0),                                  P(0, 0.25f, -0.06f) },
            { "ArkWeaponLureGrenade",          false, 0.060f, 0.35f, 0.30f, 1.0f, 1.0f, 1.0f, 1.0f, P(0, 0, 0),                                  P(0, 0.25f, -0.06f) },
            { "ArkWeaponRecyclerGrenade",      false, 0.000f, 0.35f, 0.30f, 1.0f, 1.0f, 1.0f, 1.0f, P(0, 0, 0),                                  P(0, 0.25f, -0.06f) },
            { "ArkWeaponNullwaveTransmitter",  false, 0.060f, 0.35f, 0.30f, 1.0f, 1.0f, 1.0f, 1.0f, P(0, 0, 0),                                  P(0, 0.25f, -0.06f) },
            { "ArkWeaponExplosiveGrenade",     false, 0.060f, 0.35f, 0.30f, 1.0f, 1.0f, 1.0f, 1.0f, P(0, 0, 0),                                  P(0, 0.25f, -0.06f) },
        };
        count = sizeof(table) / sizeof(table[0]);
        return table;
    }
}

const WeaponSettings* WeaponSettings::BuiltIn(const char* weaponClass)
{
    static std::map<std::string, WeaponSettings> s_cache;
    if (!weaponClass || !*weaponClass)
        return nullptr;
    auto it = s_cache.find(weaponClass);
    if (it != s_cache.end())
        return &it->second;
    size_t n = 0;
    const BuiltInWeapon* t = BuiltInTable(n);
    for (size_t i = 0; i < n; i++)
    {
        if (strcmp(t[i].cls, weaponClass) != 0)
            continue;
        WeaponSettings w;
        w.aimAllowed = t[i].aimAllowed;
        w.wallPush = t[i].wallPush;
        w.fireCoupling = t[i].fireCoupling;
        w.fireCouplingTime = t[i].fireCouplingTime;
        w.aimRecoilScale = t[i].aimRecoilScale;
        w.aimKickScale = t[i].aimKickScale;
        w.aimSpreadMult = t[i].aimSpread;
        w.hipSpreadMult = t[i].hipSpread;
        w.hip = t[i].hip;
        w.aim = t[i].aim;
        w.valid = false; // built-in: not written to the XML until the user changes it
        return &s_cache.emplace(weaponClass, w).first->second;
    }
    return nullptr;
}

const WeaponSettings* ModMain::FindWeapon(const char* weaponClass) const
{
    if (!weaponClass || !*weaponClass)
        return nullptr;
    auto it = m_weapons.find(weaponClass);
    if (it != m_weapons.end())
        return &it->second;
    return WeaponSettings::BuiltIn(weaponClass);
}

const WeaponSettings* ModMain::FindCurrentWeapon() const
{
    return FindWeapon(m_currentWeaponClass.c_str());
}

WeaponSettings& ModMain::GetCurrentWeapon()
{
    const std::string& key = m_currentWeaponClass.empty() ? std::string("_none") : m_currentWeaponClass;
    auto it = m_weapons.find(key);
    if (it == m_weapons.end())
    {
        WeaponSettings w;
        if (const WeaponSettings* pBuiltIn = WeaponSettings::BuiltIn(key.c_str()))
            w = *pBuiltIn;
        else
            w.aim = WeaponSettings::DefaultAim();
        w.valid = false;
        it = m_weapons.emplace(key, w).first;
    }
    return it->second;
}

PoseOffset& ModMain::GetActivePose(const char** outName)
{
    if (m_isAiming && m_settings.aimEnabled)
    {
        if (outName) *outName = "aim pose (this weapon)";
        return GetCurrentWeapon().aim;
    }
    if (m_settings.nudgeTarget == 1 && !m_currentWeaponClass.empty())
    {
        if (outName) *outName = "hip offset (this weapon)";
        return GetCurrentWeapon().hip;
    }
    if (m_isCrouching && m_settings.crouchEnabled)
    {
        if (outName) *outName = "crouch offset (global)";
        return m_settings.crouch;
    }
    if (outName) *outName = "standing offset (global)";
    return m_settings.base;
}

void ModMain::UpdateNudge(float dt)
{
    if (!m_settings.nudgeKeys || m_mouseCaptured || IsHardwareCursorVisible())
        return;

    bool any = false;
    for (float v : m_nudgeAxis)
        any |= (v != 0.0f);
    if (!any)
        return;

    PoseOffset& pose = GetActivePose(nullptr);
    const float mul = m_nudgeSlow ? 0.2f : 1.0f;
    for (int i = 0; i < 3; i++)
        pose.Axis(i) += m_nudgeAxis[i] * m_settings.nudgePosSpeed * mul * dt;
    for (int i = 3; i < 6; i++)
        pose.Axis(i) += m_nudgeAxis[i] * m_settings.nudgeRotSpeed * mul * dt;

    const bool editingWeapon = m_isAiming || (!m_isAiming && m_settings.nudgeTarget == 1);
    if (editingWeapon && !m_currentWeaponClass.empty())
    {
        GetCurrentWeapon().valid = true;
        m_weaponsDirty = true;
    }
}

//---------------------------------------------------------------------------------
// Input
//---------------------------------------------------------------------------------
bool ModMain::HandleNudgeKey(EKeyId key, bool pressed)
{
    const float v = pressed ? 1.0f : 0.0f;
    auto axis = [&](int posAxis, int rotAxis, float dir) {
        // Clear both candidates so a mode switch mid-press cannot leave an axis stuck.
        m_nudgeAxis[posAxis] = 0.0f;
        m_nudgeAxis[rotAxis] = 0.0f;
        m_nudgeAxis[m_nudgeRotMode ? rotAxis : posAxis] = dir * v;
        return true;
    };

    if (m_settings.nudgeLayout == 0)
    {
        switch (key)
        {
        case eKI_NP_0: m_nudgeRotMode = pressed; return true;
        case eKI_NP_Period: m_nudgeSlow = pressed; return true;
        case eKI_NP_4: return axis(0, 4, -1.0f); // left  / yaw -
        case eKI_NP_6: return axis(0, 4, +1.0f); // right / yaw +
        case eKI_NP_8: return axis(2, 3, +1.0f); // up    / pitch +
        case eKI_NP_2: return axis(2, 3, -1.0f); // down  / pitch -
        case eKI_NP_7: return axis(1, 5, +1.0f); // fwd   / roll +
        case eKI_NP_1: return axis(1, 5, -1.0f); // back  / roll -
        default: return false;
        }
    }
    else
    {
        switch (key)
        {
        case eKI_H: m_nudgeRotMode = pressed; return true;
        case eKI_N: m_nudgeSlow = pressed; return true;
        case eKI_J: return axis(0, 4, -1.0f); // left  / yaw -
        case eKI_L: return axis(0, 4, +1.0f); // right / yaw +
        case eKI_I: return axis(2, 3, +1.0f); // up    / pitch +
        case eKI_K: return axis(2, 3, -1.0f); // down  / pitch -
        case eKI_U: return axis(1, 5, +1.0f); // fwd   / roll +
        case eKI_O: return axis(1, 5, -1.0f); // back  / roll -
        default: return false;
        }
    }
}

bool ModMain::OnInputEvent(const SInputEvent& event)
{
    // Only buttons/keys, not axes.
    if (event.deviceType != eIDT_Keyboard && event.deviceType != eIDT_Mouse && event.deviceType != eIDT_Gamepad)
        return false;
    if (event.state != eIS_Pressed && event.state != eIS_Released)
        return false;

    const bool pressed = (event.state == eIS_Pressed);

    if (m_waitingForAimKey)
    {
        if (pressed)
        {
            if (event.keyId != eKI_Escape)
                m_settings.aimKey = (int)event.keyId;
            m_waitingForAimKey = false;
            m_aimKeyHeld = false;
            return true;
        }
        return false;
    }

    if (m_waitingForSteadyKey)
    {
        if (pressed)
        {
            if (event.keyId != eKI_Escape)
                m_settings.steadyKey = (int)event.keyId;
            m_waitingForSteadyKey = false;
            return true;
        }
        return false;
    }
    if (m_settings.steadyKey != 0 && (int)event.keyId == m_settings.steadyKey)
    {
        m_feel.steadyHeld = pressed;
        if (Active() && m_settings.steadyEnabled && m_settings.steadyConsumeKey && (int)event.keyId != m_settings.aimKey)
            return true;
    }

    // Nudge keys (only while enabled). They are swallowed so the game does not react to them.
    if (Active() && m_settings.nudgeKeys && event.deviceType == eIDT_Keyboard && !m_mouseCaptured && !IsHardwareCursorVisible())
    {
        if (HandleNudgeKey(event.keyId, pressed))
            return true;
    }

    if (!Active() || !m_settings.aimEnabled || (int)event.keyId != m_settings.aimKey)
        return false;

    if (m_settings.aimToggle)
    {
        if (pressed && !m_mouseCaptured && !IsHardwareCursorVisible())
            m_aimKeyHeld = !m_aimKeyHeld;
    }
    else
    {
        m_aimKeyHeld = pressed;
    }

    return m_settings.aimConsumeKey != 0;
}

const char* ModMain::GetKeyName(int keyId) const
{
    if (gCL && gCL->cl)
    {
        const auto& names = gCL->cl->GetKeyNames();
        auto it = names.left.find((EKeyId)keyId);
        if (it != names.left.end())
            return it->second.c_str();
    }
    static char buf[32];
    snprintf(buf, sizeof(buf), "key 0x%X", keyId);
    return buf;
}

//---------------------------------------------------------------------------------
// Weapon FOV
//---------------------------------------------------------------------------------
void ModMain::EnforceWeaponFov()
{
    if (!gEnv || !gEnv->pRenderer)
        return;

    ArkPlayer* pPlayer = ArkPlayer::GetInstancePtr();
    if (!pPlayer)
        return;

    const ArkPlayerZoomManager& zoom = pPlayer->m_zoomManager;

    // When the game "locks" the near FOV it wants the weapon rendered with the world FOV
    // (cl_fov). Respect that.
    if (zoom.m_nearFOVLockedCount != 0)
        return;

    if (!m_settings.fovEnabled || m_settings.bypass)
    {
        // Put the stock value back once when the override gets disabled.
        if (m_lastAppliedFov >= 0.0f)
        {
            float stock = PreyInternals::STOCK_WEAPON_FOV;
            gEnv->pRenderer->EF_Query(EFQ_SetDrawNearFov, stock);
            m_lastAppliedFov = -1.0f;
        }
        return;
    }

    // The game scales the near FOV proportionally to the current horizontal FOV while zooming
    // (ArkPlayerZoomManager::Update): 55 degrees at cl_hfov, then near *= new/old on every HFOV
    // change. Mirror that relative to cl_hfov. (Do NOT derive the reference from the zoom stack:
    // it is normally empty, and our own aim-zoom entry would become the "base".)
    SCVars* pCVars = *g_pGameCVars;
    const float baseHfov = (pCVars && pCVars->cl_hfov > 1.0f) ? pCVars->cl_hfov : zoom.m_currentHFOV;

    float userFov = m_settings.fov;
    if (m_settings.aimEnabled && m_settings.aimFovEnabled)
        userFov = LERP(m_settings.fov, m_settings.aimFov, SmoothStep01(m_aimBlend));

    const float scale = (baseHfov > 1.0f) ? (zoom.m_currentHFOV / baseHfov) : 1.0f;
    float desired = clamp_tpl(userFov, 5.0f, 170.0f) * scale;

    float current = 0.0f;
    gEnv->pRenderer->EF_Query(EFQ_GetDrawNearFov, current);

    if (fabsf(current - desired) > 0.001f)
    {
        gEnv->pRenderer->EF_Query(EFQ_SetDrawNearFov, desired);
        m_lastAppliedFov = desired;
    }
}

void ModMain::UpdateCameraInputOverrides()
{
    ArkPlayer* pPlayer = ArkPlayer::GetInstancePtr();
    if (!pPlayer || !gEnv || !gEnv->pConsole)
        return;
    const ViewmodelSettings& s = m_settings;

    // World FOV: the game's own options slider writes cl_hfov; we just keep it where the user wants.
    if (s.worldFovEnabled)
    {
        if (ICVar* pVar = gEnv->pConsole->GetCVar("cl_hfov"))
        {
            const float want = clamp_tpl(s.worldFov, 50.0f, 140.0f);
            if (fabsf(pVar->GetFVal() - want) > 0.01f)
                pVar->Set(want);
        }
    }

    // Sprint look-sensitivity scale (ArkPlayerInput::GetRotation multiplies the look delta by
    // m_sprintCameraRotationRateScale while the movement FSM says "sprinting"). Loaded from the
    // player's Lua params, so re-apply every frame.
    float& sprintScale = pPlayer->m_input.m_sprintCameraRotationRateScale;
    if (m_gameSprintSensScale < 0.0f)
        m_gameSprintSensScale = sprintScale;
    if (s.sprintSensEnabled)
    {
        sprintScale = clamp_tpl(s.sprintSensScale, 0.1f, 3.0f);
        m_sprintSensApplied = true;
    }
    else if (m_sprintSensApplied)
    {
        sprintScale = m_gameSprintSensScale; // switched off: put the game's value back
        m_sprintSensApplied = false;
    }
}

void ModMain::UpdateReticle()
{
    // The reticle cvars' change handling writes into the player's reticle state; with no player
    // (main menu, loading) that is a null dereference inside the game. Only touch them in a level.
    if (!gEnv || !gEnv->pConsole || !ArkPlayer::GetInstancePtr())
    {
        m_reticlePlayerSeen = false;
        m_reticleStyleBeforeHide = -1;
        return;
    }
    const ViewmodelSettings& s = m_settings;
    ArkPlayer* pPlayer = ArkPlayer::GetInstancePtr();
    // A level was (re)loaded: the game re-applies its profile, so our style has to be applied once more.
    const bool reapply = !m_reticlePlayerSeen;
    m_reticlePlayerSeen = true;

    // The reticle doubles as the mouse cursor on in-world screens (the HUD gets "reticlePosition" events
    // from the examination mode and draws the very same reticle sprite there). hud_reticleSetting 0 hides
    // that sprite as well, so while a screen is being used we put the reticle back and hide it again after.
    static_assert(offsetof(ArkExaminationMode, m_reticlePos) == 0x60, "ArkExaminationMode layout");
    static_assert(offsetof(ArkExaminationMode, m_examinationState) == 0x98, "ArkExaminationMode layout");
    const ArkExaminationMode& exam = pPlayer->m_examinationMode;
    const bool usingScreen = exam.m_examinationState == ArkExaminationMode::EArkExaminationState::active
        && exam.m_examinationType == ArkExaminationMode::EArkExaminationType::worldUI;

    // --- Position (g_reticleYPercentage) ---
    if (s.reticleMode != 0)
    {
        if (ICVar* pVar = gEnv->pConsole->GetCVar("g_reticleYPercentage"))
        {
            const float want = (s.reticleMode == 1) ? 0.5f : clamp_tpl(s.reticleY, 0.0f, 1.0f);
            if (fabsf(pVar->GetFVal() - want) > 0.0001f)
                pVar->Set(want);
        }
    }

    // --- Style / visibility (hud_reticleSetting: 0 none, 1 default, 2 dot) ---
    ICVar* pStyle = gEnv->pConsole->GetCVar("hud_reticleSetting");
    if (!pStyle)
        return;
    // Right after a (re)load the cvar still holds the game's own option value (the profile was just
    // applied): remember it, so "Hidden" and "hide while aiming" know what to bring back for screens.
    if (reapply && pStyle->GetIVal() > 0 && pStyle->GetIVal() != m_reticleStyleApplied)
        m_reticleGameStyle = pStyle->GetIVal();
    const bool hide = s.aimHideReticle && s.aimEnabled && SmoothStep01(m_aimBlend) > 0.5f;
    int want = -1; // -1 = leave alone
    switch (s.reticleStyle)
    {
    case 1: want = 1; break;
    case 2: want = 2; break;
    case 3: want = 0; break;
    default: break;
    }
    if (hide)
    {
        if (m_reticleStyleBeforeHide < 0)
            m_reticleStyleBeforeHide = (want >= 0) ? want : pStyle->GetIVal(); // remember what to go back to
        want = 0;
    }
    else if (m_reticleStyleBeforeHide >= 0)
    {
        if (want < 0)
            want = m_reticleStyleBeforeHide; // style is "leave alone": put back what the game had
        m_reticleStyleBeforeHide = -1;
    }
    if (want == 0 && usingScreen)
    {
        // Cursor needed: show the reticle the game itself would show (or the default one if the game's
        // own option is "off" too - otherwise there would be no cursor at all).
        const int base = (s.reticleStyle == 1 || s.reticleStyle == 2) ? s.reticleStyle : m_reticleGameStyle;
        want = (base > 0) ? base : 1;
    }
    // Apply only when OUR desired value changes (or a level was loaded) - never every frame, so the
    // game's own handling of the option is never fought.
    if (want >= 0 && (want != m_reticleStyleApplied || reapply))
    {
        if (pStyle->GetIVal() != want)
            pStyle->Set(want);
        m_reticleStyleApplied = want;
    }
    else if (want < 0)
        m_reticleStyleApplied = -1;
}

//---------------------------------------------------------------------------------
// Pop tracer: what the filters output vs. what the weapon actually does, per update
//---------------------------------------------------------------------------------
void ModMain::RecordTrace()
{
    constexpr size_t N = 1500; // ~30 s at 50 updates/s
    if (m_trace.size() != N)
    {
        m_trace.assign(N, TraceSample());
        m_traceHead = 0;
        m_traceCount = 0;
    }
    TraceSample smp;
    smp.t = (gEnv && gEnv->pTimer) ? (float)(gEnv->pTimer->GetAsyncTime().GetValue() % 100000000000LL) * 1e-5f : 0.0f;
    smp.dt = m_dtUsed;
    smp.cYaw = m_convergeYaw; smp.cYawT = m_convergeTargetYaw;
    smp.cPitch = m_convergePitch; smp.cPitchT = m_convergeTargetPitch;
    smp.wall = m_wallPush; smp.wallT = m_wallPushTarget;
    smp.dist = m_convergeDist;
    smp.attachValid = m_render.attachValid;
    if (m_render.attachValid && SaneQuatT(m_render.weaponRelCam, 5.0f))
    {
        const Ang3 a = SafeAng3(m_render.weaponRelCam.q);
        smp.mPitch = RAD2DEG(a.x); smp.mRoll = RAD2DEG(a.y); smp.mYaw = RAD2DEG(a.z);
        smp.mX = m_render.weaponRelCam.t.x; smp.mY = m_render.weaponRelCam.t.y; smp.mZ = m_render.weaponRelCam.t.z;
    }
    smp.lookYaw = RAD2DEG(SafeAng3(m_gameOffsets[0].q).z);
    smp.aimBlend = m_aimBlend;
    smp.sprintBlend = m_feel.sprintBlend;
    smp.dragYaw = m_feel.dragHipOut.yaw;
    smp.pwa = m_diag.pwaUpdatesLastFrame; smp.ctx = m_diag.ctxUpdatesLastFrame; smp.cam = m_render.callsLastFrame;

    // Pop detector: the weapon turned by a lot in one update while the smoothed value barely moved.
    const TraceSample& prev = m_trace[(m_traceHead + N - 1) % N];
    if (m_traceAuto && m_traceCount > 10 && prev.attachValid && smp.attachValid)
    {
        const float dMeasured = fabsf(smp.mYaw - prev.mYaw) + fabsf(smp.mPitch - prev.mPitch);
        const float dFilter = fabsf(smp.cYaw - prev.cYaw) + fabsf(smp.cPitch - prev.cPitch);
        const float dLook = fabsf(smp.lookYaw - prev.lookYaw);
        if (dMeasured > 3.0f && dFilter < 0.25f * dMeasured && dLook < 0.25f * dMeasured && smp.t - m_traceLastSave > 20.0f)
            m_traceStatus = "pop!"; // saved below, after this sample is in the buffer
    }
    m_trace[m_traceHead] = smp;
    m_traceHead = (m_traceHead + 1) % N;
    if (m_traceCount < N) m_traceCount++;
    if (m_traceStatus == "pop!")
        SaveTrace("auto (weapon jumped while the filter did not)");
}

void ModMain::SaveTrace(const char* reason)
{
    const fs::path path = GetWeaponsPath().parent_path() / "Vee.ViewmodelTweaks.trace.csv";
    FILE* f = fopen(path.u8string().c_str(), "w");
    if (!f)
    {
        m_traceStatus = "could not write " + path.u8string();
        return;
    }
    fprintf(f, "# %s | smoothing converge=%.3f wall=%.3f | %d samples, newest last\n", reason, m_settings.convergeSmoothTime, m_settings.wallPushSmoothTime, (int)m_traceCount);
    fprintf(f, "t,dt_ms,conv_yaw,conv_yaw_target,conv_pitch,conv_pitch_target,wall_cm,wall_target_cm,ray_dist,meas_yaw,meas_pitch,meas_roll,meas_x,meas_y,meas_z,look_yaw,aim_blend,sprint_blend,drag_yaw,pwa,ctx,cam,attach\n");
    const size_t N = m_trace.size();
    for (size_t i = 0; i < m_traceCount; i++)
    {
        const TraceSample& x = m_trace[(m_traceHead + N - m_traceCount + i) % N];
        fprintf(f, "%.4f,%.2f,%.3f,%.3f,%.3f,%.3f,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%d\n",
            x.t, x.dt * 1000.0f, x.cYaw, x.cYawT, x.cPitch, x.cPitchT, x.wall * 100.0f, x.wallT * 100.0f, x.dist,
            x.mYaw, x.mPitch, x.mRoll, x.mX, x.mY, x.mZ, x.lookYaw, x.aimBlend, x.sprintBlend, x.dragYaw, x.pwa, x.ctx, x.cam, x.attachValid ? 1 : 0);
    }
    fclose(f);
    m_traceSaves++;
    m_traceLastSave = m_traceCount ? m_trace[(m_traceHead + N - 1) % N].t : 0.0f;
    char buf[256];
    snprintf(buf, sizeof(buf), "trace #%d saved (%s)", m_traceSaves, reason);
    m_traceStatus = buf;
    CryLog("ViewmodelTweaks: {} -> {}", buf, path.u8string());
}

//---------------------------------------------------------------------------------
// Per-weapon persistence
//---------------------------------------------------------------------------------
fs::path ModMain::GetWeaponsPath() const
{
    fs::path dir;
    if (gCL && gCL->conf)
        dir = gCL->conf->getConfigPath("Vee.ViewmodelTweaks").parent_path();
    if (dir.empty() && gCL && gCL->cl)
        dir = gCL->cl->GetModsPath() / "config";
    return dir / "Vee.ViewmodelTweaks.weapons.xml";
}

static void ReadPose(pugi::xml_node n, const char* prefix, PoseOffset& p, const PoseOffset& def)
{
    auto attr = [&](const char* suffix, float d) {
        std::string name = std::string(prefix) + suffix;
        return n.attribute(name.c_str()).as_float(d);
    };
    p.posX = attr("x", def.posX); p.posY = attr("y", def.posY); p.posZ = attr("z", def.posZ);
    p.pitch = attr("pitch", def.pitch); p.yaw = attr("yaw", def.yaw); p.roll = attr("roll", def.roll);
}

static void WritePose(pugi::xml_node n, const char* prefix, const PoseOffset& p)
{
    auto attr = [&](const char* suffix, float v) {
        std::string name = std::string(prefix) + suffix;
        n.append_attribute(name.c_str()) = v;
    };
    attr("x", p.posX); attr("y", p.posY); attr("z", p.posZ);
    attr("pitch", p.pitch); attr("yaw", p.yaw); attr("roll", p.roll);
}

void ModMain::LoadWeapons()
{
    m_weapons.clear();
    pugi::xml_document doc;
    const fs::path path = GetWeaponsPath();
    if (!doc.load_file(path.c_str()))
        return;
    for (pugi::xml_node n : doc.child("Weapons").children("Weapon"))
    {
        const char* cls = n.attribute("class").as_string("");
        if (!*cls)
            continue;
        WeaponSettings w;
        w.aimAllowed = n.attribute("aim_allowed").as_bool(true);
        w.wallPush = n.attribute("wall_push").as_float(WeaponSettings().wallPush);
        w.fireCoupling = n.attribute("fire_coupling").as_float(WeaponSettings().fireCoupling);
        w.fireCouplingTime = n.attribute("fire_coupling_time").as_float(WeaponSettings().fireCouplingTime);
        w.aimRecoilScale = n.attribute("aim_recoil_scale").as_float(WeaponSettings().aimRecoilScale);
        w.aimKickScale = n.attribute("aim_kick_scale").as_float(WeaponSettings().aimKickScale);
        w.aimSpreadMult = n.attribute("aim_spread_mult").as_float(WeaponSettings().aimSpreadMult);
        w.hipSpreadMult = n.attribute("hip_spread_mult").as_float(WeaponSettings().hipSpreadMult);
        ReadPose(n, "hip_", w.hip, PoseOffset());
        ReadPose(n, "aim_", w.aim, WeaponSettings::DefaultAim());
        // Backwards compatibility with 1.3.0 files (aim pose stored as x/y/z/pitch/yaw/roll)
        if (n.attribute("x") && !n.attribute("aim_x"))
            ReadPose(n, "", w.aim, WeaponSettings::DefaultAim());
        w.valid = true;
        m_weapons[cls] = w;
    }
    CryLog("ViewmodelTweaks: loaded {} weapon entries from {}", m_weapons.size(), path.u8string());
    SanitizeSettings();
}

void ModMain::SaveWeapons()
{
    pugi::xml_document doc;
    pugi::xml_node root = doc.append_child("Weapons");
    root.append_attribute("comment") = "Per-weapon settings. hip_*: additive offset while not aiming. aim_*: weapon relative to camera while aiming. wall_push: pull-back (m) against walls. Meters (x right, y forward, z up), degrees.";
    for (auto& kv : m_weapons)
    {
        if (!kv.second.valid)
            continue;
        pugi::xml_node n = root.append_child("Weapon");
        n.append_attribute("class") = kv.first.c_str();
        n.append_attribute("aim_allowed") = kv.second.aimAllowed;
        n.append_attribute("wall_push") = kv.second.wallPush;
        n.append_attribute("fire_coupling") = kv.second.fireCoupling;
        n.append_attribute("fire_coupling_time") = kv.second.fireCouplingTime;
        n.append_attribute("aim_recoil_scale") = kv.second.aimRecoilScale;
        n.append_attribute("aim_kick_scale") = kv.second.aimKickScale;
        n.append_attribute("aim_spread_mult") = kv.second.aimSpreadMult;
        n.append_attribute("hip_spread_mult") = kv.second.hipSpreadMult;
        WritePose(n, "hip_", kv.second.hip);
        WritePose(n, "aim_", kv.second.aim);
    }
    const fs::path path = GetWeaponsPath();
    if (!doc.save_file(path.c_str()))
        CryError("ViewmodelTweaks: failed to save {}", path.u8string());
    m_weaponsDirty = false;
}

//---------------------------------------------------------------------------------
// Mod Initialization
//---------------------------------------------------------------------------------
void ModMain::FillModInfo(ModDllInfoEx& info)
{
    info.modName = "Vee.ViewmodelTweaks";
    info.logTag = "ViewmodelTweaks";
    info.supportsHotReload = true;
}

void ModMain::InitHooks()
{
    s_hookPwaUpdate.SetHookFunc(&CProceduralWeaponAnimation_Update_Hook);
    s_hookPwaContextUpdate.SetHookFunc(&CProceduralWeaponAnimationContext_Update_Hook);
    s_hookLookCompute.SetHookFunc(&LookCompute_Hook);
    s_hookStrafeCompute.SetHookFunc(&StrafeCompute_Hook);
    s_hookRecoilCompute.SetHookFunc(&RecoilCompute_Hook);
    s_hookBumpCompute.SetHookFunc(&BumpCompute_Hook);
    s_hookHfovMultiplier.SetHookFunc(&ArkPlayerZoomManager_GetHFOVDependentMultiplier_Hook);
    s_hookUpdateView.SetHookFunc(&ArkPlayerCamera_UpdateView_Hook);
    s_hookDispMin.SetHookFunc(&CArkWeaponShotgun_GetDispersionMinimum_Hook);
    s_hookDispMax.SetHookFunc(&CArkWeaponShotgun_GetDispersionMaximum_Hook);
    s_hookGetStatFloat.SetHookFunc(&CArkWeapon_GetStatFloat_Hook);
    s_hookFireWeapon.SetHookFunc(&CArkWeapon_FireWeapon_Hook);
}

static void RegisterPoseCVars(PoseOffset& p, const char* prefix, const char* what)
{
    // Names: vm_<prefix>pos_x ... e.g. vm_pos_x, vm_crouch_pos_x, vm_aim_pos_x.
    // The console keeps the name/help pointers, so they must outlive this call.
    static std::deque<std::string> s_strings;
    auto name = [&](const char* suffix) -> const char* {
        s_strings.push_back(std::string("vm_") + prefix + suffix);
        return s_strings.back().c_str();
    };
    auto help = [&](const char* text) -> const char* {
        s_strings.push_back(std::string("Viewmodel Tweaks (") + what + "): " + text);
        return s_strings.back().c_str();
    };
    REGISTER_CVAR2(name("pos_x"), &p.posX, p.posX, VF_DUMPTOCHAIR, help("right(+)/left(-) offset in meters"));
    REGISTER_CVAR2(name("pos_y"), &p.posY, p.posY, VF_DUMPTOCHAIR, help("forward(+)/back(-) offset in meters"));
    REGISTER_CVAR2(name("pos_z"), &p.posZ, p.posZ, VF_DUMPTOCHAIR, help("up(+)/down(-) offset in meters"));
    REGISTER_CVAR2(name("rot_pitch"), &p.pitch, p.pitch, VF_DUMPTOCHAIR, help("pitch offset in degrees (tilt up/down)"));
    REGISTER_CVAR2(name("rot_yaw"), &p.yaw, p.yaw, VF_DUMPTOCHAIR, help("yaw offset in degrees (turn left/right)"));
    REGISTER_CVAR2(name("rot_roll"), &p.roll, p.roll, VF_DUMPTOCHAIR, help("roll offset in degrees"));
}

void ModMain::RegisterCVars()
{
    ViewmodelSettings& s = m_settings;
    REGISTER_CVAR2("vm_enabled", &s.enabled, s.enabled, VF_DUMPTOCHAIR, "Viewmodel Tweaks: enable position/rotation offsets (0/1)");
    REGISTER_CVAR2("vm_bypass", &s.bypass, s.bypass, VF_DUMPTOCHAIR, "Viewmodel Tweaks: vanilla viewmodel - switch off every viewmodel feature except reticle, world FOV and sprint sensitivity (0/1)");
    RegisterPoseCVars(s.base, "", "global standing");

    REGISTER_CVAR2("vm_crouch_enabled", &s.crouchEnabled, s.crouchEnabled, VF_DUMPTOCHAIR, "Viewmodel Tweaks: blend in the crouch pose while sneaking (0/1)");
    RegisterPoseCVars(s.crouch, "crouch_", "global crouch, added to standing");
    REGISTER_CVAR2("vm_crouch_time", &s.crouchTime, s.crouchTime, VF_DUMPTOCHAIR, "Viewmodel Tweaks: stand<->crouch pose transition time in seconds");

    REGISTER_CVAR2("vm_aim_enabled", &s.aimEnabled, s.aimEnabled, VF_DUMPTOCHAIR, "Viewmodel Tweaks: enable the aim key / aim pose (0/1)");
    REGISTER_CVAR2("vm_aim_key", &s.aimKey, s.aimKey, VF_DUMPTOCHAIR, "Viewmodel Tweaks: EKeyId of the aim key (257 = right mouse button)");
    REGISTER_CVAR2("vm_aim_toggle", &s.aimToggle, s.aimToggle, VF_DUMPTOCHAIR, "Viewmodel Tweaks: 0 = hold to aim, 1 = toggle");
    REGISTER_CVAR2("vm_aim_consume_key", &s.aimConsumeKey, s.aimConsumeKey, VF_DUMPTOCHAIR, "Viewmodel Tweaks: swallow the aim key so the game's own binding on it is ignored (0/1)");
    REGISTER_CVAR2("vm_aim_render_lock", &s.aimRenderLock, s.aimRenderLock, VF_DUMPTOCHAIR, "Viewmodel Tweaks: place the weapon exactly at render time, after the camera is final (0/1)");
    REGISTER_CVAR2("vm_aim_hands_follow", &s.aimHandsFollow, s.aimHandsFollow, VF_DUMPTOCHAIR, "Viewmodel Tweaks: move the hand joints with the render-time placement (0/1)");
    REGISTER_CVAR2("vm_aim_left_hand_follow", &s.aimLeftHandFollow, s.aimLeftHandFollow, VF_DUMPTOCHAIR, "Viewmodel Tweaks: also move the support hand when it is on the weapon (0/1)");
    REGISTER_CVAR2("vm_aim_anim_recoil", &s.aimAnimRecoil, s.aimAnimRecoil, VF_DUMPTOCHAIR, "Viewmodel Tweaks: multiplier on the game's recoil/bump weapon motion while locked (0..3)");
    REGISTER_CVAR2("vm_aim_bob_tau", &s.aimBobTau, s.aimBobTau, VF_DUMPTOCHAIR, "Viewmodel Tweaks: head-bob separation time constant in seconds");
    REGISTER_CVAR2("vm_aim_bob", &s.aimBobAmount, s.aimBobAmount, VF_DUMPTOCHAIR, "Viewmodel Tweaks: head-bob coupling while locked (0 = still sights, 1 = weapon lags the head fully, negative = leads)");
    REGISTER_CVAR2("vm_aim_anim_sway", &s.aimAnimSway, s.aimAnimSway, VF_DUMPTOCHAIR, "Viewmodel Tweaks: how much of the game's look/strafe sway to keep while locked (0..1)");
    REGISTER_CVAR2("vm_aim_sens_mode", &s.aimSensMode, s.aimSensMode, VF_DUMPTOCHAIR, "Viewmodel Tweaks: ADS look sensitivity. 0 = game default (weapon's zoomed multiplier), 1 = match the camera zoom factor, 2 = custom (vm_aim_sens_scale)");
    REGISTER_CVAR2("vm_aim_sens_scale", &s.aimSensScale, s.aimSensScale, VF_DUMPTOCHAIR, "Viewmodel Tweaks: custom ADS look sensitivity multiplier (mode 2)");
    REGISTER_CVAR2("vm_aim_time", &s.aimTime, s.aimTime, VF_DUMPTOCHAIR, "Viewmodel Tweaks: hip<->aim transition time in seconds");
    REGISTER_CVAR2("vm_aim_ignores_crouch", &s.aimIgnoresCrouch, s.aimIgnoresCrouch, VF_DUMPTOCHAIR, "Viewmodel Tweaks: fade the crouch pose out while aiming (0/1)");
    REGISTER_CVAR2("vm_aim_fov_enabled", &s.aimFovEnabled, s.aimFovEnabled, VF_DUMPTOCHAIR, "Viewmodel Tweaks: use a separate weapon FOV while aiming (0/1)");
    REGISTER_CVAR2("vm_aim_fov", &s.aimFov, s.aimFov, VF_DUMPTOCHAIR, "Viewmodel Tweaks: weapon FOV while aiming, in degrees");
    REGISTER_CVAR2("vm_aim_camera_zoom", &s.aimCameraZoom, s.aimCameraZoom, VF_DUMPTOCHAIR, "Viewmodel Tweaks: zoom the camera while aiming (0/1)");
    REGISTER_CVAR2("vm_aim_camera_zoom_factor", &s.aimCameraZoomFactor, s.aimCameraZoomFactor, VF_DUMPTOCHAIR, "Viewmodel Tweaks: camera HFOV multiplier while aiming (1 = none)");

    REGISTER_CVAR2("vm_nudge_keys", &s.nudgeKeys, s.nudgeKeys, VF_DUMPTOCHAIR, "Viewmodel Tweaks: nudge keys edit the active pose (0/1)");
    REGISTER_CVAR2("vm_nudge_layout", &s.nudgeLayout, s.nudgeLayout, VF_DUMPTOCHAIR, "Viewmodel Tweaks: nudge key layout. 0 = numpad (4/6 8/2 7/1, NP0 rotate, NP. slow), 1 = IJKL (J/L I/K U/O, H rotate, N slow)");
    REGISTER_CVAR2("vm_nudge_target", &s.nudgeTarget, s.nudgeTarget, VF_DUMPTOCHAIR, "Viewmodel Tweaks: what the nudge keys edit while not aiming. 0 = global standing/crouch offset (by stance), 1 = this weapon's hip offset");
    REGISTER_CVAR2("vm_nudge_pos_speed", &s.nudgePosSpeed, s.nudgePosSpeed, VF_DUMPTOCHAIR, "Viewmodel Tweaks: nudge speed in m/s");
    REGISTER_CVAR2("vm_nudge_rot_speed", &s.nudgeRotSpeed, s.nudgeRotSpeed, VF_DUMPTOCHAIR, "Viewmodel Tweaks: nudge speed in deg/s");

    REGISTER_CVAR2("vm_converge", &s.convergeEnabled, s.convergeEnabled, VF_DUMPTOCHAIR, "Viewmodel Tweaks: hip-fire weapon convergence on the crosshair's impact point (0/1)");
    REGISTER_CVAR2("vm_converge_strength", &s.convergeStrength, s.convergeStrength, VF_DUMPTOCHAIR, "Viewmodel Tweaks: convergence strength (0..1)");
    REGISTER_CVAR2("vm_converge_max_angle", &s.convergeMaxAngle, s.convergeMaxAngle, VF_DUMPTOCHAIR, "Viewmodel Tweaks: convergence max angle in degrees");
    REGISTER_CVAR2("vm_converge_smooth", &s.convergeSmoothTime, s.convergeSmoothTime, VF_DUMPTOCHAIR, "Viewmodel Tweaks: convergence smoothing time constant in seconds");
    REGISTER_CVAR2("vm_converge_max_dist", &s.convergeMaxDist, s.convergeMaxDist, VF_DUMPTOCHAIR, "Viewmodel Tweaks: convergence raycast distance in meters (no hit = parallel)");
    REGISTER_CVAR2("vm_aim_wall_block", &s.aimWallBlockEnabled, s.aimWallBlockEnabled, VF_DUMPTOCHAIR, "Viewmodel Tweaks: prevent aiming while the weapon would poke into a wall (0/1)");
    REGISTER_CVAR2("vm_aim_block_reload", &s.aimBlockReload, s.aimBlockReload, VF_DUMPTOCHAIR, "Viewmodel Tweaks: no aiming down sights while the weapon reloads (0/1)");
    REGISTER_CVAR2("vm_aim_block_switch", &s.aimBlockSwitch, s.aimBlockSwitch, VF_DUMPTOCHAIR, "Viewmodel Tweaks: no aiming down sights while a weapon is holstered / drawn (0/1)");
    REGISTER_CVAR2("vm_aim_switch_delay", &s.aimSwitchDelay, s.aimSwitchDelay, VF_DUMPTOCHAIR, "Viewmodel Tweaks: seconds after a weapon change before aiming is allowed (covers the raise animation)");
    REGISTER_CVAR2("vm_reload_fade", &s.reloadFadesOffsets, s.reloadFadesOffsets, VF_DUMPTOCHAIR, "Viewmodel Tweaks: fade the hip offsets out while reloading so the support hand meets the weapon (0/1)");
    REGISTER_CVAR2("vm_reload_fade_time", &s.reloadFadeTime, s.reloadFadeTime, VF_DUMPTOCHAIR, "Viewmodel Tweaks: reload fade in/out time in seconds");
    REGISTER_CVAR2("vm_aim_wall_block_scale", &s.aimWallBlockScale, s.aimWallBlockScale, VF_DUMPTOCHAIR, "Viewmodel Tweaks: tolerance multiplier on the aim-block distance");
    REGISTER_CVAR2("vm_wall_push", &s.wallPushEnabled, s.wallPushEnabled, VF_DUMPTOCHAIR, "Viewmodel Tweaks: pull the weapon back towards the camera near walls (0/1); amount is per weapon");
    REGISTER_CVAR2("vm_wall_push_start", &s.wallPushStartDist, s.wallPushStartDist, VF_DUMPTOCHAIR, "Viewmodel Tweaks: distance (m) at which the wall pull-back starts");
    REGISTER_CVAR2("vm_wall_push_full", &s.wallPushFullDist, s.wallPushFullDist, VF_DUMPTOCHAIR, "Viewmodel Tweaks: distance (m) at which the full per-weapon pull-back is reached");
    REGISTER_CVAR2("vm_wall_push_smooth", &s.wallPushSmoothTime, s.wallPushSmoothTime, VF_DUMPTOCHAIR, "Viewmodel Tweaks: wall pull-back smoothing time constant in seconds");
    REGISTER_CVAR2("vm_wall_push_aiming", &s.wallPushWhileAiming, s.wallPushWhileAiming, VF_DUMPTOCHAIR, "Viewmodel Tweaks: keep the wall pull-back while aiming (0/1)");
    REGISTER_CVAR2("vm_reticle_mode", &s.reticleMode, s.reticleMode, VF_DUMPTOCHAIR, "Viewmodel Tweaks: reticle position. 0 = game default, 1 = centered, 2 = custom (vm_reticle_y)");
    REGISTER_CVAR2("vm_reticle_y", &s.reticleY, s.reticleY, VF_DUMPTOCHAIR, "Viewmodel Tweaks: custom g_reticleYPercentage (0 = top, 1 = bottom)");
    REGISTER_CVAR2("vm_reticle_style", &s.reticleStyle, s.reticleStyle, VF_DUMPTOCHAIR, "Viewmodel Tweaks: reticle style (hud_reticleSetting). 0 = game default, 1 = default reticle, 2 = simple dot, 3 = hidden");
    REGISTER_CVAR2("vm_aim_hide_reticle", &s.aimHideReticle, s.aimHideReticle, VF_DUMPTOCHAIR, "Viewmodel Tweaks: hide the reticle while aiming down sights (0/1)");

    REGISTER_CVAR2("vm_world_fov_enabled", &s.worldFovEnabled, s.worldFovEnabled, VF_DUMPTOCHAIR, "Viewmodel Tweaks: override the game's horizontal FOV, cl_hfov (0/1)");
    REGISTER_CVAR2("vm_world_fov", &s.worldFov, s.worldFov, VF_DUMPTOCHAIR, "Viewmodel Tweaks: horizontal FOV in degrees");
    REGISTER_CVAR2("vm_sprint_sens_enabled", &s.sprintSensEnabled, s.sprintSensEnabled, VF_DUMPTOCHAIR, "Viewmodel Tweaks: override the look-sensitivity scale while sprinting (0/1)");
    REGISTER_CVAR2("vm_sprint_sens_scale", &s.sprintSensScale, s.sprintSensScale, VF_DUMPTOCHAIR, "Viewmodel Tweaks: look-sensitivity multiplier while sprinting (1 = same as walking)");
    // feel: sprint
    REGISTER_CVAR2("vm_sprint_pose", &s.sprintPoseEnabled, s.sprintPoseEnabled, VF_DUMPTOCHAIR, "Viewmodel Tweaks: lower / tilt the weapon while sprinting (0/1)");
    RegisterPoseCVars(s.sprint, "sprint_", "sprint pose, added while sprinting");
    REGISTER_CVAR2("vm_sprint_blend_in", &s.sprintBlendIn, s.sprintBlendIn, VF_DUMPTOCHAIR, "Viewmodel Tweaks: seconds to blend into the sprint pose");
    REGISTER_CVAR2("vm_sprint_blend_out", &s.sprintBlendOut, s.sprintBlendOut, VF_DUMPTOCHAIR, "Viewmodel Tweaks: seconds to blend out of the sprint pose");
    REGISTER_CVAR2("vm_sprint_blocks_aim", &s.sprintBlocksAim, s.sprintBlocksAim, VF_DUMPTOCHAIR, "Viewmodel Tweaks: no aiming down sights while sprinting (0/1)");
    REGISTER_CVAR2("vm_sprint_zerog", &s.sprintInZeroG, s.sprintInZeroG, VF_DUMPTOCHAIR, "Viewmodel Tweaks: treat the zero-G thruster boost as sprinting (pose, sway, aim block) (0/1)");
    REGISTER_CVAR2("vm_sprint_sway", &s.sprintSwayEnabled, s.sprintSwayEnabled, VF_DUMPTOCHAIR, "Viewmodel Tweaks: extra weapon sway while sprinting (0/1)");
    REGISTER_CVAR2("vm_sprint_sway_pos", &s.sprintSwayPos, s.sprintSwayPos, VF_DUMPTOCHAIR, "Viewmodel Tweaks: sprint sway amplitude in meters");
    REGISTER_CVAR2("vm_sprint_sway_rot", &s.sprintSwayRot, s.sprintSwayRot, VF_DUMPTOCHAIR, "Viewmodel Tweaks: sprint sway roll amplitude in degrees");
    REGISTER_CVAR2("vm_sprint_sway_freq", &s.sprintSwayFreq, s.sprintSwayFreq, VF_DUMPTOCHAIR, "Viewmodel Tweaks: sprint sway frequency in Hz");
    // feel: aim sway
    REGISTER_CVAR2("vm_sway_enabled", &s.aimSwayEnabled, s.aimSwayEnabled, VF_DUMPTOCHAIR, "Viewmodel Tweaks: sway and settle while aiming (0/1)");
    REGISTER_CVAR2("vm_sway_pos", &s.aimSwayPos, s.aimSwayPos, VF_DUMPTOCHAIR, "Viewmodel Tweaks: aim sway position amplitude at rest, meters");
    REGISTER_CVAR2("vm_sway_rot", &s.aimSwayRot, s.aimSwayRot, VF_DUMPTOCHAIR, "Viewmodel Tweaks: aim sway rotation amplitude at rest, degrees");
    REGISTER_CVAR2("vm_sway_freq", &s.aimSwayFreq, s.aimSwayFreq, VF_DUMPTOCHAIR, "Viewmodel Tweaks: aim sway frequency in Hz");
    REGISTER_CVAR2("vm_sway_initial", &s.aimSwayInitial, s.aimSwayInitial, VF_DUMPTOCHAIR, "Viewmodel Tweaks: sway multiplier when the sights come up");
    REGISTER_CVAR2("vm_sway_settle", &s.aimSwaySettleTime, s.aimSwaySettleTime, VF_DUMPTOCHAIR, "Viewmodel Tweaks: seconds for the sway to settle");
    REGISTER_CVAR2("vm_sway_move", &s.aimSwayMoveMult, s.aimSwayMoveMult, VF_DUMPTOCHAIR, "Viewmodel Tweaks: extra sway at walking speed");
    REGISTER_CVAR2("vm_sway_sprint_penalty", &s.aimSwaySprintPenalty, s.aimSwaySprintPenalty, VF_DUMPTOCHAIR, "Viewmodel Tweaks: extra sway right after sprinting");
    REGISTER_CVAR2("vm_sway_sprint_recover", &s.aimSwaySprintRecover, s.aimSwaySprintRecover, VF_DUMPTOCHAIR, "Viewmodel Tweaks: seconds for the sprint penalty to fade");
    REGISTER_CVAR2("vm_steady_enabled", &s.steadyEnabled, s.steadyEnabled, VF_DUMPTOCHAIR, "Viewmodel Tweaks: hold-breath key steadies the sights (0/1)");
    REGISTER_CVAR2("vm_steady_key", &s.steadyKey, s.steadyKey, VF_DUMPTOCHAIR, "Viewmodel Tweaks: EKeyId of the steady key (0 = none)");
    REGISTER_CVAR2("vm_steady_reduce", &s.steadyReduce, s.steadyReduce, VF_DUMPTOCHAIR, "Viewmodel Tweaks: sway reduction while steady (0..1)");
    REGISTER_CVAR2("vm_steady_duration", &s.steadyDuration, s.steadyDuration, VF_DUMPTOCHAIR, "Viewmodel Tweaks: seconds of breath");
    REGISTER_CVAR2("vm_steady_recover", &s.steadyRecover, s.steadyRecover, VF_DUMPTOCHAIR, "Viewmodel Tweaks: seconds to recover a full breath");
    REGISTER_CVAR2("vm_steady_consume_key", &s.steadyConsumeKey, s.steadyConsumeKey, VF_DUMPTOCHAIR, "Viewmodel Tweaks: swallow the steady key (0/1)");
    // feel: view drag
    REGISTER_CVAR2("vm_drag_enabled", &s.dragEnabled, s.dragEnabled, VF_DUMPTOCHAIR, "Viewmodel Tweaks: GoldenEye-style view drag (0/1)");
    REGISTER_CVAR2("vm_drag_lead", &s.dragLead, s.dragLead, VF_DUMPTOCHAIR, "Viewmodel Tweaks: 1 = weapon leads into the turn, 0 = lags behind");
    REGISTER_CVAR2("vm_drag_pos", &s.dragPos, s.dragPos, VF_DUMPTOCHAIR, "Viewmodel Tweaks: view drag meters per rad/s");
    REGISTER_CVAR2("vm_drag_rot", &s.dragRot, s.dragRot, VF_DUMPTOCHAIR, "Viewmodel Tweaks: view drag degrees per rad/s");
    REGISTER_CVAR2("vm_drag_stiffness", &s.dragStiffness, s.dragStiffness, VF_DUMPTOCHAIR, "Viewmodel Tweaks: view drag spring stiffness");
    REGISTER_CVAR2("vm_drag_damping", &s.dragDamping, s.dragDamping, VF_DUMPTOCHAIR, "Viewmodel Tweaks: view drag damping ratio");
    REGISTER_CVAR2("vm_drag_max_pos", &s.dragMaxPos, s.dragMaxPos, VF_DUMPTOCHAIR, "Viewmodel Tweaks: view drag position clamp, meters");
    REGISTER_CVAR2("vm_drag_max_rot", &s.dragMaxRot, s.dragMaxRot, VF_DUMPTOCHAIR, "Viewmodel Tweaks: view drag rotation clamp, degrees");
    REGISTER_CVAR2("vm_drag_aim_scale", &s.dragAimScale, s.dragAimScale, VF_DUMPTOCHAIR, "Viewmodel Tweaks: view drag while aiming, 0..1 of the hip amount");
    REGISTER_CVAR2("vm_drag_pitch_scale", &s.dragPitchScale, s.dragPitchScale, VF_DUMPTOCHAIR, "Viewmodel Tweaks: view drag pitch (look up/down) relative to yaw");

    REGISTER_CVAR2("vm_fov_enabled", &s.fovEnabled, s.fovEnabled, VF_DUMPTOCHAIR, "Viewmodel Tweaks: enable weapon FOV override (0/1)");
    REGISTER_CVAR2("vm_fov", &s.fov, s.fov, VF_DUMPTOCHAIR, "Viewmodel Tweaks: weapon FOV in degrees (game default 55)");
    REGISTER_CVAR2("vm_gui_mouse", &s.guiMouse, s.guiMouse, VF_DUMPTOCHAIR, "Viewmodel Tweaks: show cursor and block look input while the settings window is open (0/1)");
    REGISTER_CVAR2("vm_show_window", &s.showWindow, s.showWindow, VF_DUMPTOCHAIR, "Viewmodel Tweaks: show the settings window in the Chairloader GUI (0/1)");
    REGISTER_CVAR2("vm_show_advanced", &s.showAdvanced, s.showAdvanced, VF_DUMPTOCHAIR, "Viewmodel Tweaks: show diagnostics, self-tests and experimental features in the window (0/1)");
}

void ModMain::InitSystem(const ModInitInfo& initInfo, ModDllInfo& dllInfo)
{
    BaseClass::InitSystem(initInfo, dllInfo);
    RegisterCVars();
    SanitizeSettings();
    m_offsetHookActive = s_hookPwaUpdate.IsHooked() && s_hookPwaContextUpdate.IsHooked();
    m_cameraHookActive = s_hookUpdateView.IsHooked();
    CryLog("ViewmodelTweaks: initialized (weapon offset hooks {}, camera hook {})", m_offsetHookActive ? "installed" : "NOT installed",
        m_cameraHookActive ? "installed" : "NOT installed");
}

void ModMain::InitGame(bool isHotReloading)
{
    BaseClass::InitGame(isHotReloading);
    if (gEnv && gEnv->pInput && !m_inputListenerRegistered)
    {
        gEnv->pInput->AddEventListener(this);
        m_inputListenerRegistered = true;
    }
    LoadWeapons();
}

//---------------------------------------------------------------------------------
// Mod Shutdown
//---------------------------------------------------------------------------------
void ModMain::ShutdownGame(bool isHotUnloading)
{
    if (m_weaponsDirty)
        SaveWeapons();
    if (gEnv && gEnv->pInput && m_inputListenerRegistered)
    {
        gEnv->pInput->RemoveEventListener(this);
        m_inputListenerRegistered = false;
    }
    m_aimKeyHeld = false;
    m_aimBlend = 0.0f;
    UpdateReticle(); // un-hide if we hid it
    UpdateCameraZoom(false);
    UpdateMouseCapture(false);
    BaseClass::ShutdownGame(isHotUnloading);
}


//! Removes every console variable with the given prefix. The console keeps raw pointers to the names, help
//! strings and value storage we registered - all of which vanish with this DLL - and the engine touches them
//! again at shutdown (crash on exit) unless they are gone before the module is unloaded.
static void UnregisterCVarsWithPrefix(const char* prefix)
{
    if (!gEnv || !gEnv->pConsole)
        return;
    const int total = gEnv->pConsole->GetNumVars(false);
    if (total <= 0)
        return;
    std::vector<const char*> names((size_t)total + 1, nullptr);
    const size_t n = gEnv->pConsole->GetSortedVars(names.data(), names.size(), prefix);
    std::vector<std::string> copies;
    for (size_t i = 0; i < n && i < names.size(); i++)
        if (names[i])
            copies.emplace_back(names[i]);
    for (const std::string& name : copies)
        gEnv->pConsole->UnregisterVariable(name.c_str(), true);
    CryLog("Unregistered {} console variable(s) with prefix '{}'", copies.size(), prefix);
}

void ModMain::ShutdownSystem(bool isHotUnloading)
{
    // Restore the stock weapon FOV so unloading leaves the game as we found it.
    if (gEnv && gEnv->pRenderer && m_lastAppliedFov >= 0.0f)
    {
        float stock = PreyInternals::STOCK_WEAPON_FOV;
        gEnv->pRenderer->EF_Query(EFQ_SetDrawNearFov, stock);
    }
    UnregisterCVarsWithPrefix("vm_");
    BaseClass::ShutdownSystem(isHotUnloading);
}

//---------------------------------------------------------------------------------
// Main Update Loop
//---------------------------------------------------------------------------------
void ModMain::MainUpdate(unsigned updateFlags)
{
    m_frameIndex++;
    // Time step for the filters (blends, convergence, wall pull-back, feel). This must be a real,
    // per-update delta: an exponential smoother uses k = 1 - exp(-dt/tau), which is exactly 0 when dt is
    // 0 (the value never moves - the filter looks "dead") and 1 when dt is huge (the value snaps - the
    // filter looks "off"). So a wrong dt does not merely mistune the smoothing, it disables it.
    //
    // The game frame time (ITimer::GetFrameTime) is the game clock: it is scaled by the trainer's time
    // dilation and paused with the game, and reads oddly here. The async *seconds* accessor is a float32
    // of absolute time, which after a while cannot resolve a 16 ms step at all (huge value minus huge
    // value rounds to 0). The async clock as an int64 tick count (100000/s) is precise and monotonic, so
    // the step between two updates is taken from that. GetFrameTime is kept only for the on-screen readout.
    m_dtGame = (gEnv && gEnv->pTimer) ? gEnv->pTimer->GetFrameTime() : 0.0f;
    float dt = 1.0f / 60.0f; // sane default until the clock gives us two readings (never 0 -> never frozen)
    if (gEnv && gEnv->pTimer)
    {
        const int64 nowTicks = gEnv->pTimer->GetAsyncTime().GetValue();
        if (m_lastAsyncTicks >= 0 && nowTicks > m_lastAsyncTicks)
        {
            const float delta = (float)(nowTicks - m_lastAsyncTicks) * (1.0f / 100000.0f);
            if (Finite(delta) && delta > 0.0f)
            {
                dt = clamp_tpl(delta, 0.0f, 0.1f); // a longer gap is a load / alt-tab / pause, not a frame
                m_updateHz += (1.0f / clamp_tpl(delta, 1e-4f, 1.0f) - m_updateHz) * 0.05f;
            }
        }
        m_lastAsyncTicks = nowTicks;
    }
    m_dtUsed = dt;
    UpdateBlendStates(dt);
    UpdateFeel(dt, ArkPlayer::GetInstancePtr());
    SanitizeFeel();
    UpdateNudge(dt);
    UpdateConvergence(dt);
    RecordTrace();

    if (m_weaponsDirty)
    {
        m_weaponsSaveTimer += dt;
        if (m_weaponsSaveTimer > 2.0f)
        {
            SaveWeapons();
            m_weaponsSaveTimer = 0.0f;
        }
    }

    UpdateReticle();
    UpdateCameraInputOverrides();

    bool want = m_settings.guiMouse != 0 && m_settings.showWindow != 0 && gCL && gCL->gui && gCL->gui->IsEnabled();
    // Player gone (level unload / main menu) -> the input-mode handle is stale, release everything.
    if (m_mouseCaptured && !ArkPlayer::GetInstancePtr())
        want = false;
    UpdateMouseCapture(want);
}

void ModMain::UpdateMouseCapture(bool wantCapture)
{
    if (wantCapture == m_mouseCaptured)
        return;

    ArkPlayer* pPlayer = ArkPlayer::GetInstancePtr();

    if (wantCapture)
    {
        // Only meaningful while in a level; in the main menu the cursor is already visible.
        if (!pPlayer)
            return;

        if (gEnv && gEnv->pHardwareMouse)
            gEnv->pHardwareMouse->IncrementCounter();

        // Switch to "menu" input mode so mouse movement drives the cursor instead of the camera.
        m_inputModeHandle = pPlayer->m_input.EnableInputMode(ArkPlayerInput::Mode::menu);
        m_mouseCaptured = true;
    }
    else
    {
        if (gEnv && gEnv->pHardwareMouse)
            gEnv->pHardwareMouse->DecrementCounter();

        if (pPlayer && m_inputModeHandle >= 0)
            pPlayer->m_input.DisableInputMode(m_inputModeHandle);

        m_inputModeHandle = -1;
        m_mouseCaptured = false;
    }
}

void ModMain::LateUpdate(unsigned updateFlags)
{
    // Runs after the game's ArkPlayerZoomManager::Update for this frame.
    EnforceWeaponFov();

    // Pipeline diagnostics: did the weapon offset code run this frame?
    PipelineDiag& d = m_diag;
    if (!m_currentWeaponClass.empty() && m_settings.enabled)
    {
        if (d.pwaUpdatesThisFrame == 0)
        {
            d.missingFrames++;
            if (Now() - d.lastMissingLog > 1.0f)
            {
                char buf[160];
                snprintf(buf, sizeof(buf), "no weapon-offset update this frame (ctx updates: %d, instanceCount %d, modifier %s)",
                    d.ctxUpdatesThisFrame, d.lastInstanceCount, d.lastModifierNull ? "NULL" : "ok");
                d.Add(Now(), buf);
                d.lastMissingLog = Now();
            }
        }
        else
        {
            if (d.missingFrames > 0)
            {
                char buf[96];
                snprintf(buf, sizeof(buf), "weapon-offset updates resumed after %d frame(s)", d.missingFrames);
                d.Add(Now(), buf);
            }
            d.missingFrames = 0;
        }
    }
    d.pwaUpdatesLastFrame = d.pwaUpdatesThisFrame;
    d.ctxUpdatesLastFrame = d.ctxUpdatesThisFrame;
    d.pwaUpdatesThisFrame = 0;
    d.ctxUpdatesThisFrame = 0;
}

//---------------------------------------------------------------------------------
// GUI
//---------------------------------------------------------------------------------
void ModMain::Draw()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Viewmodel Tweaks"))
        {
            bool show = m_settings.showWindow != 0;
            if (ImGui::MenuItem("Settings window", nullptr, &show))
                m_settings.showWindow = show ? 1 : 0;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (m_settings.showWindow)
        DrawWindow();
}

static bool SliderCm(const char* label, float& meters, float rangeCm, const char* tooltip)
{
    float cm = meters * 100.0f;
    bool changed = ImGui::SliderFloat(label, &cm, -rangeCm, rangeCm, "%.2f cm");
    if (ImGui::IsItemHovered() && tooltip)
        ImGui::SetTooltip("%s", tooltip);
    if (changed)
        meters = cm / 100.0f;
    return changed;
}

static bool SliderDeg(const char* label, float& deg, float range, const char* tooltip)
{
    bool changed = ImGui::SliderFloat(label, &deg, -range, range, "%.2f deg");
    if (ImGui::IsItemHovered() && tooltip)
        ImGui::SetTooltip("%s", tooltip);
    return changed;
}

static bool CheckboxInt(const char* label, int& value, const char* tooltip = nullptr)
{
    bool b = value != 0;
    bool changed = ImGui::Checkbox(label, &b);
    if (changed)
        value = b ? 1 : 0;
    if (tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
    return changed;
}

static void TextQuatT(const char* label, const QuatT& q)
{
    Ang3 a(q.q);
    ImGui::Text("%s: pos (%.3f, %.3f, %.3f) m  rot (p %.1f, r %.1f, y %.1f) deg", label,
        q.t.x, q.t.y, q.t.z, RAD2DEG(a.x), RAD2DEG(a.y), RAD2DEG(a.z));
}

static void DrawReticleControls(ViewmodelSettings& s, const char* id)
{
    ImGui::PushID(id);
    ImGui::Text("Reticle position");
    ImGui::SameLine();
    ImGui::RadioButton("Game default", &s.reticleMode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Centered", &s.reticleMode, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Custom", &s.reticleMode, 2);
    if (s.reticleMode == 2)
        ImGui::SliderFloat("Vertical position", &s.reticleY, 0.2f, 0.8f, "%.3f of screen height");
    ImGui::Text("Reticle style   ");
    ImGui::SameLine();
    ImGui::RadioButton("Game default##style", &s.reticleStyle, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Weapon reticle", &s.reticleStyle, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Simple dot", &s.reticleStyle, 2);
    ImGui::SameLine();
    ImGui::RadioButton("Hidden", &s.reticleStyle, 3);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("hud_reticleSetting: the game's own reticle options. While you use an in-world screen the reticle\ncomes back as the cursor and is hidden again afterwards.");
    CheckboxInt("Hide reticle while aiming", s.aimHideReticle, "Switches the reticle off once the weapon is more than halfway up to the sights, and back when you lower it.");
    ImGui::PopID();
}

bool ModMain::DrawPoseSliders(PoseOffset& pose, const char* id, float posRangeCm, float rotRangeDeg)
{
    bool ch = false;
    ImGui::PushID(id);
    ImGui::Text("Position (view space)");
    ch |= SliderCm("Right / Left", pose.posX, posRangeCm, "Positive moves the weapon to the right.");
    ch |= SliderCm("Forward / Back", pose.posY, posRangeCm, "Positive moves the weapon away from the camera.");
    ch |= SliderCm("Up / Down", pose.posZ, posRangeCm, "Positive moves the weapon up.");
    ImGui::Spacing();
    ImGui::Text("Rotation");
    ch |= SliderDeg("Pitch", pose.pitch, rotRangeDeg, "Positive tilts the muzzle up.");
    ch |= SliderDeg("Yaw", pose.yaw, rotRangeDeg, "Positive turns the muzzle to the left.");
    ch |= SliderDeg("Roll", pose.roll, rotRangeDeg, "Rolls the weapon around its forward axis.");
    if (ImGui::Button("Reset"))
    {
        pose.Reset();
        ch = true;
    }
    ImGui::PopID();
    return ch;
}

void ModMain::DrawWindow()
{
    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(500, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Viewmodel Tweaks", &open))
    {
        ViewmodelSettings& s = m_settings;

        ImGui::TextDisabled("New here? Start with Quick Settings. Aim with %s. Per-weapon poses live in the Weapon tab. Ctrl+click a slider to type a value.", GetKeyName(s.aimKey));
        CheckboxInt("Enable position / rotation offsets", s.enabled);
        ImGui::SameLine();
        ImGui::TextDisabled("| %s%s%s | %s", m_isCrouching ? "crouching" : "standing", m_isAiming ? " | aiming" : "", m_feel.sprinting ? " | sprinting" : "",
            m_currentWeaponClass.empty() ? "no weapon" : m_currentWeaponClass.c_str());
        {
            bool vanilla = s.bypass != 0;
            if (ImGui::Checkbox("Vanilla viewmodel (bypass the whole mod)", &vanilla))
                s.bypass = vanilla ? 1 : 0;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Switches off every viewmodel feature at once - offsets, ironsights, feel, weapon FOV, convergence, wall pull-back,\n"
                                  "spread changes - for before/after comparisons. The reticle, world FOV and sprint sensitivity settings stay as they are.\n"
                                  "Your settings are kept; untick to get everything back.");
            if (s.bypass)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "VANILLA - the mod is bypassed");
            }
        }
        if (!m_offsetHookActive || !m_cameraHookActive)
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Warning: some game hooks could not be installed (%s%s). This game build is not the one Chairloader supports; the mod is partly disabled.",
                m_offsetHookActive ? "" : "weapon offsets ", m_cameraHookActive ? "" : "camera/ironsights");
        if (m_nanRecoveries > 0 && s.showAdvanced)
            ImGui::TextDisabled("Numeric recoveries this session: %d (frames where a bad value from the game or the mod was caught and reset)", m_nanRecoveries);

        ImGui::Separator();

        if (ImGui::BeginTabBar("vm_tabs"))
        {
            //------------------------------------------------------------------ Quick Settings
            if (ImGui::BeginTabItem("Quick Settings"))
            {
                ImGui::TextWrapped("The essentials. Everything else (per-weapon poses, crouch offsets, nudge keys, fine tuning) is in the other tabs.");
                ImGui::Spacing();

                CheckboxInt("Enable weapon offsets", s.enabled, "Master switch for all position / rotation offsets, convergence and wall pull-back.");
                ImGui::SameLine();
                CheckboxInt("Vanilla viewmodel##q", s.bypass, "Bypass every viewmodel feature (everything except reticle, world FOV and sprint sensitivity) for before/after comparisons.");
                ImGui::Indent();
                CheckboxInt("Override weapon FOV", s.fovEnabled, "Vertical FOV used to render the weapon and arms. Game default is 55. Higher = smaller weapon.");
                ImGui::BeginDisabled(!s.fovEnabled);
                ImGui::SliderFloat("Weapon FOV##q", &s.fov, 20.0f, 120.0f, "%.1f deg");
                ImGui::EndDisabled();
                ImGui::Unindent();
                ImGui::Spacing();

                CheckboxInt("Override field of view", s.worldFovEnabled, "The game's horizontal FOV (cl_hfov), same as the options menu slider but kept where you set it.");
                ImGui::BeginDisabled(!s.worldFovEnabled);
                ImGui::SliderFloat("Field of view##q", &s.worldFov, 60.0f, 130.0f, "%.1f deg (horizontal)");
                ImGui::EndDisabled();
                if (!s.worldFovEnabled && gEnv && gEnv->pConsole)
                    if (ICVar* pVar = gEnv->pConsole->GetCVar("cl_hfov"))
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("use current"))
                            s.worldFov = pVar->GetFVal();
                    }
                CheckboxInt("Sensitivity sprint scale", s.sprintSensEnabled,
                    "The game turns the look sensitivity down while sprinting. 1.0 = same as walking.");
                ImGui::BeginDisabled(!s.sprintSensEnabled);
                ImGui::SliderFloat("Sprint sensitivity##q", &s.sprintSensScale, 0.1f, 2.0f, "x%.2f");
                ImGui::EndDisabled();
                if (m_gameSprintSensScale >= 0.0f)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(game: x%.2f)", m_gameSprintSensScale);
                }
                ImGui::Spacing();

                CheckboxInt("Enable ironsights", s.aimEnabled, "Hold (or toggle) the aim key to bring the weapon up to the sights. Poses are per weapon (Weapon tab).");
                ImGui::BeginDisabled(!s.aimEnabled);
                ImGui::Indent();
                ImGui::Text("Aim key: %s", GetKeyName(s.aimKey));
                ImGui::SameLine();
                if (m_waitingForAimKey)
                    ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "press a key or mouse button... (Esc cancels)");
                else if (ImGui::Button("Bind##q"))
                    m_waitingForAimKey = true;
                ImGui::SameLine();
                if (ImGui::Button("RMB##q"))
                    s.aimKey = (int)eKI_Mouse2;
                CheckboxInt("Toggle instead of hold##q", s.aimToggle);
                CheckboxInt("Block the game's own action on this key##q", s.aimConsumeKey,
                    "Swallows the key so whatever the game has bound to it (e.g. RMB) does nothing while this mod is enabled.");
                CheckboxInt("Separate weapon FOV while aiming##q", s.aimFovEnabled, "Blends the weapon FOV towards this value while aiming. Lower = bigger weapon.");
                ImGui::BeginDisabled(!s.aimFovEnabled);
                ImGui::SliderFloat("Ironsights FOV##q", &s.aimFov, 20.0f, 120.0f, "%.1f deg");
                ImGui::EndDisabled();
                CheckboxInt("Zoom camera while aiming##q", s.aimCameraZoom, "Lowers the world FOV while aiming, like ADS in modern shooters.");
                ImGui::BeginDisabled(!s.aimCameraZoom);
                ImGui::SliderFloat("Camera zoom##q", &s.aimCameraZoomFactor, 0.2f, 1.5f, "x%.2f FOV");
                ImGui::EndDisabled();
                const char* sensModesQ[] = { "Game default (weapon's zoomed multiplier)", "Match camera zoom factor", "Custom multiplier" };
                ImGui::Combo("ADS look sensitivity##q", &s.aimSensMode, sensModesQ, 3);
                ImGui::BeginDisabled(s.aimSensMode != 2);
                ImGui::SliderFloat("Sensitivity while aiming##q", &s.aimSensScale, 0.05f, 3.0f, "x%.2f");
                ImGui::EndDisabled();
                ImGui::Unindent();
                ImGui::EndDisabled();
                ImGui::Spacing();

                CheckboxInt("Enable weapon convergence", s.convergeEnabled,
                    "Hip fire: rotates the weapon so the barrel points at what the crosshair is over. Tuning in the Global tab.");
                CheckboxInt("Enable wall pull-back", s.wallPushEnabled,
                    "Slides the weapon back towards the camera near walls. Distances in the Global tab, amount per weapon in the Weapon tab.");
                ImGui::Spacing();
                ImGui::Text("Feel");
                ImGui::SameLine();
                CheckboxInt("Sprint pose##q", s.sprintPoseEnabled, "Lowers and tilts the weapon while sprinting, with a bit more sway. Tuning in the Feel tab.");
                ImGui::SameLine();
                CheckboxInt("Sway while aiming##q", s.aimSwayEnabled, "The sights drift in a slow figure-eight: large when they come up, after sprinting or while moving, calm when you hold still. Tuning in the Feel tab.");
                ImGui::SameLine();
                CheckboxInt("View drag##q", s.dragEnabled, "GoldenEye-style: the weapon follows your turns on a spring. Tuning in the Feel tab.");
                ImGui::Spacing();
                DrawReticleControls(s, "qret");
                ImGui::EndTabItem();
            }

            //------------------------------------------------------------------ Global
            if (ImGui::BeginTabItem("Global"))
            {
                ImGui::BeginDisabled(!s.enabled);
                ImGui::TextWrapped("Offsets applied to ALL weapons. Per-weapon offsets (next tab) are added on top.");
                if (ImGui::CollapsingHeader("Standing offset", ImGuiTreeNodeFlags_DefaultOpen))
                    DrawPoseSliders(s.base, "base", 30.0f, 45.0f);
                if (ImGui::CollapsingHeader("Crouch offset", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    CheckboxInt("Enable crouch offset", s.crouchEnabled, "Blends this extra offset in while sneaking (crouched).");
                    ImGui::BeginDisabled(!s.crouchEnabled);
                    ImGui::SliderFloat("Transition time", &s.crouchTime, 0.0f, 1.5f, "%.2f s");
                    ImGui::ProgressBar(SmoothStep01(m_crouchBlend), ImVec2(-1, 0), m_isCrouching ? "crouched" : "standing");
                    DrawPoseSliders(s.crouch, "crouch", 30.0f, 45.0f);
                    ImGui::EndDisabled();
                }
                if (ImGui::CollapsingHeader("Reloading", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    CheckboxInt("Fade the offsets out while reloading", s.reloadFadesOffsets,
                        "Reload animations move the support hand to where the weapon is in the STOCK pose (shells, magazines), so a\n"
                        "shifted weapon leaves a gap between hand and shell. With this on, every hip-side change (pose, convergence,\n"
                        "wall pull-back, sprint pose, drag) eases out when a reload starts and back in when it ends.");
                    ImGui::BeginDisabled(!s.reloadFadesOffsets);
                    ImGui::SliderFloat("Fade time", &s.reloadFadeTime, 0.05f, 0.6f, "%.2f s");
                    ImGui::ProgressBar(SmoothStep01(m_reloadFade), ImVec2(-1, 0), m_wsReloading ? "reloading" : "not reloading");
                    ImGui::EndDisabled();
                }
                if (ImGui::CollapsingHeader("Weapon convergence (hip fire)"))
                {
                    CheckboxInt("Point the weapon at the crosshair's impact point", s.convergeEnabled,
                        "Raycasts along the view every frame and rotates the weapon in place so the barrel\nconverges on what the crosshair is over. Noticeable at close range, invisible far away.\nFades out while aiming down sights.");
                    ImGui::BeginDisabled(!s.convergeEnabled);
                    ImGui::SliderFloat("Strength", &s.convergeStrength, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Max angle", &s.convergeMaxAngle, 0.0f, 30.0f, "%.1f deg");
                    ImGui::SliderFloat("Smoothing", &s.convergeSmoothTime, 0.0f, 0.5f, "%.2f s");
                    ImGui::SliderFloat("Max distance", &s.convergeMaxDist, 2.0f, 100.0f, "%.0f m");
                    ImGui::TextDisabled("Hit: %s at %.2f m | yaw %.2f (target %.2f) pitch %.2f (target %.2f) deg | weapon offset from eye (%.1f, %.1f, %.1f) cm",
                        m_convergeHit ? "yes" : "no", m_convergeDist, m_convergeYaw, m_convergeTargetYaw, m_convergePitch, m_convergeTargetPitch,
                        m_render.hipRelCam.t.x * 100, m_render.hipRelCam.t.y * 100, m_render.hipRelCam.t.z * 100);
                    {
                        const float tau = max(s.convergeSmoothTime, 0.0f);
                        const float k = (tau > 0.0005f) ? (1.0f - expf(-m_dtUsed / tau)) : 1.0f;
                        ImGui::TextDisabled("Filter step %.2f ms (game frame time %.2f ms) | %.0f updates/s | %.1f %% of the gap closed per update",
                            m_dtUsed * 1000.0f, m_dtGame * 1000.0f, m_updateHz, k * 100.0f);
                    }
                    if (ImGui::Button("Save trace CSV"))
                        SaveTrace("manual");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Writes the last ~30 s of per-update values (filter output vs. the weapon's measured orientation) to\n"
                            "Mods/config/Vee.ViewmodelTweaks.trace.csv. Also written automatically when the weapon jumps while the filter did not.");
                    ImGui::SameLine();
                    ImGui::Checkbox("auto on pop", &m_traceAuto);
                    if (!m_traceStatus.empty())
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", m_traceStatus.c_str());
                    }
                    ImGui::EndDisabled();
                    ImGui::Spacing();
                    CheckboxInt("Prevent aiming while colliding with a wall", s.aimWallBlockEnabled,
                        "No ironsights while the crosshair's impact point is closer than the weapon's reach\n"
                        "(its distance from the eye + its wall pull-back, so longer weapons block sooner). Uses the same view raycast.");
                    ImGui::BeginDisabled(!s.aimWallBlockEnabled);
                    ImGui::SliderFloat("Tolerance", &s.aimWallBlockScale, 0.25f, 4.0f, "x%.2f");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Multiplier on the blocking distance. Higher = aiming is blocked further away from the wall.");
                    ImGui::TextDisabled("Blocks when the wall is closer than %.2f m | wall at %.2f m | %s", m_aimBlockDist, m_convergeDist,
                        m_aimBlockedByWall ? "BLOCKED" : "free");
                    ImGui::EndDisabled();
                }
                if (ImGui::CollapsingHeader("Wall pull-back"))
                {
                    CheckboxInt("Pull the weapon back near walls", s.wallPushEnabled,
                        "Uses the same view raycast: the closer the crosshair's impact point, the further the weapon slides\n"
                        "back towards the camera, as if you were keeping it from hitting the wall.\n"
                        "How far each weapon moves is set per weapon (Weapon tab): a little for the pistol, more for the shotgun / GLOO gun.");
                    ImGui::BeginDisabled(!s.wallPushEnabled);
                    ImGui::SliderFloat("Starts at", &s.wallPushStartDist, 0.3f, 3.0f, "%.2f m");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Distance from the camera to the wall where the pull-back begins.");
                    ImGui::SliderFloat("Full at", &s.wallPushFullDist, 0.0f, 2.0f, "%.2f m");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Distance where the weapon has moved back by its full per-weapon amount. Must be less than 'Starts at'.");
                    ImGui::SliderFloat("Smoothing##wall", &s.wallPushSmoothTime, 0.0f, 0.5f, "%.2f s");
                    CheckboxInt("Also while aiming", s.wallPushWhileAiming,
                        "The pull-back is along the view axis, so the sights stay on the crosshair; the weapon just gets a bit closer/bigger.");
                    {
                        const WeaponSettings* pW = FindCurrentWeapon();
                        ImGui::TextDisabled("Now: %.1f cm back (target %.1f cm) | this weapon's max: %.1f cm | wall at %.2f m",
                            m_wallPush * 100, m_wallPushTarget * 100, (pW ? pW->wallPush : WeaponSettings().wallPush) * 100, m_convergeDist);
                    }
                    ImGui::EndDisabled();
                }
                ImGui::EndDisabled();
                ImGui::EndTabItem();
            }

            //------------------------------------------------------------------ Feel
            if (ImGui::BeginTabItem("Feel"))
            {
                ImGui::BeginDisabled(!s.enabled);
                const FeelState& f = m_feel;
                if (ImGui::CollapsingHeader("Sprint pose", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    CheckboxInt("Lower the weapon while sprinting", s.sprintPoseEnabled);
                    ImGui::BeginDisabled(!s.sprintPoseEnabled);
                    ImGui::TextDisabled("%s%s | blend %.2f | speed %.1f m/s", f.sprinting ? "sprinting" : "not sprinting", f.zeroG ? " | zero-G" : "", f.sprintBlend, f.speed);
                    DrawPoseSliders(s.sprint, "sprintpose", 20.0f, 30.0f);
                    ImGui::SliderFloat("Blend in", &s.sprintBlendIn, 0.05f, 1.0f, "%.2f s");
                    ImGui::SliderFloat("Blend out", &s.sprintBlendOut, 0.05f, 1.0f, "%.2f s");
                    CheckboxInt("No aiming while sprinting", s.sprintBlocksAim, "The aim key is ignored while sprinting; the sights come up as soon as you stop.");
                    CheckboxInt("Also in zero-G", s.sprintInZeroG,
                        "The thruster boost in zero-G counts as sprinting for the game. Off (default): no sprint pose, sway or aim block while\n"
                        "floating - there is no running body to lower the weapon for. On: boosting behaves like sprinting.");
                    CheckboxInt("Extra sway while sprinting", s.sprintSwayEnabled, "A side-to-side / up-down figure on top of the game's own sprint animation.");
                    ImGui::BeginDisabled(!s.sprintSwayEnabled);
                    ImGui::SliderFloat("Sway position", &s.sprintSwayPos, 0.0f, 0.05f, "%.3f m");
                    ImGui::SliderFloat("Sway roll", &s.sprintSwayRot, 0.0f, 6.0f, "%.1f deg");
                    ImGui::SliderFloat("Sway frequency", &s.sprintSwayFreq, 0.5f, 5.0f, "%.2f Hz");
                    ImGui::EndDisabled();
                    ImGui::EndDisabled();
                }
                if (ImGui::CollapsingHeader("Sway and settle while aiming", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    CheckboxInt("Sway while aiming", s.aimSwayEnabled,
                        "A slow figure-eight of the sights. The rotation part moves the sights off the crosshair, so with the reticle hidden\n"
                        "while aiming (FOV tab) it is real inaccuracy you have to time your shots around; with the reticle visible it is cosmetic.");
                    ImGui::BeginDisabled(!s.aimSwayEnabled);
                    ImGui::TextDisabled("amplitude x%.2f | aiming for %.1f s | %.1f s since sprint | breath %.1f s%s", f.swayAmplitude, f.aimTime,
                        min(f.timeSinceSprint, 999.0f), f.steadyLeft, f.steadyActive ? " (steady)" : "");
                    ImGui::SliderFloat("Position at rest", &s.aimSwayPos, 0.0f, 0.02f, "%.4f m");
                    ImGui::SliderFloat("Rotation at rest", &s.aimSwayRot, 0.0f, 3.0f, "%.2f deg");
                    ImGui::SliderFloat("Frequency", &s.aimSwayFreq, 0.05f, 2.0f, "%.2f Hz");
                    ImGui::SliderFloat("When the sights come up", &s.aimSwayInitial, 1.0f, 8.0f, "x%.1f");
                    ImGui::SliderFloat("Settle time", &s.aimSwaySettleTime, 0.1f, 4.0f, "%.2f s");
                    ImGui::SliderFloat("Moving", &s.aimSwayMoveMult, 0.0f, 8.0f, "+x%.1f at walking speed");
                    ImGui::SliderFloat("After sprinting", &s.aimSwaySprintPenalty, 0.0f, 8.0f, "+x%.1f");
                    ImGui::SliderFloat("Sprint recovery", &s.aimSwaySprintRecover, 0.1f, 5.0f, "%.2f s");
                    ImGui::Separator();
                    CheckboxInt("Hold breath to steady", s.steadyEnabled);
                    ImGui::BeginDisabled(!s.steadyEnabled);
                    ImGui::Text("Steady key: %s", GetKeyName(s.steadyKey));
                    ImGui::SameLine();
                    if (m_waitingForSteadyKey)
                        ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "press a key... (Esc cancels)");
                    else if (ImGui::Button("Bind##steady"))
                        m_waitingForSteadyKey = true;
                    ImGui::SameLine();
                    if (ImGui::Button("None##steady"))
                        s.steadyKey = 0;
                    CheckboxInt("Swallow the steady key", s.steadyConsumeKey, "The game does not see the key while it is bound here.");
                    {
                        float pct = s.steadyReduce * 100.0f;
                        if (ImGui::SliderFloat("Sway reduction", &pct, 0.0f, 100.0f, "%.0f %%"))
                            s.steadyReduce = pct / 100.0f;
                    }
                    ImGui::SliderFloat("Breath", &s.steadyDuration, 0.5f, 15.0f, "%.1f s");
                    ImGui::SliderFloat("Recovery", &s.steadyRecover, 0.5f, 15.0f, "%.1f s");
                    ImGui::EndDisabled();
                    ImGui::EndDisabled();
                }
                if (ImGui::CollapsingHeader("View drag (GoldenEye)", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    CheckboxInt("Weapon follows your turns on a spring", s.dragEnabled,
                        "Turning drags the weapon along (or behind), then it springs back. Uses this frame's turn rate, so there is no input lag on the camera itself.");
                    ImGui::BeginDisabled(!s.dragEnabled);
                    ImGui::TextDisabled("turn rate %.2f / %.2f rad/s | spring %.2f / %.2f", f.yawRate, f.pitchRate, f.dragX, f.dragY);
                    ImGui::Text("Direction");
                    ImGui::SameLine();
                    ImGui::RadioButton("Leads into the turn (GoldenEye)", &s.dragLead, 1);
                    ImGui::SameLine();
                    ImGui::RadioButton("Lags behind", &s.dragLead, 0);
                    ImGui::SliderFloat("Position amount", &s.dragPos, 0.0f, 0.05f, "%.3f m per rad/s");
                    ImGui::SliderFloat("Rotation amount", &s.dragRot, 0.0f, 10.0f, "%.1f deg per rad/s");
                    ImGui::SliderFloat("Stiffness", &s.dragStiffness, 1.0f, 30.0f, "%.1f");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Higher = reacts and returns faster.");
                    ImGui::SliderFloat("Damping", &s.dragDamping, 0.2f, 1.5f, "%.2f");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("1 = settles without overshoot; lower values wobble a little at the end of a turn.");
                    ImGui::SliderFloat("Max position", &s.dragMaxPos, 0.0f, 0.15f, "%.3f m");
                    ImGui::SliderFloat("Max rotation", &s.dragMaxRot, 0.0f, 20.0f, "%.1f deg");
                    ImGui::SliderFloat("Up / down relative to sideways", &s.dragPitchScale, 0.0f, 1.5f, "x%.2f");
                    ImGui::SliderFloat("While aiming", &s.dragAimScale, 0.0f, 1.0f, "x%.2f");
                    ImGui::EndDisabled();
                }
                ImGui::EndDisabled();
                ImGui::EndTabItem();
            }

            //------------------------------------------------------------------ Weapon
            if (ImGui::BeginTabItem("Weapon"))
            {
                ImGui::BeginDisabled(!s.enabled);
                if (m_currentWeaponClass.empty())
                {
                    ImGui::TextWrapped("Equip a weapon to edit its offsets.");
                }
                else
                {
                    WeaponSettings& w = GetCurrentWeapon();
                    ImGui::Text("Current weapon: %s%s", m_currentWeaponClass.c_str(), w.valid ? "  (customized)" : "  (built-in defaults)");
                    if (w.valid)
                    {
                        ImGui::SameLine();
                        if (ImGui::Button("Reset to built-in"))
                        {
                            if (const WeaponSettings* pB = WeaponSettings::BuiltIn(m_currentWeaponClass.c_str()))
                                w = *pB;
                            else
                            {
                                w = WeaponSettings();
                                w.aim = WeaponSettings::DefaultAim();
                            }
                            m_weaponsDirty = true;
                        }
                    }
                    bool ch = false;
                    bool allow = w.aimAllowed;
                    if (ImGui::Checkbox("Allow aiming with this weapon", &allow))
                    {
                        w.aimAllowed = allow;
                        ch = true;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Turn off for wrench, grenades, etc. The aim key then does nothing while this weapon is equipped.");
                    ch |= SliderCm("Wall pull-back", w.wallPush, 30.0f, "How far this weapon slides back towards the camera when the crosshair is right against a wall\n(Global tab > Wall pull-back sets the distances). Longer weapons want more: pistol ~4 cm, shotgun / GLOO gun ~12 cm.");
                    if (w.wallPush < 0.0f) { w.wallPush = 0.0f; }

                    if (ImGui::CollapsingHeader("Hip offset (not aiming)", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::TextWrapped("Added on top of the global standing/crouch offsets while this weapon is equipped.");
                        ch |= DrawPoseSliders(w.hip, "whip", 30.0f, 45.0f);
                    }
                    if (ImGui::CollapsingHeader("Aim pose (ironsights)", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::BeginDisabled(!w.aimAllowed);
                        ImGui::TextWrapped("Weapon bone relative to the camera while aiming. Centered = Right/Left 0 and no rotation; "
                                           "tune Forward and Up/Down until the sights sit on the crosshair, then small rotations.");
                        ImGui::PushID("waim");
                        ch |= SliderCm("Right / Left", w.aim.posX, 20.0f, "0 = centered on the crosshair.");
                        ch |= SliderCm("Forward", w.aim.posY, 60.0f, "Distance of the weapon bone from the camera.");
                        ch |= SliderCm("Up / Down", w.aim.posZ, 20.0f, "Raise until the sights meet the crosshair.");
                        ch |= SliderDeg("Pitch", w.aim.pitch, 90.0f, "If the weapon bone's forward axis is not the barrel, fix it here.");
                        ch |= SliderDeg("Yaw", w.aim.yaw, 90.0f, nullptr);
                        ch |= SliderDeg("Roll", w.aim.roll, 90.0f, nullptr);
                        if (ImGui::Button("Start from current hip pose"))
                        {
                            w.aim.FromQuatT(m_render.hipRelCam);
                            ch = true;
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Copies the weapon's current (hip) camera-relative transform into the aim pose\n(measured exactly at render time). Do this while NOT aiming, then Center and raise it.");
                        ImGui::SameLine();
                        if (ImGui::Button("Center (x=0, no rotation)"))
                        {
                            w.aim.posX = 0.0f; w.aim.pitch = w.aim.yaw = w.aim.roll = 0.0f;
                            ch = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Reset##aim"))
                        {
                            w.aim = WeaponSettings::DefaultAim();
                            ch = true;
                        }
                        ImGui::Spacing();
                        ImGui::Text("Firing feel while aiming");
                        ch |= ImGui::SliderFloat("Coupling after a shot", &w.fireCoupling, 0.0f, 1.0f, "%.2f");
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Head-bob coupling used right after each shot (decays to the Aim tab's value). Lets the fire kick move the\nweapon relative to the sights instead of the weapon being glued to the camera.");
                        ch |= ImGui::SliderFloat("... for", &w.fireCouplingTime, 0.05f, 1.0f, "%.2f s");
                        ch |= ImGui::SliderFloat("Recoil motion multiplier", &w.aimRecoilScale, 0.0f, 3.0f, "x%.2f");
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Per-weapon multiplier on the game's procedural recoil offset while aiming (times the Aim tab's global one).\nThe shotgun's kick is mostly this.");
                        ch |= ImGui::SliderFloat("Fire animation kick", &w.aimKickScale, 0.0f, 3.0f, "x%.2f");
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("How much of the fire animation's hand motion shows while aiming (the pistol's kick is animated, not procedural).\nMeasured against the pose just before the shot and active for the coupling time above. 0 = still sights.");
                        ImGui::Spacing();
                        ImGui::Text("Bullet spread (pistol / shotgun only)");
                        ch |= ImGui::SliderFloat("Spread while aiming##w", &w.aimSpreadMult, 0.0f, 2.0f, "x%.2f");
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Multiplier on the spread cone while aiming. Pistol: the random bullet cone. Shotgun: the pellet cone and its wobble.\n0 = every shot exactly on the crosshair.");
                        ch |= ImGui::SliderFloat("Spread while not aiming##w", &w.hipSpreadMult, 0.0f, 3.0f, "x%.2f");
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Above 1 makes hip fire less accurate than the stock game, to reward aiming.");
                        ImGui::PopID();
                        ImGui::EndDisabled();
                    }
                    if (ch)
                    {
                        w.valid = true;
                        m_weaponsDirty = true;
                    }

                    if (ImGui::TreeNode("All weapons"))
                    {
                        for (auto& kv : m_weapons)
                        {
                            if (!kv.second.valid)
                                continue;
                            ImGui::Text("%s  aim %s  hip(%.1f,%.1f,%.1f cm)  aim(%.1f,%.1f,%.1f cm)", kv.first.c_str(),
                                kv.second.aimAllowed ? "on " : "off",
                                kv.second.hip.posX * 100, kv.second.hip.posY * 100, kv.second.hip.posZ * 100,
                                kv.second.aim.posX * 100, kv.second.aim.posY * 100, kv.second.aim.posZ * 100);
                        }
                        ImGui::TreePop();
                    }
                }
                ImGui::EndDisabled();
                ImGui::EndTabItem();
            }

            //------------------------------------------------------------------ Aim
            if (ImGui::BeginTabItem("Aim"))
            {
                ImGui::BeginDisabled(!s.enabled);
                CheckboxInt("Enable aiming", s.aimEnabled,
                    "Hold (or toggle) the aim key to bring the weapon up to the sights,\noptionally zooming the camera like ADS in modern shooters.");
                ImGui::BeginDisabled(!s.aimEnabled);

                ImGui::Text("Aim key: %s", GetKeyName(s.aimKey));
                ImGui::SameLine();
                if (m_waitingForAimKey)
                    ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "press a key or mouse button... (Esc cancels)");
                else if (ImGui::Button("Bind"))
                    m_waitingForAimKey = true;
                ImGui::SameLine();
                if (ImGui::Button("RMB"))
                    s.aimKey = (int)eKI_Mouse2;
                CheckboxInt("Toggle instead of hold", s.aimToggle);
                ImGui::SameLine();
                CheckboxInt("Block the game's own action on this key", s.aimConsumeKey,
                    "Swallows the key so whatever the game has bound to it (e.g. RMB) does nothing while this mod is enabled.");

                ImGui::SliderFloat("Transition time", &s.aimTime, 0.0f, 1.0f, "%.2f s");
                ImGui::ProgressBar(SmoothStep01(m_aimBlend), ImVec2(-1, 0), m_isAiming ? "aiming" : "hip");
                CheckboxInt("Ignore crouch offset while aiming", s.aimIgnoresCrouch,
                    "Fades the crouch offset out as the aim pose blends in, so the sights line up in any stance.");
                CheckboxInt("No aiming while reloading", s.aimBlockReload,
                    "Reload animations are authored for the stock pose; locking the weapon to the sights during one distorts the hands.\n"
                    "The sights drop when a reload starts and come back when it ends (the aim key can stay held).");
                CheckboxInt("No aiming while switching weapons", s.aimBlockSwitch,
                    "Same for the holster / draw animations: aiming waits until the new weapon is ready.");
                ImGui::BeginDisabled(!s.aimBlockSwitch);
                ImGui::SliderFloat("Draw time", &s.aimSwitchDelay, 0.0f, 2.0f, "%.2f s");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("How long after a weapon change aiming stays blocked. The game flags the weapon ready before its raise\n"
                        "animation is over, so this covers the rest of it; too short and the sights can capture a hand that is still moving.");
                ImGui::EndDisabled();
                ImGui::TextDisabled("Weapon: %s%s%s%s%s", m_wsReloading ? "reloading " : "", m_wsUnequipping ? "holstering " : "",
                    m_wsDrawing ? "drawing " : "", m_wsReady ? "" : "(busy) ",
                    (!m_wsReloading && !m_wsUnequipping && !m_wsDrawing) ? "idle" : "");

                ImGui::Spacing();
                CheckboxInt("Zoom camera while aiming", s.aimCameraZoom, "Lowers the world FOV while aiming (the weapon gets bigger with it, like real ADS).");
                ImGui::BeginDisabled(!s.aimCameraZoom);
                ImGui::SliderFloat("Camera zoom", &s.aimCameraZoomFactor, 0.2f, 1.5f, "x%.2f FOV");
                ImGui::EndDisabled();
                CheckboxInt("Separate weapon FOV while aiming", s.aimFovEnabled,
                    "Blends the weapon FOV towards this value while aiming (on top of the camera zoom). Lower = bigger weapon.");
                ImGui::BeginDisabled(!s.aimFovEnabled);
                ImGui::SliderFloat("Aim weapon FOV", &s.aimFov, 20.0f, 120.0f, "%.1f deg");
                ImGui::EndDisabled();

                ImGui::Separator();
                ImGui::Text("Ironsight motion");
                {
                    if (s.showAdvanced)
                    {
                        CheckboxInt("Exact render-time placement", s.aimRenderLock,
                            "After the game has built this frame's camera (from the evaluated skeleton) the weapon's attachment\n"
                            "transform is set to exactly camera * aim pose before rendering. No prediction, no correction loop:\n"
                            "lean, landing, crouching, bob cannot move the sights off the crosshair.\n"
                            "Off = skeleton-only lock (the target has to be predicted a frame ahead; small errors show).");
                        ImGui::BeginDisabled(!s.aimRenderLock);
                        ImGui::Indent();
                        CheckboxInt("Move hands with the weapon", s.aimHandsFollow,
                            "Moves the final hand poses (weapon hand and, when it is on the weapon, the support hand) by the same\n"
                            "delta so they stay on the grip. The delta is normally a few millimetres.");
                        ImGui::SameLine();
                        CheckboxInt("incl. support hand", s.aimLeftHandFollow);
                        ImGui::Unindent();
                        ImGui::EndDisabled();
                    }

                    ImGui::SliderFloat("Head-bob coupling", &s.aimBobAmount, -0.5f, 1.0f, "%.2f");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("0 = the weapon is glued to the camera (perfectly still sights).\n"
                                          "Positive = the weapon lags behind the head bob by this fraction (0.05-0.15 feels natural),\n"
                                          "negative = it leads. Derived from the camera's own motion this frame, so it never jitters.");
                    ImGui::SliderFloat("Bob separation", &s.aimBobTau, 0.05f, 1.0f, "%.2f s");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Camera motion faster than this counts as bob; slower motion (crouching, leaning, eye height) does not.");
                    ImGui::SliderFloat("Recoil / bump motion", &s.aimAnimRecoil, 0.0f, 3.0f, "x%.2f");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Multiplier on the game's procedural recoil kick and landing bump while locked (times the per-weapon multiplier in the Weapon tab).");
                    ImGui::SliderFloat("Keep look / strafe sway", &s.aimAnimSway, 0.0f, 1.0f, "%.2f");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Re-applies the game's look/strafe sway on top of the lock. Any amount moves the sights off the crosshair while turning.");

                    ImGui::Separator();
                    ImGui::TextDisabled("Bullet spread multipliers are per weapon (Weapon tab).");
                    if (s.showAdvanced)
                        ImGui::TextDisabled("spread hook calls: %d | shots seen: %d | fire coupling: %.2f s", m_spreadHookCalls, m_shotsSeen, m_fireTimer);
                    ImGui::Separator();
                    const char* sensModes[] = { "Game default (weapon's zoomed multiplier)", "Match camera zoom factor", "Custom multiplier" };
                    ImGui::Combo("ADS look sensitivity", &s.aimSensMode, sensModes, 3);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("The game treats the aim camera zoom like its own weapon zoom and blends the look speed towards the\nweapon's zoomed camera-speed stat, which is very aggressive. Override what it blends towards instead.");
                    if (s.aimSensMode == 2)
                        ImGui::SliderFloat("Sensitivity while aiming", &s.aimSensScale, 0.05f, 3.0f, "x%.2f");
                }

                if (s.showAdvanced && ImGui::TreeNode("Lock debug"))
                {
                    const RenderLockState& R = m_render;
                    if (!m_cameraHookActive)
                        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Camera hook NOT installed - render-time placement unavailable.");
                    ImGui::Text("Camera updates last frame: %d (expect 1) | position was %s", R.callsLastFrame, R.positionWasWorld ? "WORLD (converted)" : "entity-relative");
                    ImGui::Text("Weapon attachment: %s | joint %d '%s' | weapon hand '%s' (%d joints) | support hand '%s' (%d joints, %s, %.1f cm from weapon)",
                        R.attachValid ? "found" : "none", R.attachJoint, R.attachJointName.c_str(),
                        R.rightHandName.c_str(), (int)R.rightSubtree.size(), R.leftHandName.c_str(), (int)R.leftSubtree.size(),
                        R.leftOnWeapon ? "on weapon" : "free", R.leftHandDist * 100);
                    TextQuatT("Camera (model, exact)", R.camModel);
                    TextQuatT("Weapon rel. camera (game)", R.weaponRelCam);
                    TextQuatT("Hip weapon rel. camera", R.hipRelCam);
                    ImGui::Text("Render-time residual (how far the skeleton-side push was off): %.2f cm, %.2f deg | max 1s: %.2f cm  <- cosmetic only, the weapon is placed exactly regardless",
                        R.residualPos * 100, RAD2DEG(R.residualRot), R.residualPosMax1s * 100);
                    ImGui::Text("Head bob (cam space): (%.2f, %.2f, %.2f) cm", R.bobCam.x * 100, R.bobCam.y * 100, R.bobCam.z * 100);
                    ImGui::Separator();
                    ImGui::Text("Skeleton side: active %s | captured %s | skeleton matches player %s | joints: right IK %d, left IK %d, weapon target %d",
                        m_lock.active ? "yes" : "no", m_lock.captured ? "yes" : "no", m_lock.charMatches ? "yes" : "NO",
                        m_lock.rightIkJoint, m_lock.leftIkJoint, m_lock.weaponJoint);
                    TextQuatT("Predicted camera (model)", m_lock.camAbs);
                    TextQuatT("Right IK joint", m_lock.ikAbs);
                    TextQuatT("Weapon bone", m_lock.weaponAbs);
                    TextQuatT("Last override", m_lock.lastTarget);
                    ImGui::Text("IK joint vs pushed target: %.2f cm (one frame of animation velocity is expected) | sensitivity hook calls: %d", m_lock.ikErr * 100, m_sensHookCalls);
                    ImGui::Text("Fire animation deviation: %.2f cm, %.1f deg | kick applied: %.2f cm | pushes not applied by the skeleton: %d | numeric recoveries: %d",
                        m_lock.kickPos * 100, RAD2DEG(m_lock.kickRot), m_lock.kick.t.GetLength() * 100, m_lock.pushesNotApplied, m_nanRecoveries);
                    TextQuatT("Game recoil", m_gameOffsets[2]);
                    TextQuatT("Game bump", m_gameOffsets[3]);
                    ImGui::Separator();
                    ImGui::TextWrapped("Self-test (works while not aiming too): raise the weapon at render time. If the weapon moves, the attachment write works; "
                                       "if the hands move with it, the joint write works.");
                    ImGui::SliderFloat("Test: raise weapon", &s.testOffsetUp, 0.0f, 0.2f, "%.2f m");
                    ImGui::SameLine();
                    CheckboxInt("hands too", s.testHands);
                    ImGui::SameLine();
                    if (ImGui::Button("Reset##test"))
                        s.testOffsetUp = 0.0f;
                    ImGui::TreePop();
                }
                if (s.showAdvanced && ImGui::TreeNode("Input debug"))
                {
                    ImGui::Text("Aim key held/toggled: %s", m_aimKeyHeld ? "yes" : "no");
                    ImGui::Text("Aiming: %s", m_isAiming ? "yes" : "no");
                    ImGui::Text("Camera zoom handle: %d", m_cameraZoomHandle);
                    ImGui::Text("Input listener: %s", m_inputListenerRegistered ? "registered" : "NOT registered");
                    ImGui::TreePop();
                }
                ImGui::EndDisabled();
                ImGui::EndDisabled();
                ImGui::EndTabItem();
            }

            //------------------------------------------------------------------ FOV
            if (ImGui::BeginTabItem("FOV"))
            {
                CheckboxInt("Override weapon FOV", s.fovEnabled);
                ImGui::BeginDisabled(!s.fovEnabled);
                ImGui::SliderFloat("Weapon FOV", &s.fov, 20.0f, 120.0f, "%.1f deg");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Vertical FOV used to render the weapon and arms.\nGame default is 55. Higher = smaller weapon.");
                ImGui::SameLine();
                if (ImGui::Button("55##fov"))
                    s.fov = PreyInternals::STOCK_WEAPON_FOV;
                ImGui::EndDisabled();
                ImGui::TextDisabled("The game's own zoom (and the aim camera zoom) still scales this proportionally.");
                ImGui::Separator();
                DrawReticleControls(s, "fret");
                if (gEnv && gEnv->pConsole)
                {
                    ICVar* pY = gEnv->pConsole->GetCVar("g_reticleYPercentage");
                    ICVar* pS = gEnv->pConsole->GetCVar("hud_reticleSetting");
                    ImGui::TextDisabled("g_reticleYPercentage %.3f | hud_reticleSetting %d", pY ? pY->GetFVal() : -1.0f, pS ? pS->GetIVal() : -1);
                }
                ImGui::EndTabItem();
            }

            //------------------------------------------------------------------ Nudge keys
            if (ImGui::BeginTabItem("Nudge keys"))
            {
                CheckboxInt("Enable nudge keys", s.nudgeKeys,
                    "Adjust the currently active pose without opening this window. The keys are blocked from the game while enabled.");
                const char* layouts[] = { "Numpad", "IJKL" };
                ImGui::Combo("Layout", &s.nudgeLayout, layouts, 2);
                const char* targets[] = { "Global standing / crouch (by stance)", "This weapon's hip offset" };
                ImGui::Combo("Not aiming edits", &s.nudgeTarget, targets, 2);
                const char* activeName = "";
                GetActivePose(&activeName);
                ImGui::Text("Active pose right now: %s", activeName);
                if (s.nudgeLayout == 0)
                    ImGui::TextWrapped(
                        "Numpad 4 / 6 : left / right\n"
                        "Numpad 8 / 2 : up / down\n"
                        "Numpad 7 / 1 : forward / back\n"
                        "Hold Numpad 0 : the same keys rotate instead (4/6 yaw, 8/2 pitch, 7/1 roll)\n"
                        "Hold Numpad . : slow (x0.2)");
                else
                    ImGui::TextWrapped(
                        "J / L : left / right\n"
                        "I / K : up / down\n"
                        "U / O : forward / back\n"
                        "Hold H : the same keys rotate instead (J/L yaw, I/K pitch, U/O roll)\n"
                        "Hold N : slow (x0.2)");
                ImGui::TextWrapped("While aiming (use Toggle aim) the keys edit the aim pose of the current weapon.");
                ImGui::SliderFloat("Position speed", &s.nudgePosSpeed, 0.005f, 0.3f, "%.3f m/s");
                ImGui::SliderFloat("Rotation speed", &s.nudgeRotSpeed, 1.0f, 90.0f, "%.0f deg/s");
                ImGui::EndTabItem();
            }

            //------------------------------------------------------------------ Options
            if (ImGui::BeginTabItem("Options"))
            {
                CheckboxInt("Capture mouse while this window is open", s.guiMouse,
                    "Shows the cursor and stops the camera from turning while you drag sliders.\nThe game keeps running. Close this window or press F1 to give the mouse back.");
                CheckboxInt("Show advanced and experimental features", s.showAdvanced,
                    "Diagnostics, self-tests and the experimental Death tab. Not needed for normal use.");
                if (ImGui::Button("Save weapon settings now"))
                    SaveWeapons();
                ImGui::SameLine();
                ImGui::TextDisabled("%s", GetWeaponsPath().u8string().c_str());
                ImGui::TextWrapped("Console cvars: vm_pos_*, vm_rot_*, vm_crouch_*, vm_aim_*, vm_fov, vm_enabled, vm_nudge_*.");
                ImGui::TextDisabled("Global settings are saved by Chairloader; weapon settings a couple of seconds after a change.");

                if (s.showAdvanced && ImGui::CollapsingHeader("Pipeline diagnostics"))
                {
                    const PipelineDiag& d = m_diag;
                    ImGui::Text("Last frame: %d context update(s), %d weapon-offset update(s), instanceCount %d%s, modifier %s",
                        d.ctxUpdatesLastFrame, d.pwaUpdatesLastFrame, d.lastInstanceCount, d.lastForced ? " (forced alive)" : "",
                        d.lastModifierNull ? "NULL" : "ok");
                    ImGui::TextWrapped("Reproduce the problem (e.g. land from a jump with the pistol), then read the events below. Newest last.");
                    if (ImGui::Button("Clear events"))
                        m_diag.events.clear();
                    ImGui::BeginChild("diagevents", ImVec2(0, 160), true);
                    for (const auto& e : d.events)
                        ImGui::Text("[%8.2f] %s", e.time, e.text.c_str());
                    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                        ImGui::SetScrollHereY(1.0f);
                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    if (!open)
        m_settings.showWindow = 0;
}

//---------------------------------------------------------------------------------
// Exported Functions
//---------------------------------------------------------------------------------
extern "C" DLL_EXPORT IChairloaderMod* ClMod_Initialize()
{
    CRY_ASSERT(!gMod);
    gMod = new ModMain();
    return gMod;
}

extern "C" DLL_EXPORT void ClMod_Shutdown()
{
    CRY_ASSERT(gMod);
    delete gMod;
    gMod = nullptr;
}

// Validate that declarations haven't changed
static_assert(std::is_same_v<decltype(ClMod_Initialize), IChairloaderMod::ProcInitialize>);
static_assert(std::is_same_v<decltype(ClMod_Shutdown), IChairloaderMod::ProcShutdown>);
