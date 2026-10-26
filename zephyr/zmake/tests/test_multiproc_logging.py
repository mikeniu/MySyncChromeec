# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for zmake multiproc logging code."""

from collections.abc import Iterator
import io
import logging
import os
import threading
from unittest import mock

import pytest

# pylint:disable=import-error
import zmake.multiproc


@pytest.fixture(autouse=True)
def reset_log_writer() -> Iterator[None]:
    """Reset LogWriter state before and after each test."""
    zmake.multiproc.LogWriter.reset()
    yield
    zmake.multiproc.LogWriter.reset()


def test_read_output_from_pipe() -> None:
    """Test reading output from a pipe."""
    semaphore = threading.Semaphore(0)
    pipe = os.pipe()
    file_desc = io.TextIOWrapper(os.fdopen(pipe[0], "rb"), encoding="utf-8")
    logger = mock.Mock(spec=logging.Logger)
    logger.log.side_effect = lambda log_lvl, line: semaphore.release()
    zmake.multiproc.LogWriter.log_output(
        logger, logging.DEBUG, file_desc, job_id=""
    )
    os.write(pipe[1], "Hello\n".encode("utf-8"))
    assert semaphore.acquire(timeout=10)  # pylint: disable=consider-using-with
    logger.log.assert_called_with(logging.DEBUG, "Hello")
    os.close(pipe[1])
    file_desc.close()


def test_read_output_change_log_level() -> None:
    """Test changing the log level."""
    semaphore = threading.Semaphore(0)
    pipe = os.pipe()
    file_desc = io.TextIOWrapper(os.fdopen(pipe[0], "rb"), encoding="utf-8")
    logger = mock.Mock(spec=logging.Logger)
    logger.log.side_effect = lambda log_lvl, line: semaphore.release()
    # This call will log output from fd (the file descriptor) to DEBUG, though
    # when the line starts with 'World', the logging level will be switched to
    # CRITICAL (see the content of the log_lvl_override_func).
    zmake.multiproc.LogWriter.log_output(
        logger=logger,
        log_level=logging.DEBUG,
        file_descriptor=file_desc,
        log_level_override_func=lambda line, lvl: (
            logging.CRITICAL if line.startswith("World") else lvl
        ),
        job_id="",
    )
    os.write(pipe[1], "Hello\n".encode("utf-8"))
    assert semaphore.acquire(timeout=10)  # pylint: disable=consider-using-with
    os.write(pipe[1], "World\n".encode("utf-8"))
    assert semaphore.acquire(timeout=10)  # pylint: disable=consider-using-with
    os.write(pipe[1], "Bye\n".encode("utf-8"))
    assert semaphore.acquire(timeout=10)  # pylint: disable=consider-using-with
    logger.log.assert_has_calls(
        [
            mock.call(logging.DEBUG, "Hello"),
            mock.call(logging.CRITICAL, "World"),
            mock.call(logging.CRITICAL, "Bye"),
        ]
    )
    os.close(pipe[1])
    file_desc.close()


def test_read_output_from_second_pipe() -> None:
    """Test that we can read from more than one pipe.

    This is particularly important since we will block on a read/select once we
    have a file descriptor. It is important that we break from the select and
    start it again with the updated list when a new one is added.
    """
    semaphore = threading.Semaphore(0)
    pipes = [os.pipe(), os.pipe()]
    fds = [
        io.TextIOWrapper(os.fdopen(pipes[0][0], "rb"), encoding="utf-8"),
        io.TextIOWrapper(os.fdopen(pipes[1][0], "rb"), encoding="utf-8"),
    ]

    logger = mock.Mock(spec=logging.Logger)
    logger.log.side_effect = lambda log_lvl, fmt, id, line: semaphore.release()

    zmake.multiproc.LogWriter.set_job_name_logging(True)

    zmake.multiproc.LogWriter.log_output(
        logger, logging.DEBUG, fds[0], job_id="0"
    )
    zmake.multiproc.LogWriter.log_output(
        logger, logging.ERROR, fds[1], job_id="1"
    )

    os.write(pipes[1][1], "Hello\n".encode("utf-8"))
    assert semaphore.acquire(timeout=10)  # pylint: disable=consider-using-with
    logger.log.assert_called_with(logging.ERROR, "[%s] %s", "1", "Hello")
    for pipe, file_desc in zip(pipes, fds):
        os.close(pipe[1])
        file_desc.close()


def test_read_output_after_another_pipe_closed() -> None:
    """Test processing output from a pipe after closing another.

    Since we don't want to complicate the API. File descriptors are
    automatically pruned away when closed. Make sure that the other descriptors
    remain functional when that happens.
    """
    semaphore = threading.Semaphore(0)
    pipes = [os.pipe(), os.pipe()]
    fds = [
        io.TextIOWrapper(os.fdopen(pipes[0][0], "rb"), encoding="utf-8"),
        io.TextIOWrapper(os.fdopen(pipes[1][0], "rb"), encoding="utf-8"),
    ]

    logger = mock.Mock(spec=logging.Logger)
    logger.log.side_effect = lambda log_lvl, fmt, id, line: semaphore.release()

    zmake.multiproc.LogWriter.set_job_name_logging(True)

    zmake.multiproc.LogWriter.log_output(
        logger, logging.DEBUG, fds[0], job_id="0"
    )
    zmake.multiproc.LogWriter.log_output(
        logger, logging.ERROR, fds[1], job_id="1"
    )

    fds[0].close()
    os.write(pipes[1][1], "Hello\n".encode("utf-8"))
    assert semaphore.acquire(timeout=10)  # pylint: disable=consider-using-with
    logger.log.assert_called_with(logging.ERROR, "[%s] %s", "1", "Hello")
    os.close(pipes[0][1])
    os.close(pipes[1][1])
    fds[1].close()
