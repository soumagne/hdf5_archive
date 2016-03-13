/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the files COPYING and Copyright.html.  COPYING can be found at the root   *
 * of the source code distribution tree; Copyright.html can be found at the  *
 * root level of an installed copy of the electronic HDF5 document set and   *
 * is linked from the top-level documents page.  It can also be found at     *
 * http://hdfgroup.org/HDF5/doc/Copyright.html.  If you do not have          *
 * access to either file, you may request a copy from help@hdfgroup.org.     *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * Purpose:	Index routines.
 */

/****************/
/* Module Setup */
/****************/

#include "H5Xmodule.h"          /* This source code file is part of the H5X module */

/***********/
/* Headers */
/***********/
#include "H5private.h"      /* Generic Functions */
#include "H5Xpkg.h"         /* Index plugins */
#include "H5Eprivate.h"     /* Error handling */
#include "H5Iprivate.h"     /* IDs */
#include "H5MMprivate.h"    /* Memory management */
#include "H5Pprivate.h"     /* Property lists */
#include "H5VLprivate.h"    /* VOL plugins */
#include "H5ESprivate.h"
#include "H5FFprivate.h"    /* FF */
#include "H5VLiod_client.h"

/****************/
/* Local Macros */
/****************/

/******************/
/* Local Typedefs */
/******************/

/********************/
/* Local Prototypes */
/********************/

/*********************/
/* Package Variables */
/*********************/

/*****************************/
/* Library Private Variables */
/*****************************/

/*******************/
/* Local Variables */
/*******************/

/*-------------------------------------------------------------------------
 * Function:    H5Xcreate_ff
 *
 * Purpose: Create a new index in a container.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Xcreate_ff(hid_t loc_id, unsigned plugin_id, hid_t xcpl_id, hid_t trans_id,
    hid_t estack_id)
{
    H5X_class_t *idx_class = NULL;
    H5_priv_request_t *request = NULL; /* private request struct inserted in event queue */
    void **req = NULL; /* pointer to plugin generate requests (NULL if VOL plugin does not support async) */
    void *idx_handle = NULL; /* pointer to index object created */
    H5VL_object_t *file = NULL, *dset = NULL;
    size_t plugin_index;
    H5P_genplist_t *plist;
    hid_t dset_id = loc_id; /* TODO for now */
    hid_t xapl_id = H5P_INDEX_ACCESS_DEFAULT; /* TODO for now */
    size_t metadata_size; /* size of metadata created by plugin */
    void *metadata; /* metadata created by plugin that needs to be stored */
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)

    /* Check args */
    if ((plugin_id < 0) || (plugin_id > H5X_PLUGIN_MAX))
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid plugin identification number");
//    if (NULL == (file = (H5VL_object_t *) H5I_object_verify(file_id, H5I_FILE)))
//        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a file ID");
    if (NULL == H5I_object_verify(dset_id, H5I_DATASET))
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "scope_id is restricted to dataset ID");
    if (NULL == (dset = H5VL_get_object(dset_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid object/file identifier");

    /* Is the plugin already registered */
//    if (FALSE == H5X__registered(plugin_id, &plugin_index))
//        HGOTO_ERROR(H5E_INDEX, H5E_BADVALUE, FAIL, "plugin is not registered");

    /* Get correct property list */
    if (H5P_DEFAULT == xcpl_id)
        xcpl_id = H5P_INDEX_CREATE_DEFAULT;
    else
        if (TRUE != H5P_isa_class(xcpl_id, H5P_INDEX_CREATE))
            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not index creation property list");

    if(estack_id != H5_EVENT_STACK_NULL) {
        /* create the private request */
        if(NULL == (request = (H5_priv_request_t *)H5MM_calloc(sizeof(H5_priv_request_t))))
            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "memory allocation failed");
        req = &request->req;
        request->vol_cls = dset->vol_info->vol_cls;
    }

    /* Store the transaction ID in the xapl_id */
    if (NULL == (plist = (H5P_genplist_t *)H5I_object(xapl_id)))
        HGOTO_ERROR(H5E_ATOM, H5E_BADATOM, FAIL, "can't find object for ID");
    if (H5P_set(plist, H5VL_TRANS_ID, &trans_id) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't set property value for trans_id");

    /* Is the plugin already registered */
    if (NULL == (idx_class = H5X_registered(plugin_id)))
        HGOTO_ERROR(H5E_INDEX, H5E_BADVALUE, FAIL, "plugin is not registered");
    /* Call create of the plugin */
    if (NULL == idx_class->create)
        HGOTO_ERROR(H5E_INDEX, H5E_BADVALUE, FAIL, "plugin create callback is not defined");
    if (NULL == (idx_handle = idx_class->create(dset_id, xcpl_id, xapl_id,
            &metadata_size, &metadata)))
        HGOTO_ERROR(H5E_INDEX, H5E_CANTCREATE, FAIL, "cannot create new plugin index");

    /* Add idx_handle to dataset */
    if (FAIL == H5VL_iod_dataset_set_index(dset, idx_handle))
        HGOTO_ERROR(H5E_INDEX, H5E_CANTSET, FAIL, "cannot set index to dataset");
    if (FAIL == H5VL_iod_dataset_set_index_info(dset, plugin_id,
            metadata_size, metadata, trans_id, req))
        HGOTO_ERROR(H5E_INDEX, H5E_CANTSET, FAIL, "cannot set index info to dataset");

    if (request && *req)
        if(H5ES_insert(estack_id, request) < 0)
            HGOTO_ERROR(H5E_SYM, H5E_CANTINIT, FAIL, "failed to insert request in event stack")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Xcreate_ff() */

/*-------------------------------------------------------------------------
 * Function:    H5Xremove_ff
 *
 * Purpose: Remove an index from objects in a container.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Xremove_ff(hid_t loc_id, unsigned idx, hid_t trans_id, hid_t estack_id)
{
    H5_priv_request_t *request = NULL; /* private request struct inserted in event queue */
    void **req = NULL; /* pointer to plugin generate requests (NULL if VOL plugin does not support async) */
    H5VL_object_t *file = NULL, *dset = NULL;
    size_t plugin_index;
    H5P_genplist_t *plist;
    hid_t dataset_id = loc_id; /* TODO for now */
    hid_t xapl_id = H5P_INDEX_ACCESS_DEFAULT; /* TODO for now */
    size_t metadata_size; /* size of metadata created by plugin */
    void *metadata; /* metadata created by plugin that needs to be stored */
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)

    /* Check args */
//    if ((plugin_id < 0) || (plugin_id > H5X_PLUGIN_MAX))
//        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid plugin identification number");
//    if (NULL == (file = (H5VL_object_t *) H5I_object_verify(file_id, H5I_FILE)))
//        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "not a file ID");
//    if (NULL == H5I_object_verify(scope_id, H5I_DATASET))
//        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "scope_id is restricted to dataset ID");
//    if (NULL == (dset = (H5VL_object_t *) H5VL_get_object(scope_id)))
//        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid object/file identifier");
//
//    /* Is the plugin already registered */
//    if (FALSE == H5X__registered(plugin_id, &plugin_index))
//        HGOTO_ERROR(H5E_INDEX, H5E_BADVALUE, FAIL, "plugin is not registered");
//
//    if(estack_id != H5_EVENT_STACK_NULL) {
//        /* create the private request */
//        if(NULL == (request = (H5_priv_request_t *)H5MM_calloc(sizeof(H5_priv_request_t))))
//            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "memory allocation failed");
//        req = &request->req;
//        request->vol_cls = dset->vol_info->vol_cls;
//    }
//
//    /* Store the transaction ID in the xapl_id */
//    if (NULL == (plist = (H5P_genplist_t *)H5I_object(xapl_id)))
//        HGOTO_ERROR(H5E_ATOM, H5E_BADATOM, FAIL, "can't find object for ID");
//    if (H5P_set(plist, H5VL_TRANS_ID, &trans_id) < 0)
//        HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't set property value for trans_id");
//
//    /* Call remove of the plugin */
//    if (NULL == H5X_table_g[plugin_index].remove)
//        HGOTO_ERROR(H5E_INDEX, H5E_BADVALUE, FAIL, "plugin remove callback is not defined");
//    if (FAIL == H5X_table_g[plugin_index].remove(file_id, dataset_id,
//            metadata_size, metadata))
//        HGOTO_ERROR(H5E_INDEX, H5E_CANTCREATE, FAIL, "cannot remove index");
//
//    /* Remove idx_handle from dataset */
//    if (FAIL == H5VL_iod_dataset_set_index(dset, NULL))
//        HGOTO_ERROR(H5E_INDEX, H5E_CANTSET, FAIL, "cannot reset index handle");
//    if (FAIL == H5VL_iod_dataset_remove_index_info(dset, trans_id, req))
//        HGOTO_ERROR(H5E_INDEX, H5E_CANTSET, FAIL, "cannot remove index from dataset");
//
//    if (request && *req)
//        if(H5ES_insert(estack_id, request) < 0)
//            HGOTO_ERROR(H5E_SYM, H5E_CANTINIT, FAIL, "failed to insert request in event stack")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Xremove_ff() */

/*-------------------------------------------------------------------------
 * Function:    H5Xget_count_ff
 *
 * Purpose: Determine the number of index objects on an object.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Xget_count_ff(hid_t loc_id, hsize_t *idx_count, hid_t rcxt_id,
    hid_t estack_id)
{
    H5_priv_request_t *request = NULL; /* private request struct inserted in event queue */
    void **req = NULL; /* pointer to plugin generate requests (NULL if VOL plugin does not support async) */
    H5VL_object_t *dset;
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)

//    if (NULL == H5I_object_verify(scope_id, H5I_DATASET))
//        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "scope_id is restricted to dataset ID");
//    if (NULL == (dset = (void *) H5VL_get_object(scope_id)))
//            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid object/file identifier");
//    if (!idx_count)
//        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "idx_count is NULL");
//
//    if(estack_id != H5_EVENT_STACK_NULL) {
//        /* create the private request */
//        if(NULL == (request = (H5_priv_request_t *)H5MM_calloc(sizeof(H5_priv_request_t))))
//            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "memory allocation failed");
//        req = &request->req;
//        request->vol_cls = dset->vol_info->vol_cls;
//    }
//
//    /* Get index info */
//    if (FAIL == H5VL_iod_dataset_get_index_info(dset, idx_count, NULL, NULL, NULL,
//            rcxt_id, req))
//        HGOTO_ERROR(H5E_INDEX, H5E_CANTSET, FAIL, "cannot get indexing info from dataset");
//
//    if (request && *req)
//        if(H5ES_insert(estack_id, request) < 0)
//            HGOTO_ERROR(H5E_SYM, H5E_CANTINIT, FAIL, "failed to insert request in event stack")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Xget_count_ff() */

/*-------------------------------------------------------------------------
 * Function:    H5Pget_xapl_transaction
 *
 * Purpose:     Retrieve the transaction ID from this access plist.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Pget_xapl_transaction(hid_t xapl_id, hid_t *trans_id)
{
    H5P_genplist_t *plist = NULL; /* Property list pointer */
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)

    if (NULL == (plist = H5P_object_verify(xapl_id, H5P_INDEX_ACCESS)))
        HGOTO_ERROR(H5E_ATOM, H5E_BADATOM, FAIL, "not a xapl");

    /* Get the trans_id */
    if (trans_id)
        if (H5P_get(plist, H5VL_TRANS_ID, trans_id) < 0)
            HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get property value");

done:
    FUNC_LEAVE_API(ret_value)
}

/*-------------------------------------------------------------------------
 * Function:    H5Pget_xapl_read_context
 *
 * Purpose:     Retrieve the read context ID from this access plist.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Pget_xapl_read_context(hid_t xapl_id, hid_t *rcxt_id)
{
    H5P_genplist_t *plist = NULL; /* Property list pointer */
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)

    if (NULL == (plist = H5P_object_verify(xapl_id, H5P_INDEX_ACCESS)))
        HGOTO_ERROR(H5E_ATOM, H5E_BADATOM, FAIL, "not a xapl");

    /* Get the trans_id */
    if (rcxt_id)
        if (H5P_get(plist, H5VL_CONTEXT_ID, rcxt_id) < 0)
            HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get property value");

done:
    FUNC_LEAVE_API(ret_value)
}

/*-------------------------------------------------------------------------
 * Function:    H5Pget_xxpl_transaction
 *
 * Purpose:     Retrieve the transaction ID from this transfer plist.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Pget_xxpl_transaction(hid_t xxpl_id, hid_t *trans_id)
{
    H5P_genplist_t *plist = NULL; /* Property list pointer */
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)

    if (NULL == (plist = H5P_object_verify(xxpl_id, H5P_INDEX_XFER)))
        HGOTO_ERROR(H5E_ATOM, H5E_BADATOM, FAIL, "not a xxpl");

    /* Get the trans_id */
    if (trans_id)
        if (H5P_get(plist, H5VL_TRANS_ID, trans_id) < 0)
            HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get property value");

done:
    FUNC_LEAVE_API(ret_value)
}

/*-------------------------------------------------------------------------
 * Function:    H5Pget_xxpl_read_context
 *
 * Purpose:     Retrieve the read context ID from this transfer plist.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Pget_xxpl_read_context(hid_t xxpl_id, hid_t *rcxt_id)
{
    H5P_genplist_t *plist = NULL; /* Property list pointer */
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)

    if (NULL == (plist = H5P_object_verify(xxpl_id, H5P_INDEX_XFER)))
        HGOTO_ERROR(H5E_ATOM, H5E_BADATOM, FAIL, "not a xxpl");

    /* Get the trans_id */
    if (rcxt_id)
        if (H5P_get(plist, H5VL_CONTEXT_ID, rcxt_id) < 0)
            HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get property value");

done:
    FUNC_LEAVE_API(ret_value)
}
