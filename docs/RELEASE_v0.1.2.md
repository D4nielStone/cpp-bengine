# Bubble Engine v0.1.2

## Performance

- Measure invalidated UI subtrees instead of measuring clean sibling elements.
- Clear leaf layout invalidation after arranging geometry.
- Emit text glyph commands only when they intersect the active clip. Preserve
  partially visible glyphs, text selection, caret positions and editor guides.
- Allocate guide position/indentation arrays only when editor guides are enabled.
- Render the editor's game camera only while its preview is needed, and render
  only the displayed camera in the standalone runtime.
- Skip spotlight shading outside the light cone and reuse existing shadow maps
  without an unnecessary texture-size query at each unchanged resolution.
- Configure Release optimization correctly in the Linux/Windows build helpers.

Local synchronized UI measurements on Intel ADL GT2 reduced the median long-text
frame from 20.56 to 2.18 ms (static) and 70.67 to 13.64 ms (dynamic).
The benchmark used three repetitions and 3,600 measured frames across ten
scenarios. Widget/window timings varied and do not establish a general FPS gain.

## Automated release benchmarks

An opt-in UI benchmark covers static/dynamic text, long documents, widget groups
and overlapping windows. Each tag release generates Linux software-rendering
reports, raw samples and logs alongside engine packages. Reports include frame
percentiles, UI/submission/GPU times, workload counters and environment metadata.
Comparisons require compatible hardware, drivers, builds and workload settings.
Timing regressions are advisory on shared CI runners. See [BENCHMARKS.md](BENCHMARKS.md).

## Included editor, lighting and Lua changes

- Spotlight components with editor controls, serialization, clipboard support,
  Lua bindings and completion, including world position/direction and cone limits.
- Point-light gizmos in the scene viewport.
- Updated lighting examples and demo scene.
- Preserve the public C++ `setUnityApi` entry point as a forwarding alias to
  `setUpApi`; existing integrations continue to compile.

Project/scene format versions remain unchanged. The included demo declares
v0.1.2 as its minimum engine version because it contains spotlight components.

Local validation and benchmark comparisons are recorded in STABILIZATION.md.
Release packages are produced by the existing Linux/Windows tag workflow;
software-rendered UI throughput is not a measurement of editor/game FPS.
