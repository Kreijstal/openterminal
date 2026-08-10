#!/usr/bin/env python3
"""Validate native XAML captures and create a deterministic textual manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def _pixel_summary(data: bytes) -> dict[str, int]:
    pixels = memoryview(data).cast("B")
    alpha = pixels[3::4]
    transparent = sum(value == 0 for value in alpha)
    opaque = sum(value == 255 for value in alpha)
    return {
        "pixel_count": len(alpha),
        "transparent_pixels": transparent,
        "partial_alpha_pixels": len(alpha) - transparent - opaque,
        "opaque_pixels": opaque,
        "distinct_bgra_values": len(
            {bytes(pixels[offset : offset + 4]) for offset in range(0, len(pixels), 4)}
        ),
    }


def finalize(directory: Path) -> dict:
    captures = []
    seen_ids: set[str] = set()
    for observation_path in sorted(directory.glob("*.json")):
        if observation_path.name in {"manifest.json", "oracle.json", "dwrite-glyph-runs.json"}:
            continue
        observation = json.loads(observation_path.read_text(encoding="utf-8"))
        case_id = observation["case_id"]
        if case_id in seen_ids:
            raise ValueError(f"duplicate case_id {case_id!r}")
        seen_ids.add(case_id)

        capture = observation["capture"]
        if capture["pixel_format"] != "BGRA8":
            raise ValueError(f"{case_id}: unsupported pixel format {capture['pixel_format']!r}")
        if capture["alpha_mode"] != "premultiplied":
            raise ValueError(f"{case_id}: unexpected alpha mode {capture['alpha_mode']!r}")
        width, height, stride = capture["width"], capture["height"], capture["stride"]
        if width <= 0 or height <= 0 or stride != width * 4:
            raise ValueError(f"{case_id}: invalid dimensions/stride {width}x{height}/{stride}")

        pixel_name = capture["pixels_file"]
        if Path(pixel_name).name != pixel_name:
            raise ValueError(f"{case_id}: pixels_file must be a basename")
        pixel_path = directory / pixel_name
        data = pixel_path.read_bytes()
        expected = stride * height
        if len(data) != expected:
            raise ValueError(f"{case_id}: {len(data)} pixel bytes, expected {expected}")

        captures.append(
            {
                "case_id": case_id,
                "observation_file": observation_path.name,
                "pixels_file": pixel_name,
                "sha256": hashlib.sha256(data).hexdigest(),
                "width": width,
                "height": height,
                "stride": stride,
                "tree_nodes": len(observation["tree"]),
                **_pixel_summary(data),
            }
        )

    if not captures:
        raise ValueError(f"no render observations in {directory}")
    manifest = {
        "schema_version": 1,
        "capture_method": "Windows.UI.Xaml.Media.Imaging.RenderTargetBitmap",
        "pixel_format": "BGRA8",
        "alpha_mode": "premultiplied",
        "captures": captures,
    }
    glyph_path = directory / "dwrite-glyph-runs.json"
    if glyph_path.is_file():
        glyph_data = glyph_path.read_bytes()
        glyph_document = json.loads(glyph_data)
        manifest["directwrite_glyph_runs"] = {
            "file": glyph_path.name,
            "observations": len(glyph_document["observations"]),
            "sha256": hashlib.sha256(glyph_data).hexdigest(),
        }
    (directory / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    manifest = finalize(args.directory)
    print(f"finalized {len(manifest['captures'])} render observations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
