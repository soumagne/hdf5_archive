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
 * Purpose:     Query routines.
 */

/****************/
/* Module Setup */
/****************/

#include "H5Qmodule.h"      /* This source code file is part of the H5Q module */

/***********/
/* Headers */
/***********/
#include "H5private.h"      /* Generic Functions */
#include "H5Qpkg.h"         /* Query */
#include "H5Eprivate.h"     /* Error handling */
#include "H5Iprivate.h"     /* IDs */
#include "H5MMprivate.h"    /* Memory management */
#include "H5Pprivate.h"
#include "H5FLprivate.h"    /* Free lists */
#include "H5Dprivate.h"     /* Datasets */
#include "H5Rprivate.h"     /* References */
#include "H5Aprivate.h"     /* Attributes */
#include "H5FDcore.h"       /* Core driver */
#include "H5FFpublic.h"
#include "H5VLiod.h"
#include "H5VLiod_client.h"
#include "H5VLnative.h"

H5_DLL H5G_t *H5Q_apply_ff(hid_t loc_id, const H5Q_t *query, unsigned *result,
    hid_t vcpl_id, hid_t rcxt_id);
H5_DLL H5G_t *H5Q_apply_multi_ff(size_t loc_count, hid_t *loc_ids, const H5Q_t *query,
    unsigned *result, hid_t vcpl_id, hid_t *rcxt_ids);

/****************/
/* Local Macros */
/****************/

//#define H5Q_DEBUG

#ifdef H5Q_DEBUG
#define H5Q_LOG_DEBUG(...) do {                                 \
      fprintf(stdout, " # %s(): ", __func__);                   \
      fprintf(stdout, __VA_ARGS__);                             \
      fprintf(stdout, "\n");                                    \
      fflush(stdout);                                           \
  } while (0)
#else
#define H5Q_LOG_DEBUG(...) do { \
  } while (0)
#endif

/******************/
/* Local Typedefs */
/******************/

typedef struct {
    const H5Q_t *query;
    unsigned *result;
    H5Q_view_t *view;
} H5Q_apply_arg_t;

typedef struct {
    const char *filename;
    const char *loc_name;
    H5Q_apply_arg_t *apply_args;
} H5Q_apply_attr_arg_t;

typedef struct {
    const H5Q_t *query;
    hbool_t *result;
} H5Q_apply_attr_elem_arg_t;

/********************/
/* Local Prototypes */
/********************/

static herr_t H5Q__apply_iterate_ff(hid_t oid, const char *name,
    const H5O_ff_info_t *oinfo, void *udata, hid_t rcxt_id);
static herr_t H5Q__apply_object_ff(hid_t oid, const char *name,
    const H5O_ff_info_t *oinfo, void *udata, hid_t rcxt_id);
static herr_t H5Q__apply_object_link_ff(hid_t oid, const char *name,
    const H5O_ff_info_t *oinfo, void *udata);
static herr_t H5Q__apply_object_data_ff(hid_t oid, const char *name,
    const H5O_ff_info_t *oinfo, void *udata, hid_t rcxt_id);
static herr_t H5Q__apply_object_attr_ff(hid_t oid, const char *name,
    void *udata, hid_t rcxt_id);
static herr_t H5Q__apply_object_attr_iterate_ff(hid_t loc_id, const char *attr_name,
    const H5A_info_t *ainfo,  void *udata, hid_t rcxt_id);
static herr_t H5Q__apply_object_attr_name_ff(const char *attr_name, void *udata);
static herr_t H5Q__apply_object_attr_value_ff(hid_t loc_id, const char *attr_name,
    void *udata, hid_t rcxt_id);
static herr_t H5Q__apply_object_attr_value_iterate_ff(void *elem, hid_t type_id,
    unsigned ndim, const hsize_t *point, void *udata);


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
 * Function:    H5Qapply
 *
 * Purpose: Apply a query and return the result. Parameters, which the
 * query applies to, are determined by the type of the query.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5Qapply_ff(hid_t loc_id, hid_t query_id, unsigned *result, hid_t vcpl_id,
    hid_t rcxt_id, hid_t estack_id)
{
    H5Q_t *query = NULL;
    H5G_t *ret_grp;
    hid_t ret_value;
    hid_t native_vol = H5VL_NATIVE;
    H5VL_class_t *vol_cls = NULL; /* VOL Class structure for callback info */
    H5VL_t *vol_info = NULL;      /* VOL info struct */

    FUNC_ENTER_API(FAIL)
    H5TRACE4("i", "ii*Iui", loc_id, query_id, result, vcpl_id);

    /* Check args and get the query objects */
    if (NULL == (query = (H5Q_t *) H5I_object_verify(query_id, H5I_QUERY)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a query ID");
    if (!result)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "NULL pointer for result");
    if(estack_id != H5_EVENT_STACK_NULL)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "Event stack not supported for now");

    /* Get the default view creation property list if the user didn't provide one */
    /* TODO fix that */
//    if (H5P_DEFAULT == vcpl_id)
//        vcpl_id = H5P_INDEX_ACCESS_DEFAULT;
//    else
//        if (TRUE != H5P_isa_class(vcpl_id, H5P_INDEX_ACCESS))
//            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not index access parms");

    /* Apply query */
    if (NULL == (ret_grp = H5Q_apply_ff(loc_id, query, result, vcpl_id, rcxt_id)))
        HGOTO_ERROR(H5E_QUERY, H5E_CANTCOMPARE, FAIL, "unable to apply query");

    /* Setup VOL info */
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(native_vol, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")
    if(NULL == (vol_info = H5FL_CALLOC(H5VL_t)))
        HGOTO_ERROR(H5E_FILE, H5E_NOSPACE, FAIL, "can't allocate VL info struct")
    vol_info->vol_cls = vol_cls;
    vol_info->vol_id = native_vol;
    if(H5I_inc_ref(vol_info->vol_id, FALSE) < 0)
        HGOTO_ERROR(H5E_ATOM, H5E_CANTINC, FAIL, "unable to increment ref count on VOL plugin")

    /* Get an atom for the group */
    if((ret_value = H5VL_register_id(H5I_GROUP, (void *)ret_grp, vol_info, TRUE)) < 0)
        HGOTO_ERROR(H5E_ATOM, H5E_CANTREGISTER, FAIL, "unable to atomize group handle")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Qapply() */

/*-------------------------------------------------------------------------
 * Function:    H5Q_apply
 *
 * Purpose: Private function for H5Qapply.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
H5G_t *
H5Q_apply_ff(hid_t loc_id, const H5Q_t *query, unsigned *result,
        hid_t H5_ATTR_UNUSED vcpl_id, hid_t rcxt_id)
{
    H5Q_apply_arg_t args;
    H5Q_view_t view = H5Q_VIEW_INITIALIZER(view); /* Resulting view */
    H5G_t *ret_grp = NULL; /* New group created */
    H5G_t *ret_value = NULL; /* Return value */
    H5P_genclass_t *pclass = NULL;
    unsigned flags;
    hid_t fapl_id = FAIL;
    H5F_t *new_file = NULL;
    H5G_loc_t file_loc;

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(query);
    HDassert(result);

    /* First check and optimize query */
    /* TODO */

    /* Create new view and init args */
    args.query = query;
    args.result = result;
    args.view = &view;

    if (FAIL == H5Ovisit_ff(loc_id, H5_INDEX_NAME, H5_ITER_NATIVE, H5Q__apply_iterate_ff,
            &args, rcxt_id, H5_EVENT_STACK_NULL))
        HGOTO_ERROR(H5E_SYM, H5E_BADITER, NULL, "object visitation failed");

    if (!H5Q_QUEUE_EMPTY(&view.reg_refs))
        H5Q_LOG_DEBUG("Number of reg refs: %zu\n", view.reg_refs.n_elem);
    if (!H5Q_QUEUE_EMPTY(&view.obj_refs))
        H5Q_LOG_DEBUG("Number of obj refs: %zu\n", view.obj_refs.n_elem);
    if (!H5Q_QUEUE_EMPTY(&view.attr_refs))
        H5Q_LOG_DEBUG("Number of attr refs: %zu\n", view.attr_refs.n_elem);

    /* Get property list class */
    if (NULL == (pclass = (H5P_genclass_t *)H5I_object_verify(H5P_FILE_ACCESS, H5I_GENPROP_CLS)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, NULL, "not a property list class");

    /* Create the new property list */
    if (FAIL == (fapl_id = H5P_create_id(pclass, TRUE)))
        HGOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "unable to create property list");

    /* Use the core VFD to store the view */
    if (FAIL == H5Pset_fapl_core(fapl_id, H5Q_VIEW_CORE_INCREMENT, FALSE))
        HGOTO_ERROR(H5E_PLIST, H5E_CANTSET, NULL, "unable to set property list to core VFD");

    /* Create a new file or truncate an existing file. */
    flags = H5F_ACC_EXCL | H5F_ACC_RDWR | H5F_ACC_CREAT;
    if (NULL == (new_file = H5F_open("view", flags, H5P_FILE_CREATE_DEFAULT, fapl_id, H5AC_dxpl_id)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, NULL, "unable to create file");

    /* Construct a group location for root group of the file */
    if (FAIL == H5G_root_loc(new_file, &file_loc))
        HGOTO_ERROR(H5E_SYM, H5E_BADVALUE, NULL, "unable to create location for file")

    /* Create the new group & get its ID */
    if (NULL == (ret_grp = H5G_create_anon(&file_loc, H5P_GROUP_CREATE_DEFAULT, H5P_GROUP_ACCESS_DEFAULT)))
        HGOTO_ERROR(H5E_SYM, H5E_CANTINIT, NULL, "unable to create group");

    /* Write view */
    if (FAIL == H5Q__view_write(ret_grp, &view))
        HGOTO_ERROR(H5E_QUERY, H5E_WRITEERROR, NULL, "can't write view");

    ret_value = ret_grp;

done:
    /* Release the group's object header, if it was created */
    if (ret_grp) {
        H5O_loc_t *grp_oloc;         /* Object location for group */

        /* Get the new group's object location */
        if (NULL == (grp_oloc = H5G_oloc(ret_grp)))
            HDONE_ERROR(H5E_SYM, H5E_CANTGET, NULL, "unable to get object location of group");

        /* Decrement refcount on group's object header in memory */
        if (FAIL == H5O_dec_rc_by_loc(grp_oloc, H5AC_dxpl_id))
            HDONE_ERROR(H5E_SYM, H5E_CANTDEC, NULL, "unable to decrement refcount on newly created object");
    } /* end if */

    /* Cleanup on failure */
    if (NULL == ret_value)
        if (ret_grp && (FAIL == H5G_close(ret_grp)))
            HDONE_ERROR(H5E_SYM, H5E_CLOSEERROR, NULL, "unable to release group");

    /* Close the property list */
    if ((fapl_id != FAIL) && (H5I_dec_app_ref(fapl_id) < 0))
        HDONE_ERROR(H5E_PLIST, H5E_CANTFREE, NULL, "can't close");

    /* Attempt to close the file/mount hierarchy */
    if (new_file && (FAIL == H5F_try_close(new_file)))
        HDONE_ERROR(H5E_FILE, H5E_CANTCLOSEFILE, NULL, "can't close file")

    /* Free the view */
    H5Q__view_free(&view);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5Q_apply() */

/*-------------------------------------------------------------------------
 * Function:    H5Q__apply_iterate
 *
 * Purpose: Private function for H5Q_apply.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5Q__apply_iterate_ff(hid_t oid, const char *name, const H5O_ff_info_t *oinfo,
    void *udata, hid_t rcxt_id)
{
    H5Q_apply_arg_t *args = (H5Q_apply_arg_t *) udata;
    H5Q_type_t query_type;
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(args->query);
    HDassert(args->result);
    HDassert(args->view);

    if (FAIL == H5Q_get_type(args->query, &query_type))
        HGOTO_ERROR(H5E_QUERY, H5E_CANTGET, FAIL, "unable to get query type");

    if (query_type != H5Q_TYPE_MISC) {
        if (FAIL == H5Q__apply_object_ff(oid, name, oinfo, args, rcxt_id))
            HGOTO_ERROR(H5E_QUERY, H5E_CANTCOMPARE, FAIL, "unable to compare query");
    } else {
        H5Q_combine_op_t op_type;
        H5Q_apply_arg_t args1, args2;
        H5Q_view_t view1 = H5Q_VIEW_INITIALIZER(view1), view2 = H5Q_VIEW_INITIALIZER(view2);
        unsigned result1 = 0, result2 = 0;

        if (FAIL == H5Q_get_combine_op(args->query, &op_type))
            HGOTO_ERROR(H5E_QUERY, H5E_CANTGET, FAIL, "unable to get combine op");

        args1.query = args->query->query.combine.l_query;
        args1.result = &result1;
        args1.view = &view1;

        args2.query = args->query->query.combine.r_query;
        args2.result = &result2;
        args2.view = &view2;

        if (FAIL == H5Q__apply_iterate_ff(oid, name, oinfo, &args1, rcxt_id))
            HGOTO_ERROR(H5E_QUERY, H5E_CANTCOMPARE, FAIL, "unable to apply query");
        if (FAIL == H5Q__apply_iterate_ff(oid, name, oinfo, &args2, rcxt_id))
            HGOTO_ERROR(H5E_QUERY, H5E_CANTCOMPARE, FAIL, "unable to apply query");

        if (FAIL == H5Q__view_combine(op_type, &view1, &view2, result1, result2,
                args->view, args->result))
            HGOTO_ERROR(H5E_QUERY, H5E_CANTMERGE, FAIL, "unable to merge results");

        if (result1) H5Q__view_free(&view1);
        if (result2) H5Q__view_free(&view2);
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5Q__apply_iterate */

/*-------------------------------------------------------------------------
 * Function:    H5Q__apply_object
 *
 * Purpose: Private function for H5Q__apply_iterate.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5Q__apply_object_ff(hid_t oid, const char *name,
    const H5O_ff_info_t *oinfo, void *udata, hid_t rcxt_id)
{
    H5Q_apply_arg_t *args = (H5Q_apply_arg_t *) udata;
    H5Q_type_t query_type;
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(name);
    HDassert(oinfo);
    HDassert(args);

    if (FAIL == H5Q_get_type(args->query, &query_type))
        HGOTO_ERROR(H5E_QUERY, H5E_CANTGET, FAIL, "unable to get query type");

    switch (query_type) {
        /* If query on a link name, just compare the name of the object */
        case H5Q_TYPE_LINK_NAME:
            if (FAIL == H5Q__apply_object_link_ff(oid, name, oinfo, udata))
                HGOTO_ERROR(H5E_QUERY, H5E_CANTCOMPARE, FAIL, "can't apply link query to object");
            break;
        case H5Q_TYPE_DATA_ELEM:
            if (FAIL == H5Q__apply_object_data_ff(oid, name, oinfo, udata, rcxt_id))
                HGOTO_ERROR(H5E_QUERY, H5E_CANTCOMPARE, FAIL, "can't apply data query to object");
            break;
        case H5Q_TYPE_ATTR_NAME:
        case H5Q_TYPE_ATTR_VALUE:
            if (FAIL == H5Q__apply_object_attr_ff(oid, name, udata, rcxt_id))
                HGOTO_ERROR(H5E_QUERY, H5E_CANTCOMPARE, FAIL, "can't apply data query to object");
            break;
        case H5Q_TYPE_MISC:
        default:
            HGOTO_ERROR(H5E_QUERY, H5E_BADTYPE, FAIL, "unsupported/unrecognized query type");
            break;
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5Q__apply_object */

/*-------------------------------------------------------------------------
 * Function:    H5Q__apply_object_link
 *
 * Purpose: Private function for H5Q__apply_object.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5Q__apply_object_link_ff(hid_t loc_id, const char *name,
    const H5O_ff_info_t *oinfo, void *udata)
{
    href_t ref;
    hbool_t result;
    H5Q_apply_arg_t *args = (H5Q_apply_arg_t *) udata;
    const char *link_name = NULL;
    const char *trimmed_path = NULL;
    const char *file_name = NULL;
    H5VL_object_t *obj = NULL;
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(name);
    HDassert(oinfo);
    HDassert(args);

    trimmed_path = HDstrrchr(name, '/');
    link_name = (trimmed_path) ? ++trimmed_path : name;

    if ((oinfo->type != H5O_TYPE_GROUP) && (oinfo->type != H5O_TYPE_DATASET))
        HGOTO_ERROR(H5E_QUERY, H5E_BADTYPE, FAIL, "unsupported/unrecognized object type");

    if (FAIL == H5Q_apply_atom(args->query, &result, link_name))
        HGOTO_ERROR(H5E_QUERY, H5E_CANTCOMPARE, FAIL, "can't compare link name");

    if (!result) HGOTO_DONE(SUCCEED);

    *(args->result) = H5Q_REF_OBJ;

    H5Q_LOG_DEBUG("Match link name: %s (%s)\n", link_name, name);

    /* Keep object reference */
    if (NULL == (obj = (H5VL_object_t *) H5VL_get_object(loc_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid object/file identifier");
    if (NULL == (file_name = H5VL_iod_get_filename(obj)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid file name");

    if (NULL == (ref = H5R_create_ext_object(file_name, name)))
        HGOTO_ERROR(H5E_DATASET, H5E_CANTCREATE, FAIL, "can't create object reference");
    if (FAIL == H5Q__view_append(args->view, H5R_EXT_OBJECT, ref))
        HGOTO_ERROR(H5E_QUERY, H5E_CANTAPPEND, FAIL, "can't append object reference to view");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5Q__apply_object_link */

/*-------------------------------------------------------------------------
 * Function:    H5Q__apply_object_data
 *
 * Purpose: Private function for H5Q__apply_object.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5Q__apply_object_data_ff(hid_t loc_id, const char *name,
    const H5O_ff_info_t *oinfo, void *udata, hid_t rcxt_id)
{
    href_t ref;
    H5Q_apply_arg_t *args = (H5Q_apply_arg_t *) udata;
    hid_t obj_id = FAIL;
    hid_t space_id = FAIL;
    H5S_t *space = NULL;
    const char *file_name = NULL;
    H5VL_object_t *obj = NULL;
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(name);
    HDassert(oinfo);
    HDassert(args);

    /* No data */
    if (oinfo->type == H5O_TYPE_GROUP)
        HGOTO_DONE(SUCCEED);

    if (oinfo->type != H5O_TYPE_DATASET)
        HGOTO_ERROR(H5E_QUERY, H5E_BADTYPE, FAIL, "unsupported/unrecognized object type");

    /* If query on a dataset, open the object and use H5D_query */
    if (FAIL == (obj_id = H5Oopen_ff(loc_id, name, H5P_DEFAULT, rcxt_id)))
        HGOTO_ERROR(H5E_DATASET, H5E_CANTOPENOBJ, FAIL, "can't open object");

    if (NULL == H5I_object_verify(obj_id, H5I_DATASET))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a dataset");

    /* Query dataset */
    if (FAIL == (space_id = H5Dquery_ff(obj_id, -1, args->query->query_id, rcxt_id)))
        HGOTO_ERROR(H5E_DATASET, H5E_CANTSELECT, FAIL, "can't query dataset");

    /* No element matched the query */
    if (H5S_SEL_NONE == H5Sget_select_type(space_id))
        HGOTO_DONE(SUCCEED);

    *(args->result) = H5Q_REF_REG;

    /* Keep dataset region reference */
    if (NULL == (obj = (H5VL_object_t *) H5VL_get_object(loc_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid object/file identifier");
    if (NULL == (file_name = H5VL_iod_get_filename(obj)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid file name");
    if (NULL == (space = (H5S_t *) H5I_object_verify(space_id, H5I_DATASPACE)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a dataset");

    if (NULL == (ref = H5R_create_ext_region(file_name, name, space)))
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "can't get buffer size for region reference");
    if (FAIL == H5Q__view_append(args->view, H5R_EXT_REGION, ref))
        HGOTO_ERROR(H5E_QUERY, H5E_CANTAPPEND, FAIL, "can't append region reference to view");

done:
    if (space_id != FAIL) H5Sclose(space_id);
    if ((obj_id != FAIL) && (FAIL == H5Oclose_ff(obj_id, H5_EVENT_STACK_NULL)))
        HDONE_ERROR(H5E_OHDR, H5E_CANTRELEASE, FAIL, "unable to close object")
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5Q__apply_object_data */

/*-------------------------------------------------------------------------
 * Function:    H5Q__apply_object_attr
 *
 * Purpose: Private function for H5Q__apply_object.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5Q__apply_object_attr_ff(hid_t loc_id, const char *name, void *udata,
    hid_t rcxt_id)
{
    H5Q_apply_attr_arg_t attr_args;
    H5Q_apply_arg_t *args = (H5Q_apply_arg_t *) udata;
    hid_t obj_id = FAIL;
    const char *file_name = NULL;
    H5VL_object_t *obj = NULL;
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(name);
    HDassert(args);

    /* Build attribute operator info */
    if (NULL == (obj = (H5VL_object_t *) H5VL_get_object(loc_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid object/file identifier");
    if (NULL == (file_name = H5VL_iod_get_filename(obj)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "invalid file name");

    attr_args.filename = file_name;
    attr_args.loc_name = name;
    attr_args.apply_args = args;

    if (0 == HDstrcmp(name, ".")) {
        obj_id = loc_id;
    } else {
        if (FAIL == (obj_id = H5Oopen_ff(loc_id, name, H5P_DEFAULT, rcxt_id)))
            HGOTO_ERROR(H5E_DATASET, H5E_CANTOPENOBJ, FAIL, "can't open object");
    }

    /* Iterate over attributes */
    if (FAIL == (ret_value = H5Aiterate_ff(obj_id, H5_INDEX_NAME, H5_ITER_NATIVE,
            NULL, H5Q__apply_object_attr_iterate_ff, &attr_args, rcxt_id, H5_EVENT_STACK_NULL)))
        HGOTO_ERROR(H5E_ATTR, H5E_BADITER, FAIL, "error iterating over attributes");

done:
    if ((obj_id != FAIL) && (obj_id != loc_id) && (FAIL == H5Oclose_ff(obj_id, H5_EVENT_STACK_NULL)))
        HDONE_ERROR(H5E_OHDR, H5E_CANTRELEASE, FAIL, "unable to close object")
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5Q__apply_object_attr */

/*-------------------------------------------------------------------------
 * Function:    H5Q__apply_object_attr_iterate
 *
 * Purpose: Private function for H5Q__apply_iterate.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5Q__apply_object_attr_iterate_ff(hid_t loc_id, const char *attr_name,
    const H5A_info_t H5_ATTR_UNUSED *ainfo, void *udata, hid_t rcxt_id)
{
    H5Q_apply_attr_arg_t *args = (H5Q_apply_attr_arg_t *) udata;
    H5Q_type_t query_type;
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(attr_name);
    HDassert(args);

    if (FAIL == H5Q_get_type(args->apply_args->query, &query_type))
        HGOTO_ERROR(H5E_QUERY, H5E_CANTGET, FAIL, "unable to get query type");

    switch (query_type) {
        /* If query on an attribute name, just compare the name of the object */
        case H5Q_TYPE_ATTR_NAME:
            if (FAIL == H5Q__apply_object_attr_name_ff(attr_name, udata))
                HGOTO_ERROR(H5E_QUERY, H5E_CANTCOMPARE, FAIL, "can't apply attr name query to object");
            break;
        case H5Q_TYPE_ATTR_VALUE:
            if (FAIL == H5Q__apply_object_attr_value_ff(loc_id, attr_name,
                    udata, rcxt_id))
                HGOTO_ERROR(H5E_QUERY, H5E_CANTCOMPARE, FAIL, "can't apply attr name query to object");
            break;
        case H5Q_TYPE_LINK_NAME:
        case H5Q_TYPE_DATA_ELEM:
        case H5Q_TYPE_MISC:
        default:
            HGOTO_ERROR(H5E_QUERY, H5E_BADTYPE, FAIL, "unsupported/unrecognized query type");
            break;
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5Q__apply_object_attr_iterate */

/*-------------------------------------------------------------------------
 * Function:    H5Q__apply_object_attr_name
 *
 * Purpose: Private function for H5Q__apply_object_attr_iterate.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5Q__apply_object_attr_name_ff(const char *attr_name, void *udata)
{
    H5Q_apply_attr_arg_t *args = (H5Q_apply_attr_arg_t *) udata;
    href_t ref;
    hbool_t result = FALSE;
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(attr_name);
    HDassert(args);

    if (FAIL == H5Q_apply_atom(args->apply_args->query, &result, attr_name))
        HGOTO_ERROR(H5E_QUERY, H5E_CANTCOMPARE, FAIL, "can't compare attr name");

    if (!result) HGOTO_DONE(SUCCEED);

    *(args->apply_args->result) = H5Q_REF_ATTR;

    H5Q_LOG_DEBUG("Match attribute name: %s\n", attr_name);

    /* Keep attribute reference */
    if (NULL == (ref = H5R_create_ext_attr(args->filename, args->loc_name, attr_name)))
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "can't get buffer size for attribute reference");
    if (FAIL == H5Q__view_append(args->apply_args->view, H5R_EXT_ATTR, ref))
        HGOTO_ERROR(H5E_QUERY, H5E_CANTAPPEND, FAIL, "can't append attribute reference to view");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5Q__apply_object_attr_name */

/*-------------------------------------------------------------------------
 * Function:    H5Q__apply_object_attr_value
 *
 * Purpose: Private function for H5Q__apply_object_attr_iterate.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5Q__apply_object_attr_value_ff(hid_t loc_id, const char *attr_name,
    void *udata, hid_t rcxt_id)
{
    H5Q_apply_attr_arg_t *args = (H5Q_apply_attr_arg_t *) udata;
    void *buf = NULL;
    size_t buf_size;
    hid_t attr_id = FAIL;
    hid_t type_id = FAIL;
    hid_t space_id = FAIL;
    size_t nelmts, elmt_size;
    H5Q_apply_attr_elem_arg_t iter_args;
    href_t ref;
    hbool_t result = FALSE;
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(attr_name);
    HDassert(args);

    /* Open attribute */
    if (FAIL == (attr_id = H5Aopen_ff(loc_id, attr_name, H5P_DEFAULT, rcxt_id, H5_EVENT_STACK_NULL)))
        HGOTO_ERROR(H5E_ATTR, H5E_CANTOPENOBJ, FAIL, "can't open attribute");

    /* Get attribute info */
    if (FAIL == (type_id = H5Aget_type(attr_id)))
        HGOTO_ERROR(H5E_OHDR, H5E_CANTGET, FAIL, "can't get attribute datatype");
    if (FAIL == (space_id = H5Aget_space(attr_id)))
        HGOTO_ERROR(H5E_INDEX, H5E_CANTGET, FAIL, "can't get attribute dataspace");
    if (0 == (nelmts = (size_t) H5Sget_select_npoints(space_id)))
        HGOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, FAIL, "invalid number of elements");
    if (0 == (elmt_size = H5Tget_size(type_id)))
        HGOTO_ERROR(H5E_DATATYPE, H5E_BADTYPE, FAIL, "invalid size of element");

    /* Allocate buffer to hold data */
    buf_size = nelmts * elmt_size;
    if (NULL == (buf = H5MM_malloc(buf_size)))
        HGOTO_ERROR(H5E_QUERY, H5E_NOSPACE, FAIL, "can't allocate read buffer");

    /* Read data */
    if (FAIL == H5Aread_ff(attr_id, type_id, buf, rcxt_id, H5_EVENT_STACK_NULL))
        HGOTO_ERROR(H5E_ATTR, H5E_READERROR, FAIL, "unable to read attribute");

    iter_args.query = args->apply_args->query;
    iter_args.result = &result;

    /* Iterate over attribute elements to compare values */
    if (FAIL == H5Diterate(buf, type_id, space_id, H5Q__apply_object_attr_value_iterate_ff, &iter_args))
        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTCOMPARE, FAIL, "unable to compare attribute elements");

    if (!result) HGOTO_DONE(SUCCEED);

    *(args->apply_args->result) = H5Q_REF_ATTR;

    H5Q_LOG_DEBUG("Match value of attribute name: %s\n", attr_name);

    /* Keep attribute reference */
    if (NULL == (ref = H5R_create_ext_attr(args->filename, args->loc_name, attr_name)))
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "can't get buffer size for attribute reference");
    if (FAIL == H5Q__view_append(args->apply_args->view, H5R_EXT_ATTR, ref))
        HGOTO_ERROR(H5E_QUERY, H5E_CANTAPPEND, FAIL, "can't append attribute reference to view");

done:
    H5MM_free(buf);
    if (attr_id != FAIL) H5Aclose_ff(attr_id, H5_EVENT_STACK_NULL);
    if (type_id != FAIL) H5Tclose(type_id);
    if (space_id != FAIL) H5Sclose(space_id);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5Q__apply_object_attr_value */

/*-------------------------------------------------------------------------
 * Function:    H5Q__apply_object_attr_value_iterate
 *
 * Purpose: Private function for H5Q__apply_iterate.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5Q__apply_object_attr_value_iterate_ff(void *elem, hid_t type_id,
    unsigned H5_ATTR_UNUSED ndim, const hsize_t H5_ATTR_UNUSED *point, void *udata)
{
    H5Q_apply_attr_elem_arg_t *args = (H5Q_apply_attr_elem_arg_t *) udata;
    hbool_t result;
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(elem);
    HDassert(args);

    /* Apply the query */
    if (FAIL == H5Qapply_atom(args->query->query_id, &result, type_id, elem))
        HGOTO_ERROR(H5E_QUERY, H5E_CANTCOMPARE, FAIL, "unable to apply query to data element");

    *(args->result) |= result;

done:

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5Q__apply_object_attr_value_iterate */

/*-------------------------------------------------------------------------
 * Function:    H5Qapply_multi
 *
 * Purpose: Apply a query on multiple locations and return the result.
 * Parameters, which the query applies to, are determined by the type of the
 * query.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5Qapply_multi_ff(size_t loc_count, hid_t *loc_ids, hid_t query_id,
    unsigned *result, hid_t vcpl_id, hid_t *rcxt_ids, hid_t estack_id)
{
    H5Q_t *query = NULL;
    H5G_t *ret_grp;
    hid_t ret_value;
    hid_t native_vol = H5VL_NATIVE;
    H5VL_class_t *vol_cls = NULL; /* VOL Class structure for callback info */
    H5VL_t *vol_info = NULL;      /* VOL info struct */

    FUNC_ENTER_API(FAIL)

    /* Check args and get the query objects */
    if (!loc_count)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "loc_count cannot be NULL");
    if (!loc_ids)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "loc_ids cannot be NULL");
    if (NULL == (query = (H5Q_t *) H5I_object_verify(query_id, H5I_QUERY)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a query ID");
    if (!result)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "NULL pointer for result");
    if (!rcxt_ids)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "rcxt_ids cannot be NULL");
    if(estack_id != H5_EVENT_STACK_NULL)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "Event stack not supported for now");

    /* Get the default view creation property list if the user didn't provide one */
    /* TODO fix that */
//    if (H5P_DEFAULT == vcpl_id)
//        vcpl_id = H5P_INDEX_ACCESS_DEFAULT;
//    else
//        if (TRUE != H5P_isa_class(vcpl_id, H5P_INDEX_ACCESS))
//            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not index access parms");

    /* Apply query */
    if (NULL == (ret_grp = H5Q_apply_multi_ff(loc_count, loc_ids, query, result,
            vcpl_id, rcxt_ids)))
        HGOTO_ERROR(H5E_QUERY, H5E_CANTCOMPARE, FAIL, "unable to apply query");

    /* Setup VOL info */
    if(NULL == (vol_cls = (H5VL_class_t *)H5I_object_verify(native_vol, H5I_VOL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VOL plugin ID")
    if(NULL == (vol_info = H5FL_CALLOC(H5VL_t)))
        HGOTO_ERROR(H5E_FILE, H5E_NOSPACE, FAIL, "can't allocate VL info struct")
    vol_info->vol_cls = vol_cls;
    vol_info->vol_id = native_vol;
    if(H5I_inc_ref(vol_info->vol_id, FALSE) < 0)
        HGOTO_ERROR(H5E_ATOM, H5E_CANTINC, FAIL, "unable to increment ref count on VOL plugin")

    /* Get an atom for the group */
    if((ret_value = H5VL_register_id(H5I_GROUP, (void *)ret_grp, vol_info, TRUE)) < 0)
        HGOTO_ERROR(H5E_ATOM, H5E_CANTREGISTER, FAIL, "unable to atomize group handle")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Qapply_multi() */

/*-------------------------------------------------------------------------
 * Function:    H5Q_apply_multi
 *
 * Purpose: Private function for H5Qapply_multi.
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
H5G_t *
H5Q_apply_multi_ff(size_t loc_count, hid_t *loc_ids, const H5Q_t *query,
    unsigned *result, hid_t H5_ATTR_UNUSED vcpl_id, hid_t *rcxt_ids)
{
    H5Q_view_t view = H5Q_VIEW_INITIALIZER(view); /* Resulting view */
    H5Q_ref_head_t *refs[H5Q_VIEW_REF_NTYPES] = { &view.reg_refs, &view.obj_refs, &view.attr_refs };
    unsigned multi_result = 0;
    H5G_t *ret_grp = NULL; /* New group created */
    H5G_t *ret_value = NULL; /* Return value */
    H5P_genclass_t *pclass = NULL;
    unsigned flags;
    hid_t fapl_id = FAIL;
    H5F_t *new_file = NULL;
    H5G_loc_t file_loc;
    size_t i;

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(loc_count);
    HDassert(loc_ids);
    HDassert(query);
    HDassert(result);
    HDassert(rcxt_ids);

    /* TODO Serial iteration for now */
    for (i = 0; i < loc_count; i++) {
        H5Q_view_t loc_view = H5Q_VIEW_INITIALIZER(loc_view); /* Resulting view */
        H5Q_ref_head_t *loc_refs[H5Q_VIEW_REF_NTYPES] = { &loc_view.reg_refs, &loc_view.obj_refs, &loc_view.attr_refs };
        unsigned loc_result;
        H5Q_apply_arg_t args;
        int j;

        /* Create new view and init args */
        args.query = query;
        args.result = &loc_result;
        args.view = &loc_view;

        if (FAIL == H5Ovisit_ff(loc_ids[i], H5_INDEX_NAME, H5_ITER_NATIVE, H5Q__apply_iterate_ff,
                &args, rcxt_ids[i], H5_EVENT_STACK_NULL))
            HGOTO_ERROR(H5E_SYM, H5E_BADITER, NULL, "object visitation failed");

        multi_result |= loc_result;
        /* Simply concatenate results from sub-view */
        for (j = 0; j < H5Q_VIEW_REF_NTYPES; j++) {
            H5Q_QUEUE_CONCAT(refs[j], loc_refs[j]);
        }
    }

    if (!H5Q_QUEUE_EMPTY(&view.reg_refs))
        H5Q_LOG_DEBUG("Number of reg refs: %zu\n", view.reg_refs.n_elem);
    if (!H5Q_QUEUE_EMPTY(&view.obj_refs))
        H5Q_LOG_DEBUG("Number of obj refs: %zu\n", view.obj_refs.n_elem);
    if (!H5Q_QUEUE_EMPTY(&view.attr_refs))
        H5Q_LOG_DEBUG("Number of attr refs: %zu\n", view.attr_refs.n_elem);

    /* Get property list class */
    if (NULL == (pclass = (H5P_genclass_t *)H5I_object_verify(H5P_FILE_ACCESS, H5I_GENPROP_CLS)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, NULL, "not a property list class");

    /* Create the new property list */
    if (FAIL == (fapl_id = H5P_create_id(pclass, TRUE)))
        HGOTO_ERROR(H5E_PLIST, H5E_CANTCREATE, NULL, "unable to create property list");

    /* Use the core VFD to store the view */
    if (FAIL == H5Pset_fapl_core(fapl_id, H5Q_VIEW_CORE_INCREMENT, FALSE))
        HGOTO_ERROR(H5E_PLIST, H5E_CANTSET, NULL, "unable to set property list to core VFD");

    /* Create a new file or truncate an existing file. */
    flags = H5F_ACC_EXCL | H5F_ACC_RDWR | H5F_ACC_CREAT;
    if (NULL == (new_file = H5F_open("view", flags, H5P_FILE_CREATE_DEFAULT, fapl_id, H5AC_dxpl_id)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, NULL, "unable to create file");

    /* Construct a group location for root group of the file */
    if (FAIL == H5G_root_loc(new_file, &file_loc))
        HGOTO_ERROR(H5E_SYM, H5E_BADVALUE, NULL, "unable to create location for file")

    /* Create the new group & get its ID */
    if (NULL == (ret_grp = H5G_create_anon(&file_loc, H5P_GROUP_CREATE_DEFAULT, H5P_GROUP_ACCESS_DEFAULT)))
        HGOTO_ERROR(H5E_SYM, H5E_CANTINIT, NULL, "unable to create group");

    /* Write view */
    if (FAIL == H5Q__view_write(ret_grp, &view))
        HGOTO_ERROR(H5E_QUERY, H5E_WRITEERROR, NULL, "can't write view");

    *result = multi_result;
    ret_value = ret_grp;

done:
    /* Release the group's object header, if it was created */
    if (ret_grp) {
        H5O_loc_t *grp_oloc;         /* Object location for group */

        /* Get the new group's object location */
        if (NULL == (grp_oloc = H5G_oloc(ret_grp)))
            HDONE_ERROR(H5E_SYM, H5E_CANTGET, NULL, "unable to get object location of group");

        /* Decrement refcount on group's object header in memory */
        if (FAIL == H5O_dec_rc_by_loc(grp_oloc, H5AC_dxpl_id))
            HDONE_ERROR(H5E_SYM, H5E_CANTDEC, NULL, "unable to decrement refcount on newly created object");
    } /* end if */

    /* Cleanup on failure */
    if (NULL == ret_value)
        if (ret_grp && (FAIL == H5G_close(ret_grp)))
            HDONE_ERROR(H5E_SYM, H5E_CLOSEERROR, NULL, "unable to release group");

    /* Close the property list */
    if ((fapl_id != FAIL) && (H5I_dec_app_ref(fapl_id) < 0))
        HDONE_ERROR(H5E_PLIST, H5E_CANTFREE, NULL, "can't close");

    /* Attempt to close the file/mount hierarchy */
    if (new_file && (FAIL == H5F_try_close(new_file)))
        HDONE_ERROR(H5E_FILE, H5E_CANTCLOSEFILE, NULL, "can't close file")

    /* Free the view */
    H5Q__view_free(&view);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5Q_apply_multi() */
