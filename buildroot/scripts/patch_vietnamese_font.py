import argparse
import os
import sys
import unicodedata

try:
    from PIL import Image, ImageDraw, ImageFilter, ImageFont
except ImportError as exc:
    raise SystemExit(
        "Pillow is required. Install it with: pip install pillow"
    ) from exc


GLYPH_WIDTH = 24
GLYPH_HEIGHT = 24
BYTES_PER_COLUMN = (GLYPH_HEIGHT + 7) // 8
BYTES_PER_GLYPH = GLYPH_WIDTH * BYTES_PER_COLUMN
UNICODE_GLYPH_COUNT = 0x10000
EXPECTED_FONT_SIZE = UNICODE_GLYPH_COUNT * BYTES_PER_GLYPH

VIETNAMESE_CHARACTERS = "".join([
    "ÀÁẢÃẠĂẰẮẲẴẶÂẦẤẨẪẬĐÈÉẺẼẸÊỀẾỂỄỆÌÍỈĨỊ",
    "ÒÓỎÕỌÔỒỐỔỖỘƠỜỚỞỠỢÙÚỦŨỤƯỪỨỬỮỰỲÝỶỸỴ",
    "àáảãạăằắẳẵặâầấẩẫậđèéẻẽẹêềếểễệìíỉĩị",
    "òóỏõọôồốổỗộơờớởỡợùúủũụưừứửữựỳýỷỹỵ",
])

STACKED_VIETNAMESE_CHARACTERS = set("".join([
    "ẮẰẲẴẶẤẦẨẪẬẾỀỂỄỆỐỒỔỖỘỚỜỞỠỢỨỪỬỮỰ",
    "ắằẳẵặấầẩẫậếềểễệốồổỗộớờởỡợứừửữự",
]))

BASE_CHARACTER_MAP = {
    "Đ": "D",
    "đ": "d",
}


def parse_args():
    parser = argparse.ArgumentParser(
        description="Patch Vietnamese glyphs into BIGTREETECH word_unicode.fon"
    )
    parser.add_argument("input", help="Path to the source word_unicode.fon")
    parser.add_argument("output", help="Path for the patched word_unicode.fon")
    parser.add_argument("font", help="Path to a TTF/OTF font with Vietnamese glyphs")
    parser.add_argument("--font-size", type=int, default=22, help="Rendering size in points")
    parser.add_argument("--x-offset", type=int, default=0, help="Horizontal glyph offset")
    parser.add_argument("--y-offset", type=int, default=0, help="Vertical glyph offset")
    parser.add_argument("--padding-x", type=int, default=0, help="Left/right padding inside the 24px glyph cell")
    parser.add_argument("--fit-height", type=int, default=22, help="Maximum glyph height inside the 24px cell")
    parser.add_argument("--embolden", type=int, default=0, help="Apply N rounds of pixel expansion after scaling")
    parser.add_argument("--threshold", type=int, default=96, help="Grayscale threshold for on pixels")
    parser.add_argument(
        "--chars",
        default=VIETNAMESE_CHARACTERS,
        help="Explicit characters to patch. Defaults to the full precomposed Vietnamese alphabet.",
    )
    return parser.parse_args()


def load_font_bytes(path):
    with open(path, "rb") as font_file:
        data = bytearray(font_file.read())

    if len(data) != EXPECTED_FONT_SIZE:
        raise ValueError(
            f"Unexpected font size: {len(data)} bytes. Expected {EXPECTED_FONT_SIZE} bytes."
        )

    return data


def normalize_charset(characters):
    normalized = unicodedata.normalize("NFC", characters)
    unique = []
    seen = set()

    for char in normalized:
        codepoint = ord(char)
        if codepoint >= UNICODE_GLYPH_COUNT:
            continue
        if char in seen:
            continue
        seen.add(char)
        unique.append(char)

    unique.sort(key=ord)
    return unique


def get_base_character(char):
    if char in BASE_CHARACTER_MAP:
        return BASE_CHARACTER_MAP[char]

    decomposed = unicodedata.normalize("NFD", char)

    for candidate in decomposed:
        if not unicodedata.combining(candidate):
            return BASE_CHARACTER_MAP.get(candidate, candidate)

    return char


def is_non_ascii_vietnamese(char):
    return ord(char) > 0x7F


def get_effective_padding_x(char, padding_x):
    if is_non_ascii_vietnamese(char):
        return max(0, padding_x - 2)

    return padding_x


def get_effective_x_offset(char, x_offset):
    if is_non_ascii_vietnamese(char):
        return x_offset - 2

    return x_offset


def use_left_alignment(char):
    return is_non_ascii_vietnamese(char)


def is_stacked_vietnamese(char):
    return char in STACKED_VIETNAMESE_CHARACTERS


def get_effective_fit_height(char, fit_height):
    if is_stacked_vietnamese(char):
        return max(15, fit_height - 3)

    return fit_height


def get_effective_y_offset(char, y_offset):
    if is_stacked_vietnamese(char):
        return y_offset + 2

    return y_offset


def get_effective_target_font_size(char, target_font_size):
    if is_stacked_vietnamese(char):
        return max(8, target_font_size - 2)

    return target_font_size


def fit_font_for_char(font_path, target_font_size, char, padding_x, fit_height):
    effective_padding_x = get_effective_padding_x(char, padding_x)
    usable_width = max(1, GLYPH_WIDTH - (effective_padding_x * 2))

    for current_size in range(target_font_size, 7, -1):
        candidate_font = ImageFont.truetype(font_path, current_size)
        ascent, descent = candidate_font.getmetrics()
        line_height = ascent + descent
        probe_image = Image.new("L", (GLYPH_WIDTH * 3, max(fit_height * 2, line_height * 2)), 0)
        probe_draw = ImageDraw.Draw(probe_image)
        bbox = probe_draw.textbbox((0, 0), char, font=candidate_font)
        draw_x = max(0, -bbox[0])
        draw_y = max(0, -bbox[1])
        probe_draw.text((draw_x, draw_y), char, fill=255, font=candidate_font)
        painted_bbox = probe_image.getbbox()

        if not painted_bbox:
            return candidate_font, None

        glyph_width = painted_bbox[2] - painted_bbox[0]
        glyph_height = painted_bbox[3] - painted_bbox[1]

        if glyph_width <= usable_width and glyph_height <= fit_height:
            return candidate_font, painted_bbox

    fallback_font = ImageFont.truetype(font_path, 8)
    fallback_image = Image.new("L", (GLYPH_WIDTH * 3, GLYPH_HEIGHT * 3), 0)
    fallback_draw = ImageDraw.Draw(fallback_image)
    fallback_bbox = fallback_draw.textbbox((0, 0), char, font=fallback_font)
    fallback_draw.text((max(0, -fallback_bbox[0]), max(0, -fallback_bbox[1])), char, fill=255, font=fallback_font)
    return fallback_font, fallback_image.getbbox()


def get_char_bbox(font, char):
    probe_image = Image.new("L", (GLYPH_WIDTH * 3, GLYPH_HEIGHT * 3), 0)
    probe_draw = ImageDraw.Draw(probe_image)
    bbox = probe_draw.textbbox((0, 0), char, font=font)
    probe_draw.text((max(0, -bbox[0]), max(0, -bbox[1])), char, fill=255, font=font)
    return probe_image.getbbox()


def render_glyph(font_path, target_font_size, char, threshold, x_offset, y_offset, padding_x, fit_height, embolden):
    effective_padding_x = get_effective_padding_x(char, padding_x)
    effective_x_offset = get_effective_x_offset(char, x_offset)
    effective_y_offset = get_effective_y_offset(char, y_offset)
    effective_fit_height = get_effective_fit_height(char, fit_height)
    effective_target_font_size = get_effective_target_font_size(char, target_font_size)
    usable_width = max(1, GLYPH_WIDTH - (effective_padding_x * 2))
    image = Image.new("L", (GLYPH_WIDTH, GLYPH_HEIGHT), 0)

    font, painted_bbox = fit_font_for_char(font_path, effective_target_font_size, char, padding_x, effective_fit_height)

    if painted_bbox:
        probe_image = Image.new("L", (GLYPH_WIDTH * 3, GLYPH_HEIGHT * 3), 0)
        probe_draw = ImageDraw.Draw(probe_image)
        bbox = probe_draw.textbbox((0, 0), char, font=font)
        probe_draw.text((max(0, -bbox[0]), max(0, -bbox[1])), char, fill=255, font=font)
        cropped = probe_image.crop(painted_bbox)

        base_char = get_base_character(char)
        base_bbox = get_char_bbox(font, base_char)
        base_width = glyph_width = cropped.size[0]
        if base_bbox:
            base_width = max(1, base_bbox[2] - base_bbox[0])

        for _ in range(max(0, embolden)):
            cropped = cropped.filter(ImageFilter.MaxFilter(3))

        glyph_width, glyph_height = cropped.size
        fit_top = max(0, (GLYPH_HEIGHT - effective_fit_height) // 2)
        if use_left_alignment(char):
            paste_x = max(0, effective_padding_x + effective_x_offset + ((base_width - glyph_width) // 2))
        else:
            paste_x = effective_padding_x + ((usable_width - glyph_width) // 2) + effective_x_offset
        paste_y = fit_top + ((effective_fit_height - glyph_height) // 2) + effective_y_offset
        image.paste(cropped, (paste_x, paste_y))

    glyph = bytearray(BYTES_PER_GLYPH)
    index = 0

    for col in range(GLYPH_WIDTH):
        for byte_index in range(BYTES_PER_COLUMN):
            value = 0
            for bit in range(8):
                row = byte_index * 8 + bit
                if row >= GLYPH_HEIGHT:
                    continue

                if image.getpixel((col, row)) >= threshold:
                    value |= 1 << (7 - bit)

            glyph[index] = value
            index += 1

    return glyph


def patch_glyphs(font_bytes, font_path, target_font_size, characters, threshold, x_offset, y_offset, padding_x, fit_height, embolden):
    for char in characters:
        codepoint = ord(char)
        glyph = render_glyph(font_path, target_font_size, char, threshold, x_offset, y_offset, padding_x, fit_height, embolden)
        offset = codepoint * BYTES_PER_GLYPH
        font_bytes[offset:offset + BYTES_PER_GLYPH] = glyph


def main():
    args = parse_args()

    font_bytes = load_font_bytes(args.input)
    characters = normalize_charset(args.chars)
    patch_glyphs(
        font_bytes,
        args.font,
        args.font_size,
        characters,
        args.threshold,
        args.x_offset,
        args.y_offset,
        args.padding_x,
        args.fit_height,
        args.embolden,
    )

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)

    with open(args.output, "wb") as output_file:
        output_file.write(font_bytes)

    print(f"Patched {len(characters)} Vietnamese glyphs into: {args.output}")


if __name__ == "__main__":
    main()