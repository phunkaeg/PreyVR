#include "preyvr/EngineMap.h"

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>

namespace {

class Handle {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle()
    {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }

private:
    HANDLE value_;
};

class MappedView {
public:
    explicit MappedView(void* value = nullptr) : value_(value) {}
    ~MappedView()
    {
        if (value_ != nullptr) {
            UnmapViewOfFile(value_);
        }
    }
    MappedView(const MappedView&) = delete;
    MappedView& operator=(const MappedView&) = delete;
    const std::uint8_t* bytes() const
    {
        return static_cast<const std::uint8_t*>(value_);
    }

private:
    void* value_;
};

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc == 2 && std::wstring_view(argv[1]) == L"--count") {
        std::cout << preyvr::engine::Landmarks().size() << '\n';
        return 0;
    }
    // `--dump <rva> <count>` prints the bytes at an RVA as a C++ initialiser.
    //
    // Adding a landmark means transcribing a prologue by hand, and a prologue
    // transcribed by hand is a prologue that can be wrong in a way the gate then
    // certifies as correct. This reads them out of the same image the gate
    // validates against, through the same mapping, so the bytes cannot drift
    // between harvesting and checking.
    bool dumpMode = false;
    std::uintptr_t dumpRva = 0;
    std::size_t dumpCount = 0;
    if (argc == 5 && std::wstring_view(argv[1]) == L"--dump") {
        dumpMode = true;
        dumpRva = static_cast<std::uintptr_t>(std::wcstoull(argv[2], nullptr, 0));
        dumpCount = static_cast<std::size_t>(std::wcstoull(argv[3], nullptr, 0));
        if (dumpCount == 0 || dumpCount > 256) {
            std::cerr << "engine_map_probe result=failed reason=dump_count_out_of_range\n";
            return 2;
        }
    } else if (argc != 2) {
        std::cerr << "usage: preyvr_engine_map_probe <PreyDll.dll> | --count"
                     " | --dump <rva> <count> <PreyDll.dll>\n";
        return 2;
    }

    const std::filesystem::path path =
        std::filesystem::absolute(dumpMode ? argv[4] : argv[1]);
    Handle file(CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) {
        std::cerr << "engine_map_probe result=failed reason=open error=" << GetLastError() << '\n';
        return 1;
    }

    Handle mapping(CreateFileMappingW(
        file.get(), nullptr, PAGE_READONLY | SEC_IMAGE_NO_EXECUTE, 0, 0, nullptr));
    if (mapping.get() == nullptr) {
        std::cerr << "engine_map_probe result=failed reason=map error=" << GetLastError() << '\n';
        return 1;
    }
    MappedView view(MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, 0));
    if (view.bytes() == nullptr) {
        std::cerr << "engine_map_probe result=failed reason=view error=" << GetLastError() << '\n';
        return 1;
    }

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(view.bytes());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        std::cerr << "engine_map_probe result=failed reason=dos_header\n";
        return 1;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        view.bytes() + static_cast<std::size_t>(dos->e_lfanew));
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.SizeOfImage == 0) {
        std::cerr << "engine_map_probe result=failed reason=nt_header\n";
        return 1;
    }

    const auto image = std::span<const std::uint8_t>(
        view.bytes(), static_cast<std::size_t>(nt->OptionalHeader.SizeOfImage));

    if (dumpMode) {
        if (dumpRva >= image.size() || dumpCount > image.size() - dumpRva) {
            std::cerr << "engine_map_probe result=failed reason=dump_out_of_range\n";
            return 1;
        }
        std::cout << "engine_map_dump rva=0x" << std::hex << std::uppercase << dumpRva
                  << " count=" << std::dec << dumpCount << '\n';
        for (std::size_t i = 0; i < dumpCount; ++i) {
            if (i % 10 == 0) {
                std::cout << (i == 0 ? "    " : "\n    ");
            }
            std::cout << "0x" << std::hex << std::uppercase
                      << (image[dumpRva + i] < 0x10 ? "0" : "")
                      << static_cast<unsigned>(image[dumpRva + i]) << std::dec << ",";
            if (i + 1 < dumpCount) {
                std::cout << ' ';
            }
        }
        std::cout << '\n';
        return 0;
    }

    const auto results = preyvr::engine::ValidateLandmarks(image);
    for (const auto& result : results) {
        std::cout << "engine_map_landmark id=" << result.landmark->id
                  << " rva=0x" << std::hex << std::uppercase << result.landmark->rva
                  << " status=" << preyvr::engine::ToString(result.status) << std::dec << '\n';
    }

    if (!preyvr::engine::AllLandmarksMatch(results)) {
        std::cerr << "engine_map_probe result=failed landmarks=" << results.size() << '\n';
        return 1;
    }
    std::cout << "engine_map_probe result=pass landmarks=" << results.size() << '\n';
    return 0;
}
