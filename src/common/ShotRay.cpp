#include "preyvr/ShotRay.h"

#include <algorithm>
#include <cmath>

namespace preyvr::shot {
namespace {
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Scale(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
bool Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}  // namespace

Route RouteFromReturnRva(std::uint32_t rva)
{
    switch (rva) {
    case kGlooQueryReturn: return Route::Gloo;
    case kBallisticQueryReturn: return Route::Ballistic;
    case kRoute13BBQueryReturn: return Route::Route13BB;
    case kHelper16A2QueryReturn: return Route::Helper16A2;
    default: return Route::Unknown;
    }
}

const char* RouteName(Route route)
{
    switch (route) {
    case Route::Gloo: return "gloo";
    case Route::Ballistic: return "ballistic";
    case Route::Route13BB: return "r13bb";
    case Route::Helper16A2: return "helper16a2";
    default: return "unknown";
    }
}

std::uint32_t FiringPositionFlags(Route route)
{
    // GLOO passes 0x8000 (0x169F477); the other routes pass 0 (0x16AB0CB,
    // 0x13BB799). The flags only reach the safety rays' physics filter.
    return route == Route::Gloo ? 0x8000u : 0u;
}

float AngleDegrees(Vec3 a, Vec3 b)
{
    const float la = Length(a), lb = Length(b);
    if (!(la > 1e-6f) || !(lb > 1e-6f)) { return 0; }
    const float c = std::clamp(Dot(a, b) / (la * lb), -1.0f, 1.0f);
    // acos loses precision near 1; the cross product keeps small angles exact.
    const Vec3 cross{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    return std::atan2(Length(cross) / (la * lb), c) * 57.29577951f;
}

float DistanceToLine(Vec3 point, Vec3 origin, Vec3 direction)
{
    const Vec3 d = Sub(point, origin);
    return Length(Sub(d, Scale(direction, Dot(d, direction))));
}

Metrics Measure(Vec3 spawn, Vec3 target, Vec3 rayOrigin, Vec3 rayDirection)
{
    Metrics m{};
    if (!Finite(spawn) || !Finite(target) || !Finite(rayOrigin) || !Finite(rayDirection)) { return m; }
    const float dl = Length(rayDirection);
    if (!(dl > .5f)) { return m; }
    const Vec3 dir = Scale(rayDirection, 1.0f / dl);
    const Vec3 flight = Sub(target, spawn);
    m.distanceMetres = Length(flight);
    m.angleDegrees = AngleDegrees(flight, dir);
    m.spawnOffLineMetres = DistanceToLine(spawn, rayOrigin, dir);
    m.targetOffLineMetres = DistanceToLine(target, rayOrigin, dir);
    m.valid = m.distanceMetres > 1e-4f;
    return m;
}

float ConvergenceErrorMetres(Vec3 spawn, Vec3 rayOrigin, Vec3 direction, float distance)
{
    const Vec3 aimPoint = Add(rayOrigin, Scale(direction, distance));
    const Vec3 flight = Sub(aimPoint, spawn);
    const float length = Length(flight);
    if (!(length > 1e-6f)) { return 0; }
    // Where the projectile is when it has travelled as far along the aim as the
    // aimed point, against where the aimed line from the spawn would put it.
    const float along = Dot(flight, direction);
    if (!(along > 1e-6f)) { return length; }
    const Vec3 atSameDepth = Add(spawn, Scale(flight, distance / along));
    return Length(Sub(atSameDepth, Add(spawn, Scale(direction, distance))));
}

Spawn DecideSpawn(const SpawnInput& in)
{
    if (!in.nativeFallback) { return Spawn::Native; }
    if (!in.haveMuzzle || !in.haveHand) { return Spawn::Blocked; }
    return in.eyeToHandClear && in.handToMuzzleClear ? Spawn::Restored : Spawn::Blocked;
}

const char* SpawnName(Spawn spawn)
{
    switch (spawn) {
    case Spawn::Restored: return "restored";
    case Spawn::Blocked: return "blocked";
    default: return "native";
    }
}

Vec3 SegmentStart(Vec3 from, Vec3 to, float maxLength)
{
    const Vec3 d = Sub(to, from);
    const float length = Length(d);
    if (!(length > maxLength) || !(maxLength > 0)) { return from; }
    return Sub(to, Scale(d, maxLength / length));
}

} // namespace preyvr::shot
