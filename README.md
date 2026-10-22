# BES Voice 0.2

A small native Windows voice app for five friends, backed by an independent
cloud relay. Closing any client leaves the other participants connected.

The Windows client joins its configured room automatically and retries after
a connection failure. There is no daily host button, invitation field, local
tunnel, account signup, browser engine, video or overlay.

## Server

Node.js 22+, one dependency (`ws` pinned by `package-lock.json`).

```powershell
npm ci
npm test
```

Set `BES_ROOM_ID` to the SHA-256 room identifier supplied separately during
deployment. It is the hexadecimal encoding of
`SHA256("BES/room/v1/" + roomSecret)`. Set `PORT` if the hosting platform does
not supply it. Start with `npm start`.

The server binds to `0.0.0.0`, provides `/health` and upgrades `/voice` to
WebSocket. It accepts one private room and at most five joined clients.
Only opaque encrypted audio is relayed. It does not save recordings or log
participant names, room identifiers, audio or keys.

The deployment should use one instance. Put TLS in front of it and connect
clients over `wss://`. No database or persistent disk is required: room
identity is configuration, and reconnecting clients rebuild the roster.

For Render: Node runtime, build `npm ci`, start `npm start`, Free compute
plan, Frankfurt region. Do not enable paid services. Free services sleep
when idle and have bandwidth/usage limits; Free compute does not imply
unlimited free bandwidth. Clients retry while a sleeping instance starts.

## Windows client

Build using `native/Build.ps1` on Windows with CMake and Ninja installed.
The script downloads checksum-verified Zig 0.14.1 and Opus 1.6.1.
No .NET, Node.js or browser runtime is needed by the finished client.

The distributed client bundle contains `BES.exe` and a private `BES-room.ini`:

```ini
[Room]
server=wss://YOUR-SERVICE/voice
key=YOUR_32_CHARACTER_RANDOM_HEX_ROOM_SECRET
```

Never commit the real profile or room secret. The same private bundle is
shared with the intended five friends. On first launch, a valid profile is
saved under `%LOCALAPPDATA%\BES\room.bin` using Windows DPAPI, scoped to the
current Windows account. No secret is printed in logs. Ordinary device and
nickname preferences are stored separately in `settings.ini`.

Opening the app starts voice automatically. Its status shows that the
microphone is connected, and the user can pause voice, mute, deafen or use
push-to-talk. Closing the app stops only this user's client. It does not
change Windows autostart settings.

## Verification and limits

The local tests cover five slots, room authentication, roster changes,
departure of the first participant, opaque audio forwarding, heartbeat,
and slot reuse. A native synthetic-audio test covers five automatic
connections, one client's departure, and automatic reconnection of the
remaining four after a server restart. DPAPI round-trip and corrupted
profile rejection are checked separately.

Actual microphones, CS frame-time impact and the final public deployment
must be verified separately. Voice travels over TCP/WSS, so packet loss
can increase delay. AES-GCM uses a shared room key; no independent security
audit or forward secrecy is claimed. Use headphones (no echo cancellation).

The app code is MIT licensed. See `licenses/` for bundled library notices.
