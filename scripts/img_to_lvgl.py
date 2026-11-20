#!/usr/bin/env python3
"""Convert an image (PNG/JPG/etc.) into an LVGL 9 RGB565 C array for ZMK."""

import argparse
from pathlib import Path
from PIL import Image


def convert(input_path: Path, output_path: Path, symbol: str, width: int, height: int, black_threshold: int) -> None:
    img = Image.open(input_path).convert("RGB")

    # Fit inside target dimensions while preserving aspect ratio, centered on pure black
    img.thumbnail((width, height), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (width, height), (0, 0, 0))
    offset_x = (width - img.width) // 2
    offset_y = (height - img.height) // 2
    canvas.paste(img, (offset_x, offset_y))

    pixels = list(canvas.getdata())
    data = bytearray()
    for r, g, b in pixels:
        if r < black_threshold and g < black_threshold and b < black_threshold:
            r = g = b = 0
        rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        data.append(rgb565 & 0xFF)
        data.append((rgb565 >> 8) & 0xFF)

    lines = [
        "/*",
        f" * Auto-generated from {input_path.name} ({width}x{height} RGB565 little-endian)",
        " * SPDX-License-Identifier: MIT",
        " */",
        "",
        "#include <lvgl.h>",
        "",
        "#ifndef LV_ATTRIBUTE_MEM_ALIGN",
        "#define LV_ATTRIBUTE_MEM_ALIGN",
        "#endif",
        "",
        f"static const LV_ATTRIBUTE_MEM_ALIGN uint8_t {symbol}_map[] = {{",
    ]

    for i in range(0, len(data), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in data[i : i + 16])
        lines.append(f"    {chunk},")

    lines.extend([
        "};",
        "",
        f"const lv_image_dsc_t {symbol} = {{",
        "    .header = {",
        "        .magic = LV_IMAGE_HEADER_MAGIC,",
        "        .cf = LV_COLOR_FORMAT_RGB565,",
        "        .flags = 0,",
        f"        .w = {width},",
        f"        .h = {height},",
        f"        .stride = {width * 2},",
        "    },",
        f"    .data_size = sizeof({symbol}_map),",
        f"    .data = {symbol}_map,",
        "};",
        "",
    ])

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote {output_path} ({width}x{height}, {len(data)} bytes)")


def main() -> None:
    parser = argparse.ArgumentParser(description="Convert an image to an LVGL 9 RGB565 C file.")
    parser.add_argument("input", type=Path, help="Input image path (PNG/JPG/etc.)")
    parser.add_argument("output", type=Path, help="Output .c file path")
    parser.add_argument("--symbol", default="straw_hat_img", help="C symbol name (default: straw_hat_img)")
    parser.add_argument("--width", type=int, default=135, help="Target width in px (default: 135)")
    parser.add_argument("--height", type=int, default=135, help="Target height in px (default: 135)")
    parser.add_argument("--black-threshold", type=int, default=12, help="Clamp near-black pixels to pure black")
    args = parser.parse_args()
    convert(args.input, args.output, args.symbol, args.width, args.height, args.black_threshold)


if __name__ == "__main__":
    main()
