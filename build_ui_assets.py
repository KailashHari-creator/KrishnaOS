#!/usr/bin/env python3

from pathlib import Path
import struct
import subprocess

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parent
ASSETS = ROOT / "assets"

ART_TEXT_PATH = ASSETS / "terminal_startup_art.txt"
ART_PNG_PATH = ASSETS / "terminal_startup_art.png"
ART_RGBA_PATH = ASSETS / "terminal_startup_art.rgba"
KFONT_PATH = ASSETS / "krishna_font.kfont"

ART_WIDTH = 520
ART_HEIGHT = 250

GLYPH_WIDTH = 10
GLYPH_HEIGHT = 20
GLYPH_FIRST = 32
GLYPH_COUNT = 95
GLYPH_POINT_SIZE = 15
GLYPH_BASELINE = 15


def find_font(pattern: str) -> str:
    result = subprocess.check_output(
        [
            "fc-match",
            "-f",
            "%{file}",
            pattern,
        ],
        text=True,
    ).strip()

    if not result:
        raise RuntimeError(
            f"Could not locate font: {pattern}"
        )

    return result


def find_braille_font() -> str:
    """
    Ask fontconfig for a font that explicitly covers the final
    Braille-pattern codepoint.
    """
    result = subprocess.check_output(
        [
            "fc-match",
            "-f",
            "%{file}",
            ":charset=28ff",
        ],
        text=True,
    ).strip()

    if not result:
        raise RuntimeError(
            "Could not locate a font containing Braille glyphs"
        )

    return result


def build_terminal_art() -> None:
    text = ART_TEXT_PATH.read_text(
        encoding="utf-8"
    ).rstrip("\n")

    if not text:
        raise RuntimeError(
            f"{ART_TEXT_PATH} is empty"
        )

    font_path = find_braille_font()

    selected_font = None
    selected_bbox = None

    temporary_image = Image.new(
        "RGBA",
        (ART_WIDTH, ART_HEIGHT),
        (0, 0, 0, 0),
    )

    temporary_draw = ImageDraw.Draw(
        temporary_image
    )

        # Choose the largest point size that fits inside the canvas.
    for point_size in range(18, 7, -1):
        font = ImageFont.truetype(
            font_path,
            point_size,
        )

        bbox = temporary_draw.multiline_textbbox(
            (0, 0),
            text,
            font=font,
            spacing=0,
            align="left",
        )

        text_width = bbox[2] - bbox[0]
        text_height = bbox[3] - bbox[1]

        if (
            text_width <= ART_WIDTH - 20
            and text_height <= ART_HEIGHT - 20
        ):
            selected_font = font
            selected_bbox = bbox
            break

    if selected_font is None or selected_bbox is None:
        raise RuntimeError(
            "The terminal artwork does not fit the canvas"
        )

    image = Image.new(
        "RGBA",
        (ART_WIDTH, ART_HEIGHT),
        (0, 0, 0, 0),
    )

    draw = ImageDraw.Draw(image)

    text_width = (
        selected_bbox[2] -
        selected_bbox[0]
    )

    text_height = (
        selected_bbox[3] -
        selected_bbox[1]
    )

    text_x = (
        ART_WIDTH -
        text_width
    ) // 2 - selected_bbox[0]

    text_y = (
        ART_HEIGHT -
        text_height
    ) // 2 - selected_bbox[1]

    draw.multiline_text(
        (text_x, text_y),
        text,
        font=selected_font,
        fill=(105, 228, 245, 235),
        spacing=0,
        align="left",
    )

    alpha = image.getchannel("A")

    if alpha.getbbox() is None:
        raise RuntimeError(
            "The generated terminal artwork is empty"
        )

    image.save(ART_PNG_PATH)
    ART_RGBA_PATH.write_bytes(
        image.tobytes()
    )

    expected_size = (
        ART_WIDTH *
        ART_HEIGHT *
        4
    )

    actual_size = (
        ART_RGBA_PATH.stat().st_size
    )

    if actual_size != expected_size:
        raise RuntimeError(
            "Invalid artwork size: "
            f"expected {expected_size}, "
            f"received {actual_size}"
        )

    print(
        "Terminal artwork:",
        ART_PNG_PATH,
    )

    print(
        "Terminal raw RGBA:",
        ART_RGBA_PATH,
        f"({actual_size} bytes)",
    )

    print(
        "Braille font:",
        font_path,
    )


def build_kfont() -> None:
    font_path = find_font(
        "DejaVu Sans Mono"
    )

    font = ImageFont.truetype(
        font_path,
        GLYPH_POINT_SIZE,
    )

    glyph_data = bytearray()

    for codepoint in range(
        GLYPH_FIRST,
        GLYPH_FIRST + GLYPH_COUNT,
    ):
        character = chr(codepoint)

        glyph = Image.new(
            "L",
            (GLYPH_WIDTH, GLYPH_HEIGHT),
            0,
        )

        draw = ImageDraw.Draw(glyph)

        advance = draw.textlength(
            character,
            font=font,
        )

        x = (
            GLYPH_WIDTH -
            advance
        ) / 2

        # /*
        #  * anchor="ls" means:
        #  *
        #  *     l = horizontally anchored from the left
        #  *     s = vertically anchored on a shared baseline
        #  *
        #  * Therefore uppercase, lowercase and descending letters all
        #  * share a proper typographic baseline.
        #  */
        draw.text(
            (x, GLYPH_BASELINE),
            character,
            font=font,
            fill=255,
            anchor="ls",
        )

        glyph_data.extend(
            glyph.tobytes()
        )

    header = struct.pack(
        "<8sHHHH",
        b"KFONT001",
        GLYPH_WIDTH,
        GLYPH_HEIGHT,
        GLYPH_FIRST,
        GLYPH_COUNT,
    )

    KFONT_PATH.write_bytes(
        header + glyph_data
    )

    expected_size = (
        len(header) +
        GLYPH_WIDTH *
        GLYPH_HEIGHT *
        GLYPH_COUNT
    )

    actual_size = (
        KFONT_PATH.stat().st_size
    )

    if actual_size != expected_size:
        raise RuntimeError(
            "Invalid KFONT size: "
            f"expected {expected_size}, "
            f"received {actual_size}"
        )

    print(
        "KFONT:",
        KFONT_PATH,
        f"({actual_size} bytes)",
    )

    print(
        "ASCII font:",
        font_path,
    )


def main() -> None:
    build_terminal_art()
    build_kfont()


if __name__ == "__main__":
    main()