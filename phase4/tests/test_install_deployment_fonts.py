"""What the deployment-font installer decides, without a Wine or a prefix.

The copy and the regedit call need an environment; the decision does not, and
the decision is the part that can be wrong. These build sfnt containers in
memory, so a font whose name table says something unusual can be tested without
one having to exist on disk.
"""

import struct
import sys
import unittest
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / "phase4" / "scripts"))
sys.path.insert(0, str(REPOSITORY / "phase3" / "scripts"))

import install_deployment_fonts as installer  # noqa: E402
from harvest_font_metrics import Font, FontError  # noqa: E402


def name_table(records: list[tuple[int, int, int, int, bytes]]) -> bytes:
    """A format-0 name table carrying exactly the records given."""
    storage = b""
    entries = b""
    for platform, encoding, language, name_id, value in records:
        entries += struct.pack(">HHHHHH", platform, encoding, language,
                               name_id, len(value), len(storage))
        storage += value
    header = struct.pack(">HHH", 0, len(records), 6 + len(entries))
    return header + entries + storage


def sfnt(tables: dict[str, bytes]) -> bytes:
    """A minimal TrueType container holding the tables given."""
    count = len(tables)
    directory_size = 12 + count * 16
    body = b""
    directory = b""
    for tag, payload in sorted(tables.items()):
        directory += struct.pack(">4sIII", tag.encode("latin-1"), 0,
                                 directory_size + len(body), len(payload))
        body += payload
    return struct.pack(">IHHHH", 0x00010000, count, 0, 0, 0) + directory + body


WINDOWS_RECORD = (3, 1, 0x0409, 4, "Cascadia Mono".encode("utf-16-be"))
MAC_RECORD = (1, 0, 0, 4, b"Cascadia Mono")


class FullName(unittest.TestCase):
    def test_reads_the_windows_us_english_record(self):
        font = Font(sfnt({"name": name_table([WINDOWS_RECORD])}))
        self.assertEqual(installer.full_name(font), "Cascadia Mono")

    def test_prefers_windows_over_macintosh(self):
        other = (1, 0, 0, 4, b"Something Else")
        font = Font(sfnt({"name": name_table([other, WINDOWS_RECORD])}))
        self.assertEqual(installer.full_name(font), "Cascadia Mono")

    def test_falls_back_to_the_macintosh_record(self):
        font = Font(sfnt({"name": name_table([MAC_RECORD])}))
        self.assertEqual(installer.full_name(font), "Cascadia Mono")

    def test_ignores_other_name_ids(self):
        family_only = (3, 1, 0x0409, 1, "Cascadia".encode("utf-16-be"))
        font = Font(sfnt({"name": name_table([family_only])}))
        with self.assertRaises(FontError) as raised:
            installer.full_name(font)
        self.assertIn("no full name", str(raised.exception))

    def test_refuses_a_name_record_running_past_the_table(self):
        table = bytearray(name_table([WINDOWS_RECORD]))
        # Lengthen the record without lengthening the storage it points into.
        struct.pack_into(">H", table, 6 + 8, 0xff00)
        font = Font(sfnt({"name": bytes(table)}))
        with self.assertRaises(FontError) as raised:
            installer.full_name(font)
        self.assertIn("past the end", str(raised.exception))


class Plan(unittest.TestCase):
    def setUp(self):
        import tempfile
        self.directory = Path(tempfile.mkdtemp())
        self.addCleanup(__import__("shutil").rmtree, self.directory)

    def write(self, name: str, records=(WINDOWS_RECORD,)) -> Path:
        path = self.directory / name
        path.write_bytes(sfnt({"name": name_table(list(records))}))
        return path

    def test_discovers_only_font_files_in_a_stable_order(self):
        self.write("b.ttf")
        self.write("a.otf")
        (self.directory / "WindowsTerminal.exe").write_bytes(b"not a font")
        (self.directory / "notes.txt").write_text("also not a font")
        self.assertEqual([path.name for path in installer.discover(self.directory)],
                         ["a.otf", "b.ttf"])

    def test_names_the_registry_value_by_flavour(self):
        self.write("CascadiaMono.ttf")
        self.write("Something.otf")
        values = {entry["file"]: entry["registry_value"]
                  for entry in installer.plan(self.directory)}
        self.assertEqual(values["CascadiaMono.ttf"], "Cascadia Mono (TrueType)")
        self.assertEqual(values["Something.otf"], "Cascadia Mono (OpenType)")

    def test_carries_the_digest_of_what_it_would_install(self):
        import hashlib
        path = self.write("CascadiaMono.ttf")
        entry, = installer.plan(self.directory)
        self.assertEqual(entry["sha256"],
                         hashlib.sha256(path.read_bytes()).hexdigest())

    def test_refuses_the_whole_plan_naming_the_font_that_failed(self):
        self.write("Good.ttf")
        (self.directory / "Nameless.ttf").write_bytes(sfnt({"name": name_table([])}))
        with self.assertRaises(FontError) as raised:
            installer.plan(self.directory)
        self.assertIn("Nameless.ttf", str(raised.exception))

    def test_renders_one_registry_key_for_every_font(self):
        self.write("CascadiaMono.ttf")
        rendered = installer.registration(installer.plan(self.directory))
        self.assertIn("Windows NT\\CurrentVersion\\Fonts", rendered)
        self.assertIn('"Cascadia Mono (TrueType)"="CascadiaMono.ttf"', rendered)

    def test_writes_no_key_at_all_for_a_deployment_with_no_fonts(self):
        self.assertEqual(installer.plan(self.directory), [])


class ShippedFonts(unittest.TestCase):
    """The fonts phase 2 actually deploys, when that build is present."""

    DEPLOYMENT = Path("/tmp/openterminal-mingw/native-build")

    def test_every_shipped_font_names_itself(self):
        if not self.DEPLOYMENT.is_dir():
            raise unittest.SkipTest(
                f"phase-2 build absent: {self.DEPLOYMENT} does not exist")
        shipped = installer.discover(self.DEPLOYMENT)
        if not shipped:
            raise unittest.SkipTest("the phase-2 build deploys no fonts")
        entries = {entry["file"]: entry for entry in installer.plan(self.DEPLOYMENT)}
        # The one AtlasEngine asks for by default, and the reason this exists.
        # Its full name carries the style, as a full name is allowed to; the
        # family DirectWrite matches is "Cascadia Mono" either way.
        self.assertEqual(entries["CascadiaMono.ttf"]["full_name"],
                         "Cascadia Mono Regular")
        self.assertEqual(entries["CascadiaMono.ttf"]["registry_value"],
                         "Cascadia Mono Regular (TrueType)")


if __name__ == "__main__":
    unittest.main()
