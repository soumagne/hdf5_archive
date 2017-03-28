/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
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
 * Purpose:	The DAOS VOL plugin where access is forwarded to the DAOS library
 */

/***********/
/* Headers */
/***********/

#include "H5private.h"          /* Generic Functions                    */
#include "H5Eprivate.h"         /* Error handling                       */
#include "H5Fprivate.h"         /* Files                                */
#include "H5FDprivate.h"        /* File drivers                         */
#include "H5Iprivate.h"         /* IDs                                  */
#include "H5MMprivate.h"        /* Memory management                    */
#include "H5Pprivate.h"         /* Property lists                       */
#include "H5Sprivate.h"         /* Dataspaces                           */
#include "H5VLprivate.h"        /* VOL plugins                          */
#include "H5CXprivate.h"        /* Contexts                             */
#include "H5VLdaos.h"           /* DAOS plugin                          */

#include <daos.h>
#include <daos_event.h>
#include <uuid/uuid.h>

/****************/
/* Local Macros */
/****************/

#define H5VL_DAOS_DEBUG

#ifdef H5VL_DAOS_DEBUG
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KNRM  "\x1B[0m"
static hid_t H5VL_DAOS_rank_g = 0;
#define H5VL_DAOS_LOG_DEBUG(...) do {                               \
        const char *color = (H5VL_DAOS_rank_g) ? KGRN : "";         \
        fprintf(stdout, "%s# (%lu):%s():%d: " KNRM, color, H5VL_DAOS_rank_g, __func__, __LINE__); \
        fprintf(stdout, __VA_ARGS__);                               \
        fprintf(stdout, "\n");                                      \
        fflush(stdout);                                             \
    } while (0)

#define H5VL_DAOS_TYPE_TO_STRING(def, value) \
  if(value == def) return #def

#else
#define H5VL_DAOS_LOG_DEBUG(...) do { \
  } while (0)
#endif

/* Constant Keys */
#define H5VL_DAOS_INT_MD_KEY "Internal Metadata"
#define H5VL_DAOS_MAX_OID_KEY "Max OID"
#define H5VL_DAOS_CPL_KEY "Creation Property List"
#define H5VL_DAOS_LINK_KEY "Link"
#define H5VL_DAOS_TYPE_KEY "Datatype"
#define H5VL_DAOS_SPACE_KEY "Dataspace"
#define H5VL_DAOS_ATTR_KEY "Attribute"
#define H5VL_DAOS_CHUNK_KEY 0u

/* Stack allocation sizes */
#define H5VL_DAOS_LINK_VAL_BUF_SIZE 256
#define H5VL_DAOS_SEQ_LIST_LEN 128

#define H5VL_DAOS_MAX_EVENTS 64 /* TODO probably sufficient */

/******************/
/* Local Typedefs */
/******************/

/* DAOS-specific file access properties */
typedef struct H5VL_daos_fapl_t {
    MPI_Comm            comm;           /*communicator                  */
    MPI_Info            info;           /*file information              */
    char                *pool_uuid;     /*pool uuid                     */
    char                *pool_grp;      /*pool group                    */
} H5VL_daos_fapl_t;

/* Common object and attribute information */
typedef struct H5VL_daos_item_t {
    H5I_type_t type;
    struct H5VL_daos_file_t *file;
    int rc;
} H5VL_daos_item_t;

/* Common object information */
typedef struct H5VL_daos_obj_t {
    H5VL_daos_item_t item; /* Must be first */
    daos_obj_id_t oid;
    daos_handle_t obj_oh;
} H5VL_daos_obj_t;

/* The file struct */
typedef struct H5VL_daos_file_t {
    H5VL_daos_item_t item; /* Must be first */
    daos_handle_t poh;
    daos_handle_t coh;
    daos_epoch_t epoch;
    int snap_epoch;
    char *file_name;
    uuid_t uuid;
    unsigned flags;
    daos_handle_t glob_md_oh;
    struct H5VL_daos_group_t *root_grp;
    uint64_t max_oid;
    hbool_t max_oid_dirty;
    hid_t fcpl_id;
    hid_t fapl_id;
    MPI_Comm comm;
    MPI_Info info;
    int my_rank;
    int num_procs;
} H5VL_daos_file_t;

/* The group struct */
typedef struct H5VL_daos_group_t {
    H5VL_daos_obj_t obj; /* Must be first */
    hid_t gcpl_id;
    hid_t gapl_id;
} H5VL_daos_group_t;

/* The dataset struct */
typedef struct H5VL_daos_dset_t {
    H5VL_daos_obj_t obj; /* Must be first */
    hid_t type_id;
    hid_t space_id;
    hid_t dcpl_id;
    hid_t dapl_id;
} H5VL_daos_dset_t;

/* The link value struct */
typedef struct H5VL_daos_link_val_t {
    H5L_type_t type;
    union {
        daos_obj_id_t hard;
        char *soft;
    } target;
} H5VL_daos_link_val_t;

struct H5VL_daos_req_pool_connect {
    H5VL_daos_file_t *file;
    H5VL_daos_fapl_t *fa;
    MPI_Request ghdl_size_req;
    MPI_Request ghdl_req;
    daos_iov_t  ghdl;
};

struct H5VL_daos_req_cont_open {
    H5VL_daos_file_t *file;
    MPI_Request ghdl_size_req;
    MPI_Request ghdl_req;
    daos_iov_t  ghdl;
    daos_epoch_state_t state;
    MPI_Request epoch_req;
    daos_obj_id_t gmd_oid;
    daos_recx_t root_grp_recx;
    daos_iov_t root_grp_sg_iov;
    daos_key_t root_grp_dkey;
    daos_vec_iod_t root_grp_iod;
};

struct H5VL_daos_req_cont_flush {
    H5VL_daos_file_t *file;
    MPI_Request barrier_req;
};

struct H5VL_daos_req_cont_close {
    H5VL_daos_file_t *file;
    MPI_Request barrier_req;
};

struct H5VL_daos_req_dset_open {
    H5VL_daos_item_t *item;
    H5VL_daos_dset_t *dset;
    H5VL_daos_group_t *target_grp;
    char *target_name;
    void *type_buf;
    void *space_buf;
    void *dcpl_buf;
};

struct H5VL_daos_req_dset_rw {
    daos_recx_t recx;
    daos_recx_t *recxs;
    daos_iov_t sg_iov;
    daos_iov_t *sg_iovs;
};

struct H5VL_daos_req_dset_close {
    H5VL_daos_dset_t *dset;
};

typedef enum H5VL_daos_req_type_t {
    H5VL_DAOS_FILE_CREATE = 1,
    H5VL_DAOS_FILE_OPEN,
    H5VL_DAOS_FILE_FLUSH,
    H5VL_DAOS_FILE_CLOSE,
    H5VL_DAOS_DATASET_CREATE,
    H5VL_DAOS_DATASET_OPEN,
    H5VL_DAOS_DATASET_READ,
    H5VL_DAOS_DATASET_WRITE,
    H5VL_DAOS_DATASET_CLOSE
} H5VL_daos_req_type_t;

typedef enum H5VL_daos_op_type_t {
    H5VL_DAOS_POOL_CONNECT = 1,
    H5VL_DAOS_POOL_CONNECT_SEND_GHDL,
    H5VL_DAOS_POOL_CONNECT_RECV_GHDL_SIZE,
    H5VL_DAOS_POOL_CONNECT_RECV_GHDL,
    H5VL_DAOS_POOL_DISCONNECT,
    H5VL_DAOS_CONT_DESTROY,
    H5VL_DAOS_CONT_CREATE,
    H5VL_DAOS_CONT_OPEN,
    H5VL_DAOS_CONT_OPEN_SEND_GHDL,
    H5VL_DAOS_CONT_OPEN_RECV_GHDL_SIZE,
    H5VL_DAOS_CONT_OPEN_RECV_GHDL,
    H5VL_DAOS_CONT_CLOSE,
    H5VL_DAOS_CONT_FLUSH,
    H5VL_DAOS_BARRIER,
    H5VL_DAOS_EPOCH_QUERY,
    H5VL_DAOS_EPOCH_HOLD,
    H5VL_DAOS_EPOCH_BCAST,
    H5VL_DAOS_EPOCH_COMMIT,
    H5VL_DAOS_METADATA_CREATE,
    H5VL_DAOS_METADATA_OPEN,
    H5VL_DAOS_METADATA_CLOSE,
    H5VL_DAOS_GROUP_CREATE,
    H5VL_DAOS_GROUP_OPEN,
    H5VL_DAOS_GROUP_CLOSE,
    H5VL_DAOS_GROUP_UPDATE_METADATA,
    H5VL_DAOS_GROUP_FETCH_METADATA_SIZE,
    H5VL_DAOS_GROUP_FETCH_METADATA,
    H5VL_DAOS_LINK_CREATE,
    H5VL_DAOS_DSET_CREATE,
    H5VL_DAOS_DSET_OPEN,
    H5VL_DAOS_DSET_CLOSE,
    H5VL_DAOS_DSET_UPDATE_METADATA,
    H5VL_DAOS_DSET_FETCH_METADATA,
    H5VL_DAOS_DSET_READ,
    H5VL_DAOS_DSET_WRITE
} H5VL_daos_op_type_t;

/* Request struct */
typedef struct H5VL_daos_req_t {
    void *reqp;
    struct H5VL_daos_context_t *context;
    hbool_t completed; /* Operation completed */
    hbool_t canceled;  /* Operation canceled */
    H5VL_daos_req_type_t req_type;
    H5VL_daos_op_type_t  op_type;
    herr_t (*cb)(struct H5VL_daos_req_t *arg); /* Callback */
    daos_event_t ev;
    union {
        struct H5VL_daos_req_pool_connect   pool_connect;
        struct H5VL_daos_req_cont_open      cont_open;
        struct H5VL_daos_req_cont_flush     cont_flush;
        struct H5VL_daos_req_cont_close     cont_close;
        struct H5VL_daos_req_dset_open      dset_open;
        struct H5VL_daos_req_dset_rw        dset_rw;
        struct H5VL_daos_req_dset_close     dset_close;
    } req;
    H5_LIST_ENTRY(H5VL_daos_req_t) entry;
} H5VL_daos_req_t;

typedef herr_t (*H5VL_daos_req_cb_t)(H5VL_daos_req_t *arg);

typedef struct H5VL_daos_context_t {
    daos_handle_t eq_handle;
    H5_LIST_HEAD(H5VL_daos_req_t) mpi_req_list;
} H5VL_daos_context_t;


/********************/
/* Local Prototypes */
/********************/

#ifdef H5VL_DAOS_LOG_DEBUG
static const char *
H5VL__daos_op_type_str(H5VL_daos_op_type_t op_type) {
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_POOL_CONNECT, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_POOL_CONNECT_SEND_GHDL, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_POOL_CONNECT_RECV_GHDL_SIZE, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_POOL_CONNECT_RECV_GHDL, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_POOL_DISCONNECT, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_CONT_DESTROY, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_CONT_CREATE, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_CONT_OPEN, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_CONT_OPEN_SEND_GHDL, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_CONT_OPEN_RECV_GHDL_SIZE, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_CONT_OPEN_RECV_GHDL, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_CONT_CLOSE, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_CONT_FLUSH, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_BARRIER, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_EPOCH_QUERY, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_EPOCH_HOLD, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_EPOCH_BCAST, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_EPOCH_COMMIT, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_METADATA_CREATE, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_METADATA_OPEN, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_METADATA_CLOSE, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_GROUP_CREATE, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_GROUP_OPEN, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_GROUP_CLOSE, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_GROUP_UPDATE_METADATA, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_GROUP_FETCH_METADATA, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_GROUP_FETCH_METADATA_SIZE, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_LINK_CREATE, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_DSET_CREATE, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_DSET_OPEN, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_DSET_CLOSE, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_DSET_UPDATE_METADATA, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_DSET_FETCH_METADATA, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_DSET_READ, op_type);
    H5VL_DAOS_TYPE_TO_STRING(H5VL_DAOS_DSET_WRITE, op_type);
    return "TAG UNDEFINED/UNRECOGNIZED";
}
#endif

static void H5VL__daos_mult128(uint64_t x_lo, uint64_t x_hi, uint64_t y_lo,
    uint64_t y_hi, uint64_t *ans_lo, uint64_t *ans_hi);
static void H5VL__daos_hash128(const char *name, void *hash);
static H5VL_daos_file_t *H5VL__daos_file_init(const char *name, unsigned flags,
   H5VL_daos_fapl_t *fa);
static herr_t H5VL__daos_file_flush(H5VL_daos_req_t *req);
static herr_t H5VL__daos_file_flush_cb(H5VL_daos_req_t *req);
static herr_t H5VL__daos_file_close(H5VL_daos_req_t *req);
static herr_t H5VL__daos_file_free(H5VL_daos_req_t *req);
static herr_t H5VL__daos_pool_connect(H5VL_daos_req_t *req);
static herr_t H5VL__daos_pool_connect_send_ghdl(H5VL_daos_req_t *req);
static herr_t H5VL__daos_pool_connect_send_ghdl_cb(H5VL_daos_req_t *req);
static herr_t H5VL__daos_pool_connect_recv_ghdl(H5VL_daos_req_t *req);
static herr_t H5VL__daos_pool_connect_recv_ghdl_cb(H5VL_daos_req_t *req);
static herr_t H5VL__daos_pool_disconnect(H5VL_daos_req_t *req);

static herr_t H5VL__daos_cont_create(H5VL_daos_req_t *req);
static herr_t H5VL__daos_cont_destroy(H5VL_daos_req_t *req);
static herr_t H5VL__daos_cont_open(H5VL_daos_req_t *req);
static herr_t H5VL__daos_cont_open_send_ghdl(H5VL_daos_req_t *req);
static herr_t H5VL__daos_cont_open_send_ghdl_cb(H5VL_daos_req_t *req);
static herr_t H5VL__daos_cont_open_recv_ghdl(H5VL_daos_req_t *req);
static herr_t H5VL__daos_cont_open_recv_ghdl_cb(H5VL_daos_req_t *req);
static herr_t H5VL__daos_cont_close(H5VL_daos_req_t *req);
static herr_t H5VL__daos_epoch_query(H5VL_daos_req_t *req);
static herr_t H5VL__daos_epoch_hold(H5VL_daos_req_t *req);
static herr_t H5VL__daos_epoch_bcast(H5VL_daos_req_t *req);
static herr_t H5VL__daos_epoch_commit(H5VL_daos_req_t *req);
static herr_t H5VL__daos_metadata_create(H5VL_daos_req_t *req);
static herr_t H5VL__daos_metadata_open(H5VL_daos_req_t *req);
static herr_t H5VL__daos_metadata_open_cb(H5VL_daos_req_t *req);
static herr_t H5VL__daos_metadata_close(H5VL_daos_req_t *req);

static herr_t H5VL__daos_write_max_oid(H5VL_daos_file_t *file, H5VL_daos_req_t *req);

static H5VL_daos_group_t *H5VL__daos_group_init(H5VL_daos_file_t *file);
static herr_t H5VL__daos_group_create(H5VL_daos_req_t *req);
static herr_t H5VL__daos_group_open(H5VL_daos_req_t *req);
static herr_t H5VL__daos_group_close(H5VL_daos_req_t *req);
static herr_t H5VL__daos_group_free(H5VL_daos_req_t *req);
static herr_t H5VL__daos_group_update_metadata(H5VL_daos_req_t *req);
static herr_t H5VL__daos_group_update_metadata_cb(H5VL_daos_req_t *req);
static herr_t H5VL__daos_group_fetch_metadata_size(H5VL_daos_req_t *req);
static herr_t H5VL__daos_group_fetch_metadata(H5VL_daos_req_t *req);
static herr_t H5VL__daos_group_fetch_metadata_cb(H5VL_daos_req_t *req);

static H5VL_daos_dset_t *H5VL__daos_dset_init(H5VL_daos_item_t *item);
static herr_t H5VL__daos_dset_create(H5VL_daos_req_t *req);
static herr_t H5VL__daos_link_create(H5VL_daos_req_t *req);
static herr_t H5VL__daos_link_write(H5VL_daos_group_t *grp, char *name,
    size_t name_len, H5VL_daos_link_val_t *val, H5VL_daos_req_t *req);
static herr_t H5VL__daos_dset_open(H5VL_daos_req_t *req);
static herr_t H5VL__daos_dset_update_metadata(H5VL_daos_req_t *req);
static herr_t H5VL__daos_dset_update_metadata_cb(H5VL_daos_req_t *req);
static herr_t H5VL__daos_sel_to_recx_iov(H5S_t *space, size_t type_size,
    void *buf, daos_recx_t **recxs, daos_iov_t **sg_iovs, size_t *list_nused);
static herr_t H5VL__daos_dset_rw_cb(H5VL_daos_req_t *req);
static herr_t H5VL__daos_dset_free(H5VL_daos_req_t *req);

static int H5VL__daos_context_poll_mpi(H5VL_daos_context_t *context,
    unsigned int timeout, unsigned int max_reqs, H5VL_daos_req_t **reqs);
static int H5VL__daos_context_poll_events(H5VL_daos_context_t *context,
    unsigned int timeout, unsigned int max_reqs, H5VL_daos_req_t **reqs);

/* FAPL callbacks */
static void *H5VL_daos_fapl_copy(const void *_old_fa);
static herr_t H5VL_daos_fapl_free(void *_fa);

/* File callbacks */
static void *H5VL_daos_file_create(const char *name, unsigned flags,
    hid_t fcpl_id, hid_t fapl_id, hid_t dxpl_id, void **req);
static void *H5VL_daos_file_open(const char *name, unsigned flags,
    hid_t fapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_daos_file_specific(void *_item,
    H5VL_file_specific_t specific_type, hid_t dxpl_id, void **req,
    va_list arguments);
static herr_t H5VL_daos_file_close(void *_file, hid_t dxpl_id, void **req);

/* Dataset callbacks */
static void *H5VL_daos_dataset_create(void *_item,
    H5VL_loc_params_t loc_params, const char *name, hid_t dcpl_id,
    hid_t dapl_id, hid_t dxpl_id, void **req);
static void *H5VL_daos_dataset_open(void *_item, H5VL_loc_params_t loc_params,
    const char *name, hid_t dapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_daos_dataset_read(void *_dset, hid_t mem_type_id,
    hid_t mem_space_id, hid_t file_space_id, hid_t dxpl_id, void *buf,
    void **req);
static herr_t H5VL_daos_dataset_write(void *_dset, hid_t mem_type_id,
    hid_t mem_space_id, hid_t file_space_id, hid_t dxpl_id, const void *buf,
    void **req);
static herr_t H5VL_daos_dataset_get(void *_dset, H5VL_dataset_get_t get_type,
    hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_daos_dataset_close(void *_dset, hid_t dxpl_id, void **req);

/* Context callbacks */
static void *H5VL_daos_context_create(void);
static herr_t H5VL_daos_context_close(void *context);
static void *H5VL_daos_request_create(void *context);
static herr_t H5VL_daos_request_close(void *context, void *req);
static int H5VL_daos_context_poll(void *context, unsigned int timeout,
    unsigned int max_reqs, void **reqs);
static herr_t H5VL_daos_request_cancel(void *context, void *req);

/*******************/
/* Local Variables */
/*******************/

/*
 * The vol identification number.
 */
static hid_t H5VL_DAOS_g = 0;

/* Free list definitions */
H5FL_DEFINE(H5VL_daos_file_t);
H5FL_DEFINE(H5VL_daos_group_t);
H5FL_DEFINE(H5VL_daos_dset_t);
H5FL_DEFINE(H5VL_daos_req_t);

/* The DAOS VOL plugin struct */
static H5VL_class_t H5VL_daos_g = {
    HDF5_VOL_DAOS_VERSION_1,                    /* Version number */
    H5_VOL_DAOS,                                /* Plugin value */
    "daos",                                     /* Plugin name  */
    NULL,                                       /* initialize   */
    NULL,                                       /* terminate    */
    sizeof(H5VL_daos_fapl_t),                   /* fapl_size    */
    H5VL_daos_fapl_copy,                        /* fapl_copy    */
    H5VL_daos_fapl_free,                        /* fapl_free    */
    {                                           /* attribute_cls */
        NULL, /* create */
        NULL, /* open */
        NULL, /* read */
        NULL, /* write */
        NULL, /* get */
        NULL, /* specific */
        NULL, /* optional */
        NULL /* close */
    },
    {                                           /* dataset_cls  */
        H5VL_daos_dataset_create,               /* create       */
        H5VL_daos_dataset_open,                 /* open         */
        H5VL_daos_dataset_read,                 /* read         */
        H5VL_daos_dataset_write,                /* write        */
        H5VL_daos_dataset_get,                  /* get          */
        NULL,                                   /* specific     */
        NULL,                                   /* optional     */
        H5VL_daos_dataset_close                 /* close        */
    },
    {                                           /* datatype_cls */
        NULL, /* commit */
        NULL, /* open */
        NULL, /* get */
        NULL, /* specific */
        NULL, /* optional */
        NULL /* close */
    },
    {                                           /* file_cls     */
        H5VL_daos_file_create,                  /* create       */
        H5VL_daos_file_open,                    /* open         */
        NULL,                                   /* get          */
        H5VL_daos_file_specific,                /* specific     */
        NULL,                                   /* optional     */
        H5VL_daos_file_close                    /* close        */
    },
    {                                           /* group_cls    */
        NULL, /* create */
        NULL, /* open */
        NULL, /* get */
        NULL, /* specific */
        NULL, /* optional */
        NULL /* close */
    },
    {                                           /* link_cls     */
        NULL, /* create */
        NULL, /* copy */
        NULL, /* move */
        NULL, /* get */
        NULL, /* specific */
        NULL /* optional */
    },
    {                                           /* object_cls   */
        NULL, /* open */
        NULL, /* copy */
        NULL, /* get */
        NULL, /* specific */
        NULL /* optional */
    },
    {                                           /* context_cls      */
        H5VL_daos_context_create,               /* create           */
        H5VL_daos_context_close,                /* close            */
        H5VL_daos_request_create,               /* request_create   */
        H5VL_daos_request_close,                /* request_close    */
        H5VL_daos_context_poll,                 /* poll             */
        H5VL_daos_request_cancel                /* request_cancel   */
    },
    NULL
};

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_mult128
 *
 * Purpose:     Multiply two 128 bit unsigned integers to yield a 128 bit
 *              unsigned integer.
 *
 *-------------------------------------------------------------------------
 */
static void
H5VL__daos_mult128(uint64_t x_lo, uint64_t x_hi, uint64_t y_lo, uint64_t y_hi,
    uint64_t *ans_lo, uint64_t *ans_hi)
{
    uint64_t xlyl;
    uint64_t xlyh;
    uint64_t xhyl;
    uint64_t xhyh;
    uint64_t temp;

    /*
     * First calculate x_lo * y_lo
     */
    /* Compute 64 bit results of multiplication of each combination of high and
     * low 32 bit sections of x_lo and y_lo */
    xlyl = (x_lo & 0xffffffff) * (y_lo & 0xffffffff);
    xlyh = (x_lo & 0xffffffff) * (y_lo >> 32);
    xhyl = (x_lo >> 32) * (y_lo & 0xffffffff);
    xhyh = (x_lo >> 32) * (y_lo >> 32);

    /* Calculate lower 32 bits of the answer */
    *ans_lo = xlyl & 0xffffffff;

    /* Calculate second 32 bits of the answer. Use temp to keep a 64 bit result
     * of the calculation for these 32 bits, to keep track of overflow past
     * these 32 bits. */
    temp = (xlyl >> 32) + (xlyh & 0xffffffff) + (xhyl & 0xffffffff);
    *ans_lo += temp << 32;

    /* Calculate third 32 bits of the answer, including overflowed result from
     * the previous operation */
    temp >>= 32;
    temp += (xlyh >> 32) + (xhyl >> 32) + (xhyh & 0xffffffff);
    *ans_hi = temp & 0xffffffff;

    /* Calculate highest 32 bits of the answer. No need to keep track of
     * overflow because it has overflowed past the end of the 128 bit answer */
    temp >>= 32;
    temp += (xhyh >> 32);
    *ans_hi += temp << 32;

    /*
     * Now add the results from multiplying x_lo * y_hi and x_hi * y_lo. No need
     * to consider overflow here, and no need to consider x_hi * y_hi because
     * those results would overflow past the end of the 128 bit answer.
     */
    *ans_hi += (x_lo * y_hi) + (x_hi * y_lo);

    return;
} /* end H5VL__daos_mult128() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_hash128
 *
 * Purpose:     Implementation of the FNV hash algorithm.
 *
 *-------------------------------------------------------------------------
 */
static void
H5VL__daos_hash128(const char *name, void *hash)
{
    const uint8_t *name_p = (const uint8_t *)name;
    uint8_t *hash_p = (uint8_t *)hash;
    uint64_t name_lo;
    uint64_t name_hi;
    /* Initialize hash value in accordance with the FNV algorithm */
    uint64_t hash_lo = 0x62b821756295c58d;
    uint64_t hash_hi = 0x6c62272e07bb0142;
    /* Initialize FNV prime number in accordance with the FNV algorithm */
    const uint64_t fnv_prime_lo = 0x13b;
    const uint64_t fnv_prime_hi = 0x1000000;
    size_t name_len_rem = HDstrlen(name);

    while(name_len_rem > 0) {
        /* "Decode" lower 64 bits of this 128 bit section of the name, so the
         * numberical value of the integer is the same on both little endian and
         * big endian systems */
        if(name_len_rem >= 8) {
            UINT64DECODE(name_p, name_lo)
            name_len_rem -= 8;
        } /* end if */
        else {
            name_lo = 0;
            UINT64DECODE_VAR(name_p, name_lo, name_len_rem)
            name_len_rem = 0;
        } /* end else */

        /* "Decode" second 64 bits */
        if(name_len_rem > 0) {
            if(name_len_rem >= 8) {
                UINT64DECODE(name_p, name_hi)
                name_len_rem -= 8;
            } /* end if */
            else {
                name_hi = 0;
                UINT64DECODE_VAR(name_p, name_hi, name_len_rem)
                name_len_rem = 0;
            } /* end else */
        } /* end if */
        else
            name_hi = 0;

        /* FNV algorithm - XOR hash with name then multiply by fnv_prime */
        hash_lo ^= name_lo;
        hash_hi ^= name_hi;
        H5VL__daos_mult128(hash_lo, hash_hi, fnv_prime_lo, fnv_prime_hi, &hash_lo, &hash_hi);
    } /* end while */

    /* "Encode" hash integers to char buffer, so the buffer is the same on both
     * little endian and big endian systems */
    UINT64ENCODE(hash_p, hash_lo)
    UINT64ENCODE(hash_p, hash_hi)

    return;
} /* end H5VL__daos_hash128() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_file_init
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static H5VL_daos_file_t *
H5VL__daos_file_init(const char *name, unsigned flags, H5VL_daos_fapl_t *fa)
{
    H5VL_daos_file_t *file = NULL;
    H5VL_daos_file_t *ret_value = NULL;

    FUNC_ENTER_NOAPI_NOINIT

    /* allocate the file object that is returned to the user */
    if(NULL == (file = H5FL_CALLOC(H5VL_daos_file_t)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate DAOS file struct");
    file->glob_md_oh.cookie = 0;
    file->root_grp = NULL;
    file->fcpl_id = FAIL;
    file->fapl_id = FAIL;

    /* Fill in fields of file we know */
    file->item.type = H5I_FILE;
    file->item.file = file;
    file->item.rc = 1;
    file->snap_epoch = (int)FALSE;
    if(NULL == (file->file_name = HDstrdup(name)))
        HGOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, NULL, "can't copy file name");
    file->flags = flags;
    file->max_oid = 0;
    file->max_oid_dirty = FALSE;

    /* Duplicate communicator and Info object. */
    if(FAIL == H5FD_mpi_comm_info_dup(fa->comm, fa->info, &file->comm, &file->info))
        HGOTO_ERROR(H5E_INTERNAL, H5E_CANTCOPY, NULL, "Communicator/Info duplicate failed");

    /* Obtain the process rank and size from the communicator attached to the
     * fapl ID */
    MPI_Comm_rank(fa->comm, &file->my_rank);
#ifdef H5VL_DAOS_DEBUG
    H5VL_DAOS_rank_g = file->my_rank;
#endif
    MPI_Comm_size(fa->comm, &file->num_procs);
    H5VL_DAOS_LOG_DEBUG("My rank is %d", file->my_rank);

    /* Hash file name to create uuid */
    H5VL__daos_hash128(name, &file->uuid);

    ret_value = file;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_file_init() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_daos_file_flush
 *
 * Purpose:     Flushes a DAOS file.
 *
 * Return:      Success:        0
 *              Failure:        -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_file_flush(H5VL_daos_req_t *req)
{
    H5VL_daos_file_t *file = NULL;
    MPI_Request *barrier_reqp = NULL;
    herr_t ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_BARRIER;

    switch(req->req_type) {
        case H5VL_DAOS_FILE_FLUSH: {
            struct H5VL_daos_req_cont_flush *req_cont_flush = &req->req.cont_flush;
            file = req_cont_flush->file;
            barrier_reqp = &req_cont_flush->barrier_req;

            /* Next callback */
//            req->cb = H5VL__daos_file_flush_cb;
            req->cb = H5VL__daos_epoch_commit;
        }
            break;
        case H5VL_DAOS_FILE_CLOSE: {
            struct H5VL_daos_req_cont_close *req_cont_close = &req->req.cont_close;
            file = req_cont_close->file;
            barrier_reqp = &req_cont_close->barrier_req;

            /* Next callback */
            req->cb = H5VL__daos_epoch_commit;
        }
            break;
        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

    /* Insert request into MPI request list */
    H5_LIST_INSERT_HEAD(&req->context->mpi_req_list, req, entry);

    /* Barrier on all ranks so we don't commit before all ranks are
     * finished writing. H5Fflush must be called collectively. */
    if(MPI_SUCCESS != MPI_Ibarrier(file->comm, barrier_reqp))
        HGOTO_ERROR(H5E_FILE, H5E_MPI, FAIL, "MPI_Ibarrier failed")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_daos_file_flush() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_file_flush_cb
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_file_flush_cb(H5VL_daos_req_t *req)
{
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    H5VL_DAOS_LOG_DEBUG("Here");

    req->completed = TRUE;

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_file_flush_cb() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_file_close
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_file_close(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_cont_close *req_cont_close = &req->req.cont_close;
    H5VL_daos_file_t *file = req_cont_close->file;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    HDassert(file);

    /* Free file data structures */
    if(file->file_name)
        HDfree(file->file_name);
    if(file->comm || file->info)
        if(H5FD_mpi_comm_info_free(&file->comm, &file->info) < 0)
            HGOTO_ERROR(H5E_INTERNAL, H5E_CANTFREE, FAIL, "Communicator/Info free failed");

    /* Note: Use of H5I_dec_app_ref is a hack, using H5I_dec_ref doesn't reduce
     * app reference count incremented by use of public API to create the ID,
     * while use of H5Idec_ref clears the error stack.  In general we can't use
     * public APIs in the "done" section or in close routines for this reason,
     * until we implement a separate error stack for the VOL plugin */
    if(file->fapl_id != FAIL && H5I_dec_app_ref(file->fapl_id) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTDEC, FAIL, "failed to close plist")
    if(file->fcpl_id != FAIL && H5I_dec_app_ref(file->fcpl_id) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTDEC, FAIL, "failed to close plist");

    /* TODO handle close of multiple structures better */
    if (!daos_handle_is_inval(file->glob_md_oh)) {
        if(H5VL__daos_metadata_close(req) < 0)
            HGOTO_ERROR(H5E_FILE, H5E_CANTCLOSEFILE, FAIL, "can't close global metadata");
    } else if(file->root_grp) {
        if(H5VL__daos_group_close(req) < 0)
            HGOTO_ERROR(H5E_FILE, H5E_CANTCLOSEFILE, FAIL, "can't close root group");
    } else if(!daos_handle_is_inval(file->coh)) {
        if(H5VL__daos_cont_close(req) < 0)
            HGOTO_ERROR(H5E_FILE, H5E_CLOSEERROR, FAIL, "can't close container");
    } else if(!daos_handle_is_inval(file->poh)) {
        if(H5VL__daos_pool_disconnect(req) < 0)
            HGOTO_ERROR(H5E_FILE, H5E_CLOSEERROR, FAIL, "can't disconnect pool");
    } else {
        if(H5VL__daos_file_free(req) < 0)
            HGOTO_ERROR(H5E_FILE, H5E_CANTFREE, FAIL, "can't free file");
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_file_close() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_file_free
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_file_free(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_cont_close *req_cont_close = &req->req.cont_close;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    HDassert(req);

    H5VL_DAOS_LOG_DEBUG("Here");

    req_cont_close->file = H5FL_FREE(H5VL_daos_file_t, req_cont_close->file);

    req->completed = TRUE;

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_file_free() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_pool_connect
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_pool_connect(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_pool_connect *req_pool_conn = &req->req.pool_connect;
    H5VL_daos_file_t *file = req_pool_conn->file;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(req);

    H5VL_DAOS_LOG_DEBUG("Here");

    if(file->my_rank == 0) {
        unsigned int pool_flags =
            (file->flags & H5F_ACC_RDWR) ? DAOS_PC_RW : DAOS_PC_RO;
        H5VL_daos_fapl_t *fa = req_pool_conn->fa;
        uuid_t pool_uuid;

        /* Current operation */
        req->op_type = H5VL_DAOS_POOL_CONNECT;

        /* Next callback */
        req->cb = H5VL__daos_pool_connect_send_ghdl;

        /* Parse UUID */
        if(0 != uuid_parse(fa->pool_uuid, pool_uuid))
            HGOTO_ERROR(H5E_FILE, H5E_CANTDECODE, FAIL, "can't parse pool UUID");

        /* Connect to the pool */
        if(0 != (ret = daos_pool_connect(pool_uuid, fa->pool_grp,
            NULL, pool_flags, &file->poh, NULL, &req->ev)))
            HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL,
                "can't connect to pool (%s, %s): %d", fa->pool_uuid, fa->pool_grp, ret);
    } else {
        daos_iov_t *ghdl = &req_pool_conn->ghdl;

        /* Current operation */
        req->op_type = H5VL_DAOS_POOL_CONNECT_RECV_GHDL_SIZE;

        /* Next callback */
        req->cb = H5VL__daos_pool_connect_recv_ghdl;

        /* Insert request into MPI request list */
        H5_LIST_INSERT_HEAD(&req->context->mpi_req_list, req, entry);

        /* Recv size of global handle from root */
        if(MPI_SUCCESS != MPI_Ibcast(&ghdl->iov_buf_len, 1, MPI_UINT64_T, 0,
            file->comm, &req_pool_conn->ghdl_size_req))
            HGOTO_ERROR(H5E_FILE, H5E_MPI, FAIL, "can't bcast global handle sizes");
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_pool_connect() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_pool_connect_send_ghdl
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_pool_connect_send_ghdl(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_pool_connect *req_pool_conn = &req->req.pool_connect;
    daos_iov_t *ghdl = &req_pool_conn->ghdl;
    H5VL_daos_file_t *file = req_pool_conn->file;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_POOL_CONNECT_SEND_GHDL;

    /* Next callback */
    req->cb = H5VL__daos_pool_connect_send_ghdl_cb;

    /* Insert request into MPI request list */
    H5_LIST_INSERT_HEAD(&req->context->mpi_req_list, req, entry);

    /* Retrieve global pool handle size */
    if(0 != (ret = daos_pool_local2global(file->poh, ghdl)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTINIT, FAIL, "can't get global pool handle size: %d", ret);

    /* Retrieve global pool handle */
    if(NULL == (ghdl->iov_buf = H5MM_malloc(ghdl->iov_buf_len)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL, "can't allocate space for global handles");
    ghdl->iov_len = ghdl->iov_buf_len;

    if(0 != (ret = daos_pool_local2global(file->poh, ghdl)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTCONVERT, FAIL, "can't convert local2global handle: %d", ret);

    HDassert(ghdl->iov_len == ghdl->iov_buf_len);

    /* Broadcast size of global handle to all peers */
    if(MPI_SUCCESS != MPI_Ibcast(&ghdl->iov_buf_len, 1, MPI_UINT64_T, 0,
        file->comm, &req_pool_conn->ghdl_size_req))
        HGOTO_ERROR(H5E_FILE, H5E_MPI, FAIL, "can't bcast global handle sizes");

    /* Broadcast global handle to all peers */
    if(MPI_SUCCESS != MPI_Ibcast(ghdl->iov_buf, (int) ghdl->iov_len, MPI_BYTE,
        0, file->comm, &req_pool_conn->ghdl_req))
        HGOTO_ERROR(H5E_FILE, H5E_MPI, FAIL, "can't bcast global handle sizes");

    /* TODO can execute next op directly here */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_pool_connect_send_ghdl() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_pool_connect_send_ghdl_cb
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_pool_connect_send_ghdl_cb(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_pool_connect *req_pool_conn = &req->req.pool_connect;
    daos_iov_t *ghdl = &req_pool_conn->ghdl;
    H5VL_daos_file_t *file = req_pool_conn->file;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    switch(req->req_type) {
        case H5VL_DAOS_FILE_CREATE:
            /* Reset request info */
            H5MM_free(ghdl->iov_buf);
            HDmemset(&req->req.cont_open, 0, sizeof(struct H5VL_daos_req_cont_open));
            req->req.cont_open.file = file;
            /* Delete the container if H5F_ACC_TRUNC is set.  This shouldn't cause a
             * problem even if the container doesn't exist. */
            /* Need to handle EXCL correctly DSMINC */
            if(file->flags & H5F_ACC_TRUNC) {
                if(FAIL == H5VL__daos_cont_destroy(req))
                    HGOTO_ERROR(H5E_VOL, H5E_CANTCLOSEFILE, FAIL, "can't destroy container");
            } else {
                /* Otherwise just create the container */
                if(FAIL == H5VL__daos_cont_create(req))
                    HGOTO_ERROR(H5E_VOL, H5E_CANTCREATE, FAIL, "can't create container");
            }
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_pool_connect_send_ghdl_cb() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_pool_connect_recv_ghdl
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_pool_connect_recv_ghdl(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_pool_connect *req_pool_conn = &req->req.pool_connect;
    daos_iov_t *ghdl = &req_pool_conn->ghdl;
    H5VL_daos_file_t *file = req_pool_conn->file;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_POOL_CONNECT_RECV_GHDL;

    /* Next callback */
    req->cb = H5VL__daos_pool_connect_recv_ghdl_cb;

    /* Insert request into MPI request list */
    H5_LIST_INSERT_HEAD(&req->context->mpi_req_list, req, entry);

    /* Retrieve global pool handle */
    if(NULL == (ghdl->iov_buf = H5MM_malloc(ghdl->iov_buf_len)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL, "can't allocate space for global handles");
    ghdl->iov_len = ghdl->iov_buf_len;

    /* Broadcast global handle to all peers */
    if(MPI_SUCCESS != MPI_Ibcast(ghdl->iov_buf, (int) ghdl->iov_len, MPI_BYTE,
        0, file->comm, &req_pool_conn->ghdl_req))
        HGOTO_ERROR(H5E_FILE, H5E_MPI, FAIL, "can't bcast global handle sizes");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_pool_connect_recv_ghdl() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_pool_connect_recv_ghdl_cb
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_pool_connect_recv_ghdl_cb(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_pool_connect *req_pool_conn = &req->req.pool_connect;
    daos_iov_t *ghdl = &req_pool_conn->ghdl;
    H5VL_daos_file_t *file = req_pool_conn->file;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    if(0 != (ret = daos_pool_global2local(*ghdl, &file->poh)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTCONVERT, FAIL, "can't convert global2local handle: %d", ret);

    switch(req->req_type) {
        case H5VL_DAOS_FILE_CREATE:
            H5MM_free(ghdl->iov_buf);
            HDmemset(&req->req.cont_open, 0, sizeof(struct H5VL_daos_req_cont_open));
            req->req.cont_open.file = file;
            if(FAIL == H5VL__daos_cont_open(req))
                HGOTO_ERROR(H5E_VOL, H5E_CANTCREATE, FAIL, "can't create container");
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_pool_connect_recv_ghdl_cb() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_pool_disconnect
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_pool_disconnect(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_cont_close *req_cont_close = &req->req.cont_close;
    H5VL_daos_file_t *file = req_cont_close->file;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_POOL_DISCONNECT;

    /* Next callback */
    req->cb = H5VL__daos_file_free;

    /* Disconnect from pool */
    if(0 != (ret = daos_pool_disconnect(file->poh, &req->ev)))
        HGOTO_ERROR(H5E_FILE, H5E_CLOSEERROR, FAIL, "can't disconnect from pool: %d", ret)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_cont_close() */


/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_cont_create
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_cont_create(H5VL_daos_req_t *req)
{
    H5VL_daos_file_t *file = NULL;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(req);
    file = req->req.cont_open.file;
    HDassert(file);

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_CONT_CREATE;

    /* Next callback */
    req->cb = H5VL__daos_cont_open;

    /* Create the container for the file */
    if(0 != (ret = daos_cont_create(file->poh, file->uuid, &req->ev)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTCREATE, FAIL, "can't create container: %d", ret)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_cont_create() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_cont_destroy
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_cont_destroy(H5VL_daos_req_t *req)
{
    H5VL_daos_file_t *file = NULL;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(req);

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_CONT_DESTROY;

    switch(req->req_type) {
        case H5VL_DAOS_FILE_CREATE:
            file = req->req.cont_open.file;
            HDassert(file);
            /* Next callback */
            req->cb = H5VL__daos_cont_create;
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

    /* Destroy the container */
    if(0 != (ret = daos_cont_destroy(file->poh, file->uuid, 1, &req->ev)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTCREATE, FAIL, "can't destroy container: %d", ret)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_cont_destroy() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_cont_open
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_cont_open(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_cont_open *req_cont_open;
    H5VL_daos_file_t *file = NULL;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    HDassert(req);
    req_cont_open = &req->req.cont_open;
    file = req_cont_open->file;
    HDassert(file);

    /* Generate oid for global metadata object */
    daos_obj_id_generate(&req_cont_open->gmd_oid, DAOS_OC_TINY_RW);

    if(file->my_rank == 0) {
        unsigned int cont_flags =
            (file->flags & H5F_ACC_RDWR) ? DAOS_COO_RW : DAOS_COO_RO;

        /* Current operation */
        req->op_type = H5VL_DAOS_CONT_OPEN;

        /* Next callback */
        req->cb = H5VL__daos_cont_open_send_ghdl;

        /* Open the container */
        if(0 != (ret = daos_cont_open(file->poh, file->uuid, cont_flags,
            &file->coh, NULL /*&file->co_info*/, &req->ev)))
            HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL, "can't open container: %d", ret);
    } else {
        daos_iov_t *ghdl = &req_cont_open->ghdl;

        /* Current operation */
        req->op_type = H5VL_DAOS_CONT_OPEN_RECV_GHDL_SIZE;

        /* Next callback */
        req->cb = H5VL__daos_cont_open_recv_ghdl;

        /* Insert request into MPI request list */
        H5_LIST_INSERT_HEAD(&req->context->mpi_req_list, req, entry);

        /* Recv size of global handle from root */
        if(MPI_SUCCESS != MPI_Ibcast(&ghdl->iov_buf_len, 1, MPI_UINT64_T, 0,
            file->comm, &req_cont_open->ghdl_size_req))
            HGOTO_ERROR(H5E_FILE, H5E_MPI, FAIL, "can't bcast global handle sizes");
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_cont_open() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_cont_open_send_ghdl
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_cont_open_send_ghdl(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_cont_open *req_cont_open = &req->req.cont_open;
    daos_iov_t *ghdl = &req_cont_open->ghdl;
    H5VL_daos_file_t *file = req_cont_open->file;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_CONT_OPEN_SEND_GHDL;

    /* Next callback */
    req->cb = H5VL__daos_cont_open_send_ghdl_cb;

    /* Insert request into MPI request list */
    H5_LIST_INSERT_HEAD(&req->context->mpi_req_list, req, entry);

    /* Retrieve global cont handle size */
    if(0 != (ret = daos_cont_local2global(file->coh, ghdl)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTINIT, FAIL, "can't get global pool handle size: %d", ret);

    /* Retrieve global cont handle */
    if(NULL == (ghdl->iov_buf = H5MM_malloc(ghdl->iov_buf_len)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL, "can't allocate space for global handles");
    ghdl->iov_len = ghdl->iov_buf_len;

    if(0 != (ret = daos_cont_local2global(file->coh, ghdl)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTCONVERT, FAIL, "can't convert local2global handle: %d", ret);

    HDassert(ghdl->iov_len == ghdl->iov_buf_len);

    /* Broadcast size of global handle to all peers */
    if(MPI_SUCCESS != MPI_Ibcast(&ghdl->iov_buf_len, 1, MPI_UINT64_T, 0,
        file->comm, &req_cont_open->ghdl_size_req))
        HGOTO_ERROR(H5E_FILE, H5E_MPI, FAIL, "can't bcast global handle sizes");

    /* Broadcast global handle to all peers */
    if(MPI_SUCCESS != MPI_Ibcast(ghdl->iov_buf, (int) ghdl->iov_len, MPI_BYTE,
        0, file->comm, &req_cont_open->ghdl_req))
        HGOTO_ERROR(H5E_FILE, H5E_MPI, FAIL, "can't bcast global handle sizes");

    /* TODO could execute next callback here */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_cont_open_send_ghdl() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_cont_open_send_ghdl_cb
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_cont_open_send_ghdl_cb(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_cont_open *req_cont_open = &req->req.cont_open;
    daos_iov_t *ghdl = &req_cont_open->ghdl;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Free ghdl now */
    H5MM_free(ghdl->iov_buf);

    switch(req->req_type) {
        case H5VL_DAOS_FILE_CREATE:
            if(FAIL == H5VL__daos_epoch_query(req))
                HGOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "can't query epoch");
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_cont_open_send_ghdl_cb() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_cont_open_recv_ghdl
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_cont_open_recv_ghdl(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_cont_open *req_cont_open = &req->req.cont_open;
    daos_iov_t *ghdl = &req_cont_open->ghdl;
    H5VL_daos_file_t *file = req_cont_open->file;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_CONT_OPEN_RECV_GHDL;

    /* Next callback */
    req->cb = H5VL__daos_cont_open_recv_ghdl_cb;

    /* Insert request into MPI request list */
    H5_LIST_INSERT_HEAD(&req->context->mpi_req_list, req, entry);

    /* Retrieve global pool handle */
    if(NULL == (ghdl->iov_buf = H5MM_malloc(ghdl->iov_buf_len)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTALLOC, FAIL, "can't allocate space for global handles");
    ghdl->iov_len = ghdl->iov_buf_len;

    /* Broadcast global handle to all peers */
    if(MPI_SUCCESS != MPI_Ibcast(ghdl->iov_buf, (int) ghdl->iov_len, MPI_BYTE,
        0, file->comm, &req_cont_open->ghdl_req))
        HGOTO_ERROR(H5E_FILE, H5E_MPI, FAIL, "can't bcast global handle sizes");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_cont_open_recv_ghdl() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_cont_open_recv_ghdl_cb
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_cont_open_recv_ghdl_cb(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_cont_open *req_cont_open = &req->req.cont_open;
    daos_iov_t *ghdl = &req_cont_open->ghdl;
    H5VL_daos_file_t *file = req_cont_open->file;

    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    if(0 != (ret = daos_cont_global2local(file->poh, *ghdl, &file->coh)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTCONVERT, FAIL, "can't convert global2local handle: %d", ret);

    /* TODO */
    switch(req->req_type) {
        case H5VL_DAOS_FILE_CREATE:
            if(FAIL == H5VL__daos_epoch_bcast(req))
                HGOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "can't get epoch");
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

done:
    H5MM_free(ghdl->iov_buf);
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_cont_open_recv_ghdl_cb() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_cont_close
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_cont_close(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_cont_close *req_cont_close = &req->req.cont_close;
    H5VL_daos_file_t *file = req_cont_close->file;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_CONT_CLOSE;

    /* Next callback */
    if(!daos_handle_is_inval(file->poh))
        req->cb = H5VL__daos_pool_disconnect;
    else
        req->cb = H5VL__daos_file_free;

    if (0 != (ret = daos_cont_close(file->coh, &req->ev)))
        HGOTO_ERROR(H5E_FILE, H5E_CLOSEERROR, FAIL, "can't close container: %d", ret)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_cont_close() */


/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_epoch_query
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_epoch_query(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_cont_open *req_cont_open = &req->req.cont_open;
    H5VL_daos_file_t *file = req_cont_open->file;
    daos_epoch_state_t *state = &req_cont_open->state;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_EPOCH_QUERY;

    /* Next callback */
    req->cb = H5VL__daos_epoch_hold;

    /* Query the epoch */
    if(0 != (ret = daos_epoch_query(file->coh, state, &req->ev)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTINIT, FAIL, "can't query epoch: %d", ret);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_epoch_query() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_epoch_hold
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_epoch_hold(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_cont_open *req_cont_open = &req->req.cont_open;
    H5VL_daos_file_t *file = req_cont_open->file;
    daos_epoch_state_t *state = &req_cont_open->state;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_EPOCH_HOLD;

    switch(req->req_type) {
        case H5VL_DAOS_FILE_CREATE:
            /* Next callback */
            req->cb = H5VL__daos_epoch_bcast;
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

    /* Hold the epoch */
    file->epoch = state->es_hce;

    /* Hold the epoch if write access is requested */
    if(file->flags & H5F_ACC_RDWR) {
        /* Hold the next epoch */
        file->epoch += (daos_epoch_t)1;
        if(0 != (ret = daos_epoch_hold(file->coh, &file->epoch, NULL /*state*/, &req->ev)))
            HGOTO_ERROR(H5E_FILE, H5E_CANTINIT, FAIL, "can't hold epoch: %d", ret);
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_epoch_hold() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_epoch_bcast
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_epoch_bcast(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_cont_open *req_cont_open = &req->req.cont_open;
    H5VL_daos_file_t *file = req_cont_open->file;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_EPOCH_BCAST;

    switch(req->req_type) {
        case H5VL_DAOS_FILE_CREATE:
            /* Next callback */
            req->cb = (file->my_rank == 0) ? H5VL__daos_metadata_create :
                H5VL__daos_metadata_open;
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

    /* Insert request into MPI request list */
    H5_LIST_INSERT_HEAD(&req->context->mpi_req_list, req, entry);

    /* Broadcast size of global handle to all peers */
    if(MPI_SUCCESS != MPI_Ibcast(&file->epoch, 1, MPI_UINT64_T, 0,
        file->comm, &req_cont_open->epoch_req))
        HGOTO_ERROR(H5E_FILE, H5E_MPI, FAIL, "can't bcast epoch");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_epoch_bcast() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_epoch_commit
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_epoch_commit(H5VL_daos_req_t *req)
{
    H5VL_daos_file_t *file = NULL;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_EPOCH_COMMIT;

    switch(req->req_type) {
        case H5VL_DAOS_FILE_FLUSH: {
            struct H5VL_daos_req_cont_flush *req_cont_flush = &req->req.cont_flush;
            file = req_cont_flush->file;

            /* Next callback */
            req->cb = H5VL__daos_file_flush_cb;
        }
            break;
        case H5VL_DAOS_FILE_CLOSE: {
            struct H5VL_daos_req_cont_close *req_cont_close = &req->req.cont_close;
            file = req_cont_close->file;

            /* Next callback */
            req->cb = H5VL__daos_file_close;
        }
            break;
        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

    /* Commit the epoch */
    if(file->my_rank == 0) {
        /* Commit the epoch */
        if(0 != (ret = daos_epoch_commit(file->coh, file->epoch, NULL /*state*/, &req->ev)))
            HGOTO_ERROR(H5E_FILE, H5E_CANTINIT, FAIL, "failed to commit epoch: %d", ret)
    } /* end if */

    /* Advance the epoch */
    file->epoch++;


done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_epoch_commit() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_metadata_create
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_metadata_create(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_cont_open *req_cont_open = &req->req.cont_open;
    H5VL_daos_file_t *file = req_cont_open->file;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_METADATA_CREATE;

    /* Next callback */
    /* req->cb = H5VL__daos_metadata_open; */

    /* Create global metadata object */
    if(0 != (ret = daos_obj_declare(file->coh, req_cont_open->gmd_oid,
        file->epoch, NULL /*oa*/, &req->ev)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTCREATE, FAIL, "can't create global metadata object: %d", ret);

    /* TODO daos_obj_declare does not do anything yet so call
     * H5VL__daos_metadata_open directly */
    if(FAIL == H5VL__daos_metadata_open(req))
        HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL, "can't open global metadata");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_metadata_create() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_metadata_open
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_metadata_open(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_cont_open *req_cont_open = &req->req.cont_open;
    H5VL_daos_file_t *file = req_cont_open->file;
    unsigned int obj_flags =
        (file->flags & H5F_ACC_RDWR) ? DAOS_OO_RW : DAOS_OO_RO;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_METADATA_OPEN;

    /* Next callback */
    req->cb = H5VL__daos_metadata_open_cb;

    /* Open global metadata object */
    if(0 != (ret = daos_obj_open(file->coh, req_cont_open->gmd_oid, file->epoch,
        obj_flags, &file->glob_md_oh, &req->ev)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL, "can't open global metadata object: %d", ret)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_metadata_open() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_metadata_open_cb
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_metadata_open_cb(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_cont_open *req_cont_open = &req->req.cont_open;
    H5VL_daos_file_t *file = req_cont_open->file;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    switch(req->req_type) {
        case H5VL_DAOS_FILE_CREATE:
            HDmemset(&req->req.cont_open, 0, sizeof(struct H5VL_daos_req_cont_open));
            req->req.cont_open.file = file;
            /* Init root group */
            if(NULL == (file->root_grp = H5VL__daos_group_init(file)))
                HGOTO_ERROR(H5E_VOL, H5E_CANTCREATE, FAIL, "can't create root group");
            if(file->my_rank == 0) {
                /* Create root group */
                if(FAIL == H5VL__daos_group_create(req))
                    HGOTO_ERROR(H5E_VOL, H5E_CANTCREATE, FAIL, "can't create root group");
            } else {
                file->root_grp->obj.oid.lo = 1; //DSMINC
                file->root_grp->obj.oid.mid = 0; //DSMINC
                file->root_grp->obj.oid.hi = 0; //DSMINC
                daos_obj_id_generate(&file->root_grp->obj.oid, DAOS_OC_TINY_RW); //DSMINC

                /* Create root group */
                if(FAIL == H5VL__daos_group_open(req))
                    HGOTO_ERROR(H5E_VOL, H5E_CANTCREATE, FAIL, "can't create root group");
            }
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_metadata_open_cb() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_metadata_close
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_metadata_close(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_cont_close *req_cont_close = &req->req.cont_close;
    H5VL_daos_file_t *file = req_cont_close->file;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_METADATA_CLOSE;

    /* Next callback */
    if(file->root_grp)
        req->cb = H5VL__daos_group_close;
    else if(!daos_handle_is_inval(file->coh))
        req->cb = H5VL__daos_cont_close;
    else if(!daos_handle_is_inval(file->poh))
        req->cb = H5VL__daos_pool_disconnect;
    else
        req->cb = H5VL__daos_file_free;

    /* Close global metadata */
    if(0 != (ret = daos_obj_close(file->glob_md_oh, &req->ev)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTCLOSEFILE, FAIL, "can't close global metadata object: %d", ret)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_metadata_close() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_group_init
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static H5VL_daos_group_t *
H5VL__daos_group_init(H5VL_daos_file_t *file)
{
    H5VL_daos_group_t *grp = NULL;
    H5VL_daos_group_t *ret_value = NULL;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Allocate the group object that is returned to the user */
    if(NULL == (grp = H5FL_CALLOC(H5VL_daos_group_t)))
        HGOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, NULL, "can't allocate DAOS group struct")
    grp->obj.item.type = H5I_GROUP;
    grp->obj.item.file = file;
    grp->obj.item.rc = 1;
    grp->obj.obj_oh.cookie = 0;
    grp->gcpl_id = FAIL;
    grp->gapl_id = FAIL;

    ret_value = grp;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_group_init() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_group_create
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_group_create(H5VL_daos_req_t *req)
{
    H5VL_daos_file_t *file = NULL;
    H5VL_daos_group_t *grp = NULL;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_GROUP_CREATE;

    /* Next callback */
    req->cb = H5VL__daos_group_open;

    switch(req->req_type) {
        case H5VL_DAOS_FILE_CREATE: {
            struct H5VL_daos_req_cont_open *req_cont_open = &req->req.cont_open;
            file = req_cont_open->file;
            grp = file->root_grp;
        }
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

    /* Create group */
    grp->obj.oid.lo = file->max_oid++;
    daos_obj_id_generate(&grp->obj.oid, DAOS_OC_TINY_RW);
    if(0 != (ret = daos_obj_declare(file->coh, grp->obj.oid, file->epoch,
        NULL /*oa*/, &req->ev)))
        HGOTO_ERROR(H5E_SYM, H5E_CANTINIT, FAIL, "can't create dataset: %d", ret)

    /* TODO daos_obj_declare does not do anything yet so call
     * H5VL__daos_write_max_oid directly */

    /* Write max OID */
     if(H5VL__daos_write_max_oid(file, req) < 0)
         HGOTO_ERROR(H5E_FILE, H5E_CANTINIT, FAIL, "can't write max OID")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_group_create() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_write_max_oid
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_write_max_oid(H5VL_daos_file_t *file, H5VL_daos_req_t *req)
{
    daos_key_t dkey;
    daos_vec_iod_t iod;
    daos_recx_t recx;
    daos_sg_list_t sgl;
    daos_iov_t sg_iov;
    char int_md_key[] = H5VL_DAOS_INT_MD_KEY;
    char max_oid_key[] = H5VL_DAOS_MAX_OID_KEY;
    int ret;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Set up dkey */
    daos_iov_set(&dkey, int_md_key, (daos_size_t)(sizeof(int_md_key) - 1));

    /* Set up recx */
    recx.rx_rsize = (uint64_t)8;
    recx.rx_idx = (uint64_t)0;
    recx.rx_nr = (uint64_t)1;

    /* Set up iod */
    HDmemset(&iod, 0, sizeof(iod));
    daos_iov_set(&iod.vd_name, (void *)max_oid_key, (daos_size_t)(sizeof(max_oid_key) - 1));
    daos_csum_set(&iod.vd_kcsum, NULL, 0);
    iod.vd_nr = 1u;
    iod.vd_recxs = &recx;

    /* Set up sgl */
    daos_iov_set(&sg_iov, &file->max_oid, (daos_size_t)8);
    sgl.sg_nr.num = 1;
    sgl.sg_iovs = &sg_iov;

    /* Write max OID to gmd obj */
    if(0 != (ret = daos_obj_update(file->glob_md_oh, file->epoch, &dkey, 1,
        &iod, &sgl, &req->ev)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTENCODE, FAIL, "can't write max OID to global metadata object: %d", ret)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_write_max_oid() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_group_open
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_group_open(H5VL_daos_req_t *req)
{
    H5VL_daos_file_t *file = NULL;
    H5VL_daos_group_t *grp = NULL;
    unsigned int obj_flags;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_GROUP_OPEN;


    switch(req->req_type) {
        case H5VL_DAOS_FILE_CREATE: {
            struct H5VL_daos_req_cont_open *req_cont_open = &req->req.cont_open;
            file = req_cont_open->file;
            grp = file->root_grp;

            /* Finish setting up group struct */
            if((grp->gcpl_id = H5Pcopy(H5P_GROUP_CREATE_DEFAULT)) < 0)
                HGOTO_ERROR(H5E_SYM, H5E_CANTCOPY, FAIL, "failed to copy gcpl");
            if((grp->gapl_id = H5Pcopy(H5P_GROUP_ACCESS_DEFAULT)) < 0)
                HGOTO_ERROR(H5E_SYM, H5E_CANTCOPY, FAIL, "failed to copy gapl");

            /* Next callback */
            req->cb = (file->my_rank == 0) ? H5VL__daos_group_update_metadata :
                H5VL__daos_group_fetch_metadata_size;

        }
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

    /* Open group */
    obj_flags = (file->flags & H5F_ACC_RDWR) ? DAOS_OO_RW : DAOS_OO_RO;
    if(0 != (ret = daos_obj_open(file->coh, grp->obj.oid, file->epoch, obj_flags,
        &grp->obj.obj_oh, &req->ev)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTOPENOBJ, FAIL, "can't open root group: %d", ret)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_group_open() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_group_close
 *
 * Purpose:     Closes a DAOS HDF5 group.
 *
 * Return:      Success:        0
 *              Failure:        -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_group_close(H5VL_daos_req_t *req)
{
    H5VL_daos_file_t *file = NULL;
    H5VL_daos_group_t *grp = NULL;
    daos_handle_t hdl_inval = {0};
    int ret;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_GROUP_CLOSE;

    switch(req->req_type) {
        case H5VL_DAOS_FILE_CLOSE: {
            struct H5VL_daos_req_cont_close *req_cont_close = &req->req.cont_close;
            file = req_cont_close->file;
            grp = file->root_grp;

            /* Next callback */
            req->cb = H5VL__daos_group_free;
        }
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

    if(--grp->obj.item.rc == 0) {
        /* Free group data structures */
        if(HDmemcmp(&grp->obj.obj_oh, &hdl_inval, sizeof(hdl_inval)))
            if(0 != (ret = daos_obj_close(grp->obj.obj_oh, &req->ev)))
                HDONE_ERROR(H5E_SYM, H5E_CANTCLOSEOBJ, FAIL, "can't close group DAOS object: %d", ret);
        if(grp->gcpl_id != FAIL && H5I_dec_app_ref(grp->gcpl_id) < 0)
            HDONE_ERROR(H5E_SYM, H5E_CANTDEC, FAIL, "failed to close plist");
        if(grp->gapl_id != FAIL && H5I_dec_app_ref(grp->gapl_id) < 0)
            HDONE_ERROR(H5E_SYM, H5E_CANTDEC, FAIL, "failed to close plist");
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_group_close() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_group_free
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_group_free(H5VL_daos_req_t *req)
{
    H5VL_daos_file_t *file = NULL;
    H5VL_daos_group_t *grp = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    switch(req->req_type) {
        case H5VL_DAOS_FILE_CLOSE: {
            struct H5VL_daos_req_cont_close *req_cont_close = &req->req.cont_close;
            file = req_cont_close->file;
            grp = file->root_grp;

            grp = H5FL_FREE(H5VL_daos_group_t, grp);

            if(!daos_handle_is_inval(file->coh)) {
                if(H5VL__daos_cont_close(req) < 0)
                    HGOTO_ERROR(H5E_FILE, H5E_CLOSEERROR, FAIL, "can't close container");
            } else if(!daos_handle_is_inval(file->poh)) {
                if(H5VL__daos_pool_disconnect(req) < 0)
                    HGOTO_ERROR(H5E_FILE, H5E_CLOSEERROR, FAIL, "can't disconnect pool");
            } else {
                if(H5VL__daos_file_free(req) < 0)
                    HGOTO_ERROR(H5E_FILE, H5E_CANTFREE, FAIL, "can't free file");
            }
        }
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_group_free() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_group_update_metadata
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_group_update_metadata(H5VL_daos_req_t *req)
{
    H5VL_daos_file_t *file = NULL;
    H5VL_daos_group_t *grp = NULL;
    daos_key_t dkey;
    daos_vec_iod_t iod;
    daos_recx_t recx;
    daos_sg_list_t sgl;
    daos_iov_t sg_iov;
    size_t gcpl_size = 0;
    void *gcpl_buf = NULL;
    char int_md_key[] = H5VL_DAOS_INT_MD_KEY;
    char gcpl_key[] = H5VL_DAOS_CPL_KEY;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_GROUP_UPDATE_METADATA;

    /* Next callback */
    req->cb = H5VL__daos_group_update_metadata_cb;

    switch(req->req_type) {
        case H5VL_DAOS_FILE_CREATE: {
            struct H5VL_daos_req_cont_open *req_cont_open = &req->req.cont_open;
            file = req_cont_open->file;
            grp = file->root_grp;
        }
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

    /* Encode GCPL  TODO clean it */
    if(H5Pencode(grp->gcpl_id, NULL, &gcpl_size) < 0)
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "can't determine serialized length of gcpl");
    if(NULL == (gcpl_buf = H5MM_malloc(gcpl_size)))
        HGOTO_ERROR(H5E_ARGS, H5E_CANTALLOC, FAIL, "can't allocate buffer for serialized gcpl");
    if(H5Pencode(grp->gcpl_id, gcpl_buf, &gcpl_size) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTENCODE, FAIL, "can't serialize gcpl");

    /* Set up operation to write GCPL to group */
    /* Set up dkey */
    daos_iov_set(&dkey, int_md_key, (daos_size_t)(sizeof(int_md_key) - 1));

    /* Set up recx */
    recx.rx_rsize = (uint64_t)gcpl_size;
    recx.rx_idx = (uint64_t)0;
    recx.rx_nr = (uint64_t)1;

    /* Set up iod */
    HDmemset(&iod, 0, sizeof(iod));
    daos_iov_set(&iod.vd_name, (void *)gcpl_key, (daos_size_t)(sizeof(gcpl_key) - 1));
    daos_csum_set(&iod.vd_kcsum, NULL, 0);
    iod.vd_nr = 1u;
    iod.vd_recxs = &recx;

    /* Set up sgl */
    daos_iov_set(&sg_iov, gcpl_buf, (daos_size_t)gcpl_size);
    sgl.sg_nr.num = 1;
    sgl.sg_iovs = &sg_iov;

    /* Write internal metadata to group */
    if(0 != (ret = daos_obj_update(grp->obj.obj_oh, file->epoch, &dkey, 1, &iod, &sgl, &req->ev)))
        HGOTO_ERROR(H5E_SYM, H5E_CANTINIT, FAIL, "can't write metadata to group: %d", ret)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_group_update_metadata() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_group_update_metadata_cb
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_group_update_metadata_cb(H5VL_daos_req_t *req)
{
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    H5VL_DAOS_LOG_DEBUG("Here");

    req->completed = TRUE;

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_group_update_metadata_cb() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_group_fetch_metadata_size
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_group_fetch_metadata_size(H5VL_daos_req_t *req)
{
    H5VL_daos_file_t *file = NULL;
    H5VL_daos_group_t *grp = NULL;
    daos_key_t *dkey;
    daos_vec_iod_t *iod;
    daos_recx_t *grp_recx;
    char int_md_key[] = H5VL_DAOS_INT_MD_KEY;
    char gcpl_key[] = H5VL_DAOS_CPL_KEY;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_GROUP_FETCH_METADATA_SIZE;

    /* Next callback */
    req->cb = H5VL__daos_group_fetch_metadata;

    switch(req->req_type) {
        case H5VL_DAOS_FILE_CREATE: {
            struct H5VL_daos_req_cont_open *req_cont_open = &req->req.cont_open;
            file = req_cont_open->file;
            HDassert(file);
            grp = file->root_grp;
            HDassert(grp);
            grp_recx = &req_cont_open->root_grp_recx;
            dkey = &req_cont_open->root_grp_dkey;
            iod = &req_cont_open->root_grp_iod;
        }
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

    /* Set up operation to read GCPL size from group */
    /* Set up dkey */
    daos_iov_set(dkey, int_md_key, (daos_size_t)(sizeof(int_md_key) - 1));

    /* Set up recx */
    grp_recx->rx_rsize = DAOS_REC_ANY;
    grp_recx->rx_idx = (uint64_t)0;
    grp_recx->rx_nr = (uint64_t)1;

    /* Set up iod */
    HDmemset(iod, 0, sizeof(*iod));
    daos_iov_set(&iod->vd_name, (void *)gcpl_key, (daos_size_t)(sizeof(gcpl_key) - 1));
    daos_csum_set(&iod->vd_kcsum, NULL, 0);
    iod->vd_nr = 1u;
    iod->vd_recxs = grp_recx;

    /* Read internal metadata size from group */
    if(0 != (ret = daos_obj_fetch(grp->obj.obj_oh, file->epoch, dkey, 1, iod, NULL, NULL /*maps*/, &req->ev)))
        HGOTO_ERROR(H5E_SYM, H5E_CANTDECODE, FAIL, "can't read metadata size from group: %d", ret);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_group_fetch_metadata_size() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_group_fetch_metadata
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_group_fetch_metadata(H5VL_daos_req_t *req)
{
    H5VL_daos_file_t *file = NULL;
    H5VL_daos_group_t *grp = NULL;
    daos_key_t *dkey;
    daos_vec_iod_t *iod;
    daos_recx_t *grp_recx;
    daos_sg_list_t sgl;
    daos_iov_t *grp_sg_iov;
    void *gcpl_buf = NULL;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_GROUP_FETCH_METADATA;

    /* Next callback */
    req->cb = H5VL__daos_group_fetch_metadata_cb;

    switch(req->req_type) {
        case H5VL_DAOS_FILE_CREATE: {
            struct H5VL_daos_req_cont_open *req_cont_open = &req->req.cont_open;
            file = req_cont_open->file;
            grp = file->root_grp;
            grp_recx = &req_cont_open->root_grp_recx;
            grp_sg_iov = &req_cont_open->root_grp_sg_iov;
            dkey = &req_cont_open->root_grp_dkey;
            iod = &req_cont_open->root_grp_iod;
        }
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

    /* Check for metadata not found */
    if(grp_recx->rx_rsize == (uint64_t)0)
        HGOTO_ERROR(H5E_SYM, H5E_NOTFOUND, FAIL, "internal metadata not found")

    /* Allocate buffer for GCPL */
    if(NULL == (gcpl_buf = H5MM_malloc(grp_recx->rx_rsize)))
        HGOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, FAIL, "can't allocate buffer for serialized gcpl")

    /* Set up sgl */
    daos_iov_set(grp_sg_iov, gcpl_buf, (daos_size_t)grp_recx->rx_rsize);
    sgl.sg_nr.num = 1;
    sgl.sg_iovs = grp_sg_iov;

    /* Read internal metadata from group */
    if(0 != (ret = daos_obj_fetch(grp->obj.obj_oh, file->epoch, dkey, 1, iod, &sgl, NULL /*maps*/, &req->ev)))
        HGOTO_ERROR(H5E_SYM, H5E_CANTDECODE, FAIL, "can't read metadata from group: %d", ret)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_group_fetch_metadata() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_group_fetch_metadata_cb
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_group_fetch_metadata_cb(H5VL_daos_req_t *req)
{
    herr_t ret_value = SUCCEED;
    daos_iov_t *grp_sg_iov;
    H5VL_daos_group_t *grp = NULL;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    switch(req->req_type) {
        case H5VL_DAOS_FILE_CREATE: {
            struct H5VL_daos_req_cont_open *req_cont_open = &req->req.cont_open;
            grp_sg_iov = &req_cont_open->root_grp_sg_iov;
            grp = req_cont_open->file->root_grp;
        }
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

    /* Decode GCPL */
    if((grp->gcpl_id = H5Pdecode(grp_sg_iov->iov_buf)) < 0)
        HGOTO_ERROR(H5E_ARGS, H5E_CANTDECODE, FAIL, "can't deserialize GCPL")

    /* Finish setting up group struct */
    if((grp->gapl_id = H5Pcopy(H5P_GROUP_ACCESS_DEFAULT)) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTCOPY, FAIL, "failed to copy gapl");

    /* Free memory */
    grp_sg_iov->iov_buf = H5MM_xfree(grp_sg_iov->iov_buf);

    req->completed = TRUE;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_group_fetch_metadata_cb() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_dset_init
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static H5VL_daos_dset_t *
H5VL__daos_dset_init(H5VL_daos_item_t *item)
{
    H5VL_daos_dset_t *dset = NULL;
    H5VL_daos_dset_t *ret_value = NULL;

    FUNC_ENTER_NOAPI_NOINIT

    /* Allocate the dataset object that is returned to the user */
    if(NULL == (dset = H5FL_CALLOC(H5VL_daos_dset_t)))
        HGOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, NULL, "can't allocate DAOS dataset struct")
    dset->obj.item.type = H5I_DATASET;
    dset->obj.item.file = item->file;
    dset->obj.item.rc = 1;
    dset->obj.obj_oh.cookie = 0;
    dset->type_id = FAIL;
    dset->space_id = FAIL;
    dset->dcpl_id = FAIL;
    dset->dapl_id = FAIL;

    ret_value = dset;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_dset_init() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_dset_create
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_dset_create(H5VL_daos_req_t *req)
{
    H5VL_daos_item_t *item = NULL;
    H5VL_daos_dset_t *dset = NULL;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_DSET_CREATE;

    /* Next callback */
    req->cb = H5VL__daos_link_create;

    switch(req->req_type) {
        case H5VL_DAOS_DATASET_CREATE: {
            struct H5VL_daos_req_dset_open *req_dset_open = &req->req.dset_open;
            item = req_dset_open->item;
            dset = req_dset_open->dset;
        }
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

    /* Create dataset */
    dset->obj.oid.lo = item->file->max_oid + (uint64_t)1;
    daos_obj_id_generate(&dset->obj.oid, DAOS_OC_LARGE_RW);
    if(0 != (ret = daos_obj_declare(item->file->coh, dset->obj.oid,
        item->file->epoch, NULL /*oa*/, &req->ev)))
        HGOTO_ERROR(H5E_SYM, H5E_CANTINIT, FAIL, "can't create dataset: %d", ret)
    item->file->max_oid = dset->obj.oid.lo;

    /* TODO daos_obj_declare does not do anything yet so call
     * H5VL__daos_write_max_oid directly */

    /* Write max OID */
    if(H5VL__daos_write_max_oid(item->file, req) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTINIT, FAIL, "can't write max OID")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_dset_create() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_link_create
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_link_create(H5VL_daos_req_t *req)
{
    H5VL_daos_dset_t *dset = NULL;
    H5VL_daos_link_val_t link_val;
    H5VL_daos_group_t *target_grp;
    char *target_name;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_LINK_CREATE;

    switch(req->req_type) {
        case H5VL_DAOS_DATASET_CREATE: {
            struct H5VL_daos_req_dset_open *req_dset_open = &req->req.dset_open;
            dset = req_dset_open->dset;
            target_grp = req_dset_open->target_grp;
            target_name = req_dset_open->target_name;

            /* Next callback */
            req->cb = H5VL__daos_dset_open;
        }
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

    /* Create link to dataset */
    link_val.type = H5L_TYPE_HARD;
    link_val.target.hard = dset->obj.oid;
    if(H5VL__daos_link_write(target_grp, target_name, HDstrlen(target_name),
        &link_val, req) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTINIT, FAIL, "can't create link to dataset")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_link_create() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_link_write
 *
 * Purpose:     Writes the specified link to the given group
 *
 * Return:      Success:        SUCCEED
 *              Failure:        FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_link_write(H5VL_daos_group_t *grp, char *name,
    size_t name_len, H5VL_daos_link_val_t *val, H5VL_daos_req_t *req)
{
    char const_link_key[] = H5VL_DAOS_LINK_KEY;
    daos_key_t dkey;
    daos_vec_iod_t iod;
    daos_recx_t recx;
    daos_sg_list_t sgl;
    daos_iov_t sg_iov[2];
    uint8_t iov_buf[25];
    uint8_t *p;
    int ret;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    /* Check for write access */
    if(!(grp->obj.item.file->flags & H5F_ACC_RDWR))
        HGOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "no write intent on file")

    /* Set up dkey */
    /* For now always use dkey = const, akey = name. Add option to switch these
     * DSMINC */
    daos_iov_set(&dkey, const_link_key, (daos_size_t)(sizeof(const_link_key) - 1));

    /* Encode link type */
    p = iov_buf;
    *p++ = (uint8_t)val->type;

    /* Encode type specific value information */
    switch(val->type) {
         case H5L_TYPE_HARD:
            HDassert(sizeof(iov_buf) == sizeof(val->target.hard) + 1);

            /* Encode oid */
            UINT64ENCODE(p, val->target.hard.lo)
            UINT64ENCODE(p, val->target.hard.mid)
            UINT64ENCODE(p, val->target.hard.hi)

            /* Set up type specific recx */
            recx.rx_rsize = (uint64_t)25;

            /* Set up type specific sgl */
            daos_iov_set(&sg_iov[0], iov_buf, (daos_size_t)sizeof(iov_buf));
            sgl.sg_nr.num = 1;

            break;

        case H5L_TYPE_SOFT:
            /* Set up type specific recx.  We need an extra byte for the link
             * type (encoded above). */
            recx.rx_rsize = (uint64_t)(HDstrlen(val->target.soft) + 1);

            /* Set up type specific sgl.  We use two entries, the first for the
             * link type, the second for the string. */
            daos_iov_set(&sg_iov[0], iov_buf, (daos_size_t)1);
            daos_iov_set(&sg_iov[1], val->target.soft, (daos_size_t)(recx.rx_rsize - (uint64_t)1));
            sgl.sg_nr.num = 2;

            break;

        case H5L_TYPE_ERROR:
        case H5L_TYPE_EXTERNAL:
        case H5L_TYPE_MAX:
        default:
            HGOTO_ERROR(H5E_SYM, H5E_BADVALUE, FAIL, "invalid or unsupported link type")
    } /* end switch */

    /* Set up general recx */
    recx.rx_idx = (uint64_t)0;
    recx.rx_nr = (uint64_t)1;

    /* Set up iod */
    HDmemset(&iod, 0, sizeof(iod));
    daos_iov_set(&iod.vd_name, name, (daos_size_t)name_len);
    daos_csum_set(&iod.vd_kcsum, NULL, 0);
    iod.vd_nr = 1u;
    iod.vd_recxs = &recx;

    /* Set up general sgl */
    sgl.sg_iovs = sg_iov;

    /* Write link */
    if(0 != (ret = daos_obj_update(grp->obj.obj_oh, grp->obj.item.file->epoch,
        &dkey, 1, &iod, &sgl, &req->ev)))
        HGOTO_ERROR(H5E_SYM, H5E_CANTINIT, FAIL, "can't write link: %d", ret)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_link_write() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_dset_open
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_dset_open(H5VL_daos_req_t *req)
{
    H5VL_daos_item_t *item = NULL;
    H5VL_daos_dset_t *dset = NULL;
    unsigned int obj_flags;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_DSET_OPEN;

    switch(req->req_type) {
        case H5VL_DAOS_DATASET_CREATE: {
            struct H5VL_daos_req_dset_open *req_dset_open = &req->req.dset_open;
            item = req_dset_open->item;
            dset = req_dset_open->dset;
            obj_flags = (item->file->flags & H5F_ACC_RDWR) ? DAOS_OO_RW : DAOS_OO_RO;

            /* Next callback */
            req->cb = H5VL__daos_dset_update_metadata;
        }
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

    /* Open dataset */
    if(0 != (ret = daos_obj_open(item->file->coh, dset->obj.oid, item->file->epoch,
        obj_flags, &dset->obj.obj_oh, &req->ev)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTOPENOBJ, FAIL, "can't open root group: %d", ret)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_dset_open() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_dset_update_metadata
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_dset_update_metadata(H5VL_daos_req_t *req)
{
    H5VL_daos_item_t *item = NULL;
    H5VL_daos_dset_t *dset = NULL;
    daos_key_t dkey;
    daos_vec_iod_t iod[3];
    daos_recx_t recx[3];
    daos_sg_list_t sgl[3];
    daos_iov_t sg_iov[3];
    size_t type_size = 0;
    size_t space_size = 0;
    size_t dcpl_size = 0;
    void *type_buf = NULL;
    void *space_buf = NULL;
    void *dcpl_buf = NULL;
    char int_md_key[] = H5VL_DAOS_INT_MD_KEY;
    char type_key[] = H5VL_DAOS_TYPE_KEY;
    char space_key[] = H5VL_DAOS_SPACE_KEY;
    char dcpl_key[] = H5VL_DAOS_CPL_KEY;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    /* Current operation */
    req->op_type = H5VL_DAOS_DSET_UPDATE_METADATA;

    /* Next callback */
    req->cb = H5VL__daos_dset_update_metadata_cb;

    switch(req->req_type) {
        case H5VL_DAOS_DATASET_CREATE: {
            struct H5VL_daos_req_dset_open *req_dset_open = &req->req.dset_open;
            item = req_dset_open->item;
            dset = req_dset_open->dset;
        }
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

    /* Encode datatype */
    if(H5Tencode(dset->type_id, NULL, &type_size) < 0)
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "can't determine serialized length of datatype")
    if(NULL == (type_buf = H5MM_malloc(type_size)))
        HGOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, FAIL, "can't allocate buffer for serialized datatype")
    if(H5Tencode(dset->type_id, type_buf, &type_size) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTENCODE, FAIL, "can't serialize datatype")

    /* Encode dataspace */
    if(H5Sencode(dset->space_id, NULL, &space_size) < 0)
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "can't determine serialized length of dataaspace")
    if(NULL == (space_buf = H5MM_malloc(space_size)))
        HGOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, FAIL, "can't allocate buffer for serialized dataaspace")
    if(H5Sencode(dset->space_id, space_buf, &space_size) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTENCODE, FAIL, "can't serialize dataaspace")

    /* Encode DCPL */
    if(H5Pencode(dset->dcpl_id, NULL, &dcpl_size) < 0)
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "can't determine serialized length of dcpl")
    if(NULL == (dcpl_buf = H5MM_malloc(dcpl_size)))
        HGOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, FAIL, "can't allocate buffer for serialized dcpl")
    if(H5Pencode(dset->dcpl_id, dcpl_buf, &dcpl_size) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTENCODE, FAIL, "can't serialize dcpl")

    /* Set up operation to write datatype, dataspace, and DCPL to dataset */
    /* Set up dkey */
    daos_iov_set(&dkey, int_md_key, (daos_size_t)(sizeof(int_md_key) - 1));

    /* Set up recx */
    recx[0].rx_rsize = (uint64_t)type_size;
    recx[0].rx_idx = (uint64_t)0;
    recx[0].rx_nr = (uint64_t)1;
    recx[1].rx_rsize = (uint64_t)space_size;
    recx[1].rx_idx = (uint64_t)0;
    recx[1].rx_nr = (uint64_t)1;
    recx[2].rx_rsize = (uint64_t)dcpl_size;
    recx[2].rx_idx = (uint64_t)0;
    recx[2].rx_nr = (uint64_t)1;

    /* Set up iod */
    HDmemset(iod, 0, sizeof(iod));
    daos_iov_set(&iod[0].vd_name, (void *)type_key, (daos_size_t)(sizeof(type_key) - 1));
    daos_csum_set(&iod[0].vd_kcsum, NULL, 0);
    iod[0].vd_nr = 1u;
    iod[0].vd_recxs = &recx[0];
    daos_iov_set(&iod[1].vd_name, (void *)space_key, (daos_size_t)(sizeof(space_key) - 1));
    daos_csum_set(&iod[1].vd_kcsum, NULL, 0);
    iod[1].vd_nr = 1u;
    iod[1].vd_recxs = &recx[1];
    daos_iov_set(&iod[2].vd_name, (void *)dcpl_key, (daos_size_t)(sizeof(dcpl_key) - 1));
    daos_csum_set(&iod[2].vd_kcsum, NULL, 0);
    iod[2].vd_nr = 1u;
    iod[2].vd_recxs = &recx[2];

    /* Set up sgl */
    daos_iov_set(&sg_iov[0], type_buf, (daos_size_t)type_size);
    sgl[0].sg_nr.num = 1;
    sgl[0].sg_iovs = &sg_iov[0];
    daos_iov_set(&sg_iov[1], space_buf, (daos_size_t)space_size);
    sgl[1].sg_nr.num = 1;
    sgl[1].sg_iovs = &sg_iov[1];
    daos_iov_set(&sg_iov[2], dcpl_buf, (daos_size_t)dcpl_size);
    sgl[2].sg_nr.num = 1;
    sgl[2].sg_iovs = &sg_iov[2];

    /* Write internal metadata to dataset */
    if(0 != (ret = daos_obj_update(dset->obj.obj_oh, item->file->epoch, &dkey, 3,
        iod, sgl, &req->ev)))
        HGOTO_ERROR(H5E_DATASET, H5E_CANTINIT, FAIL, "can't write metadata to dataset: %d", ret)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_dset_update_metadata() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_dset_update_metadata_cb
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_dset_update_metadata_cb(H5VL_daos_req_t *req)
{
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    switch(req->req_type) {
        case H5VL_DAOS_DATASET_CREATE: {
            struct H5VL_daos_req_dset_open *req_dset_open = &req->req.dset_open;
            req_dset_open->type_buf = H5MM_xfree(req_dset_open->type_buf);
            req_dset_open->space_buf = H5MM_xfree(req_dset_open->space_buf);
            req_dset_open->dcpl_buf = H5MM_xfree(req_dset_open->dcpl_buf);
        }
            break;

        default:
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
            break;
    }

    req->completed = TRUE;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_dset_update_metadata_cb() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_sel_to_recx_iov
 *
 * Purpose:     Given a dataspace with a selection and the datatype
 *              (element) size, build a list of DAOS-M records (recxs)
 *              and/or scatter/gather list I/O vectors (sg_iovs). *recxs
 *              and *sg_iovs should, if requested, point to a (probably
 *              statically allocated) single element.  Does not release
 *              buffers on error.
 *
 * Return:      Success:        0
 *              Failure:        -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_sel_to_recx_iov(H5S_t *space, size_t type_size, void *buf,
    daos_recx_t **recxs, daos_iov_t **sg_iovs, size_t *list_nused)
{
    H5S_sel_iter_t sel_iter;    /* Selection iteration info */
    hbool_t sel_iter_init = FALSE;      /* Selection iteration info has been initialized */
    size_t nseq;
    size_t nelem;
    hsize_t off[H5VL_DAOS_SEQ_LIST_LEN];
    size_t len[H5VL_DAOS_SEQ_LIST_LEN];
    size_t buf_len = 1;
    void *vp_ret;
    size_t szi;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(recxs || sg_iovs);
    HDassert(!recxs || *recxs);
    HDassert(!sg_iovs || *sg_iovs);
    HDassert(list_nused);

    /* Initialize list_nused */
    *list_nused = 0;

    /* Initialize selection iterator  */
    if(H5S_select_iter_init(&sel_iter, space, (size_t)1) < 0)
        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINIT, FAIL, "unable to initialize selection iterator")
    sel_iter_init = TRUE;       /* Selection iteration info has been initialized */

    /* Generate sequences from the file space until finished */
    do {
        /* Get the sequences of bytes */
        if(H5S_SELECT_GET_SEQ_LIST(space, 0, &sel_iter, (size_t)H5VL_DAOS_SEQ_LIST_LEN, (size_t)-1, &nseq, &nelem, off, len) < 0)
            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTGET, FAIL, "sequence length generation failed")

        /* Make room for sequences in recxs */
        if((buf_len == 1) && (nseq > 1)) {
            if(recxs)
                if(NULL == (*recxs = (daos_recx_t *)H5MM_malloc(H5VL_DAOS_SEQ_LIST_LEN * sizeof(daos_recx_t))))
                    HGOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, FAIL, "can't allocate memory for records")
            if(sg_iovs)
                if(NULL == (*sg_iovs = (daos_iov_t *)H5MM_malloc(H5VL_DAOS_SEQ_LIST_LEN * sizeof(daos_iov_t))))
                    HGOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, FAIL, "can't allocate memory for sgl iovs")
            buf_len = H5VL_DAOS_SEQ_LIST_LEN;
        } /* end if */
        else if(*list_nused + nseq > buf_len) {
            if(recxs) {
                if(NULL == (vp_ret = H5MM_realloc(*recxs, 2 * buf_len * sizeof(daos_recx_t))))
                    HGOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, FAIL, "can't reallocate memory for records")
                *recxs = (daos_recx_t *)vp_ret;
            } /* end if */
            if(sg_iovs) {
                if(NULL == (vp_ret = H5MM_realloc(*sg_iovs, 2 * buf_len * sizeof(daos_iov_t))))
                    HGOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, FAIL, "can't reallocate memory for sgls")
                *sg_iovs = (daos_iov_t *)vp_ret;
            } /* end if */
            buf_len *= 2;
        } /* end if */
        HDassert(*list_nused + nseq <= buf_len);

        /* Copy offsets/lengths to recxs and sg_iovs */
        for(szi = 0; szi < nseq; szi++) {
            if(recxs) {
                (*recxs)[szi + *list_nused].rx_rsize = (uint64_t)type_size;
                (*recxs)[szi + *list_nused].rx_idx = (uint64_t)off[szi];
                (*recxs)[szi + *list_nused].rx_nr = (uint64_t)len[szi];
            } /* end if */
            if(sg_iovs)
                daos_iov_set(&(*sg_iovs)[szi + *list_nused],
                        (uint8_t *)buf + (off[szi] * type_size),
                        (daos_size_t)len[szi] * (daos_size_t)type_size);
        } /* end for */
        *list_nused += nseq;
    } while(nseq == H5VL_DAOS_SEQ_LIST_LEN);

done:
    /* Release selection iterator */
    if(sel_iter_init && H5S_SELECT_ITER_RELEASE(&sel_iter) < 0)
        HDONE_ERROR(H5E_DATASPACE, H5E_CANTRELEASE, FAIL, "unable to release selection iterator")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_sel_to_recx_iov() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_dset_rw_cb
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_dset_rw_cb(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_dset_rw *req_dset_rw = &req->req.dset_rw;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    HDassert(req);

    H5VL_DAOS_LOG_DEBUG("Here");

    if(req_dset_rw->recxs != &req_dset_rw->recx)
        req_dset_rw->recxs = H5MM_xfree(req_dset_rw->recxs);
    if(req_dset_rw->sg_iovs != &req_dset_rw->sg_iov)
        req_dset_rw->sg_iovs = H5MM_xfree(req_dset_rw->sg_iovs);

    req->completed = TRUE;

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_dset_rw_cb() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_dset_free
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_dset_free(H5VL_daos_req_t *req)
{
    struct H5VL_daos_req_dset_close *req_dset_close = &req->req.dset_close;
    H5VL_daos_dset_t *dset = req->req.dset_close.dset;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(req);

    H5VL_DAOS_LOG_DEBUG("Here");

    if(dset->type_id != FAIL && H5I_dec_app_ref(dset->type_id) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTDEC, FAIL, "failed to close datatype")
    if(dset->space_id != FAIL && H5I_dec_app_ref(dset->space_id) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTDEC, FAIL, "failed to close dataspace")
    if(dset->dcpl_id != FAIL && H5I_dec_app_ref(dset->dcpl_id) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTDEC, FAIL, "failed to close plist")
    if(dset->dapl_id != FAIL && H5I_dec_app_ref(dset->dapl_id) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTDEC, FAIL, "failed to close plist")
    req_dset_close->dset = H5FL_FREE(H5VL_daos_dset_t, dset);

    req->completed = TRUE;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_dset_free() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_context_poll_mpi
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static int
H5VL__daos_context_poll_mpi(H5VL_daos_context_t *context, unsigned int timeout,
    unsigned int max_reqs, H5VL_daos_req_t **reqs)
{
    H5VL_daos_req_t *daos_req;
    int ret_value = 0;
    unsigned int i_req = 0;
    double remaining = timeout / 1000.0; /* Convert timeout in ms into seconds */
    double t1, t2;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    if(H5_LIST_IS_EMPTY(&context->mpi_req_list))
        HGOTO_DONE(0);

    do {
        t1 = MPI_Wtime();

        daos_req = H5_LIST_FIRST(&context->mpi_req_list);
        while(daos_req && i_req < max_reqs) {
            MPI_Request *request = NULL;
            int flag = 0;
            MPI_Status *status = MPI_STATUS_IGNORE;

            /* If the op_id is marked as completed, something is wrong */
            if(daos_req->completed)
                HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Req should not have completed yet");

            switch(daos_req->op_type) {
                case H5VL_DAOS_POOL_CONNECT_SEND_GHDL:
                    request = (daos_req->req.pool_connect.ghdl_size_req
                        != MPI_REQUEST_NULL) ? &daos_req->req.pool_connect.ghdl_size_req
                            : &daos_req->req.pool_connect.ghdl_req;
                    break;
                case H5VL_DAOS_POOL_CONNECT_RECV_GHDL_SIZE:
                    request = &daos_req->req.pool_connect.ghdl_size_req;
                    break;
                case H5VL_DAOS_POOL_CONNECT_RECV_GHDL:
                    request = &daos_req->req.pool_connect.ghdl_req;
                    break;
                case H5VL_DAOS_CONT_OPEN_SEND_GHDL:
                    request = (daos_req->req.cont_open.ghdl_size_req
                        != MPI_REQUEST_NULL) ? &daos_req->req.cont_open.ghdl_size_req
                            : &daos_req->req.cont_open.ghdl_req;
                    break;
                case H5VL_DAOS_CONT_OPEN_RECV_GHDL_SIZE:
                    request = &daos_req->req.cont_open.ghdl_size_req;
                    break;
                case H5VL_DAOS_CONT_OPEN_RECV_GHDL:
                    request = &daos_req->req.cont_open.ghdl_req;
                    break;
                case H5VL_DAOS_EPOCH_BCAST:
                    request = &daos_req->req.cont_open.epoch_req;
                    break;
                case H5VL_DAOS_BARRIER:
                    request = &daos_req->req.cont_close.barrier_req;
                    break;

                default:
                    HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Unknown type of request");
                    break;
            }

            /* If request is MPI_REQUEST_NULL, the operation should be completed */
            if(!request || (request && (*request == MPI_REQUEST_NULL)))
                HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "NULL request");

            if(MPI_SUCCESS != MPI_Test(request, &flag, status))
                HGOTO_ERROR(H5E_VOL, H5E_MPI, FAIL, "MPI_Test() failed");

            if(!flag) {
                daos_req = H5_LIST_NEXT(daos_req, entry);
                continue;
            }

            *request = MPI_REQUEST_NULL;

            switch(daos_req->op_type) {
                case H5VL_DAOS_POOL_CONNECT_SEND_GHDL:
                    /* Remove entry from list */
                    if (daos_req->req.pool_connect.ghdl_size_req == MPI_REQUEST_NULL
                        && daos_req->req.pool_connect.ghdl_req == MPI_REQUEST_NULL) {
                        H5_LIST_REMOVE(daos_req, entry);
                        reqs[i_req] = daos_req;
                        i_req++;
                    }
                    break;
                case H5VL_DAOS_CONT_OPEN_SEND_GHDL:
                    /* Remove entry from list */
                    if (daos_req->req.cont_open.ghdl_size_req == MPI_REQUEST_NULL
                        && daos_req->req.cont_open.ghdl_req == MPI_REQUEST_NULL) {
                        H5_LIST_REMOVE(daos_req, entry);
                        reqs[i_req] = daos_req;
                        i_req++;
                    }
                    break;

                default:
                    /* Remove entry from list */
                    H5_LIST_REMOVE(daos_req, entry);
                    reqs[i_req] = daos_req;
                    i_req++;
                    break;
            }

            daos_req = H5_LIST_NEXT(daos_req, entry);
        }
        t2 = MPI_Wtime();
        remaining -= (t2 - t1);

        if(H5_LIST_IS_EMPTY(&context->mpi_req_list) || i_req == max_reqs)
            HGOTO_DONE((int)i_req);

    } while(remaining > 0);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_context_poll_mpi() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_context_poll_events
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static int
H5VL__daos_context_poll_events(H5VL_daos_context_t *context,
    unsigned int timeout, unsigned int max_reqs, H5VL_daos_req_t **reqs)
{
    int64_t timeout_us = timeout * 1000; /* Convert timeout ms to us */
    daos_event_t *daos_evps[H5VL_DAOS_MAX_EVENTS];
    unsigned int nevents = MIN(H5VL_DAOS_MAX_EVENTS, max_reqs);
    herr_t ret_value = SUCCEED;
    int ret, i;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    if((ret = daos_eq_poll(context->eq_handle, 0, timeout_us,
        nevents, daos_evps)) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "daos_eq_poll() failed: %d", ret);

    for(i = 0; i < ret; i++) {
        H5VL_daos_req_t *daos_req = container_of(daos_evps[i], H5VL_daos_req_t, ev);
        HDassert(&daos_req->ev == daos_evps[i]);

        /* If the op_id is marked as completed, something is wrong */
        if(daos_req->completed)
            HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "Req should not have completed yet");

        reqs[i] = daos_req;
    }

    ret_value = ret;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL__daos_context_poll_events() */

/*--------------------------------------------------------------------------
NAME
   H5VL__init_package -- Initialize interface-specific information
USAGE
    herr_t H5VL__init_package()

RETURNS
    Non-negative on success/Negative on failure
DESCRIPTION
    Initializes any interface-specific data or routines.  (Just calls
    H5VL_daos_init currently).

--------------------------------------------------------------------------*/
static herr_t
H5VL__init_package(void)
{
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_STATIC

    if(H5VL_daos_init() < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, FAIL, "unable to initialize DAOS VOL plugin")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* H5VL__init_package() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_daos_init
 *
 * Purpose:     Initialize this vol plugin by registering the driver with the
 *              library.
 *
 * Return:      Success:        The ID for the DAOS plugin.
 *              Failure:        Negative.
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5VL_daos_init(void)
{
    hid_t ret_value = H5I_INVALID_HID;            /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Register the DAOS VOL, if it isn't already */
    if(NULL == H5I_object_verify(H5VL_DAOS_g, H5I_VOL)) {
        if((H5VL_DAOS_g = H5VL_register((const H5VL_class_t *)&H5VL_daos_g,
                                          sizeof(H5VL_class_t), TRUE)) < 0)
            HGOTO_ERROR(H5E_ATOM, H5E_CANTINSERT, FAIL, "can't create ID for DAOS plugin")
    }

    /* Set return value */
    ret_value = H5VL_DAOS_g;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_daos_init() */

/*---------------------------------------------------------------------------
 * Function:    H5VL__daos_term
 *
 * Purpose:     Shut down the DAOS VOL
 *
 * Returns:     SUCCEED (Can't fail)
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL__daos_term(void)
{
    FUNC_ENTER_STATIC_NOERR

    /* Reset VOL ID */
    H5VL_DAOS_g = 0;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5VL__daos_term() */

/*-------------------------------------------------------------------------
 * Function:    H5Pset_fapl_daos
 *
 * Purpose:     Modify the file access property list to use the H5VL_DAOS
 *              plugin defined in this source file.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Pset_fapl_daos(hid_t fapl_id, MPI_Comm comm, MPI_Info info,
    const char *pool_uuid, const char *pool_grp)
{
    H5VL_daos_fapl_t fa;
    H5P_genplist_t  *plist;      /* Property list pointer */
    herr_t          ret_value;

    FUNC_ENTER_API(FAIL)

    if(fapl_id == H5P_DEFAULT)
        HGOTO_ERROR(H5E_PLIST, H5E_BADVALUE, FAIL, "can't set values in default property list");
    if(NULL == (plist = H5P_object_verify(fapl_id, H5P_FILE_ACCESS)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a file access property list");
    if(MPI_COMM_NULL == comm)
        HGOTO_ERROR(H5E_PLIST, H5E_BADTYPE, FAIL, "not a valid communicator");
    if(!pool_uuid)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "pool_uuid cannot be NULL");

    /* Initialize driver specific properties */
    fa.comm = comm;
    fa.info = info;
    if(NULL == (fa.pool_uuid = HDstrdup(pool_uuid)))
        HGOTO_ERROR(H5E_PLIST, H5E_NOSPACE, FAIL, "can't copy pool uuid")
    if(pool_grp) {
        if(NULL == (fa.pool_grp = HDstrdup(pool_grp)))
            HGOTO_ERROR(H5E_PLIST, H5E_NOSPACE, FAIL, "can't copy pool group")
    } /* end if */
    else
        fa.pool_grp = NULL;

    ret_value = H5P_set_vol(plist, H5VL_DAOS, &fa);

    H5VL_DAOS_LOG_DEBUG("Here");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Pset_fapl_daos() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_daos_fapl_copy
 *
 * Purpose:     Copies the DAOS-specific file access properties.
 *
 * Return:      Success:        Ptr to a new property list
 *              Failure:        NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_daos_fapl_copy(const void *_old_fa)
{
    const H5VL_daos_fapl_t *old_fa = (const H5VL_daos_fapl_t*) _old_fa;
    H5VL_daos_fapl_t *new_fa = NULL;
    void *ret_value = NULL;

    FUNC_ENTER_NOAPI_NOINIT

    if(NULL == (new_fa = (H5VL_daos_fapl_t *)H5MM_malloc(sizeof(H5VL_daos_fapl_t))))
        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "memory allocation failed");

    /* Copy the general information */
    HDmemcpy(new_fa, old_fa, sizeof(H5VL_daos_fapl_t));

    /* Clear allocated fields, so they aren't freed if something goes wrong.  No
     * need to clear info since it is only freed if comm is not null. */
    new_fa->comm = MPI_COMM_NULL;
    new_fa->pool_uuid = NULL;
    new_fa->pool_grp = NULL;

    /* Duplicate communicator and Info object. */
    if(FAIL == H5FD_mpi_comm_info_dup(old_fa->comm, old_fa->info, &new_fa->comm, &new_fa->info))
        HGOTO_ERROR(H5E_INTERNAL, H5E_CANTCOPY, NULL, "Communicator/Info duplicate failed");

    /*  Duplicate the pool uuid */
    if(NULL == (new_fa->pool_uuid = HDstrdup(old_fa->pool_uuid)))
        HGOTO_ERROR(H5E_PLIST, H5E_NOSPACE, NULL, "can't copy pool uuid");

    /*  Duplicate the pool group */
    if(old_fa->pool_grp) {
        if(NULL == (new_fa->pool_grp = HDstrdup(old_fa->pool_grp)))
            HGOTO_ERROR(H5E_PLIST, H5E_NOSPACE, NULL, "can't copy pool group");
    } /* end if */
    else
        new_fa->pool_grp = NULL;

    ret_value = new_fa;

done:
    if (NULL == ret_value) {
        /* cleanup */
        if(new_fa && H5VL_daos_fapl_free(new_fa) < 0)
            HDONE_ERROR(H5E_PLIST, H5E_CANTFREE, NULL, "can't free fapl")
    } /* end if */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_daos_fapl_copy() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_daos_fapl_free
 *
 * Purpose:     Frees the DAOS-specific file access properties.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_daos_fapl_free(void *_fa)
{
    herr_t              ret_value = SUCCEED;
    H5VL_daos_fapl_t   *fa = (H5VL_daos_fapl_t*)_fa;

    FUNC_ENTER_NOAPI_NOINIT

    assert(fa);

    /* Free the internal communicator and INFO object */
    if(fa->comm != MPI_COMM_NULL)
        if(H5FD_mpi_comm_info_free(&fa->comm, &fa->info) < 0)
            HGOTO_ERROR(H5E_INTERNAL, H5E_CANTFREE, FAIL, "Communicator/Info free failed");

    /* Free the pool uuid/group */
    HDfree(fa->pool_uuid);
    HDfree(fa->pool_grp);

    /* free the struct */
    H5MM_xfree(fa);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_daos_fapl_free() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_daos_file_create
 *
 * Purpose:     Creates a file as a DAOS HDF5 file.
 *
 * Return:      Success:        the file id. 
 *              Failure:        NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_daos_file_create(const char *name, unsigned flags, hid_t fcpl_id,
    hid_t fapl_id, hid_t H5_ATTR_UNUSED dxpl_id, void **reqp)
{
    H5VL_daos_req_t **daos_reqp = (H5VL_daos_req_t **) reqp;
    H5VL_daos_req_t *req = NULL;
    H5VL_daos_fapl_t *fa = NULL;
    H5P_genplist_t *plist = NULL;      /* Property list pointer */
    H5VL_daos_file_t *file = NULL;
    void *ret_value = NULL;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    HDassert(name);
    HDassert(daos_reqp);

    req = *daos_reqp;
    HDassert(req);
    req->reqp = reqp;

    /*
     * Adjust bit flags by turning on the creation bit and making sure that
     * the EXCL or TRUNC bit is set.  All newly-created files are opened for
     * reading and writing.
     */
    if(0 == (flags & (H5F_ACC_EXCL|H5F_ACC_TRUNC)))
        flags |= H5F_ACC_EXCL; /*default*/
    flags |= H5F_ACC_RDWR | H5F_ACC_CREAT;

    /* Get information from the FAPL */
    if(NULL == (plist = H5P_object_verify(fapl_id, H5P_FILE_ACCESS)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, NULL, "not a file access property list");
    if(NULL == (fa = (H5VL_daos_fapl_t *)H5P_get_vol_info(plist)))
        HGOTO_ERROR(H5E_SYM, H5E_CANTGET, NULL, "can't get DAOS info struct");

    /* Initialize file */
    if(NULL == (file = H5VL__daos_file_init(name, flags, fa)))
        HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, NULL, "can't init DAOS file struct");
    if((file->fcpl_id = H5Pcopy(fcpl_id)) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTCOPY, NULL, "failed to copy fcpl");
    if((file->fapl_id = H5Pcopy(fapl_id)) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTCOPY, NULL, "failed to copy fapl");

    /* Set request type */
    req->req_type = H5VL_DAOS_FILE_CREATE;

    /* TODO: move pool handling to startup/shutdown routines DSMINC */
    req->req.pool_connect.file = file;
    req->req.pool_connect.fa = fa;
    if(FAIL == H5VL__daos_pool_connect(req))
        HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, NULL, "can't connect to pool");

    ret_value = (void *)file;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_daos_file_create() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_daos_file_open
 *
 * Purpose:     Opens a file as a DAOS HDF5 file.
 *
 * Return:      Success:        the file id.
 *              Failure:        NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_daos_file_open(const char H5_ATTR_UNUSED *name, unsigned H5_ATTR_UNUSED flags, hid_t H5_ATTR_UNUSED fapl_id,
    hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req)
{
    void *ret_value = NULL;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_daos_file_open() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_daos_file_specific
 *
 * Purpose:     Perform an operation
 *
 * Return:      Success:        0
 *              Failure:        -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_daos_file_specific(void *item, H5VL_file_specific_t specific_type,
    hid_t H5_ATTR_UNUSED dxpl_id, void **reqp, va_list H5_ATTR_UNUSED arguments)
{
    H5VL_daos_file_t *file = ((H5VL_daos_item_t *)item)->file;
    H5VL_daos_req_t **daos_reqp = (H5VL_daos_req_t **) reqp;
    H5VL_daos_req_t *req = NULL;
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    HDassert(daos_reqp);
    HDassert(file);

    req = *daos_reqp;
    HDassert(req);
    req->reqp = reqp;

    switch (specific_type) {
        /* H5Fflush` */
        case H5VL_FILE_FLUSH:
            /* Set request type */
            req->req_type = H5VL_DAOS_FILE_FLUSH;
            req->req.cont_flush.file = file;

            /* Nothing to do if no write intent */
            if(!(file->flags & H5F_ACC_RDWR)) {
                req->completed = TRUE;
                HGOTO_DONE(SUCCEED);
            }

            if(H5VL__daos_file_flush(req) < 0)
                HGOTO_ERROR(H5E_FILE, H5E_WRITEERROR, FAIL, "can't flush file")

            break;
        /* H5Fmount */
        case H5VL_FILE_MOUNT:
        /* H5Fmount */
        case H5VL_FILE_UNMOUNT:
        /* H5Fis_accessible */
        case H5VL_FILE_IS_ACCESSIBLE:
        default:
            HGOTO_ERROR(H5E_VOL, H5E_UNSUPPORTED, FAIL, "invalid or unsupported specific operation")
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_daos_file_specific() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_daos_file_close
 *
 * Purpose:     Closes a DAOS HDF5 file, committing the epoch if
 *              appropriate.
 *
 * Return:      Success:        the file id.
 *              Failure:        NULL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_daos_file_close(void *_file, hid_t H5_ATTR_UNUSED dxpl_id, void **reqp)
{
    H5VL_daos_file_t *file = (H5VL_daos_file_t *)_file;
    H5VL_daos_req_t **daos_reqp = (H5VL_daos_req_t **) reqp;
    H5VL_daos_req_t *req = NULL;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    HDassert(daos_reqp);
    HDassert(file);

    req = *daos_reqp;
    HDassert(req);
    req->reqp = reqp;

    /* Set request type */
    req->req_type = H5VL_DAOS_FILE_CLOSE;
    req->req.cont_close.file = file;

    /* Nothing to do if no write intent */
    /* Flush the file (barrier, commit epoch, slip epoch) */
    if(file->flags & H5F_ACC_RDWR) {
        if(H5VL__daos_file_flush(req) < 0)
            HGOTO_ERROR(H5E_FILE, H5E_WRITEERROR, FAIL, "can't flush file");
    } else {
        /* Close the file */
        if(H5VL__daos_file_close(req) < 0)
            HGOTO_ERROR(H5E_FILE, H5E_CANTCLOSEFILE, FAIL, "can't close file");
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_daos_file_close() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_daos_dataset_create
 *
 * Purpose:     Sends a request to DAOS-M to create a dataset
 *
 * Return:      Success:        dataset object.
 *              Failure:        NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_daos_dataset_create(void *_item,
    H5VL_loc_params_t H5_ATTR_UNUSED loc_params, const char *name,
    hid_t dcpl_id, hid_t dapl_id, hid_t H5_ATTR_UNUSED dxpl_id, void **reqp)
{
    H5VL_daos_item_t *item = (H5VL_daos_item_t *)_item;
    H5VL_daos_dset_t *dset = NULL;
    H5P_genplist_t *plist = NULL;      /* Property list pointer */
    hid_t type_id, space_id;
    H5VL_daos_group_t *target_grp = NULL;
    char *target_name = NULL;
    H5VL_daos_req_t **daos_reqp = (H5VL_daos_req_t **) reqp;
    H5VL_daos_req_t *req = NULL;
    void *ret_value = NULL;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    HDassert(daos_reqp);
    HDassert(item);

    req = *daos_reqp;
    HDassert(req);
    req->reqp = reqp;

    /* Check for write access */
    if(!(item->file->flags & H5F_ACC_RDWR))
        HGOTO_ERROR(H5E_FILE, H5E_BADVALUE, NULL, "no write intent on file")

    /* Get the dcpl plist structure */
    if(NULL == (plist = (H5P_genplist_t *)H5I_object(dcpl_id)))
        HGOTO_ERROR(H5E_ATOM, H5E_BADATOM, NULL, "can't find object for ID")

    /* get creation properties */
    if(H5P_get(plist, H5VL_PROP_DSET_TYPE_ID, &type_id) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, NULL, "can't get property value for datatype id")
    if(H5P_get(plist, H5VL_PROP_DSET_SPACE_ID, &space_id) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, NULL, "can't get property value for space id")

    /* Traverse the path */
//    if(NULL == (target_grp = H5VL_daos_group_traverse(item, name, dxpl_id, req, &target_name)))
//        HGOTO_ERROR(H5E_SYM, H5E_BADITER, NULL, "can't traverse path")
    /* Open starting group */
    /* TODO for now until group traverse if fixed */
    target_grp = item->file->root_grp;
    target_name = HDstrdup(name);

    /* Init dataset */
    if(NULL == (dset = H5VL__daos_dset_init(item)))
        HGOTO_ERROR(H5E_VOL, H5E_CANTINIT, NULL, "can't init DAOS dset");

    /* Finish setting up dataset struct */
    if((dset->type_id = H5Tcopy(type_id)) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTCOPY, NULL, "failed to copy datatype");
    if((dset->space_id = H5Scopy(space_id)) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTCOPY, NULL, "failed to copy dataspace");
    if(H5Sselect_all(dset->space_id) < 0)
        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTDELETE, NULL, "can't change selection");
    if((dset->dcpl_id = H5Pcopy(dcpl_id)) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTCOPY, NULL, "failed to copy dcpl");
    if((dset->dapl_id = H5Pcopy(dapl_id)) < 0)
        HGOTO_ERROR(H5E_SYM, H5E_CANTCOPY, NULL, "failed to copy dapl");

    /* Set request type */
    req->req_type = H5VL_DAOS_DATASET_CREATE;
    req->req.dset_open.item = item;
    req->req.dset_open.dset = dset;
    req->req.dset_open.target_grp = target_grp;
    req->req.dset_open.target_name = target_name;

    /* Create dataset */
    if(FAIL == H5VL__daos_dset_create(req))
        HGOTO_ERROR(H5E_VOL, H5E_CANTCREATE, NULL, "can't create dataset");

    ret_value = (void *)dset;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_daos_dataset_create() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_daos_dataset_open
 *
 * Purpose:     Sends a request to DAOS to open a dataset
 *
 * Return:      Success:        dataset object.
 *              Failure:        NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_daos_dataset_open(void H5_ATTR_UNUSED *_item,
    H5VL_loc_params_t H5_ATTR_UNUSED loc_params,
    const char H5_ATTR_UNUSED *name, hid_t H5_ATTR_UNUSED dapl_id,
    hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req)
{
    void *ret_value = NULL;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_daos_dataset_open() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_daos_dataset_read
 *
 * Purpose:     Reads raw data from a dataset into a buffer.
 *`
 * Return:      Success:        0
 *              Failure:        -1, dataset not read.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_daos_dataset_read(void *_dset, hid_t H5_ATTR_UNUSED mem_type_id,
    hid_t mem_space_id, hid_t file_space_id, hid_t H5_ATTR_UNUSED dxpl_id,
    void *buf, void **reqp)
{
    H5VL_daos_dset_t *dset = (H5VL_daos_dset_t *)_dset;
    int ndims;
    hsize_t dim[H5S_MAX_RANK];
    H5S_t *space = NULL;
    uint64_t chunk_coords[H5S_MAX_RANK];
    daos_key_t dkey;
    daos_vec_iod_t iod;
    daos_recx_t *recxs = NULL;
    daos_sg_list_t sgl;
    daos_iov_t *sg_iovs = NULL;
    size_t tot_nseq;
    uint8_t dkey_buf[1 + H5S_MAX_RANK];
    uint8_t akey = H5VL_DAOS_CHUNK_KEY;
    size_t type_size;
    uint8_t *p;
    H5VL_daos_req_t **daos_reqp = (H5VL_daos_req_t **) reqp;
    H5VL_daos_req_t *req = NULL;
    int ret, i;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    HDassert(daos_reqp);
    HDassert(dset);

    req = *daos_reqp;
    HDassert(req);
    req->reqp = reqp;

    /* Set request type */
    req->req_type = H5VL_DAOS_DATASET_READ;
    recxs = &req->req.dset_rw.recx;
    sg_iovs = &req->req.dset_rw.sg_iov;

    /* Current operation */
    req->op_type = H5VL_DAOS_DSET_READ;

    /* Next callback */
    req->cb = H5VL__daos_dset_rw_cb;

    /* Get dataspace extent */
    if((ndims = H5Sget_simple_extent_ndims(dset->space_id)) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "can't get number of dimensions")
    if(ndims != H5Sget_simple_extent_dims(dset->space_id, dim, NULL))
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "can't get dimensions")

    /* Get datatype size */
    if((type_size = H5Tget_size(dset->type_id)) == 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "can't get datatype size")

    /* Encode dkey (chunk coordinates).  Prefix with '/' to avoid accidental
     * collisions with other d-keys in this object.  For now just 1 chunk,
     * starting at 0. */
    HDmemset(chunk_coords, 0, sizeof(chunk_coords)); //DSMINC
    p = dkey_buf;
    *p++ = (uint8_t)'/';
    for(i = 0; i < ndims; i++)
        UINT64ENCODE(p, chunk_coords[i])

    /* Set up dkey */
    daos_iov_set(&dkey, dkey_buf, (daos_size_t)(1 + ((size_t)ndims * sizeof(chunk_coords[0]))));

    /* Set up iod */
    HDmemset(&iod, 0, sizeof(iod));
    daos_iov_set(&iod.vd_name, (void *)&akey, (daos_size_t)(sizeof(akey)));
    daos_csum_set(&iod.vd_kcsum, NULL, 0);

    /* Build recxs and sg_iovs */
    /* Get file dataspace object */
    if(NULL == (space = (H5S_t *)H5I_object((file_space_id == H5S_ALL)
            ? dset->space_id : file_space_id)))
        HGOTO_ERROR(H5E_ATOM, H5E_BADATOM, FAIL, "can't find object for ID");

    /* Check for memory space is H5S_ALL, use file space in this case */
    if(mem_space_id == H5S_ALL) {
        /* Calculate both recxs and sg_iovs at the same time from file space */
        if(H5VL__daos_sel_to_recx_iov(space, type_size, buf, &recxs, &sg_iovs, &tot_nseq) < 0)
            HGOTO_ERROR(H5E_DATASET, H5E_CANTINIT, FAIL, "can't generate sequence lists for DAOS I/O")
        iod.vd_nr = (unsigned)tot_nseq;
        sgl.sg_nr.num = (uint32_t)tot_nseq;
    } /* end if */
    else {
        /* Calculate recxs from file space */
        if(H5VL__daos_sel_to_recx_iov(space, type_size, buf, &recxs, NULL, &tot_nseq) < 0)
            HGOTO_ERROR(H5E_DATASET, H5E_CANTINIT, FAIL, "can't generate sequence lists for DAOS I/O")
        iod.vd_nr = (unsigned)tot_nseq;

        /* Get memory dataspace object */
        if(NULL == (space = (H5S_t *)H5I_object(mem_space_id)))
            HGOTO_ERROR(H5E_ATOM, H5E_BADATOM, FAIL, "can't find object for ID");

        /* Calculate sg_iovs from mem space */
        if(H5VL__daos_sel_to_recx_iov(space, type_size, buf, NULL, &sg_iovs, &tot_nseq) < 0)
            HGOTO_ERROR(H5E_DATASET, H5E_CANTINIT, FAIL, "can't generate sequence lists for DAOS I/O")
        sgl.sg_nr.num = (uint32_t)tot_nseq;
    } /* end else */

    /* Point iod and sgl to lists generated above */
    iod.vd_recxs = recxs;
    sgl.sg_iovs = sg_iovs;
    req->req.dset_rw.recxs = recxs;
    req->req.dset_rw.sg_iovs = sg_iovs;

    /* Read data from dataset */
    if(0 != (ret = daos_obj_fetch(dset->obj.obj_oh, dset->obj.item.file->epoch,
        &dkey, 1, &iod, &sgl, NULL /*maps*/, &req->ev)))
        HGOTO_ERROR(H5E_DATASET, H5E_READERROR, FAIL, "can't read data from dataset: %d", ret)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_daos_dataset_read() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_daos_dataset_write
 *
 * Purpose:     Writes raw data from a buffer into a dataset.
 *
 * Return:      Success:        0
 *              Failure:        -1, dataset not written.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_daos_dataset_write(void *_dset, hid_t H5_ATTR_UNUSED mem_type_id,
    hid_t mem_space_id, hid_t file_space_id, hid_t H5_ATTR_UNUSED dxpl_id,
    const void *buf, void **reqp)
{
    H5VL_daos_dset_t *dset = (H5VL_daos_dset_t *)_dset;
    int ndims;
    hsize_t dim[H5S_MAX_RANK];
    H5S_t *space = NULL;
    uint64_t chunk_coords[H5S_MAX_RANK];
    daos_key_t dkey;
    daos_vec_iod_t iod;
    daos_recx_t *recxs = NULL;
    daos_sg_list_t sgl;
    daos_iov_t *sg_iovs = NULL;
    size_t tot_nseq;
    uint8_t dkey_buf[1 + H5S_MAX_RANK];
    uint8_t akey = H5VL_DAOS_CHUNK_KEY;
    size_t type_size;
    uint8_t *p;
    H5VL_daos_req_t **daos_reqp = (H5VL_daos_req_t **) reqp;
    H5VL_daos_req_t *req = NULL;
    int ret, i;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    HDassert(daos_reqp);
    HDassert(dset);

    req = *daos_reqp;
    HDassert(req);
    req->reqp = reqp;

    /* Check for write access */
    if(!(dset->obj.item.file->flags & H5F_ACC_RDWR))
        HGOTO_ERROR(H5E_FILE, H5E_BADVALUE, FAIL, "no write intent on file");

    /* Set request type */
    req->req_type = H5VL_DAOS_DATASET_WRITE;
    recxs = &req->req.dset_rw.recx;
    sg_iovs = &req->req.dset_rw.sg_iov;

    /* Current operation */
    req->op_type = H5VL_DAOS_DSET_WRITE;

    /* Next callback */
    req->cb = H5VL__daos_dset_rw_cb;

    /* Get dataspace extent */
    if((ndims = H5Sget_simple_extent_ndims(dset->space_id)) < 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "can't get number of dimensions")
    if(ndims != H5Sget_simple_extent_dims(dset->space_id, dim, NULL))
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "can't get dimensions")

    /* Get datatype size */
    if((type_size = H5Tget_size(dset->type_id)) == 0)
        HGOTO_ERROR(H5E_DATASET, H5E_CANTGET, FAIL, "can't get datatype size")

    /* Encode dkey (chunk coordinates).  Prefix with '/' to avoid accidental
     * collisions with other d-keys in this object.  For now just 1 chunk,
     * starting at 0. */
    HDmemset(chunk_coords, 0, sizeof(chunk_coords)); //DSMINC
    p = dkey_buf;
    *p++ = (uint8_t)'/';
    for(i = 0; i < ndims; i++)
        UINT64ENCODE(p, chunk_coords[i])

    /* Set up dkey */
    daos_iov_set(&dkey, dkey_buf, (daos_size_t)(1 + ((size_t)ndims * sizeof(chunk_coords[0]))));

    /* Set up iod */
    HDmemset(&iod, 0, sizeof(iod));
    daos_iov_set(&iod.vd_name, (void *)&akey, (daos_size_t)(sizeof(akey)));
    daos_csum_set(&iod.vd_kcsum, NULL, 0);

    /* Build recxs and sg_iovs */
    /* Get file dataspace object */
    if(NULL == (space = (H5S_t *)H5I_object((file_space_id == H5S_ALL)
            ? dset->space_id : file_space_id)))
        HGOTO_ERROR(H5E_ATOM, H5E_BADATOM, FAIL, "can't find object for ID");

    /* Check for memory space is H5S_ALL, use file space in this case */
    if(mem_space_id == H5S_ALL) {
        /* Calculate both recxs and sg_iovs at the same time from file space */
        if(H5VL__daos_sel_to_recx_iov(space, type_size, (void *)buf, &recxs, &sg_iovs, &tot_nseq) < 0)
            HGOTO_ERROR(H5E_DATASET, H5E_CANTINIT, FAIL, "can't generate sequence lists for DAOS I/O")
        iod.vd_nr = (unsigned)tot_nseq;
        sgl.sg_nr.num = (uint32_t)tot_nseq;
    } /* end if */
    else {
        /* Calculate recxs from file space */
        if(H5VL__daos_sel_to_recx_iov(space, type_size, (void *)buf, &recxs, NULL, &tot_nseq) < 0)
            HGOTO_ERROR(H5E_DATASET, H5E_CANTINIT, FAIL, "can't generate sequence lists for DAOS I/O")
        iod.vd_nr = (unsigned)tot_nseq;

        /* Get memory dataspace object */
        if(NULL == (space = (H5S_t *)H5I_object(mem_space_id)))
            HGOTO_ERROR(H5E_ATOM, H5E_BADATOM, FAIL, "can't find object for ID");

        /* Calculate sg_iovs from mem space */
        if(H5VL__daos_sel_to_recx_iov(space, type_size, (void *)buf, NULL, &sg_iovs, &tot_nseq) < 0)
            HGOTO_ERROR(H5E_DATASET, H5E_CANTINIT, FAIL, "can't generate sequence lists for DAOS I/O")
        sgl.sg_nr.num = (uint32_t)tot_nseq;
    } /* end else */

    /* Point iod and sgl to lists generated above */
    iod.vd_recxs = recxs;
    sgl.sg_iovs = sg_iovs;
    req->req.dset_rw.recxs = recxs;
    req->req.dset_rw.sg_iovs = sg_iovs;

    /* Write data to dataset */
    if(0 != (ret = daos_obj_update(dset->obj.obj_oh, dset->obj.item.file->epoch,
        &dkey, 1, &iod, &sgl, &req->ev)))
        HGOTO_ERROR(H5E_DATASET, H5E_WRITEERROR, FAIL, "can't write data to dataset: %d", ret)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_daos_dataset_write() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_daos_dataset_get
 *
 * Purpose:     Gets certain information about a dataset
 *
 * Return:      Success:        0
 *              Failure:        -1
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VL_daos_dataset_get(void *_dset, H5VL_dataset_get_t get_type,
    hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req, va_list arguments)
{
    H5VL_daos_dset_t *dset = (H5VL_daos_dset_t *)_dset;
    herr_t       ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    switch (get_type) {
        case H5VL_DATASET_GET_DCPL:
            {
                hid_t *plist_id = va_arg(arguments, hid_t *);

                /* Retrieve the file's access property list */
                if((*plist_id = H5Pcopy(dset->dcpl_id)) < 0)
                    HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get dset creation property list")

                break;
            }
        case H5VL_DATASET_GET_DAPL:
            {
                hid_t *plist_id = va_arg(arguments, hid_t *);

                /* Retrieve the file's access property list */
                if((*plist_id = H5Pcopy(dset->dapl_id)) < 0)
                    HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get dset access property list")

                break;
            }
        case H5VL_DATASET_GET_SPACE:
            {
                hid_t *ret_id = va_arg(arguments, hid_t *);

                if((*ret_id = H5Scopy(dset->space_id)) < 0)
                    HGOTO_ERROR(H5E_ARGS, H5E_CANTGET, FAIL, "can't get dataspace ID of dataset");
                break;
            }
        case H5VL_DATASET_GET_SPACE_STATUS:
            {
                H5D_space_status_t *allocation = va_arg(arguments, H5D_space_status_t *);

                *allocation = H5D_SPACE_STATUS_NOT_ALLOCATED;
                break;
            }
        case H5VL_DATASET_GET_TYPE:
            {
                hid_t *ret_id = va_arg(arguments, hid_t *);

                if((*ret_id = H5Tcopy(dset->type_id)) < 0)
                    HGOTO_ERROR(H5E_ARGS, H5E_CANTGET, FAIL, "can't get datatype ID of dataset")
                break;
            }
        case H5VL_DATASET_GET_STORAGE_SIZE:
        case H5VL_DATASET_GET_OFFSET:
        default:
            HGOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "can't get this type of information from dataset")
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_daos_dataset_get() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_daos_dataset_close
 *
 * Purpose:     Closes a DAOS HDF5 dataset.
 *
 * Return:      Success:        0
 *              Failure:        -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_daos_dataset_close(void *_dset, hid_t H5_ATTR_UNUSED dxpl_id,
    void **reqp)
{
    H5VL_daos_dset_t *dset = (H5VL_daos_dset_t *)_dset;
    H5VL_daos_req_t **daos_reqp = (H5VL_daos_req_t **) reqp;
    H5VL_daos_req_t *req = NULL;
    int ret;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    HDassert(daos_reqp);
    HDassert(dset);

    req = *daos_reqp;
    HDassert(req);
    req->reqp = reqp;

    if(--dset->obj.item.rc == 0) {
        /* Set request type */
        req->req_type = H5VL_DAOS_DATASET_CLOSE;
        req->req.dset_close.dset = dset;

        /* Current operation */
        req->op_type = H5VL_DAOS_DSET_CLOSE;

        /* Next callback */
        req->cb = H5VL__daos_dset_free;

        /* Free dataset data structures */
        if(!daos_handle_is_inval(dset->obj.obj_oh))
            if(0 != (ret = daos_obj_close(dset->obj.obj_oh, &req->ev)))
                HDONE_ERROR(H5E_DATASET, H5E_CANTCLOSEOBJ, FAIL, "can't close dataset DAOS object: %d", ret)
    } /* end if */


    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_daos_dataset_close() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_daos_context_create
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_daos_context_create(void)
{
    H5VL_daos_context_t *context;
    void *ret_value = NULL;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    H5VL_DAOS_LOG_DEBUG("Here");

    if(NULL == (context = H5MM_malloc(sizeof(H5VL_daos_context_t))))
        HGOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, NULL, "can't allocate daos context")
    if(0 != (ret = daos_eq_create(&context->eq_handle)))
        HGOTO_ERROR(H5E_VOL, H5E_CANTCREATE, NULL, "can't create event queue: %d", ret)
    H5_LIST_INIT(&context->mpi_req_list);

    ret_value = context;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_daos_context_create() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_daos_context_close
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_daos_context_close(void *context)
{
    H5VL_daos_context_t *daos_context = (H5VL_daos_context_t *) context;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(daos_context);

    if(!H5_LIST_IS_EMPTY(&daos_context->mpi_req_list))
        HGOTO_ERROR(H5E_VOL, H5E_BADVALUE, FAIL, "MPI request list is not empty");
    if(0 != (ret = daos_eq_destroy(daos_context->eq_handle, 0)))
        HGOTO_ERROR(H5E_VOL, H5E_CANTCLOSEOBJ, FAIL, "can't destroy event queue: %d", ret);
    H5MM_free(daos_context);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_daos_context_close() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_daos_request_create
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_daos_request_create(void *context)
{
    H5VL_daos_context_t *daos_context = (H5VL_daos_context_t *) context;
    H5VL_daos_req_t *daos_req;
    void *ret_value = NULL;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(daos_context);

    H5VL_DAOS_LOG_DEBUG("Here");

    if(NULL == (daos_req = H5FL_CALLOC(H5VL_daos_req_t)))
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTALLOC, NULL, "can't allocate DAOS request struct");
    if(0 != (ret = daos_event_init(&daos_req->ev, daos_context->eq_handle, NULL)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTINIT, NULL, "can't init daos event: %d", ret);
    daos_req->context = daos_context;

    ret_value = daos_req;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_daos_request_create() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_daos_request_close
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_daos_request_close(void H5_ATTR_UNUSED *context, void *req)
{
    H5VL_daos_req_t *daos_req = (H5VL_daos_req_t *) req;
    herr_t ret_value = SUCCEED;
    int ret;

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(daos_req);

    if(0 != (ret = daos_event_fini(&daos_req->ev)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTCLOSEOBJ, FAIL, "can't finalize daos event: %d", ret);
    daos_req = H5FL_FREE(H5VL_daos_req_t, daos_req);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_daos_request_close() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__daos_context_poll
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static int
H5VL_daos_context_poll(void *context, unsigned int timeout,
    unsigned int max_reqs, void **reqs)
{
    H5VL_daos_req_t *poll_reqs[H5VL_DAOS_MAX_EVENTS];
    unsigned int npoll = MIN(H5VL_DAOS_MAX_EVENTS, max_reqs);
    double remaining = timeout / 1000.0; /* Convert timeout in ms into seconds */
    double t1, t2;
    unsigned int i_req = 0;
    int ret_value = 0;

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(context);

    H5VL_DAOS_LOG_DEBUG("Here");

    do {
        hbool_t completed = FALSE;
        int nreqs = 0, i;

        t1 = MPI_Wtime();

        /* Progress MPI functions in priority */
        if(FAIL == (nreqs = H5VL__daos_context_poll_mpi(context, timeout, npoll, poll_reqs)))
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "can't poll MPI requests");
        if(nreqs > 0)
            H5VL_DAOS_LOG_DEBUG("%d requests completed", nreqs);
        for(i = 0; i < nreqs; i++) {
            H5VL_DAOS_LOG_DEBUG("Completed req: %s", H5VL__daos_op_type_str(poll_reqs[i]->op_type));
            /* Execute callbacks */
            H5VL_DAOS_LOG_DEBUG("Executing cb %d", i);
            if(FAIL == poll_reqs[i]->cb(poll_reqs[i]))
                HGOTO_ERROR(H5E_VOL, H5E_CANTOPERATE, FAIL, "cannot execute callback");
            if(poll_reqs[i]->completed) {
                completed |= TRUE;
                reqs[i_req] = poll_reqs[i]->reqp;
                i_req++;
                if(i_req > max_reqs)
                    break;
            }
        }

        if(completed)
            break;
        if(nreqs)
            continue;

        if(FAIL == (nreqs = H5VL__daos_context_poll_events(context, timeout, npoll, poll_reqs)))
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "can't poll DAOS events");
        if(nreqs > 0)
            H5VL_DAOS_LOG_DEBUG("%d requests completed", nreqs);
        for(i = 0; i < nreqs; i++) {
            H5VL_DAOS_LOG_DEBUG("Completed req: %s", H5VL__daos_op_type_str(poll_reqs[i]->op_type));
            /* Execute callbacks */
            H5VL_DAOS_LOG_DEBUG("Executing cb %d", i);
            if(FAIL == poll_reqs[i]->cb(poll_reqs[i]))
                HGOTO_ERROR(H5E_VOL, H5E_CANTOPERATE, FAIL, "cannot execute callback");
            if(poll_reqs[i]->completed) {
                completed |= TRUE;
                reqs[i_req] = poll_reqs[i]->reqp;
                i_req++;
                if(i_req > max_reqs)
                    break;
            }
        }

        if(completed)
            break;

        t2 = MPI_Wtime();
        remaining -= (t2 - t1);
    } while(remaining > 0);

    ret_value = (int)i_req;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_daos_context_poll() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_daos_request_cancel
 *
 * Purpose:
 *
 * Return:
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_daos_request_cancel(void *context, void *req)
{
    daos_handle_t *eq_handle = (daos_handle_t *) context;
    H5VL_daos_req_t *daos_req = (H5VL_daos_req_t *) req;
    int ret;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(eq_handle);

    if(0 != (ret = daos_event_abort(&daos_req->ev)))
        HGOTO_ERROR(H5E_VOL, H5E_CANTCANCEL, FAIL, "daos_event_abort() failed: %d", ret);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5VL_daos_request_cancel() */
