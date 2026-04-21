# Security

The system is local-only: BLE between a nearby Mac and an ESP32, plus a Unix socket between the hook handler and the daemon. No cloud services, no network calls. Security scope is correspondingly narrow.

## Threat model

1. **Nearby BLE attacker.** Anyone in Bluetooth range could in principle discover the device's GATT server and send crafted `add` / `set_status` / `set_sessions` payloads. The device parses, validates, and rejects malformed JSON. The blast radius is display content — no persistent storage is writable from BLE.
2. **Malicious Claude Code hook input.** Hook stdin is user-controlled (it's the tool that generated it) but passes through `clawd-tank-notify` which only forwards well-structured JSON to the daemon. The daemon validates again before sending over BLE.
3. **Local user processes.** The Unix socket `~/.clawd-tank/daemon.sock` is user-owned. Any process running as the same user can write to it. This is accepted: hook forwarding *requires* any Claude Code process to be able to notify the daemon.
4. **Menu bar preferences file.** `~/.clawd-tank/preferences.json` and `sessions.json` are user-owned, read-modify-write. Tampering with them affects the same user's display only.

## Rules

- **No `eval` / dynamic code execution on BLE or hook input.** Ever.
- **No shell interpolation of external data.** The daemon's `SimProcessManager` builds argv as a list, not a string.
- **BLE JSON payloads are size-capped at 256 bytes (MTU).** Longer inputs are rejected at parse time on device and simulator.
- **Notification content is rendered as text only** — never interpreted as LVGL styles, URLs, or commands.
- **Atomic writes for persisted state.** Temp + rename for `preferences.json` and `sessions.json` to prevent truncation on crash.
- **No network calls triggered by BLE input.** If a notification contains a URL it is displayed as text, never fetched.
- **Hook handler is stdlib-only.** No third-party imports — minimizes supply-chain surface for a binary that runs inside Claude Code's hook lifecycle.

## Sensitive files

The system does not read any secrets. The menu bar app should warn (not block) if an operation touches:
- `.env`, `.env.*`
- Files matching `*secret*`, `*credential*`, `*token*`

None of these are currently read by Clawd Tank components. Keep it that way.
