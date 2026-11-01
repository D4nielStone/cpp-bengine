"""Contracts for release benchmark aggregation, comparisons and failures."""
import copy
import importlib.util
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("benchmark_runner", ROOT / "tools/benchmarks/run.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def raw():
    samples = [{key: value for key in runner.TIMINGS + runner.COUNTS} for value in (1., 2., 3.)]
    return dict(schema_version=1, suite_version=1, build_type="Release", compiler="GCC",
                gpu_renderer="test GPU", gpu_vendor="test", gl_version="3.3", engine_version="v0.1.1",
                resolution=[1280,720], vsync=False, synchronized=True, warmup_frames=2, scenarios=[dict(
                    name="text", items=10, lines_per_text=1, mutation="none", samples=samples)])


def report():
    environment, scenarios = runner.aggregate([raw()])
    environment.update(cpu="CPU", os="test OS", machine="x64", font_sha256="font")
    return dict(schema_version=1, environment=environment, scenarios=scenarios)


class BenchmarkReportTests(unittest.TestCase):
    def test_percentiles_are_interpolated_and_mean_is_not_mean_fps(self):
        summary = runner.summarize([1., 2., 3., 4.])
        self.assertEqual(summary["median"], 2.5)
        self.assertAlmostEqual(summary["p95"], 3.85)
        self.assertAlmostEqual(summary["p99"], 3.97)
        self.assertEqual(runner.aggregate([raw()])[1][0]["isolated_fps"], 500.)

    def test_repetitions_are_aggregated_without_dropping_samples(self):
        self.assertEqual(runner.aggregate([raw(), raw()])[1][0]["metrics"]["ui_ms"]["samples"], 6)

    def test_invalid_measurements_are_rejected(self):
        for values in ([], [-1.], [math.nan], [math.inf]):
            with self.subTest(values=values), self.assertRaises(ValueError):
                runner.summarize(values)

    def test_environment_mismatch_is_rejected(self):
        changed = raw()
        changed["build_type"] = "Debug"
        with self.assertRaises(ValueError):
            runner.aggregate([raw(), changed])

    def test_missing_duplicate_and_empty_workloads_are_rejected(self):
        for mode in ("empty", "duplicate", "no_samples", "no_draws"):
            data = raw()
            if mode == "empty": data["scenarios"] = []
            elif mode == "duplicate": data["scenarios"] *= 2
            elif mode == "no_samples": data["scenarios"][0]["samples"] = []
            else:
                for sample in data["scenarios"][0]["samples"]: sample["quad_commands"] = 0
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                runner.aggregate([data])

    def test_comparison_requires_same_driver_cpu_build_and_workload(self):
        baseline = report()
        for key in ("gpu_renderer", "cpu", "compiler", "gl_version", "warmup_frames", "suite_version", "font_sha256"):
            current = copy.deepcopy(baseline)
            current["environment"][key] = "different"
            self.assertEqual(runner.compare(current, baseline, 20)["status"], "incompatible")
        current = copy.deepcopy(baseline)
        current["scenarios"][0]["items"] += 1
        self.assertEqual(runner.compare(current, baseline, 20)["status"], "incompatible")

    def test_regression_uses_frame_median_and_threshold(self):
        baseline = report()
        current = copy.deepcopy(baseline)
        current["scenarios"][0]["metrics"]["frame_ms"]["median"] *= 1.5
        change = runner.compare(current, baseline, 20)["changes"][0]
        self.assertAlmostEqual(change["frame_median_change_percent"], 50.)
        self.assertTrue(change["regression"])
        self.assertFalse(runner.compare(current, baseline, 60)["changes"][0]["regression"])

    def test_atomic_output_replaces_whole_report(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.json"
            path.write_text("old")
            runner.atomic_write(path, '{"ok":true}')
            self.assertTrue(json.loads(path.read_text())["ok"])
            self.assertEqual(list(Path(directory).iterdir()), [path])

    def test_unknown_cli_arguments_fail(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/benchmarks/run.py"), "--unknown"],
                                capture_output=True)
        self.assertNotEqual(result.returncode, 0)

    @unittest.skipUnless(os.name == "posix", "Executable test fixture uses a POSIX script")
    def test_runner_writes_reports_and_preserves_failed_process_logs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = root / "fake-benchmark"
            executable.write_text("#!" + sys.executable + "\n" +
                                  "import json,sys\n" +
                                  "from pathlib import Path\n" +
                                  "data=" + repr(raw()) + "\n" +
                                  "Path(sys.argv[sys.argv.index('--output')+1]).write_text(json.dumps(data))\n")
            executable.chmod(0o755)
            output = root / "success"
            command = [sys.executable, str(ROOT / "tools/benchmarks/run.py"),
                       "--executable", str(executable), "--output", str(output), "--label", "Test",
                       "--frames", "3", "--warmup", "2", "--repetitions", "2"]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            generated = json.loads((output / "benchmark-Test.json").read_text())
            self.assertEqual(generated["scenarios"][0]["metrics"]["frame_ms"]["samples"], 6)
            self.assertEqual(len(generated["executable_sha256"]), 64)
            self.assertTrue((output / "benchmark-Test.md").exists())
            baseline = root / "baseline.json"
            generated["scenarios"][0]["metrics"]["frame_ms"]["median"] /= 2
            baseline.write_text(json.dumps(generated))
            gated_command = command + ["--baseline", str(baseline), "--fail-on-regression"]
            gated_command[gated_command.index("--output") + 1] = str(root / "gated")
            gated = subprocess.run(gated_command, capture_output=True, text=True)
            self.assertEqual(gated.returncode, 2, gated.stderr)
            executable.write_text("#!" + sys.executable + "\nimport sys\nprint('fixture failed')\nsys.exit(7)\n")
            failed = root / "failed"
            command[command.index("--output") + 1] = str(failed)
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((failed / "benchmark-Test.json").exists())
            self.assertIn("fixture failed", (failed / "benchmark-Test-run-1.log").read_text())

    def test_release_workflow_builds_runs_and_preserves_reports(self):
        workflow = (ROOT / ".github/workflows/release_installer.yml").read_text()
        self.assertIn("-DBENGINE_BUILD_BENCHMARKS=ON", workflow)
        self.assertIn("tools/benchmarks/run.py", workflow)
        self.assertIn("LIBGL_ALWAYS_SOFTWARE=1", workflow)
        self.assertIn("benchmark-Linux.json", workflow)
        self.assertIn("benchmark-Linux-raw", workflow)


if __name__ == "__main__":
    unittest.main()
