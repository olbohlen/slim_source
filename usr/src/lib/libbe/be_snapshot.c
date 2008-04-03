/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * System includes
 */
#include <assert.h>
#include <errno.h>
#include <libintl.h>
#include <libnvpair.h>
#include <libzfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libbe.h"
#include "libbe_priv.h"

/* Private function prototypes */
static int be_rollback_check_callback(zfs_handle_t *, void *);
static int be_rollback_callback(zfs_handle_t *, void *);


/* ******************************************************************** */
/*                      Public Functions                                */
/* ******************************************************************** */

/*
 * Function:	be_create_snapshot
 * Description:	Creates a recursive snapshot of all the datasets within a BE.
 *		If the name of the BE to snapshot is not provided, it assumes
 *		we're snapshotting the currently running BE.  If the snapshot
 *		name is not provided it creates an auto named snapshot, which
 *		will be returned to the caller upon success.
 * Parameters:
 *		be_attrs - pointer to nvlist_t of attributes being passed in.
 *			The following attributes are used by this function:
 *
 *			BE_ATTR_ORIG_BE_NAME		*optional
 *			BE_ATTR_SNAP_NAME		*optional
 *			BE_ATTR_POLICY			*optional
 *
 *			If the BE_ATTR_SNAP_NAME was not passed in, upon
 *			successful BE snapshot creation, the following
 *			attribute value will be returned to the caller by
 *			setting it in the be_attrs parameter passed in:
 *
 *			BE_ATTR_SNAP_NAME
 *
 * Return:
 *		0 - Success
 *		non-zero - Failure
 * Scope:
 *		Public
 */
int
be_create_snapshot(nvlist_t *be_attrs)
{
	char		*be_name = NULL;
	char		*snap_name = NULL;
	char		*policy = NULL;
	boolean_t	autoname = B_FALSE;
	int 		ret = 0;

	/* Initialize libzfs handle */
	if (!be_zfs_init())
		return (1);

	/* Get original BE name if one was provided */
	if (nvlist_lookup_pairs(be_attrs, NV_FLAG_NOENTOK,
	    BE_ATTR_ORIG_BE_NAME, DATA_TYPE_STRING, &be_name, NULL) != 0) {
		(void) fprintf(stderr, gettext(
		    "be_create_snapshot: failed to lookup "
		    "BE_ATTR_ORIG_BE_NAME attribute\n"));
		be_zfs_fini();
		return (1);
	}

	/* Validate original BE name if one was provided */
	if (be_name != NULL && !be_valid_be_name(be_name)) {
		(void) fprintf(stderr, gettext("be_create_snapshot: "
		    "invalid BE name %s\n"), be_name);
		be_zfs_fini();
		return (1);
	}

	/* Get snapshot name to create if one was provided */
	if (nvlist_lookup_pairs(be_attrs, NV_FLAG_NOENTOK,
	    BE_ATTR_SNAP_NAME, DATA_TYPE_STRING, &snap_name, NULL) != 0) {
		(void) fprintf(stderr, gettext("be_create_snapshot: "
		    "failed to lookup BE_ATTR_SNAP_NAME attribute\n"));
		be_zfs_fini();
		return (1);
	}

	/* Get BE policy to create this snapshot under */
	if (nvlist_lookup_pairs(be_attrs, NV_FLAG_NOENTOK,
	    BE_ATTR_POLICY, DATA_TYPE_STRING, &policy, NULL) != 0) {
		(void) fprintf(stderr, gettext("be_create_snapshot: "
		    "failed to lookup BE_ATTR_POLICY attribute\n"));
		be_zfs_fini();
		return (1);
	}

	/*
	 * If no snap_name ws provided, we're going to create an
	 * auto named snapshot.  Set flag so that we know to pass
	 * the auto named snapshot to the caller later.
	 */
	if (snap_name == NULL)
		autoname = B_TRUE;

	if ((ret = _be_create_snapshot(be_name, &snap_name, policy)) == 0) {
		if (autoname == B_TRUE) {
			/*
			 * Set auto named snapshot name in the
			 * nvlist passed in by the caller.
			 */
			if (nvlist_add_string(be_attrs, BE_ATTR_SNAP_NAME,
			    snap_name) != 0) {
				(void) fprintf(stderr,
				    gettext("be_create_snapshot: failed "
				    "to add auto snap name to be_attrs\n"));
				ret = 1;
			}
		}
	}

	be_zfs_fini();

	return (ret);
}

/*
 * Function:	be_destroy_snapshot
 * Description:	Iterates through all the datasets of the BE and deletes
 *		the snapshots of each one with the specified name.  If the
 *		BE name is not provided, it assumes we're operating on the
 *		currently running BE.  The name of the snapshot name to
 *		destroy must be provided.
 * Parameters:
 *		be_attrs - pointer to nvlist_t of attributes being passed in.
 *			   The following attribute values are used by this
 *			   function:
 *
 *			   BE_ATTR_ORIG_BE_NAME		*optional
 *			   BE_ATTR_SNAP_NAME		*required
 * Return:
 *		0 - Success
 *		non-zero - Failure
 * Scope:
 *		Public
 */
int
be_destroy_snapshot(nvlist_t *be_attrs)
{
	char	*be_name = NULL;
	char	*snap_name = NULL;
	int 	ret = 0;

	/* Initialize libzfs handle */
	if (!be_zfs_init())
		return (1);

	/* Get original BE name if one was provided */
	if (nvlist_lookup_pairs(be_attrs, NV_FLAG_NOENTOK,
	    BE_ATTR_ORIG_BE_NAME, DATA_TYPE_STRING, &be_name, NULL) != 0) {
		(void) fprintf(stderr, gettext("be_destroy_snapshot: "
		    "failed to lookup BE_ATTR_ORIG_BE_NAME attribute\n"));
		return (1);
	}

	/* Validate original BE name if one was provided */
	if (be_name != NULL && !be_valid_be_name(be_name)) {
		(void) fprintf(stderr, gettext("be_destroy_snapshot: "
		    "invalid BE name %s\n"), be_name);
		return (1);
	}

	/* Get snapshot name to destroy */
	if (nvlist_lookup_string(be_attrs, BE_ATTR_SNAP_NAME, &snap_name)
	    != 0) {
		(void) fprintf(stderr, gettext("be_destroy_snapshot: "
		    "failed to lookup BE_ATTR_SNAP_NAME attribute.\n"));
		return (1);
	}

	ret = _be_destroy_snapshot(be_name, snap_name);

	be_zfs_fini();

	return (ret);
}

/*
 * Function:	be_rollback
 * Description:	Rolls back a BE and all of its children datasets to the
 *		named snapshot.  All of the BE's datasets must have the
 *		named snapshot for this function to succeed.  If the name
 *		of the BE is not passed in, this function assumes we're
 *		operating on the currently booted live BE.
 *
 *		Note - This function does not check if the BE has any
 *		younger snapshots than the one we're trying to rollback to.
 *		If it does, then those younger snapshots and their dependent
 *		clone file systems will get destroyed in the process of
 *		rolling back.
 *
 * Parameters:
 *		be_attrs - pointer to nvlist_t of attributes being passed in.
 *			   The following attributes are used by this function:
 *
 *			   BE_ATTR_ORIG_BE_NAME		*optional
 *			   BE_ATTR_SNAP_NAME		*required
 *
 * Returns:
 *		0 - Success
 *		1 - Failure
 * Scope:
 *		Public
 */
int
be_rollback(nvlist_t *be_attrs)
{
	be_transaction_data_t	bt = { 0 };
	zfs_handle_t		*zhp = NULL;
	char			obe_root_ds[MAXPATHLEN];
	int			zret;
	int			ret = 0;

	/* Initialize libzfs handle */
	if (!be_zfs_init())
		return (1);

	/* Get original BE name if one was provided */
	if (nvlist_lookup_pairs(be_attrs, NV_FLAG_NOENTOK,
	    BE_ATTR_ORIG_BE_NAME, DATA_TYPE_STRING, &bt.obe_name, NULL) != 0) {
		(void) fprintf(stderr, gettext("be_rollback: "
		    "failed to lookup BE_ATTR_ORIG_BE_NAME attribute\n"));
		return (1);
	}

	/* If original BE name not provided, use current BE */
	if (bt.obe_name == NULL) {
		if ((zret = zpool_iter(g_zfs,
		    be_zpool_find_current_be_callback, &bt)) == 0) {
			(void) fprintf(stderr, gettext("be_rollback: "
			    "failed to find current BE name\n"));
			return (1);
		} else if (zret < 0) {
			(void) fprintf(stderr, gettext("be_rollack: "
			    "zpool_iter failed: %s\n"),
			    libzfs_error_description(g_zfs));
			return (1);
		}
	} else {
		/* Validate original BE name  */
		if (!be_valid_be_name(bt.obe_name)) {
			(void) fprintf(stderr, gettext("be_rollback: "
			    "invalid BE name %s\n"), bt.obe_name);
			return (1);
		}
	}

	/* Get snapshot name to rollback to */
	if (nvlist_lookup_string(be_attrs, BE_ATTR_SNAP_NAME, &bt.obe_snap_name)
	    != 0) {
		(void) fprintf(stderr, gettext("be_rollback: "
		    "failed to lookup BE_ATTR_SNAP_NAME attribute.\n"));
		return (1);
	}

	/* Find which zpool obe_name lives in */
	if ((zret = zpool_iter(g_zfs, be_find_zpool_callback, &bt)) == 0) {
		(void) fprintf(stderr, gettext("be_rollback: "
		    "failed to find zpool for BE (%s)\n"), bt.obe_name);
		return (1);
	} else if (zret < 0) {
		(void) fprintf(stderr, gettext("be_rollback: "
		    "zpool_iter failed: %s\n"),
		    libzfs_error_description(g_zfs));
		return (1);
	}

	/* Generate string for BE's root dataset */
	be_make_root_ds(bt.obe_zpool, bt.obe_name, obe_root_ds,
	    sizeof (obe_root_ds));
	bt.obe_root_ds = obe_root_ds;

	/* Get handle to BE's root dataset */
	if ((zhp = zfs_open(g_zfs, bt.obe_root_ds, ZFS_TYPE_DATASET)) == NULL) {
		(void) fprintf(stderr, gettext("be_rollback: "
		    "failed to open BE root dataset (%s): %s\n"),
		    bt.obe_root_ds, libzfs_error_description(g_zfs));
		return (1);
	}

	/*
	 * Check that snapshot name exists for this BE and all of its
	 * children file systems.
	 */
	if (be_rollback_check_callback(zhp, bt.obe_snap_name) != 0) {
		zfs_close(zhp);
		return (1);
	}

	/*
	 * Iterate through a BE's datasets and roll them all back to
	 * the specified snapshot.
	 */
	if (be_rollback_callback(zhp, bt.obe_snap_name) != 0) {
		zfs_close(zhp);
		(void) fprintf(stderr, gettext("be_rollback: "
		    "failed to rollback BE %s to %s\n"), bt.obe_name,
		    bt.obe_snap_name);
		return (1);
	}

	zfs_close(zhp);

	be_zfs_fini();

	return (0);
}


/* ******************************************************************** */
/*                      Semi-Private Functions                          */
/* ******************************************************************** */

/*
 * Function:	_be_create_snapshot
 * Description:	see be_create_snapshot
 * Parameters:
 *		be_name - The name of the BE that we're taking a snapshot of.
 *		snap_name - The name of the snapshot we're creating. If
 *			snap_name is NULL an auto generated name will be used,
 *			and upon success, will return that name via this
 *			reference pointer.  The caller is responsible for
 *			freeing the returned name.
 *		policy - The clean-up policy type. (library wide use only)
 * Return:
 *		0 - Success
 *		non-zero - Failure
 * Scope:
 *		Semi-private (library wide use only)
 */
int
_be_create_snapshot(char *be_name, char **snap_name, char *policy)
{
	be_transaction_data_t	bt = { 0 };
	zfs_handle_t		*zhp = NULL;
	char			ss[MAXPATHLEN];
	char			root_ds[MAXPATHLEN];
	int			err = 0;
	int			i, ret;
	boolean_t		autoname = B_FALSE;

	/* Set parameters in bt structure */
	bt.obe_name = be_name;
	bt.obe_snap_name = *snap_name;
	bt.policy = policy;

	/* If original BE name not supplied, use current BE */
	if (bt.obe_name == NULL) {
		if ((ret = zpool_iter(g_zfs, be_zpool_find_current_be_callback,
		    &bt)) == 0) {
			(void) fprintf(stderr, gettext("be_create_snapshot: "
			    "failed to find current BE name\n"));
			return (1);
		} else if (ret < 0) {
			(void) fprintf(stderr, gettext("be_create_snapshot: "
			    "zpool_iter failed: %s\n"),
			    libzfs_error_description(g_zfs));
		return (1);
		}
	}

	/* Find which zpool obe_name lives in */
	if ((ret = zpool_iter(g_zfs, be_find_zpool_callback, &bt)) == 0) {
		(void) fprintf(stderr, gettext("be_create_snapshot: failed to "
		    "find zpool for BE (%s)\n"), bt.obe_name);
		return (1);
	} else if (ret < 0) {
		(void) fprintf(stderr, gettext("be_create_snapshot: "
		    "zpool_iter failed: %s\n"),
		    libzfs_error_description(g_zfs));
		return (1);
	}

	be_make_root_ds(bt.obe_zpool, bt.obe_name, root_ds,
	    sizeof (root_ds));
	bt.obe_root_ds = root_ds;

	/* If BE policy not specified, use the default policy */
	if (bt.policy == NULL) {
		bt.policy = be_default_policy();
	} else {
		/* Validate policy type */
		if (!valid_be_policy(bt.policy)) {
			(void) fprintf(stderr, gettext("be_create_snapshot: "
			    "invalid BE policy type (%s)\n"), bt.policy);
			return (1);
		}
	}

	/*
	 * If snapshot name not specified, set auto name flag and
	 * generate auto snapshot name.
	 */
	if (bt.obe_snap_name == NULL) {
		autoname = B_TRUE;
		if ((bt.obe_snap_name = be_auto_snap_name(bt.policy))
		    == NULL) {
			(void) fprintf(stderr,
			    gettext("be_create_snapshot: "
			    "failed to create auto snapshot name\n"));
			err = 1;
			goto done;
		}
	}

	/* Generate the name of the snapshot to take. */
	(void) snprintf(ss, sizeof (ss), "%s@%s", bt.obe_root_ds,
	    bt.obe_snap_name);

	/* Get handle to BE's root dataset */
	if ((zhp = zfs_open(g_zfs, bt.obe_root_ds, ZFS_TYPE_DATASET))
	    == NULL) {
		(void) fprintf(stderr, gettext("be_create_snapshot: "
		    "failed to open BE root dataset (%s)\n"),
		    bt.obe_root_ds);
		err = 1;
		goto done;
	}

	/* Create the snapshots recursively */
	if (zfs_snapshot(g_zfs, ss, B_TRUE) != 0) {
		if (!autoname || libzfs_errno(g_zfs) != EZFS_EXISTS) {
			(void) fprintf(stderr,
			    gettext("be_create_snapshot: "
			    "recursive snapshot of %s failed: %s\n"),
			    ss, libzfs_error_description(g_zfs));
			err = 1;
			goto done;
		} else {
			for (i = 1; i < BE_AUTO_NAME_MAX_TRY; i++) {

				/* Sleep 1 before retrying */
				sleep (1);

				/* Generate new auto snapshot name. */
				free(bt.obe_snap_name);
				if ((bt.obe_snap_name =
				    be_auto_snap_name(bt.policy)) == NULL) {
					(void) fprintf(stderr, gettext(
					    "be_create_snapshot: failed to "
					    "create auto snapshot name\n"));
					err = 1;
					goto done;
				}

				/* Generate string of the snapshot to take. */
				(void) snprintf(ss, sizeof (ss), "%s@%s",
				    bt.obe_root_ds, bt.obe_snap_name);

				/* Create the snapshots recursively */
				if (zfs_snapshot(g_zfs, ss, B_TRUE) != 0) {
					if (libzfs_errno(g_zfs) !=
					    EZFS_EXISTS) {
						(void) fprintf(stderr,
						    gettext(
						    "be_create_snapshot: "
						    "recursive snapshot of %s "
						    "failed: %s\n"), ss,
					    libzfs_error_description(g_zfs));
						err = 1;
						goto done;
					}
				} else {
					break;
				}
			}

			/*
			 * If we exhausted the maximum number of tries,
			 * free the auto snap name and set error.
			 */
			if (i == BE_AUTO_NAME_MAX_TRY) {
				(void) fprintf(stderr,
				    gettext("be_create_snapshot: failed "
				    "to create unique auto snapshot name\n"));
				free(bt.obe_snap_name);
				bt.obe_snap_name = NULL;
				err = 1;
			}
		}
	}

	/*
	 * If we succeeded in creating an auto named snapshot, store
	 * the name in the nvlist passed in by the caller.
	 */
	if (autoname && bt.obe_snap_name) {
		*snap_name = bt.obe_snap_name;
	}

done:	if (zhp != NULL)
		zfs_close(zhp);

	return(err);
}

/*
 * Function:	_be_destroy_snapshot
 * Description:	see be_destroy_snapshot
 * Parameters:
 *		be_name - The name of the BE that the snapshot belongs to.
 *		snap_name - The name of the snapshot we're destroying. 
 * Return:
 *		0 - Success
 *		non-zero - Failure
 * Scope:
 *		Semi-private (library wide use only)
 */
int
_be_destroy_snapshot(char *be_name, char *snap_name)
{
	be_transaction_data_t	bt = { 0 };
	zfs_handle_t		*zhp;
	char			ss[MAXPATHLEN];
	char			root_ds[MAXPATHLEN];
	int			err = 0;
	int			ret;

	/* Make sure we actaully have a snapshot name */
	if (snap_name == NULL) {
		(void) fprintf(stderr, gettext("be_destroy_snapshot: "
		    "invalid snapshot name\n"));
		return (1);
	}

	/* Set parameters in bt structure */
	bt.obe_name = be_name;
	bt.obe_snap_name = snap_name;

	/* If original BE name not supplied, use current BE */
	if (bt.obe_name == NULL) {
		if ((ret = zpool_iter(g_zfs, be_zpool_find_current_be_callback,
		    &bt)) == 0) {
			(void) fprintf(stderr, gettext("be_destroy_snapshot: "
			    "failed to find current BE name\n"));
			return (1);
		} else if (ret < 0) {
			(void) fprintf(stderr, gettext("be_destroy_snapshot: "
			    "zpool_iter failed: %s\n"),
			    libzfs_error_description(g_zfs));
		return (1);
		}
	}

	/* Find which zpool be_name lives in */
	if ((ret = zpool_iter(g_zfs, be_find_zpool_callback, &bt)) == 0) {
		(void) fprintf(stderr, gettext("be_destroy_snapshot: "
		    "failed to find zpool for BE (%s)\n"), bt.obe_name);
		return (1);
	} else if (ret < 0) {
		(void) fprintf(stderr, gettext("be_destroy_snapshot: "
		    "zpool_iter failed: %s\n"),
		    libzfs_error_description(g_zfs));
		return (1);
	}

	be_make_root_ds(bt.obe_zpool, bt.obe_name, root_ds,
	    sizeof (root_ds));
	bt.obe_root_ds = root_ds;

	zhp = zfs_open(g_zfs, bt.obe_root_ds, ZFS_TYPE_DATASET);
	if (zhp == NULL) {
		/*
		 * The zfs_open failed, return an error.
		 */
		(void) fprintf(stderr, gettext("be_destroy_snapshot: "
		    "failed to open BE root dataset (%s)\n"),
		    bt.obe_root_ds);
		err = 1;
	} else {
		/*
		 * Generate the name of the snapshot to take.
		 */
		(void) snprintf(ss, sizeof (ss), "%s@%s", bt.obe_name,
		    bt.obe_snap_name);

		/*
		 * destroy the snapshot.
		 */
		if (zfs_destroy_snaps(zhp, bt.obe_snap_name) != 0) {
			err = libzfs_errno(g_zfs);
			(void) fprintf(stderr, gettext("be_destroy_snapshot: "
			    "failed to destroy snapshot %s: %s\n"), ss,
			    libzfs_error_description(g_zfs));
		}
	}

	if (zhp != NULL)
		zfs_close(zhp);

	return(err);
}

/* ********************************************************************	*/
/*			Private Functions				*/
/* ********************************************************************	*/

/*
 * Function:	be_rollback_check_callback
 * Description:	Callback function used to iterate through a BE's filesystems
 *		to check if a given snapshot name exists.
 * Parameters:
 *		zhp - zfs_handle_t pointer to filesystem being processed.
 *		data - name of the snapshot to check for.
 * Returns:
 *		0 - Success, snapshot name exists for all filesystems.
 *		1 - Failure, snapshot name does not exist for all filesystems.
 * Scope:
 *		Private
 */
static int
be_rollback_check_callback(zfs_handle_t *zhp, void *data)
{
	char		*snap_name = data;
	char		ss[MAXPATHLEN];

	/* Generate string for this filesystem's snapshot name */
	(void) snprintf(ss, sizeof (ss), "%s@%s", zfs_get_name(zhp), snap_name);

	/* Check if snapshot exists */
	if (!zfs_dataset_exists(g_zfs, ss, ZFS_TYPE_SNAPSHOT)) {
		(void) fprintf(stderr, gettext("be_rollback_check_callback: "
		    "snapshot does not exist %s\n"), ss);
		return (1);
	}

	/* Iterate this dataset's children and check them */
	if (zfs_iter_filesystems(zhp, be_rollback_check_callback, snap_name)
	    != 0) {
		return (1);
	}

	return (0);
}

/*
 * Function:	be_rollback_callback
 * Description:	Callback function used to iterate through a BE's filesystems
 *		and roll them all back to the specified snapshot name.
 * Parameters:
 *		zhp - zfs_handle_t pointer to filesystem being processed.
 *		data - name of snapshot to rollback to.
 * Returns:
 *		0 - Success
 *		1 - Failure
 * Scope:
 *		Private
 */
static int
be_rollback_callback(zfs_handle_t *zhp, void *data)
{
	zfs_handle_t	*zhp_snap = NULL;
	char		*snap_name = data;
	char		ss[MAXPATHLEN];

	/* Generate string for this filesystem's snapshot name */
	(void) snprintf(ss, sizeof (ss), "%s@%s", zfs_get_name(zhp), snap_name);

	/* Get handle to this filesystem's snapshot */
	if ((zhp_snap = zfs_open(g_zfs, ss, ZFS_TYPE_SNAPSHOT)) == NULL) {
		(void) fprintf(stderr, gettext("be_rollback_callback: "
		    "failed to open snapshot %s: %s\n"), zfs_get_name(zhp),
		    libzfs_error_description(g_zfs));
		return (1);
	}

	/* Rollback dataset */
	if (zfs_rollback(zhp, zhp_snap, B_FALSE) != 0) {
		(void) fprintf(stderr, gettext("be_rollback_callback: "
		    "failed to rollback BE dataset %s to snapshot %s: %s\n"),
		    zfs_get_name(zhp), ss, libzfs_error_description(g_zfs));
		return (1);
	}

	/* Iterate this dataset's children and roll them back */
	if (zfs_iter_filesystems(zhp, be_rollback_callback, snap_name) != 0) {
		return (1);
	}

	return (0);
}