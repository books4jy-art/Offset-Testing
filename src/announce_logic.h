// Developers Announcement: pure helper logic, free of ImGui/game dependencies so it can be unit-tested on the host
// (tests/announce_logic_test.cpp) and reused by the mod (features.cpp). Nothing here touches the game; it only
// shapes strings and picks which of an event's settings carry the channel and the message.
#pragma once

#include <string>
#include <vector>

namespace lab {
namespace ann {

constexpr size_t kMaxAnnounceChars = 180;  // chat messages are short; keep well under the game's own limit
constexpr char kChannelPrefix[] = "kChatChannel_";
constexpr char kLoopbackOption[] = "kChatChannel_Loopback";
constexpr char kBroadcastOption[] = "kChatChannel_AudienceBroadcast";

// "kChatChannel_AudienceBroadcast" -> "AudienceBroadcast"; anything without the prefix is returned unchanged.
inline std::string ChannelShortName(const std::string& option) {
    const size_t n = sizeof(kChannelPrefix) - 1;
    if (option.size() > n && option.compare(0, n, kChannelPrefix) == 0) return option.substr(n);
    return option;
}

inline bool IsChannelOption(const std::string& option) {
    const size_t n = sizeof(kChannelPrefix) - 1;
    return option.size() > n && option.compare(0, n, kChannelPrefix) == 0;
}

// Trim ASCII whitespace from both ends.
inline std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; };
    while (a < b && ws(s[a])) a++;
    while (b > a && ws(s[b - 1])) b--;
    return s.substr(a, b - a);
}

// Why a message can't be sent ("" = it's fine). Checks emptiness and length only; the game/server decide the rest.
inline std::string MessageProblem(const std::string& message) {
    std::string m = Trim(message);
    if (m.empty()) return "Type a message first.";
    if (m.size() > kMaxAnnounceChars)
        return "Message is too long (" + std::to_string(m.size()) + " of " + std::to_string(kMaxAnnounceChars) +
               " characters).";
    return "";
}

// One of an event's settings, reduced to what the picker needs.
struct Setting {
    std::string name;
    bool isText = false;
    bool isEnum = false;
    std::string firstEnumOption;  // first option of an enum setting (used to spot the chat-channel enum)
};

// The chat-channel enum setting is the enum whose options are kChatChannel_*. Returns its name, or "" if none.
inline std::string PickChannelField(const std::vector<Setting>& settings) {
    for (const auto& s : settings)
        if (s.isEnum && IsChannelOption(s.firstEnumOption)) return s.name;
    return "";
}

// The message field is a text setting; prefer one whose name mentions message/text/chat, else the first text setting.
inline std::string PickMessageField(const std::vector<Setting>& settings) {
    auto mentions = [](const std::string& name, const char* w) {
        std::string n;
        for (char c : name) n += (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        return n.find(w) != std::string::npos;
    };
    for (const auto& s : settings)
        if (s.isText && (mentions(s.name, "message") || mentions(s.name, "text") || mentions(s.name, "chat")))
            return s.name;
    for (const auto& s : settings)
        if (s.isText) return s.name;
    return "";
}

// The honest status line after an attempt. rc is the mod's RunLua code (0 queued/sent, -1 not ready, -3 busy, other
// = game refused). loopback = the local-only preview channel.
inline std::string StatusText(const std::string& channelShort, int rc, bool loopback) {
    if (rc == -1) return "The game isn't ready yet (enter the world first).";
    if (rc == -3) return "The game isn't running frames right now; try again.";
    if (rc != 0) return "The game refused the announcement (code " + std::to_string(rc) +
                        "): the event may not exist in this build, or a setting was rejected.";
    if (loopback)
        return "Shown locally only (Loopback channel): this never leaves your device. Use it to preview wording.";
    return "Sent to the server on channel " + channelShort +
           ". Whether it reaches other players is the server's decision: it delivers only if your account is an "
           "authorized broadcaster with a valid reservation. This mod does not forge that authorization.";
}

}  // namespace ann
}  // namespace lab
