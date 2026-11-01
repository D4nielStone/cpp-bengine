#!/usr/bin/env python3
"""Run the BGUI suite and produce versioned, machine-readable release reports."""
import argparse
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TIMINGS = ("mutation_ms", "ui_ms", "submit_ms", "wait_ms", "frame_ms", "gpu_ms")
COUNTS = ("quad_commands", "geometry_vertices", "text_measurements", "text_generations")


def percentile(values, fraction):
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lo, hi = math.floor(position), math.ceil(position)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (position - lo)


def summarize(values):
    if not values or any(not math.isfinite(v) or v < 0 for v in values):
        raise ValueError("Benchmark samples must be finite and nonnegative")
    return dict(mean=statistics.mean(values), median=statistics.median(values),
                p95=percentile(values, .95), p99=percentile(values, .99),
                minimum=min(values), maximum=max(values),
                stddev=statistics.pstdev(values), samples=len(values))


def aggregate(runs):
    if not runs or runs[0].get("schema_version") != 1:
        raise ValueError("Unsupported or empty benchmark report")
    environment = {k: v for k, v in runs[0].items() if k != "scenarios"}
    required = ("suite_version", "engine_version", "build_type", "compiler", "gpu_renderer",
                "gpu_vendor", "gl_version", "resolution", "vsync", "synchronized", "warmup_frames")
    if any(key not in environment for key in required):
        raise ValueError("Incomplete benchmark environment metadata")
    names = [s["name"] for s in runs[0]["scenarios"]]
    if not names or len(names) != len(set(names)):
        raise ValueError("Empty or duplicate benchmark scenarios")
    for run in runs:
        if {k: v for k, v in run.items() if k != "scenarios"} != environment:
            raise ValueError("Benchmark environment changed between repetitions")
        if [s["name"] for s in run["scenarios"]] != names:
            raise ValueError("Scenario sets changed between repetitions")
    results = []
    for index, name in enumerate(names):
        definition = {k: v for k, v in runs[0]["scenarios"][index].items() if k != "samples"}
        samples = []
        for run in runs:
            case = run["scenarios"][index]
            if {k: v for k, v in case.items() if k != "samples"} != definition or not case["samples"]:
                raise ValueError("Inconsistent or empty workload: " + name)
            samples.extend(case["samples"])
        metrics = {key: summarize([sample[key] for sample in samples]) for key in TIMINGS + COUNTS}
        if metrics["frame_ms"]["mean"] <= 0 or metrics["quad_commands"]["maximum"] == 0:
            raise ValueError("Benchmark produced no work: " + name)
        results.append(dict(**definition, metrics=metrics,
                            isolated_fps=1000 / metrics["frame_ms"]["mean"]))
    return environment, results


def git(*args, directory=ROOT):
    result = subprocess.run(["git", "-C", str(directory), *args], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def cpu_name():
    if Path("/proc/cpuinfo").exists():
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                return line.split(":", 1)[1].strip()
    return platform.processor() or platform.machine()


def comparison_key(report):
    env = report["environment"]
    result = {key: env.get(key) for key in (
        "suite_version", "build_type", "compiler", "gpu_vendor", "gpu_renderer", "gl_version",
        "resolution", "vsync", "synchronized", "warmup_frames", "cpu", "os", "machine", "font_sha256",
        "font_antialiasing", "font_resolution", "font_scale")}
    result.update(frames_per_repetition=report.get("frames_per_repetition"),
                  repetitions=report.get("repetitions"))
    return result


def compare(report, baseline, threshold):
    if baseline.get("schema_version") != 1 or comparison_key(report) != comparison_key(baseline):
        return {"status": "incompatible", "reason": "Hardware, driver, build or suite configuration differs"}
    previous = {s["name"]: s for s in baseline["scenarios"]}
    if set(previous) != {s["name"] for s in report["scenarios"]}:
        return {"status": "incompatible", "reason": "Scenario sets differ"}
    changes = []
    for case in report["scenarios"]:
        old = previous.get(case["name"])
        if old is None or any(case.get(k) != old.get(k) for k in ("items", "lines_per_text", "mutation")):
            return {"status": "incompatible", "reason": "Workloads differ"}
        value = old["metrics"]["frame_ms"]["median"]
        if not math.isfinite(value) or value <= 0:
            raise ValueError("Baseline frame medians must be finite and positive")
        delta = 100 * (case["metrics"]["frame_ms"]["median"] / value - 1)
        changes.append(dict(scenario=case["name"], frame_median_change_percent=delta,
                            regression=delta > threshold))
    return {"status": "comparable", "threshold_percent": threshold, "changes": changes}


def markdown(report):
    env = report["environment"]
    lines = [f"# BGUI benchmark — {report['release']}", "",
             f"Commit: `{report['commit']}`; BGUI: `{report['bgui_commit']}`; dirty checkout: **{report['dirty']}**.", "",
             f"{env['os']} / {env['cpu']} / {env['gpu_renderer']}", "",
             f"Build: {env['build_type']} ({env['compiler']}); OpenGL: {env['gl_version']}.", "",
             f"1280×720; VSync off; {report['repetitions']} repetitions; {env['warmup_frames']} warmup frames per scenario.", "",
             "Synchronized isolated UI workload: frame time includes mutation, UI update, submission and GPU wait.",
             "GPU time overlaps CPU submission; do not add GPU time to frame time. FPS is workload throughput, not editor/game FPS.",
             "Quad commands are pre-batching commands, not GPU draw calls. Offscreen widgets remain in the UI tree.", "",
             "| Scenario | Frame median ms | p95 ms | p99 ms | UI mean ms | Submit mean ms | GPU mean ms | Isolated FPS | Text measures/frame | Text generations/frame | Quads/frame |",
             "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for case in report["scenarios"]:
        m = case["metrics"]
        values = [m['frame_ms']['median'], m['frame_ms']['p95'], m['frame_ms']['p99'],
                  m['ui_ms']['mean'], m['submit_ms']['mean'], m['gpu_ms']['mean'],
                  case['isolated_fps'], m['text_measurements']['mean'],
                  m['text_generations']['mean'], m['quad_commands']['mean']]
        lines.append("| " + case["name"] + " | " + " | ".join(f"{v:.3f}" for v in values) + " |")
    lines += ["", "Software-rendered CI results measure a CPU/driver workload and must not be compared with physical GPUs.",
              "Timings on shared runners are advisory; repeat on a dedicated machine to confirm regressions."]
    comparison = report.get("comparison")
    if comparison:
        lines += ["", "## Baseline comparison", "", comparison["status"]]
        if comparison["status"] == "comparable":
            for change in comparison["changes"]:
                lines.append(f"- {change['scenario']}: {change['frame_median_change_percent']:+.1f}% median frame time"
                             + (" — regression candidate" if change["regression"] else ""))
        else:
            lines += [comparison["reason"]]
    return "\n".join(lines) + "\n"


def atomic_write(path, contents):
    path = Path(path)
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent, delete=False) as output:
        temporary = Path(output.name)
        try:
            output.write(contents)
            output.flush()
            os.fsync(output.fileno())
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def positive(value):
    value = int(value)
    if not 1 <= value <= 10000:
        raise argparse.ArgumentTypeError("Expected 1..10000")
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--release", default="local")
    parser.add_argument("--label", default=platform.system())
    parser.add_argument("--frames", type=positive, default=120)
    parser.add_argument("--warmup", type=positive, default=30)
    parser.add_argument("--repetitions", type=positive, default=3)
    parser.add_argument("--scenario")
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--regression-percent", type=float, default=20)
    parser.add_argument("--fail-on-regression", action="store_true")
    args = parser.parse_args()
    if not args.label or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_" for c in args.label):
        parser.error("Label must contain only letters, digits, '-' or '_'")
    if not math.isfinite(args.regression_percent) or args.regression_percent < 0:
        parser.error("Regression threshold must be finite and nonnegative")
    if args.fail_on_regression and args.baseline is None:
        parser.error("Regression gating requires --baseline")
    args.output.mkdir(parents=True, exist_ok=True)
    runs = []
    for repetition in range(args.repetitions):
        raw_path = args.output / f"benchmark-{args.label}-raw-{repetition + 1}.json"
        # A failed process must never leave an old successful run in its place.
        with tempfile.TemporaryDirectory(dir=args.output) as directory:
            temporary = Path(directory) / "raw.json"
            command = [str(args.executable.resolve()), "--output", str(temporary.resolve()),
                       "--frames", str(args.frames), "--warmup", str(args.warmup)]
            if args.scenario:
                command += ["--scenario", args.scenario]
            with (args.output / f"benchmark-{args.label}-run-{repetition + 1}.log").open("w") as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=600)
            run = json.loads(temporary.read_text())
            if any(len(case["samples"]) != args.frames for case in run["scenarios"]):
                raise ValueError("Unexpected sample count from benchmark executable")
            runs.append(run)
            atomic_write(raw_path, json.dumps(run, indent=2, allow_nan=False) + "\n")
    environment, scenarios = aggregate(runs)
    environment.update(cpu=cpu_name(), os=platform.platform(), machine=platform.machine(),
                       font_sha256=hashlib.sha256((ROOT / "commons/assets/fonts/HackNerdFont-Regular.ttf").read_bytes()).hexdigest())
    environment["software_rendering"] = any(name in environment["gpu_renderer"].lower() for name in ("llvmpipe", "softpipe", "software"))
    report = dict(schema_version=1, release=args.release, created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  commit=git("rev-parse", "HEAD"), bgui_commit=git("rev-parse", "HEAD", directory=ROOT / "external/cpp-bgui"),
                  backend_commit=git("rev-parse", "HEAD", directory=ROOT / "external/cpp-bgui/backend"),
                  dirty=bool(git("status", "--porcelain", "--untracked-files=no")),
                  executable_sha256=hashlib.sha256(args.executable.read_bytes()).hexdigest(),
                  repetitions=args.repetitions, frames_per_repetition=args.frames,
                  environment=environment, scenarios=scenarios)
    if args.baseline:
        report["comparison"] = compare(report, json.loads(args.baseline.read_text()), args.regression_percent)
    atomic_write(args.output / f"benchmark-{args.label}.json", json.dumps(report, indent=2, allow_nan=False) + "\n")
    atomic_write(args.output / f"benchmark-{args.label}.md", markdown(report))
    print(args.output / f"benchmark-{args.label}.md")
    if args.fail_on_regression:
        comparison = report.get("comparison", {})
        if comparison.get("status") != "comparable":
            raise ValueError("A compatible baseline is required for regression gating")
        if any(change["regression"] for change in comparison["changes"]):
            return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
