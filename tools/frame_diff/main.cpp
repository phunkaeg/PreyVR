// Compares two frame dumps and prints the numbers.
//
// This is the instrument for the first render-path write test. "Does modifying
// CSystem::m_ViewCamera change the image?" is answered by capturing a frame
// before and after and running this -- which yields a measurement rather than an
// impression, and can be re-run identically tomorrow.
//
// It prints numbers and does not pass judgement. No acceptance threshold exists
// yet, and inventing one before the separation table has been measured would
// produce something that looks like evidence without being any.

#include "preyvr/FrameDump.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool ReadFile(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        return false;
    }
    const std::streamsize size = stream.tellg();
    if (size <= 0) {
        return false;
    }
    stream.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    return static_cast<bool>(stream.read(reinterpret_cast<char*>(out.data()), size));
}

} // namespace

int main(int argc, char** argv) {
    // Optional trailing knobs on the shift search. They exist because the first
    // live run came back with bestShift pinned at the default limit, and a
    // clamped search reports a *floor* rather than a measurement. The only
    // honest response is to be able to widen the window -- not to read the edge
    // value as though it meant something.
    int maxShift = 320;
    int step = 4;
    int positional = 0;
    const char* paths[2] = {nullptr, nullptr};
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--max-shift" && i + 1 < argc) {
            maxShift = std::atoi(argv[++i]);
        } else if (arg == "--step" && i + 1 < argc) {
            step = std::atoi(argv[++i]);
        } else if (positional < 2) {
            paths[positional++] = argv[i];
        } else {
            std::cerr << "unexpected argument: " << arg << '\n';
            return 2;
        }
    }
    if (positional != 2) {
        std::cerr << "usage: preyvr_frame_diff <a.pvrframe> <b.pvrframe> "
                     "[--max-shift N] [--step N]\n";
        return 2;
    }

    std::vector<std::uint8_t> fileA;
    std::vector<std::uint8_t> fileB;
    if (!ReadFile(paths[0], fileA)) {
        std::printf("preyvr_frame_diff result=unreadable which=a path=%s\n", paths[0]);
        return 3;
    }
    if (!ReadFile(paths[1], fileB)) {
        std::printf("preyvr_frame_diff result=unreadable which=b path=%s\n", paths[1]);
        return 3;
    }

    const auto headerA = preyvr::framedump::DecodeHeader(fileA);
    const auto headerB = preyvr::framedump::DecodeHeader(fileB);
    if (!headerA) {
        std::printf("preyvr_frame_diff result=invalid which=a path=%s\n", argv[1]);
        return 4;
    }
    if (!headerB) {
        std::printf("preyvr_frame_diff result=invalid which=b path=%s\n", argv[2]);
        return 4;
    }

    const std::size_t offset = preyvr::framedump::kHeaderSize;
    const auto difference = preyvr::framedump::Compare(
        *headerA,
        std::span<const std::uint8_t>(fileA).subspan(offset),
        *headerB,
        std::span<const std::uint8_t>(fileB).subspan(offset));

    if (!difference.comparable) {
        // Reported distinctly rather than as a zero difference: "the frames do
        // not match in shape" and "the image did not change" are opposite
        // conclusions and must never be confusable.
        std::printf("preyvr_frame_diff result=incomparable a=%ux%u fmt=%u b=%ux%u fmt=%u\n",
                    headerA->width, headerA->height, headerA->dxgiFormat,
                    headerB->width, headerB->height, headerB->dxgiFormat);
        return 5;
    }

    // A shear and real parallax both make a pair "differ"; only this tells them
    // apart, so it is printed alongside rather than behind a flag.
    const auto scan = preyvr::framedump::FindHorizontalShift(
        *headerA,
        std::span<const std::uint8_t>(fileA).subspan(offset),
        *headerB,
        std::span<const std::uint8_t>(fileB).subspan(offset),
        maxShift,
        step);
    if (scan.comparable) {
        // A best shift sitting exactly on the search boundary means the residual
        // was still falling when the search ran out of room. That is reported as
        // its own verdict rather than as a ratio, because the ratio in that case
        // is a lower bound and reading it as a result is how a clamped search
        // gets mistaken for a finding.
        const bool clamped = scan.bestShift == maxShift || scan.bestShift == -maxShift;
        std::printf("preyvr_frame_diff shiftscan bestShift=%d residualAtZero=%.6f "
                    "residualAtBest=%.6f improvement=%.3fx maxShift=%d verdict=%s\n",
                    scan.bestShift, scan.residualAtZero, scan.residualAtBest,
                    scan.improvementRatio, maxShift,
                    clamped ? "clamped_at_limit_widen_max_shift"
                            : (scan.improvementRatio >= 2.0 ? "uniform_shear_dominates"
                                                            : "no_single_shift_explains_it"));
    }

    std::printf("preyvr_frame_diff result=ok width=%u height=%u pixels=%llu "
                "meanAbsolute=%.9f maxAbsolute=%u changedPixelRatio=%.9f "
                "tagA=%u tagB=%u frameA=%llu frameB=%llu\n",
                headerA->width, headerA->height,
                static_cast<unsigned long long>(difference.pixelsCompared),
                difference.meanAbsolute, difference.maxAbsolute,
                difference.changedPixelRatio,
                headerA->tag, headerB->tag,
                static_cast<unsigned long long>(headerA->frameIndex),
                static_cast<unsigned long long>(headerB->frameIndex));
    return 0;
}
