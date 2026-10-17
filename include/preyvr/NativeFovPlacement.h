#pragma once
#include <optional>

namespace preyvr::xr {

// Tangents of a frustum: left and down negative, right and up positive.
struct Tangents {
    float left = 0, right = 0, up = 0, down = 0;
};

// Where one eye's image goes inside a projection view that declares the runtime's NATIVE field.
//
// **Why.** A runtime whose view configuration reports `fovMutable = false` -- Meta's PC runtime, measured on the
// Quest 3 and documented by other OpenXR apps -- lays every projection view across the eye's own field whatever the
// layer declares. Prey renders a symmetric frustum and the Quest's eye fields are mirrored-asymmetric (about -52/+42
// degrees for the left eye), so an image declared under Prey's frustum lands about 10 degrees off in each eye, in
// opposite directions: a double image at every distance. Declaring the native field is valid on every runtime; this
// says where Prey's pixels sit inside it.
//
// Both frusta are linear in tangent, so the mapping is a pure offset at the render's own pixel density: the image is
// copied, never resampled. `rect` is the projection view's imageRect (the native field at that density, from the
// swapchain's top-left); the copy moves `copyW` x `copyH` pixels from (`srcX`, `srcY`) of the image to (`dstX`,
// `dstY`) of the swapchain image. Parts of the native field the render does not reach are not covered (`complete` is
// false) and must be cleared by the caller.
struct Placement {
    int rectW = 0, rectH = 0;
    int srcX = 0, srcY = 0, dstX = 0, dstY = 0, copyW = 0, copyH = 0;
    bool complete = false;
};

// Empty when the frusta are degenerate, when they do not overlap, or when the native field at the render's density
// does not fit the swapchain image (`swapW` x `swapH`).
std::optional<Placement> PlaceInNativeFov(const Tangents& rendered, int imageW, int imageH, const Tangents& native,
                                          int swapW, int swapH);

}  // namespace preyvr::xr
