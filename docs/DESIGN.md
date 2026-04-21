# Output & UI Conventions

Conventions for user-facing output across the device display, simulator, and menu bar app.

## Output principles

1. **Pixel-first.** The device is 320x172. Every layout decision is made against that grid — no "looks fine in the simulator" shortcuts that assume more pixels.
2. **Animation communicates state.** Clawd's animation (IDLE, TYPING, JUGGLING, BUILDING, CONFUSED, DIZZY, SWEEPING, SLEEPING, etc.) is the primary indicator of session activity. Text is secondary.
3. **One slot per session.** Up to 4 sessions are visible at once, each with its own sprite. Additional sessions are shown as a "+N" overflow badge rather than cycling.
4. **Notifications are cards, not toasts.** Up to 8 are held in a ring buffer. The featured card auto-rotates; the compact list surfaces the rest. Nothing is auto-dismissed — the host explicitly dismisses or clears.
5. **LED augments, never replaces.** The onboard WS2812B flashes on new notifications and triple-flashes red on `StopFailure`. The LED is a supplement to the screen, not a substitute.
6. **Menu bar app is terse.** Emoji dots for connection status, compact submenus. No modal dialogs during normal operation.

## Display format

Device layout:

```
┌──────────────────────────────────────────────────────────────┐  172px
│ sky + stars                                                  │
│                                                              │
│     ┌───┐     ┌───┐     ┌───┐     ┌───┐                      │
│     │🦀 │     │🦀 │     │🦀 │     │🦀 │   +2                  │
│     └───┘     └───┘     └───┘     └───┘                      │
│ ──────────────────── grass ──────────────────                │
│                                           └─ notification ──┐│
│                                            ┌─────────────┐  ││
│                                            │  featured   │  ││
│                                            │  card       │  ││
│                                            └─────────────┘  ││
│                                            • compact list   ││
└──────────────────────────────────────────────────────────────┘
 320px
```

Scene width transitions: **107 px** when notifications are present (leaving 213 px for cards), **320 px** when idle.

Color palette (card accents): 8 colors cycling by notification type.

Transparency key color: `0x18C5` (RGB565).

## Future considerations

- HUD overlay currently renders subagent count + session overflow badge. Any further HUD elements (e.g., battery, clock) must stay within the sky region above Clawd.
- Sprite bounding boxes are auto-cropped with symmetric horizontal padding to keep Clawd centered. New animations should share this constraint or the walk-in/walk-out positioning will drift.
- The simulator always draws at 1x with integer scaling when resized. Don't rely on subpixel rendering for anything that has to look identical on the device.
