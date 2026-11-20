# Profiler (0.1.5 stabilization)

The editor's **Profiler** dock replaces Editor Diagnostics. The standalone runtime's
old text overlay has been removed. Persisted workspaces still use the canonical
`Editor Diagnostics` identity during save/load, preserving existing dock positions.
The runtime shares the collector but has no dock or remote capture transport.
Use `runtime_app --project <project.bengine> --profile-json <capture.json>` and/or
`--profile-trace <capture.trace.json>` to export the retained history on normal
exit. These options can be combined with the existing `--test-spec` scene runner.
Output paths are resolved from the runtime working directory; build-disabled
profiling produces an empty capture. Abnormal termination does not export.

## Controls and measured data

- Live history: at most 240 frames, with a frametime graph, instantaneous FPS,
  average/minimum/maximum FPS and mean, median, P95/P99 frametimes. Percentiles use
  nearest rank; invalid/nonpositive intervals are excluded from statistics.
- Previous/Next freezes capture for inspection; Pause/resume continues collection.
  Filters match scope name, category or numeric thread ID. The CPU view shows
  start offsets, hierarchy, total/self times and the ten most costly scopes with
  call counts in the inspected frame. It lists at most 100 timeline entries;
  exports retain every stored event.
- Set comparison baseline retains the current history and displays the difference
  in average frametime. Baselines are manual, in memory, without scene/build
  equivalence checks; they are not automated performance regression decisions.
- Export JSON or Chrome Trace writes timestamped files in the project root.
  Chrome Trace exports CPU complete events (`X`, microseconds), thread IDs and
  parent/frame IDs; open in a compatible trace viewer. JSON also includes GPU
  durations, mesh counters and dropped CPU events. Existing paths are refused;
  disk errors are shown in the dock. No project documents are rewritten.
- Alerts flag frames over 33.333 ms and stutters over twice the history median
  with an additional 16.667 ms floor. These are fixed heuristics, not OS hang
  detection, crash diagnosis or proof of a particular performance cause.

CPU scopes measure wall-clock time on the calling thread, including waits. Root
scope sums can exceed frametime with concurrent work. Instrumented sites include
window systems, render, Lua updates (script filename), physics, animation, skeleton
updates, audio updates, BGUI update/layout and render submission, project/scene
loading and asset import. This is explicit RAII instrumentation, not sampling of
all C++ functions. `Bubble::profile_scope` scopes must be stack-bound and destroyed
on their creating thread. Application task names can be supplied as scope names.

GPU timestamp query pairs measure renderer intervals and shadows, combined geometry
and lighting, SSAO/HBAO and post-processing. Queries are polled for availability;
results are read only when ready, without `glFinish` or result-wait loops. Each
renderer/pass owns eight query pairs, tied to the creating GLFW context. A full
queue skips measurement. Results are attached to the originating retained frame.
Unsupported, not-yet-ready, absent and skipped GPU samples are unavailable, not zero.
Only passes that run are listed. Intervals include GPU gaps between submitted
commands; nested passes must not be added to the enclosing Render interval.
Queries may remain pending until that renderer/pass runs again. Chrome Trace has
CPU events only: GPU timestamps are not falsely mapped onto the CPU clock.

Draw counts originate from actual `mesh::draw_geometry` submissions, including
shadow passes and instances. Triangles are submitted indices/3 multiplied by the
instance count. “Index references” are submitted index references, not unique
vertices or post-culling GPU primitive counts. Fullscreen draws, BGUI and other
paths outside mesh drawing are excluded. These counters are not whole-frame draw
counts. Process RSS is read from `/proc/self/statm` on Linux; other platforms show
unavailable. ECS entity/component counts and RSS are **live**, not historical
samples of the frozen frame.

## Configuration, cost and limits

Configure `-DBENGINE_PROFILING=OFF` to remove CPU scope macros, GPU query operations,
frame collection and mesh counter updates. ON is the default. Runtime pause uses
an atomic enable flag. The shared `Bubble::profiler` singleton is the sole collector;
a mutex protects cross-thread recording and snapshots. Names are copied (up to 256
bytes, categories 64); no component references are retained.

At most 4096 CPU events are retained per frame. Overflow is counted and exported.
Scopes spanning a frame boundary remain attached to their starting frame if it is
still in history; work completing after history eviction cannot be retained. The
UI refreshes at 5 Hz and copies retained captures; densely instrumented workloads
can incur noticeable allocation, copying and mutex contention. Collection is
bounded but is not allocation-free or lock-free.

Build `bengine_profiler_benchmark` with `-DBENGINE_BUILD_BENCHMARKS=ON`. It reports
elapsed nanoseconds per scope for 120 frames of 1000 short scopes, including frame
collection, with collection disabled and enabled. It is a collector microbenchmark,
not a gameplay, rendering or multithreaded contention benchmark. Its measurements
must not be presented as frame performance for a real project.

## Outstanding requested capabilities

This implementation is an initial functional replacement, not the full requested
profiling suite. Still outstanding:

- Resource inventory/cache accounting, texture/mesh/buffer byte sizes and rankings,
  VRAM estimates, allocation/deallocation interception and leak heuristics.
- Shader/material transition counters, culling statistics and dedicated batching/
  instancing metrics; complete render submission coverage and isolated lighting.
- Physics body/contact counters and collision substeps; audio backend/mixer timing;
  task scheduler metadata and script setup/collision callback breakdowns.
- Individual panel/dock/Hierarchy timing, separate BGUI layout timing, richer
  interactive timeline/flame charts, configurable alerts and capture triggers.
- Persistent capture import, comparison of equivalent editor/runtime sessions,
  runtime-to-editor transport, automated scene benchmarks and benchmark baselines.
- Contention stress testing and graphical dock validation on a desktop. GPU
  regression tests create a hidden OpenGL 3.3 context and explicitly skip when
  no display/context is available.
