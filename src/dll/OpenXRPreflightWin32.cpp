#include "OpenXRPreflightWin32.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace preyvr::dll {
namespace {

constexpr std::uintmax_t kMaximumManifestBytes = 1024 * 1024;
constexpr std::uintmax_t kMaximumLoaderBytes = 64 * 1024 * 1024;

bool IsRegularFile(const std::filesystem::path& path)
{
    if (path.empty()) {
        return false;
    }
    std::error_code error;
    const bool regular = std::filesystem::is_regular_file(path, error);
    return regular && !error;
}

std::optional<std::vector<std::uint8_t>> ReadFileBounded(
    const std::filesystem::path& path,
    std::uintmax_t maximumBytes)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > maximumBytes ||
        size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        return std::nullopt;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return std::nullopt;
    }
    return bytes;
}

bool LooksLikeRuntimeManifest(std::span<const std::uint8_t> bytes)
{
    const std::string_view text(
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size());
    const auto first = text.find_first_not_of(" \t\r\n");
    const auto last = text.find_last_not_of(" \t\r\n");
    return first != std::string_view::npos && last != std::string_view::npos &&
        text[first] == '{' && text[last] == '}' &&
        text.find("\"file_format_version\"") != std::string_view::npos &&
        text.find("\"runtime\"") != std::string_view::npos &&
        text.find("\"library_path\"") != std::string_view::npos;
}

template <typename T>
bool ReadStruct(std::span<const std::uint8_t> bytes, std::size_t offset, T& value)
{
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
        return false;
    }
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return true;
}

std::optional<std::size_t> RvaToFileOffset(
    std::span<const std::uint8_t> bytes,
    const IMAGE_OPTIONAL_HEADER64& optional,
    std::span<const IMAGE_SECTION_HEADER> sections,
    DWORD rva,
    std::size_t requiredBytes)
{
    if (rva < optional.SizeOfHeaders) {
        const auto offset = static_cast<std::size_t>(rva);
        if (offset <= bytes.size() && requiredBytes <= bytes.size() - offset) {
            return offset;
        }
        return std::nullopt;
    }

    for (const auto& section : sections) {
        const DWORD mappedSize = std::max(section.Misc.VirtualSize, section.SizeOfRawData);
        const auto sectionStart = static_cast<std::uint64_t>(section.VirtualAddress);
        const auto sectionEnd = sectionStart + mappedSize;
        if (static_cast<std::uint64_t>(rva) < sectionStart ||
            static_cast<std::uint64_t>(rva) >= sectionEnd) {
            continue;
        }
        const auto delta = static_cast<std::size_t>(rva - section.VirtualAddress);
        if (delta > section.SizeOfRawData || requiredBytes > section.SizeOfRawData - delta) {
            return std::nullopt;
        }
        const auto offset = static_cast<std::size_t>(section.PointerToRawData) + delta;
        if (offset <= bytes.size() && requiredBytes <= bytes.size() - offset) {
            return offset;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

struct LoaderPeInspection {
    bool isX64 = false;
    bool isDll = false;
    bool exportsEntryPoint = false;
};

LoaderPeInspection InspectLoaderPe(std::span<const std::uint8_t> bytes)
{
    LoaderPeInspection result;
    IMAGE_DOS_HEADER dos{};
    if (!ReadStruct(bytes, 0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0) {
        return result;
    }

    const auto ntOffset = static_cast<std::size_t>(dos.e_lfanew);
    DWORD signature = 0;
    IMAGE_FILE_HEADER fileHeader{};
    if (!ReadStruct(bytes, ntOffset, signature) || signature != IMAGE_NT_SIGNATURE ||
        !ReadStruct(bytes, ntOffset + sizeof(signature), fileHeader)) {
        return result;
    }
    result.isX64 = fileHeader.Machine == IMAGE_FILE_MACHINE_AMD64;
    result.isDll = (fileHeader.Characteristics & IMAGE_FILE_DLL) != 0;

    const auto optionalOffset = ntOffset + sizeof(signature) + sizeof(fileHeader);
    IMAGE_OPTIONAL_HEADER64 optional{};
    if (fileHeader.SizeOfOptionalHeader < sizeof(optional) ||
        !ReadStruct(bytes, optionalOffset, optional) ||
        optional.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        optional.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT) {
        return result;
    }

    const auto sectionOffset = optionalOffset + fileHeader.SizeOfOptionalHeader;
    std::vector<IMAGE_SECTION_HEADER> sections(fileHeader.NumberOfSections);
    const auto sectionBytes = sections.size() * sizeof(IMAGE_SECTION_HEADER);
    if (sectionOffset > bytes.size() || sectionBytes > bytes.size() - sectionOffset) {
        return result;
    }
    std::memcpy(sections.data(), bytes.data() + sectionOffset, sectionBytes);

    const auto& exportData = optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (exportData.VirtualAddress == 0 || exportData.Size < sizeof(IMAGE_EXPORT_DIRECTORY)) {
        return result;
    }
    const auto exportOffset = RvaToFileOffset(
        bytes, optional, sections, exportData.VirtualAddress, sizeof(IMAGE_EXPORT_DIRECTORY));
    IMAGE_EXPORT_DIRECTORY exports{};
    if (!exportOffset || !ReadStruct(bytes, *exportOffset, exports) ||
        exports.NumberOfNames == 0 || exports.NumberOfNames > 65536) {
        return result;
    }

    const auto namesOffset = RvaToFileOffset(
        bytes,
        optional,
        sections,
        exports.AddressOfNames,
        static_cast<std::size_t>(exports.NumberOfNames) * sizeof(DWORD));
    if (!namesOffset) {
        return result;
    }

    constexpr std::string_view expected = "xrGetInstanceProcAddr";
    for (DWORD index = 0; index < exports.NumberOfNames; ++index) {
        DWORD nameRva = 0;
        if (!ReadStruct(bytes, *namesOffset + index * sizeof(DWORD), nameRva)) {
            return result;
        }
        const auto nameOffset = RvaToFileOffset(bytes, optional, sections, nameRva, 1);
        if (!nameOffset) {
            continue;
        }
        const auto remaining = bytes.subspan(*nameOffset);
        const auto terminator = std::find(remaining.begin(), remaining.end(), 0);
        if (terminator == remaining.end()) {
            continue;
        }
        const std::string_view name(
            reinterpret_cast<const char*>(remaining.data()),
            static_cast<std::size_t>(terminator - remaining.begin()));
        if (name == expected) {
            result.exportsEntryPoint = true;
            break;
        }
    }
    return result;
}

std::optional<std::wstring> ReadActiveRuntimeRegistry(REGSAM view)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Khronos\\OpenXR\\1",
            0,
            KEY_READ | view,
            &key) != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD type = 0;
    DWORD bytes = 0;
    LONG query = RegQueryValueExW(
        key, L"ActiveRuntime", nullptr, &type, nullptr, &bytes);
    if (query != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) ||
        bytes < sizeof(wchar_t)) {
        RegCloseKey(key);
        return std::nullopt;
    }

    std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 1, L'\0');
    query = RegQueryValueExW(
        key,
        L"ActiveRuntime",
        nullptr,
        &type,
        reinterpret_cast<BYTE*>(value.data()),
        &bytes);
    RegCloseKey(key);
    if (query != ERROR_SUCCESS) {
        return std::nullopt;
    }

    std::wstring path(value.data());
    if (type == REG_EXPAND_SZ) {
        const DWORD needed = ExpandEnvironmentStringsW(path.c_str(), nullptr, 0);
        if (needed > 0) {
            std::wstring expanded(static_cast<std::size_t>(needed), L'\0');
            if (ExpandEnvironmentStringsW(path.c_str(), expanded.data(), needed) > 0) {
                if (!expanded.empty() && expanded.back() == L'\0') {
                    expanded.pop_back();
                }
                path = std::move(expanded);
            }
        }
    }
    return path;
}

struct RuntimeManifest {
    std::string source = "none";
    std::filesystem::path path;
};

RuntimeManifest ResolveRuntimeManifest()
{
    std::wstring environment(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(
        L"XR_RUNTIME_JSON",
        environment.data(),
        static_cast<DWORD>(environment.size()));
    if (length > 0 && length < environment.size()) {
        environment.resize(length);
        return {"environment", std::filesystem::path(environment)};
    }
    if (const auto value = ReadActiveRuntimeRegistry(KEY_WOW64_64KEY)) {
        return {"hklm64", std::filesystem::path(*value)};
    }
    if (const auto value = ReadActiveRuntimeRegistry(KEY_WOW64_32KEY)) {
        return {"hklm32", std::filesystem::path(*value)};
    }
    return {};
}

} // namespace

OpenXRFileInspection InspectOpenXRFiles(
    const std::filesystem::path& runtimeManifest,
    const std::filesystem::path& loader)
{
    OpenXRFileInspection result;
    result.runtimeManifestPresent = IsRegularFile(runtimeManifest);
    if (result.runtimeManifestPresent) {
        if (const auto bytes = ReadFileBounded(runtimeManifest, kMaximumManifestBytes)) {
            result.runtimeManifestLooksValid = LooksLikeRuntimeManifest(*bytes);
        }
    }

    result.loaderPresent = IsRegularFile(loader);
    if (result.loaderPresent) {
        if (const auto bytes = ReadFileBounded(loader, kMaximumLoaderBytes)) {
            const auto pe = InspectLoaderPe(*bytes);
            result.loaderIsX64 = pe.isX64;
            result.loaderIsDll = pe.isDll;
            result.loaderExportsEntryPoint = pe.exportsEntryPoint;
        }
    }
    return result;
}

OpenXRPreflightReport CollectOpenXRPreflight(
    const std::filesystem::path& modulePath)
{
    const RuntimeManifest runtime = ResolveRuntimeManifest();
    const bool runtimeRegistered = !runtime.path.empty();

    std::filesystem::path loader;
    if (!modulePath.empty()) {
        loader = modulePath.parent_path() / L"openxr_loader.dll";
        if (!IsRegularFile(loader)) {
            const auto debugLoader = modulePath.parent_path() / L"openxr_loaderd.dll";
            if (IsRegularFile(debugLoader)) {
                loader = debugLoader;
            }
        }
    }

    const auto files = InspectOpenXRFiles(runtime.path, loader);
    return {
        EvaluateOpenXRBootstrap({
            true,
            runtimeRegistered,
            files.runtimeManifestPresent,
            files.runtimeManifestLooksValid,
            files.loaderPresent,
            files.loaderIsX64,
            files.loaderIsDll,
            files.loaderExportsEntryPoint,
        }),
        runtime.source,
        runtime.path,
        loader,
    };
}

} // namespace preyvr::dll
