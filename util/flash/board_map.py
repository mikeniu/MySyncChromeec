# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Static legacy board-to-chip lookup tables for EC flashing."""

from typing import Optional


BOARDS_IT83XX = {
    "adlrvpm_ite",
    "adlrvpp_ite",
    "cherry",
    "corsola",
    "dedede",
    "elm",
    "fizz",
    "geralt",
    "hayato",
    "jacuzzi",
    "kalista",
    "krane",
    "kukui",
    "oak",
    "poppy",
    "rambi",
    "sarien",
    "staryu",
    "veluza",
}

BOARDS_IT83XX_SPI_PROGRAMMING = {
    "it8xxx2_evb",
}

BOARDS_NPCX_UUT = {
    "ambassador",
    "brya",
    "constitution",
    "drallion",
    "endeavour",
    "grunt",
    "guybrush",
    "hatch",
    "nissa",
    "octopus",
    "ptlrvp_npcx",
    "puff",
    "rex",
    "skyrim",
    "volteer",
    "zork",
}

BOARDS_NPCX_INT_SPI = {
    "atlas",
    "caroline",
    "cave",
    "chell",
    "coral",
    "eve",
    "glados",
    "kevin",
    "lars",
    "mtlrvpp_npcx",
    "nami",
    "nautilus",
    "nocturne",
    "pyro",
    "rammus",
    "reef",
    "rowan",
    "scarlet",
    "sentry",
    "sion",
    "snappy",
    "soraka",
    "strago",
}

BOARDS_NPCX_SPI = {
    "buccaneer",
    "helipilot",
    "rosalia",
}

BOARDS_NPCX_5M5G_JTAG = {
    "npcx_evb",
}

BOARDS_STM32 = {
    "bloonchipper",
    "chell_pd",
    "chocodile_bec",
    "dartmonkey",
    "glados_pd",
    "hatch_fp",
    "jerry",
    "nami_fp",
    "nocturne_fp",
    "oak_pd",
    "pit",
    "strago_pd",
    "trogdor",
}

BOARDS_DFU = {
    "c2d2",
    "fluffy",
    "hammer",
    "servo_micro",
    "servo_v4",
    "servo_v4p1",
    "staff",
    "star",
    "sweetberry",
    "twinkie",
    "wand",
    "whiskers",
    "zinger",
}

BOARDS_MCHP_INT_UART = {
    "mtlrvp_mchp",
    "mtlrvpp_mchp",
    "ptlrvp_mchp",
}

_CHIP_BOARD_SETS = (
    ("stm32_dfu", BOARDS_DFU),
    ("stm32", BOARDS_STM32),
    ("npcx_5m5g_jtag", BOARDS_NPCX_5M5G_JTAG),
    ("npcx_spi", BOARDS_NPCX_SPI),
    ("npcx_int_spi", BOARDS_NPCX_INT_SPI),
    ("npcx_uut", BOARDS_NPCX_UUT),
    ("it83xx", BOARDS_IT83XX),
    ("ite_spi", BOARDS_IT83XX_SPI_PROGRAMMING),
    ("mchp", BOARDS_MCHP_INT_UART),
)


def get_chip_from_board(board: str) -> Optional[str]:
    """Return the legacy EC chip type for a given board name, or None."""
    if not board:
        return None
    b_lower = board.lower().strip()
    for chip_type, board_set in _CHIP_BOARD_SETS:
        if b_lower in board_set:
            return chip_type
    return None
