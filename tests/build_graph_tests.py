"""Exercise production incremental rules with compiler-free Ninja fixtures."""

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BuildGraphTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "build/test-output/build-graph"
        scratch.mkdir(parents=True, exist_ok=True)
        # Retain the small fixture for diagnosis; never recursively delete a path
        # from a Python test outside the C++ scratch-removal policy.
        self.source = Path(tempfile.mkdtemp(prefix="patchy-build-graph-", dir=scratch))
        self.build = self.source / "build"
        (self.source / "src").mkdir()
        (self.source / "resources").mkdir()
        (self.source / "resources/initial.bin").write_text("valid\n", encoding="utf-8")
        (self.source / "src/icons.qrc").write_text(
            '<RCC><qresource><file alias="sample">../resources/initial.bin</file></qresource></RCC>\n',
            encoding="utf-8")
        for name in ("src/source.cpp", "src/header.hpp", "catalog.ts", "catalog.qm", "checker.txt"):
            (self.source / name).write_text("valid\n", encoding="utf-8")
        (self.source / "validate.py").write_text(
            "import pathlib,sys\n"
            "marker=pathlib.Path(sys.argv[1])\n"
            "if any('fail' in pathlib.Path(p).read_text() for p in sys.argv[2:]): sys.exit(1)\n"
            "marker.write_text(str(int(marker.read_text())+1) if marker.exists() else '1')\n",
            encoding="utf-8")
        module = (ROOT / "cmake/patchy_incremental.cmake").as_posix()
        python = Path(sys.executable).as_posix()
        (self.source / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 3.26)
project(BuildGraphFixture NONE)
include("{module}")
file(GLOB PATCHY_BUNDLED_FONT_FILES CONFIGURE_DEPENDS "${{PROJECT_SOURCE_DIR}}/resources/*")
patchy_setup_build_stamp()
patchy_write_input_manifest("${{CMAKE_BINARY_DIR}}/resource-inputs.txt" ${{PATCHY_BUNDLED_FONT_FILES}})
add_custom_command(OUTPUT "${{CMAKE_BINARY_DIR}}/resources.stamp"
  COMMAND "{python}" "${{PROJECT_SOURCE_DIR}}/validate.py" "${{CMAKE_BINARY_DIR}}/resources.count" ${{PATCHY_BUNDLED_FONT_FILES}}
  COMMAND "${{CMAKE_COMMAND}}" -E copy "${{CMAKE_BINARY_DIR}}/resource-inputs.txt" "${{CMAKE_BINARY_DIR}}/resource-inventory.txt"
  COMMAND "${{CMAKE_COMMAND}}" -E touch "${{CMAKE_BINARY_DIR}}/resources.stamp"
  DEPENDS ${{PATCHY_BUNDLED_FONT_FILES}} "${{CMAKE_BINARY_DIR}}/resource-inputs.txt")
file(GLOB inputs CONFIGURE_DEPENDS "${{PROJECT_SOURCE_DIR}}/src/*")
file(GENERATE OUTPUT "${{CMAKE_BINARY_DIR}}/manifest.txt" CONTENT "${{inputs}}\n")
patchy_add_success_check(catalog_check
  STAMP "${{CMAKE_BINARY_DIR}}/catalog.stamp"
  COMMAND "{python}" "${{PROJECT_SOURCE_DIR}}/validate.py" "${{CMAKE_BINARY_DIR}}/catalog.count" "${{PROJECT_SOURCE_DIR}}/catalog.ts"
  DEPENDS ${{inputs}} "${{CMAKE_BINARY_DIR}}/manifest.txt" "${{PROJECT_SOURCE_DIR}}/catalog.ts" "${{PROJECT_SOURCE_DIR}}/validate.py")
patchy_add_success_check(runtime_check
  STAMP "${{CMAKE_BINARY_DIR}}/runtime.stamp"
  COMMAND "{python}" "${{PROJECT_SOURCE_DIR}}/validate.py" "${{CMAKE_BINARY_DIR}}/runtime.count" "${{PROJECT_SOURCE_DIR}}/catalog.qm" "${{PROJECT_SOURCE_DIR}}/checker.txt"
  DEPENDS "${{CMAKE_BINARY_DIR}}/catalog.stamp" "${{PROJECT_SOURCE_DIR}}/catalog.qm" "${{PROJECT_SOURCE_DIR}}/checker.txt" "${{PROJECT_SOURCE_DIR}}/validate.py" "${{CMAKE_BINARY_DIR}}/resources.stamp")
add_custom_command(OUTPUT "${{CMAKE_BINARY_DIR}}/app.bin"
  COMMAND "${{CMAKE_COMMAND}}" -E copy "${{PATCHY_BUILD_STAMP_HEADER}}" "${{CMAKE_BINARY_DIR}}/app.bin"
  DEPENDS "${{PATCHY_BUILD_STAMP_HEADER}}" "${{CMAKE_BINARY_DIR}}/runtime.stamp")
add_custom_target(app ALL DEPENDS "${{CMAKE_BINARY_DIR}}/app.bin")
''', encoding="utf-8")
        self.command("-S", self.source, "-B", self.build, "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ARGS.ninja}")
        self.run_build()

    def command(self, *arguments, success=True):
        result = subprocess.run([ARGS.cmake, *map(str, arguments)], capture_output=True, text=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def run_build(self, *arguments, success=True):
        return self.command("--build", self.build, *arguments, success=success)

    def state(self):
        return tuple((self.build / name).stat().st_mtime_ns for name in (
            "generated/patchy_build_stamp.hpp", "catalog.stamp", "runtime.stamp", "app.bin"))

    def test_noop_and_unrelated_file_preserve_outputs(self):
        before = self.state()
        self.run_build()
        self.assertEqual(before, self.state())
        (self.source / "notes.txt").write_text("unrelated\n", encoding="utf-8")
        self.run_build()
        self.assertEqual(before, self.state())
        self.command("-S", self.source, "-B", self.build)
        self.run_build()
        self.assertEqual(before, self.state(), "unchanged reconfiguration dirtied the build")

    def test_source_header_addition_and_removal_invalidate(self):
        for name in ("src/source.cpp", "src/header.hpp", "src/added.hpp"):
            before = self.state()
            (self.source / name).write_text("changed\n", encoding="utf-8")
            self.run_build()
            self.assertTrue(all(a < b for a, b in zip(before, self.state())), name)
            current = self.state()
            self.run_build()
            self.assertEqual(current, self.state(), name)
        before = self.state()
        (self.source / "src/added.hpp").unlink()
        self.run_build()
        self.assertTrue(all(a < b for a, b in zip(before, self.state())))

    def test_failed_catalog_and_runtime_never_publish_success(self):
        for input_name, stamp in (("catalog.ts", "catalog.stamp"), ("catalog.qm", "runtime.stamp")):
            before = (self.build / "app.bin").stat().st_mtime_ns
            (self.source / input_name).write_text("fail\n", encoding="utf-8")
            self.run_build(success=False)
            self.assertFalse((self.build / stamp).exists())
            self.assertEqual(before, (self.build / "app.bin").stat().st_mtime_ns)
            self.run_build(success=False)
            (self.source / input_name).write_text("valid again\n", encoding="utf-8")
            self.run_build()
            self.assertTrue((self.build / stamp).exists())
            self.assertGreater((self.build / "app.bin").stat().st_mtime_ns, before)

    def test_checker_change_missing_output_and_release_refresh(self):
        before = self.state()
        (self.source / "checker.txt").write_text("changed checker\n", encoding="utf-8")
        self.run_build()
        self.assertEqual(before[:2], self.state()[:2])
        self.assertGreater(self.state()[2], before[2])
        (self.build / "runtime.stamp").unlink()
        self.run_build()
        before = self.state()
        self.run_build("--target", "patchy_refresh_build_stamp")
        self.run_build()
        self.assertGreater(self.state()[0], before[0])
        self.assertEqual(self.state()[1:3], before[1:3])
        self.assertGreater(self.state()[3], before[3])

    def test_configuration_change_and_missing_stamp(self):
        before = self.state()
        self.command("-S", self.source, "-B", self.build, "-DPATCHY_TEST_CONFIG=changed")
        self.run_build()
        self.assertGreater(self.state()[0], before[0])
        self.assertEqual(self.state()[1:3], before[1:3])
        (self.build / "generated/patchy_build_stamp.hpp").unlink()
        self.run_build()
        self.assertTrue((self.build / "generated/patchy_build_stamp.hpp").exists())

    def test_resource_changes_removal_and_external_qrc_reference(self):
        for name in ("resources/initial.bin", "resources/added.bin"):
            before = self.state()
            (self.source / name).write_text("changed resource\n", encoding="utf-8")
            self.run_build()
            self.assertGreater(self.state()[0], before[0])
            self.assertEqual(self.state()[1], before[1])
            self.assertGreater(self.state()[2], before[2])
        before = self.state()
        (self.source / "resources/added.bin").unlink()
        self.run_build()
        self.assertGreater(self.state()[0], before[0])
        self.assertNotIn("added.bin", (self.build / "resource-inventory.txt").read_text())
        self.assertGreater(self.state()[2], before[2])
        external = self.source / "external.svg"
        external.write_text("original\n", encoding="utf-8")
        (self.source / "src/icons.qrc").write_text(
            '<RCC><qresource><file>../external.svg</file></qresource></RCC>\n', encoding="utf-8")
        self.run_build()
        before = self.state()
        external.write_text("changed\n", encoding="utf-8")
        self.run_build()
        self.assertGreater(self.state()[0], before[0], "external QRC resource must refresh the date")
        self.assertEqual(self.state()[1:3], before[1:3])
        before = self.state()
        self.command("-S", self.source, "-B", self.build, "-DCMAKE_OSX_ARCHITECTURES=fixture-arch")
        self.run_build()
        self.assertGreater(self.state()[0], before[0], "platform identity must refresh the date")


class PackagingManifestTests(unittest.TestCase):
    def test_flatpak_manifest_refresh_preserves_source_and_quotes_paths(self):
        scratch = ROOT / "build/test-output/build-graph"
        scratch.mkdir(parents=True, exist_ok=True)
        root = Path(tempfile.mkdtemp(prefix="patchy-manifest-", dir=scratch)) / "source with spaces & \u03b1"
        manifest = root / "packaging/linux/flatpak/com.rtsoft.patchy.yml"
        manifest.parent.mkdir(parents=True)
        (root / "CMakeLists.txt").write_text("project(Fixture NONE)\n", encoding="utf-8")
        original = ("      - -DCMAKE_BUILD_TYPE=Release\n        path: ../../..\n"
                    "        path: ../../../scripts/dev/gegl/common.sh\n")
        manifest.write_text(original, encoding="utf-8")
        spec = importlib.util.spec_from_file_location(
            "prepare_flatpak_manifest", ROOT / "scripts/release/prepare-flatpak-manifest.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        output = root / "build/release.yml"
        module.prepare(root, output, "first")
        first = output.read_text(encoding="utf-8")
        self.assertIn("PATCHY_BUILD_STAMP_REFRESH_TOKEN=first", first)
        path_lines = [line for line in first.splitlines() if "path:" in line]
        self.assertEqual([json.loads(line.split("path:", 1)[1]) for line in path_lines],
                         [root.resolve().as_posix(), root.resolve().as_posix() + "/scripts/dev/gegl/common.sh"])
        module.prepare(root, output, "second")
        self.assertIn("PATCHY_BUILD_STAMP_REFRESH_TOKEN=second", output.read_text(encoding="utf-8"))
        self.assertEqual(original, manifest.read_text(encoding="utf-8"))
        manifest.write_text(original + "        path: ../../..\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            module.prepare(root, output, "ambiguous")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--ninja", required=True)
    ARGS, remaining = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *remaining])
