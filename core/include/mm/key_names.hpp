#pragma once
// Human key names <-> virtual-key codes for the CLI and (later) profiles.
// Values are the Win32 VK_* constants, spelled out so this stays portable.
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mm {

namespace detail {
struct NamedKey { std::string_view name; uint16_t vk; };
inline constexpr NamedKey kNamedKeys[] = {
    {"LBUTTON", 0x01}, {"RBUTTON", 0x02}, {"MBUTTON", 0x04}, {"XBUTTON1", 0x05}, {"XBUTTON2", 0x06},
    {"BACKSPACE", 0x08}, {"TAB", 0x09}, {"ENTER", 0x0D}, {"RETURN", 0x0D},
    {"SHIFT", 0x10}, {"CTRL", 0x11}, {"CONTROL", 0x11}, {"ALT", 0x12}, {"MENU", 0x12},
    {"PAUSE", 0x13}, {"CAPSLOCK", 0x14}, {"ESC", 0x1B}, {"ESCAPE", 0x1B}, {"SPACE", 0x20},
    {"PAGEUP", 0x21}, {"PAGEDOWN", 0x22}, {"END", 0x23}, {"HOME", 0x24},
    {"LEFT", 0x25}, {"UP", 0x26}, {"RIGHT", 0x27}, {"DOWN", 0x28},
    {"INSERT", 0x2D}, {"DELETE", 0x2E},
    {"LWIN", 0x5B}, {"RWIN", 0x5C},
    {"NUMPAD0", 0x60}, {"NUMPAD1", 0x61}, {"NUMPAD2", 0x62}, {"NUMPAD3", 0x63}, {"NUMPAD4", 0x64},
    {"NUMPAD5", 0x65}, {"NUMPAD6", 0x66}, {"NUMPAD7", 0x67}, {"NUMPAD8", 0x68}, {"NUMPAD9", 0x69},
    {"MULTIPLY", 0x6A}, {"ADD", 0x6B}, {"SUBTRACT", 0x6D}, {"DECIMAL", 0x6E}, {"DIVIDE", 0x6F},
    {"NUMLOCK", 0x90}, {"SCROLLLOCK", 0x91},
    {"LSHIFT", 0xA0}, {"RSHIFT", 0xA1}, {"LCTRL", 0xA2}, {"RCTRL", 0xA3}, {"LALT", 0xA4}, {"RALT", 0xA5},
    {"SEMICOLON", 0xBA}, {"EQUALS", 0xBB}, {"COMMA", 0xBC}, {"MINUS", 0xBD}, {"PERIOD", 0xBE},
    {"SLASH", 0xBF}, {"GRAVE", 0xC0}, {"TILDE", 0xC0}, {"LBRACKET", 0xDB}, {"BACKSLASH", 0xDC},
    {"RBRACKET", 0xDD}, {"QUOTE", 0xDE},
};

inline std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
}  // namespace detail

// Accepts: single letters/digits ("W", "3"), names ("SPACE", "F12", "CAPSLOCK"),
// and hex ("0x57"). Case-insensitive.
inline std::optional<uint16_t> parse_key(std::string_view text) {
    if (text.empty()) return std::nullopt;
    const std::string s = detail::upper(text);

    if (s.size() == 1) {
        const char c = s[0];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return static_cast<uint16_t>(c);
        return std::nullopt;
    }
    if (s.size() > 2 && s[0] == '0' && s[1] == 'X') {
        uint32_t v = 0;
        for (std::size_t i = 2; i < s.size(); ++i) {
            const char c = s[i];
            uint32_t d;
            if (c >= '0' && c <= '9') d = static_cast<uint32_t>(c - '0');
            else if (c >= 'A' && c <= 'F') d = static_cast<uint32_t>(c - 'A' + 10);
            else return std::nullopt;
            v = v * 16 + d;
            if (v > 0xFF) return std::nullopt;
        }
        return static_cast<uint16_t>(v);
    }
    if (s[0] == 'F' && s.size() <= 3) {
        int n = 0;
        for (std::size_t i = 1; i < s.size(); ++i) {
            if (s[i] < '0' || s[i] > '9') { n = -1; break; }
            n = n * 10 + (s[i] - '0');
        }
        if (n >= 1 && n <= 24) return static_cast<uint16_t>(0x70 + n - 1);
    }
    for (const auto& k : detail::kNamedKeys) if (k.name == s) return k.vk;
    return std::nullopt;
}

inline std::string key_name(uint16_t vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, static_cast<char>(vk));
    if (vk >= 0x70 && vk <= 0x87) return "F" + std::to_string(vk - 0x70 + 1);
    for (const auto& k : detail::kNamedKeys) if (k.vk == vk) return std::string(k.name);
    char buf[8];
    std::snprintf(buf, sizeof buf, "0x%02X", vk);
    return buf;
}

}  // namespace mm
