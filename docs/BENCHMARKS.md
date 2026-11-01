# Release performance benchmarks

`bengine_ui_benchmark` exercises the BGUI version linked into Bubble Engine.
It is an opt-in executable, independent of editor projects and saved UI state.
It never opens, modifies or saves a game project. Python 3 is needed only for
aggregation; the executable uses the engine's existing GLFW/OpenGL/FreeType stack.

## Run locally

From the repository root on Linux:

```sh
cmake --preset performance
cmake --build --preset performance --target bengine_ui_benchmark
python3 tools/benchmarks/run.py \
  --executable out/performance/tools/benchmarks/bengine_ui_benchmark \
  --output out/benchmarks/local --release local --label Linux
```

For another build/generator, configure with `-DBENGINE_BUILD_BENCHMARKS=ON`,
use an optimized configuration, and pass its executable path to the runner.
On Windows multi-configuration generators, build with `--config Release` and
use `out/tools/benchmarks/Release/bengine_ui_benchmark.exe` when building in `out`.
The Windows executable has not been validated in this investigation.

A graphical display and OpenGL 3.3 timer queries are required. For headless CI:

```sh
xvfb-run -a env LIBGL_ALWAYS_SOFTWARE=1 python3 tools/benchmarks/run.py \
  --executable out/performance/tools/benchmarks/bengine_ui_benchmark \
  --output out/benchmarks/software --release local --label Software
```

Defaults: 30 warmup frames, 120 measured frames per case, three repetitions.
Use `--frames`, `--warmup`, `--repetitions` to change them. For a quick smoke test:

```sh
python3 tools/benchmarks/run.py \
  --executable out/performance/tools/benchmarks/bengine_ui_benchmark \
  --output out/benchmarks/smoke --frames 3 --warmup 1 --repetitions 1 \
  --scenario text_dynamic_100
```

## Workloads and interpretation

All workloads use the bundled Hack Nerd Font at resolution 40, scale 0.3,
the BGUI dark theme, a hidden 1280×720 OpenGL window, VSync disabled and a
stationary cursor outside the UI. No profiling overlay is enabled. Font loading,
UI construction and first frames are excluded by warmup. Font antialiasing is explicitly enabled. The initial suite has ten deterministic scenarios:

- 100 and 1,000 static text elements.
- 100 and 1,000 text elements with **only the first text** changed each frame.
- One static or dynamically changed text containing 2,000 UTF-8 lines.
- 100 and 1,000 widget groups, each containing a button and checkbox.
- Four and 32 overlapping floating windows, each containing text, a button
  and checkbox. Windows use a fixed position grid to keep visibility reproducible.

Items outside the viewport deliberately remain in the tree. This distinguishes
render clipping from traversal and layout costs. Item count is the workload size,
not the number of visible widgets. Counters for text measurement and generation
instrument the explicit text elements through a subclass; they do not count
internal labels inside stock widgets/window headers. The subclass delegates
all behavior to BGUI and does not alter the library.

Each frame records milliseconds for mutation, `bgui::on_update`, OpenGL
submission, completion wait and total synchronized frame time. An OpenGL elapsed
time query separately records GPU time. Every measured frame waits for GPU
completion: this sacrifices pipeline overlap to bound the measured work and
excludes swap/compositor time. It is **not** a measurement of interactive editor
FPS. GPU time overlaps submission/wait and must not be added to CPU frame time.
Driver scheduling and software rendering also affect the query.

The report includes mean, median, p95, p99, standard deviation, min/max and sample
counts. Isolated FPS equals `1000 / mean(frame_ms)`, not the mean of reciprocals.
Quad commands are measured before backend batching: they are **not GPU draw
calls**. Geometry vertices and text work counts help explain timing changes.
Raw frame samples and execution logs are retained alongside JSON and Markdown
summaries. Reports are replaced atomically only after successful aggregation;
a failed invocation exits nonzero and its logs identify the failed repetition.
Use a fresh output directory when you need to retain successive local runs.

## Comparisons and release integration

```sh
python3 tools/benchmarks/run.py \
  --executable out/performance/tools/benchmarks/bengine_ui_benchmark \
  --output out/benchmarks/candidate --label Linux \
  --baseline out/benchmarks/local/benchmark-Linux.json
```

Comparison requires matching CPU, OS, architecture, GPU/driver, compiler,
configuration, resolution, font hash/settings, warmup, sample counts,
synchronization and suite version,
as well as matching workloads. A mismatch is reported explicitly instead of
producing a misleading percentage. Commits are expected to differ. The binary
hash, engine/BGUI/backend commits, dirty checkout flag and timestamp are retained.
Raw samples and source revisions are necessary evidence; a dirty local build
is not a release baseline.

By default, increases over 20% in median synchronized frame time are advisory
regression candidates. `--regression-percent` changes this threshold.
`--fail-on-regression` opts into gating (exit 2 for a regression, error if no
compatible baseline exists). Confirm timing changes on a quiet, dedicated host:
shared GitHub runners and different CPUs are not a stable performance lab.
Do not compare llvmpipe/software results with physical GPU results.

The tag-triggered `release_installer.yml` builds the benchmark in the Linux
Release job, runs it under Xvfb/llvmpipe, tries to download the previous release's
`benchmark-Linux.json`, and writes the report into the job summary. Missing
previous reports start a new baseline. Incompatible environments remain visible
in the report; noisy timing regressions do not block releases by default.
Execution failures do block publishing, so a missing report cannot look like
successful validation.

Each release receives `benchmark-Linux.json`, `benchmark-Linux.md` and
`benchmark-Linux-raw.zip` beside the engine packages. A separate CI artifact
preserves evidence even if a later packaging step fails. Windows release
packaging is unchanged; native Windows benchmark execution is not automated yet.

When changing workload sizes, font, default rendering settings, timing semantics
or counters, increment `suite_version` in the executable before comparing new
results. Keep schema changes explicit and update the report tests. The benchmark
is not registered as a mandatory graphical CTest: ordinary headless unit tests
can run without a display. Aggregation/comparison tests run through CTest as
`benchmark_reports`.
