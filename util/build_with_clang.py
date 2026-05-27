#!/usr/bin/env python3
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Build firmware with clang instead of gcc."""

import argparse
import concurrent
from concurrent.futures import ThreadPoolExecutor
import logging
import math
import multiprocessing
import shutil
import subprocess
import sys
import typing


# Add to this list as compilation errors are fixed for boards.
BOARDS_THAT_COMPILE_SUCCESSFULLY_WITH_CLANG = [
    # Fingerprint boards
    "dartmonkey",
    "buccaneer",
    "helipilot",
    "nami_fp",
    "nucleo-dartmonkey",
    "nucleo-f412zg",
    "nucleo-h743zi",
    "rosalia",
    # Boards that use CHIP:=stm32 and *not* CHIP_FAMILY:=stm32f0
    # git grep  --name-only 'CHIP:=stm32' | xargs grep -L \
    #   'CHIP_FAMILY:=stm32f0' | sed 's#board/\(.*\)/build.mk#"\1",#'
    "baklava",
    "hyperdebug",
    "nocturne_fp",
    "panqueque",
    "quiche",
    "sweetberry",
    # Boards that use CHIP:=stm32 *and* CHIP_FAMILY:=stm32f0
    # git grep  --name-only 'CHIP:=stm32' | xargs grep -L \
    #   'CHIP_FAMILY:=stm32f0' | sed 's#board/\(.*\)/build.mk#"\1",#'
    "c2d2",
    "don",
    "duck",
    "eel",
    "elm",
    "fluffy",
    "hammer",
    "jewel",
    "kakadu",
    "kappa",
    "katsu",
    "kukui",
    "magnemite",
    "masterball",
    "moonball",
    "prism",
    "servo_micro",
    "servo_v4",
    "servo_v4p1",
    "staff",
    "star",
    "whitebeard",
    "zed",
    # Boards that use CHIP:=mchp
    # git grep --name-only 'CHIP:=mchp' | sed 's#board/\(.*\)/build.mk#"\1",#'
    "adlrvpp_mchp1727",
    # Boards that use CHIP:=npcx
    # git grep --name-only 'CHIP:=npcx' | sed 's#^board/\(.*\)/build.mk#"\1",#'
    "akemi",
    "aleena",
    "ambassador",
    "anahera",
    "atlas",
    "aurash",
    "banshee",
    "berknip",
    "bloog",
    "brask",
    "brya",
    "bugzzy",
    "bujia",
    "cappy2",
    "careena",
    "casta",
    "chronicler",
    "collis",
    "constitution",
    "copano",
    "coral",
    "corori",
    "crota",
    "dalboz",
    "delbin",
    "dewatt",
    "dirinboz",
    "dochi",
    "dood",
    "dooly",
    "dratini",
    "driblee",
    "drobit",
    "eldrid",
    "elemi",
    "endeavour",
    "eve",
    "ezkinil",
    "felwinter",
    "fleex",
    "foob",
    "gaelin",
    "genesis",
    "gimble",
    "gladios",
    "grunt",
    "gumboz",
    "guybrush",
    "hatch",
    "helios",
    "homestar",
    "jinlon",
    "kano",
    "karma",
    "kindred",
    "kingoftown",
    "kinox",
    "kohaku",
    "kuldax",
    "kulnex",
    "lalala",
    "lazor",
    "liara",
    "lick",
    "lindar",
    "lisbon",
    "madoo",
    "magolor",
    "marasov",
    "marzipan",
    "meep",
    "metaknight",
    "mithrax",
    "moli",
    "moonbuggy",
    "morphius",
    "moxie",
    "moxoe",
    "nami",
    "nautilus",
    "nightfury",
    "nipperkin",
    "nocturne",
    "nova",
    "nuwani",
    "omnigul",
    "osiris",
    "pazquel",
    "phaser",
    "pompom",
    "poppy",
    "primus",
    "puff",
    "quackingstick",
    "rammus",
    "redrix",
    "reef",
    "sasuke",
    "scout",
    "shuboz",
    "soraka",
    "stryke",
    "taeko",
    "taniks",
    "treeya",
    "trembyle",
    "trogdor",
    "vell",
    "vilboz",
    "voema",
    "volet",
    "volmar",
    "voxel",
    "whiskers",
    "woomax",
    "wormdingler",
    "xol",
    "yorp",
    # CHIP=mt_scp *and* CHIP_VARIANT=mt818x
    # git grep --name-only 'CHIP:=mt_scp' | xargs grep -L \
    #   'CHIP_VARIANT:=mt818' | sed 's#board/\(.*\)/build.mk#"\1",#'
    "corsola_scp",
    "kukui_scp",
    # Boards that use CORE:=minute-ia
    "arcada_ish",
    "drallion_ish",
]

NDS32_BOARDS = [
    "adlrvpm_ite",
    "adlrvpp_ite",
    "ampton",
    "awasuki",
    "beadrix",
    "blipper",
    "boten",
    "boxy",
    "dexi",
    "dibbi",
    "dita",
    "drawcia",
    "galtic",
    "gooey",
    "haboki",
    "kracko",
    "lantis",
    "pirika",
    "sasukette",
    "shotzo",
    "storo",
    "taranza",
    "waddledee",
]

RISCV_BOARDS = [
    "asurada",
    "asurada_scp",
    "cherry",
    "cherry_scp",
    "cherry_scp_core1",
    "cozmo",
    "dojo",
    "geralt_scp",
    "geralt_scp_core1",
    "hayato",
    "icarus",
    "pico",
    "spherion",
    "tomato",
]

BOARDS_THAT_FAIL_WITH_CLANG = [
    # Boards that use CHIP:=npcx
    "garg",
    # Boards that don't fit in flash with clang
    "cerise",
    "cret",
    "munna",
    "volteer",
    "willow",
    # Not enough flash space with CONFIG_POWER_SLEEP_FAILURE_DETECTION enabled
    "bobba",
    "burnet",
    "coachz",
    "cret",
    "damu",
    "fennel",
    "fizz",
    "gelarshie",
    "jacuzzi",
    "juniper",
    "kodama",
    "krane",
    "makomo",
    "waddledoo",
]

# TODO(b/201311714): NDS32 is not supported by LLVM.
BOARDS_THAT_FAIL_WITH_CLANG += NDS32_BOARDS
# TODO(b/201310017): RISC-V is not supported in our LLVM toolchain.
BOARDS_THAT_FAIL_WITH_CLANG += RISCV_BOARDS


def build(board_name: str, max_cpus: int) -> None:
    """Build with clang for specified board."""
    logging.debug('Building board: "%s"', board_name)
    cmd = [
        "make",
        "BOARD=" + board_name,
        "CROSS_COMPILE_CC_NAME=clang",
        f"-j{max_cpus}",
    ]
    logging.debug('Running command: "%s"', " ".join(cmd))
    subprocess.run(cmd, check=True)


def get_all_boards() -> typing.List[str]:
    """Return the list of all EC boards."""
    cmd = [
        "make",
        "print-boards",
    ]

    logging.debug('Running command: "%s"', " ".join(cmd))
    ret = subprocess.run(cmd, stdout=subprocess.PIPE, check=True)
    all_boards = ret.stdout.decode("utf-8").splitlines()
    return all_boards


def check_boards() -> None:
    """Checks that all boards are explicitly mentioned in this source."""
    all_boards = get_all_boards()
    diff = set(all_boards) ^ set(
        BOARDS_THAT_COMPILE_SUCCESSFULLY_WITH_CLANG
        + BOARDS_THAT_FAIL_WITH_CLANG
    )
    if len(diff) > 0:
        print(
            "The following boards are missing and must be added to "
            "BOARDS_THAT_COMPILE_SUCCESSFULLY_WITH_CLANG or "
            "BOARDS_THAT_FAIL_WITH_CLANG:"
        )
        for i in sorted(diff):
            print(i)
        sys.exit(1)


def calculate_job_distribution(jobs_arg: int) -> typing.Tuple[int, int]:
    """Calculates how to distribute our `-j` argument.

    Returns:
        A tuple of:
            - The number of `make` invocations to run concurrently
            - The number of jobs for each `make` invocation to run on its own
    """
    # Experimentally (see comments on crrev.com/c/5723373), splitting the job
    # count evenly results in the fastest builds by a statistically significant
    # margin. So have ${M} `make` jobs each with `-j${M}`
    split_jobs = math.sqrt(jobs_arg)

    # If there's a fractional part, prefer to potentially overutilize rather
    # than always underutilize. `make -j${N}` often takes less than ${N} * 100%
    # CPU.
    if split_jobs % 1:
        split_jobs += 1
    make_threads = int(split_jobs)
    jobs_per_make = make_threads
    return make_threads, jobs_per_make


def main() -> int:
    """The mainest function of them all.

    Returns:
        The posix exit status.
    """
    parser = argparse.ArgumentParser()

    log_level_choices = ["DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"]
    parser.add_argument(
        "--log_level", "-l", choices=log_level_choices, default="DEBUG"
    )

    parser.add_argument(
        "--num_threads", "-j", type=int, default=multiprocessing.cpu_count()
    )

    group = parser.add_mutually_exclusive_group(required=False)
    group.add_argument(
        "--clean",
        action="store_true",
        help="Remove build directory before compiling",
    )
    group.add_argument(
        "--no-clean",
        dest="clean",
        action="store_false",
        help="Do not remove build directory before compiling",
    )
    parser.set_defaults(clean=True)

    args = parser.parse_args()
    logging.basicConfig(level=args.log_level)

    if args.clean:
        logging.debug("Removing build directory")
        shutil.rmtree("./build", ignore_errors=True)

    check_boards()

    logging.debug("Building with %d threads", args.num_threads)

    failed_boards = []

    make_threads, jobs_per_make = calculate_job_distribution(args.num_threads)
    with ThreadPoolExecutor(max_workers=make_threads) as executor:
        future_to_board = {
            executor.submit(build, board, jobs_per_make): board
            for board in BOARDS_THAT_COMPILE_SUCCESSFULLY_WITH_CLANG
        }
        for future in concurrent.futures.as_completed(future_to_board):
            board = future_to_board[future]
            try:
                future.result()
            except subprocess.CalledProcessError:
                failed_boards.append(board)

    if len(failed_boards) > 0:
        failed_boards.sort()
        logging.error(
            "The following boards failed to compile:\n%s",
            "\n".join(failed_boards),
        )
        return 1

    logging.info("All boards compiled successfully!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
