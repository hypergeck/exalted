#pragma once
// Single point of inclusion for <windows.h> with the project's SDK settings.
// Everything under mm/win/ is Windows-only; nothing under mm/ (without win/)
// may include this header.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00  // Windows 10+: EcoQoS, per-monitor DPI v2
#endif
#include <windows.h>

#include <string>
#include <string_view>

namespace mm::win {

// Owning HANDLE wrapper (CloseHandle on destruction). Move-only.
class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE h) noexcept : h_(h) {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(UniqueHandle&& o) noexcept : h_(o.release()) {}
    UniqueHandle& operator=(UniqueHandle&& o) noexcept {
        if (this != &o) { reset(); h_ = o.release(); }
        return *this;
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    bool valid() const noexcept { return h_ != nullptr && h_ != INVALID_HANDLE_VALUE; }
    explicit operator bool() const noexcept { return valid(); }
    HANDLE get() const noexcept { return h_; }
    HANDLE release() noexcept { HANDLE h = h_; h_ = nullptr; return h; }
    void reset(HANDLE h = nullptr) noexcept {
        if (valid()) CloseHandle(h_);
        h_ = h;
    }

private:
    HANDLE h_ = nullptr;
};

inline uint64_t qpc_now() noexcept {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return static_cast<uint64_t>(li.QuadPart);
}

inline uint64_t qpc_frequency() noexcept {
    static const uint64_t f = [] {
        LARGE_INTEGER li;
        QueryPerformanceFrequency(&li);
        return static_cast<uint64_t>(li.QuadPart);
    }();
    return f;
}

inline uint32_t qpc_to_us(uint64_t ticks) noexcept {
    return static_cast<uint32_t>(ticks * 1000000ull / qpc_frequency());
}

inline std::string narrow(std::wstring_view w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

inline std::wstring widen(std::string_view s, UINT codepage = CP_UTF8) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(codepage, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(codepage, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

inline std::string last_error_message(DWORD code = GetLastError()) {
    wchar_t* buf = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, code, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::string msg = n ? narrow(std::wstring_view(buf, n)) : "unknown error";
    if (buf) LocalFree(buf);
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r' || msg.back() == ' ')) msg.pop_back();
    return msg + " (" + std::to_string(code) + ")";
}

}  // namespace mm::win
