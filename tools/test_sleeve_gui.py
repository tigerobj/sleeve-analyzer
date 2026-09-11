import json
import os
import tempfile
import unittest
from pathlib import Path

import sleeve_gui


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
WORKSPACE_ROOT = REPOSITORY_ROOT.parent


class SleeveGuiGeneratorTests(unittest.TestCase):
    def test_bulge_axial_extremum(self):
        vertices = [
            sleeve_gui.Vertex((0.0, 0.0), 1.0),
            sleeve_gui.Vertex((2.0, 0.0)),
        ]
        self.assertAlmostEqual(
            sleeve_gui.minimum_axial_projection(vertices, (0.0, 0.0),
                                                (0.0, 1.0)),
            -1.0, places=8)

    def test_classic_polyline_fixture(self):
        fixture = self._fixture("M-Q0419A-*.dxf")
        result = self._generate(fixture)
        self.assertEqual(result.analysis.source.source_kind, "POLYLINE")
        self.assertAlmostEqual(result.analysis.sleeve_length_mm, 203.573,
                               delta=0.2)
        self.assertAlmostEqual(result.analysis.cuff_width_mm, 330.200,
                               delta=0.2)

    def test_lwpolyline_fixture(self):
        fixture = self._fixture("S-R1126-*.dxf")
        result = self._generate(fixture)
        self.assertEqual(result.analysis.source.source_kind, "LWPOLYLINE")
        self.assertAlmostEqual(result.requested_length_mm, 627.0, places=6)

    def test_preview_uses_sleeve_analyzer_geometry(self):
        fixture = self._fixture("M-Q0419A-*.dxf")
        document = sleeve_gui.read_dxf(fixture)
        analysis = sleeve_gui.find_sleeve(document)
        preview = sleeve_gui.build_preview(
            analysis, document.unit_scale_mm, 627.0, 225.17)

        self.assertEqual(preview.point1, analysis.point1)
        self.assertEqual(preview.point2, analysis.point2)
        self.assertEqual(preview.point3, analysis.point3)
        self.assertEqual(preview.axis_start, analysis.point3)
        self.assertEqual(preview.axis_end, analysis.cuff_center)
        self.assertEqual(len(preview.upper_side), 2)
        self.assertEqual(len(preview.lower_side), 2)
        self.assertAlmostEqual(
            sleeve_gui.distance(preview.target_cuff_upper,
                                preview.target_cuff_lower)
            * document.unit_scale_mm,
            225.17, places=6)

    def test_preview_transform_contains_every_geometry_point(self):
        fixture = self._fixture("M-Q0419A-*.dxf")
        document = sleeve_gui.read_dxf(fixture)
        preview = sleeve_gui.build_preview(
            sleeve_gui.find_sleeve(document), document.unit_scale_mm,
            627.0, 225.17)
        transform = sleeve_gui.make_preview_transform(preview, 560, 520)

        for point in sleeve_gui.preview_points(preview):
            screen_x, screen_y = sleeve_gui.preview_to_screen(transform, point)
            self.assertGreaterEqual(screen_x, 0.0)
            self.assertLessEqual(screen_x, 560.0)
            self.assertGreaterEqual(screen_y, 0.0)
            self.assertLessEqual(screen_y, 520.0)

    def _generate(self, fixture: Path) -> sleeve_gui.GenerationResult:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory) / "long_sleeve.dxf"
            result = sleeve_gui.generate_file(
                fixture, output, 627.0, 225.17)
            self.assertTrue(output.exists())
            document = sleeve_gui.read_dxf(output)
            self.assertIsNotNone(result.analysis.seam_allowance)
            self.assertEqual(
                {polyline.layer for polyline in document.polylines},
                {"LONG_SLEEVE_CUT", "LONG_SLEEVE_SEAM"})
            inner = next(polyline for polyline in document.polylines
                          if polyline.layer == "LONG_SLEEVE_SEAM")
            outer = next(polyline for polyline in document.polylines
                          if polyline.layer == "LONG_SLEEVE_CUT")
            points = sleeve_gui.sampled_closed(inner.vertices)
            projections = [sleeve_gui.dot(
                sleeve_gui.sub(point, result.analysis.point3),
                result.analysis.axis) for point in points]
            self.assertAlmostEqual(
                (max(projections) - min(projections))
                * document.unit_scale_mm,
                627.0, places=6)
            self.assertAlmostEqual(
                sleeve_gui.distance(inner.vertices[-1].point,
                                    inner.vertices[-2].point)
                * document.unit_scale_mm,
                225.17, places=6)
            inner_cuff = sleeve_gui.scale(
                sleeve_gui.add(inner.vertices[-1].point,
                               inner.vertices[-2].point), 0.5)
            outer_cuff = sleeve_gui.scale(
                sleeve_gui.add(outer.vertices[-1].point,
                               outer.vertices[-2].point), 0.5)
            self.assertAlmostEqual(
                sleeve_gui.dot(sleeve_gui.sub(outer_cuff, inner_cuff),
                               result.analysis.axis)
                * document.unit_scale_mm,
                19.05, places=6)

            source_cap = sleeve_gui.expanded_cap_route(
                result.analysis.seam_allowance, result.analysis,
                result.analysis.cap_vertices[0].point,
                result.analysis.cap_vertices[-1].point)
            generated_points = [vertex.point for vertex in outer.vertices]
            for point in source_cap[1:-1]:
                self.assertTrue(
                    any(sleeve_gui.distance(point, generated) < 1.0e-7
                        for generated in generated_points),
                    f"original sleeve-cap detail was not retained: {point}")
            return result

    @staticmethod
    def _fixture(pattern: str) -> Path:
        matches = [path for path in WORKSPACE_ROOT.glob(pattern)
                   if "python_long" not in path.name]
        if not matches:
            raise AssertionError(f"fixture not found: {pattern}")
        return sorted(matches)[0]


class SizeTableTests(unittest.TestCase):
    def _write(self, directory: str, data: dict) -> Path:
        path = Path(directory) / "sleeve_sizes.json"
        path.write_text(json.dumps(data, ensure_ascii=False, indent=2),
                        encoding="utf-8")
        return path

    def test_filename_size_detection_uses_independent_tokens_and_aliases(self):
        available = ("12", "XS", "S", "M", "L", "XL", "2L", "3L",
                     "4L", "5L")
        self.assertEqual(
            sleeve_gui.detect_size_from_filename(
                "M-Q0419A-袖X2.dxf", available).size,
            "M")
        self.assertEqual(
            sleeve_gui.detect_size_from_filename("pattern_XS_v2.dxf",
                                                 available).size,
            "XS")
        self.assertEqual(
            sleeve_gui.detect_size_from_filename("裁片-3XL-長袖.dxf",
                                                 available).size,
            "3L")
        for alias, expected in (("2XL", "2L"), ("4XL", "4L"),
                                ("5XL", "5L")):
            self.assertEqual(
                sleeve_gui.detect_size_from_filename(
                    f"裁片-{alias}-長袖.dxf", available).size,
                expected)
        self.assertIsNone(
            sleeve_gui.detect_size_from_filename("patternQ0419A12B.dxf",
                                                 available).size)

    def test_filename_size_detection_reports_conflict_and_unknown(self):
        available = ("XS", "M", "XL")
        conflict = sleeve_gui.detect_size_from_filename("M-XL.dxf",
                                                        available)
        self.assertEqual(conflict.status, "conflict")
        self.assertIn("M", conflict.reason)
        unknown = sleeve_gui.detect_size_from_filename("Q0419A-袖X2.dxf",
                                                       available)
        self.assertEqual(unknown.status, "unknown")
        self.assertIn("無法", unknown.reason)

    def test_legacy_zero_cuff_uses_standard_without_rewriting_json(self):
        data = {
            "M": {
                "default": "short",
                "metadata": {"keep": True},
                "variants": {
                    "standard": {"sleeve_length_mm": 627,
                                  "cuff_width_mm": 225.17,
                                  "future": "preserve"},
                    "short": {"sleeve_length_mm": 610,
                               "cuff_width_mm": 0},
                },
            }
        }
        with tempfile.TemporaryDirectory() as directory:
            path = self._write(directory, data)
            before = path.read_bytes()
            table = sleeve_gui.SizeTable(path)
            values = table.effective_values("M", "short")
            self.assertEqual(values.cuff_width_mm, 225.17)
            self.assertEqual(values.cuff_source, "同尺寸 standard")
            self.assertEqual(path.read_bytes(), before)
            self.assertEqual(table.to_dict()["M"]["metadata"],
                             {"keep": True})
            self.assertEqual(table.to_dict()["M"]["variants"]["standard"]
                             ["future"], "preserve")

    def test_known_root_metadata_is_preserved(self):
        data = {
            "_meta": {"owner": "pattern-team"},
            "M": {"default": "standard", "variants": {
                "standard": {"sleeve_length_mm": 627,
                              "cuff_width_mm": 225.17}}}}
        with tempfile.TemporaryDirectory() as directory:
            path = self._write(directory, data)
            table = sleeve_gui.SizeTable(path)
            self.assertEqual(table.sizes(), ("M",))
            table.add_variant("M", "short", 610, 220)
            table.save()
            self.assertEqual(json.loads(path.read_text(encoding="utf-8"))
                             ["_meta"], {"owner": "pattern-team"})

    def test_crud_default_and_cancel_clone(self):
        data = {
            "M": {"default": "standard", "variants": {
                "standard": {"sleeve_length_mm": 627,
                              "cuff_width_mm": 225.17}}}}
        with tempfile.TemporaryDirectory() as directory:
            path = self._write(directory, data)
            table = sleeve_gui.SizeTable(path)
            working = table.clone()
            working.add_variant("M", "short", 610, 220)
            working.set_default("M", "short")
            working.rename_variant("M", "short", "短版")
            working.copy_variant("M", "短版", "複製", 605, 218)
            working.delete_variant("M", "複製")
            self.assertTrue(working.dirty)
            self.assertEqual(table.variants("M"), ("standard",))
            with self.assertRaises(sleeve_gui.SizeDataError):
                working.delete_variant("M", "短版")
            working.delete_variant("M", "短版", replacement="standard")
            self.assertEqual(working.default_variant("M"), "standard")
            working.add_size("L", 640, 230)
            working.copy_size("L", "XL")
            working.rename_size("XL", "加大型")
            self.assertIn("加大型", working.sizes())
            working.delete_size("加大型")

    def test_crud_rejects_blank_duplicate_and_non_positive_new_values(self):
        data = {
            "M": {"default": "standard", "variants": {
                "standard": {"sleeve_length_mm": 627,
                              "cuff_width_mm": 225.17}}}}
        with tempfile.TemporaryDirectory() as directory:
            table = sleeve_gui.SizeTable(self._write(directory, data))
            with self.assertRaises(sleeve_gui.SizeDataError):
                table.add_variant("M", " ", 600, 220)
            with self.assertRaises(sleeve_gui.SizeDataError):
                table.add_variant("M", "standard", 600, 220)
            with self.assertRaises(sleeve_gui.SizeDataError):
                table.add_variant("M", "bad", float("nan"), 220)
            with self.assertRaises(sleeve_gui.SizeDataError):
                table.add_variant("M", "bad", 600, 0)

    def test_size_names_are_case_insensitively_unique(self):
        data = {
            "M": {"default": "standard", "variants": {
                "standard": {"sleeve_length_mm": 627,
                              "cuff_width_mm": 225.17}}},
            "m": {"default": "standard", "variants": {
                "standard": {"sleeve_length_mm": 627,
                              "cuff_width_mm": 225.17}}},
        }
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(sleeve_gui.SizeDataError):
                sleeve_gui.SizeTable(self._write(directory, data))

    def test_save_creates_backup_and_detects_external_modification(self):
        data = {
            "M": {"default": "standard", "variants": {
                "standard": {"sleeve_length_mm": 627,
                              "cuff_width_mm": 225.17}}}}
        with tempfile.TemporaryDirectory() as directory:
            path = self._write(directory, data)
            table = sleeve_gui.SizeTable(path)
            table.update_variant("M", "standard", 628, 225.17)
            table.save()
            backup = Path(str(path) + ".bak")
            self.assertTrue(backup.exists())
            self.assertEqual(json.loads(backup.read_text(encoding="utf-8")),
                             data)
            path.write_text(json.dumps({"external": True}), encoding="utf-8")
            table.update_variant("M", "standard", 629, 225.17)
            with self.assertRaises(sleeve_gui.SizeDataConflictError):
                table.save()

    def test_corrupt_or_missing_json_error_includes_path_and_reason(self):
        with tempfile.TemporaryDirectory() as directory:
            missing = Path(directory) / "missing.json"
            with self.assertRaises(sleeve_gui.SizeDataError) as missing_error:
                sleeve_gui.SizeTable(missing)
            self.assertIn(str(missing), str(missing_error.exception))
            corrupt = Path(directory) / "corrupt.json"
            corrupt.write_text("{not json", encoding="utf-8")
            with self.assertRaises(sleeve_gui.SizeDataError) as corrupt_error:
                sleeve_gui.SizeTable(corrupt)
            self.assertIn(str(corrupt), str(corrupt_error.exception))
            self.assertIn("JSON", str(corrupt_error.exception))

    def test_size_order_places_custom_sizes_after_standard_order(self):
        data = {
            "custom": {"default": "standard", "variants": {
                "standard": {"sleeve_length_mm": 600,
                              "cuff_width_mm": 200}}},
            "XL": {"default": "standard", "variants": {
                "standard": {"sleeve_length_mm": 600,
                              "cuff_width_mm": 200}}},
            "12": {"default": "standard", "variants": {
                "standard": {"sleeve_length_mm": 600,
                              "cuff_width_mm": 200}}},
            "M": {"default": "standard", "variants": {
                "standard": {"sleeve_length_mm": 600,
                              "cuff_width_mm": 200}}},
        }
        with tempfile.TemporaryDirectory() as directory:
            table = sleeve_gui.SizeTable(self._write(directory, data))
            self.assertEqual(table.sizes(), ("12", "M", "XL", "custom"))

    def test_m_default_and_zero_cuff_short_long_values(self):
        table = sleeve_gui.SizeTable(
            REPOSITORY_ROOT / "plugins" / "sleeveanalyzer" /
            "sleeve_sizes.json")
        self.assertEqual(table.default_variant("M"), "standard")
        standard = table.effective_values("M", "standard")
        self.assertEqual((standard.sleeve_length_mm, standard.cuff_width_mm),
                         (627.0, 225.17))
        for variant, expected_length in (("short", 610.0), ("long", 645.0)):
            values = table.effective_values("M", variant)
            self.assertEqual(values.sleeve_length_mm, expected_length)
            self.assertEqual(values.cuff_width_mm, 225.17)
            self.assertEqual(values.cuff_source, "同尺寸 standard")

    def test_added_variant_survives_save_and_restart(self):
        data = {
            "M": {"default": "standard", "variants": {
                "standard": {"sleeve_length_mm": 627,
                              "cuff_width_mm": 225.17}}}}
        with tempfile.TemporaryDirectory() as directory:
            path = self._write(directory, data)
            table = sleeve_gui.SizeTable(path)
            table.add_variant("M", "客製645", 645, 230)
            table.save()
            restarted = sleeve_gui.SizeTable(path)
            self.assertIn("客製645", restarted.variants("M"))
            self.assertEqual(restarted.effective_values("M", "客製645")
                             .sleeve_length_mm, 645.0)

    def test_zero_cuff_without_positive_standard_is_blocked(self):
        data = {
            "M": {"default": "short", "variants": {
                "standard": {"sleeve_length_mm": 627,
                              "cuff_width_mm": 0},
                "short": {"sleeve_length_mm": 610,
                           "cuff_width_mm": 0}}}}
        with tempfile.TemporaryDirectory() as directory:
            table = sleeve_gui.SizeTable(self._write(directory, data))
            with self.assertRaises(sleeve_gui.SizeDataError):
                table.effective_values("M", "short")


class ParameterValidationTests(unittest.TestCase):
    def test_generation_parameters_require_finite_positive_dimensions(self):
        with self.assertRaises(ValueError):
            sleeve_gui.validate_generation_parameters(float("nan"), 225.17,
                                                      7.9375, 19.05)
        with self.assertRaises(ValueError):
            sleeve_gui.validate_generation_parameters(627, float("inf"),
                                                      7.9375, 19.05)
        with self.assertRaises(ValueError):
            sleeve_gui.validate_generation_parameters(627, 225.17, -1, 19.05)
        with self.assertRaises(ValueError):
            sleeve_gui.validate_generation_parameters(627, 225.17, 7.9375, 0)


if __name__ == "__main__":
    unittest.main()
