#!/usr/bin/env python3
"""Regenerate the README's colored SVG blueprints from real terminal output."""
import argparse
from html import escape
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SGR = re.compile(r"\x1b\[([0-9;]*)m")
BACKGROUND, FOREGROUND = "#101820", "#dce5f2"


def color(index):
    basic = ("000000", "cd0000", "00cd00", "cdcd00", "0000ee", "cd00cd", "00cdcd", "e5e5e5",
             "7f7f7f", "ff0000", "00ff00", "ffff00", "5c5cff", "ff00ff", "00ffff", "ffffff")
    if index < 16:
        return "#" + basic[index]
    if index < 232:
        levels = (0, 95, 135, 175, 215, 255)
        value = index - 16
        return "#%02x%02x%02x" % (levels[value // 36], levels[value // 6 % 6], levels[value % 6])
    value = 8 + (index - 232) * 10
    return "#%02x%02x%02x" % (value, value, value)


def segments(line):
    foreground, background, bold = FOREGROUND, None, False
    position = 0
    for match in SGR.finditer(line):
        if match.start() > position:
            yield line[position:match.start()], foreground, background, bold
        codes = [int(value) if value else 0 for value in match[1].split(";")]
        index = 0
        while index < len(codes):
            code = codes[index]
            if code == 0:
                foreground, background, bold = FOREGROUND, None, False
            elif code == 1:
                bold = True
            elif code == 22:
                bold = False
            elif 30 <= code <= 37:
                foreground = color(code - 30)
            elif code == 39:
                foreground = FOREGROUND
            elif code == 49:
                background = None
            elif code in (38, 48) and codes[index + 1:index + 2] == [5]:
                if code == 38:
                    foreground = color(codes[index + 2])
                else:
                    background = color(codes[index + 2])
                index += 2
            else:
                raise ValueError(f"Unsupported terminal color sequence: {match[0]!r}")
            index += 1
        position = match.end()
    if position < len(line):
        yield line[position:], foreground, background, bold


def blueprint_lines(output):
    lines = output.splitlines()
    plain = [SGR.sub("", line) for line in lines]
    start = next(index for index, line in enumerate(plain) if "[STATIC DEPOT BOUNDARY]" in line)
    end = next(index for index in range(start, len(lines)) if plain[index] == "-" * 80)
    indent = min(len(line) - len(line.lstrip()) for line in plain[start:end] if line.strip())
    result = [line[indent:].replace("[STATIC DEPOT BOUNDARY]", "[DEPOT]") for line in lines[start:end]]
    legend = next(line for line in lines if "Conveyors:" in line)
    items = re.findall(r"\x1b\[38;5;(\d+)m══ ([^\x1b]+)", legend)
    if not items:
        raise ValueError("Blueprint has no conveyor color legend")
    result += ["", "Belt colors"] + [f"\x1b[38;5;{shade}m══ {item}\x1b[0m" for shade, item in items]
    return result


def render_svg(title, lines):
    cell_width, row_height, padding = 9, 18, 18
    width = max(len(SGR.sub("", line)) for line in lines) * cell_width + padding * 2
    height = len(lines) * row_height + padding * 2
    elements = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-labelledby="title description">',
                f'<title id="title">{escape(title)} blueprint</title>',
                '<desc id="description">Generated factory map with colored facilities, item-colored conveyors, port arrows, coordinate labels, and a belt color legend. The depot is above the factory.</desc>',
                f'<rect width="{width}" height="{height}" rx="8" fill="{BACKGROUND}"/>',
                '<g font-family="DejaVu Sans Mono, Liberation Mono, monospace" font-size="15" xml:space="preserve">']
    for row, line in enumerate(lines):
        column = 0
        for text, foreground, background, bold in segments(line):
            x, y = padding + column * cell_width, padding + row * row_height
            length = len(text) * cell_width
            if background:
                elements.append(f'<rect x="{x}" y="{y}" width="{length}" height="{row_height}" fill="{background}"/>')
            if text.strip():
                weight = ' font-weight="bold"' if bold else ""
                elements.append(f'<text x="{x}" y="{y + 14}" fill="{foreground}" textLength="{length}" lengthAdjust="spacingAndGlyphs"{weight}>{escape(text)}</text>')
            column += len(text)
    return "\n".join(elements + ["</g>", "</svg>", ""])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/bin/aic_planner")
    args = parser.parse_args()
    try:
        for product, title in (("ferrium_part", "Ferrium Part"), ("sc_valley_battery", "SC Valley Battery")):
            result = subprocess.run([str(args.binary.resolve()), "--product", product, "--blueprints-only", str(ROOT / "data/test")],
                                    check=True, capture_output=True, text=True, encoding="utf-8")
            svg = render_svg(title, blueprint_lines(result.stdout))
            destination = ROOT / "docs/images" / (product + ".svg")
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(svg, encoding="utf-8")
            print(f"Generated {destination.relative_to(ROOT)}")
    except (OSError, ValueError, StopIteration, subprocess.CalledProcessError) as error:
        print(f"Showcase generation failed: {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError):
            print(error.stderr, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
