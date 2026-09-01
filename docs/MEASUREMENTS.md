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

### Present API (eager first present, added same day)

First-ever window (no saved ini), time from first draw call to the OS
window existing: without Present 3 frames / 41.7 ms; with Present on the
opening frames 2 frames / 17.2 ms. Windows with saved sizes skip the
auto-fit frame and should show after 1. Wired into nvk-ReaScripts'
Loop.End for the first 3 frames and on persistent-mode reopen.

## 2026-08-31 — Phase 2: idle frame-skipping (SetIdleHint)

Same machine. Note: absolute CPU numbers are NOT comparable to the
baseline above (bare REAPER measured 7.0% this session vs 18.2% then —
different project/session state). All comparisons below are same-session
on/off pairs, transport stopped, mouse untouched, 12–15 × 1s `ps` samples.

### Synthetic heavy static window (900×700: 720 text cells + 400 rects)

- hint off: 15.7% · hint on: **8.4%** · bare REAPER: 7.0%.
- Render+present was ~7.3 of the window's ~8.7 points; SetIdleHint removes
  ~85% of the window's total idle cost. The residual ~1.4 points is the
  Lua draw-call submission + NewFrame bookkeeping that deliberately keeps
  running (the skip is render-side only).

### nvk_SIGNAL (docked module set, transport stopped, unfocused)

- idle-skip off: 18.7% · on: **16.2%** (bare REAPER 7.0%).
- −2.5 points: SIGNAL's render cost is fully skipped, but most of its
  idle cost is Lua/native analyzer updates that keep running every frame
  by design. Further SIGNAL idle wins are script-side (skip analyzer
  updates when idle), not renderer-side.

Wired into nvk-ReaScripts' shared Loop.End as a strict opt-in
(`scr.idle_safe = true` at script startup, gated further on unfocused +
transport stopped) and into the persistent-mode dormant keepalive.
SIGNAL and Create do not opt in: they animate on their own in too many
places to hook invalidation reliably, and their render cost was the
small share of their idle CPU anyway (see above).

### nvk_NAVIGATE opt-in + project-update refresh (same day)

Loop.End now also redraws idle-safe scripts on any
`GetProjectStateChangeCount` change (default behavior) and exposes
`Loop.Refresh(frames)` for other external invalidations. Verified with
temporary wake/skip logging in the fork: NAVIGATE unfocused skips ~96%
of frames (safety-net repaints + rare wakes only), and a stream of undo
points drops skips to zero for its duration, resuming after.

NAVIGATE floating 500×400, unfocused, transport stopped: idle_safe off
13.7% → on 13.1%. Its render was never the cost (small window; frame
body is Lua-side) — the win is the principle plus headroom for bigger
windows. Note REAPER's defer loop ran at ~60fps on this machine, so the
IDLE_REPAINT_INTERVAL (30 ticks) safety net fires ~2×/s here.

### Full-cycle skip: WantsFrame (same day)

Render-skip alone left the Lua frame body running, which for NAVIGATE
was most of its idle cost. `WantsFrame` closes that: while an accepted
idle hint stands and no wake is pending it returns false, and the script
skips its entire cycle — no drawing code, no NewFrame, no render. The
hint became sticky (cleared by SetIdleHint(false) or any wake); both
WantsFrame and SetIdleHint use assertValid only, so they keep the
context alive without starting a frame. The safety-net repaint moved to
Context::heartbeat (runs every tick even with no frame), which also
polls viewport PlatformRequest flags so window events wake fully
skipped contexts.

Cadence test (synthetic, mirroring loop.lua): 600 idle cycles → 577
skipped / 23 safety repaints; 240 cycles with an undo-point stream →
239 drawn; idle re-engaged cleanly after. Window content verified
intact on screen while skipping.

NAVIGATE unfocused, transport stopped: idle_safe off 13.1% → full skip
**9.5%** (bare REAPER 7.0%): the script's idle cost drops from +6.1 to
+2.5 points. The residual is REAPER's defer machinery plus 4 cheap
API/keepalive calls per tick (TrackControls.Update, play state, project
state, WantsFrame).
