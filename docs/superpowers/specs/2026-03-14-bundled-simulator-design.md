# Bundled Simulator Design

Bundle the native simulator inside the macOS Menu Bar app so users without hardware can see the animated Clawd display on their desktop.

## Context

The simulator (`clawd-tank-sim`) compiles the same firmware source files against SDL2 and runs as a standalone native binary. The menu bar app (`Clawd Tank.app`) is a Python/rumps application packaged via py2app. Currently the simulator must be built and launched separately — users run `clawd-tank-sim --listen` and toggle "Enable Simulator" in the menu bar to connect over TCP.

This design eliminates that manual step: the simulator binary ships inside the `.app` bundle and the menu bar app manages its lifecycle directly.

## Distribution

- GitHub release with a zipped `.app` — no DMG, no code signing, no notarization
- Users download, unzip, drag to Applications, right-click → Open on first launch (Gatekeeper bypass)
- macOS 13+ (Ventura), arm64 only

## Simulator Build Changes

### Static SDL2 Linking

Add a `STATIC_SDL2` CMake option to `simulator/CMakeLists.txt`:

- `STATIC_SDL2=ON`: downloads a pinned SDL2 release tarball, builds it as a static library, and links it into the simulator binary. Produces a self-contained arm64 executable with no dynamic dependencies beyond system frameworks (Cocoa, libSystem).
- `STATIC_SDL2=OFF` (default): uses system/Homebrew SDL2 via `find_package`, preserving the current local development workflow.

SDL2 is zlib-licensed — static linking is permitted.

### New TCP Commands

The simulator's TCP listener (`--listen`) gains three new JSON actions:

| Action | Payload | Effect |
|---|---|---|
| `show_window` | `{"action":"show_window"}` | Shows the SDL window (`SDL_ShowWindow`) |
| `hide_window` | `{"action":"hide_window"}` | Hides the SDL window (`SDL_HideWindow`), process stays alive |
| `set_window` | `{"action":"set_window","pinned":true\|false}` | Toggles always-on-top |

### Window Close Behavior

When the user closes the SDL window (SDL_QUIT / Cmd+W), instead of exiting the process:

1. The window is hidden (`SDL_HideWindow`)
2. The process continues running headless, accepting TCP commands
3. A `{"event":"window_hidden"}` JSON message is sent back over the TCP connection so the menu app can update its toggle state

### Launch Flag Changes

- `--borderless` becomes the default when launched without `--headless` (override with `--bordered` for development)
- New `--hidden` flag: creates the window but starts it hidden. The menu app uses this combined with the `show_window` TCP command for controlled initial launch.

## Menu Bar App Changes

### Transport Architecture

Both BLE and Simulator become independently enable/disable-able transports with preferences:

```json
{
  "ble_enabled": true,
  "sim_enabled": true,
  "sim_window_visible": true,
  "sim_always_on_top": true
}
```

On startup, the daemon reads preferences and only creates transports that are enabled. Toggling "Enabled" in a submenu:

- **Enable**: creates the transport client, adds it to the daemon, starts connecting. For simulator: also spawns the process.
- **Disable**: removes the transport from the daemon. For simulator: kills the subprocess.

### Simulator Process Manager

New class `SimProcessManager` in `clawd_tank_daemon/`:

- **Spawning**: launches `clawd-tank-sim --listen --borderless --hidden` as a subprocess
- **Binary discovery**: looks for the binary at `os.path.join(os.path.dirname(sys.executable), 'clawd-tank-sim')` (inside `.app` bundle), falls back to `shutil.which('clawd-tank-sim')` for development
- **Window control**: sends show/hide/pinned TCP commands via the existing SimClient connection
- **Process monitoring**: if the process crashes, logs it and updates status. No auto-restart.
- **Clean shutdown**: SIGTERM → wait → SIGKILL on disable or app quit

The SimClient remains unchanged — it connects to `127.0.0.1:19872` over TCP. The process manager is a layer above it that owns the subprocess lifecycle.

### Menu Bar UI

Transport-centric layout with submenus:

**Top level:**
```
BLE  ● Connected   ▸
Simulator  ● Running   ▸
─────────────────────────
Brightness              ▸
Session Timeout         ▸
─────────────────────────
Install Claude Code Hooks
Launch at Login
─────────────────────────
Quit Clawd Tank
```

**BLE submenu (enabled):**
```
Status: Connected
✓ Enabled
Reconnect
```

**BLE submenu (disabled):**
```
Status: Disabled
  Enabled
```

**Simulator submenu (enabled):**
```
Status: Running
✓ Enabled
✓ Show Window
✓ Always on Top
```

**Simulator submenu (disabled):**
```
Status: Disabled
  Enabled
```

**Status indicators** on the top-level transport items:
- `●` green — connected / running
- `●` yellow — connecting / launching
- `○` gray — disabled (text also dimmed)

**Menu bar icon**: reflects aggregate state — connected crab if any transport is connected, disconnected crab if none are, notification crab if there are active notifications. Per-transport detail lives in the submenus.

### Defaults

- BLE: enabled
- Simulator: enabled, window visible, borderless, always on top
- Both transports active out of the box for the best first-run experience

### Window Behavior

- Simulator window is always borderless (no title bar)
- Always on top by default, toggleable from the simulator submenu
- Closing the SDL window (red X / Cmd+W) hides the window — process stays alive, same as toggling "Show Window" off
- Show/hide controlled via the simulator submenu or by the `show_window`/`hide_window` TCP commands

### Both Transports Disabled

No special handling. The submenu status shows "Disabled" for each, and the menu bar icon shows the disconnected crab. User can re-enable at any time.

## App Bundle Structure

```
Clawd Tank.app/
  Contents/
    MacOS/
      Clawd Tank          (py2app stub launcher)
      python              (Python interpreter)
      clawd-tank-sim      (simulator binary — NEW)
    Frameworks/
      libpython3.11.dylib, libcrypto, libssl, etc.
    Resources/
      lib/python3.11/
        clawd_tank_daemon/
        clawd_tank_menubar/
      icons/
      AppIcon.icns
    Info.plist
```

The simulator binary is placed in `Contents/MacOS/` per macOS conventions for executables.

## GitHub Actions Workflow

Triggered on tag push (e.g., `v1.2.0`):

1. **Build simulator** — checkout repo, `cmake -B build -DSTATIC_SDL2=ON`, `cmake --build build`. Produces self-contained `clawd-tank-sim` arm64 binary.
2. **Build menu app** — `cd host && python setup.py py2app`. Produces `Clawd Tank.app`.
3. **Inject simulator** — `cp simulator/build/clawd-tank-sim "host/dist/Clawd Tank.app/Contents/MacOS/"`.
4. **Package** — `cd host/dist && zip -r clawd-tank-macos-arm64.zip "Clawd Tank.app"`.
5. **Release** — attach zip to the GitHub release.

macOS runner: `macos-14` (arm64 Apple Silicon).

## Migration

**Existing users upgrading:**

- **Preferences**: no migration code needed. `load_preferences()` falls back to defaults for missing keys. Existing `sim_enabled: false` is preserved. New keys (`ble_enabled`, `sim_window_visible`, `sim_always_on_top`) get their defaults.
- **Note**: the default for `sim_enabled` changes from `false` to `true`, but existing users who never toggled it already have `{"sim_enabled": false}` on disk, so their behavior is unchanged.
- **Hooks**: unchanged — same script, same settings.
- **Launchd plist**: if the user switches from a dev install to the bundled `.app`, the plist's `ProgramArguments` will point to the old Python interpreter path. On startup, detect if the plist exists but points to a different executable than the current one, and prompt the user to re-enable "Launch at Login".
- **Menu structure**: changes from flat to submenus. No user data affected.

## Out of Scope

- Code signing / notarization (future work)
- DMG with drag-to-Applications visual
- Auto-update mechanism
- Intel (x86_64) support
- Windows / Linux builds
- Simulator auto-restart on crash
