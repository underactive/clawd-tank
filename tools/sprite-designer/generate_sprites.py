#!/usr/bin/env python3
"""Generate Clawd sprite PNGs for sleeping, disconnected animations, and BLE icon."""

import os
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Pillow required. Install: pip install Pillow")
    sys.exit(1)

# ---- Colors ----
BG = (0x1A, 0x1A, 0x2E)
SHELL_NORMAL = (0xFF, 0x6B, 0x2B)
SHELL_DARK = (0x99, 0x3D, 0x1A)
EYES_BLACK = (0x00, 0x00, 0x00)
EYES_CLOSED = (0x55, 0x55, 0x55)
Z_BLUE = (0x77, 0x77, 0xBB)
Z_LIGHT = (0x99, 0x99, 0xCC)
Z_MUTED = (0x55, 0x55, 0xAA)
Q_YELLOW = (0xFF, 0xDD, 0x57)
Q_FADED = (0x88, 0x77, 0x44)
BLE_BLUE = (0x44, 0x66, 0xAA)

W, H = 64, 64


# ---- Drawing helpers ----
def new_frame(w=W, h=H):
    return Image.new('RGBA', (w, h), BG + (255,))


def rect(img, x0, y0, x1, y1, color):
    for y in range(max(0, y0), min(img.height, y1 + 1)):
        for x in range(max(0, x0), min(img.width, x1 + 1)):
            img.putpixel((x, y), color + (255,))


def pixel(img, x, y, color):
    if 0 <= x < img.width and 0 <= y < img.height:
        img.putpixel((x, y), color + (255,))


def bg_pixel(img, x, y):
    pixel(img, x, y, BG)


# ---- Sleeping Clawd (Task 6) ----
# Curled up pose: body 30x14, darker shell #993d1a

def draw_sleeping_body(img, yo=0):
    """Draw sleeping Clawd body (curled up, wider and shorter)."""
    s = SHELL_DARK
    # Main body: 30w x 14h centered at ~(31.5, 32.5)
    rect(img, 17, 26 + yo, 46, 39 + yo, s)
    # Round corners
    bg_pixel(img, 17, 26 + yo)
    bg_pixel(img, 46, 26 + yo)
    bg_pixel(img, 17, 39 + yo)
    bg_pixel(img, 46, 39 + yo)
    # Left claw (tucked small): 3x4
    rect(img, 14, 30 + yo, 16, 33 + yo, s)
    # Right claw (tucked small): 3x4
    rect(img, 47, 30 + yo, 49, 33 + yo, s)
    # Legs (barely visible stubs): 4 x 2x2
    for lx in [20, 25, 37, 42]:
        rect(img, lx, 40 + yo, lx + 1, 41 + yo, s)


def draw_eyes_open_sleeping(img, yo=0):
    """2x2 black eyes for frame 1."""
    rect(img, 25, 30 + yo, 26, 31 + yo, EYES_BLACK)
    rect(img, 37, 30 + yo, 38, 31 + yo, EYES_BLACK)


def draw_eyes_closed(img, yo=0):
    """2x1 horizontal line eyes (sleeping)."""
    rect(img, 25, 31 + yo, 26, 31 + yo, EYES_CLOSED)
    rect(img, 37, 31 + yo, 38, 31 + yo, EYES_CLOSED)


def draw_z(img, x, y, color):
    """3x3 'z' pattern: ##. / .#. / .##"""
    pixel(img, x, y, color)
    pixel(img, x + 1, y, color)
    pixel(img, x + 1, y + 1, color)
    pixel(img, x + 1, y + 2, color)
    pixel(img, x + 2, y + 2, color)


def draw_small_z(img, x, y, color):
    """2x2 small 'z': #. / .#"""
    pixel(img, x, y, color)
    pixel(img, x + 1, y + 1, color)


def generate_sleeping():
    """Generate 6 sleeping animation frames."""
    frames = []

    # Frame 1: Curled up, regular eyes
    f = new_frame()
    draw_sleeping_body(f, yo=0)
    draw_eyes_open_sleeping(f, yo=0)
    frames.append(f)

    # Frame 2: Same pose, eyes become horizontal lines
    f = new_frame()
    draw_sleeping_body(f, yo=0)
    draw_eyes_closed(f, yo=0)
    frames.append(f)

    # Frame 3: Body shifts down 1px (breathing out)
    f = new_frame()
    draw_sleeping_body(f, yo=1)
    draw_eyes_closed(f, yo=1)
    frames.append(f)

    # Frame 4: Body shifts up 1px (breathing in), "z" top-right
    f = new_frame()
    draw_sleeping_body(f, yo=-1)
    draw_eyes_closed(f, yo=-1)
    draw_z(f, 44, 18, Z_BLUE)
    frames.append(f)

    # Frame 5: "z" moves up 2px, lighter color
    f = new_frame()
    draw_sleeping_body(f, yo=-1)
    draw_eyes_closed(f, yo=-1)
    draw_z(f, 44, 16, Z_LIGHT)
    frames.append(f)

    # Frame 6: "z" gone, new smaller "z" near body
    f = new_frame()
    draw_sleeping_body(f, yo=0)
    draw_eyes_closed(f, yo=0)
    draw_small_z(f, 43, 22, Z_MUTED)
    frames.append(f)

    return frames


# ---- Disconnected Clawd (Task 7) ----
# Normal colors, looking around confused

def draw_normal_body(img, xo=0):
    """Draw normal Clawd body."""
    s = SHELL_NORMAL
    # Main body: 28w x 18h
    rect(img, 18 + xo, 23, 45 + xo, 40, s)
    # Round corners
    bg_pixel(img, 18 + xo, 23)
    bg_pixel(img, 45 + xo, 23)
    bg_pixel(img, 18 + xo, 40)
    bg_pixel(img, 45 + xo, 40)
    # Left claw: 4x4
    rect(img, 14 + xo, 27, 17 + xo, 30, s)
    # Right claw: 4x4
    rect(img, 46 + xo, 27, 49 + xo, 30, s)
    # Legs: 4 stubs, 2w x 5h
    for lx in [21, 26, 36, 41]:
        rect(img, lx + xo, 41, lx + 1 + xo, 45, s)


def draw_eyes(img, ex_off=0, ey_off=0, xo=0):
    """Draw 2x2 black eyes with offset for looking direction."""
    rect(img, 25 + ex_off + xo, 28 + ey_off, 26 + ex_off + xo, 29 + ey_off, EYES_BLACK)
    rect(img, 37 + ex_off + xo, 28 + ey_off, 38 + ex_off + xo, 29 + ey_off, EYES_BLACK)


def draw_question(img, x0, y0, color):
    """3x4 '?' pattern."""
    # .#.
    # #.#
    # ..#
    # .#.
    pixel(img, x0 + 1, y0, color)
    pixel(img, x0, y0 + 1, color)
    pixel(img, x0 + 2, y0 + 1, color)
    pixel(img, x0 + 2, y0 + 2, color)
    pixel(img, x0 + 1, y0 + 3, color)


def generate_disconnected():
    """Generate 6 disconnected animation frames."""
    frames = []

    # Frame 1: Neutral pose, eyes looking up-right
    f = new_frame()
    draw_normal_body(f, xo=0)
    draw_eyes(f, ex_off=1, ey_off=-1, xo=0)
    frames.append(f)

    # Frame 2: Head tilts right (body shifts 1px right)
    f = new_frame()
    draw_normal_body(f, xo=1)
    draw_eyes(f, ex_off=1, ey_off=-1, xo=1)
    frames.append(f)

    # Frame 3: Eyes shift left (looking away)
    f = new_frame()
    draw_normal_body(f, xo=1)
    draw_eyes(f, ex_off=-1, ey_off=0, xo=1)
    frames.append(f)

    # Frame 4: Eyes shift back right (looking at icon)
    f = new_frame()
    draw_normal_body(f, xo=1)
    draw_eyes(f, ex_off=1, ey_off=0, xo=1)
    frames.append(f)

    # Frame 5: "?" above head
    f = new_frame()
    draw_normal_body(f, xo=0)
    draw_eyes(f, ex_off=1, ey_off=-1, xo=0)
    draw_question(f, 30, 15, Q_YELLOW)
    frames.append(f)

    # Frame 6: "?" fades, back to neutral
    f = new_frame()
    draw_normal_body(f, xo=0)
    draw_eyes(f, ex_off=1, ey_off=-1, xo=0)
    draw_question(f, 30, 15, Q_FADED)
    frames.append(f)

    return frames


# ---- BLE Icon (Task 7 Part A) ----

def generate_ble_icon():
    """Generate 16x16 Bluetooth rune icon."""
    img = new_frame(16, 16)

    # Bluetooth rune pixel coordinates
    pixels = [
        # Vertical backbone (x=8)
        (8, 2), (8, 3), (8, 4), (8, 5), (8, 6), (8, 7),
        (8, 8), (8, 9), (8, 10), (8, 11), (8, 12),
        # Upper-right arrow: top to mid-right
        (9, 3), (10, 4), (11, 5),
        # Upper-right return: mid-right back to center
        (10, 6), (9, 7),
        # Lower-right arrow: bottom to mid-right
        (9, 11), (10, 10), (11, 9),
        # Lower-right return: mid-right back to center
        (10, 8), (9, 7),
        # Upper-left crossing diagonal
        (5, 5), (6, 6), (7, 7),
        # Lower-left crossing diagonal
        (5, 9), (6, 8), (7, 7),
    ]

    for x, y in pixels:
        pixel(img, x, y, BLE_BLUE)

    return img


# ---- Save helpers ----

def save_frames(frames, output_dir):
    Path(output_dir).mkdir(parents=True, exist_ok=True)
    for i, frame in enumerate(frames):
        path = os.path.join(output_dir, f"frame_{i:02d}.png")
        frame.save(path)
        print(f"  {path}")


def main():
    base = Path(__file__).parent / "exports"

    print("Generating sleeping frames (6)...")
    save_frames(generate_sleeping(), base / "sleeping")

    print("Generating disconnected frames (6)...")
    save_frames(generate_disconnected(), base / "disconnected")

    print("Generating BLE icon (16x16)...")
    ble_dir = base / "ble-icon"
    ble_dir.mkdir(parents=True, exist_ok=True)
    ble = generate_ble_icon()
    ble.save(ble_dir / "frame_00.png")
    print(f"  {ble_dir / 'frame_00.png'}")

    print("\nDone! Now run png2rgb565.py to convert to C headers.")


if __name__ == "__main__":
    main()
