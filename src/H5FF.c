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
 * Purpose:	Wrappers around existing HDF5 to support Exascale FastForward
 *              functionality.
 */


/****************/
/* Module Setup */
/****************/

#include "H5FFmodule.h"         /* This source code file is part of the H5FF module */

#define H5F_FRIEND		/*suppress error about including H5Fpkg	  */

/***********/
/* Headers */
/***********/
#include "H5private.h"          /* Generic Functions                    */
#include "H5Eprivate.h"         /* Error handling                       */
#include "H5Fpkg.h"             /* File access                          */
#include "H5FFprivate.h"        /* FastForward wrappers                 */
#include "H5Iprivate.h"         /* IDs                                  */
#include "H5MMprivate.h"        /* Memory management                    */
#include "H5Pprivate.h"         /* Property lists                       */
#include "H5CXprivate.h"        /* Contexts                             */

/****************/
/* Local Macros */
/****************/
H5FL_EXTERN(H5VL_t);

/******************/
/* Local Typedefs */
/******************/


/********************/
/* Local Prototypes */
/********************/


/*********************/
/* Package Variables */
/*********************/

/* Package initialization variable */
hbool_t H5_PKG_INIT_VAR = FALSE;

/*****************************/
/* Library Private Variables */
/*****************************/


/*******************/
/* Local Variables */
/*******************/

herr_t
H5FF__init_package(void)
{
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_STATIC

    if(H5F_init() < 0)
        HDONE_ERROR(H5E_SYM, H5E_CANTINIT, FAIL, "unable to init file interface")

    if(H5CX_init() < 0)
        HDONE_ERROR(H5E_SYM, H5E_CANTINIT, FAIL, "unable to init context interface")

    FUNC_LEAVE_NOAPI(ret_value)
} /* H5FF__init_package() */

/*-------------------------------------------------------------------------
 * Function:    H5Fcreate_ff
 *
 * Purpose:     Asynchronous wrapper around H5Fcreate(). If requested,
 *              trans_id returns the transaction used to create the file,
 *              uncommitted. If trans_id is NULL, the transaction used to
 *              create the file will be committed.
 *
 * Return:      Success:        The placeholder ID for a new file.  When
 *                              the asynchronous operation completes, this
 *                              ID will transparently be modified to be a
 *                              "normal" ID.
 *              Failure:        FAIL
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5Fcreate_ff(const char *filename, unsigned flags, hid_t fcpl_id, hid_t fapl_id,
    hid_t context_id)
{
    H5VL_object_t *file = NULL;
    void *vol_file = NULL;          /* file token from VOL plugin */
    H5P_genplist_t *plist;          /* Property list pointer */
    H5VL_plugin_prop_t plugin_prop; /* Property for vol plugin ID & info */
    H5VL_class_t *vol_cls = NULL;   /* VOL Class structure for callback info */
    H5VL_t *vol_info = NULL;        /* VOL info struct */
    hid_t dxpl_id = H5AC_ind_read_dxpl_id;  /* dxpl used by library */
    H5CX_t *context;                /* Context */
    H5CX_req_t *req;
    hid_t file_id;
    hid_t ret_value;                /* Return value */

    FUNC_ENTER_API(FAIL)

    /* Check/fix arguments */
    if(!filename || !*filename)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid file name");
    /* In this routine, we only accept the following flags:
     *          H5F_ACC_EXCL, H5F_ACC_TRUNC and H5F_ACC_DEBUG
     */
    if(flags & ~(H5F_ACC_EXCL | H5F_ACC_TRUNC | H5F_ACC_DEBUG))
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid flags");
    /* The H5F_ACC_EXCL and H5F_ACC_TRUNC flags are mutually exclusive */
    if((flags & H5F_ACC_EXCL) && (flags & H5F_ACC_TRUNC))
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "mutually exclusive flags for file creation");

    /* Check file creation property list */
    if(H5P_DEFAULT == fcpl_id)
        fcpl_id = H5P_FILE_CREATE_DEFAULT;
    else
        if(TRUE != H5P_isa_class(fcpl_id, H5P_FILE_CREATE))
            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not file create property list");

    /* Verify access property list and get correct dxpl */
    if(H5P_verify_apl_and_dxpl(&fapl_id, H5P_CLS_FACC, &dxpl_id, H5I_INVALID_HID, TRUE) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTSET, FAIL, "can't set access and transfer property lists");

    /* get the VOL info from the fapl */
    if(NULL == (plist = (H5P_genplist_t *)H5I_object(fapl_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a file access property list");

    if(H5P_peek(plist, H5F_ACS_VOL_NAME, &plugin_prop) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get vol plugin info");

    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(plugin_prop.plugin_id, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID");

    /* Get context */
    if(NULL == (context = (H5CX_t *)H5I_object_verify(context_id, H5I_CONTEXT)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a context ID");

    /* Attach context to VOL */
    if(H5CX_attach(context, vol_cls) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTCREATE, FAIL, "cannot attach context");

    /* Create request */
    if(NULL == (req = H5CX_request_create(context)))
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTCREATE, FAIL, "cannot create request");
    req->req_type = H5CX_FILE_CREATE;
    req->callback = NULL;

    /* Add request to context */
    if(FAIL == H5CX_request_insert_processing(context, req))
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTINSERT, FAIL, "cannot insert request");

    /* Create a new file or truncate an existing file through the VOL */
    if(NULL == (vol_file = H5VL_file_create(vol_cls, filename, flags, fcpl_id,
        fapl_id, dxpl_id, &req->vol_req)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL, "unable to create file");

    /* setup VOL info struct */
    if(NULL == (vol_info = H5FL_CALLOC(H5VL_t)))
        HGOTO_ERROR(H5E_FILE, H5E_NOSPACE, FAIL, "can't allocate VL info struct");
    vol_info->vol_cls = vol_cls;
    vol_info->vol_id = plugin_prop.plugin_id;
    if(H5I_inc_ref(vol_info->vol_id, FALSE) < 0)
        HGOTO_ERROR(H5E_ATOM, H5E_CANTINC, FAIL, "unable to increment ref count on VOL plugin");

    /* Get an atom for the file */
    if((file_id = H5VL_register_id(H5I_FILE, vol_file, vol_info, TRUE)) < 0)
        HGOTO_ERROR(H5E_ATOM, H5E_CANTREGISTER, FAIL, "unable to atomize file handle");

    /* get the file object */
    if(NULL == (file = H5VL_get_object(file_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid file identifier");
    file->context = context;
    file->nreqs++;
    req->obj = file;

    ret_value = file_id;

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Fcreate_ff() */
