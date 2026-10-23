# BES Voice 0.4

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
The script downloads checksum-verified Zig 0.14.1, Opus 1.6.1 and SpeexDSP 1.2.1.
No .NET, Node.js or browser runtime is needed by the finished client.

The private release can embed its room profile in `BES.exe`, so friends can
open a single file. This profile is not committed. Source builds also support
placing a private `BES-room.ini` beside `BES.exe`:

```ini
[Room]
server=wss://YOUR-SERVICE/voice
key=YOUR_32_CHARACTER_RANDOM_HEX_ROOM_SECRET
```

The embedded format is UTF-8 server URL, newline, then the 32-character room
secret in RCDATA resource 201. Never publish a configured private executable
in a public release. It contains the room capability just like a private
invitation file.

Never commit the real profile or room secret. The same private bundle is
shared with the intended five friends. On first launch, a valid profile is
saved under `%LOCALAPPDATA%\BES\room.bin` using Windows DPAPI, scoped to the
current Windows account. No secret is printed in logs. Ordinary device and
nickname preferences are stored separately in `settings.ini`.

Opening the app starts voice automatically. Its status shows that the
microphone is connected, and the user can pause voice, mute, deafen or use
push-to-talk. Closing the app stops only this user's client. It does not
change Windows autostart settings.

## Cleaner voice and bounded latency

Version 0.4 uses 48 kHz mono, 20 ms (960 sample) frames, Opus AUDIO at a target
64 kbit/s, fullband and complexity 4. It replaces the 16 kHz / 24 kbit/s VOIP
profile, which limited bandwidth and changed voice timbre.

The default Natural profile bypasses denoising and the amplitude gate. On the
first 0.4 launch the older noise/gate settings migrate to Natural; later user
changes persist. Device, nickname and other preferences are preserved.
Optional Light and Strong SpeexDSP modes use -12/-22 dB suppression and a
time-aligned dry path to preserve timbre. AGC remains disabled. A 60 Hz high-pass
is applied only to the wet path. Filtering adds about 20 ms; bypass adds none.
Stationary noise can be reduced; background speech and arbitrary clicks cannot
be guaranteed to disappear. This is not acoustic echo cancellation.

All valid Opus frames, including one-byte DTX packets and zeroed muted/gated
frames, are transmitted. Dropping short packets previously desynchronized the
encoder and playback timelines. Playout now advances the sequence deadline on
concealment and discards late frames even after rebuffering, instead of playing
old sound after its deadline. Voice indicators use decoded audio energy.

Mic capture and network transmission have separate threads. The outgoing voice
queue keeps at most three frames and discards stale audio under backpressure.
The receive buffer keeps at most six encoded frames, adapts its initial target
to two or three frames, and catches up after bursts. Decoding happens on the
playback clock, with short Opus concealment when sequence gaps are detected.
FEC is requested where the encoded mode supports it; CELT may use PLC instead.
Long gaps fade out rather than repeating audio indefinitely. TCP still has
head-of-line blocking, so these bounds do not guarantee end-to-end latency.

A mix limiter provides headroom when four friends speak together. Each peer has
a local volume button cycling through 0/50/75/100/125/150%. Local volume resets
when the slot changes participant. Device selections and noise mode are saved.
Hardware audio uses MMCSS Audio threads and a two-frame output queue. The native
UI uses a cached paint buffer and does not repaint while minimized.

Update all five clients for the new capture quality. The relay wire format and
room are unchanged; an old 16 kHz sender still cannot provide fullband speech.
The higher bitrate and continuous silence clock consume more bandwidth. Render
Free workspace quotas still apply, including during long connected sessions.

## Verification and limits

The local tests cover five slots, room authentication, roster changes,
departure of the first participant, opaque audio forwarding, heartbeat,
and slot reuse. A native synthetic-audio test covers five automatic
connections, one client's departure, and automatic reconnection of the
remaining four after a server restart. DPAPI round-trip and corrupted
profile rejection are checked separately.

A public Render WSS test passed with five native Windows clients sending
synthetic Opus audio. The first left after eight seconds; the remaining
four continued until 24 seconds, with zero decryption errors. See
`public-results.json`.

`voice-quality-results.json` evaluates clean speech through the actual codec,
optional filtering with added noise, and a 12 kHz passband probe.
`timing-results.json` covers continuous speech/silence, dropped packets and a
180 ms ordered transport stall, including monotonic playback deadlines.
The public Opus speech sample itself is not redistributed. The previous 0.3
filter report is archived under `verification/0.3/`. `audio-results.json` checks noise reduction,
bypass, speech-gate hold and mute, mix headroom, sequence-gap concealment,
replay rejection and queue bounds. Run `BES.exe --audio-test report.json` after
building. Run `BES.exe --timing-test report.json` for the timing simulation.
UI demonstration builds can define `BES_UI_DEMO`; they do not connect,
open audio devices or change saved preferences. Their participant rows are fixtures.

Actual microphones and CS frame-time impact must be verified separately.
Voice travels over TCP/WSS, so packet loss
can increase delay. AES-GCM uses a shared room key; no independent security
audit or forward secrecy is claimed. Use headphones (no echo cancellation).

The app code is MIT licensed. See `licenses/` for bundled library notices.
