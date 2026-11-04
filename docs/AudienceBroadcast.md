# AudienceBroadcast — investigation findings

Investigation of the `AudienceBroadcast` functionality in Sky's game library
(`libBootloader.so`, Sky 0.35.2), and an assessment of whether a single authorized
announcer can distribute instructions across multiple instances for a private Eden Run.

All offsets below are **file offsets into `libBootloader.so`** (49 MB, ARM aarch64,
stripped). They were read from the binary's string/data tables, not from a running
game, so they identify strings and symbols, not live code addresses. Nothing here was
assumed from a name alone — each claim is tied to a string or symbol found in the image.

## 1. What `AudienceBroadcast` actually is

`AudienceBroadcast` is **not a function and not a standalone feature**. It is one value
of the game's **chat-channel enumeration**:

```
0x494e47  kChatChannel_AudienceBroadcast
0x494e66  AudienceBroadcast            (the short enum-name form, interned next to it)
```

The full chat-channel enum found in the image:

```
kChatChannel_Loopback      kChatChannel_Local     kChatChannel_Public
kChatChannel_Private       kChatChannel_Table     kChatChannel_AudienceBroadcast
kChatChannel_Chat1..Chat8  kChatChannel_Bench1..Bench12
kChatChannel_Invalid
```

So `AudienceBroadcast` is the **channel on which an authorized broadcaster's chat/signal
messages are delivered to a whole audience**. It sits in the same enum as ordinary chat
channels (`Local`, `Public`, `Private`, table/bench seat channels) and the purely local
`Loopback` channel.

## 2. Where/how it is used — the Audience/Broadcast system

`AudienceBroadcast` is part of the game's official **Audience / Broadcast** subsystem —
the machinery behind Sky's concerts and live events (AURORA, SkyFest, "Days of…" events)
and **gameroom events including Eden Run**. Evidence from the image:

Reservation + event types (the event an audience gathers for):
```
0x4b310b  kAudienceReservationEventType_EdenRun        <-- Eden Run is a first-class type
0x459beb  GameroomEdenRun
0x4d2f13  kTrustActivityId_GameroomEdenRun
0x476966  AudienceReservationBarn
0x4babe6  AccountAudienceReservationRequest
0x47697e  AccountAudienceCodeReservationRequestEvent   (join-by-code reservations)
0x484279  requiresReservation
          kAudienceReservationEventType_{Aurora,Museum,Cinema_TE_EP,Interview_Stage,
                                         MaskRitual_05,DaysOfMischief,DaysOfRainbow,…}
```

Broadcaster role + control RPCs (what the announcer does):
```
0x4f85dc  AccountHasSuperLikeBroadcasterRequest        (server check: am I a broadcaster?)
0x491f9b  AccountSuperLikeBroadcasterRequest
0x4edb7c  AccountHasSuperLikeBroadcasterRequestEvent
0x549db5  AccountSuperLikeBroadcasterRequestEvent
0x4f0e2d  StartBroadcasterAttractRepelRpc              (broadcaster-only RPCs)
0x52dce6  StopBroadcasterEventByMetaNameRpc
0x4ad5d8  system_broadcaster_grow_confirm
0x46e8e0  UiMenuBroadcastInterview
0x48502f  UiBroadcast
0x47083a  OnBroadcast
0x497f8f  onLocalTgcBroadcastingToggle
0x4a2e1e  onLocalTgcNotBroadcastingToggle              (TGC = thatgamecompany host)
```

Stage state, confirm dialogs, and the broadcast chat wiring:
```
0x45811f  "This will get triggered upon state change when a non local player exits the broadcast stage."
0x4769d5  "This will get triggered upon state change when a non local player enters the actual broadcast stage."
0x46246f  broadcast_confirm_begin_title
0x46248d  broadcast_confirm_finish_title
0x46c8d4  broadcast_confirm_cancel_title
0x49e168  chat_message_local_broadcast
0x494049  AudienceEmotesAtlas
0x46240c  Data/Resources/AudienceEventDefs.json        (audience-event definitions)
```

Network messages (how it reaches other instances — server-driven):
```
0x48a349  [kNetMsg_Audience]              0x480607  [kNetMsg_AudienceRoomIds]
0x480621  [kNetMsg_AudienceFeatureFlags]  0x4624b9  [kNetMsg_AudienceReliableData]
0x4abaf3  kNetMsg_AudienceChatDownUnreliable
0x494049  [kNetMsg_SocialBroadcastDown]   0x54eb8b  kNetMsg_ReservationResponse
```

## 3. How a message is actually sent

The client send path is:

```
SendChatMessageRequestEvent      (0x487fee) — a client event (eventBarn:AddEventByMetaName)
      │   carries at least: a channel (chat-channel enum), message text, recipient/roomId
      ▼
AccountSendChatMessageRequest    (0x591a81) — an account/service-server request
      ▼
server validates + routes; audience fan-out returns via kNetMsg_Audience* /
kNetMsg_SocialBroadcastDown to every instance's clients
```

Relevant field strings seen in the image: `channel` (0x4556e5), `channelId`
(0x56b286), `recipient` (0x45dfc6), `roomId` (0x462aab), `localizeMessage` (0x4e626f).
The exact setter names are read from the game's own type table at runtime (the mod does
not hard-code them); see §6.

`kChatChannel_Loopback` is the one channel that is **purely local** — a message on it is
shown on your own device and never leaves it. It is the only channel that can be
exercised end-to-end without a server.

## 4. Classes, functions, interfaces, dependencies

| Piece | Role |
|---|---|
| `kChatChannel_AudienceBroadcast` | the chat channel for audience-wide messages |
| `SendChatMessageRequestEvent` | client event that submits a chat/broadcast message |
| `AccountSendChatMessageRequest` | server request the event turns into |
| `AudienceReservationBarn`, `AccountAudienceReservationRequest`, `…CodeReservationRequestEvent` | reserve an audience/seat (server-granted); `kAudienceReservationEventType_EdenRun` is a valid type |
| `AccountHasSuperLikeBroadcasterRequest` / `AccountSuperLikeBroadcasterRequest` | the server's broadcaster-authorization check/grant |
| `StartBroadcasterAttractRepelRpc`, `StopBroadcasterEventByMetaNameRpc` | broadcaster-only control RPCs |
| `kNetMsg_Audience*`, `kNetMsg_SocialBroadcastDown`, `kNetMsg_ReservationResponse` | server→client messages that fan a broadcast out across instances |
| `AudienceSettings` (already in the mod, `features.cpp`) | **local** audience-display node (reaction menu, avatar replacement). Unrelated to sending a broadcast — do not confuse the two. |

## 5. Who it talks to, and the authorization requirements

- **It talks to a wider audience, via the server.** The client does not fan a message out
  to other instances itself; it sends one request, and the **server** delivers it to every
  instance's clients through the `kNetMsg_Audience*` / `…SocialBroadcastDown` messages.
- **It is gated by two server-side authorizations:**
  1. **Broadcaster role** — `AccountHasSuperLikeBroadcasterRequest`. The *account server*
     decides whether an account may broadcast. The client only asks.
  2. **Audience reservation** — `AccountAudienceReservationRequest` /
     `…CodeReservationRequestEvent`, for a reservation type such as
     `kAudienceReservationEventType_EdenRun`. `requiresReservation` controls whether the
     audience node enforces it.
- There is **no client-side flag that grants broadcaster status.** The authorization lives
  on the server; the binary only contains the *request* that asks the server.

## 6. Can one authorized announcer broadcast across instances? (assessment)

**Yes — the game has exactly this capability, and it is entirely server-authorized.**

- The feature the Eden Run runner wants is real: an authorized broadcaster, holding an
  audience reservation, sends on `kChatChannel_AudienceBroadcast`, and the server delivers
  it to every participant across all 8-player instances. `kAudienceReservationEventType_EdenRun`
  shows Eden Run is a supported audience-event type.
- **What a client (and therefore this mod) can do:** compose a message and fire the game's
  real `SendChatMessageRequestEvent` on a chosen channel. This is the normal client path —
  no exploit, no memory patch.
- **What a client cannot do:** authorize itself. Whether the request is delivered depends
  on the server confirming (a) the account is a broadcaster and (b) a valid reservation
  exists. On the official servers a normal account is neither, so the request is simply
  rejected — correct behavior. The mod must not, and this implementation does not, patch or
  forge the broadcaster check, the reservation, or any credential; doing so would be both
  futile (the server re-decides) and abuse.
- **On the temporary private server:** if that server's backend is configured to recognize
  the announcer's account as a broadcaster and honor the Eden Run reservation, then firing
  the event **will** distribute cross-instance — because fan-out is the server's job. That
  configuration lives in the private-server backend, which is **not available to this task**,
  so cross-instance delivery **cannot be verified here**. It can only be confirmed against
  the running private server.

## 7. The Developers Announcement box vs. the MOTD system

- The repo did **not** previously contain a "Developers" tab or an announcement box — the
  mod's tabs were Game functions, Lua, Memory, Native, Log, Features, Other offsets,
  Recorder. "Make the existing box functional" therefore meant **building** the control and
  wiring it to the game's real send path. That control is added in this change as a
  **Developers** sub-tab under Features (see §8).
- The **MOTD** strings (`/account/get_motd`, `AccountServerMOTDRequest`, `MOTDMessage`,
  `MOTDPanel`, `ShowMOTD`, `MOTDMenu`, `SkyFest_DeveloperQA`, `personality_fun_qa_announce`)
  are a **separate** system: a server-pushed "Message of the Day" panel shown at login. It is
  **not** `AudienceBroadcast` and is **also** server-originated — a client cannot author a
  MOTD. It is not used for the announcement box, by design.

## 8. What was implemented, and what still needs the private server

Implemented (in `features.cpp`, a new **Developers** sub-tab):

- An announcement composer: message text + a channel picker whose options are read from the
  game's own chat-channel enum at runtime (no hard-coded field names or offsets).
- **Preview locally (Loopback):** fires `SendChatMessageRequestEvent` on
  `kChatChannel_Loopback` — a purely local, client-side display, shown in blue. This is the
  part that is fully exercisable without a server.
- **Send announcement:** fires the same event on the chosen channel (including
  `AudienceBroadcast`), via the mod's existing event machinery, and reports the result.
- Error handling and delivery-status reporting: not-in-world, event-absent-in-this-build,
  queued/sent vs. server verdict, and an explicit statement that cross-instance delivery is
  the server's decision and that the mod does **not** forge broadcaster authorization.

Requires the temporary private server to verify (cannot be tested here):

- Actual cross-instance delivery on `kChatChannel_AudienceBroadcast`.
- The server recognizing the announcer's account as a broadcaster
  (`AccountHasSuperLikeBroadcasterRequest` returning true).
- A valid `kAudienceReservationEventType_EdenRun` reservation being honored.

Tested here:

- The pure message/channel/status helper logic, via a host-compiled unit test
  (`tests/announce_logic_test.cpp`). The native game-interaction code is compiled by CI
  (Android NDK r23c); it cannot run without the game and the private server.
