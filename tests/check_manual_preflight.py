#!/usr/bin/env python3
"""Offline regression checks for manual catalog validation; no PDF tools needed."""
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "manuals"))
import preflight
import build_manuals


class CatalogValidationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.catalog = self.root / "plugins.yml"
        self.catalog_patch = patch.object(preflight, "PLUGINS_YML", self.catalog)
        self.catalog_patch.start()
        self.addCleanup(self.catalog_patch.stop)

    def write_catalog(self, status):
        self.catalog.write_text(yaml.safe_dump([
            {"slug": "good", "version": "1.0.0", "status": "released"},
            {"slug": "subject", "version": "1.0.0", "status": status},
        ]))

    def test_invalid_status_reports_error_and_excludes_chapter(self):
        for status in (["in-dev"], {"state": "in-dev"}, [], {}, True, False, 7, "relased"):
            with self.subTest(status=status):
                self.write_catalog(status)
                errors = []
                versions, unreleased = preflight.load_plugin_catalog(errors)
                self.assertEqual(versions, {"good": "1.0.0", "subject": "1.0.0"})
                self.assertEqual(unreleased, {"subject"})
                self.assertEqual(len(errors), 1)
                self.assertIn("subject: unknown status", errors[0])

    def test_known_and_missing_status_keep_existing_release_rules(self):
        for status, unreleased in ((None, set()), ("released", set()),
                                   ("pre-release", set()), ("in-dev", {"subject"}),
                                   ("coming-soon", {"subject"})):
            with self.subTest(status=status):
                self.write_catalog(status)
                errors = []
                _, actual = preflight.load_plugin_catalog(errors)
                self.assertEqual(actual, unreleased)
                self.assertEqual(errors, [])

    def test_non_string_slugs_report_errors_without_entering_catalog(self):
        for slug in (["subject"], {"name": "subject"}, [], {}, True, False, 7, 0):
            for version in (None, "1.0.0"):
                with self.subTest(slug=slug, version=version):
                    self.catalog.write_text(yaml.safe_dump([
                        {"slug": "good", "version": "1.0.0", "status": "released"},
                        {"slug": slug, "version": version},
                    ]))
                    errors = []
                    versions, unreleased = preflight.load_plugin_catalog(errors)
                    self.assertEqual(versions, {"good": "1.0.0"})
                    self.assertEqual(unreleased, set())
                    self.assertEqual(len(errors), 1)
                    self.assertIn("invalid slug", errors[0])

    def test_missing_and_empty_slugs_keep_existing_skip_behavior(self):
        self.catalog.write_text(yaml.safe_dump([
            {"version": "1.0.0"}, {"slug": None}, {"slug": "", "version": "1.0.0"},
        ]))
        errors = []
        self.assertEqual(preflight.load_plugin_catalog(errors), ({}, set()))
        self.assertEqual(errors, [])

    def test_preflight_and_renderer_reject_malformed_status_without_traceback(self):
        self.write_catalog(["in-dev"])
        for slug in ("good", "subject"):
            (self.root / f"{slug}.md").write_text(
                f"---\nslug: {slug}\nversion: 1.0.0\n---\nA manual chapter.\n")
        with patch.object(preflight, "MANUALS_DIR", self.root):
            for combined in (False, True):
                with self.subTest(combined=combined), contextlib.redirect_stderr(io.StringIO()) as err:
                    with contextlib.redirect_stdout(io.StringIO()):
                        self.assertEqual(preflight.run_preflight(None, combined=combined), 1)
                    self.assertIn("subject: unknown status", err.getvalue())
            with patch.object(build_manuals, "run_pandoc") as render:
                with contextlib.redirect_stderr(io.StringIO()) as err:
                    with self.assertRaises(SystemExit) as exit_result:
                        build_manuals.render_combined()
                self.assertEqual(exit_result.exception.code, 2)
                self.assertIn("subject: unknown status", err.getvalue())
                render.assert_not_called()


if __name__ == "__main__":
    unittest.main()
