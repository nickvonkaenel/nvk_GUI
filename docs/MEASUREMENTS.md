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

## 2026-09-01 — Style.Push enum-lookup cache (nvk-ReaScripts, Lua side only)

`SHARED/Data/data/style.lua` built its ImGui enum lookups every frame: for each
of the 26 entries in `style.vars`, a fresh `'StyleVar_' .. k` concatenation, a
hash into the `ImGui` table, and a `Style.Scale` call that re-hashed
`style.scalable` — all under `pairs()`, which re-walks the hash part each frame.
Replaced with an ordered array of records (resolved enum, key, vec2 flag,
scalable flag) built once on the first push and walked with `ipairs`.
`Style.Pop`'s count now comes from `#push_list()`, the same source, so the two
cannot drift.

Method: headless Lua 5.4 (same runtime REAPER embeds), `ImGui.PushStyleVar`
stubbed to a near-free counter so only the Lua work is timed. Median of 7
trials × 100k frames. Pre-change implementation taken verbatim from HEAD.

| config.scale | old     | new     | saved            |
|--------------|---------|---------|------------------|
| 1            | 4.76 us | 2.56 us | 2.20 us (46%)    |
| 1.25         | 6.22 us | 3.36 us | 2.85 us (46%)    |

Two corrections to the premise this change started from:

- **The concatenation was not producing garbage.** Lua interns short strings, so
  `'StyleVar_' .. k` re-finds the existing interned string every frame; measured
  allocation is 0 bytes/frame both before and after. The cost was CPU (buffer +
  hash + string-table probe), not GC pressure.
- **The absolute win is small.** ~2 us/frame per window against a Lua frame body
  measured at ~300 us/window in the Phase 1 steady-state run above — roughly
  0.7% of the Lua frame. The change is still worth keeping (it halves the cost
  of the thing it touches, and the code is no worse to read), but it is not a
  frame-rate fix.

### In-REAPER: Lua vs the ReaImGui/nvk_GUI call boundary

The control experiment this change was meant to feed. Harness opens its own
context, loads both style.lua implementations into sandboxes against the real
`ImGui` table, and interleaves four samples per frame so host-load drift hits
each variant equally: old push/pop, new push/pop, 26 raw `PushStyleVar` with
every Lua lookup hoisted out of the loop (= pure boundary), and 60
`PushStyleColor`. 20 inner reps/frame, median of 300 frames.

REAPER 7.78/macOS-arm64, nvk_GUI 0.10.0.5, docked default project. Three runs:

| leg                          | run 1  | run 2  | run 3  |
|------------------------------|--------|--------|--------|
| `Style.Push`+`Pop` old       | 11.40  | 11.30  | 10.36  |
| `Style.Push`+`Pop` new       |  8.00  |  8.00  |  7.30  |
| 26x `PushStyleVar` (boundary)|  6.25  |  6.54  |  5.96  |
| 60x `PushStyleColor`         |  8.85  |  9.10  |  8.34  |

(us per frame, one window.)

- Lua overhead of `Style.Push`: **5.15 -> 1.75 us**, a 66% cut. That is the part
  this change owns, and it tracks the headless 46% once the boundary floor is
  subtracted out.
- **The boundary is now ~80% of what `Style.Push` costs** (78/82/82%). Remaining
  Lua headroom in the whole function is ~1.5 us; the 26 crossings alone cost
  ~6.2 us and no Lua change can touch them.
- Per crossing: style vars ~0.24 us, style colors ~0.15 us. Style vars cost
  more, presumably the vec2 overload and wider arg marshalling.
- The ~86 pushes a frame (60 `Colors.Push` + 26 `Style.Push`) cost **~15 us of
  boundary alone**, per window, before any Lua runs. That is the number worth
  attacking - by cutting crossings (batching, or pushing only what differs from
  the baked-in style), not by tightening Lua.

Harness kept at `/Applications/REAPER/Scripts/nvk_style_bench_tmp/`. One trap
worth recording: it originally found the style-color count by calling
`GetStyleColor` with rising indices under `pcall` until one raised. The `pcall`
catches the error, but the raised ReaImGui error invalidates the context, and
the script then dies silently on its next deferred frame - no error dialog, no
traceback, just a defer loop that never ticks. Enumerate `Col_*` off the `ImGui`
table the way `demo.EachEnum` does instead.
