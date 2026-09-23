#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for fix_header_guards.py."""

from pathlib import Path
import sys
import tempfile
import unittest


# pylint: disable=wrong-import-position
sys.path.insert(0, str(Path(__file__).resolve().parent))
import fix_header_guards


# pylint: enable=wrong-import-position


class TestFixHeaderGuards(unittest.TestCase):
    """Test suite for header guard fixes and preprocessor parsing."""

    def setUp(self):
        self.root = Path(
            self.enterContext(
                tempfile.TemporaryDirectory()  # pylint: disable=consider-using-with
            )
        )

    def _create_header(self, rel_path: str, content: str) -> Path:
        p = self.root / rel_path
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(content, encoding="utf-8")
        return p

    def test_internal_endif_not_removed(self):
        """Ensures internal #endif directives (e.g. #ifdef __cplusplus) are preserved."""
        content = """/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license.
 */

#pragma once

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif
int dsp_service_read_requested(void);
#ifdef __cplusplus
}
#endif

namespace cros::dsp::service {
class Driver {};
}  // namespace cros::dsp::service
"""
        header = self._create_header(
            "zephyr/drivers/cros/service/driver.hh", content
        )
        modified = fix_header_guards.fix_header_guard(
            header, self.root, use_c_comments=True
        )
        self.assertTrue(modified)

        result = header.read_text(encoding="utf-8")
        # Ensure #pragma once is removed
        self.assertNotIn("#pragma once", result)
        # Ensure #ifdef __cplusplus blocks and their #endif are intact
        self.assertIn('#ifdef __cplusplus\nextern "C" {\n#endif', result)
        self.assertIn("#ifdef __cplusplus\n}\n#endif", result)
        # Ensure guard was added at the top and bottom
        guard = "ZEPHYR_DRIVERS_CROS_SERVICE_DRIVER_HH_"
        self.assertIn(f"#ifndef {guard}\n#define {guard}\n", result)
        self.assertTrue(result.strip().endswith(f"#endif /* {guard} */"))

        # Idempotency check
        self.assertFalse(
            fix_header_guards.fix_header_guard(
                header, self.root, use_c_comments=True
            )
        )

    def test_missing_header_guard(self):
        """Tests adding include guards to a file with none."""
        content = """/* Copyright 2026 The ChromiumOS Authors */

#include <stdio.h>

void run(void);
"""
        header = self._create_header("include/tool.h", content)
        modified = fix_header_guards.fix_header_guard(
            header, self.root, use_c_comments=True
        )
        self.assertTrue(modified)

        result = header.read_text(encoding="utf-8")
        self.assertIn(
            "#ifndef INCLUDE_TOOL_H_\n#define INCLUDE_TOOL_H_\n", result
        )
        self.assertTrue(result.strip().endswith("#endif /* INCLUDE_TOOL_H_ */"))
        self.assertIn("void run(void);", result)

    def test_internal_if_blocks_when_guard_missing(self):
        """Tests that internal #if / #endif blocks are not deleted when guard is missing."""
        content = """/* Copyright 2026 The ChromiumOS Authors */

#if defined(CONFIG_FOO)
int a;
#endif

#if defined(CONFIG_BAR)
int b;
#endif
"""
        header = self._create_header("include/config.h", content)
        modified = fix_header_guards.fix_header_guard(
            header, self.root, use_c_comments=True
        )
        self.assertTrue(modified)

        result = header.read_text(encoding="utf-8")
        self.assertIn("#if defined(CONFIG_FOO)\nint a;\n#endif", result)
        self.assertIn("#if defined(CONFIG_BAR)\nint b;\n#endif", result)
        self.assertIn("#ifndef INCLUDE_CONFIG_H_", result)
        self.assertTrue(
            result.strip().endswith("#endif /* INCLUDE_CONFIG_H_ */")
        )

    def test_existing_valid_guard_updated(self):
        """Tests updating an existing header guard macro name in place."""
        content = """/* Copyright 2026 */
#ifndef OLD_GUARD_H_
#define OLD_GUARD_H_

void test(void);

#endif /* OLD_GUARD_H_ */
"""
        header = self._create_header("include/my_header.h", content)
        modified = fix_header_guards.fix_header_guard(
            header, self.root, use_c_comments=True
        )
        self.assertTrue(modified)

        result = header.read_text(encoding="utf-8")
        self.assertNotIn("OLD_GUARD_H_", result)
        self.assertIn(
            "#ifndef INCLUDE_MY_HEADER_H_\n#define INCLUDE_MY_HEADER_H_",
            result,
        )
        self.assertTrue(
            result.strip().endswith("#endif /* INCLUDE_MY_HEADER_H_ */")
        )

    def test_misplaced_guard_code_before_ifndef(self):
        """Tests moving #ifndef/#define above code that precedes it."""
        content = """/* Copyright 2026 */

#include <stdint.h>

#ifndef INCLUDE_TYPES_H_
#define INCLUDE_TYPES_H_

typedef uint32_t my_type;

#endif /* INCLUDE_TYPES_H_ */
"""
        header = self._create_header("include/types.h", content)
        modified = fix_header_guards.fix_header_guard(
            header, self.root, use_c_comments=True
        )
        self.assertTrue(modified)

        result = header.read_text(encoding="utf-8")
        lines = result.splitlines()
        # #ifndef must come before #include <stdint.h>
        ifndef_idx = next(
            i for i, l in enumerate(lines) if l.startswith("#ifndef")
        )
        include_idx = next(
            i for i, l in enumerate(lines) if l.startswith("#include")
        )
        self.assertLess(ifndef_idx, include_idx)

    def test_misplaced_guard_code_after_endif(self):
        """Tests moving #endif after code that follows it."""
        content = """/* Copyright 2026 */
#ifndef INCLUDE_API_H_
#define INCLUDE_API_H_

void first(void);

#endif /* INCLUDE_API_H_ */

void second(void);
"""
        header = self._create_header("include/api.h", content)
        modified = fix_header_guards.fix_header_guard(
            header, self.root, use_c_comments=True
        )
        self.assertTrue(modified)

        result = header.read_text(encoding="utf-8")
        self.assertTrue(result.strip().endswith("#endif /* INCLUDE_API_H_ */"))
        self.assertIn("void second(void);", result)

    def test_prefix_option(self):
        """Tests the --prefix option."""
        content = """#ifndef FOO_H_
#define FOO_H_
#endif /* FOO_H_ */
"""
        header = self._create_header("util/foo.h", content)
        fix_header_guards.fix_header_guard(
            header, self.root, prefix="PLATFORM_EC", use_c_comments=True
        )
        result = header.read_text(encoding="utf-8")
        self.assertIn("#ifndef PLATFORM_EC_UTIL_FOO_H_", result)
        self.assertIn("#define PLATFORM_EC_UTIL_FOO_H_", result)

    def test_long_comment_wrapping(self):
        """Tests that comments > 78 chars are wrapped matching clang-format."""
        content = """#ifndef SHORT_H_
#define SHORT_H_
#endif /* SHORT_H_ */
"""
        long_path = "a" * 70 + ".h"
        header = self._create_header(long_path, content)
        fix_header_guards.fix_header_guard(
            header, self.root, use_c_comments=True
        )
        result = header.read_text(encoding="utf-8")
        # Line continuation backslash followed by tab indent
        self.assertIn("\\\n\t*/", result)

    def test_cpp_comments(self):
        """Tests using // comments instead of C comments."""
        content = """#ifndef UTIL_BAR_H_
#define UTIL_BAR_H_
#endif // UTIL_BAR_H_
"""
        header = self._create_header("util/bar.h", content)
        fix_header_guards.fix_header_guard(
            header, self.root, use_c_comments=False
        )
        result = header.read_text(encoding="utf-8")
        self.assertIn("#endif  // UTIL_BAR_H_", result)

    def test_precondition_check_preserved(self):
        """Tests that #error inclusion / precondition checks before #ifndef are preserved."""
        content = """/* Copyright 2026 */
#if !defined(PARENT_H_) || defined(INCLUDE_CHILD_H_)
#error "Include parent.h directly"
#endif

#ifndef OLD_GUARD_H_
#define OLD_GUARD_H_

void child_func(void);

#endif /* OLD_GUARD_H_ */
"""
        header = self._create_header("include/child.h", content)
        modified = fix_header_guards.fix_header_guard(
            header, self.root, use_c_comments=True
        )
        self.assertTrue(modified)

        result = header.read_text(encoding="utf-8")
        lines = result.splitlines()
        # #error block must remain BEFORE #ifndef
        error_idx = next(i for i, l in enumerate(lines) if "#error" in l)
        ifndef_idx = next(
            i for i, l in enumerate(lines) if l.startswith("#ifndef")
        )
        self.assertLess(error_idx, ifndef_idx)
        self.assertIn("#ifndef INCLUDE_CHILD_H_", result)
        self.assertIn("#define INCLUDE_CHILD_H_", result)
        self.assertTrue(
            result.strip().endswith("#endif /* INCLUDE_CHILD_H_ */")
        )

        # Idempotency check: should not modify further
        self.assertFalse(
            fix_header_guards.fix_header_guard(
                header, self.root, use_c_comments=True
            )
        )

    def test_dual_guard_compatibility_shim(self):
        """Tests recognizing #if !defined(NEW) && !defined(OLD) without duplicating."""
        content = """/* Copyright 2026 */
#if !defined(INCLUDE_COMPAT_H_) && \\
\t!defined(__CROS_EC_COMPAT_H)
#define INCLUDE_COMPAT_H_
#define __CROS_EC_COMPAT_H

void compat_func(void);

#endif /* INCLUDE_COMPAT_H_ */
"""
        header = self._create_header("include/compat.h", content)
        # Should be recognized as already having valid guard INCLUDE_COMPAT_H_
        modified = fix_header_guards.fix_header_guard(
            header, self.root, use_c_comments=True
        )
        self.assertFalse(modified)

    def test_symlinks_skipped(self):
        """Tests that symlinks are skipped in collect_header_files."""
        real_target = self._create_header("real/target.h", "#pragma once\n")
        symlink_path = self.root / "link.h"
        symlink_path.symlink_to(real_target)

        collected = fix_header_guards.collect_header_files([self.root])
        self.assertIn(real_target, collected)
        self.assertNotIn(symlink_path, collected)

    def test_deferred_todo_switch_to_skipped(self):
        """Tests that headers with TODO(b/510249930): Switch to are skipped."""
        content = """/* Copyright 2026 */
/** TODO(b/510249930): Switch to PLATFORM_EC_DEFERRED_H_ */
#ifndef OLD_NAME_H
#define OLD_NAME_H
#endif /* OLD_NAME_H */
"""
        header = self._create_header("include/deferred.h", content)
        modified = fix_header_guards.fix_header_guard(
            header, self.root, use_c_comments=True
        )
        self.assertFalse(modified)
        self.assertIn("OLD_NAME_H", header.read_text(encoding="utf-8"))

    def test_pragma_once_with_existing_guard(self):
        """Tests removing #pragma once when #ifndef exists without line shifting."""
        content = """/* Copyright 2026 */
#pragma once

#ifndef OLD_GUARD_H_
#define OLD_GUARD_H_

void preserve_me(void);

#endif /* OLD_GUARD_H_ */
"""
        header = self._create_header("include/pragma_guard.h", content)
        modified = fix_header_guards.fix_header_guard(
            header, self.root, use_c_comments=True
        )
        self.assertTrue(modified)

        result = header.read_text(encoding="utf-8")
        self.assertNotIn("#pragma once", result)
        self.assertIn("#ifndef INCLUDE_PRAGMA_GUARD_H_", result)
        self.assertIn("#define INCLUDE_PRAGMA_GUARD_H_", result)
        self.assertIn("void preserve_me(void);", result)
        self.assertTrue(
            result.strip().endswith("#endif /* INCLUDE_PRAGMA_GUARD_H_ */")
        )

        # Idempotency check
        self.assertFalse(
            fix_header_guards.fix_header_guard(
                header, self.root, use_c_comments=True
            )
        )

    def test_pragma_once_inside_existing_guard(self):
        """Tests that #pragma once inside existing guards is removed."""
        content = """/* Copyright 2026 */
#ifndef OLD_GUARD_H_
#define OLD_GUARD_H_

#pragma once

void func(void);

#endif /* OLD_GUARD_H_ */
"""
        header = self._create_header("include/pragma_inside.h", content)
        modified = fix_header_guards.fix_header_guard(
            header, self.root, use_c_comments=True
        )
        self.assertTrue(modified)

        result = header.read_text(encoding="utf-8")
        self.assertNotIn("#pragma once", result)
        self.assertIn("#ifndef INCLUDE_PRAGMA_INSIDE_H_", result)
        self.assertIn("void func(void);", result)

    def test_mixed_comment_and_code_on_first_line(self):
        """Tests inserting guards when a line starts with a block comment but contains code."""
        content = """/* Copyright 2026 */ #include <stdio.h>

void func(void);
"""
        header = self._create_header("include/mixed_line.h", content)
        modified = fix_header_guards.fix_header_guard(
            header, self.root, use_c_comments=True
        )
        self.assertTrue(modified)

        result = header.read_text(encoding="utf-8")
        lines = result.splitlines()
        ifndef_idx = next(
            i for i, l in enumerate(lines) if l.startswith("#ifndef")
        )
        mixed_idx = next(
            i for i, l in enumerate(lines) if "/* Copyright 2026 */" in l
        )
        # The guard must be inserted before or enclosing the code on that line
        self.assertLess(ifndef_idx, mixed_idx)
        self.assertIn("void func(void);", result)
        self.assertTrue(
            result.strip().endswith("#endif /* INCLUDE_MIXED_LINE_H_ */")
        )


if __name__ == "__main__":
    unittest.main()
