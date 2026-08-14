#!/usr/bin/env python3

import struct
import sys
import zlib


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def paeth(left, above, upper_left):
    prediction = left + above - upper_left
    left_distance = abs(prediction - left)
    above_distance = abs(prediction - above)
    upper_left_distance = abs(prediction - upper_left)
    if left_distance <= above_distance and left_distance <= upper_left_distance:
        return left
    if above_distance <= upper_left_distance:
        return above
    return upper_left


def decode_png(path):
    with open(path, "rb") as image_file:
        contents = image_file.read()

    if not contents.startswith(PNG_SIGNATURE):
        raise ValueError("invalid PNG signature")

    offset = len(PNG_SIGNATURE)
    header = None
    compressed = bytearray()

    while offset < len(contents):
        if offset + 12 > len(contents):
            raise ValueError("truncated PNG chunk")
        length = struct.unpack(">I", contents[offset:offset + 4])[0]
        chunk_type = contents[offset + 4:offset + 8]
        chunk_start = offset + 8
        chunk_end = chunk_start + length
        crc_end = chunk_end + 4
        if crc_end > len(contents):
            raise ValueError("truncated PNG payload")

        payload = contents[chunk_start:chunk_end]
        expected_crc = struct.unpack(">I", contents[chunk_end:crc_end])[0]
        actual_crc = zlib.crc32(chunk_type)
        actual_crc = zlib.crc32(payload, actual_crc) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            raise ValueError("PNG chunk checksum mismatch")

        if chunk_type == b"IHDR":
            if length != 13:
                raise ValueError("invalid PNG header")
            header = struct.unpack(">IIBBBBB", payload)
        elif chunk_type == b"IDAT":
            compressed.extend(payload)
        elif chunk_type == b"IEND":
            break

        offset = crc_end

    if header is None or not compressed:
        raise ValueError("PNG is missing image data")

    width, height, bit_depth, color_type, compression, filtering, interlace = header
    if bit_depth != 8 or color_type not in (2, 6):
        raise ValueError("only 8-bit RGB and RGBA PNG files are supported")
    if compression != 0 or filtering != 0 or interlace != 0:
        raise ValueError("unsupported PNG encoding")

    bytes_per_pixel = 3 if color_type == 2 else 4
    stride = width * bytes_per_pixel
    raw = zlib.decompress(bytes(compressed))
    expected_size = height * (stride + 1)
    if len(raw) != expected_size:
        raise ValueError("unexpected decompressed PNG size")

    rows = []
    previous = bytearray(stride)
    raw_offset = 0
    for _ in range(height):
        filter_type = raw[raw_offset]
        source = raw[raw_offset + 1:raw_offset + 1 + stride]
        row = bytearray(stride)
        raw_offset += stride + 1

        for index, value in enumerate(source):
            left = row[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
            above = previous[index]
            upper_left = previous[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
            if filter_type == 0:
                reconstructed = value
            elif filter_type == 1:
                reconstructed = value + left
            elif filter_type == 2:
                reconstructed = value + above
            elif filter_type == 3:
                reconstructed = value + ((left + above) // 2)
            elif filter_type == 4:
                reconstructed = value + paeth(left, above, upper_left)
            else:
                raise ValueError("unknown PNG row filter")
            row[index] = reconstructed & 0xFF

        rows.append(row)
        previous = row

    return width, height, bytes_per_pixel, rows


def validate_underwater_scene(width, height, bytes_per_pixel, rows):
    sample_count = 0
    water_color_count = 0
    neutral_color_count = 0

    # The E2E deep-ocean camera leaves this band unobstructed by the HUD or
    # seabed. A leaked sky clear color is neutral gray; underwater fog is cyan.
    for y in range(height * 35 // 100, height * 60 // 100):
        row = rows[y]
        for x in range(width * 40 // 100, width * 90 // 100):
            offset = x * bytes_per_pixel
            red, green, blue = row[offset:offset + 3]
            sample_count += 1
            if green >= red + 20 and blue >= red + 20:
                water_color_count += 1
            if max(red, green, blue) - min(red, green, blue) <= 16:
                neutral_color_count += 1

    water_ratio = water_color_count / sample_count
    neutral_ratio = neutral_color_count / sample_count
    if water_ratio < 0.80 or neutral_ratio > 0.20:
        raise ValueError(
            "underwater background is not fog-colored: "
            f"water_ratio={water_ratio:.3f} neutral_ratio={neutral_ratio:.3f}"
        )


def validate(path, expected_width, expected_height, allow_dark_ui=False,
             underwater_scene=False):
    width, height, bytes_per_pixel, rows = decode_png(path)
    if (width, height) != (expected_width, expected_height):
        raise ValueError(
            f"unexpected image dimensions {width}x{height}, "
            f"expected {expected_width}x{expected_height}"
        )

    x_step = max(1, width // 256)
    y_step = max(1, height // 144)
    colors = set()
    channel_min = [255, 255, 255]
    channel_max = [0, 0, 0]
    luminance_min = 255
    luminance_max = 0

    for y in range(0, height, y_step):
        row = rows[y]
        for x in range(0, width, x_step):
            offset = x * bytes_per_pixel
            red, green, blue = row[offset:offset + 3]
            colors.add((red, green, blue))
            for channel, value in enumerate((red, green, blue)):
                channel_min[channel] = min(channel_min[channel], value)
                channel_max[channel] = max(channel_max[channel], value)
            luminance = (2126 * red + 7152 * green + 722 * blue) // 10000
            luminance_min = min(luminance_min, luminance)
            luminance_max = max(luminance_max, luminance)

    varied_channels = sum(
        maximum - minimum >= 16
        for minimum, maximum in zip(channel_min, channel_max)
    )
    minimum_colors = 8 if allow_dark_ui else 64
    if len(colors) < minimum_colors or varied_channels < 2 or luminance_max - luminance_min < 24:
        raise ValueError(
            "image appears blank or visually degenerate: "
            f"colors={len(colors)} channels={list(zip(channel_min, channel_max))} "
            f"luminance={luminance_min}..{luminance_max}"
        )

    if underwater_scene:
        validate_underwater_scene(width, height, bytes_per_pixel, rows)

    print(
        f"PNG validation passed: {width}x{height}, "
        f"sampled_colors={len(colors)}, luminance={luminance_min}..{luminance_max}"
    )


def main():
    options = set(sys.argv[4:])
    valid_options = {"--allow-dark-ui", "--underwater-scene"}
    if len(sys.argv) < 4 or not options.issubset(valid_options) or \
            len(options) != len(sys.argv[4:]):
        print("usage: validate_png.py PATH WIDTH HEIGHT "
              "[--allow-dark-ui] [--underwater-scene]", file=sys.stderr)
        return 2
    try:
        validate(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]),
                 "--allow-dark-ui" in options,
                 "--underwater-scene" in options)
    except (OSError, ValueError, zlib.error) as error:
        print(f"PNG validation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
