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

/****************/
/* Module Setup */
/****************/

#include "H5CXmodule.h"  /* This source code file is part of the H5CX module */

/***********/
/* Headers */
/***********/
#include "H5private.h"   /* Generic Functions   */
#include "H5Eprivate.h"  /* Error handling      */
#include "H5Iprivate.h"  /* IDs                 */
#include "H5MMprivate.h" /* Memory management   */
#include "H5VLprivate.h" /* VOL plugins         */
#include "H5CXprivate.h" /* Contexts            */

/****************/
/* Local Macros */
/****************/

//#define H5CX_DEBUG

#ifdef H5CX_DEBUG
#define H5CX_LOG_DEBUG(...) do {                                \
      fprintf(stdout, "# %s:%d(): ",  __func__, __LINE__);      \
      fprintf(stdout, __VA_ARGS__);                             \
      fprintf(stdout, "\n");                                    \
      fflush(stdout);                                           \
  } while (0)
#else
#define H5CX_LOG_DEBUG(...) do { \
  } while (0)
#endif

#define H5CX_MAX_POLL_REQS 64 /* TODO probably sufficient */

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

/* Declare a free list to manage the H5CX_t struct */
H5FL_DEFINE(H5CX_t);
H5FL_DEFINE(H5CX_req_t);

/* Dataspace ID class */
static const H5I_class_t H5I_CX_CLS[1] = {{
    H5I_CONTEXT, /* ID class value */
    0,           /* Class flags */
    2,           /* # of reserved IDs for class */
    (H5I_free_t)H5CX_close /* Callback routine for closing objects of this class */
}};

/*-------------------------------------------------------------------------
 * Function:    H5CX_init
 *
 * Purpose:     Initialize the interface from some other package.
 *
 * Return:      Success:        non-negative
 *              Failure:        negative
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_init(void)
{
    herr_t ret_value = SUCCEED;   /* Return value */

    FUNC_ENTER_NOAPI(FAIL)
    /* FUNC_ENTER() does all the work */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_init() */

/*--------------------------------------------------------------------------
NAME
   H5CX__init_package -- Initialize interface-specific information
USAGE
    herr_t H5CX__init_package()

RETURNS
    Non-negative on success/Negative on failure
DESCRIPTION
    Initializes any interface-specific data or routines.

--------------------------------------------------------------------------*/
herr_t
H5CX__init_package(void)
{
    herr_t ret_value = SUCCEED;   /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Initialize the atom group for the TR IDs */
    if(H5I_register_type(H5I_CX_CLS) < 0)
        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINIT, FAIL, "unable to initialize interface");

    H5_PKG_INIT_VAR = TRUE;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX__init_package() */

/*--------------------------------------------------------------------------
 NAME
    H5CX_term_package
 PURPOSE
    Terminate various H5CX objects
 USAGE
    void H5CX_term_package()
 RETURNS
    Non-negative on success/Negative on failure
 DESCRIPTION
    Release the atom group and any other resources allocated.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
     Can't report errors...
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
int
H5CX_term_package(void)
{
    int	n = 0;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    if(H5_PKG_INIT_VAR) {
        if(H5I_nmembers(H5I_CONTEXT) > 0) {
            (void)H5I_clear_type(H5I_CONTEXT, FALSE, FALSE);
            n++; /*H5I*/
        } /* end if */
        else {
            n += (H5I_dec_type_ref(H5I_CONTEXT) > 0);

            /* Mark closed */
            if(0 == n)
                H5_PKG_INIT_VAR = FALSE;
        } /* end else */
    } /* end if */

    FUNC_LEAVE_NOAPI(n)
} /* end H5CX_term_package() */

/*-------------------------------------------------------------------------
 * Function:    H5CXcreate
 *
 * Purpose:     Wraps an hid_t around a transaction number, a file ID, 
 *              and a read context ID on that file that operations using 
 *              the created transaction will read from.
 *
 * Return:      Success:        The ID for a new transaction.
 *              Failure:        FAIL
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5CXcreate(void)
{
    H5CX_t *cx = NULL;
    hid_t ret_value;

    FUNC_ENTER_API(FAIL)

    /* create a new transaction object */
    if(NULL == (cx = H5CX_create(NULL)))
        HGOTO_ERROR(H5E_SYM, H5E_CANTCREATE, FAIL, "unable to create context");

    /* Get an atom for the context */
    if((ret_value = H5I_register(H5I_CONTEXT, cx, TRUE)) < 0)
        HGOTO_ERROR(H5E_ATOM, H5E_CANTREGISTER, FAIL, "unable to register context");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5CXcreate() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_create
 *
 * Purpose:     Private version for H5CXcreate.
 *
 * Return:      Success:        Context struct.
 *              Failure:        NULL
 *
 *-------------------------------------------------------------------------
 */
H5CX_t *
H5CX_create(const H5VL_class_t *vol_cls)
{
    H5CX_t *cx = NULL;
    H5CX_t *ret_value = NULL; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    H5CX_LOG_DEBUG("Here");

    /* allocate transaction struct */
    if(NULL == (cx = H5FL_CALLOC(H5CX_t)))
        HGOTO_ERROR(H5E_SYM, H5E_NOSPACE, NULL, "can't allocate context structure");

    cx->vol_cls = vol_cls;
    /* Don't check for error as this is only optional */
    cx->vol_context = (vol_cls) ? H5VL_context_create(vol_cls) : NULL;
    H5_QUEUE_INIT(&cx->pending_queue);
    H5_LIST_INIT(&cx->processing_list);

    /* set return value */
    ret_value = cx;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_create() */

/*-------------------------------------------------------------------------
 * Function:    H5CXclose
 *
 * Purpose:
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CXclose(hid_t cx_id)
{
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_API(FAIL)

    /* Check args */
    if(NULL == H5I_object_verify(cx_id, H5I_CONTEXT))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not an context ID");

    if(H5I_dec_app_ref(cx_id) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTRELEASE, FAIL, "unable to close context");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5CXclose() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_close
 *
 * Purpose:
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_close(H5CX_t *cx)
{
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    H5CX_LOG_DEBUG("Here");

    /* Call plugin close */
    if (cx->vol_context) {
        if (H5VL_context_close(cx->vol_cls, cx->vol_context) < 0)
            HGOTO_ERROR(H5E_VOL, H5E_CANTCLOSEOBJ, FAIL, "unable to close plugin context");
    }
    cx = H5FL_FREE(H5CX_t, cx);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_close() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_attach
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_attach(H5CX_t *context, const H5VL_class_t *vol_cls)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    HDassert(context);
    HDassert(vol_cls);

    H5CX_LOG_DEBUG("Here");

    context->vol_cls = vol_cls;
    /* Don't check for error as this is only optional */
    context->vol_context = H5VL_context_create(vol_cls);

    H5CX_LOG_DEBUG("Here: %x", context->vol_context);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_attach() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_request_create
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
H5CX_req_t *
H5CX_request_create(H5CX_t *context)
{
    H5CX_req_t *req = NULL;
    H5CX_req_t *ret_value = NULL; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    H5CX_LOG_DEBUG("Here");

    /* allocate transaction struct */
    if(NULL == (req = H5FL_CALLOC(H5CX_req_t)))
        HGOTO_ERROR(H5E_SYM, H5E_NOSPACE, NULL, "can't allocate request structure");

    /* Don't check for error as this is only optional */
    req->vol_req = H5VL_request_create(context->vol_cls, context->vol_context);
    H5CX_LOG_DEBUG("Vol request 0x%x inside request 0x%x", req->vol_req, req);
    req->context = context;

    /* set return value */
    ret_value = req;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_request_create() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_request_close
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_request_close(H5CX_req_t *req)
{
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    H5CX_LOG_DEBUG("Here");

    /* Call plugin close */
    if (H5VL_request_close(req->context->vol_cls, req->context->vol_context, req->vol_req) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTCLOSEOBJ, FAIL, "unable to close plugin request");
    req = H5FL_FREE(H5CX_req_t, req);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_request_close() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_request_insert_pending
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_request_insert_pending(H5CX_t *context, H5CX_req_t *req)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    HDassert(context);
    HDassert(req);

    H5CX_LOG_DEBUG("Inserting 0x%x into list", req);

    H5_QUEUE_PUSH_TAIL(&context->pending_queue, req, pending_entry);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_request_insert_pending() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_request_insert_processing
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_request_insert_processing(H5CX_t *context, H5CX_req_t *req)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    HDassert(context);
    HDassert(req);

    H5CX_LOG_DEBUG("Inserting 0x%x into list", req);

    H5_LIST_INSERT_HEAD(&context->processing_list, req, processing_entry);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_request_insert_processing() */

/*-------------------------------------------------------------------------
 * Function:    H5CXwait
 *
 * Purpose:
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CXwait(hid_t context_id, unsigned int timeout, hbool_t *flag)
{
    H5CX_t *context;
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_API(FAIL)

    /* Check args */
    if(NULL == (context = (H5CX_t *) H5I_object_verify(context_id, H5I_CONTEXT)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a context ID");

    if(FAIL == H5CX_wait(context, timeout, flag))
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "can't wait on context");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5CXwait() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_wait
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_wait(H5CX_t *context, unsigned int timeout, hbool_t *flag)
{
    herr_t ret_value = SUCCEED; /* Return value */
    double remaining = timeout / 1000.0; /* Convert timeout in ms into seconds */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(context);

    /* Check whether list is empty or not */
    while(!H5_LIST_IS_EMPTY(&context->processing_list)) {
        H5CX_req_t *pending_req, *first_pending_req;
        void *reqs[H5CX_MAX_POLL_REQS];
        int nreqs, i;
        double t1, t2;

        t1 = MPI_Wtime();

        /* Progress until context queue is empty */
        if((nreqs = H5VL_context_poll(context->vol_cls, context->vol_context,
            (unsigned int) (remaining * 1000.0), H5CX_MAX_POLL_REQS, reqs)) < 0)
            HGOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "unable to poll context");
        for(i = 0; i < nreqs; i++) {
            H5CX_req_t *req = container_of(reqs[i], H5CX_req_t, vol_req);
            HDassert(&req->vol_req == reqs[i]);

            H5CX_LOG_DEBUG("Completed request %d/%d (0x%x)", i+1, nreqs, reqs[i]);
            H5CX_LOG_DEBUG("Vol request 0x%x inside request 0x%0x", req->vol_req, req);

            /* Remove entry from list */
            H5_LIST_REMOVE(req, processing_entry);

            /* Decrement nreqs */
            req->obj->nreqs--;
            H5CX_LOG_DEBUG("Nreqs on obj: %u", req->obj->nreqs);

            H5CX_LOG_DEBUG("Executing request callback");
            if(req->callback && (FAIL == req->callback(req)))
                HGOTO_ERROR(H5E_CONTEXT, H5E_CANTOPERATE, FAIL, "cannot execute callback");
            if(FAIL == H5CX_request_close(req))
                HGOTO_ERROR(H5E_CONTEXT, H5E_CANTFREE, FAIL, "unable to free request");
        }

        first_pending_req = H5_QUEUE_FIRST(&context->pending_queue);
        pending_req = first_pending_req;
        while(pending_req) {
            H5CX_req_t *next_req = H5_QUEUE_NEXT(pending_req, pending_entry);
            hbool_t execute = FALSE;

            switch(pending_req->req_type) {
                case H5CX_DATASET_CREATE:
                    if(!pending_req->obj->parent_obj->nreqs ||
                        pending_req == first_pending_req) {
                        execute = TRUE;
                    }
                    break;
                case H5CX_FILE_FLUSH:
                case H5CX_DATASET_READ:
                case H5CX_DATASET_WRITE:
                case H5CX_DATASET_CLOSE:
                case H5CX_FILE_CLOSE:
                    if(!pending_req->obj->nreqs ||
                        pending_req == first_pending_req) {
                        execute = TRUE;
                    }
                    break;
                default:
                    break;
            }

            if(execute) {
                /* Remove entry from list */
                H5_QUEUE_REMOVE(&context->pending_queue, pending_req, H5CX_req_t, pending_entry);

                /* Add request to context */
                if(FAIL == H5CX_request_insert_processing(context, pending_req))
                    HGOTO_ERROR(H5E_CONTEXT, H5E_CANTINSERT, FAIL, "cannot insert request");

                H5CX_LOG_DEBUG("Submitting pending request");
                if(pending_req->callback && (FAIL == pending_req->callback(pending_req)))
                    HGOTO_ERROR(H5E_CONTEXT, H5E_CANTOPERATE, FAIL, "cannot execute callback");
            }
            pending_req = next_req;
        }

        t2 = MPI_Wtime();
        remaining -= (t2 - t1);
        if(remaining < 0)
            break;
    }

    if(flag)
        *flag = H5_LIST_IS_EMPTY(&context->processing_list);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_wait() */

/*-------------------------------------------------------------------------
 * Function:    H5CXcancel
 *
 * Purpose:
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CXcancel(hid_t context_id)
{
    H5CX_t *context;
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_API(FAIL)

    /* Check args */
    if(NULL == (context = (H5CX_t *) H5I_object_verify(context_id, H5I_CONTEXT)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a context ID");

    if(FAIL == H5CX_cancel(context))
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "can't wait on context");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5CXcancel() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_cancel
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_cancel(H5CX_t *context)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(context);

    /* Check whether list is empty or not */
    while(!H5_LIST_IS_EMPTY(&context->processing_list)) {
        H5CX_req_t *req = H5_LIST_FIRST(&context->processing_list);

        /* Cancel request */
        if(FAIL == H5VL_request_cancel(context->vol_cls, context->vol_context,
            req->vol_req))
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTCANCEL, FAIL, "unable to cancel plugin request");

        /* Remove request */
        H5_LIST_REMOVE(req, processing_entry);
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_cancel() */
