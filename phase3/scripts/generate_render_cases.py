#!/usr/bin/env python3
"""Generate the small, authored corpus used by the native XAML pixel oracle.

These cases isolate rendering contracts.  They deliberately do not contain
expected pixels: the Windows.UI.Xaml runtime supplies those observations.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


XAML_NS = "http://schemas.microsoft.com/winfx/2006/xaml/presentation"


CASES = (
    {
        "id": "R1-solid-fill",
        "render_size": [128, 96],
        "boundaries": ["pixels", "solid-fill", "size"],
        "markup": f'''<Grid xmlns="{XAML_NS}" Width="128" Height="96" Background="#FF336699"/>''',
    },
    {
        "id": "R2-nested-layout-offset",
        "render_size": [160, 112],
        "boundaries": ["pixels", "layout-slot", "transform-to-root"],
        "markup": f'''<Grid xmlns="{XAML_NS}" Width="160" Height="112" Background="#FFFFFFFF">
  <Border Width="100" Height="72" HorizontalAlignment="Center" VerticalAlignment="Center" Background="#FF203040" Padding="11,7">
    <Border Background="#FFFFB000"/>
  </Border>
</Grid>''',
    },
    {
        "id": "R3-alpha-overlap",
        "render_size": [128, 96],
        "boundaries": ["pixels", "premultiplied-alpha", "opacity"],
        "markup": f'''<Canvas xmlns="{XAML_NS}" Width="128" Height="96" Background="Transparent">
  <Rectangle Width="72" Height="56" Fill="#C0FF4000" Canvas.Left="12" Canvas.Top="10"/>
  <Rectangle Width="72" Height="56" Fill="#8000A0FF" Canvas.Left="42" Canvas.Top="28" Opacity="0.75"/>
</Canvas>''',
    },
    {
        "id": "R4-geometry-clip",
        "render_size": [128, 96],
        "boundaries": ["pixels", "clip"],
        "markup": f'''<Canvas xmlns="{XAML_NS}" Width="128" Height="96" Background="#FFFFFFFF">
  <Rectangle Width="92" Height="68" Fill="#FF6A2CA0" Canvas.Left="18" Canvas.Top="14">
    <Rectangle.Clip>
      <RectangleGeometry Rect="9,8,54,37"/>
    </Rectangle.Clip>
  </Rectangle>
</Canvas>''',
    },
    {
        "id": "R5-render-transform",
        "render_size": [144, 112],
        "boundaries": ["pixels", "transform-to-root", "composition-visual"],
        "markup": f'''<Canvas xmlns="{XAML_NS}" Width="144" Height="112" Background="#FFFFFFFF">
  <Rectangle Width="70" Height="38" Fill="#FF00A878" Canvas.Left="35" Canvas.Top="34" RenderTransformOrigin="0.5,0.5">
    <Rectangle.RenderTransform>
      <RotateTransform Angle="17"/>
    </Rectangle.RenderTransform>
  </Rectangle>
</Canvas>''',
    },
    {
        "id": "R6-z-order",
        "render_size": [128, 96],
        "boundaries": ["pixels", "sibling-order", "z-index"],
        "markup": f'''<Canvas xmlns="{XAML_NS}" Width="128" Height="96" Background="#FFFFFFFF">
  <Rectangle Width="70" Height="54" Fill="#FFE84855" Canvas.Left="10" Canvas.Top="10" Canvas.ZIndex="1"/>
  <Rectangle Width="70" Height="54" Fill="#FF3D72C9" Canvas.Left="30" Canvas.Top="24" Canvas.ZIndex="0"/>
  <Rectangle Width="54" Height="42" Fill="#FFFFC857" Canvas.Left="54" Canvas.Top="40" Canvas.ZIndex="2"/>
</Canvas>''',
    },
    {
        "id": "R7-text",
        "render_size": [256, 80],
        "boundaries": ["pixels", "text-rasterization", "font-environment"],
        "markup": f'''<Grid xmlns="{XAML_NS}" Width="256" Height="80" Background="#FFFFFFFF">
  <TextBlock Text="Hamburgefontsiv 0123" FontFamily="Segoe UI" FontSize="20" Foreground="#FF101010" HorizontalAlignment="Left" VerticalAlignment="Top" Margin="12,9,0,0"/>
</Grid>''',
    },
    {
        "id": "R8-mini-xaml-ui",
        "render_size": [240, 144],
        "boundaries": ["pixels", "nested-tree", "border", "text"],
        "markup": f'''<Grid xmlns="{XAML_NS}" Width="240" Height="144" Background="#FF20252B" Padding="16">
  <StackPanel Spacing="8">
    <TextBlock Text="Open Terminal" FontFamily="Segoe UI" FontSize="22" Foreground="#FFF4F4F4"/>
    <Border Height="54" Background="#FF303841" BorderBrush="#FF5B6672" BorderThickness="1" Padding="10,7">
      <StackPanel>
        <TextBlock Text="Renderer boundary" FontFamily="Segoe UI" FontSize="14" Foreground="#FFFFFFFF"/>
        <TextBlock Text="pixels + visual geometry" FontFamily="Segoe UI" FontSize="12" Foreground="#FFB8C2CC"/>
      </StackPanel>
    </Border>
  </StackPanel>
</Grid>''',
    },
)


def generate(out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    expected = {f"{case['id']}.json" for case in CASES}
    for stale in out.glob("*.json"):
        if stale.name not in expected:
            stale.unlink()
    for case in CASES:
        width, height = case["render_size"]
        # Both sides consume the same file. The native probe asks for
        # render_size explicitly; the platform-neutral layout/runtime uses its
        # ordinary case environment. Keeping both in one generated document
        # prevents an acceptance run from quietly changing the input between
        # implementations.
        payload = {
            "schema_version": 1,
            **case,
            "environment": {
                "available_size": [width, height],
                "language": "en-US",
                "dpi_scale": 1.0,
            },
        }
        (out / f"{case['id']}.json").write_text(
            json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    generate(args.out)
    print(f"generated {len(CASES)} render cases in {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
