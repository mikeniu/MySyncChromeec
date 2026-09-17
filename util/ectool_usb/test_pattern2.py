#!/usr/bin/env python3
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Standalone script to test pattern2 (image constant test) PGM file.

This script analyzes a PGM file captured from the fingerprint sensor's
internal constant test pattern (pattern2) and verifies:
1. The overall image median is within the allowed range.
2. Pixels do not deviate too far from the overall median.
3. Slot medians (horizontal strips) are within the allowed range.

Based on the ImageConstantTest method in fingerprint_sensor_fpc.py.
"""

import argparse
import sys
from pathlib import Path

import numpy as np


# Default test parameters (from fingerprint_sensor_fpc.py)
DEFAULT_IC_MEDIAN = (168, 241)  # [min, max] range for slot median
DEFAULT_IC_MAX_PIXEL_DEV = 25   # max deviation from overall median
DEFAULT_IC_MAX_DEAD_PIXELS = 10 # max allowed dead pixels


def read_pgm(filepath: Path) -> tuple[np.ndarray, int, int]:
    """Read a PGM file and return the pixel array, width, and height.

    Supports both P2 (ASCII) and P5 (binary) PGM formats.

    Args:
        filepath: Path to the PGM file.

    Returns:
        A tuple of (pixel_array, width, height).
        pixel_array is a 2D numpy array of shape (height, width).

    Raises:
        ValueError: If the file format is invalid or unsupported.
    """
    with open(filepath, 'rb') as f:
        # Read magic number
        magic = f.readline().decode('ascii').strip()
        if magic not in ('P2', 'P5'):
            raise ValueError(f'Unsupported PGM format: {magic}')

        # Skip comments
        line = f.readline().decode('ascii').strip()
        while line.startswith('#'):
            line = f.readline().decode('ascii').strip()

        # Read dimensions
        width, height = map(int, line.split())

        # Read max value
        maxval = int(f.readline().decode('ascii').strip())

        # Read pixel data
        if magic == 'P2':
            # ASCII format
            pixels = []
            for line in f:
                line = line.decode('ascii').strip()
                if line:
                    pixels.extend(map(int, line.split()))
            data = np.array(pixels, dtype=np.uint16)
        else:
            # Binary format (P5)
            data = np.frombuffer(f.read(), dtype=np.uint8)
            if maxval > 255:
                data = np.frombuffer(f.read(), dtype='>u2')

        # Reshape to 2D array (height x width)
        if data.size != width * height:
            raise ValueError(
                f'Pixel data size mismatch: expected {width * height}, '
                f'got {data.size}')
        pixels = data.reshape((height, width))

    return pixels, width, height


def analyze_image_constant_test(
    pixels: np.ndarray,
    ic_median: tuple[int, int] = DEFAULT_IC_MEDIAN,
    ic_max_pixel_dev: int = DEFAULT_IC_MAX_PIXEL_DEV,
    ic_max_dead_pixels: int = DEFAULT_IC_MAX_DEAD_PIXELS,
) -> dict:
    """Analyze the image constant test pattern.

    Args:
        pixels: 2D numpy array of pixel values (height x width).
        ic_median: (min, max) allowed range for slot medians.
        ic_max_pixel_dev: Maximum allowed deviation from overall median.
        ic_max_dead_pixels: Maximum allowed number of dead pixels.

    Returns:
        A dictionary containing the analysis results:
        - overall_median: The median of all pixel values.
        - slot_medians: List of medians for each horizontal slot.
        - dead_pixel_count: Number of pixels deviating too far from median.
        - out_of_range_medians: List of slot medians outside allowed range.
        - passed: Boolean indicating if the test passed.
        - errors: List of error messages if test failed.
    """
    height, width = pixels.shape
    ic_min, ic_max = ic_median

    # Calculate overall median
    pixel_values = pixels.flatten()
    overall_median = float(np.median(pixel_values))

    # Calculate slot medians (divide image into ~10 horizontal slots)
    slot_size = max(1, height // 10)
    slot_medians = []
    for i in range(0, height, slot_size):
        slot = pixels[i:i + slot_size, :].flatten()
        slot_medians.append(float(np.median(slot)))

    # Check 1: slot medians within allowed range
    out_of_range_medians = [
        m for m in slot_medians if m < ic_min or m > ic_max
    ]

    # Check 2: count pixels deviating too far from overall median
    deviation_map = np.abs(pixel_values - overall_median)
    dead_pixel_count = int(np.count_nonzero(deviation_map > ic_max_pixel_dev))

    # Determine pass/fail
    errors = []
    if dead_pixel_count > ic_max_dead_pixels:
        errors.append(
            f'Too many dead pixels: {dead_pixel_count} '
            f'(limit: {ic_max_dead_pixels})')
    if out_of_range_medians:
        errors.append(
            f'Slot medians out of range [{ic_min}, {ic_max}]: '
            f'{out_of_range_medians}')

    passed = len(errors) == 0

    return {
        'overall_median': overall_median,
        'slot_medians': slot_medians,
        'dead_pixel_count': dead_pixel_count,
        'out_of_range_medians': out_of_range_medians,
        'passed': passed,
        'errors': errors,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description='Test pattern2 (image constant test) PGM file.')
    parser.add_argument(
        'pgm_file',
        type=Path,
        help='Path to the PGM file to test.')
    parser.add_argument(
        '--ic-min',
        type=int,
        default=DEFAULT_IC_MEDIAN[0],
        help=f'Minimum allowed slot median (default: {DEFAULT_IC_MEDIAN[0]})')
    parser.add_argument(
        '--ic-max',
        type=int,
        default=DEFAULT_IC_MEDIAN[1],
        help=f'Maximum allowed slot median (default: {DEFAULT_IC_MEDIAN[1]})')
    parser.add_argument(
        '--max-pixel-dev',
        type=int,
        default=DEFAULT_IC_MAX_PIXEL_DEV,
        help=f'Maximum pixel deviation from median '
             f'(default: {DEFAULT_IC_MAX_PIXEL_DEV})')
    parser.add_argument(
        '--max-dead-pixels',
        type=int,
        default=DEFAULT_IC_MAX_DEAD_PIXELS,
        help=f'Maximum allowed dead pixels '
             f'(default: {DEFAULT_IC_MAX_DEAD_PIXELS})')
    parser.add_argument(
        '-v', '--verbose',
        action='store_true',
        help='Print detailed analysis results.')

    args = parser.parse_args()

    # Read the PGM file
    try:
        pixels, width, height = read_pgm(args.pgm_file)
    except (FileNotFoundError, ValueError) as e:
        print(f'Error reading PGM file: {e}', file=sys.stderr)
        return 1

    print(f'Image: {width}x{height}')

    # Analyze the image
    results = analyze_image_constant_test(
        pixels,
        ic_median=(args.ic_min, args.ic_max),
        ic_max_pixel_dev=args.max_pixel_dev,
        ic_max_dead_pixels=args.max_dead_pixels,
    )

    # Print results
    print(f'Overall median: {results["overall_median"]:.1f}')
    print(f'Dead pixels: {results["dead_pixel_count"]} '
          f'(limit: {args.max_dead_pixels})')

    if args.verbose:
        print(f'Slot medians: {results["slot_medians"]}')
        print(f'Out of range medians: {results["out_of_range_medians"]}')

    # Print pass/fail result
    if results['passed']:
        print('\nResult: PASS')
        return 0
    else:
        print('\nResult: FAIL')
        for error in results['errors']:
            print(f'  - {error}')
        return 1


if __name__ == '__main__':
    sys.exit(main())
