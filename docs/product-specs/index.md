# Product Specs Index

Product specs define user-facing behavior and acceptance criteria. Agents reference these to understand what the product should do, not just how the code is structured.

## Core user flow

1. User installs the menu bar app (`./build.sh --install`) and opens it
2. Menu bar app installs Claude Code hooks into `~/.clawd-tank/` and registers them with Claude Code
3. User plugs in the ESP32-C6 device (or enables the simulator transport from the menu bar)
4. Daemon connects over BLE (or TCP for the simulator) and syncs time + timezone
5. User starts a Claude Code session → a new Clawd walks onto the display, animations reflect session activity
6. User dismisses notifications on the device (implicit) or from the menu bar; state is kept in sync
7. User ends or times out the session → Clawd burrows off-screen, remaining sessions reposition

## Specs

| Spec | Status | Description |
|------|--------|-------------|
| (none yet) | — | — |

## Writing specs

Each spec should define:
- **User story:** Who wants what and why
- **Acceptance criteria:** Observable behaviors that must be true
- **Edge cases:** What happens when input is invalid, services fail, or timeouts occur
- **Not in scope:** Explicitly state what this spec does NOT cover

Use [_template.md](_template.md) as a starting point.
