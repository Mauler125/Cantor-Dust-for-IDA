#!/usr/bin/env python3
"""Render the 3D Byte Cloud geometries and assemble a 4-panel sheet."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path
from PIL import Image, ImageOps, ImageDraw, ImageFont

LABELS = [
    ("cube_xyz", "Cube (XYZ)"),
    ("sphere", "Sphere"),
    ("cylinder", "Cylinder"),
    ("tetrahedron", "Tetrahedron"),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Render the 3D Byte Cloud geometries and a sheet preview.")
    parser.add_argument("input", type=Path, help="Input binary file")
    parser.add_argument("output_dir", type=Path, help="Directory for PNG output")
    parser.add_argument(
        "--tool",
        type=Path,
        default=Path("build/cantordust_cube_preview"),
        help="Path to the cantordust_cube_preview executable",
    )
    parser.add_argument(
        "--palette-index",
        type=int,
        default=0,
        help="Palette index, defaults to 0 (Reference)",
    )
    parser.add_argument(
        "--brightness-percent",
        type=float,
        default=160.0,
        help="Brightness percentage for the renderer",
    )
    parser.add_argument(
        "--size-percent",
        type=float,
        default=100.0,
        help="Point size percentage for the renderer",
    )
    parser.add_argument("--auto-brightness", action="store_true",
                        help="Enable adaptive exposure")
    parser.add_argument("--value-mode", type=int, default=0,
                        help="Cube value mode index (0-20)")
    parser.add_argument("--stride", type=int, default=0,
                        help="Stride in bytes (0 = mode default)")
    parser.add_argument("--alignment", type=int, default=0,
                        help="Byte-offset alignment")
    parser.add_argument("--bit-plane", type=int, default=0,
                        help="Bit index for bit-plane mode (0-7)")
    return parser.parse_args()


def build_sheet(output_dir: Path) -> Path:
    panel_w = 250
    panel_h = 250
    margin_left = 24
    margin_right = 24
    margin_top = 18
    margin_bottom = 22
    gap = 20
    title_band = 34
    background = (2, 13, 33)
    title_color = (240, 245, 252)

    width = margin_left + len(LABELS) * panel_w + (len(LABELS) - 1) * gap + margin_right
    height = margin_top + title_band + panel_h + margin_bottom

    sheet = Image.new("RGB", (width, height), background)
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()

    panel_y = margin_top + title_band

    for index, (suffix, title) in enumerate(LABELS):
        panel_x = margin_left + index * (panel_w + gap)
        draw.text((panel_x, margin_top), title, fill=title_color, font=font)

        image_path = output_dir / f"byte_cloud_{suffix}.png"
        image = Image.open(image_path)
        image = ImageOps.contain(image, (panel_w, panel_h))
        sheet.paste(image, (panel_x, panel_y))

    sheet_path = output_dir / "byte_cloud_geometries.png"
    sheet.save(sheet_path)
    return sheet_path


def main() -> int:
    args = parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    prefix = args.output_dir / "byte_cloud"
    command = [
        str(args.tool),
        str(args.input),
        str(prefix),
        str(args.palette_index),
        str(args.brightness_percent),
        str(args.size_percent),
        "1" if args.auto_brightness else "0",
        str(args.value_mode),
        str(args.stride),
        str(args.alignment),
        str(args.bit_plane),
    ]
    subprocess.run(command, check=True)

    for suffix, _ in LABELS:
        ppm_path = args.output_dir / f"byte_cloud_{suffix}.ppm"
        png_path = args.output_dir / f"byte_cloud_{suffix}.png"
        Image.open(ppm_path).save(png_path)
        ppm_path.unlink(missing_ok=True)

    sheet_path = build_sheet(args.output_dir)
    print(sheet_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
