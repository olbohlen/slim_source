/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <stdio.h>
#include <string.h>

#include "../liberrsvc.h"

/*
 * Test #3
 */
boolean_t
test3(void)
{
	boolean_t	retval = B_FALSE;
	err_info_t	*rv1 = NULL;
	err_info_t	*rv2 = NULL;
	err_info_list_t *list = NULL;
	int count = 0;
	err_info_list_t *item;

	printf("\nTest 3: es_get_errors_by_modid - invalid mod_id\n");

	rv1 = es_create_err_info("TD", ES_ERR);
	if (rv1 == NULL) {
		printf("test FAILED\n");
	} else {
		rv2 = es_create_err_info("TI", ES_CLEANUP_ERR);
		if (rv2 == NULL) {
			printf("test FAILED\n");
		}
	}

	if (rv2 != NULL) {
		list = es_get_errors_by_modid("XXX");
		if (list == NULL) {
			printf("test PASSED\n");
			retval = B_TRUE;
		} else {
			count = 0;

			item = list;
			while (item != NULL) {
				count++;
				item = item->ei_next;
			}
			if (count != 0) {
				printf("test FAILED\n");
			} else {
				printf("test PASSES\n");
				retval = B_TRUE;
			}
		}

		es_free_err_info_list(list);
	}

	es_free_errors();

	return (retval);
}
