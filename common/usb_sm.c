/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "task.h"
#include "usb_pd.h"
#include "usb_sm.h"
#include "util.h"

void init_state(int port, struct sm_obj *obj, sm_state target)
{
#if (CONFIG_SM_NESTING_NUM > 1)
	int i;
	sm_state tmp_super[CONFIG_SM_NESTING_NUM];
#endif

	obj->last_state = NULL;
	obj->task_state = target;

#if (CONFIG_SM_NESTING_NUM > 1)
	/* Prepare to execute all entry actions of the target's super states */

	/*
	 * Get targets super state. This will be NULL if the target
	 * has not super state
	 */
	tmp_super[CONFIG_SM_NESTING_NUM - 1] =
					(sm_state)target(port, SUPER_SIG);

	/* Initialize to NULL */
	for (i = CONFIG_SM_NESTING_NUM - 2; i >= 0; i--)
		tmp_super[i] = NULL;

	/* Get all super states of the target */
	for (i = CONFIG_SM_NESTING_NUM - 1; i >= 0; i--)
		if (tmp_super[i] != NULL) {
			tmp_super[i - 1] =
					(sm_state)tmp_super[i](port, SUPER_SIG);
	}

	/* Execute all super state entry actions in forward order */
	for (i = 0; i < CONFIG_SM_NESTING_NUM; i++)
		if (tmp_super[i] != NULL)
			tmp_super[i](port, ENTRY_SIG);
#endif
	/* Now execute the target entry action */
	target(port, ENTRY_SIG);
}

void set_state(int port, struct sm_obj *obj, sm_state target)
{
#if (CONFIG_SM_NESTING_NUM > 1)
	int i;
	sm_state tmp_super[CONFIG_SM_NESTING_NUM];
	sm_state target_super;
	sm_state last_super;

	/* Execute all exit actions is reverse order */

	/* Get target's super state */
	target_super = (sm_state)target(port, SUPER_SIG);
	tmp_super[0] = obj->task_state;
	do {
		/* Execute exit action */
		tmp_super[0](port, EXIT_SIG);

		/* Get super state */
		tmp_super[0] = (sm_state)tmp_super[0](port, SUPER_SIG);

		/*
		 * Break if the target's super state is the same as the
		 * task's super state.
		 */
		if (target_super == tmp_super[0])
			break;

		/* Get target state next super state if it exists */
		if (target_super != NULL)
			target_super = (sm_state)target_super(port, SUPER_SIG);
	} while (tmp_super[0] != NULL);

	/* All done executing the exit actions */
#else
	obj->task_state(port, EXIT_SIG);
#endif
	/* update the state variables */
	obj->last_state = obj->task_state;
	obj->task_state = target;

#if (CONFIG_SM_NESTING_NUM > 1)
	/* Prepare to execute all entry actions of the target's super states */

	/* Get task's super state */
	last_super = (sm_state)obj->last_state(port, SUPER_SIG);

	/*
	 * Get targets super state. This will be NULL if the target
	 * has not super state
	 */
	tmp_super[CONFIG_SM_NESTING_NUM - 1] =
					(sm_state)target(port, SUPER_SIG);

	/* Initialize to NULL */
	for (i = CONFIG_SM_NESTING_NUM - 2; i >= 0; i--)
		tmp_super[i] = NULL;

	/* Get all super states of the target */
	for (i = CONFIG_SM_NESTING_NUM - 1; i >= 0; i--) {
		if ((tmp_super[i] != NULL) && (tmp_super[i] != last_super)) {
			if (last_super != NULL)
				last_super =
					(sm_state)last_super(port, SUPER_SIG);
			if (i > 0)
				tmp_super[i - 1] =
					(sm_state)tmp_super[i](port, SUPER_SIG);
		} else {
			tmp_super[i] = NULL;
			break;
		}
	}

	/* Execute all super state entry actions in forward order */
	for (i = 0; i < CONFIG_SM_NESTING_NUM; i++)
		if (tmp_super[i] != NULL)
			tmp_super[i](port, ENTRY_SIG);
#endif
	target(port, ENTRY_SIG);
}

void exe_state(int port, struct sm_obj *obj, int sig)
{
#if (CONFIG_SM_NESTING_NUM > 1)
	sm_state state = obj->task_state;

	do {
		state = (sm_state)state(port, sig);
	} while (state != NULL);
#else
	obj->task_state(port, sig);
#endif
}

