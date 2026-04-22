# Quality Score

Tracks the current quality grade of each domain and architectural layer. Updated as domains are built or change significantly. Agents and humans use this to prioritize cleanup and investment.

## Grading scale

| Grade | Meaning                                                    |
|-------|------------------------------------------------------------|
| A     | Well-tested, documented, agent-legible, no known debt      |
| B     | Functional and tested, minor gaps in docs or edge cases    |
| C     | Works but has known debt, missing tests, or unclear naming |
| D     | Fragile, undertested, or structurally problematic          |
| F     | Broken or placeholder only                                 |

## Domain grades

| Domain              | Grade | Notes                                                | Last reviewed |
|---------------------|-------|------------------------------------------------------|---------------|
| `firmware/main`     | —     | Ungraded                                             | —             |
| `firmware/ble`      | —     | Ungraded                                             | —             |
| `firmware/scene`    | —     | Ungraded                                             | —             |
| `firmware/notif`    | —     | Ungraded — has C unit tests under `firmware/test/`   | —             |
| `firmware/platform` | —     | Ungraded                                             | —             |
| `firmware/board-port-fnk0104` | C     | Ported and verified: both `esp32c6` and `esp32s3` targets build clean, 23 firmware C tests pass under UBSan, 222 host pytest tests pass. BGR element order confirmed at bring-up. ILI9341 driver, FT6336G touch, battery ADC + HUD, PSRAM sprite buffers wired in behind Kconfig guards. Orientation flags (`swap_xy`, `mirror_x`, `mirror_y`), per-card touch dismiss, and 240-row star distribution are still open follow-ups in `tech-debt-tracker.md` — holds grade at C until those are cleared on hardware. | 2026-04-21    |
| `simulator`         | —     | Ungraded                                             | —             |
| `host/notify`       | —     | Ungraded — stdlib-only hook handler                  | —             |
| `host/daemon`       | —     | Ungraded — has pytest suite under `host/tests/`      | —             |
| `host/menubar`      | —     | Ungraded                                             | —             |
| `tools/sprite`      | —     | Ungraded                                             | —             |

## Cross-cutting grades

| Concern                | Grade | Notes                                             | Last reviewed |
|------------------------|-------|---------------------------------------------------|---------------|
| BLE protocol           | —     | Ungraded — versioned via GATT characteristic      | —             |
| Session state model    | —     | Ungraded — persisted to `~/.clawd-tank/sessions.json` | —         |
| Logging                | —     | Ungraded — `ESP_LOG*` (firmware) + Python logging (host) | —      |
| Build / packaging      | —     | Ungraded — py2app + static SDL2 for the .app bundle | —          |

## Process

- Review and update grades when a domain ships or changes significantly.
- A domain at grade C or below should have an entry in [exec-plans/tech-debt-tracker.md](exec-plans/tech-debt-tracker.md).
- Background cleanup tasks target the lowest-graded domains first.
