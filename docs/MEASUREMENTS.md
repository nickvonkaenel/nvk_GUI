# Performance measurements

Method and numbers per FORK_PLAN.md §9. All timings via `reaper.time_precise()`
inside the measured script; CPU/RSS via `ps` sampled once per second from a
shell. Rounded, single-run — treat as magnitudes, not statistics.

## 2026-08-31 — after Phase 1 (shared GPU device + shared font atlas)

Machine: Apple M3 Max, macOS 15, REAPER 7.78 (portable), Metal renderer.
Workload: 5 contexts, each loading the 5 nvk house fonts
(nvk_SANS ×3, nvk_MONO, icons.otf) and drawing text at 3 sizes.
Stock ReaImGui (per-context atlas/device) vs the fork, same REAPER session.

### Script open: context + 5 fonts + first text-heavy frame (ms)

| leg                    | stock              | fork               |
|------------------------|--------------------|--------------------|
| open 1 (cold session)  | 23.5 + 0.0 + 4.1   | 15.9 + 0.0 + 6.8   |
| opens 2–5 (avg)        |  7.9 + 0.0 + 3.5   |  7.1 + 0.0 + 1.2   |
| reopen after close     | 11.6 + 0.0 + 4.5   |  9.3 + 0.0 + 0.9   |

Columns are ctx-create + font-create + first-frame. Font *creation* is free
in both (lazy). The first-frame column is where rasterization lives: stock
pays 2.5–5.2 ms on every open forever; the fork pays once per session
(6.8 ms bakes all 5 faces × 3 sizes) and then 0.7–0.9 ms — reopen first
frame is ~5× faster. Context creation (~7–9 ms: imgui init, ini load,
screenset) dominates both and is untouched so far.

### Steady state / memory / idle CPU (5 live windows)

- Steady frame body (Lua side, all 5 windows): stock 1.5 ms, fork 1.6 ms —
  parity, as predicted (1.92 bakes lazily either way).
- RSS: stock 477 MB, fork 463 MB (−14 MB: duplicate atlases + pipelines).
  Grows with glyph coverage; latin-only atlases are small.
- Idle CPU, 5 small text windows: stock 16.7%, fork 16.5% — indistinguishable
  from bare REAPER (18.2%), i.e. small idle windows cost ~nothing measurable.

### Phase 2 baseline (idle frame-skipping justification)

- Bare REAPER, default project, idle: **18.2%** CPU.
- REAPER + nvk_SIGNAL (docked module set), transport stopped: **25.8%**.

→ The real always-open tool set adds ~7.6 points at idle. That, not
synthetic text windows, is what Phase 2 should be measured against
(before/after, same project, transport stopped).
