# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""The entry point into zmake."""

import argparse
import contextlib
import inspect
import json
import logging
import os
import pathlib
import subprocess
import sys
import tempfile

from zmake import jobserver
from zmake import modules
from zmake import multiproc
from zmake.repository_prep import _has_files
from zmake.repository_prep import _has_patch_files
from zmake.repository_prep import CodebasePreparer
from zmake.repository_prep import copy_outputs_back
from zmake.repository_prep import create_shadow_checkout
from zmake.repository_prep import find_target_repos
from zmake.repository_prep import get_default_patches_dirs
from zmake.repository_prep import get_default_src_override_dirs
from zmake.repository_prep import get_dirty_files
import zmake.zmake as zm


def maybe_reexec(argv):
    """Re-exec zmake from the EC source tree, if possible and desired.

    Zmake installs into the users' chroot, which makes it convenient
    to execute, but can sometimes become tedious when zmake changes
    land and users haven't upgraded their chroots yet.

    We can partially subvert this problem by re-execing zmake from the
    source if it's available.  This won't make it so developers never
    need to upgrade their chroots (e.g., a toolchain upgrade could
    require chroot upgrades), but at least makes it slightly more
    convenient for an average repo sync.

    Args:
        argv: The argument list passed to the main function, not
            including the executable path.

    Returns:
        None, if the re-exec did not happen, or never returns if the
        re-exec did happen.
    """
    # We only re-exec if we are inside of a chroot (since if installed
    # standalone using pip, there's already an "editable install"
    # feature for that in pip.)
    env = dict(os.environ)
    srcroot = env.get("CROS_WORKON_SRCROOT")
    if not srcroot:
        return

    # If for some reason we decide to move zmake in the future, then
    # we don't want to use the re-exec logic.
    zmake_path = (
        pathlib.Path(srcroot) / "src" / "platform" / "ec" / "zephyr" / "zmake"
    ).resolve()
    if not zmake_path.is_dir():
        return

    # If PYTHONPATH is set, it is either because we just did a
    # re-exec, or because the user wants to run a specific copy of
    # zmake.  In either case, we don't want to re-exec.
    if "PYTHONPATH" in env:
        return

    # Set PYTHONPATH so that we run zmake from source.
    env["PYTHONPATH"] = str(zmake_path)

    os.execve(sys.executable, [sys.executable, "-m", "zmake", *argv], env)


def call_with_namespace(func, namespace, **kwds):
    """Call a function with arguments applied from a Namespace.

    Args:
        func: The callable to call.
        namespace: The namespace to apply to the callable.

    Returns:
        The result of calling the callable.
    """
    sig = inspect.signature(func)
    names = [p.name for p in sig.parameters.values()]
    for name, value in vars(namespace).items():
        pyname = name.replace("-", "_")
        if pyname in names:
            kwds[pyname] = value
    return func(**kwds)


# Dictionary used to map log level strings to their corresponding int values.
log_level_map = {
    "DEBUG": logging.DEBUG,
    "INFO": logging.INFO,
    "WARNING": logging.WARNING,
    "ERROR": logging.ERROR,
    "CRITICAL": logging.CRITICAL,
}


def get_argparser():
    """Get the argument parser.

    Returns:
        A two tuple, the argument parser, and the subcommand action.
    """
    parser = argparse.ArgumentParser(
        prog="zmake",
        description="Chromium OS's meta-build tool for Zephyr",
    )
    parser.add_argument(
        "--checkout", type=pathlib.Path, help="Path to ChromiumOS checkout"
    )
    parser.add_argument(
        "-j",
        "--jobs",
        type=int,
        help="Degree of multiprogramming to use",
    )

    log_level_group = parser.add_mutually_exclusive_group()
    log_level_group.add_argument(
        "-l",
        "--log-level",
        choices=log_level_map.values(),
        metavar=f"{{{','.join(log_level_map)}}}",
        type=lambda x: log_level_map[x],
        default=logging.INFO,
        help="Set the logging level (default=INFO)",
    )
    log_level_group.add_argument(
        "-D",
        "--debug",
        dest="log_level",
        action="store_const",
        const=logging.DEBUG,
        help="Alias for --log-level=DEBUG",
    )

    parser.add_argument(
        "-L",
        "--no-log-label",
        action="store_false",
        help="Turn off logging labels",
        dest="log_label",
        default=None,
    )
    parser.add_argument(
        "--log-label",
        action="store_true",
        help="Turn on logging labels",
        dest="log_label",
        default=None,
    )
    parser.add_argument(
        "--modules-dir",
        type=pathlib.Path,
        help="The path to a directory containing all modules "
        "needed.  If unspecified, zmake will assume you have "
        "a Chrome OS checkout and try locating them in the "
        "checkout.",
    )
    parser.add_argument(
        "--projects-dir",
        type=pathlib.Path,
        action="append",
        dest="projects_dirs",
        metavar="PROJECTS_DIR",
        help="Base directory to search for BUILD.py files. Can be repeated.",
    )
    parser.add_argument(
        "--zephyr-base", type=pathlib.Path, help="Path to Zephyr OS repository"
    )
    parser.add_argument(
        "--patches-dir",
        action="append",
        dest="patches_dirs",
        type=pathlib.Path,
        help="Path to a patches directory. Can be repeated.",
    )
    parser.add_argument(
        "--src-override-dir",
        action="append",
        dest="src_override_dirs",
        type=pathlib.Path,
        help="Path to a source override directory. Can be repeated.",
    )
    parser.add_argument(
        "--no-shadow",
        action="store_true",
        help="Do not use a shadow workspace; apply changes in-place.",
    )

    sub = parser.add_subparsers(
        dest="subcommand",
        metavar="subcommand",
        help="Subcommand to run",
    )
    sub.required = True

    configure = sub.add_parser(
        "configure",
        help="Set up a build directory to be built later by the build subcommand",
    )
    add_common_configure_args(configure)
    add_common_build_args(configure)

    build = sub.add_parser(
        "build",
        help="Configure and build projects",
    )
    add_common_configure_args(build)
    add_common_build_args(build)

    compare_builds = sub.add_parser(
        "compare-builds", help="Compare output binaries from two commits"
    )
    compare_builds.add_argument(
        "-m",
        "--module",
        default="ec",
        help=(
            "Name of the module repository to compare (e.g. 'ec', 'google-private'), "
            "default='ec'"
        ),
    )
    compare_builds.add_argument(
        "--ref1",
        default="HEAD",
        help="1st git reference (commit, branch, etc), default=HEAD",
    )
    compare_builds.add_argument(
        "--ref2",
        default="HEAD~",
        help=(
            "2nd git reference (commit, branch, etc), default=HEAD~. "
            "If ref2==ref1 then only one checkout is performed and the "
            "firmware binary comparison is skipped."
        ),
    )
    compare_builds.add_argument(
        "-k",
        "--keep-temps",
        action="store_true",
        help="Keep temporary build directories on exit",
    )
    compare_builds.add_argument(
        "-n",
        "--compare-configs",
        action="store_true",
        help="Compare configs of build outputs",
    )
    compare_builds.add_argument(
        "-b",
        "--compare-binaries-disable",
        action="store_true",
        help="Don't compare binaries of build outputs",
    )
    compare_builds.add_argument(
        "-d",
        "--compare-devicetrees",
        action="store_true",
        help="Compare devicetrees of build outputs",
    )
    compare_builds.add_argument(
        "-D",
        "--cmake-define",
        action="append",
        dest="cmake_defs",
    )
    add_common_build_args(compare_builds)

    list_projects = sub.add_parser(
        "list-projects",
        help="List projects known to zmake.",
    )
    list_projects.add_argument(
        "--format",
        default="{config.project_name}\n",
        dest="fmt",
        help=(
            "Output format to print projects (str.format(config=project.config) is "
            "called on this for each project)."
        ),
    )
    list_projects.add_argument(
        "project_names",
        nargs="*",
        metavar="project_name",
        help="Name(s) of the project(s) to list, Unix-style wildcard "
        "expressions to select multiple projects (e.g. brox-*), or "
        "program directory names prefixed with '%%' (e.g. %%nissa)",
        default=[],
    )

    generate_readme = sub.add_parser(
        "generate-readme",
        help="Update the auto-generated markdown documentation",
    )
    generate_readme.add_argument(
        "-o",
        "--output-file",
        default=pathlib.Path(__file__).parent.parent / "README.md",
        help="File to write to.  It will only be written if changed.",
    )
    generate_readme.add_argument(
        "--diff",
        action="store_true",
        help=(
            "If specified, diff the README with the expected contents instead of "
            "writing out."
        ),
    )

    analyze_build_diff = sub.add_parser(
        "analyze-build-diff",
        help="Analyze binary differences between two EC builds",
    )
    analyze_build_diff.add_argument(
        "target1",
        type=pathlib.Path,
        help="First build directory or ec.bin file",
    )
    analyze_build_diff.add_argument(
        "target2",
        type=pathlib.Path,
        help="Second build directory or ec.bin file",
    )
    analyze_build_diff.add_argument(
        "--sections",
        action="store_true",
        help="Print detailed section size comparison and changed sections",
    )

    return parser, sub


def add_common_build_args(sub_parser: argparse.ArgumentParser):
    """Adds common arguments used by build-like subcommands"""
    sub_parser.add_argument(
        "-t", "--toolchain", help="Name of toolchain to use"
    )
    sub_parser.add_argument(
        "--extra-cflags",
        help="Additional CFLAGS to use for target builds",
    )
    group = sub_parser.add_mutually_exclusive_group(required=True)
    group.add_argument(
        "-a",
        "--all",
        action="store_true",
        dest="all_projects",
        help="Select all projects",
    )
    group.add_argument(
        "project_names",
        nargs="*",
        metavar="project_name",
        help="Name(s) of the project(s) to build, Unix-style wildcard "
        "expressions to select multiple projects (e.g. brox-*), or "
        "program directory names prefixed with '%%' (e.g. %%nissa)",
        default=[],
    )


def add_common_configure_args(sub_parser: argparse.ArgumentParser):
    """Adds common arguments used by configure-like subcommands."""
    sub_parser.add_argument(
        "--bringup",
        action="store_true",
        dest="bringup",
        help="Enable bringup debugging features",
    )
    sub_parser.add_argument(
        "--clobber",
        action="store_true",
        dest="clobber",
        help="Delete existing build directories, even if configuration is unchanged",
    )
    sub_parser.add_argument(
        "-v",
        "--version",
        help="Base version string to use in build",
    )
    sub_parser.add_argument(
        "--static",
        action="store_true",
        dest="static_version",
        help="Generate static version information for reproducible builds and official builds",
    )
    sub_parser.add_argument(
        "--save-temps",
        action="store_true",
        dest="save_temps",
        help="Save the temporary files containing preprocessor output",
    )
    sub_parser.add_argument(
        "--allow-warnings",
        action="store_true",
        default=False,
        help="Do not treat warnings as errors",
    )
    sub_parser.add_argument(
        "--cmake-trace",
        action="store_true",
        default=False,
        dest="cmake_trace",
        help="Run cmake in trace mode and write the output to cmake-build.trace",
    )
    sub_parser.add_argument(
        "--cmake-graph",
        action="store_true",
        default=False,
        dest="cmake_graph",
        help="Create a cmake-graph.dot file to trace dependencies",
    )
    sub_parser.add_argument(
        "-B",
        "--build-dir",
        type=pathlib.Path,
        help="Root build directory, project files will be in "
        "${build_dir}/${project_name}",
    )
    sub_parser.add_argument(
        "-c",
        "--coverage",
        action="store_true",
        dest="coverage",
        help="Enable CONFIG_COVERAGE Kconfig.",
    )
    sub_parser.add_argument(
        "--delete-intermediates",
        action="store_true",
        dest="delete_intermediates",
        help="Delete intermediate files to save disk space",
    )
    sub_parser.add_argument(
        "-D",
        "--cmake-define",
        action="append",
        dest="cmake_defs",
    )


def find_toolchains():
    """Attempts to load toolchain paths.

    Args: None

    Returns:
        None: Modifies the os environment if found
    """
    env = dict(os.environ)
    if "COREBOOT_SDK_ROOT" not in env:
        # Try to find util relative to this file first (handles running from source/editable)
        ec_util_path = (
            pathlib.Path(__file__).resolve().parent.parent.parent.parent
            / "util"
        ).resolve()
        if not ec_util_path.is_dir():
            # Fallback to sys.path
            for sys_path in sys.path:
                ec_util_path = (
                    pathlib.Path(sys_path) / ".." / ".." / "util"
                ).resolve()
                if ec_util_path.is_dir():
                    break
            else:
                ec_util_path = None

        if ec_util_path and ec_util_path.is_dir():
            run_result = subprocess.run(
                ["./coreboot_sdk.py", "-j"],
                check=True,
                stdout=subprocess.PIPE,
                cwd=str(ec_util_path),
            )
            env_vars = json.loads(run_result.stdout.decode("utf-8"))
            if env_vars:
                os.environ.update(env_vars.items())


def main(argv=None):
    """The main function.

    Args:
        argv: Optionally, the command-line to parse, not including argv[0].

    Returns:
        Zero upon success, or non-zero upon failure.
    """
    if argv is None:
        argv = sys.argv[1:]

    maybe_reexec(argv)

    parser, _ = get_argparser()
    opts = parser.parse_args(argv)

    find_toolchains()

    # Default logging
    log_label = False

    if opts.log_label is not None:
        log_label = opts.log_label
    if log_label:
        log_format = "%(levelname)s: %(message)s"
        multiproc.LogWriter.set_job_name_logging(True)
    else:
        log_format = "%(message)s"

    logging.basicConfig(format=log_format, level=opts.log_level)
    # Create the jobserver client BEFORE any pipes get opened in LogWriter
    jobserver_client = jobserver.GNUMakeJobClient.from_environ(jobs=opts.jobs)
    multiproc.LogWriter.reset()

    zmake = None
    workspace_prepared = os.environ.get("ZMAKE_WORKSPACE_PREPARED") == "1" or (
        opts.modules_dir
        and (
            opts.modules_dir.resolve().parent.parent / ".workspace_prepared"
        ).exists()
    )
    prep_subcommands = {"configure", "build", "compare-builds"}
    need_prep = opts.subcommand in prep_subcommands and not workspace_prepared

    def _set_workspace_prepared(exit_stack):
        prev_val = os.environ.get("ZMAKE_WORKSPACE_PREPARED")
        os.environ["ZMAKE_WORKSPACE_PREPARED"] = "1"
        if prev_val is None:
            exit_stack.callback(
                os.environ.pop, "ZMAKE_WORKSPACE_PREPARED", None
            )
        else:
            exit_stack.callback(
                os.environ.__setitem__, "ZMAKE_WORKSPACE_PREPARED", prev_val
            )

    def _remap_to_shadow(path, real_root, shadow_root):
        resolved = pathlib.Path(path).resolve()
        try:
            return shadow_root / resolved.relative_to(real_root)
        except ValueError:
            return path

    with contextlib.ExitStack() as stack:
        use_shadow = False
        shadow_ready = False
        real_checkout = None
        checkout = opts.checkout

        if need_prep:
            if not checkout and (not opts.zephyr_base or not opts.modules_dir):
                try:
                    checkout = zm.util.locate_cros_checkout().resolve()
                except FileNotFoundError:
                    checkout = None
            elif checkout:
                checkout = pathlib.Path(checkout).resolve()

            if checkout:
                patches_dirs = (
                    opts.patches_dirs
                    if opts.patches_dirs
                    else get_default_patches_dirs(checkout)
                )
                src_override_dirs = (
                    opts.src_override_dirs
                    if opts.src_override_dirs
                    else get_default_src_override_dirs(checkout)
                )

                has_overrides = any(_has_files(d) for d in src_override_dirs)
                has_patches = any(_has_patch_files(d) for d in patches_dirs)
                use_shadow = (
                    (has_overrides or has_patches)
                    and not opts.no_shadow
                    and opts.subcommand != "compare-builds"
                )

                if use_shadow:
                    logging.info(
                        "Using shadow workspace for zmake execution..."
                    )
                    target_repos = find_target_repos(
                        checkout, src_override_dirs, patches_dirs
                    )
                    user_dirty_files = {
                        f
                        for repo in target_repos
                        for f in get_dirty_files(repo, checkout)
                    }
                    shadow_dir_ctx = stack.enter_context(
                        tempfile.TemporaryDirectory()
                    )
                    shadow_checkout = pathlib.Path(shadow_dir_ctx) / "shadow"
                    shadow_checkout.mkdir()

                    located_modules = modules.locate_from_checkout(checkout)
                    if opts.zephyr_base:
                        zephyr_base = opts.zephyr_base
                    else:
                        zephyr_base = (
                            checkout
                            / "src"
                            / "third_party"
                            / "zephyrproject"
                            / "zephyr"
                        )

                    create_shadow_checkout(
                        checkout,
                        shadow_checkout,
                        located_modules,
                        zephyr_base,
                        src_override_dirs,
                        patches_dirs,
                    )

                    real_checkout = checkout
                    checkout = shadow_checkout
                    opts.checkout = shadow_checkout
                    if opts.zephyr_base:
                        opts.zephyr_base = _remap_to_shadow(
                            opts.zephyr_base, real_checkout, shadow_checkout
                        )
                    if opts.projects_dirs:
                        opts.projects_dirs = [
                            _remap_to_shadow(p, real_checkout, shadow_checkout)
                            for p in opts.projects_dirs
                        ]
                    if opts.modules_dir:
                        opts.modules_dir = _remap_to_shadow(
                            opts.modules_dir, real_checkout, shadow_checkout
                        )

                    stack.enter_context(
                        CodebasePreparer(
                            shadow_checkout,
                            patches_dirs,
                            src_override_dirs,
                            user_dirty_files=user_dirty_files,
                            commit=True,
                        )
                    )
                    shadow_ready = True
                elif (
                    opts.subcommand == "compare-builds"
                    and (has_overrides or has_patches)
                    and not opts.no_shadow
                ):
                    target_repos = find_target_repos(
                        checkout, src_override_dirs, patches_dirs
                    )
                    opts.user_dirty_files = {
                        f
                        for repo in target_repos
                        for f in get_dirty_files(repo, checkout)
                    }
                    opts.worktree_patches_dirs = patches_dirs
                    opts.worktree_src_override_dirs = src_override_dirs
                else:
                    stack.enter_context(
                        CodebasePreparer(
                            checkout,
                            patches_dirs,
                            src_override_dirs,
                            commit=False,
                        )
                    )
                _set_workspace_prepared(stack)
            elif opts.zephyr_base and opts.modules_dir:
                located_modules = modules.locate_from_directory(
                    opts.modules_dir
                )
                path_mappings = {
                    pathlib.Path("src/third_party/zephyrproject/zephyr"): (
                        opts.zephyr_base.resolve()
                    ),
                    pathlib.Path("src/third_party/zephyrproject"): (
                        opts.zephyr_base.resolve().parent
                    ),
                }
                for mod_name, mod_path in located_modules.items():
                    if mod_name in modules.known_modules:
                        rel_mod = modules.known_modules[mod_name](
                            mod_name, pathlib.Path("")
                        )
                        path_mappings[rel_mod] = mod_path.resolve()

                ec_mod = located_modules.get("ec")
                sysroot = pathlib.Path(os.environ.get("SYSROOT", "/"))
                default_priv = (
                    opts.modules_dir.resolve().parent.parent / "ec-private"
                )
                if not default_priv.exists():
                    default_priv = (
                        sysroot
                        / "firmware"
                        / "private-directories"
                        / "zephyr-ec-private"
                    )
                if not default_priv.exists():
                    default_priv = (
                        sysroot
                        / "firmware"
                        / "private-directories"
                        / "ec-private"
                    )
                patches_dirs = opts.patches_dirs or [
                    *(
                        [ec_mod / "upstream_overrides" / "patches"]
                        if ec_mod
                        else []
                    ),
                    default_priv / "patches",
                ]
                src_override_dirs = opts.src_override_dirs or [
                    *(
                        [ec_mod / "upstream_overrides" / "src-override"]
                        if ec_mod
                        else []
                    ),
                    default_priv / "src-override",
                ]
                stack.enter_context(
                    CodebasePreparer(
                        opts.modules_dir.resolve().parent,
                        patches_dirs,
                        src_override_dirs,
                        commit=False,
                        path_mappings=path_mappings,
                        allow_dirty=True,
                    )
                )
                _set_workspace_prepared(stack)

        try:
            zmake = call_with_namespace(
                zm.Zmake, opts, jobserver=jobserver_client
            )
            subcommand_method = getattr(
                zmake, opts.subcommand.replace("-", "_")
            )
            result = call_with_namespace(subcommand_method, opts)
            wait_rv = zmake.executor.wait()
            ret = result or wait_rv
        finally:
            if shadow_ready and real_checkout:
                try:
                    located_modules = modules.locate_from_checkout(
                        real_checkout
                    )
                    ec_rel_path = (
                        located_modules["ec"]
                        .resolve()
                        .relative_to(real_checkout)
                    )
                    logging.info("Copying outputs back to real checkout...")
                    copy_outputs_back(checkout, real_checkout, ec_rel_path)
                except OSError as e:
                    logging.error("Failed to copy outputs back: %s", e)

        multiproc.LogWriter.wait_for_log_end()
        if zmake:
            for file, failed_projects in zmake.cmp_failed_projects.items():
                if failed_projects:
                    logging.error(
                        "Failed projects by diff in %s: %s",
                        file,
                        ", ".join(sorted(failed_projects)),
                    )
            if zmake.failed_projects:
                logging.error(
                    "All failed projects: %s",
                    " ".join(sorted(zmake.failed_projects)),
                )
        return ret


if __name__ == "__main__":
    sys.exit(main())
