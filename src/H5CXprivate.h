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
 * This file contains private information about the H5CX module
 */
#ifndef _H5CXprivate_H
#define _H5CXprivate_H

/* Include package's public header */
#include "H5CXpublic.h"

/**************************/
/* Library Private Macros */
/**************************/

#define H5_REQUEST_NULL (void *)0
//#define H5_CONTEXT_NULL ((hid_t)-1)

#if !defined(container_of)
/**
 * Given a pointer @ptr to the field @member embedded into type (usually
 * struct) @type, return pointer to the embedding instance of @type.
 */
# define container_of(ptr, type, member)                \
        ((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))
#endif

#define H5_QUEUE_HEAD_INITIALIZER(name)  { NULL, &(name).head }

#define H5_QUEUE_HEAD_INIT(struct_head_name, var_name) \
    struct struct_head_name var_name = HG_QUEUE_HEAD_INITIALIZER(var_name)

#define H5_QUEUE_HEAD_DECL(struct_head_name, struct_entry_name)  \
    struct struct_head_name {                               \
        struct struct_entry_name *head;                     \
        struct struct_entry_name **tail;                    \
    }

#define H5_QUEUE_HEAD(struct_entry_name)                    \
    struct {                                                \
        struct struct_entry_name *head;                     \
        struct struct_entry_name **tail;                    \
    }

#define H5_QUEUE_ENTRY(struct_entry_name)               \
    struct {                                            \
        struct struct_entry_name *next;                 \
    }

#define H5_QUEUE_INIT(head_ptr) do {                    \
    (head_ptr)->head = NULL;                            \
    (head_ptr)->tail = &(head_ptr)->head;               \
} while (/*CONSTCOND*/0)

#define H5_QUEUE_IS_EMPTY(head_ptr) \
    ((head_ptr)->head == NULL)

#define H5_QUEUE_FIRST(head_ptr) \
    ((head_ptr)->head)

#define H5_QUEUE_NEXT(entry_ptr, entry_field_name)  \
    ((entry_ptr)->entry_field_name.next)

#define H5_QUEUE_PUSH_TAIL(head_ptr, entry_ptr, entry_field_name) do {  \
    (entry_ptr)->entry_field_name.next = NULL;                          \
    *(head_ptr)->tail = (entry_ptr);                                    \
    (head_ptr)->tail = &(entry_ptr)->entry_field_name.next;             \
} while (/*CONSTCOND*/0)

/* TODO would be nice to not have any condition */
#define H5_QUEUE_POP_HEAD(head_ptr, entry_field_name) do {                    \
    if ((head_ptr)->head && ((head_ptr)->head = (head_ptr)->head->entry_field_name.next) == NULL) \
        (head_ptr)->tail = &(head_ptr)->head;                                 \
} while (/*CONSTCOND*/0)

#define H5_QUEUE_FOREACH(var, head_ptr, entry_field_name)   \
    for ((var) = ((head_ptr)->head);                        \
        (var);                                              \
        (var) = ((var)->entry_field_name.next))

/**
 * Avoid using those for performance reasons or use mercury_list.h instead
 */

#define H5_QUEUE_REMOVE(head_ptr, entry_ptr, type, entry_field_name) do {   \
    if ((head_ptr)->head == (entry_ptr)) {                                  \
        H5_QUEUE_POP_HEAD((head_ptr), entry_field_name);                    \
    } else {                                                                \
        struct type *curelm = (head_ptr)->head;                             \
        while (curelm->entry_field_name.next != (entry_ptr))                \
            curelm = curelm->entry_field_name.next;                         \
        if ((curelm->entry_field_name.next =                                \
            curelm->entry_field_name.next->entry_field_name.next) == NULL)  \
                (head_ptr)->tail = &(curelm)->entry_field_name.next;        \
    }                                                                       \
} while (/*CONSTCOND*/0)

/****************************/

#define H5_LIST_HEAD_INITIALIZER(name)  { NULL }

#define H5_LIST_HEAD_INIT(struct_head_name, var_name)   \
    struct struct_head_name var_name = H5_LIST_HEAD_INITIALIZER(var_name)

#define H5_LIST_HEAD_DECL(struct_head_name, struct_entry_name)  \
    struct struct_head_name {                                   \
        struct struct_entry_name *head;                         \
    }

#define H5_LIST_HEAD(struct_entry_name)     \
    struct {                                \
        struct struct_entry_name *head;     \
    }

#define H5_LIST_ENTRY(struct_entry_name)    \
    struct {                                \
        struct struct_entry_name *next;     \
        struct struct_entry_name **prev;    \
    }

#define H5_LIST_INIT(head_ptr) do {         \
    (head_ptr)->head = NULL;                \
} while (/*CONSTCOND*/0)

#define H5_LIST_IS_EMPTY(head_ptr)          \
    ((head_ptr)->head == NULL)

#define H5_LIST_FIRST(head_ptr)             \
    ((head_ptr)->head)

#define H5_LIST_NEXT(entry_ptr, entry_field_name)   \
    ((entry_ptr)->entry_field_name.next)

#define H5_LIST_INSERT_AFTER(list_entry_ptr, entry_ptr, entry_field_name) do { \
    if (((entry_ptr)->entry_field_name.next =                               \
        (list_entry_ptr)->entry_field_name.next) != NULL)                   \
        (list_entry_ptr)->entry_field_name.next->entry_field_name.prev =    \
            &(entry_ptr)->entry_field_name.next;                            \
    (list_entry_ptr)->entry_field_name.next = (entry_ptr);                  \
    (entry_ptr)->entry_field_name.prev =                                    \
        &(list_entry_ptr)->entry_field_name.next;                           \
} while (/*CONSTCOND*/0)

#define H5_LIST_INSERT_BEFORE(list_entry_ptr, entry_ptr, entry_field_name) do { \
    (entry_ptr)->entry_field_name.prev =                    \
        (list_entry_ptr)->entry_field_name.prev;            \
    (entry_ptr)->entry_field_name.next = (list_entry_ptr);  \
    *(list_entry_ptr)->entry_field_name.prev = (entry_ptr); \
    (list_entry_ptr)->entry_field_name.prev =               \
        &(entry_ptr)->entry_field_name.next;                \
} while (/*CONSTCOND*/0)

#define H5_LIST_INSERT_HEAD(head_ptr, entry_ptr, entry_field_name) do {     \
    if (((entry_ptr)->entry_field_name.next = (head_ptr)->head) != NULL)    \
        (head_ptr)->head->entry_field_name.prev =                           \
            &(entry_ptr)->entry_field_name.next;                            \
    (head_ptr)->head = (entry_ptr);                                         \
    (entry_ptr)->entry_field_name.prev = &(head_ptr)->head;                 \
} while (/*CONSTCOND*/0)

/* TODO would be nice to not have any condition */
#define H5_LIST_REMOVE(entry_ptr, entry_field_name) do {                      \
    if ((entry_ptr)->entry_field_name.next != NULL)                           \
        (entry_ptr)->entry_field_name.next->entry_field_name.prev =           \
            (entry_ptr)->entry_field_name.prev;                               \
    *(entry_ptr)->entry_field_name.prev = (entry_ptr)->entry_field_name.next; \
} while (/*CONSTCOND*/0)

#define H5_LIST_FOREACH(var, head_ptr, entry_field_name)    \
    for ((var) = ((head_ptr)->head);                        \
        (var);                                              \
        (var) = ((var)->entry_field_name.next))

/****************************/
/* Library Private Typedefs */
/****************************/

struct H5VL_class_t;
struct H5VL_object_t;

/* the transaction struct */
typedef struct H5CX_t {
    const struct H5VL_class_t *vol_cls; /* VOL plugin class */
    void *vol_context;                  /* Context created by VOL plugin */
    H5_QUEUE_HEAD(H5CX_req_t) pending_queue;
    H5_LIST_HEAD(H5CX_req_t) processing_list;
} H5CX_t;

struct H5CX_req_info_file_create {
    void *file;
};

struct H5CX_req_info_file_open {
    void *file;
};

struct H5CX_req_info_file_flush {
    H5I_type_t obj_type;
    H5F_scope_t scope;
};

struct H5CX_req_info_dset_create {
    struct H5VL_object_t *parent_obj;
    H5VL_loc_params_t loc_params;
    char *name;
    hid_t dcpl_id;
    hid_t dapl_id;
    hid_t dxpl_id;
};

struct H5CX_req_info_dset_read {
    hid_t mem_type_id;
    hid_t mem_space_id;
    hid_t file_space_id;
    hid_t plist_id;
    void *buf;
};

struct H5CX_req_info_dset_write {
    hid_t mem_type_id;
    hid_t mem_space_id;
    hid_t file_space_id;
    hid_t dxpl_id;
    const void *buf;
};

//struct H5CX_req_info_dset_close {
//    /* Nothing */
//};

typedef enum H5CX_req_type_t {
    H5CX_FILE_CREATE,
    H5CX_FILE_OPEN,
    H5CX_FILE_FLUSH,
    H5CX_FILE_CLOSE,
    H5CX_DATASET_CREATE,
    H5CX_DATASET_OPEN,
    H5CX_DATASET_WRITE,
    H5CX_DATASET_READ,
    H5CX_DATASET_CLOSE
} H5CX_req_type_t;

typedef struct H5CX_req_t {
    H5CX_t *context;
    void *vol_req;      /* VOL plugin request */
    hbool_t completed; /* Operation completed */
    hbool_t canceled;  /* Operation canceled */
    H5CX_req_type_t req_type;
    struct H5VL_object_t *obj;
    herr_t (*callback)(struct H5CX_req_t *req); /* Callback */
    union {
        struct H5CX_req_info_file_flush file_flush;
        struct H5CX_req_info_dset_create dset_create;
        struct H5CX_req_info_dset_read dset_read;
        struct H5CX_req_info_dset_write dset_write;
    } info;
    H5_LIST_ENTRY(H5CX_req_t) processing_entry;
    H5_QUEUE_ENTRY(H5CX_req_t) pending_entry;
} H5CX_req_t;

/*****************************/
/* Library Private Variables */
/*****************************/

/******************************/
/* Library Private Prototypes */
/******************************/
herr_t H5CX_init(void);

H5_DLL H5CX_t *H5CX_create(const struct H5VL_class_t *vol_cls);
H5_DLL herr_t H5CX_close(H5CX_t *context);
H5_DLL herr_t H5CX_attach(H5CX_t *context, const struct H5VL_class_t *vol_cls);
H5_DLL H5CX_req_t *H5CX_request_create(H5CX_t *context);
H5_DLL herr_t H5CX_request_close(H5CX_req_t *request);
H5_DLL herr_t H5CX_request_insert_pending(H5CX_t *context, H5CX_req_t *req);
H5_DLL herr_t H5CX_request_insert_processing(H5CX_t *context, H5CX_req_t *req);
H5_DLL herr_t H5CX_wait(H5CX_t *context, unsigned int timeout, hbool_t *flag);
H5_DLL herr_t H5CX_cancel(H5CX_t *context);

#endif /* _H5CXprivate_H */
