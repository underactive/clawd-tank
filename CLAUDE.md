@AGENTS.md

## Claude Code

- Always check `docs/PLANS.md` at session start for current project status, active plans, and release log.
- Use plan mode and xhigh effort for complex architectural changes.
- Read `ARCHITECTURE.md` before making cross-domain changes.
- Check `docs/exec-plans/active/` for in-progress plans before starting new work.
- Update `docs/QUALITY_SCORE.md` when you ship or significantly change a domain.
- **Do not use the bond-firmware MCP plugin** — use `idf.py` directly from the `firmware/` directory.
- Firmware and host use separate Python environments. Never install host deps (`bleak`, `rumps`) into the ESP-IDF venv or vice versa.
- LVGL is pinned at 9.5.0 — do not translate 8.x examples verbatim.
