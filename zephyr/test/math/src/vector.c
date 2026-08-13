/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "math.h"
#include "math_util.h"

#include <zephyr/ztest.h>

ZTEST_USER(math, test_cosine_of_angle_diff__zero_magnitude_vector)
{
	intv3_t v0 = { 0, 0, 0 };
	intv3_t v1 = { 1, 1, 1 };

	zassert_equal(cosine_of_angle_diff(v0, v1), 0);
	zassert_equal(cosine_of_angle_diff(v1, v0), 0);
}

ZTEST_USER(math, test_rotate_inv__null_matrix)
{
	intv3_t v = { 1, 2, 3 };
	intv3_t r = { 4, 5, 6 };

	rotate_inv(v, NULL, r);
	zassert_equal(v[0], r[0]);
	zassert_equal(v[1], r[1]);
	zassert_equal(v[2], r[2]);
}

ZTEST_USER(math, test_vector_operations)
{
	intv3_t v1 = { 1, 0, 0 };
	intv3_t v2 = { 0, 1, 0 };
	intv3_t cross;

	/* Cross product: i x j = k */
	cross_product(v1, v2, cross);
	zassert_equal(cross[X], 0);
	zassert_equal(cross[Y], 0);
	zassert_equal(cross[Z], 1);

	/* Dot product: i . j = 0, i . i = 1 */
	zassert_equal(dot_product(v1, v2), 0);
	zassert_equal(dot_product(v1, v1), 1);

	/* Vector magnitude: 3^2 + 4^2 + 0 = 5^2 */
	intv3_t v3 = { 3, 4, 0 };
	zassert_equal(vector_magnitude(v3), 5);

	/* Vector scale */
	vector_scale(v3, FLOAT_TO_FP(2.0));
	zassert_equal(v3[X], 6);
	zassert_equal(v3[Y], 8);
	zassert_equal(v3[Z], 0);

	/* Cosine of angle diff */
	zassert_within(cosine_of_angle_diff(v1, v2), FLOAT_TO_FP(0.0),
		       FLOAT_TO_FP(0.01));
	zassert_within(cosine_of_angle_diff(v1, v1), FLOAT_TO_FP(1.0),
		       FLOAT_TO_FP(0.01));
}

ZTEST_USER(math, test_rotate_and_rotate_inv)
{
	/*
	 * +90 degree rotation matrix around Z axis (results in -90 deg
	 * rotation due to row-vector multiplication in rotate()).
	 */
	mat33_fp_t R = {
		{ FLOAT_TO_FP(0.0), FLOAT_TO_FP(-1.0), FLOAT_TO_FP(0.0) },
		{ FLOAT_TO_FP(1.0), FLOAT_TO_FP(0.0), FLOAT_TO_FP(0.0) },
		{ FLOAT_TO_FP(0.0), FLOAT_TO_FP(0.0), FLOAT_TO_FP(1.0) }
	};
	intv3_t v = { 10, 0, 5 };
	intv3_t rotated;
	intv3_t restored;

	/* rotate: R == NULL no-op */
	rotate(v, NULL, rotated);
	zassert_equal(rotated[0], 10);
	zassert_equal(rotated[1], 0);
	zassert_equal(rotated[2], 5);

	/* rotate: matrix transform */
	rotate(v, R, rotated);
	zassert_equal(rotated[0], 0);
	zassert_equal(rotated[1], -10);
	zassert_equal(rotated[2], 5);

	/* rotate_inv: inverse transform */
	rotate_inv(rotated, R, restored);
	zassert_equal(restored[0], 10);
	zassert_equal(restored[1], 0);
	zassert_equal(restored[2], 5);
}

ZTEST_USER(math, test_round_divide)
{
	zassert_equal(round_divide(5, 2), 3);
	zassert_equal(round_divide(4, 2), 2);
	zassert_equal(round_divide(-5, 2), -3);
	zassert_equal(round_divide(5, -2), -3);
	zassert_equal(round_divide(-5, -2), 3);
}
