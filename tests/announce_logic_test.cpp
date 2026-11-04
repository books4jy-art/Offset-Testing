// Host unit test for the Developers Announcement pure logic (src/announce_logic.h). Build and run:
//   c++ -std=c++20 -I src tests/announce_logic_test.cpp -o /tmp/announce_test && /tmp/announce_test
// This is the only part of the announcement feature that runs without the game; the native game-interaction code is
// compiled by CI (Android NDK) and can only run against the game and the private server.
#include "announce_logic.h"

#include <cassert>
#include <cstdio>

using namespace lab::ann;

static int gChecks = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        gChecks++;                                                             \
        if (!(cond)) {                                                         \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);                \
            return 1;                                                          \
        }                                                                      \
    } while (0)

int main() {
    // ChannelShortName
    CHECK(ChannelShortName("kChatChannel_AudienceBroadcast") == "AudienceBroadcast");
    CHECK(ChannelShortName("kChatChannel_Loopback") == "Loopback");
    CHECK(ChannelShortName("Loopback") == "Loopback");  // no prefix: unchanged
    CHECK(ChannelShortName("kChatChannel_") == "kChatChannel_");  // nothing after prefix: unchanged

    // IsChannelOption
    CHECK(IsChannelOption("kChatChannel_Public"));
    CHECK(!IsChannelOption("kAudienceColorType_Green"));
    CHECK(!IsChannelOption("kChatChannel_"));

    // Trim
    CHECK(Trim("  hello  ") == "hello");
    CHECK(Trim("\t\nx\r\n") == "x");
    CHECK(Trim("   ") == "");

    // MessageProblem
    CHECK(MessageProblem("") != "");
    CHECK(MessageProblem("    ") != "");       // whitespace only
    CHECK(MessageProblem("Go to Eden now") == "");
    CHECK(MessageProblem(std::string(kMaxAnnounceChars, 'a') ) == "");
    CHECK(MessageProblem(std::string(kMaxAnnounceChars + 1, 'a')) != "");

    // PickChannelField / PickMessageField
    std::vector<Setting> settings = {
        {"recipient", false, false, ""},
        {"channel", false, true, "kChatChannel_Loopback"},   // the chat-channel enum
        {"relationshipType", false, true, "kRelationship_None"},  // a different enum: must not win
        {"message", true, false, ""},                        // the message text
        {"note", true, false, ""},
    };
    CHECK(PickChannelField(settings) == "channel");
    CHECK(PickMessageField(settings) == "message");

    // message field falls back to the first text setting when none is named message/text/chat
    std::vector<Setting> noNamed = {{"payload", true, false, ""}, {"extra", true, false, ""}};
    CHECK(PickMessageField(noNamed) == "payload");

    // nothing found
    std::vector<Setting> none = {{"x", false, false, ""}};
    CHECK(PickChannelField(none) == "");
    CHECK(PickMessageField(none) == "");

    // StatusText: honest about loopback vs server, and never claims cross-instance delivery
    std::string sent = StatusText("AudienceBroadcast", 0, false);
    CHECK(sent.find("server's decision") != std::string::npos);
    CHECK(sent.find("does not forge") != std::string::npos);
    std::string loop = StatusText("Loopback", 0, true);
    CHECK(loop.find("never leaves your device") != std::string::npos);
    CHECK(StatusText("AudienceBroadcast", -1, false).find("enter the world") != std::string::npos);
    CHECK(StatusText("AudienceBroadcast", 7, false).find("refused") != std::string::npos);

    std::printf("All %d checks passed.\n", gChecks);
    return 0;
}
