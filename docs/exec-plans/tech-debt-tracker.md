# Tech Debt Tracker

Known technical debt, tracked as inventory. Items here should be addressed by targeted cleanup tasks on a regular cadence — not accumulated for a "big refactor."

## Format

```
### <short title>
- **Domain:** which domain is affected
- **Grade impact:** what quality grade this drags down
- **Severity:** low | medium | high
- **Added:** YYYY-MM-DD
- **Notes:** context for why this exists and what fixing looks like
```

## Active debt

### firmware/test Makefile dropped ASan (macOS 26 toolchain bug)
- **Domain:** `firmware/test`
- **Grade impact:** Reduces runtime memory-safety coverage in host-side C tests
- **Severity:** medium
- **Added:** 2026-04-21
- **Notes:** Apple Clang on macOS 26.x (Tahoe) deadlocks inside `AsanInitFromRtl` during `libsystem_malloc`'s initializer — the ASan malloc hook re-enters `AsanInitFromRtl` through `_Block_copy → malloc`, spins on `StaticSpinMutex::LockSlow`, and never reaches `main()`. Confirmed via `sample(1)` against a running test binary. `make test` previously ran with `-fsanitize=address,undefined`; now only `-fsanitize=undefined`. UBSan still catches signed-overflow / null-deref / alignment bugs. Re-add `,address` once Apple ships a fixed asan runtime (or switch to Homebrew clang with LLVM asan, which tracks upstream more closely).

### fnk0104 display orientation flags not verified on hardware
- **Domain:** `firmware/board-port-fnk0104`
- **Grade impact:** Prevents the port from reaching grade B until bring-up confirms
- **Severity:** medium
- **Added:** 2026-04-21
- **Notes:** `BOARD_LCD_SWAP_XY`, `BOARD_LCD_MIRROR_X`, `BOARD_LCD_MIRROR_Y`, `BOARD_LCD_RGB_ORDER_BGR` in `board_config.h` are best-guess values derived from `pixel-agents-esp32` (which uses TFT_eSPI's `setRotation(1)`). ILI9341 modules vary in RGB vs BGR order and the MADCTL translation to `esp_lcd_panel_swap_xy`/`esp_lcd_panel_mirror` can be off by 180°. First flash will reveal whether the image is upside-down or has swapped red/blue; adjust macros accordingly.

### fnk0104 touch: tap anywhere = clear all, not per-card dismiss
- **Domain:** `firmware/board-port-fnk0104`
- **Grade impact:** UX polish, not correctness
- **Severity:** low
- **Added:** 2026-04-21
- **Notes:** Port chose parity with the C6 BOOT button (tap = clear all) as the MVP. A follow-up pass should hit-test touch coordinates against the notification panel so tap-on-card dismisses only that card, matching a more natural mental model. `esp_lcd_touch_get_coordinates` already returns (x, y) in display coordinates; the hit-test logic belongs in `touch.c` or `ui_manager.c`.

### fnk0104 star distribution not redistributed for 240-row display
- **Domain:** `firmware/scene`
- **Grade impact:** Visual polish
- **Severity:** low
- **Added:** 2026-04-21
- **Notes:** Stars in `scene.c:star_cfg[]` are placed at y=5–30, which looks correct on the 172-row C6 but clusters tightly at the top on the 240-row fnk0104 (12% of sky instead of 17%). Either redistribute with y values scaled to `SCENE_HEIGHT`, or add a second tier of stars for the extra sky space.

### fnk0104 flash: 8 MB partition on a 16 MB-flash chip
- **Domain:** `firmware/platform`
- **Grade impact:** Low — wastes flash but doesn't break anything
- **Severity:** low
- **Added:** 2026-04-21
- **Notes:** `partitions.csv` still uses the single 8 MB factory-app layout from the C6. The fnk0104 has 16 MB. Revisit when adding OTA, embedded asset bundles, or anything that actually needs the extra room. Until then, deferring avoids a partition-table rewrite and an OTA discussion this port didn't need.

## Resolved debt

(none)

## Process

- When you discover tech debt during a task, add it here rather than fixing it inline (unless the fix is trivial and scoped to your current change).
- Cleanup tasks should reference the specific item they resolve.
- Move resolved items to the "Resolved" section with the date and PR/commit.
