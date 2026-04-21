# Clawd Tank — Product Sense

This doc captures taste and product judgment. When making decisions that aren't covered by a spec, use these principles.

## Who is the user?

Developers who use Claude Code daily and want a glanceable, delightful, physical indicator of what their agents are doing — without switching context to a terminal. They value low-friction setup (plug in device, install menu bar app, go), pixel art aesthetics, and the small-fleet feel of several Clawds working at once.

## Product principles

### 1. Ambient, not interruptive
The device sits on a desk. Its job is to be peripherally informative — visible when you look, invisible when you don't. Animations and LED flashes should draw the eye for real events (API errors, new prompts) and not for routine activity.

### 2. Cute beats efficient
Clawd walks on-screen when a session starts and burrows off-screen when it exits. That animation costs frames and state that a raw status readout wouldn't. Keep it. The charm *is* the product.

### 3. Hardware is optional, not required
Everything should work in the simulator without an ESP32. Developers without hardware can still use, test, and contribute. The daemon treats simulator and device identically at the transport layer.

### 4. Zero-config by default
Install the menu bar app, approve the hook installer, plug in the device. No JSON edits, no PATH wrangling. Preferences are available for power users but the defaults must be sensible.

### 5. Claude Code hooks are the contract
The product is driven entirely by Claude Code's hook surface. If Claude Code adds a new hook, we should use it. We do not scrape terminal output or watch files.

### 6. Respect the BLE budget
BLE MTU is 256 bytes. Every payload shape decision considers that budget. When the protocol has to change to fit, we bump the version characteristic rather than cram bits.

### 7. Sessions are first class
Users think in terms of "my three Claudes" — not "my display's current state." The model tracks per-session identity with stable UUIDs so a compacting session visibly sweeps, then comes back. It is never just a global counter.
