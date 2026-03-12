# Code Review: Tasks 1 & 2 — Sprite Designer Tooling

**Files reviewed:**
- `tools/sprite-designer/index.html` (Task 1 — Canvas sprite editor)
- `tools/png2rgb565.py` (Task 2 — PNG→RGB565 converter)

---

## Code Review Summary

Both tools are well-written and appropriate for their scope. The sprite editor is a clean single-file implementation with good UX fundamentals. The Python converter is concise, readable, and generates correct C output. No blocking issues — a few things to clean up.

---

## Critical Issues ⚠️

None.

---

## High Priority Issues 🔴

### `index.html`

**1. `addFrame`/`copyFrame` naming is inverted from button labels (line 151–153)**

The button labeled `+ Duplicate` calls `addFrame()`, and `+ Blank` calls `copyFrame()`. But:
- `addFrame()` (line 529) duplicates the current frame via `cloneGrid(currentGrid())`
- `copyFrame()` (line 538) creates an **empty** frame via `createEmptyGrid()`

The function names are the opposite of what they do. This is a maintenance hazard — any future developer (or the spec-checker) reading the code will be confused.

**Fix:** Rename `addFrame → duplicateFrame` and `copyFrame → addBlankFrame`.

---

**2. `pick` tool saves unnecessary undo state (line 411)**

```js
if (currentTool === 'fill' || currentTool === 'pick') {
    saveUndoState();   // ← pick doesn't modify the canvas!
    applyTool(x, y);
    ...
}
```

`pickColor()` only updates `currentColor`. It never writes to the grid. Saving an undo state here pushes a duplicate of the current canvas state, wasting the undo stack.

**Fix:** Only call `saveUndoState()` when `currentTool === 'fill'`.

---

**3. `updateFrameList()` called every animation frame during playback (line 652)**

```js
playInterval = setInterval(() => {
    currentFrameIdx = (currentFrameIdx + 1) % frames.length;
    render();
    updateFrameList();  // ← rebuilds entire DOM + draws all thumbnails
}, 1000 / fps);
```

`updateFrameList()` destroys and recreates all frame DOM elements including canvas thumbnails on every tick. At 30 FPS with 8 frames, that's 30 full DOM rebuilds/second, each drawing 8 thumbnail canvases. Easily causes jank.

**Fix:** During playback, only update the `active` class rather than rebuilding the list:
```js
// During playback tick:
document.querySelectorAll('.frame-thumb').forEach((el, i) => {
    el.classList.toggle('active', i === currentFrameIdx);
});
```

---

**4. Multi-frame PNG export may be blocked by browser popup blockers (line 705–713)**

`exportAllPNG()` fires one `a.click()` per frame synchronously. Browsers typically allow only the first programmatic download without a delay; subsequent ones are silently blocked by popup/download protection.

**Fix:** Either stagger clicks with `setTimeout(fn, i * 100)` per frame, or export as a ZIP using JSZip (preferred — no popup blocking).

---

### `png2rgb565.py`

**5. Unused `name` parameter in `format_pixel_array` (line 69)**

```python
def format_pixel_array(pixels, name, indent="    "):
```

`name` is accepted but never referenced inside the function. Dead parameter.

**Fix:** Remove the `name` parameter and update the call site at line 105.

---

**6. No validation of `--name` as a C identifier (line 129–131)**

The `--name` value is used directly as a C array prefix. Passing `"my-sprite"`, `"2bad"`, or `"void"` generates syntactically invalid or undefined-behavior C.

**Fix:** Add a quick validation:
```python
import re
if not re.match(r'^[a-zA-Z_][a-zA-Z0-9_]*$', args.name):
    print(f"Error: --name '{args.name}' is not a valid C identifier")
    sys.exit(1)
```

---

## Medium Priority Issues 🟡

### `index.html`

**7. Hardcoded `512` in `render()` instead of `W * PIXEL` (line 247)**

```js
ctx.clearRect(0, 0, 512, 512);
```

W=64, PIXEL=8 are defined as constants. Using the literal `512` here (and in `exportAllPNG` line 694) means changing PIXEL or W requires hunting down these literals.

**Fix:** `ctx.clearRect(0, 0, W * PIXEL, H * PIXEL)`.

---

**8. Grid drawn with 128 individual `stroke()` calls (lines 275–286)**

128 separate `beginPath/moveTo/lineTo/stroke` calls instead of one batched path. At 64x64 this renders fine, but one path is the correct Canvas2D pattern:

```js
ctx.beginPath();
for (let x = 0; x <= W; x++) { ctx.moveTo(x * PIXEL, 0); ctx.lineTo(x * PIXEL, H * PIXEL); }
for (let y = 0; y <= H; y++) { ctx.moveTo(0, y * PIXEL); ctx.lineTo(W * PIXEL, y * PIXEL); }
ctx.stroke();
```

---

### `png2rgb565.py`

**9. `BG_COLOR` exact-match assumes pixel-perfect canvas output (line 43)**

```python
if (r, g, b) == BG_COLOR:  # exact match of #1a1a2e
```

This works correctly for PNGs exported by the sprite editor (which outputs exact hex colors). But if the workflow ever includes any image processing that could shift even 1 channel value (gamma correction, color profile, etc.), this silent transparency detection breaks without warning. Document this assumption prominently in the docstring.

---

**10. `img.getpixel()` called in a nested loop (line 56)**

`getpixel()` has Python-level overhead per call. For 64×64 = 4,096 pixels this is negligible (<1ms), but `img.tobytes()` or `list(img.getdata())` would be idiomatic Pillow. Not a practical issue at this scale.

---

## Low Priority Suggestions 🔵

- **`index.html` line 513:** All custom palette colors are named `"Custom"`. If you add 3 custom colors, the palette shows three identical labels. Adding a counter (`Custom 1`, `Custom 2`) would be a small UX improvement.

- **`index.html` undo clears on frame switch:** `selectFrame()`, `addFrame()`, `deleteFrame()` all clear the undo stack. This means you can't undo a frame deletion. This is a pragmatic trade-off for simplicity, but worth noting in a comment so future maintainers don't accidentally "fix" it.

- **`png2rgb565.py` line 170:** `output_path.write_text(header)` silently overwrites existing headers. A `--force` flag or a brief "Overwriting existing file" warning would be developer-friendly.

---

## Positive Highlights ✨

- **`index.html`:** Flood fill uses an iterative stack (not recursion) — correct choice, avoids call stack overflow on large uniform fills.
- **`index.html`:** `lastDrawn` deduplication during drag strokes is elegant — prevents multiple undo states per drag pass, preserves ergonomic undo granularity.
- **`index.html`:** Right-click erase is a natural UX affordance, well-implemented with the `e.buttons === 2` check in `mousemove`.
- **`index.html`:** `mouseleave` handler mirrors `mouseup` to prevent stuck drawing state — correct defensive coding.
- **`index.html`:** The checkerboard transparency background is properly decoupled from the pixel data and excluded from the export canvas.
- **`png2rgb565.py`:** The `TRANSPARENT_KEY + 1` collision fix is correct and documented. Simple, surgical, and safe.
- **`png2rgb565.py`:** `static const uint16_t* const` for the frame table is the right pattern for ROM data on ESP32 — both pointer and data are const.
- **`png2rgb565.py`:** Graceful Pillow import error with actionable install message (lines 20–25).
- **`png2rgb565.py`:** Per-frame size mismatch detection with a warning (not a hard failure) is the right behavior for a batch converter.

---

## Recommendations

**For `index.html`:**
1. Fix function naming inversion (`addFrame`/`copyFrame`) — this is a maintenance bug waiting to happen
2. Fix playback to avoid full DOM rebuild per animation tick
3. Fix pick tool undo leak
4. Consider export reliability (stagger or ZIP)

**For `png2rgb565.py`:**
1. Remove unused `name` param from `format_pixel_array`
2. Add C identifier validation for `--name`

Overall: solid, usable tools. The Python converter is production-ready after the two cleanups. The sprite editor needs the naming fix and playback optimization before it becomes harder to maintain.
