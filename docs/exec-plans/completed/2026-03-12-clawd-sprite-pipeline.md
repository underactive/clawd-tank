# Clawd Sprite Pipeline & UI Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a complete sprite design → export → firmware integration pipeline, producing all Clawd animation frames and the full pixel art UI for the 320x172 LCD.

**Architecture:** HTML Canvas tools design pixel art sprites faithful to the original Clawd. A Python converter transforms exported PNGs to RGB565 C arrays. The firmware ui_manager.c is rewritten to render sprites, scene elements, and notification cards using LVGL.

**Tech Stack:** HTML5 Canvas (sprite design), Python + Pillow (PNG→RGB565 conversion), C + LVGL v9 (firmware rendering), ESP-IDF v5.3

---

## Reference: Original Clawd

The original Clawd is a minimalist pixel art crab:
- Salmon/orange rectangular body (~8x5 pixel proportions)
- Two small black square dot eyes on upper body
- Four short legs (two per side), same orange color
- Single flat color, no shading — charm is in the simplicity
- At 64x64 canvas: each "logical pixel" ≈ 6-8px, keeping the blocky aesthetic

## File Structure

### New files to create:

```
tools/
  sprite-designer/
    index.html          # Canvas-based sprite editor with frame export
    clawd-idle.html     # Idle animation frames (8-12 frames)
    clawd-alert.html    # Alert animation frames (6-8 frames)
    clawd-happy.html    # Happy animation frames (6-8 frames)
    clawd-sleeping.html # Sleeping animation frames (6-8 frames)
    clawd-disconnected.html  # Disconnected animation frames (6-8 frames)
    ble-icon.html       # BLE icon sprite (1 frame)
  png2rgb565.py         # PNG → RGB565 C array converter

firmware/main/
  assets/
    sprite_idle.h       # Generated: idle frame RGB565 arrays
    sprite_alert.h      # Generated: alert frame RGB565 arrays
    sprite_happy.h      # Generated: happy frame RGB565 arrays
    sprite_sleeping.h   # Generated: sleeping frame RGB565 arrays
    sprite_disconnected.h  # Generated: disconnected frame RGB565 arrays
    sprite_ble_icon.h   # Generated: BLE icon RGB565 array
    sprites.h           # Master include + animation metadata (frame counts, durations)
  scene.c/h             # Scene rendering (sky gradient, stars, grass, time)
  notification_ui.c/h   # Notification cards (featured + compact list)
```

### Files to modify:

```
firmware/main/ui_manager.c  # Rewrite: sprite animation, state machine, scene orchestration
firmware/main/ui_manager.h  # Update: new API for scene/sprite control
firmware/main/CMakeLists.txt # Add scene.c, notification_ui.c to SRCS
```

---

## Chunk 1: Tooling

### Task 1: Canvas Sprite Editor Base

Build the shared sprite editor infrastructure — a reusable HTML Canvas tool that all animation designers will use.

**Files:**
- Create: `tools/sprite-designer/index.html`

- [ ] **Step 1: Create the sprite editor HTML**

Create `tools/sprite-designer/index.html` — a self-contained HTML file with:

1. A 64x64 logical pixel canvas (displayed at 8x magnification = 512x512 on screen)
2. A color palette sidebar with Clawd's colors pre-loaded:
   - Shell primary: `#ff6b2b`
   - Shell highlight: `#ff8844`
   - Shell shadow: `#993d1a`
   - Eyes: `#000000`
   - Background/transparent: `#1a1a2e` (scene background, used as transparency key)
3. Click-to-paint on the canvas (each click fills one logical pixel)
4. Right-click to erase (set to transparent key color)
5. A "frame list" panel showing thumbnails of all frames in the current animation
6. "Add Frame" button — duplicates current canvas as a new frame
7. "Play" button — cycles through frames at configurable FPS (default 6fps for pixel art)
8. "Export All PNGs" button — downloads each frame as `frame_NN.png` (64x64 actual pixels)
9. A preview area showing the animation playing at 1x size (64x64) on a dark background

The canvas drawing uses `fillRect(x * pixelSize, y * pixelSize, pixelSize, pixelSize)` for each logical pixel. Export uses a temporary 64x64 canvas, copying logical pixels 1:1.

Include the Clawd reference as a guide overlay (toggleable) — draw the basic body shape as a faint outline.

```html
<!DOCTYPE html>
<html>
<head>
  <title>Clawd Sprite Designer</title>
  <style>
    /* Dark theme matching the pixel art aesthetic */
    body { background: #1a1a2e; color: #eee; font-family: monospace; margin: 0; display: flex; }
    /* ... full styles ... */
  </style>
</head>
<body>
  <!-- Left: color palette -->
  <!-- Center: 512x512 canvas (64x64 at 8x zoom) -->
  <!-- Right: frame list + preview + export -->
  <script>
    // Pixel grid state: 64x64 array of hex colors
    // Frame list: array of 64x64 grids
    // Drawing: mousedown/mousemove fill pixels
    // Export: render each frame to 64x64 canvas, toBlob, download
  </script>
</body>
</html>
```

This is a creative tool — make it functional and pleasant to use. Include grid lines on the canvas, undo (Ctrl+Z), and a "Copy Frame" button for iterating on animations.

- [ ] **Step 2: Test the editor in a browser**

Open `tools/sprite-designer/index.html` in a browser. Verify:
- Can draw pixels by clicking
- Can add multiple frames
- Can play animation preview
- Can export PNGs (check they are exactly 64x64 pixels)

- [ ] **Step 3: Commit**

```bash
git add tools/sprite-designer/index.html
git commit -m "feat: add Canvas-based sprite editor for Clawd pixel art"
```

### Task 2: PNG → RGB565 Converter

Build the Python tool that converts exported PNG frames into C header files.

**Files:**
- Create: `tools/png2rgb565.py`

- [ ] **Step 1: Write the converter script**

Create `tools/png2rgb565.py`:

```python
#!/usr/bin/env python3
"""Convert PNG sprite frames to RGB565 C arrays for ESP32 firmware.

Usage:
    python tools/png2rgb565.py <input_dir> <output_header> --name <array_prefix>

Example:
    python tools/png2rgb565.py sprites/idle/ firmware/main/assets/sprite_idle.h --name idle

Reads all PNG files from input_dir (sorted alphabetically), converts each to
an RGB565 uint16_t array, and writes a C header file with:
    - One array per frame: idle_frame_0[], idle_frame_1[], ...
    - A frame table: const uint16_t *idle_frames[]
    - Frame count: IDLE_FRAME_COUNT
    - Dimensions: IDLE_WIDTH, IDLE_HEIGHT
"""
import argparse
import sys
from pathlib import Path
from PIL import Image

TRANSPARENT_COLOR = (0x1a, 0x1a, 0x2e)  # Scene background = transparency key

def rgb888_to_rgb565(r, g, b):
    """Convert 8-bit RGB to 16-bit RGB565 (big-endian for LVGL)."""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def convert_png_to_rgb565(png_path, transparent_color=TRANSPARENT_COLOR):
    """Convert a PNG file to a list of RGB565 uint16_t values."""
    img = Image.open(png_path).convert("RGBA")
    width, height = img.size
    pixels = []
    for y in range(height):
        for x in range(width):
            r, g, b, a = img.getpixel((x, y))
            if a < 128 or (r, g, b) == transparent_color:
                # Use a known transparency key color in RGB565
                pixels.append(rgb888_to_rgb565(*transparent_color))
            else:
                pixels.append(rgb888_to_rgb565(r, g, b))
    return width, height, pixels

def write_header(frames_data, output_path, name, width, height):
    """Write C header file with RGB565 arrays."""
    prefix = name.upper()
    with open(output_path, "w") as f:
        f.write(f"// Auto-generated by png2rgb565.py — do not edit\n")
        f.write(f"#pragma once\n")
        f.write(f"#include <stdint.h>\n\n")
        f.write(f"#define {prefix}_WIDTH  {width}\n")
        f.write(f"#define {prefix}_HEIGHT {height}\n")
        f.write(f"#define {prefix}_FRAME_COUNT {len(frames_data)}\n\n")

        for i, pixels in enumerate(frames_data):
            f.write(f"static const uint16_t {name}_frame_{i}[] = {{\n")
            for row_start in range(0, len(pixels), width):
                row = pixels[row_start:row_start + width]
                hex_vals = ", ".join(f"0x{p:04x}" for p in row)
                f.write(f"    {hex_vals},\n")
            f.write(f"}};\n\n")

        f.write(f"static const uint16_t *{name}_frames[{prefix}_FRAME_COUNT] = {{\n")
        for i in range(len(frames_data)):
            f.write(f"    {name}_frame_{i},\n")
        f.write(f"}};\n")

def main():
    parser = argparse.ArgumentParser(description="Convert PNG sprites to RGB565 C arrays")
    parser.add_argument("input_dir", help="Directory containing PNG frames")
    parser.add_argument("output_header", help="Output C header file path")
    parser.add_argument("--name", required=True, help="Array name prefix (e.g., 'idle')")
    args = parser.parse_args()

    input_dir = Path(args.input_dir)
    pngs = sorted(input_dir.glob("*.png"))
    if not pngs:
        print(f"No PNG files found in {input_dir}", file=sys.stderr)
        sys.exit(1)

    frames_data = []
    width = height = None
    for png in pngs:
        w, h, pixels = convert_png_to_rgb565(png)
        if width is None:
            width, height = w, h
        elif (w, h) != (width, height):
            print(f"Size mismatch: {png.name} is {w}x{h}, expected {width}x{height}", file=sys.stderr)
            sys.exit(1)
        frames_data.append(pixels)

    Path(args.output_header).parent.mkdir(parents=True, exist_ok=True)
    write_header(frames_data, args.output_header, args.name, width, height)
    print(f"Wrote {len(frames_data)} frames ({width}x{height}) to {args.output_header}")

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Test with a synthetic PNG**

```bash
cd /Users/marciorodrigues/Projects/espc6-lcd-display
python3 -c "
from PIL import Image
img = Image.new('RGBA', (64, 64), (0x1a, 0x1a, 0x2e, 255))
# Draw a simple orange square in the center
for x in range(24, 40):
    for y in range(24, 40):
        img.putpixel((x, y), (0xff, 0x6b, 0x2b, 255))
img.save('/tmp/test_frame_00.png')
"
mkdir -p /tmp/test_sprites
cp /tmp/test_frame_00.png /tmp/test_sprites/
python3 tools/png2rgb565.py /tmp/test_sprites /tmp/test_sprite.h --name test
head -20 /tmp/test_sprite.h
```

Expected: header file with `TEST_WIDTH 64`, `TEST_HEIGHT 64`, `TEST_FRAME_COUNT 1`, and a `test_frame_0[]` array.

- [ ] **Step 3: Commit**

```bash
git add tools/png2rgb565.py
git commit -m "feat: add PNG to RGB565 C array converter for sprite pipeline"
```

---

## Chunk 2: Sprite Design

### Task 3: Design Clawd Idle Animation

Design the main idle/living animation — Clawd breathing, blinking, and looking around. This is the most-seen animation.

**Files:**
- Create: `tools/sprite-designer/clawd-idle.html`
- Output: 8-12 PNG frames in `tools/sprite-designer/exports/idle/`

- [ ] **Step 1: Create the idle animation designer**

Create `tools/sprite-designer/clawd-idle.html` — a self-contained HTML file that programmatically draws Clawd's idle animation frames on a 64x64 Canvas.

**Clawd base design (scale up from reference):**
- Body: ~28x18 logical pixels, centered horizontally, salmon/orange (#ff6b2b)
- Eyes: 2x2 or 3x3 black pixels, spaced ~8px apart on upper body
- Legs: 4 legs, each ~3x6 pixels, extending downward from body corners
- Top of head: slightly rounded (1-2 pixel indent on corners)

**Idle frames (8 total):**
1. Neutral pose (base)
2. Neutral pose (identical — holds for 2 beats)
3. Eyes half-closed (blink start — shrink eye height by 1px)
4. Eyes closed (blink — eyes become 2x1 horizontal line)
5. Eyes half-open (blink end)
6. Neutral pose
7. Slight lean right (shift body 1px right, legs adjust)
8. Back to neutral

Draw each frame programmatically using `ctx.fillRect()` calls. The file should:
- Show all frames side by side
- Have a play/pause animation preview at 6fps
- Have an "Export PNGs" button that downloads `idle_00.png` through `idle_07.png`

Use the transparency key color `#1a1a2e` for the background.

- [ ] **Step 2: Preview and verify animation**

Open in browser. Verify:
- Clawd looks like the reference (blocky, minimalist, orange crab)
- Blink animation is smooth and reads correctly at 64x64
- Lean animation is subtle (1-2 pixel shift)
- All frames export as exactly 64x64 PNGs

- [ ] **Step 3: Export PNGs and convert**

```bash
mkdir -p tools/sprite-designer/exports/idle
# (export PNGs from browser to this directory)
python3 tools/png2rgb565.py tools/sprite-designer/exports/idle firmware/main/assets/sprite_idle.h --name idle
```

- [ ] **Step 4: Commit**

```bash
git add tools/sprite-designer/clawd-idle.html tools/sprite-designer/exports/idle/
git add firmware/main/assets/sprite_idle.h
git commit -m "feat: design Clawd idle animation (8 frames)"
```

### Task 4: Design Clawd Alert Animation

Clawd glances toward the right when a notification arrives.

**Files:**
- Create: `tools/sprite-designer/clawd-alert.html`
- Output: 6 PNG frames in `tools/sprite-designer/exports/alert/`

- [ ] **Step 1: Create the alert animation designer**

Create `tools/sprite-designer/clawd-alert.html` with 6 frames:

1. Neutral pose (same as idle frame 0)
2. Eyes shift right 1px (looking toward notifications)
3. Body leans right 1px, eyes shifted right
4. Small "!" pixel appears above head (2x4 pixels, yellow #ffdd57)
5. Hold the alert pose
6. "!" fades (becomes background color), body still leaning right

This is the pose Clawd holds while notifications are visible.

- [ ] **Step 2: Preview and export**

Same as Task 3 — verify animation, export PNGs, convert.

```bash
mkdir -p tools/sprite-designer/exports/alert
python3 tools/png2rgb565.py tools/sprite-designer/exports/alert firmware/main/assets/sprite_alert.h --name alert
```

- [ ] **Step 3: Commit**

```bash
git add tools/sprite-designer/clawd-alert.html tools/sprite-designer/exports/alert/
git add firmware/main/assets/sprite_alert.h
git commit -m "feat: design Clawd alert animation (6 frames)"
```

### Task 5: Design Clawd Happy Animation

Celebratory bounce when all notifications are cleared.

**Files:**
- Create: `tools/sprite-designer/clawd-happy.html`
- Output: 6 PNG frames in `tools/sprite-designer/exports/happy/`

- [ ] **Step 1: Create the happy animation designer**

6 frames:
1. Neutral pose
2. Legs compress (crouch — legs shorten by 2px)
3. Body jumps up 4px, legs extend
4. At peak — small sparkle pixels (yellow #ffdd57) appear at body corners
5. Coming down — body at +2px
6. Landing — back to neutral, sparkles fade

- [ ] **Step 2: Preview, export, convert**

```bash
mkdir -p tools/sprite-designer/exports/happy
python3 tools/png2rgb565.py tools/sprite-designer/exports/happy firmware/main/assets/sprite_happy.h --name happy
```

- [ ] **Step 3: Commit**

```bash
git add tools/sprite-designer/clawd-happy.html tools/sprite-designer/exports/happy/
git add firmware/main/assets/sprite_happy.h
git commit -m "feat: design Clawd happy animation (6 frames)"
```

### Task 6: Design Clawd Sleeping Animation

Peaceful rest with Zzz bubbles.

**Files:**
- Create: `tools/sprite-designer/clawd-sleeping.html`
- Output: 6 PNG frames in `tools/sprite-designer/exports/sleeping/`

- [ ] **Step 1: Create the sleeping animation designer**

6 frames:
1. Curled up pose — body wider and shorter (~30x14px), legs tucked under (barely visible)
2. Same pose, eyes are horizontal lines (2x1 pixels, dark gray #555555 instead of black)
3. Body shifts down 1px (breathing out)
4. Body shifts up 1px (breathing in), small "z" pixel cluster appears (3x3, muted blue #7777bb) at top-right of body
5. "z" moves up 2px, becomes slightly transparent (lighter color)
6. "z" gone, new smaller "z" starts near body — loop back to frame 3

Use darker variant of Clawd's colors: shell becomes #993d1a (shadow color).

- [ ] **Step 2: Preview, export, convert**

```bash
mkdir -p tools/sprite-designer/exports/sleeping
python3 tools/png2rgb565.py tools/sprite-designer/exports/sleeping firmware/main/assets/sprite_sleeping.h --name sleeping
```

- [ ] **Step 3: Commit**

```bash
git add tools/sprite-designer/clawd-sleeping.html tools/sprite-designer/exports/sleeping/
git add firmware/main/assets/sprite_sleeping.h
git commit -m "feat: design Clawd sleeping animation (6 frames)"
```

### Task 7: Design Clawd Disconnected Animation + BLE Icon

Clawd staring at a floating BLE icon, confused.

**Files:**
- Create: `tools/sprite-designer/clawd-disconnected.html`
- Create: `tools/sprite-designer/ble-icon.html`
- Output: 6 PNG frames in `tools/sprite-designer/exports/disconnected/`
- Output: 1 PNG in `tools/sprite-designer/exports/ble-icon/`

- [ ] **Step 1: Create the BLE icon designer**

Create `tools/sprite-designer/ble-icon.html` — a single 16x16 pixel Bluetooth rune symbol:
- The classic Bluetooth "B rune" shape in muted blue (#4466aa)
- On transparent background (#1a1a2e)
- Simple pixel art interpretation: angular lines forming the ᛒ shape

Export as `ble_icon_00.png`.

- [ ] **Step 2: Create the disconnected animation designer**

Create `tools/sprite-designer/clawd-disconnected.html` with 6 frames:

1. Neutral pose, eyes looking up-right (toward where BLE icon will be placed in scene)
2. Head tilts right slightly (body shifts 1px)
3. Eyes shift left (looking away from icon)
4. Eyes shift back right (looking at icon again)
5. Small "?" pixel cluster (3x4, yellow #ffdd57) appears above head
6. "?" fades, back to frame 1 pose

The BLE icon is rendered separately by the scene layer, not baked into the sprite.

- [ ] **Step 3: Export and convert both**

```bash
mkdir -p tools/sprite-designer/exports/disconnected tools/sprite-designer/exports/ble-icon
python3 tools/png2rgb565.py tools/sprite-designer/exports/disconnected firmware/main/assets/sprite_disconnected.h --name disconnected
python3 tools/png2rgb565.py tools/sprite-designer/exports/ble-icon firmware/main/assets/sprite_ble_icon.h --name ble_icon
```

- [ ] **Step 4: Commit**

```bash
git add tools/sprite-designer/clawd-disconnected.html tools/sprite-designer/ble-icon.html
git add tools/sprite-designer/exports/disconnected/ tools/sprite-designer/exports/ble-icon/
git add firmware/main/assets/sprite_disconnected.h firmware/main/assets/sprite_ble_icon.h
git commit -m "feat: design Clawd disconnected animation + BLE icon sprite"
```

### Task 8: Master Sprite Header + Metadata

Create the master include file that ties all sprite assets together.

**Files:**
- Create: `firmware/main/assets/sprites.h`

- [ ] **Step 1: Create the master sprite header**

```c
// Master sprite header — create this AFTER all sprite_*.h files are generated.
// Each #include below must exist on disk before building firmware.
#pragma once

#include "sprite_idle.h"
#include "sprite_alert.h"
#include "sprite_happy.h"
#include "sprite_sleeping.h"
#include "sprite_disconnected.h"
#include "sprite_ble_icon.h"

// Transparency key color in RGB565 (matches #1a1a2e)
// R=0x1a: (0x18<<8)=0x1800, G=0x1a: (0x18<<3)=0x00C0, B=0x2e: (0x2e>>3)=0x05
#define SPRITE_TRANSPARENT_COLOR 0x18C5

// Animation frame rates (ms per frame)
#define ANIM_IDLE_FRAME_MS      167  // ~6fps
#define ANIM_ALERT_FRAME_MS     100  // ~10fps (snappier reaction)
#define ANIM_HAPPY_FRAME_MS     120  // ~8fps
#define ANIM_SLEEPING_FRAME_MS  250  // ~4fps (slow, peaceful)
#define ANIM_DISCONNECTED_FRAME_MS 200  // ~5fps

// Sprite metadata struct for animation system
typedef struct {
    const uint16_t **frames;
    int frame_count;
    int width;
    int height;
    int frame_ms;
    bool loop;           // true = loop forever, false = play once
} sprite_anim_t;

// Pre-built animation descriptors
static const sprite_anim_t ANIM_IDLE = {
    .frames = idle_frames, .frame_count = IDLE_FRAME_COUNT,
    .width = IDLE_WIDTH, .height = IDLE_HEIGHT,
    .frame_ms = ANIM_IDLE_FRAME_MS, .loop = true
};

// Non-looping animations: when they reach the last frame, scene_tick()
// holds on the final frame. The caller can check scene_anim_finished()
// to decide what to do next (e.g., transition alert→idle hold, or happy→idle).
static const sprite_anim_t ANIM_ALERT = {
    .frames = alert_frames, .frame_count = ALERT_FRAME_COUNT,
    .width = ALERT_WIDTH, .height = ALERT_HEIGHT,
    .frame_ms = ANIM_ALERT_FRAME_MS, .loop = false
};

static const sprite_anim_t ANIM_HAPPY = {
    .frames = happy_frames, .frame_count = HAPPY_FRAME_COUNT,
    .width = HAPPY_WIDTH, .height = HAPPY_HEIGHT,
    .frame_ms = ANIM_HAPPY_FRAME_MS, .loop = false
};

static const sprite_anim_t ANIM_SLEEPING = {
    .frames = sleeping_frames, .frame_count = SLEEPING_FRAME_COUNT,
    .width = SLEEPING_WIDTH, .height = SLEEPING_HEIGHT,
    .frame_ms = ANIM_SLEEPING_FRAME_MS, .loop = true
};

static const sprite_anim_t ANIM_DISCONNECTED = {
    .frames = disconnected_frames, .frame_count = DISCONNECTED_FRAME_COUNT,
    .width = DISCONNECTED_WIDTH, .height = DISCONNECTED_HEIGHT,
    .frame_ms = ANIM_DISCONNECTED_FRAME_MS, .loop = true
};
```

- [ ] **Step 2: Commit**

```bash
git add firmware/main/assets/sprites.h
git commit -m "feat: add master sprite header with animation metadata"
```

---

## Chunk 3: Firmware UI Implementation

### Task 9: Scene Renderer (Sky, Stars, Grass, Time)

Build the pixel art scene that serves as Clawd's world.

**Files:**
- Create: `firmware/main/scene.h`
- Create: `firmware/main/scene.c`
- Modify: `firmware/main/CMakeLists.txt` — add `scene.c` to SRCS

- [ ] **Step 1: Create scene.h**

```c
#pragma once
#include "lvgl.h"

// Scene manages the pixel art background (sky, stars, grass, time)
// and Clawd sprite animation.

typedef struct scene_t scene_t;

// Create the scene on the given screen. Returns opaque handle.
scene_t *scene_create(lv_obj_t *parent);

// Set the scene width (full 320 for idle, ~107 for notification view).
void scene_set_width(scene_t *scene, int width_px, int anim_ms);

// Set which Clawd animation to play.
typedef enum {
    CLAWD_ANIM_IDLE,
    CLAWD_ANIM_ALERT,
    CLAWD_ANIM_HAPPY,
    CLAWD_ANIM_SLEEPING,
    CLAWD_ANIM_DISCONNECTED,
} clawd_anim_id_t;

void scene_set_clawd_anim(scene_t *scene, clawd_anim_id_t anim);

// Show/hide the time display.
void scene_set_time_visible(scene_t *scene, bool visible);

// Update the time text (call once per minute from UI task).
void scene_update_time(scene_t *scene, int hour, int minute);

// Show/hide BLE disconnected icon.
void scene_set_ble_icon_visible(scene_t *scene, bool visible);

// Tick the sprite animation (call from UI task loop).
// When a non-looping animation finishes, scene_tick auto-transitions to CLAWD_ANIM_IDLE.
void scene_tick(scene_t *scene);

// Returns true if a non-looping animation (alert, happy) is still playing.
bool scene_is_playing_oneshot(scene_t *scene);
```

- [ ] **Step 2: Implement scene.c**

Create `firmware/main/scene.c`:

The scene is an `lv_obj_t` container that fills the screen height (172px) and has a configurable width. It contains:

1. **Sky background:** An `lv_obj_t` with a vertical gradient style (using `lv_style_set_bg_grad_color`). Three-stop gradient: #0a0e1a → #16213e → #1a1a2e.

2. **Stars:** 6 `lv_obj_t` small rectangles (2-4px) at fixed positions, with specific colors from the spec: `#ffff88`, `#88ccff`, `#ffaa88`, `#aaccff`, `#ffdd88`, `#88ffcc`. An LVGL timer randomly toggles their opacity between 0.3 and 1.0 every 2-4 seconds.

3. **Grass strip:** An `lv_obj_t` at the bottom, 14px tall, with gradient #2d4a2d → #1a331a. Small lighter rectangles for tufts.

4. **Clawd sprite:** An `lv_image_t` at center. Uses `lv_image_set_src()` to swap frames. An `lv_timer_t` advances frames at the animation's configured frame rate. The image data is an `lv_image_dsc_t` pointing to the current frame's RGB565 array in flash.

5. **Time label:** An `lv_label_t` positioned top-right, Montserrat 18px, color #4466aa.

6. **BLE icon:** An `lv_image_t` positioned upper-right of scene, hidden by default.

7. **"No connection" label:** An `lv_label_t` at the bottom of the scene, Montserrat 8px, muted color (#556677). Hidden by default, shown alongside BLE icon in disconnected state.

8. **Disconnected desaturation:** When entering disconnected state, apply `lv_obj_set_style_img_recolor(clawd_img, lv_color_hex(0x4466aa), 0)` and `lv_obj_set_style_img_recolor_opa(clawd_img, LV_OPA_30, 0)` to blue-shift the sprite. Reset to `LV_OPA_TRANSP` when leaving disconnected state.

The `scene_t` struct holds pointers to all these LVGL objects plus current animation state (which anim, current frame index, timer handle).

Key implementation detail for sprites: create `lv_image_dsc_t` descriptors on the fly:
```c
static lv_image_dsc_t s_sprite_dsc = {
    .header = {
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = 64,
        .h = 64,
    },
    .data_size = 64 * 64 * 2,
    .data = NULL,  // updated each frame
};
```

On each animation tick: use **one `lv_image_dsc_t` per frame** (array of descriptors, each pointing to its frame's data in flash). Call `lv_image_set_src(clawd_img, &frame_dscs[frame_idx])` — since the pointer changes, LVGL properly invalidates the cache. This avoids the LVGL v9 image cache issue where mutating `.data` on a single descriptor at the same address may not trigger re-render.

Frame time tracking: `scene_tick()` uses `lv_tick_get()` (LVGL's own millisecond counter) to measure elapsed time since last frame advance, compared against the animation's `frame_ms`.

**Note on display refresh vs sprite flip rate:** The LVGL render loop runs at ~30fps (every `lv_timer_handler()` call). Sprite animations flip at lower artistic rates (4-10fps) controlled by their own timers. Both are independent — LVGL re-renders the full screen at 30fps regardless of sprite flip rate.

- [ ] **Step 3: Update CMakeLists.txt**

Add `scene.c` to the SRCS list in `firmware/main/CMakeLists.txt`.

- [ ] **Step 4: Commit**

```bash
git add firmware/main/scene.h firmware/main/scene.c firmware/main/CMakeLists.txt
git commit -m "feat: add scene renderer (sky, stars, grass, time, Clawd sprite)"
```

### Task 10: Notification UI (Featured Card + Compact List)

Build the right-panel notification display.

**Files:**
- Create: `firmware/main/notification_ui.h`
- Create: `firmware/main/notification_ui.c`
- Modify: `firmware/main/CMakeLists.txt` — add `notification_ui.c` to SRCS

- [ ] **Step 1: Create notification_ui.h**

```c
#pragma once
#include "lvgl.h"
#include "notification.h"

typedef struct notification_ui_t notification_ui_t;

// Create the notification panel. Hidden initially.
notification_ui_t *notification_ui_create(lv_obj_t *parent);

// Show/hide the panel with animation.
void notification_ui_show(notification_ui_t *ui, bool show, int anim_ms);

// Update panel x-position (called during scene width animation to track the scene edge).
void notification_ui_set_x(notification_ui_t *ui, int x_px);

// Rebuild the notification list from the store.
// Call after any add/dismiss/clear event.
void notification_ui_rebuild(notification_ui_t *ui, const notification_store_t *store);
```

- [ ] **Step 2: Implement notification_ui.c**

Create `firmware/main/notification_ui.c`:

The notification panel is an `lv_obj_t` container. Its x-position is **not hardcoded** — it is always placed at `scene_width` pixels from the left and fills the remaining width (`320 - scene_width`). The `notification_ui_show()` function accepts the current scene width, and `notification_ui_set_x()` is called by the scene width animation callback to keep the panel tracking the scene edge during the 400ms contraction/expansion animation. When the scene is at full 320px width, the panel is offscreen at x=320.

Layout (top to bottom):
1. **Counter label:** `lv_label_t` — "▸ N WAITING!" in Montserrat 10px bold, yellow #ffdd57
2. **Featured card:** `lv_obj_t` container with:
   - Background #2a1a3e, 2px border in accent color
   - Project name label: Montserrat 10px bold, yellow
   - Message label: Montserrat 8px, #cc99ff
   - Badge label: Montserrat 8px, accent color
3. **Compact list:** Up to 7 `lv_obj_t` rows, each 12px:
   - Colored dot (small `lv_obj_t` circle, 4x4px) + project name label (Montserrat 8px)

The accent color palette (8 colors from the spec) is indexed by notification position.

`notification_ui_rebuild()`:
1. Collect active notifications by iterating `store->items[0..NOTIF_MAX_COUNT-1]` where `.active == true`
2. Sort locally by `seq` (ascending). Use a simple insertion sort — max 8 elements.
3. The highest-seq notification becomes the featured card
4. All others become compact list entries, each with a tinted background row:
   - Row background tints per position: `lv_color_hex(accent_colors[i])` at `LV_OPA_10` (semi-transparent)
   - Accent colors array: `{0xff6b2b, 0x4488ff, 0xaaaa33, 0x44aa44, 0x7b68ee, 0x44cccc, 0xcc4488, 0xccaa22}`
5. Update all labels and colors
6. Show/hide rows based on count

For **slide-in** animation on new notifications: use `lv_anim_t` on the featured card's x position (start offscreen at x=320, end at normal position, 300ms ease-out). **Existing compact entries animate downward** (~200ms) to make room — use `lv_anim_t` on each entry's y position.

Auto-rotation timer (~8 seconds): cycles which notification is "featured", updating the card content. Badge shows "NEWEST" for the most recent notification, or relative age (e.g., "2m ago") for older ones during rotation.

- [ ] **Step 3: Update CMakeLists.txt**

Add `notification_ui.c` to SRCS.

- [ ] **Step 4: Commit**

```bash
git add firmware/main/notification_ui.h firmware/main/notification_ui.c firmware/main/CMakeLists.txt
git commit -m "feat: add notification UI (featured card + compact list)"
```

### Task 11: Rewrite UI Manager (State Machine + Integration)

Rewrite `ui_manager.c` to orchestrate the scene and notification panel.

**Files:**
- Modify: `firmware/main/ui_manager.c` (full rewrite)
- Modify: `firmware/main/ui_manager.h` (update API)

- [ ] **Step 1: Update ui_manager.h**

Keep the existing public API but add the internal state enum documentation:

```c
#pragma once
#include "ble_service.h"

void ui_manager_init(void);
void ui_manager_handle_event(const ble_evt_t *evt);
void ui_manager_tick(void);
```

- [ ] **Step 2: Rewrite ui_manager.c**

Replace the current placeholder implementation with:

```c
#include "ui_manager.h"
#include "scene.h"
#include "notification_ui.h"
#include "notification.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
// _lock_t is from newlib/picolibc, available via stdio.h or sys/lock.h
#include <time.h>
#include <sys/time.h>

typedef enum {
    UI_STATE_FULL_IDLE,       // Full-screen scene, no notifications
    UI_STATE_NOTIFICATION,    // Split: scene left, notifications right
    UI_STATE_DISCONNECTED,    // Full-screen scene with BLE icon
} ui_state_t;

static _lock_t s_lock;
static ui_state_t s_state = UI_STATE_DISCONNECTED;
static notification_store_t s_store;
static scene_t *s_scene;
static notification_ui_t *s_notif_ui;
static bool s_ble_connected = false;
static uint32_t s_last_activity_tick = 0;

#define SLEEP_TIMEOUT_MS (5 * 60 * 1000)  // 5 minutes

static void transition_to(ui_state_t new_state) {
    if (s_state == new_state) return;
    ui_state_t old = s_state;
    s_state = new_state;

    switch (new_state) {
        case UI_STATE_FULL_IDLE:
            scene_set_width(s_scene, 320, old == UI_STATE_NOTIFICATION ? 400 : 0);
            notification_ui_show(s_notif_ui, false, 300);
            scene_set_ble_icon_visible(s_scene, false);
            // Don't overwrite Clawd animation here — let pending
            // non-looping anims (happy) finish first. scene_tick()
            // will auto-transition to IDLE when they complete.
            if (!scene_is_playing_oneshot(s_scene)) {
                scene_set_clawd_anim(s_scene, CLAWD_ANIM_IDLE);
            }
            scene_set_time_visible(s_scene, true);
            break;

        case UI_STATE_NOTIFICATION:
            scene_set_width(s_scene, 107, old == UI_STATE_FULL_IDLE ? 400 : 0);
            notification_ui_show(s_notif_ui, true, 300);
            scene_set_ble_icon_visible(s_scene, false);
            scene_set_clawd_anim(s_scene, CLAWD_ANIM_ALERT);
            scene_set_time_visible(s_scene, false);
            break;

        case UI_STATE_DISCONNECTED:
            scene_set_width(s_scene, 320, 0);
            notification_ui_show(s_notif_ui, false, 0);
            scene_set_ble_icon_visible(s_scene, true);
            scene_set_clawd_anim(s_scene, CLAWD_ANIM_DISCONNECTED);
            scene_set_time_visible(s_scene, false);
            break;
    }
}

void ui_manager_init(void) {
    _lock_init(&s_lock);
    notif_store_init(&s_store);

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a2e), 0);

    s_scene = scene_create(screen);
    s_notif_ui = notification_ui_create(screen);

    transition_to(UI_STATE_DISCONNECTED);
}

void ui_manager_handle_event(const ble_evt_t *evt) {
    _lock_acquire(&s_lock);
    s_last_activity_tick = xTaskGetTickCount();

    switch (evt->type) {
        case BLE_EVT_CONNECTED:
            s_ble_connected = true;
            // Per spec: "Clawd does a happy reaction" on reconnect
            scene_set_clawd_anim(s_scene, CLAWD_ANIM_HAPPY);
            if (notif_store_count(&s_store) > 0) {
                notification_ui_rebuild(s_notif_ui, &s_store);
                transition_to(UI_STATE_NOTIFICATION);
            } else {
                transition_to(UI_STATE_FULL_IDLE);
            }
            break;

        case BLE_EVT_DISCONNECTED:
            s_ble_connected = false;
            transition_to(UI_STATE_DISCONNECTED);
            break;

        case BLE_EVT_NOTIF_ADD:
            notif_store_add(&s_store, evt->id, evt->project, evt->message);
            notification_ui_rebuild(s_notif_ui, &s_store);
            if (s_state != UI_STATE_NOTIFICATION) {
                transition_to(UI_STATE_NOTIFICATION);
            }
            // Play alert animation briefly, then hold
            scene_set_clawd_anim(s_scene, CLAWD_ANIM_ALERT);
            break;

        case BLE_EVT_NOTIF_DISMISS:
            notif_store_dismiss(&s_store, evt->id);
            if (notif_store_count(&s_store) == 0) {
                // Play happy animation, then transition to idle
                scene_set_clawd_anim(s_scene, CLAWD_ANIM_HAPPY);
                // After happy animation completes, transition_to FULL_IDLE
                // (scene tick checks if non-looping anim finished)
                transition_to(UI_STATE_FULL_IDLE);
            } else {
                notification_ui_rebuild(s_notif_ui, &s_store);
            }
            break;

        case BLE_EVT_NOTIF_CLEAR:
            notif_store_clear(&s_store);
            scene_set_clawd_anim(s_scene, CLAWD_ANIM_HAPPY);
            transition_to(UI_STATE_FULL_IDLE);
            break;
    }

    _lock_release(&s_lock);
}

void ui_manager_tick(void) {
    _lock_acquire(&s_lock);

    // Check sleep timeout (idle for 5 minutes with no notifications)
    if (s_state == UI_STATE_FULL_IDLE && s_ble_connected) {
        uint32_t elapsed = (xTaskGetTickCount() - s_last_activity_tick) * portTICK_PERIOD_MS;
        if (elapsed > SLEEP_TIMEOUT_MS) {
            scene_set_clawd_anim(s_scene, CLAWD_ANIM_SLEEPING);
        }
    }

    // Update time display (check once per tick, ~200ms)
    static int s_last_minute = -1;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm && tm->tm_min != s_last_minute) {
        s_last_minute = tm->tm_min;
        scene_update_time(s_scene, tm->tm_hour, tm->tm_min);
    }

    scene_tick(s_scene);
    lv_timer_handler();

    _lock_release(&s_lock);
}
```

- [ ] **Step 3: Commit**

```bash
git add firmware/main/ui_manager.c firmware/main/ui_manager.h
git commit -m "feat: rewrite UI manager with sprite animations and scene orchestration"
```

### Task 12: Build, Flash, and Verify

Build the complete firmware and verify on hardware.

**Files:**
- No new files

- [ ] **Step 1: Build**

```bash
cd /Users/marciorodrigues/Projects/espc6-lcd-display/firmware
bash -c 'source ../bsp/esp-idf/export.sh && idf.py build'
```

Fix any compilation errors.

- [ ] **Step 2: Flash**

```bash
cd /Users/marciorodrigues/Projects/espc6-lcd-display/firmware
bash -c 'source ../bsp/esp-idf/export.sh && idf.py -p /dev/cu.usbmodem2101 flash'
```

- [ ] **Step 3: Verify on hardware**

1. Confirm display shows the pixel art scene (sky, stars, grass, Clawd idle animation)
2. Connect via BLE from Python daemon and send a test notification
3. Verify Clawd reacts (alert animation) and notification card appears
4. Send a dismiss and verify Clawd does happy animation, returns to idle
5. Disconnect BLE and verify disconnected state (Clawd + BLE icon)

- [ ] **Step 4: Commit any fixes**

```bash
git add -u
git commit -m "fix: resolve build/runtime issues from sprite integration"
```

---

## Task Dependencies

```
Task 1 (Editor) ──┐
                   ├──→ Tasks 3-7 (Sprites, parallelizable) ──→ Task 8 (Master Header)
Task 2 (Converter)┘                                                      │
                                                                          ▼
                                          Task 9 (Scene) ──→ Task 11 (UI Manager) ──→ Task 12 (Build)
                                          Task 10 (Notif UI) ──┘
```

Tasks 3-7 can all run in parallel once Tasks 1-2 are complete.
Tasks 9 and 10 can run in parallel.
Task 11 depends on 8, 9, and 10.
Task 12 depends on 11.
