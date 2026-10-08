#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Decodes tokenized Zephyr EC logs using Pigweed pw_tokenizer.

This script detokenizes Base64-encoded tokenized log messages from EC logs,
EC console output, or standalone token strings using local build databases,
ELF files, or the historical database.

Usage:
    # Decode a log file:
    ./util/decode_tokenized_logs.py failed_log.txt
    ./util/decode_tokenized_logs.py -i failed_log.txt

    # Decode from stdin (pipe):
    cat failed_log.txt | ./util/decode_tokenized_logs.py

    # Decode an inline string or token:
    ./util/decode_tokenized_logs.py -s "`vO2VnwAMQXR0YWNoZWQuU05LClJEX1BXUl9MVkw="
    ./util/decode_tokenized_logs.py "vO2VnwAMQXR0YWNoZWQuU05LClJEX1BXUl9MVkw="

    # Use a specific database or ELF:
    ./util/decode_tokenized_logs.py -d build/tokens.bin -i failed_log.txt
    ./util/decode_tokenized_logs.py -d build/zephyr/skyrim/output/zephyr.ro.elf -i failed_log.txt

    # Force download/update of the historical token database:
    ./util/decode_tokenized_logs.py --update-db -i failed_log.txt
"""

import argparse
import base64
import os
import pathlib
import re
import sys
import urllib.request


EC_DIR = pathlib.Path(__file__).resolve().parent.parent
HISTORICAL_DB_URL = (
    "https://storage.googleapis.com/chromeos-localmirror/distfiles/cros_ec/"
    "tokens/chromeos-ec-token-historical.bin"
)
CACHE_DIR = pathlib.Path.home() / ".cache" / "cros_ec" / "tokens"
CACHE_HISTORICAL_DB = CACHE_DIR / "chromeos-ec-token-historical.bin"

# Setup pigweed pw_tokenizer search paths before import.
_PW_CANDIDATES = [
    EC_DIR.parent.parent / "third_party" / "pigweed" / "pw_tokenizer" / "py",
    pathlib.Path("/mnt/host/source/src/third_party/pigweed/pw_tokenizer/py"),
]
for _candidate in _PW_CANDIDATES:
    if _candidate.is_dir() and str(_candidate) not in sys.path:
        sys.path.insert(0, str(_candidate))

# pylint: disable=import-error, wrong-import-position
try:
    from pw_tokenizer import detokenize
except ImportError:
    detokenize = None
# pylint: enable=import-error, wrong-import-position


def download_historical_db(dest_path: pathlib.Path) -> None:
    """Downloads the historical token database from GCS."""
    dest_path.parent.mkdir(parents=True, exist_ok=True)
    print(
        f"Downloading historical database from {HISTORICAL_DB_URL} to {dest_path}...",
        file=sys.stderr,
    )
    temp_path = dest_path.with_suffix(".tmp")
    urllib.request.urlretrieve(HISTORICAL_DB_URL, temp_path)
    temp_path.replace(dest_path)
    print("Download complete.", file=sys.stderr)


def find_token_databases(
    explicit_db: str | None = None,
    board: str | None = None,
    force_update: bool = False,
) -> list[pathlib.Path]:
    """Finds suitable token databases or downloads the historical database."""
    if explicit_db:
        p = pathlib.Path(explicit_db)
        if not p.exists():
            sys.exit(f"Error: Specified database does not exist: {explicit_db}")
        return [p]

    if force_update:
        download_historical_db(CACHE_HISTORICAL_DB)
        return [CACHE_HISTORICAL_DB]

    if board:
        board_db = (
            EC_DIR / "build" / "zephyr" / board / "output" / "database.bin"
        )
        if board_db.exists():
            return [board_db]
        board_elf = (
            EC_DIR / "build" / "zephyr" / board / "output" / "zephyr.ro.elf"
        )
        if board_elf.exists():
            return [board_elf]

    databases: list[pathlib.Path] = []

    # Local unified build DB
    local_unified = EC_DIR / "build" / "tokens.bin"
    if local_unified.exists():
        databases.append(local_unified)

    # Historical / System DBs (prefer cached updated DB, then system paths)
    historical_candidates = [
        CACHE_HISTORICAL_DB,
        pathlib.Path("/usr/share/cros_ec/tokens/historical.bin"),
        pathlib.Path("/usr/share/cros_ec/tokens.bin"),
        pathlib.Path("/usr/local/cros_ec/tokens.bin"),
        pathlib.Path("/tmp/chromeos-ec-token-historical.bin"),
    ]
    for sp in historical_candidates:
        if sp.exists() and sp not in databases:
            databases.append(sp)
            break

    if not databases:
        download_historical_db(CACHE_HISTORICAL_DB)
        databases.append(CACHE_HISTORICAL_DB)

    return databases


def detokenize_content(
    detok,
    content: str | bytes,
    prefix: str = "`",
    strip_end_delimiter: bool = True,
) -> str:
    """Detokenizes input text content using the provided Detokenizer."""
    if isinstance(content, str):
        content_bytes = content.encode("utf-8", errors="surrogateescape")
    else:
        content_bytes = content

    # If the input is a single token without the prefix, add prefix for detokenizer
    stripped = content_bytes.strip()
    if prefix.encode() not in stripped and re.fullmatch(
        rb"[A-Za-z0-9+/=]+", stripped
    ):
        content_bytes = prefix.encode() + stripped

    result_bytes = detok._detokenize_nested(  # pylint: disable=protected-access
        content_bytes, detokenize.DEFAULT_RECURSION
    )

    # Fallback for tokens merged directly with adjacent alphanumeric text
    # (e.g. `tokenRTK0:`) where greedy Base64 matching swallowed trailing characters.
    def fallback_replace(match: re.Match[bytes]) -> bytes:
        raw_b64 = match.group(1)
        cur = raw_b64
        suffix = b""
        while len(cur) >= 4:
            pad_needed = (4 - (len(cur) % 4)) % 4
            test_b64 = cur + (b"=" * pad_needed)
            try:
                raw_bytes = base64.b64decode(test_b64)
                r = detok.detokenize(raw_bytes)
                if r.ok():
                    return str(r).encode("utf-8") + suffix
            except Exception:  # pylint: disable=broad-exception-caught
                pass
            suffix = cur[-1:] + suffix
            cur = cur[:-1]
        return match.group(0)

    pattern = re.compile(rf"{re.escape(prefix)}([A-Za-z0-9+/=]{{4,}})".encode())
    result_bytes = pattern.sub(fallback_replace, result_bytes)

    result_str = result_bytes.decode("utf-8", errors="replace")

    if strip_end_delimiter:
        # kEndDelimiter is '~' in pw_log_zephyr_tokenized.cc. Remove '~' after
        # decoded newlines or log boundaries.
        result_str = re.sub(r"(\r?\n)~", r"\1", result_str)
        result_str = re.sub(r"~(\r?\n|$)", r"\1", result_str)

    return result_str


def main() -> None:
    """Parses arguments and decodes tokenized EC logs."""
    if detokenize is None:
        sys.exit(
            "Error: Could not import 'pw_tokenizer'. Please install pw_tokenizer "
            "or ensure third_party/pigweed is present."
        )

    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "file_or_string",
        nargs="?",
        help="Path to log file, or inline token string to decode.",
    )
    parser.add_argument(
        "-i",
        "--input",
        dest="input_file",
        help="Input file to detokenize (default: stdin if no positional input).",
    )
    parser.add_argument(
        "-s",
        "--string",
        dest="inline_string",
        help="Inline string or token to detokenize.",
    )
    parser.add_argument(
        "-o",
        "--output",
        dest="output_file",
        help="Output file (default: stdout).",
    )
    parser.add_argument(
        "-d",
        "--database",
        "--token-db",
        dest="database",
        help="Path to token database (.bin, .csv) or ELF file (.elf).",
    )
    parser.add_argument(
        "-b",
        "--board",
        dest="board",
        help="Board name (to look up build/zephyr/<board>/output/database.bin).",
    )
    parser.add_argument(
        "-p",
        "--prefix",
        dest="prefix",
        default="`",
        help="Prefix character for tokenized messages (default: '`').",
    )
    parser.add_argument(
        "--update-db",
        action="store_true",
        help="Force download/update of the latest historical token database.",
    )
    parser.add_argument(
        "-q",
        "--quiet",
        action="store_true",
        help="Suppress informational stderr messages.",
    )
    parser.add_argument(
        "--no-strip-delimiter",
        action="store_false",
        dest="strip_delimiter",
        help="Do not strip Zephyr's '~' end delimiter from output.",
    )

    args = parser.parse_args()

    db_paths = find_token_databases(
        explicit_db=args.database,
        board=args.board,
        force_update=args.update_db,
    )

    if not args.quiet:
        for p in db_paths:
            print(f"Using token database: {p}", file=sys.stderr)

    detok = detokenize.Detokenizer(
        *(str(p) for p in db_paths), prefix=args.prefix
    )

    # Determine input content
    if args.inline_string:
        content = args.inline_string
    elif args.file_or_string and not os.path.exists(args.file_or_string):
        # Positional argument passed but file doesn't exist; treat as inline string
        content = args.file_or_string
    else:
        input_path = args.input_file or args.file_or_string
        if input_path and input_path != "-":
            with open(input_path, "rb") as f:
                content = f.read()
        else:
            content = sys.stdin.buffer.read()

    output_str = detokenize_content(
        detok,
        content,
        prefix=args.prefix,
        strip_end_delimiter=args.strip_delimiter,
    )

    if args.output_file and args.output_file != "-":
        with open(args.output_file, "w", encoding="utf-8") as f:
            f.write(output_str)
    else:
        sys.stdout.write(output_str)
        sys.stdout.flush()


if __name__ == "__main__":
    main()
