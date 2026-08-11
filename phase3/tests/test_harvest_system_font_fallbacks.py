#!/usr/bin/env python3
"""Which FontIcon glyphs need a DirectWrite system fallback, and which do not.

This script has never run. It was added with a workflow step that has failed
on every run since it landed -- the probe it drives did not compile on the
Windows runner -- so the first time its discovery pass executes for real will
be the first time anybody sees its output. That is a bad moment to find out it
selects the wrong codepoints, and `requested_fallbacks` is pure: markup and
harvested metrics in, (family, codepoint, locale) out. It can be checked here.

The case that pays for this is `L4-icon-rule-mdl2-latin-14`, the only case in
the corpus that reaches the system-fallback path. It is a `<FontIcon
FontFamily="Segoe MDL2 Assets" Glyph="M"/>`: the family is harvested, it has 44
advances, and U+004D is not one of them. So the shaper refuses, the case does
not lay out, and the missing thing is exactly one probe result.
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIRECTORY = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPT_DIRECTORY))

import harvest_system_font_fallbacks as fallbacks  # noqa: E402

CASES = Path(__file__).resolve().parents[1] / "xaml-db" / "cases"

# Segoe MDL2 Assets as the runner harvests it: an icon font that covers its
# icon codepoints and no Latin letter.
MDL2 = {"family": "Segoe MDL2 Assets", "provenance": "harvested",
        "units_per_em": 2048, "advances": {"57344": 2048, "58004": 2048}}
FLUENT = {"family": "Segoe Fluent Icons", "provenance": "harvested",
          "units_per_em": 2048, "advances": {"57344": 2048}}


def case(markup: str, language: str = "en-US") -> dict:
    return {"markup": markup, "environment": {"language": language}}


class RequestedFallbacksTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.cases = Path(self.tmp.name)
        self.fonts = {"Segoe MDL2 Assets": MDL2, "Segoe Fluent Icons": FLUENT}

    def write(self, name: str, document: dict) -> None:
        (self.cases / f"{name}.json").write_text(json.dumps(document), encoding="utf-8")

    def test_a_glyph_the_named_family_lacks_is_requested(self) -> None:
        self.write("icon", case('<FontIcon FontFamily="Segoe MDL2 Assets" Glyph="M"/>'))
        self.assertEqual(fallbacks.requested_fallbacks(self.cases, self.fonts),
                         {("Segoe MDL2 Assets", ord("M"), "en-US")})

    def test_a_glyph_the_family_covers_is_not_requested(self) -> None:
        # U+E000 is in the harvest. Probing for it would attach a "fallback"
        # for a codepoint the font itself answers, which attach_system_fallbacks
        # refuses outright -- so selecting it here would turn into a hard error
        # much later, on the runner, with a confusing message.
        self.write("icon", case('<FontIcon FontFamily="Segoe MDL2 Assets" Glyph="&#xE000;"/>'))
        self.assertEqual(fallbacks.requested_fallbacks(self.cases, self.fonts), set())

    def test_a_fallback_list_is_satisfied_by_any_member(self) -> None:
        # Terminal writes these families as a list precisely because the two do
        # not cover the same set. If either covers the glyph, nothing is needed.
        self.write("icon", case('<FontIcon FontFamily="Segoe Fluent Icons,Segoe MDL2 Assets" '
                                'Glyph="&#xE294;"/>'))
        self.assertEqual(fallbacks.requested_fallbacks(self.cases, self.fonts), set())

    def test_a_family_nothing_was_harvested_for_is_not_requested(self) -> None:
        # There is no metrics file to attach the mapping to, so a request would
        # have nowhere to land.
        self.write("icon", case('<FontIcon FontFamily="Not Harvested" Glyph="M"/>'))
        self.assertEqual(fallbacks.requested_fallbacks(self.cases, self.fonts), set())

    def test_the_default_family_is_segoe_mdl2_assets(self) -> None:
        # A FontIcon with no FontFamily is Segoe MDL2 Assets, and the request
        # must be attributed to it rather than skipped.
        self.write("icon", case('<FontIcon Glyph="M"/>'))
        self.assertEqual(fallbacks.requested_fallbacks(self.cases, self.fonts),
                         {("Segoe MDL2 Assets", ord("M"), "en-US")})

    def test_a_bound_family_currently_falls_back_to_the_default_family(self) -> None:
        # Recording what it does, not endorsing it. `literal()` returns None
        # for a `{Binding}`/`{ThemeResource}` exactly as it does for an absent
        # attribute, and the caller's `or "Segoe MDL2 Assets"` then applies the
        # XAML default to both. For an absent attribute that is right. For a
        # bound one it is a guess: the request is attributed to Segoe MDL2
        # Assets whatever the resource would have resolved to, and the mapping
        # is attached to that family's metrics.
        #
        # Left alone deliberately. No case in the corpus binds a FontIcon's
        # FontFamily today, so nothing is currently wrong; and deciding what
        # the right answer is -- skip it, or resolve the resource -- is a
        # question for the oracle rather than for a test author. Flagged here
        # so the next person to add such a case finds this rather than a
        # surprise.
        self.write("icon", case('<FontIcon FontFamily="{ThemeResource K}" Glyph="M"/>'))
        self.assertEqual(fallbacks.requested_fallbacks(self.cases, self.fonts),
                         {("Segoe MDL2 Assets", ord("M"), "en-US")})

    def test_the_locale_travels_with_the_request(self) -> None:
        # DirectWrite's fallback mapping is locale-dependent, and the script
        # refuses two different mappings for one codepoint, so carrying the
        # wrong locale would be a silently different answer.
        self.write("icon", case('<FontIcon FontFamily="Segoe MDL2 Assets" Glyph="M"/>',
                                language="ja-JP"))
        self.assertEqual(fallbacks.requested_fallbacks(self.cases, self.fonts),
                         {("Segoe MDL2 Assets", ord("M"), "ja-JP")})

    def test_every_codepoint_of_a_multi_character_glyph_is_considered(self) -> None:
        self.write("icon", case('<FontIcon FontFamily="Segoe MDL2 Assets" Glyph="MN"/>'))
        self.assertEqual(fallbacks.requested_fallbacks(self.cases, self.fonts),
                         {("Segoe MDL2 Assets", ord("M"), "en-US"),
                          ("Segoe MDL2 Assets", ord("N"), "en-US")})


class AgainstTheRealCorpusTest(unittest.TestCase):
    """The one thing that decides whether this closes L4-icon-rule-mdl2-latin-14."""

    def test_the_corpus_asks_for_exactly_the_glyph_that_case_needs(self) -> None:
        if not CASES.is_dir():
            raise unittest.SkipTest(
                f"no generated corpus at {CASES}; run "
                f"phase3/scripts/generate_cases.py")
        found = fallbacks.requested_fallbacks(CASES, {"Segoe MDL2 Assets": MDL2,
                                                      "Segoe Fluent Icons": FLUENT})
        self.assertIn(("Segoe MDL2 Assets", ord("M"), "en-US"), found,
                      "the discovery pass does not ask for U+004D in Segoe MDL2 "
                      "Assets, which is the single mapping "
                      "L4-icon-rule-mdl2-latin-14 is blocked on")


if __name__ == "__main__":
    unittest.main()
