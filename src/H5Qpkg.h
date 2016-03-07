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
 * Purpose:     This file contains declarations which are visible
 *              only within the H5Q package. Source files outside the
 *              H5Q package should include H5Qprivate.h instead.
 */
#if !(defined H5Q_FRIEND || defined H5Q_MODULE)
#error "Do not include this file outside the H5Q package!"
#endif

#ifndef _H5Qpkg_H
#define _H5Qpkg_H

/* Get package's private header */
#include "H5Qprivate.h"

/* Other private headers needed by this file */

/**************************/
/* Package Private Macros */
/**************************/

/*
 * Singly-linked Tail queue declarations. (from sys/queue.h)
 */
#define H5Q_QUEUE_HEAD(name, type)                                          \
struct name {                                                               \
    struct type *stqh_first;    /* first element */                         \
    struct type **stqh_last;    /* addr of last next element */             \
    size_t n_elem;              /* number of elements */                    \
}

#define H5Q_QUEUE_HEAD_INITIALIZER(head)                                    \
    { NULL, &(head).stqh_first, 0 }

#define H5Q_QUEUE_ENTRY(type)                                               \
struct {                                                                    \
    struct type *stqe_next; /* next element */                              \
}

/*
 * Singly-linked Tail queue functions.
 */
#define H5Q_QUEUE_INIT(head) do {                                           \
    (head)->stqh_first = NULL;                                              \
    (head)->stqh_last = &(head)->stqh_first;                                \
    (head)->n_elem = 0;                                                     \
} while (/*CONSTCOND*/0)

#define H5Q_QUEUE_INSERT_HEAD(head, elm, field) do {                        \
    if (((elm)->field.stqe_next = (head)->stqh_first) == NULL)              \
        (head)->stqh_last = &(elm)->field.stqe_next;                        \
    (head)->stqh_first = (elm);                                             \
    (head)->n_elem++;                                                       \
} while (/*CONSTCOND*/0)

#define H5Q_QUEUE_INSERT_TAIL(head, elm, field) do {                        \
    (elm)->field.stqe_next = NULL;                                          \
    *(head)->stqh_last = (elm);                                             \
    (head)->stqh_last = &(elm)->field.stqe_next;                            \
    (head)->n_elem++;                                                       \
} while (/*CONSTCOND*/0)

#define H5Q_QUEUE_REMOVE_HEAD(head, field) do {                             \
    if (((head)->stqh_first = (head)->stqh_first->field.stqe_next) == NULL) { \
        (head)->stqh_last = &(head)->stqh_first;                            \
        (head)->n_elem--;                                                   \
    }                                                                       \
} while (/*CONSTCOND*/0)

#define H5Q_QUEUE_REMOVE(head, elm, type, field) do {                       \
    if ((head)->stqh_first == (elm)) {                                      \
        H5Q_QUEUE_REMOVE_HEAD((head), field);                               \
    } else {                                                                \
        struct type *curelm = (head)->stqh_first;                           \
        while (curelm->field.stqe_next != (elm))                            \
            curelm = curelm->field.stqe_next;                               \
        if ((curelm->field.stqe_next =                                      \
            curelm->field.stqe_next->field.stqe_next) == NULL)              \
                (head)->stqh_last = &(curelm)->field.stqe_next;             \
        (head)->n_elem--;                                                   \
    }                                                                       \
} while (/*CONSTCOND*/0)

#define H5Q_QUEUE_FOREACH(var, head, field)                                 \
    for ((var) = ((head)->stqh_first);                                      \
        (var);                                                              \
        (var) = ((var)->field.stqe_next))

#define H5Q_QUEUE_CONCAT(head1, head2) do {                                 \
    if (!H5Q_QUEUE_EMPTY((head2))) {                                        \
        *(head1)->stqh_last = (head2)->stqh_first;                          \
        (head1)->stqh_last = (head2)->stqh_last;                            \
        (head1)->n_elem += (head2)->n_elem;                                 \
        H5Q_QUEUE_INIT((head2));                                            \
    }                                                                       \
} while (/*CONSTCOND*/0)

/*
 * Singly-linked Tail queue access methods.
 */
#define H5Q_QUEUE_EMPTY(head)       ((head)->stqh_first == NULL)
#define H5Q_QUEUE_FIRST(head)       ((head)->stqh_first)
#define H5Q_QUEUE_NEXT(elm, field)  ((elm)->field.stqe_next)

#define H5Q_VIEW_INITIALIZER(view) \
    {H5Q_QUEUE_HEAD_INITIALIZER(view.reg_refs), H5Q_QUEUE_HEAD_INITIALIZER(view.obj_refs), H5Q_QUEUE_HEAD_INITIALIZER(view.attr_refs)}

#define H5Q_VIEW_REF_NTYPES      3          /* number of reference types */
#define H5Q_VIEW_CORE_INCREMENT  1024       /* increment for core VFD */

/****************************/
/* Package Private Typedefs */
/****************************/

typedef struct H5Q_ref_entry_t H5Q_ref_entry_t;

struct H5Q_ref_entry_t {
    href_t ref;
    H5Q_QUEUE_ENTRY(H5Q_ref_entry_t) entry;
};

typedef H5Q_QUEUE_HEAD(H5Q_ref_head_t, H5Q_ref_entry_t) H5Q_ref_head_t;

typedef struct {
    H5Q_ref_head_t reg_refs;
    H5Q_ref_head_t obj_refs;
    H5Q_ref_head_t attr_refs;
} H5Q_view_t;

/*****************************/
/* Package Private Variables */
/*****************************/

/******************************/
/* Package Private Prototypes */
/******************************/

herr_t H5Q__view_append(H5Q_view_t *view, H5R_type_t ref_type, href_t ref);
herr_t H5Q__view_combine(H5Q_combine_op_t combine_op, H5Q_view_t *view1, H5Q_view_t *view2,
    unsigned result1, unsigned result2, H5Q_view_t *view, unsigned *result);
herr_t H5Q__view_write(H5G_t *grp, H5Q_view_t *view);
herr_t H5Q__view_free(H5Q_view_t *view);


#endif /* _H5Qpkg_H */

