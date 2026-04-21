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
