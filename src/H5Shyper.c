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
 * Programmer:  Quincey Koziol <koziol@ncsa.uiuc.edu>
 *              Thursday, June 18, 1998
 *
 * Purpose:	Hyperslab selection dataspace I/O functions.
 */

#define H5S_PACKAGE		/*suppress error about including H5Spkg	  */


#include "H5private.h"		/* Generic Functions			*/
#include "H5Eprivate.h"		/* Error handling			*/
#include "H5FLprivate.h"	/* Free Lists				*/
#include "H5Iprivate.h"		/* ID Functions				*/
#include "H5Spkg.h"		/* Dataspace functions			*/
#include "H5VMprivate.h"         /* Vector functions			*/

/* Local Macros */
#define H5S_HYPER_COMPUTE_B_NOT_A 0x01
#define H5S_HYPER_COMPUTE_A_AND_B 0x02
#define H5S_HYPER_COMPUTE_A_NOT_B 0x04

/* Local datatypes */

/* Static function prototypes */
static H5S_hyper_span_t *H5S_hyper_new_span(hsize_t low, hsize_t high,
    H5S_hyper_span_info_t *down, H5S_hyper_span_t *next);
static herr_t H5S_hyper_span_precompute(H5S_hyper_span_info_t *spans, size_t elmt_size);
static void H5S_hyper_span_scratch(H5S_hyper_span_info_t *spans, void *scr_value);
static H5S_hyper_span_info_t *H5S_hyper_copy_span(H5S_hyper_span_info_t *spans);
static htri_t H5S_hyper_cmp_spans(const H5S_hyper_span_info_t *span_info1,
    const H5S_hyper_span_info_t *span_info2);
static herr_t H5S_hyper_free_span_info(H5S_hyper_span_info_t *span_info);
static herr_t H5S_hyper_free_span(H5S_hyper_span_t *span);
static herr_t H5S_hyper_span_blocklist(const H5S_hyper_span_info_t *spans,
    hsize_t start[], hsize_t end[], hsize_t rank, hsize_t *startblock, hsize_t *numblocks,
    hsize_t **buf);
static herr_t H5S_get_select_hyper_blocklist(H5S_t *space, hbool_t internal,
    hsize_t startblock, hsize_t numblocks, hsize_t *buf);
static herr_t H5S_hyper_recover_span(hbool_t *recover, H5S_hyper_span_t **curr_span,
    H5S_hyper_span_t *next_span);
static H5S_hyper_span_t *H5S_hyper_coord_to_span(unsigned rank, const hsize_t *coords);
static herr_t H5S_hyper_append_span(H5S_hyper_span_t **prev_span,
    H5S_hyper_span_info_t **span_tree, hsize_t low, hsize_t high,
    H5S_hyper_span_info_t *down, H5S_hyper_span_t *next);
static herr_t H5S_hyper_clip_spans(H5S_hyper_span_info_t *a_spans,
    H5S_hyper_span_info_t *b_spans, unsigned int selector,
    unsigned cur_dim, unsigned dim_size, const hsize_t *span_a_b_bounds[4],
    hsize_t *all_clips_bounds, H5S_hyper_span_info_t **a_not_b,
    H5S_hyper_span_info_t **a_and_b, H5S_hyper_span_info_t **b_not_a);
static herr_t H5S_hyper_merge_spans(H5S_t *space, H5S_hyper_span_info_t *new_spans);
static hsize_t H5S_hyper_spans_nelem(const H5S_hyper_span_info_t *spans);
static herr_t H5S_hyper_add_disjoint_spans(H5S_t *space, H5S_hyper_span_info_t *new_spans);
static H5S_hyper_span_info_t *H5S_hyper_make_spans(unsigned rank,
    const hsize_t *start, const hsize_t *stride,
    const hsize_t *count, const hsize_t *block);
static herr_t H5S_hyper_update_diminfo(H5S_t *space, H5S_seloper_t op,
    const H5S_hyper_dim_t *new_hyper_diminfo);
static hbool_t H5S_hyper_rebuild_helper(const H5S_hyper_span_t *span,
    H5S_hyper_dim_t span_slab_info[], unsigned rank);
static herr_t H5S_hyper_generate_spans(H5S_t *space);
static herr_t H5S_hyper_update_bound_box(H5S_t *space, H5S_seloper_t op,
    const hsize_t low_bounds[], const hsize_t high_bounds[]);
static herr_t H5S_fill_in_new_space(H5S_t *space1, H5S_seloper_t op, H5S_hyper_span_info_t *space2_span_lst, const hsize_t *space2_low_bound, const hsize_t *space2_high_bound, hbool_t can_own_span2, hbool_t *span2_owned, hbool_t *updated_spans, H5S_t **result);
static herr_t H5S_generate_hyperslab(H5S_t *space, H5S_seloper_t op,
    const hsize_t start[], const hsize_t stride[], const hsize_t count[],
    const hsize_t block[]);
static herr_t H5S_fill_in_hyperslab(H5S_t *old_space, H5S_seloper_t op,
    const hsize_t start[], const hsize_t *stride, const hsize_t count[],
    const hsize_t *block, H5S_t **new_space);
static herr_t H5S_fill_in_select(H5S_t *space1, H5S_seloper_t op, H5S_t *space2,
    H5S_t **result);
static H5S_t *H5S_combine_select(H5S_t *space1, H5S_seloper_t op, H5S_t *space2);
static herr_t H5S_modify_select(H5S_t *space1, H5S_seloper_t op, H5S_t *space2);
static herr_t H5S_hyper_get_seq_list_gen(const H5S_t *space, H5S_sel_iter_t *iter,
    size_t maxseq, size_t maxelem, size_t *nseq, size_t *nelem, hsize_t *off,
    size_t *len);
static herr_t H5S_hyper_get_seq_list_opt(const H5S_t *space, H5S_sel_iter_t *iter,
    size_t maxseq, size_t maxelem, size_t *nseq, size_t *nelem, hsize_t *off,
    size_t *len);
static herr_t H5S_hyper_get_seq_list_single(const H5S_t *space, H5S_sel_iter_t *iter,
    size_t maxseq, size_t maxelem, size_t *nseq, size_t *nelem, hsize_t *off,
    size_t *len);

/* Selection callbacks */
static herr_t H5S_hyper_copy(H5S_t *dst, const H5S_t *src, hbool_t share_selection);
static herr_t H5S_hyper_get_seq_list(const H5S_t *space, unsigned flags,
    H5S_sel_iter_t *iter, size_t maxseq, size_t maxbytes,
    size_t *nseq, size_t *nbytes, hsize_t *off, size_t *len);
static herr_t H5S_hyper_release(H5S_t *space);
static htri_t H5S_hyper_is_valid(const H5S_t *space);
static hssize_t H5S_hyper_serial_size(const H5S_t *space);
static herr_t H5S_hyper_serialize(const H5S_t *space, uint8_t *buf);
static herr_t H5S_hyper_deserialize(H5S_t *space, const uint8_t *buf);
static herr_t H5S_hyper_bounds(const H5S_t *space, hsize_t *start, hsize_t *end);
static herr_t H5S_hyper_offset(const H5S_t *space, hsize_t *offset);
static htri_t H5S_hyper_is_contiguous(const H5S_t *space);
static htri_t H5S_hyper_is_single(const H5S_t *space);
static htri_t H5S_hyper_is_regular(const H5S_t *space);
static herr_t H5S_hyper_adjust_u(H5S_t *space, const hsize_t *offset);
static herr_t H5S_hyper_project_scalar(const H5S_t *space, hsize_t *offset);
static herr_t H5S_hyper_project_simple(const H5S_t *space, H5S_t *new_space, hsize_t *offset);
static herr_t H5S_hyper_iter_init(H5S_sel_iter_t *iter, const H5S_t *space);

/* Selection iteration callbacks */
static herr_t H5S_hyper_iter_coords(const H5S_sel_iter_t *iter, hsize_t *coords);
static herr_t H5S_hyper_iter_block(const H5S_sel_iter_t *iter, hsize_t *start, hsize_t *end);
static hsize_t H5S_hyper_iter_nelmts(const H5S_sel_iter_t *iter);
static htri_t H5S_hyper_iter_has_next_block(const H5S_sel_iter_t *sel_iter);
static herr_t H5S_hyper_iter_next(H5S_sel_iter_t *sel_iter, size_t nelem);
static herr_t H5S_hyper_iter_next_block(H5S_sel_iter_t *sel_iter);
static herr_t H5S_hyper_iter_release(H5S_sel_iter_t *sel_iter);

/* Selection properties for hyperslab selections */
const H5S_select_class_t H5S_sel_hyper[1] = {{
    H5S_SEL_HYPERSLABS,

    /* Methods on selection */
    H5S_hyper_copy,
    H5S_hyper_get_seq_list,
    H5S_hyper_release,
    H5S_hyper_is_valid,
    H5S_hyper_serial_size,
    H5S_hyper_serialize,
    H5S_hyper_deserialize,
    H5S_hyper_bounds,
    H5S_hyper_offset,
    H5S_hyper_is_contiguous,
    H5S_hyper_is_single,
    H5S_hyper_is_regular,
    H5S_hyper_adjust_u,
    H5S_hyper_project_scalar,
    H5S_hyper_project_simple,
    H5S_hyper_iter_init,
}};

/* Iteration properties for hyperslab selections */
static const H5S_sel_iter_class_t H5S_sel_iter_hyper[1] = {{
    H5S_SEL_HYPERSLABS,

    /* Methods on selection iterator */
    H5S_hyper_iter_coords,
    H5S_hyper_iter_block,
    H5S_hyper_iter_nelmts,
    H5S_hyper_iter_has_next_block,
    H5S_hyper_iter_next,
    H5S_hyper_iter_next_block,
    H5S_hyper_iter_release,
}};

/* Static variables */

/* Array for default stride, block, etc. */
static const hsize_t _ones[H5O_LAYOUT_NDIMS]={
    1,1,1,1, 1,1,1,1,
    1,1,1,1, 1,1,1,1,
    1,1,1,1, 1,1,1,1,
    1,1,1,1, 1,1,1,1,1};

/* Declare a free list to manage the H5S_hyper_sel_t struct */
H5FL_DEFINE_STATIC(H5S_hyper_sel_t);

/* Declare a free list to manage the H5S_hyper_span_t struct */
H5FL_DEFINE_STATIC(H5S_hyper_span_t);

/* Declare a free list to manage the H5S_hyper_span_info_t struct */
H5FL_DEFINE_STATIC(H5S_hyper_span_info_t);

/* #define H5S_HYPER_DEBUG */
#ifdef H5S_HYPER_DEBUG
static herr_t
H5S_hyper_print_spans_helper(FILE *f, struct H5S_hyper_span_t *span,unsigned depth)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    while(span) {
        HDfprintf(f,"%s: depth=%u, span=%p, (%d, %d), nelem=%u, pstride=%u\n",FUNC,depth,span,(int)span->low,(int)span->high,(unsigned)span->nelem,(unsigned)span->pstride);
        if(span->down && span->down->head) {
            HDfprintf(f,"%s: spans=%p, count=%u, scratch=%p, head=%p\n",FUNC,span->down,span->down->count,span->down->scratch,span->down->head);
            H5S_hyper_print_spans_helper(f,span->down->head,depth+1);
        } /* end if */
        span=span->next;
    } /* end while */

    FUNC_LEAVE_NOAPI(SUCCEED)
}

herr_t
H5S_hyper_print_spans(FILE *f, const struct H5S_hyper_span_info_t *span_lst)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    if(span_lst!=NULL) {
        HDfprintf(f,"%s: spans=%p, count=%u, scratch=%p, head=%p\n",FUNC,span_lst,span_lst->count,span_lst->scratch,span_lst->head);
        H5S_hyper_print_spans_helper(f,span_lst->head,0);
    } /* end if */

    FUNC_LEAVE_NOAPI(SUCCEED)
}

herr_t
H5S_space_print_spans(FILE *f, const H5S_t *space)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    H5S_hyper_print_spans(f,space->select.sel_info.hslab->span_lst);

    FUNC_LEAVE_NOAPI(SUCCEED)
}

static herr_t
H5S_hyper_print_diminfo_helper(FILE *f, const char *field, unsigned ndims, const H5S_hyper_dim_t *dinfo)
{
    unsigned u;                 /* Local index variable */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    if(dinfo!=NULL) {
        HDfprintf(f,"%s: %s: start=[",FUNC,field);
        for(u=0; u<ndims; u++)
            HDfprintf(f,"%Hd%s",dinfo[u].start, (u<(ndims-1) ? ", " : "]\n"));
        HDfprintf(f,"%s: %s: stride=[",FUNC,field);
        for(u=0; u<ndims; u++)
            HDfprintf(f,"%Hu%s",dinfo[u].stride, (u<(ndims-1) ? ", " : "]\n"));
        HDfprintf(f,"%s: %s: count=[",FUNC,field);
        for(u=0; u<ndims; u++)
            HDfprintf(f,"%Hu%s",dinfo[u].count, (u<(ndims-1) ? ", " : "]\n"));
        HDfprintf(f,"%s: %s: block=[",FUNC,field);
        for(u=0; u<ndims; u++)
            HDfprintf(f,"%Hu%s",dinfo[u].block, (u<(ndims-1) ? ", " : "]\n"));
    } /* end if */
    else
        HDfprintf(f,"%s: %s==NULL\n",FUNC,field);

    FUNC_LEAVE_NOAPI(SUCCEED)
}

herr_t
H5S_hyper_print_diminfo(FILE *f, const H5S_t *space)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    H5S_hyper_print_diminfo_helper(f,"opt_diminfo",space->extent.rank,space->select.sel_info.hslab->opt_diminfo);
    H5S_hyper_print_diminfo_helper(f,"app_diminfo",space->extent.rank,space->select.sel_info.hslab->app_diminfo);

    FUNC_LEAVE_NOAPI(SUCCEED)
}


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_print_spans_dfs
 PURPOSE
    Output the span elements for one span list in depth-first order
 USAGE
    herr_t H5S_hyper_print_spans_dfs(f, span_lst, depth)
        FILE *f;                                  IN: the file to output
        const H5S_hyper_span_info_t *span_lst;    IN: the span list to output
        unsigned depth;                           IN: the level of this span list
 RETURNS
    non-negative on success, negative on failure
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_print_spans_dfs(FILE *f, const H5S_hyper_span_info_t *span_lst,
    unsigned depth)
{
    H5S_hyper_span_t *actual_tail = NULL;
    H5S_hyper_span_t *cur_elem;
    unsigned num_elems = 0;
    unsigned i, elem_idx;

    FUNC_ENTER_NOAPI_NOINIT_NOFUNC(H5S_hyper_print_spans_dfs)

    /* get the actual tail from head */
    cur_elem = span_lst->head;
    HDassert(cur_elem); /* at least 1 element */
    while(cur_elem) {
        actual_tail = cur_elem;
        cur_elem = cur_elem->next;
        num_elems++;
    } /* end while */

    for(i = 0; i < depth; i++)
        HDfprintf(f, "\t");
    HDfprintf(f, "DIM[%u]: ref_count=%u, #elems=%u, scratch=%p, head=%p, tail=%p, actual_tail=%p, matched=%t\n", depth,
              span_lst->count, num_elems, span_lst->scratch, span_lst->head,
              span_lst->tail, actual_tail, (span_lst->tail == actual_tail));

    cur_elem = span_lst->head;
    elem_idx = 0;
    while(cur_elem) {
        for(i = 0; i < depth; i++)
            HDfprintf(f, "\t");
        HDfprintf(f, "ELEM[%u]: ptr=%p, low=%Hu, high=%Hu, nelem=%Hu, pstride=%Hu, down=%p\n",
                  elem_idx++, cur_elem, cur_elem->low, cur_elem->high, cur_elem->nelem,
                  cur_elem->pstride, cur_elem->down);
        if(cur_elem->down)
            H5S_hyper_print_spans_dfs(f, cur_elem->down, depth + 1);
        cur_elem = cur_elem->next;
    } /* end while */

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5S_hyper_print_spans_dfs() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_print_space_dfs
 PURPOSE
    Output the span elements for one hyperslab selection space in depth-first order
 USAGE
    herr_t H5S_hyper_print_space_dfs(f, space)
        FILE *f;               IN: the file to output
        const H5S_t *space;    IN: the selection space to output
 RETURNS
    non-negative on success, negative on failure
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_print_space_dfs(FILE *f, const H5S_t *space)
{
    const H5S_hyper_sel_t *hslab = space->select.sel_info.hslab;
    const unsigned dims = space->extent.rank;
    unsigned i;

    FUNC_ENTER_NOAPI_NOINIT_NOFUNC(H5S_hyper_print_space_dfs)
    HDassert(hslab);

    HDfprintf(f, "=======================\n");
    HDfprintf(f, "SPACE: span_lst=%p, #dims=%u, offset_changed=%d\n", hslab->span_lst, dims, space->select.offset_changed);
    HDfprintf(f, "       offsets=[");
    for(i = 0; i < dims - 1; i++)
        HDfprintf(f, "%lld,", space->select.offset[i]);
    HDfprintf(f, "%lld]\n", space->select.offset[dims-1]);
    HDfprintf(f, "       low_bounds=[");
    for(i = 0; i < dims-1; i++)
        HDfprintf(f, "%llu,", space->select.low_bounds[i]);
    HDfprintf(f, "%llu]\n", space->select.low_bounds[dims-1]);
    HDfprintf(f, "       high_bounds=[");
    for(i = 0; i < dims - 1; i++)
        HDfprintf(f, "%llu,", space->select.high_bounds[i]);
    HDfprintf(f, "%llu]\n", space->select.high_bounds[dims-1]);

    /* Start print out the highest-order of dimension */
    if(hslab->span_lst)
        H5S_hyper_print_spans_dfs(f, hslab->span_lst, 0);
    HDfprintf(f, "=======================\n\n");

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5S_hyper_print_space_dfs() */
#endif /* H5S_HYPER_DEBUG */


/*-------------------------------------------------------------------------
 * Function:	H5S_hyper_iter_init
 *
 * Purpose:	Initializes iteration information for hyperslab span tree selection.
 *
 * Return:	non-negative on success, negative on failure.
 *
 * Programmer:	Quincey Koziol
 *              Saturday, February 24, 2001
 *
 * Notes:       If the 'elmt_size' parameter is set to zero, the regular
 *              hyperslab selection iterator will not be 'flattened'.  This
 *              is used by the H5S_select_shape_same() code to avoid changing
 *              the rank and appearance of the selection.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5S_hyper_iter_init(H5S_sel_iter_t *iter, const H5S_t *space)
{
    const H5S_hyper_dim_t *tdiminfo;    /* Temporary pointer to diminfo information */
    H5S_hyper_span_info_t *spans;   /* Pointer to hyperslab span info node */
    unsigned rank;                  /* Dataspace's dimension rank */
    unsigned u;                     /* Index variable */
    int i;                          /* Index variable */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Check args */
    HDassert(space && H5S_SEL_HYPERSLABS == H5S_GET_SELECT_TYPE(space));
    HDassert(iter);

    /* Initialize the number of points to iterate over */
    iter->elmt_left = space->select.num_elem;
    iter->u.hyp.iter_rank = 0;

    /* Get the rank of the dataspace */
    rank = space->extent.rank;

    /* Rebuild diminfo if it is invalid and has not been confirmed to be
     * impossible */
    if(space->select.sel_info.hslab->diminfo_valid == H5S_DIMINFO_VALID_NO)
        /* Casting away const OK -NAF */
        (void)H5S_hyper_rebuild((H5S_t *)space);

    /* Set the temporary pointer to the dimension information */
    tdiminfo = space->select.sel_info.hslab->opt_diminfo;

    /* Check for the special case of just one H5Sselect_hyperslab call made */
    if(space->select.sel_info.hslab->diminfo_valid == H5S_DIMINFO_VALID_YES) {
/* Initialize the information needed for regular hyperslab I/O */
        const hsize_t *mem_size;    /* Temporary pointer to dataspace extent's dimension sizes */
        hsize_t acc;                /* Accumulator for "flattened" dimension's sizes */
        unsigned cont_dim = 0;      /* # of contiguous dimensions */

        /* Set the temporary pointer to the dataspace extent's dimension sizes */
        mem_size = space->extent.size;

        /*
         * For a regular hyperslab to be contiguous up to some dimension, it
         * must have only one block (i.e. count==1 in all dimensions up to that
         * dimension) and the block size must be the same as the dataspace's
         * extent in that dimension and all dimensions up to that dimension.
         */

        /* Don't flatten adjacent elements into contiguous block if the
         * element size is 0.  This is for the H5S_select_shape_same() code.
         */
        if(iter->elmt_size > 0) {
            /* Check for any "contiguous" blocks that can be flattened */
            for(u = (rank - 1); u > 0; u--) {
                if(tdiminfo[u].count == 1 && tdiminfo[u].block == mem_size[u]) {
                    cont_dim++;
                    iter->u.hyp.flattened[u] = TRUE;
                } /* end if */
                else
                    iter->u.hyp.flattened[u] = FALSE;
            } /* end for */
            iter->u.hyp.flattened[0] = FALSE;
        } /* end if */

        /* Check if the regular selection can be "flattened" */
        if(cont_dim > 0) {
            unsigned last_dim_flattened = 1;    /* Flag to indicate that the last dimension was flattened */
            unsigned flat_rank = rank-cont_dim; /* Number of dimensions after flattening */
            unsigned curr_dim;                  /* Current dimension */

            /* Set the iterator's rank to the contiguous dimensions */
            iter->u.hyp.iter_rank = flat_rank;

            /* "Flatten" dataspace extent and selection information */
            curr_dim = flat_rank - 1;
            for(i = (int)rank - 1, acc = 1; i >= 0; i--) {
                if(tdiminfo[i].block == mem_size[i] && i > 0) {
                    /* "Flatten" this dimension */
                    HDassert(tdiminfo[i].start == 0);
                    acc *= mem_size[i];

                    /* Indicate that the dimension was flattened */
                    last_dim_flattened = 1;
                } /* end if */
                else {
                    if(last_dim_flattened) {
                        /* First dimension after flattened dimensions */
                        iter->u.hyp.diminfo[curr_dim].start = tdiminfo[i].start * acc;

                        /* Special case for single block regular selections */
                        if(tdiminfo[i].count == 1)
                            iter->u.hyp.diminfo[curr_dim].stride = 1;
                        else
                            iter->u.hyp.diminfo[curr_dim].stride = tdiminfo[i].stride * acc;
                        iter->u.hyp.diminfo[curr_dim].count = tdiminfo[i].count;
                        iter->u.hyp.diminfo[curr_dim].block = tdiminfo[i].block * acc;
                        iter->u.hyp.size[curr_dim] = mem_size[i] * acc;
                        iter->u.hyp.sel_off[curr_dim] = space->select.offset[i] * (hssize_t)acc;

                        /* Reset the "last dim flattened" flag to avoid flattened any further dimensions */
                        last_dim_flattened = 0;

                        /* Reset the "accumulator" for possible further dimension flattening */
                        acc = 1;
                    } /* end if */
                    else {
                        /* All other dimensions */
                        iter->u.hyp.diminfo[curr_dim].start = tdiminfo[i].start;
                        iter->u.hyp.diminfo[curr_dim].stride = tdiminfo[i].stride;
                        iter->u.hyp.diminfo[curr_dim].count = tdiminfo[i].count;
                        iter->u.hyp.diminfo[curr_dim].block = tdiminfo[i].block;
                        iter->u.hyp.size[curr_dim] = mem_size[i];
                        iter->u.hyp.sel_off[curr_dim] = space->select.offset[i];
                    } /* end else */

                    /* Decrement "current" flattened dimension */
                    curr_dim--;
                } /* end if */
            } /* end for */

            /* Initialize "flattened" iterator offset to initial location and dataspace extent and selection information to correct values */
            for(u = 0; u < flat_rank; u++)
                iter->u.hyp.off[u] = iter->u.hyp.diminfo[u].start;
        } /* end if */
        else {
            /* Initialize position to initial location */
            /* Also make local copy of the regular selection information */
            for(u = 0; u < rank; u++) {
                /* Regular selection information */
                iter->u.hyp.diminfo[u].start = tdiminfo[u].start;
                iter->u.hyp.diminfo[u].stride = tdiminfo[u].stride;
                iter->u.hyp.diminfo[u].count = tdiminfo[u].count;
                iter->u.hyp.diminfo[u].block = tdiminfo[u].block;

                /* Position information */
                iter->u.hyp.off[u] = tdiminfo[u].start;
            } /* end if */
        } /* end else */

        /* Flag the diminfo information as valid in the iterator */
        iter->u.hyp.diminfo_valid = TRUE;

        /* Initialize irregular region information also (for release) */
        iter->u.hyp.spans = NULL;
    } /* end if */
    else {
/* Initialize the information needed for non-regular hyperslab I/O */
        HDassert(space->select.sel_info.hslab->span_lst);
        /* Make a copy of the span tree to iterate over */
        iter->u.hyp.spans = H5S_hyper_copy_span(space->select.sel_info.hslab->span_lst);

        /* Set the nelem & pstride values according to the element size */
        H5S_hyper_span_precompute(iter->u.hyp.spans,iter->elmt_size);

        /* Initialize the starting span_info's and spans */
        spans = iter->u.hyp.spans;
        for(u = 0; u < rank; u++) {
            /* Set the pointers to the initial span in each dimension */
            HDassert(spans);
            HDassert(spans->head);

            /* Set the pointer to the first span in the list for this node */
            iter->u.hyp.span[u] = spans->head;

            /* Set the initial offset to low bound of span */
            iter->u.hyp.off[u] = iter->u.hyp.span[u]->low;

            /* Get the pointer to the next level down */
            spans = spans->head->down;
        } /* end for */

        /* Flag the diminfo information as not valid in the iterator */
        iter->u.hyp.diminfo_valid = FALSE;
    } /* end else */

    /* Initialize type of selection iterator */
    iter->type = H5S_sel_iter_hyper;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5S_hyper_iter_init() */


/*-------------------------------------------------------------------------
 * Function:	H5S_hyper_iter_coords
 *
 * Purpose:	Retrieve the current coordinates of iterator for current
 *              selection
 *
 * Return:	non-negative on success, negative on failure
 *
 * Programmer:	Quincey Koziol
 *              Tuesday, April 22, 2003
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5S_hyper_iter_coords(const H5S_sel_iter_t *iter, hsize_t *coords)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Check args */
    HDassert(iter);
    HDassert(coords);

    /* Copy the offset of the current point */

    /* Check for a single "regular" hyperslab */
    if(iter->u.hyp.diminfo_valid) {
        /* Check if this is a "flattened" regular hyperslab selection */
        if(iter->u.hyp.iter_rank != 0 && iter->u.hyp.iter_rank < iter->rank) {
            int u, v;           /* Dimension indices */

            /* Set the starting rank of both the "natural" & "flattened" dimensions */
            u = (int)iter->rank - 1;
            v = (int)iter->u.hyp.iter_rank - 1;

            /* Construct the "natural" dimensions from a set of flattened coordinates */
            while(u >= 0) {
                if(iter->u.hyp.flattened[u]) {
                    int begin = u;      /* The rank of the first flattened dimension */

                    /* Walk up through as many flattened dimensions as possible */
                    do {
                        u--;
                    } while(u >= 0 && iter->u.hyp.flattened[u]);

                    /* Compensate for possibly overshooting dim 0 */
                    if(u < 0)
                        u = 0;

                    /* Sanity check */
                    HDassert(v >= 0);

                    /* Compute the coords for the flattened dimensions */
                    H5VM_array_calc(iter->u.hyp.off[v], (unsigned)((begin - u) + 1), &(iter->dims[u]), &(coords[u]));

                    /* Continue to faster dimension in both indices */
                    u--;
                    v--;
                } /* end if */
                else {
                    /* Walk up through as many non-flattened dimensions as possible */
                    while(u >= 0 && !iter->u.hyp.flattened[u]) {
                        /* Sanity check */
                        HDassert(v >= 0);

                        /* Copy the coordinate */
                        coords[u] = iter->u.hyp.off[v];

                        /* Continue to faster dimension in both indices */
                        u--;
                        v--;
                    } /* end while */
                } /* end else */
            } /* end while */
            HDassert(v < 0);
        } /* end if */
        else
            HDmemcpy(coords, iter->u.hyp.off, sizeof(hsize_t) * iter->rank);
    } /* end if */
    else
        HDmemcpy(coords, iter->u.hyp.off, sizeof(hsize_t) * iter->rank);

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5S_hyper_iter_coords() */


/*-------------------------------------------------------------------------
 * Function:	H5S_hyper_iter_block
 *
 * Purpose:	Retrieve the current block of iterator for current
 *              selection
 *
 * Return:	non-negative on success, negative on failure
 *
 * Programmer:	Quincey Koziol
 *              Monday, June 2, 2003
 *
 * Notes:       This routine assumes that the iterator is always located at
 *              the beginning of a block.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5S_hyper_iter_block(const H5S_sel_iter_t *iter, hsize_t *start, hsize_t *end)
{
    unsigned u;                 /* Local index variable */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Check args */
    HDassert(iter);
    HDassert(start);
    HDassert(end);

    /* Copy the offset of the current point */

    /* Check for a single "regular" hyperslab */
    if(iter->u.hyp.diminfo_valid) {
        /* Compute the end of the block */
        for(u=0; u<iter->rank; u++) {
            start[u]=iter->u.hyp.off[u];
            end[u]=(start[u]+iter->u.hyp.diminfo[u].block)-1;
        } /* end for */
    } /* end if */
    else {
        /* Copy the start of the block */
        for(u=0; u<iter->rank; u++)
            start[u]=iter->u.hyp.span[u]->low;

        /* Copy the end of the block */
        for(u=0; u<iter->rank; u++)
            end[u]=iter->u.hyp.span[u]->high;
    } /* end else */

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5S_hyper_iter_block() */


/*-------------------------------------------------------------------------
 * Function:	H5S_hyper_iter_nelmts
 *
 * Purpose:	Return number of elements left to process in iterator
 *
 * Return:	non-negative number of elements on success, zero on failure
 *
 * Programmer:	Quincey Koziol
 *              Tuesday, June 16, 1998
 *
 *-------------------------------------------------------------------------
 */
static hsize_t
H5S_hyper_iter_nelmts(const H5S_sel_iter_t *iter)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Check args */
    HDassert(iter);

    FUNC_LEAVE_NOAPI(iter->elmt_left)
} /* end H5S_hyper_iter_nelmts() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_iter_has_next_block
 PURPOSE
    Check if there is another block left in the current iterator
 USAGE
    htri_t H5S_hyper_iter_has_next_block(iter)
        const H5S_sel_iter_t *iter;       IN: Pointer to selection iterator
 RETURNS
    Non-negative (TRUE/FALSE) on success/Negative on failure
 DESCRIPTION
    Check if there is another block available in the selection iterator.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static htri_t
H5S_hyper_iter_has_next_block(const H5S_sel_iter_t *iter)
{
    unsigned u;                 /* Local index variable */
    htri_t ret_value = FALSE;   /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Check args */
    HDassert(iter);

    /* Check for a single "regular" hyperslab */
    if(iter->u.hyp.diminfo_valid) {
        const H5S_hyper_dim_t *tdiminfo;    /* Temporary pointer to diminfo information */
        const hsize_t *toff;               /* Temporary offset in selection */

        /* Check if the offset of the iterator is at the last location in all dimensions */
        tdiminfo = iter->u.hyp.diminfo;
        toff = iter->u.hyp.off;
        for(u = 0; u < iter->rank; u++) {
            /* If there is only one block, continue */
            if(tdiminfo[u].count == 1)
                continue;
            if(toff[u] != (tdiminfo[u].start + ((tdiminfo[u].count - 1) * tdiminfo[u].stride)))
                HGOTO_DONE(TRUE);
        } /* end for */
    } /* end if */
    else {
        /* Check for any levels of the tree with more sequences in them */
        for(u = 0; u < iter->rank; u++)
            if(iter->u.hyp.span[u]->next != NULL)
                HGOTO_DONE(TRUE);
    } /* end else */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_iter_has_next_block() */


/*-------------------------------------------------------------------------
 * Function:	H5S_hyper_iter_next
 *
 * Purpose:	Moves a hyperslab iterator to the beginning of the next sequence
 *      of elements to read.  Handles walking off the end in all dimensions.
 *
 * Return:	Success:	non-negative
 *		Failure:	negative
 *
 * Programmer:	Quincey Koziol
 *              Friday, September 8, 2000
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5S_hyper_iter_next(H5S_sel_iter_t *iter, size_t nelem)
{
    unsigned ndims;     /* Number of dimensions of dataset */
    int fast_dim;       /* Rank of the fastest changing dimension for the dataspace */
    unsigned i;         /* Counters */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Check for the special case of just one H5Sselect_hyperslab call made */
    /* (i.e. a regular hyperslab selection */
    if(iter->u.hyp.diminfo_valid) {
        const H5S_hyper_dim_t *tdiminfo;    /* Temporary pointer to diminfo information */
        hsize_t iter_offset[H5O_LAYOUT_NDIMS];
        hsize_t iter_count[H5O_LAYOUT_NDIMS];
        int temp_dim;  /* Temporary rank holder */

        /* Check if this is a "flattened" regular hyperslab selection */
        if(iter->u.hyp.iter_rank!=0 && iter->u.hyp.iter_rank<iter->rank) {
            /* Set the aliases for the dimension rank */
            ndims=iter->u.hyp.iter_rank;
        } /* end if */
        else {
            /* Set the aliases for the dimension rank */
            ndims=iter->rank;
        } /* end else */

        /* Set the fastest dimension rank */
        fast_dim = (int)ndims - 1;

        /* Set the local copy of the diminfo pointer */
        tdiminfo=iter->u.hyp.diminfo;

        /* Calculate the offset and block count for each dimension */
        for(i=0; i<ndims; i++) {
            if(tdiminfo[i].count==1) {
                iter_offset[i]=iter->u.hyp.off[i]-tdiminfo[i].start;
                iter_count[i]=0;
            } /* end if */
            else {
                iter_offset[i]=(iter->u.hyp.off[i]-tdiminfo[i].start)%tdiminfo[i].stride;
                iter_count[i]=(iter->u.hyp.off[i]-tdiminfo[i].start)/tdiminfo[i].stride;
            } /* end else */
        } /* end for */

        /* Loop through, advancing the offset & counts, until all the nelements are accounted for */
        while(nelem>0) {
            /* Start with the fastest changing dimension */
            temp_dim=fast_dim;
            while(temp_dim>=0) {
                if(temp_dim==fast_dim) {
                    size_t actual_elem;     /* Actual # of elements advanced on each iteration through loop */
                    hsize_t block_elem;     /* Number of elements left in a block */

                    /* Compute the number of elements left in block */
                    block_elem=tdiminfo[temp_dim].block-iter_offset[temp_dim];

                    /* Compute the number of actual elements to advance */
                    actual_elem=(size_t)MIN(nelem,block_elem);

                    /* Move the iterator over as many elements as possible */
                    iter_offset[temp_dim]+=actual_elem;

                    /* Decrement the number of elements advanced */
                    nelem-=actual_elem;
                } /* end if */
                else {
                    /* Move to the next row in the current dimension */
                    iter_offset[temp_dim]++;
                } /* end else */

                /* If this block is still in the range of blocks to output for the dimension, break out of loop */
                if(iter_offset[temp_dim]<tdiminfo[temp_dim].block)
                    break;
                else {
                    /* Move to the next block in the current dimension */
                    iter_offset[temp_dim]=0;
                    iter_count[temp_dim]++;

                    /* If this block is still in the range of blocks to output for the dimension, break out of loop */
                    if(iter_count[temp_dim]<tdiminfo[temp_dim].count)
                        break;
                    else
                        iter_count[temp_dim]=0; /* reset back to the beginning of the line */
                } /* end else */

                /* Decrement dimension count */
                temp_dim--;
            } /* end while */
        } /* end while */

        /* Translate current iter_offset and iter_count into iterator position */
        for(i=0; i<ndims; i++)
            iter->u.hyp.off[i]=tdiminfo[i].start+(tdiminfo[i].stride*iter_count[i])+iter_offset[i];
    } /* end if */
    /* Must be an irregular hyperslab selection */
    else {
        H5S_hyper_span_t *curr_span;    /* Current hyperslab span node */
        H5S_hyper_span_t **ispan;       /* Iterator's hyperslab span nodes */
        hsize_t *abs_arr;              /* Absolute hyperslab span position */
        int curr_dim;                   /* Temporary rank holder */

        /* Set the rank of the fastest changing dimension */
        ndims=iter->rank;
        fast_dim = (int)ndims - 1;

        /* Get the pointers to the current span info and span nodes */
        abs_arr=iter->u.hyp.off;
        ispan=iter->u.hyp.span;

        /* Loop through, advancing the span information, until all the nelements are accounted for */
        while(nelem>0) {
            /* Start at the fastest dim */
            curr_dim=fast_dim;

            /* Work back up through the dimensions */
            while(curr_dim>=0) {
                /* Reset the current span */
                curr_span=ispan[curr_dim];

                /* Increment absolute position */
                if(curr_dim==fast_dim) {
                    size_t actual_elem;     /* Actual # of elements advanced on each iteration through loop */
                    hsize_t span_elem;      /* Number of elements left in a span */

                    /* Compute the number of elements left in block */
                    span_elem=(curr_span->high-abs_arr[curr_dim])+1;

                    /* Compute the number of actual elements to advance */
                    actual_elem=(size_t)MIN(nelem,span_elem);

                    /* Move the iterator over as many elements as possible */
                    abs_arr[curr_dim]+=actual_elem;

                    /* Decrement the number of elements advanced */
                    nelem-=actual_elem;
                } /* end if */
                else {
                    /* Move to the next row in the current dimension */
                    abs_arr[curr_dim]++;
                } /* end else */

                /* Check if we are still within the span */
                if(abs_arr[curr_dim]<=curr_span->high) {
                    break;
                } /* end if */
                /* If we walked off that span, advance to the next span */
                else {
                    /* Advance span in this dimension */
                    curr_span=curr_span->next;

                    /* Check if we have a valid span in this dimension still */
                    if(curr_span!=NULL) {
                        /* Reset the span in the current dimension */
                        ispan[curr_dim]=curr_span;

                        /* Reset absolute position */
                        abs_arr[curr_dim]=curr_span->low;

                        break;
                    } /* end if */
                    else {
                        /* If we finished the span list in this dimension, decrement the dimension worked on and loop again */
                        curr_dim--;
                    } /* end else */
                } /* end else */
            } /* end while */

            /* Check if we are finished with the spans in the tree */
            if(curr_dim>=0) {
                /* Walk back down the iterator positions, reseting them */
                while(curr_dim<fast_dim) {
                    HDassert(curr_span);
                    HDassert(curr_span->down);
                    HDassert(curr_span->down->head);

                    /* Increment current dimension */
                    curr_dim++;

                    /* Set the new span_info & span for this dimension */
                    ispan[curr_dim]=curr_span->down->head;

                    /* Advance span down the tree */
                    curr_span=curr_span->down->head;

                    /* Reset the absolute offset for the dim */
                    abs_arr[curr_dim]=curr_span->low;
                } /* end while */

                /* Verify that the curr_span points to the fastest dim */
                HDassert(curr_span==ispan[fast_dim]);
            } /* end if */
        } /* end while */
    } /* end else */

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5S_hyper_iter_next() */


/*-------------------------------------------------------------------------
 * Function:	H5S_hyper_iter_next_block
 *
 * Purpose:	Moves a hyperslab iterator to the beginning of the next sequence
 *      of elements to read.  Handles walking off the end in all dimensions.
 *
 * Return:	Success:	non-negative
 *		Failure:	negative
 *
 * Programmer:	Quincey Koziol
 *              Tuesday, June 3, 2003
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5S_hyper_iter_next_block(H5S_sel_iter_t *iter)
{
    unsigned ndims;     /* Number of dimensions of dataset */
    int fast_dim;       /* Rank of the fastest changing dimension for the dataspace */
    unsigned u;         /* Counters */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Check for the special case of just one H5Sselect_hyperslab call made */
    /* (i.e. a regular hyperslab selection */
    if(iter->u.hyp.diminfo_valid) {
        const H5S_hyper_dim_t *tdiminfo;    /* Temporary pointer to diminfo information */
        hsize_t iter_offset[H5O_LAYOUT_NDIMS];
        hsize_t iter_count[H5O_LAYOUT_NDIMS];
        int temp_dim;  /* Temporary rank holder */

        /* Check if this is a "flattened" regular hyperslab selection */
        if(iter->u.hyp.iter_rank!=0 && iter->u.hyp.iter_rank<iter->rank) {
            /* Set the aliases for the dimension rank */
            ndims=iter->u.hyp.iter_rank;
        } /* end if */
        else {
            /* Set the aliases for the dimension rank */
            ndims=iter->rank;
        } /* end else */

        /* Set the fastest dimension rank */
        fast_dim = (int)ndims - 1;

        /* Set the local copy of the diminfo pointer */
        tdiminfo=iter->u.hyp.diminfo;

        /* Calculate the offset and block count for each dimension */
        for(u=0; u<ndims; u++) {
            if(tdiminfo[u].count==1) {
                iter_offset[u]=iter->u.hyp.off[u]-tdiminfo[u].start;
                iter_count[u]=0;
            } /* end if */
            else {
                iter_offset[u]=(iter->u.hyp.off[u]-tdiminfo[u].start)%tdiminfo[u].stride;
                iter_count[u]=(iter->u.hyp.off[u]-tdiminfo[u].start)/tdiminfo[u].stride;
            } /* end else */
        } /* end for */

        /* Advance one block */
        temp_dim=fast_dim; /* Start with the fastest changing dimension */
        while(temp_dim>=0) {
            if(temp_dim==fast_dim) {
                /* Move iterator over current block */
                iter_offset[temp_dim]+=tdiminfo[temp_dim].block;
            } /* end if */
            else {
                /* Move to the next row in the current dimension */
                iter_offset[temp_dim]++;
            } /* end else */

            /* If this block is still in the range of blocks to output for the dimension, break out of loop */
            if(iter_offset[temp_dim]<tdiminfo[temp_dim].block)
                break;
            else {
                /* Move to the next block in the current dimension */
                iter_offset[temp_dim]=0;
                iter_count[temp_dim]++;

                /* If this block is still in the range of blocks to output for the dimension, break out of loop */
                if(iter_count[temp_dim]<tdiminfo[temp_dim].count)
                    break;
                else
                    iter_count[temp_dim]=0; /* reset back to the beginning of the line */
            } /* end else */

            /* Decrement dimension count */
            temp_dim--;
        } /* end while */

        /* Translate current iter_offset and iter_count into iterator position */
        for(u=0; u<ndims; u++)
            iter->u.hyp.off[u]=tdiminfo[u].start+(tdiminfo[u].stride*iter_count[u])+iter_offset[u];
    } /* end if */
    /* Must be an irregular hyperslab selection */
    else {
        H5S_hyper_span_t *curr_span;    /* Current hyperslab span node */
        H5S_hyper_span_t **ispan;       /* Iterator's hyperslab span nodes */
        hsize_t *abs_arr;              /* Absolute hyperslab span position */
        int curr_dim;                   /* Temporary rank holder */

        /* Set the rank of the fastest changing dimension */
        ndims = iter->rank;
        fast_dim = (int)ndims - 1;

        /* Get the pointers to the current span info and span nodes */
        abs_arr=iter->u.hyp.off;
        ispan=iter->u.hyp.span;

        /* Loop through, advancing the span information, until all the nelements are accounted for */
        curr_dim=fast_dim; /* Start at the fastest dim */

        /* Work back up through the dimensions */
        while(curr_dim>=0) {
            /* Reset the current span */
            curr_span=ispan[curr_dim];

            /* Increment absolute position */
            if(curr_dim==fast_dim) {
                /* Move the iterator over rest of element in span */
                abs_arr[curr_dim]=curr_span->high+1;
            } /* end if */
            else {
                /* Move to the next row in the current dimension */
                abs_arr[curr_dim]++;
            } /* end else */

            /* Check if we are still within the span */
            if(abs_arr[curr_dim]<=curr_span->high) {
                break;
            } /* end if */
            /* If we walked off that span, advance to the next span */
            else {
                /* Advance span in this dimension */
                curr_span=curr_span->next;

                /* Check if we have a valid span in this dimension still */
                if(curr_span!=NULL) {
                    /* Reset the span in the current dimension */
                    ispan[curr_dim]=curr_span;

                    /* Reset absolute position */
                    abs_arr[curr_dim]=curr_span->low;

                    break;
                } /* end if */
                else {
                    /* If we finished the span list in this dimension, decrement the dimension worked on and loop again */
                    curr_dim--;
                } /* end else */
            } /* end else */
        } /* end while */

        /* Check if we are finished with the spans in the tree */
        if(curr_dim>=0) {
            /* Walk back down the iterator positions, reseting them */
            while(curr_dim<fast_dim) {
                HDassert(curr_span);
                HDassert(curr_span->down);
                HDassert(curr_span->down->head);

                /* Increment current dimension */
                curr_dim++;

                /* Set the new span_info & span for this dimension */
                ispan[curr_dim]=curr_span->down->head;

                /* Advance span down the tree */
                curr_span=curr_span->down->head;

                /* Reset the absolute offset for the dim */
                abs_arr[curr_dim]=curr_span->low;
            } /* end while */

            /* Verify that the curr_span points to the fastest dim */
            HDassert(curr_span == ispan[fast_dim]);
        } /* end if */
    } /* end else */

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5S_hyper_iter_next() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_iter_release
 PURPOSE
    Release hyperslab selection iterator information for a dataspace
 USAGE
    herr_t H5S_hyper_iter_release(iter)
        H5S_sel_iter_t *iter;       IN: Pointer to selection iterator
 RETURNS
    Non-negative on success/Negative on failure
 DESCRIPTION
    Releases all information for a dataspace hyperslab selection iterator
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_iter_release(H5S_sel_iter_t *iter)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Check args */
    HDassert(iter);

/* Release the information needed for non-regular hyperslab I/O */
    /* Free the copy of the selections span tree */
    if(iter->u.hyp.spans != NULL)
        H5S_hyper_free_span_info(iter->u.hyp.spans);

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5S_hyper_iter_release() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_new_span
 PURPOSE
    Make a new hyperslab span node
 USAGE
    H5S_hyper_span_t *H5S_hyper_new_span(low, high, down, next)
        hsize_t low, high;         IN: Low and high bounds for new span node
        H5S_hyper_span_info_t *down;     IN: Down span tree for new node
        H5S_hyper_span_t *next;     IN: Next span for new node
 RETURNS
    Pointer to next span node on success, NULL on failure
 DESCRIPTION
    Allocate and initialize a new hyperslab span node, filling in the low &
    high bounds, the down span and next span pointers also.  Increment the
    reference count of the 'down span' if applicable.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static H5S_hyper_span_t *
H5S_hyper_new_span(hsize_t low, hsize_t high, H5S_hyper_span_info_t *down, H5S_hyper_span_t *next)
{
    H5S_hyper_span_t *ret_value;

    FUNC_ENTER_NOAPI_NOINIT

    /* Allocate a new span node */
    if(NULL == (ret_value = H5FL_MALLOC(H5S_hyper_span_t)))
        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, NULL, "can't allocate hyperslab span")

    /* Copy the span's basic information */
    ret_value->low = low;
    ret_value->high = high;
    ret_value->nelem = (high - low) + 1;
    ret_value->pstride = 0;
    ret_value->down = down;
    ret_value->next = next;

    /* Increment the reference count of the 'down span' if there is one */
    if(ret_value->down)
        ret_value->down->count++;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_new_span() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_span_precompute_helper
 PURPOSE
    Helper routine to precompute the nelem and pstrides in bytes.
 USAGE
    herr_t H5S_hyper_span_precompute_helper(span_info, elmt_size)
        H5S_hyper_span_info_t *span_info;      IN/OUT: Span tree to work on
        size_t elmt_size;                      IN: element size to work with
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Change the nelem and pstride values in the span tree from elements to
    bytes using the elmt_size parameter.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_span_precompute_helper(H5S_hyper_span_info_t *spans, size_t elmt_size)
{
    H5S_hyper_span_t *span;             /* Hyperslab span */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(spans);

    /* Check if we've already set this down span tree */
    if(spans->scratch != (H5S_hyper_span_info_t *)~((size_t)NULL)) {
        /* Set the tree's scratch pointer */
        spans->scratch=(H5S_hyper_span_info_t *)~((size_t)NULL);

        /* Set the scratch pointers in all the nodes */
        span=spans->head;

        /* Loop over all the spans for this down span tree */
        while(span!=NULL) {
            /* If there are down spans, set their scratch value also */
            if(span->down!=NULL) {
                if(H5S_hyper_span_precompute_helper(span->down,elmt_size)==FAIL)
                    HGOTO_ERROR(H5E_INTERNAL, H5E_CANTFREE, FAIL, "can't reset hyperslab scratch pointer")
            } /* end if */

            /* Change the nelem & pstride values into bytes */
            span->nelem *= elmt_size;
            span->pstride *= elmt_size;

            /* Advance to next span */
            span=span->next;
        } /* end while */
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_span_precompute_helper() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_span_precompute
 PURPOSE
    Precompute the nelem and pstrides in bytes.
 USAGE
    herr_t H5S_hyper_span_precompute(span_info, elmt_size)
        H5S_hyper_span_info_t *span_info;      IN/OUT: Span tree to work on
        size_t elmt_size;                      IN: element size to work with
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Change the nelem and pstride values in the span tree from elements to
    bytes using the elmt_size parameter.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_span_precompute(H5S_hyper_span_info_t *spans, size_t elmt_size)
{
    herr_t      ret_value = SUCCEED;       /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(spans);

    /* Call the helper routine to actually do the work */
    if(H5S_hyper_span_precompute_helper(spans, elmt_size) < 0)
        HGOTO_ERROR(H5E_INTERNAL, H5E_CANTFREE, FAIL, "can't precompute span info")

    /* Reset the scratch pointers for the next routine which needs them */
    H5S_hyper_span_scratch(spans, NULL);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_span_precompute() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_span_scratch
 PURPOSE
    Set the scratch pointers on hyperslab span trees
 USAGE
    void H5S_hyper_span_scratch(span_info)
        H5S_hyper_span_info_t *span_info;      IN: Span tree to reset
 RETURNS
    <none>
 DESCRIPTION
    Set the scratch pointers on a hyperslab span tree.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static void
H5S_hyper_span_scratch(H5S_hyper_span_info_t *spans, void *scr_value)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    HDassert(spans);

    /* Check if we've already set this down span tree */
    if(spans->scratch != scr_value) {
        H5S_hyper_span_t *span;             /* Hyperslab span */

        /* Set the tree's scratch pointer */
        spans->scratch = (H5S_hyper_span_info_t *)scr_value;

        /* Set the scratch pointers in all the nodes */
        span = spans->head;
        while(span != NULL) {
            /* If there are down spans, set their scratch value also */
            if(span->down != NULL)
                H5S_hyper_span_scratch(span->down, scr_value);

            /* Advance to next span */
            span = span->next;
        } /* end while */
    } /* end if */

    FUNC_LEAVE_NOAPI_VOID
} /* end H5S_hyper_span_scratch() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_copy_span_helper
 PURPOSE
    Helper routine to copy a hyperslab span tree
 USAGE
    H5S_hyper_span_info_t * H5S_hyper_copy_span_helper(spans)
        H5S_hyper_span_info_t *spans;      IN: Span tree to copy
 RETURNS
    Pointer to the copied span tree on success, NULL on failure
 DESCRIPTION
    Copy a hyperslab span tree, using reference counting as appropriate.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static H5S_hyper_span_info_t *
H5S_hyper_copy_span_helper(H5S_hyper_span_info_t *spans)
{
    H5S_hyper_span_t *span;         /* Hyperslab span */
    H5S_hyper_span_t *new_span;     /* Temporary hyperslab span */
    H5S_hyper_span_t *prev_span;    /* Previous hyperslab span */
    H5S_hyper_span_info_t *new_down;    /* New down span tree */
    H5S_hyper_span_info_t *ret_value;

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(spans);

    /* Check if the span tree was already copied */
    if(spans->scratch != NULL && spans->scratch != (H5S_hyper_span_info_t *)~((size_t)NULL)) {
        /* Just return the value of the already copied span tree */
        ret_value = spans->scratch;

        /* Increment the reference count of the span tree */
        ret_value->count++;
    } /* end if */
    else {
        /* Allocate a new span_info node */
        if(NULL == (ret_value = H5FL_CALLOC(H5S_hyper_span_info_t)))
            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, NULL, "can't allocate hyperslab span info")

        /* Set the non-zero span_info information */
        ret_value->count = 1;

        /* Set the scratch pointer in the node being copied to the newly allocated node */
        spans->scratch = ret_value;

        /* Copy over the nodes in the span list */
        span = spans->head;
        prev_span = NULL;
        while(span != NULL) {
            /* Allocate a new node */
            if(NULL == (new_span = H5S_hyper_new_span(span->low, span->high, NULL, NULL)))
                HGOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, NULL, "can't allocate hyperslab span")

            /* Append to list of spans */
            if(NULL == prev_span)
                ret_value->head = new_span;
            else
                prev_span->next = new_span;

            /* Copy the pstride */
            new_span->pstride = span->pstride;

            /* Recurse to copy the 'down' spans, if there are any */
            if(span->down != NULL) {
                if(NULL == (new_down = H5S_hyper_copy_span_helper(span->down)))
                    HGOTO_ERROR(H5E_DATASPACE, H5E_CANTCOPY, NULL, "can't copy hyperslab spans")
                new_span->down = new_down;
            } /* end if */

            /* Update the previous (new) span */
            prev_span = new_span;

            /* Advance to next span */
            span = span->next;
        } /* end while */

        /* Retain a pointer to the last span */
        ret_value->tail = prev_span;
    } /* end else */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_copy_span_helper() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_copy_span
 PURPOSE
    Copy a hyperslab span tree
 USAGE
    H5S_hyper_span_info_t * H5S_hyper_copy_span(span_info)
        H5S_hyper_span_info_t *span_info;      IN: Span tree to copy
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Copy a hyperslab span tree, using reference counting as appropriate.
    (Which means that just the nodes in the top span tree are duplicated and
    the reference counts of their 'down spans' are just incremented)
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static H5S_hyper_span_info_t *
H5S_hyper_copy_span(H5S_hyper_span_info_t *spans)
{
    H5S_hyper_span_info_t *ret_value;

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(spans);

    /* Copy the hyperslab span tree */
    if(NULL == (ret_value = H5S_hyper_copy_span_helper(spans)))
        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTCOPY, NULL, "can't copy hyperslab span tree")

    /* Reset the scratch pointers for the next routine which needs them */
    H5S_hyper_span_scratch(spans, NULL);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_copy_span() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_cmp_spans
 PURPOSE
    Check if two hyperslab slabs are the same
 USAGE
    htri_d H5S_hyper_cmp_spans(span1, span2)
        H5S_hyper_span_t *span1;    IN: First span tree to compare
        H5S_hyper_span_t *span2;    IN: Second span tree to compare
 RETURNS
    TRUE (1) or FALSE (0) on success, negative on failure
 DESCRIPTION
    Compare two hyperslab slabs to determine if they refer to the same
    selection.  If span1 & span2 are both NULL, that counts as equal
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static htri_t
H5S_hyper_cmp_spans(const H5S_hyper_span_info_t *span_info1, const H5S_hyper_span_info_t *span_info2)
{
    htri_t ret_value = FAIL;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Check for redundant comparison */
    if(span_info1 == span_info2)
        ret_value = TRUE;
    else {
        const H5S_hyper_span_t *span1;
        const H5S_hyper_span_t *span2;
        htri_t nest = FAIL;

        /* Check for both spans being NULL */
        if(span_info1==NULL && span_info2==NULL)
            ret_value=TRUE;
        else {
            /* Check for one span being NULL */
            if(span_info1==NULL || span_info2==NULL)
                ret_value=FALSE;
            else {
                /* Get the pointers to the actual lists of spans */
                span1=span_info1->head;
                span2=span_info2->head;

                /* Sanity checking */
                HDassert(span1);
                HDassert(span2);

                /* infinite loop which must be broken out of */
                while (1) {
                    /* Check for both spans being NULL */
                    if(span1==NULL && span2==NULL) {
                        ret_value=TRUE;
                        break;
                    } /* end if */
                    else {
                        /* Check for one span being NULL */
                        if(span1==NULL || span2==NULL) {
                            ret_value=FALSE;
                            break;
                        } /* end if */
                        else {
                            /* Check if the actual low & high span information is the same */
                            if(span1->low!=span2->low || span1->high!=span2->high) {
                                ret_value=FALSE;
                                break;
                            } /* end if */
                            else {
                                if(span1->down!=NULL || span2!=NULL) {
                                    if((nest=H5S_hyper_cmp_spans(span1->down,span2->down))==FAIL) {
                                        ret_value=FAIL;
                                        break;
                                    } /* end if */
                                    else {
                                        if(nest==FALSE) {
                                            ret_value=FALSE;
                                            break;
                                        } /* end if */
                                        else {
                                            /* Keep going... */
                                        } /* end else */
                                    } /* end else */
                                } /* end if */
                                else {
                                    /* Keep going... */
                                } /* end else */
                            } /* end else */
                        } /* end else */
                    } /* end else */

                    /* Advance to the next nodes in the span list */
                    span1=span1->next;
                    span2=span2->next;
                } /* end while */
            } /* end else */
        } /* end else */
    } /* end else */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_cmp_spans() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_free_span_info
 PURPOSE
    Free a hyperslab span info node
 USAGE
    herr_t H5S_hyper_free_span_info(span_info)
        H5S_hyper_span_info_t *span_info;      IN: Span info node to free
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Free a hyperslab span info node, along with all the span nodes and the
    'down spans' from the nodes, if reducing their reference count to zero
    indicates it is appropriate to do so.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_free_span_info(H5S_hyper_span_info_t *span_info)
{
    H5S_hyper_span_t *span, *next_span;
    herr_t      ret_value=SUCCEED;       /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(span_info);

    /* Decrement the span tree's reference count */
    span_info->count--;

    /* Free the span tree if the reference count drops to zero */
    if(span_info->count==0) {

        /* Work through the list of spans pointed to by this 'info' node */
        span=span_info->head;
        while(span!=NULL) {
            next_span=span->next;
            if(H5S_hyper_free_span(span)<0)
                HGOTO_ERROR(H5E_INTERNAL, H5E_CANTFREE, FAIL, "failed to release hyperslab span")
            span=next_span;
        } /* end while */

        /* Free this span info */
        span_info = H5FL_FREE(H5S_hyper_span_info_t, span_info);
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_free_span_info() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_free_span
 PURPOSE
    Free a hyperslab span node
 USAGE
    herr_t H5S_hyper_free_span(span)
        H5S_hyper_span_t *span;      IN: Span node to free
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Free a hyperslab span node, along with the 'down spans' from the node,
    if reducing their reference count to zero indicates it is appropriate to
    do so.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_free_span(H5S_hyper_span_t *span)
{
    herr_t ret_value=SUCCEED;

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(span);

    /* Decrement the reference count of the 'down spans', freeing them if appropriate */
    if(span->down!=NULL) {
        if(H5S_hyper_free_span_info(span->down)<0)
            HGOTO_ERROR(H5E_INTERNAL, H5E_CANTFREE, FAIL, "failed to release hyperslab span tree")
    } /* end if */

    /* Free this span */
    span = H5FL_FREE(H5S_hyper_span_t, span);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_free_span() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_copy
 PURPOSE
    Copy a selection from one dataspace to another
 USAGE
    herr_t H5S_hyper_copy(dst, src)
        H5S_t *dst;  OUT: Pointer to the destination dataspace
        H5S_t *src;  IN: Pointer to the source dataspace
 RETURNS
    Non-negative on success/Negative on failure
 DESCRIPTION
    Copies all the hyperslab selection information from the source
    dataspace to the destination dataspace.

    If the SHARE_SELECTION flag is set, then the selection can be shared
    between the source and destination dataspaces.  (This should only occur in
    situations where the destination dataspace will immediately change to a new
    selection)
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_copy(H5S_t *dst, const H5S_t *src, hbool_t share_selection)
{
    H5S_hyper_sel_t *dst_hslab;         /* Pointer to destination hyperslab info */
    const H5S_hyper_sel_t *src_hslab;   /* Pointer to source hyperslab info */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    HDassert(src);
    HDassert(dst);

    /* Allocate space for the hyperslab selection information */
    if(NULL == (dst->select.sel_info.hslab = H5FL_MALLOC(H5S_hyper_sel_t)))
        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab info")

    /* Set temporary pointers */
    dst_hslab = dst->select.sel_info.hslab;
    src_hslab = src->select.sel_info.hslab;

    /* Copy the hyperslab information */
    dst_hslab->diminfo_valid = src_hslab->diminfo_valid;
    if(src_hslab->diminfo_valid == H5S_DIMINFO_VALID_YES) {
        HDmemcpy(dst_hslab->opt_diminfo, src_hslab->opt_diminfo, sizeof(H5S_hyper_dim_t) * src->extent.rank);
        HDmemcpy(dst_hslab->app_diminfo, src_hslab->app_diminfo, sizeof(H5S_hyper_dim_t) * src->extent.rank);
    } /* end if */

    dst->select.sel_info.hslab->span_lst = src->select.sel_info.hslab->span_lst;

    /* Check if there is hyperslab span information to copy */
    /* (Regular hyperslab information is copied with the selection structure) */
    if(src->select.sel_info.hslab->span_lst != NULL) {
        if(share_selection) {
            /* Share the source's span tree by incrementing the reference count on it */
            dst->select.sel_info.hslab->span_lst->count++;
        } /* end if */
        else
            /* Copy the hyperslab span information */
            dst->select.sel_info.hslab->span_lst = H5S_hyper_copy_span(src->select.sel_info.hslab->span_lst);
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_copy() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_is_valid
 PURPOSE
    Check whether the selection fits within the extent, with the current
    offset defined.
 USAGE
    htri_t H5S_hyper_is_valid(space);
        H5S_t *space;             IN: Dataspace pointer to query
 RETURNS
    TRUE if the selection fits within the extent, FALSE if it does not and
        Negative on an error.
 DESCRIPTION
    Determines if the current selection at the current offet fits within the
    extent for the dataspace.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static htri_t
H5S_hyper_is_valid(const H5S_t *space)
{
    unsigned u;                 /* Counter */
    htri_t ret_value = TRUE;    /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    HDassert(space);

    /* Check each dimension */
    for(u = 0; u < space->extent.rank; u++) {
        /* Bounds check the selected point + offset against the extent */
        if(((hssize_t)space->select.low_bounds[u] + space->select.offset[u]) < 0)
            HGOTO_DONE(FALSE)
        if((space->select.high_bounds[u] + (hsize_t)space->select.offset[u]) >= space->extent.size[u])
            HGOTO_DONE(FALSE)
    } /* end for */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_is_valid() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_span_nblocks
 PURPOSE
    Count the number of blocks in a span tree
 USAGE
    hsize_t H5S_hyper_span_nblocks(spans)
        const H5S_hyper_span_info_t *spans; IN: Hyperslab span tree to count elements of
 RETURNS
    Number of blocks in span tree on success; negative on failure
 DESCRIPTION
    Counts the number of blocks described by the spans in a span tree.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static hsize_t
H5S_hyper_span_nblocks(H5S_hyper_span_info_t *spans)
{
    H5S_hyper_span_t *span;     /* Hyperslab span */
    hsize_t ret_value = 0;      /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Count the number of elements in the span tree */
    if(spans != NULL) {
        span = spans->head;
        while(span != NULL) {
            /* If there are down spans, add the total down span blocks */
            if(span->down!=NULL)
                ret_value += H5S_hyper_span_nblocks(span->down);
            /* If there are no down spans, just count the block in this span */
            else
                ret_value++;

            /* Advance to next span */
            span = span->next;
        } /* end while */
    } /* end else */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_span_nblocks() */


/*--------------------------------------------------------------------------
 NAME
    H5S_get_select_hyper_nblocks
 PURPOSE
    Get the number of hyperslab blocks in current hyperslab selection
 USAGE
    hsize_t H5S_get_select_hyper_nblocks(space)
        H5S_t *space;             IN: Dataspace ptr of selection to query
 RETURNS
    The number of hyperslab blocks in selection on success, negative on failure
 DESCRIPTION
    Returns the number of hyperslab blocks in current selection for dataspace.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static hsize_t
H5S_get_select_hyper_nblocks(H5S_t *space)
{
    hsize_t ret_value;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    HDassert(space);

    /* Check for a "regular" hyperslab selection */
    /* (No need to rebuild the dimension info yet -QAK) */
    if(space->select.sel_info.hslab->diminfo_valid == H5S_DIMINFO_VALID_YES) {
        unsigned u;                 /* Local index variable */

        /* Check each dimension */
        for(ret_value = 1, u = 0; u < space->extent.rank; u++)
            ret_value *= space->select.sel_info.hslab->app_diminfo[u].count;
    } /* end if */
    else
        ret_value = H5S_hyper_span_nblocks(space->select.sel_info.hslab->span_lst);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_get_select_hyper_nblocks() */


/*--------------------------------------------------------------------------
 NAME
    H5Sget_select_hyper_nblocks
 PURPOSE
    Get the number of hyperslab blocks in current hyperslab selection
 USAGE
    hssize_t H5Sget_select_hyper_nblocks(dsid)
        hid_t dsid;             IN: Dataspace ID of selection to query
 RETURNS
    The number of hyperslab blocks in selection on success, negative on failure
 DESCRIPTION
    Returns the number of hyperslab blocks in current selection for dataspace.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
hssize_t
H5Sget_select_hyper_nblocks(hid_t spaceid)
{
    H5S_t *space;               /* Dataspace to modify selection of */
    hssize_t ret_value;         /* return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE1("Hs", "i", spaceid);

    /* Check args */
    if(NULL == (space = (H5S_t *)H5I_object_verify(spaceid, H5I_DATASPACE)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a dataspace")
    if(H5S_GET_SELECT_TYPE(space) != H5S_SEL_HYPERSLABS)
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a hyperslab selection")

    ret_value = (hssize_t)H5S_get_select_hyper_nblocks(space);

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Sget_select_hyper_nblocks() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_serial_size
 PURPOSE
    Determine the number of bytes needed to store the serialized hyperslab
        selection information.
 USAGE
    hssize_t H5S_hyper_serial_size(space)
        H5S_t *space;             IN: Dataspace pointer to query
 RETURNS
    The number of bytes required on success, negative on an error.
 DESCRIPTION
    Determines the number of bytes required to serialize the current hyperslab
    selection information for storage on disk.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static hssize_t
H5S_hyper_serial_size(const H5S_t *space)
{
    unsigned u;                 /* Counter */
    hsize_t block_count;       /* block counter for regular hyperslabs */
    hssize_t ret_value;         /* return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    HDassert(space);

    /* Basic number of bytes required to serialize hyperslab selection:
     *  <type (4 bytes)> + <version (4 bytes)> + <padding (4 bytes)> +
     *      <length (4 bytes)> + <rank (4 bytes)> + <# of blocks (4 bytes)> = 24 bytes
     */
    ret_value = 24;

    /* Check for a "regular" hyperslab selection */
    /* (It would be useful to rebuild the regular hyperslab selection, if we
     *  encoded it efficiently, which we aren't currently. *sigh* -QAK)
     */
    if(space->select.sel_info.hslab->diminfo_valid == H5S_DIMINFO_VALID_YES) {
        /* Check each dimension */
        for(block_count = 1, u = 0; u < space->extent.rank; u++)
            block_count *= space->select.sel_info.hslab->opt_diminfo[u].count;
    } /* end if */
    else
        /* Spin through hyperslab spans, adding 8 * rank bytes for each block */
        block_count = H5S_hyper_span_nblocks(space->select.sel_info.hslab->span_lst);

    H5_CHECK_OVERFLOW((8 * space->extent.rank * block_count), hsize_t, hssize_t);
    ret_value += (hssize_t)(8 * block_count * space->extent.rank);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_serial_size() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_serialize_helper
 PURPOSE
    Serialize the current selection into a user-provided buffer.
 USAGE
    herr_t H5S_hyper_serialize_helper(spans, start, end, rank, buf)
        H5S_hyper_span_info_t *spans;   IN: Hyperslab span tree to serialize
        hssize_t start[];       IN/OUT: Accumulated start points
        hssize_t end[];         IN/OUT: Accumulated end points
        hsize_t rank;           IN: Current rank looking at
        uint8 *buf;             OUT: Buffer to put serialized selection into
 RETURNS
    Non-negative on success/Negative on failure
 DESCRIPTION
    Serializes the current element selection into a buffer.  (Primarily for
    storing on disk).
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_serialize_helper(const H5S_hyper_span_info_t *spans, hsize_t *start, hsize_t *end, hsize_t rank, uint8_t **buf)
{
    H5S_hyper_span_t *curr;     /* Pointer to current hyperslab span */
    hsize_t u;                  /* Index variable */
    herr_t ret_value=SUCCEED;  /* return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Sanity checks */
    HDassert(spans);
    HDassert(start);
    HDassert(end);
    HDassert(rank < H5O_LAYOUT_NDIMS);
    HDassert(buf && *buf);

    /* Walk through the list of spans, recursing or outputing them */
    curr=spans->head;
    while(curr!=NULL) {
        /* Recurse if this node has down spans */
        if(curr->down!=NULL) {
            /* Add the starting and ending points for this span to the list */
            start[rank]=curr->low;
            end[rank]=curr->high;

            /* Recurse down to the next dimension */
            if(H5S_hyper_serialize_helper(curr->down,start,end,rank+1,buf)<0)
                HGOTO_ERROR(H5E_INTERNAL, H5E_CANTFREE, FAIL, "failed to release hyperslab spans")
        } /* end if */
        else {
            /* Encode all the previous dimensions starting & ending points */

            /* Encode previous starting points */
            for(u=0; u<rank; u++)
                UINT32ENCODE(*buf, (uint32_t)start[u]);

            /* Encode starting point for this span */
            UINT32ENCODE(*buf, (uint32_t)curr->low);

            /* Encode previous ending points */
            for(u=0; u<rank; u++)
                UINT32ENCODE(*buf, (uint32_t)end[u]);

            /* Encode starting point for this span */
            UINT32ENCODE(*buf, (uint32_t)curr->high);
        } /* end else */

        /* Advance to next node */
        curr=curr->next;
    } /* end while */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_serialize_helper() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_serialize
 PURPOSE
    Serialize the current selection into a user-provided buffer.
 USAGE
    herr_t H5S_hyper_serialize(space, buf)
        H5S_t *space;           IN: Dataspace pointer of selection to serialize
        uint8 *buf;             OUT: Buffer to put serialized selection into
 RETURNS
    Non-negative on success/Negative on failure
 DESCRIPTION
    Serializes the current element selection into a buffer.  (Primarily for
    storing on disk).
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_serialize(const H5S_t *space, uint8_t *buf)
{
    const H5S_hyper_dim_t *diminfo;         /* Alias for dataspace's diminfo information */
    hsize_t tmp_count[H5O_LAYOUT_NDIMS];    /* Temporary hyperslab counts */
    hsize_t offset[H5O_LAYOUT_NDIMS];      /* Offset of element in dataspace */
    hsize_t start[H5O_LAYOUT_NDIMS];   /* Location of start of hyperslab */
    hsize_t end[H5O_LAYOUT_NDIMS];     /* Location of end of hyperslab */
    hsize_t temp_off;            /* Offset in a given dimension */
    uint8_t *lenp;          /* pointer to length location for later storage */
    uint32_t len = 0;       /* number of bytes used */
    hsize_t block_count;    /* block counter for regular hyperslabs */
    unsigned fast_dim;      /* Rank of the fastest changing dimension for the dataspace */
    unsigned ndims;         /* Rank of the dataspace */
    int done;               /* Whether we are done with the iteration */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    HDassert(space);

    /* Store the preamble information */
    UINT32ENCODE(buf, (uint32_t)H5S_GET_SELECT_TYPE(space));  /* Store the type of selection */
    UINT32ENCODE(buf, (uint32_t)1);  /* Store the version number */
    UINT32ENCODE(buf, (uint32_t)0);  /* Store the un-used padding */
    lenp = buf;           /* keep the pointer to the length location for later */
    buf += 4;             /* skip over space for length */

    /* Encode number of dimensions */
    UINT32ENCODE(buf, (uint32_t)space->extent.rank);
    len += 4;

    /* Check for a "regular" hyperslab selection */
    /* (It would be useful to rebuild the regular hyperslab selection, if we
     *  encoded it efficiently, which we aren't currently. *sigh* -QAK)
     */
    if(space->select.sel_info.hslab->diminfo_valid == H5S_DIMINFO_VALID_YES) {
        unsigned u;     /* Local counting variable */

        /* Set some convenience values */
        ndims = space->extent.rank;
        fast_dim = ndims - 1;
        diminfo = space->select.sel_info.hslab->opt_diminfo;

        /* Check each dimension */
        for(block_count = 1, u = 0; u < ndims; u++)
            block_count *= diminfo[u].count;

        /* Encode number of hyperslabs */
        H5_CHECK_OVERFLOW(block_count, hsize_t, uint32_t);
        UINT32ENCODE(buf, (uint32_t)block_count);
        len+=4;

        /* Now serialize the information for the regular hyperslab */

        /* Build the tables of count sizes as well as the initial offset */
        for(u = 0; u < ndims; u++) {
            tmp_count[u] = diminfo[u].count;
            offset[u] = diminfo[u].start;
        } /* end for */

        /* We're not done with the iteration */
        done=0;

        /* Go iterate over the hyperslabs */
        while(done==0) {
            /* Iterate over the blocks in the fastest dimension */
            while(tmp_count[fast_dim]>0) {
                /* Add 8 bytes times the rank for each hyperslab selected */
                len+=8*ndims;

                /* Encode hyperslab starting location */
                for(u = 0; u < ndims; u++)
                    UINT32ENCODE(buf, (uint32_t)offset[u]);

                /* Encode hyperslab ending location */
                for(u = 0; u < ndims; u++)
                    UINT32ENCODE(buf, (uint32_t)(offset[u] + (diminfo[u].block - 1)));

                /* Move the offset to the next sequence to start */
                offset[fast_dim]+=diminfo[fast_dim].stride;

                /* Decrement the block count */
                tmp_count[fast_dim]--;
            } /* end while */

            /* Work on other dimensions if necessary */
            if(fast_dim > 0) {
                int temp_dim;           /* Temporary rank holder */

                /* Reset the block counts */
                tmp_count[fast_dim]=diminfo[fast_dim].count;

                /* Bubble up the decrement to the slower changing dimensions */
                temp_dim = (int)fast_dim - 1;
                while(temp_dim >= 0 && done == 0) {
                    /* Decrement the block count */
                    tmp_count[temp_dim]--;

                    /* Check if we have more blocks left */
                    if(tmp_count[temp_dim] > 0)
                        break;

                    /* Check for getting out of iterator */
                    if(temp_dim == 0)
                        done = 1;

                    /* Reset the block count in this dimension */
                    tmp_count[temp_dim] = diminfo[temp_dim].count;

                    /* Wrapped a dimension, go up to next dimension */
                    temp_dim--;
                } /* end while */
            } /* end if */
            else
                break;  /* Break out now, for 1-D selections */

            /* Re-compute offset array */
            for(u = 0; u < ndims; u++) {
                temp_off = diminfo[u].start + diminfo[u].stride * (diminfo[u].count - tmp_count[u]);
                offset[u] = temp_off;
            } /* end for */
        } /* end while */
    } /* end if */
    else {
        /* Encode number of hyperslabs */
        block_count = H5S_hyper_span_nblocks(space->select.sel_info.hslab->span_lst);
        H5_CHECK_OVERFLOW(block_count, hsize_t, uint32_t);
        UINT32ENCODE(buf, (uint32_t)block_count);
        len+=4;

        /* Add 8 bytes times the rank for each hyperslab selected */
        H5_CHECK_OVERFLOW((8 * space->extent.rank * block_count), hsize_t, size_t);
        len += (uint32_t)(8 * space->extent.rank * block_count);

        /* Encode each hyperslab in selection */
        H5S_hyper_serialize_helper(space->select.sel_info.hslab->span_lst, start, end, (hsize_t)0, &buf);
    } /* end else */

    /* Encode length */
    UINT32ENCODE(lenp, (uint32_t)len);  /* Store the length of the extra information */

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5S_hyper_serialize() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_deserialize
 PURPOSE
    Deserialize the current selection from a user-provided buffer.
 USAGE
    herr_t H5S_hyper_deserialize(space, buf)
        H5S_t *space;           IN/OUT: Dataspace pointer to place selection into
        uint8 *buf;             IN: Buffer to retrieve serialized selection from
 RETURNS
    Non-negative on success/Negative on failure
 DESCRIPTION
    Deserializes the current selection into a buffer.  (Primarily for retrieving
    from disk).
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_deserialize(H5S_t *space, const uint8_t *buf)
{
    uint32_t rank;           	/* rank of points */
    size_t num_elem=0;      	/* number of elements in selection */
    hsize_t start[H5O_LAYOUT_NDIMS];	/* hyperslab start information */
    hsize_t end[H5O_LAYOUT_NDIMS];	/* hyperslab end information */
    hsize_t stride[H5O_LAYOUT_NDIMS];   /* hyperslab stride information */
    hsize_t count[H5O_LAYOUT_NDIMS];    /* hyperslab count information */
    hsize_t block[H5O_LAYOUT_NDIMS];    /* hyperslab block information */
    hsize_t *tstart=NULL;	/* temporary hyperslab pointers */
    hsize_t *tend=NULL;		/* temporary hyperslab pointers */
    hsize_t *tstride=NULL;	/* temporary hyperslab pointers */
    hsize_t *tcount=NULL;	/* temporary hyperslab pointers */
    hsize_t *tblock=NULL;	/* temporary hyperslab pointers */
    unsigned i,j;              	/* local counting variables */
    herr_t ret_value=FAIL;  	/* return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Check args */
    HDassert(space);
    HDassert(buf);

    /* Deserialize slabs to select */
    buf+=16;    /* Skip over selection header */
    UINT32DECODE(buf,rank);  /* decode the rank of the point selection */
    if(rank!=space->extent.rank)
        HGOTO_ERROR(H5E_DATASPACE, H5E_BADRANGE, FAIL, "rank of pointer does not match dataspace")
    UINT32DECODE(buf,num_elem);  /* decode the number of points */

    /* Set the count & stride for all blocks */
    for(tcount=count,tstride=stride,j=0; j<rank; j++,tstride++,tcount++) {
        *tcount=1;
        *tstride=1;
    } /* end for */

    /* Retrieve the coordinates from the buffer */
    for(i=0; i<num_elem; i++) {
        /* Decode the starting points */
        for(tstart=start,j=0; j<rank; j++,tstart++)
            UINT32DECODE(buf, *tstart);

        /* Decode the ending points */
        for(tend=end,j=0; j<rank; j++,tend++)
            UINT32DECODE(buf, *tend);

        /* Change the ending points into blocks */
        for(tblock=block,tstart=start,tend=end,j=0; j<(unsigned)rank; j++,tstart++,tend++,tblock++)
            *tblock=(*tend-*tstart)+1;

        /* Select or add the hyperslab to the current selection */
        if((ret_value=H5S_select_hyperslab(space,(i==0 ? H5S_SELECT_SET : H5S_SELECT_OR),start,stride,count,block))<0)
            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTDELETE, FAIL, "can't change selection")
    } /* end for */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_deserialize() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_span_blocklist
 PURPOSE
    Get a list of hyperslab blocks currently selected
 USAGE
    herr_t H5S_hyper_span_blocklist(spans, start, end, rank, startblock, numblocks, buf)
        H5S_hyper_span_info_t *spans;   IN: Dataspace pointer of selection to query
        hsize_t start[];       IN/OUT: Accumulated start points
        hsize_t end[];         IN/OUT: Accumulated end points
        hsize_t rank;           IN: Rank of dataspace
        hsize_t *startblock;    IN/OUT: Hyperslab block to start with
        hsize_t *numblocks;     IN/OUT: Number of hyperslab blocks to get
        hsize_t **buf;          OUT: List of hyperslab blocks selected
 RETURNS
    Non-negative on success/Negative on failure
 DESCRIPTION
        Puts a list of the hyperslab blocks into the user's buffer.  The blocks
    start with the '*startblock'th block in the list of blocks and put
    '*numblocks' number of blocks into the user's buffer (or until the end of
    the list of blocks, whichever happens first)
        The block coordinates have the same dimensionality (rank) as the
    dataspace they are located within.  The list of blocks is formatted as
    follows: <"start" coordinate> immediately followed by <"opposite" corner
    coordinate>, followed by the next "start" and "opposite" coordinate, etc.
    until all the block information requested has been put into the user's
    buffer.
        No guarantee of any order of the blocks is implied.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_span_blocklist(const H5S_hyper_span_info_t *spans, hsize_t start[],
    hsize_t end[], hsize_t rank, hsize_t *startblock, hsize_t *numblocks,
    hsize_t **buf)
{
    const H5S_hyper_span_t *curr;     /* Pointer to current hyperslab span */
    hsize_t u;                  /* Index variable */
    herr_t ret_value = SUCCEED; /* return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Sanity checks */
    HDassert(spans);
    HDassert(rank < H5O_LAYOUT_NDIMS);
    HDassert(start);
    HDassert(end);
    HDassert(startblock);
    HDassert(numblocks && *numblocks > 0);
    HDassert(buf && *buf);

    /* Walk through the list of spans, recursing or outputing them */
    curr = spans->head;
    while(curr != NULL && *numblocks > 0) {
        /* Recurse if this node has down spans */
        if(curr->down != NULL) {
            /* Add the starting and ending points for this span to the list */
            start[rank] = curr->low;
            end[rank] = curr->high;

            /* Recurse down to the next dimension */
            if(H5S_hyper_span_blocklist(curr->down, start, end, (rank + 1), startblock, numblocks, buf) < 0)
                HGOTO_ERROR(H5E_INTERNAL, H5E_CANTFREE, FAIL, "failed to release hyperslab spans")
        } /* end if */
        else {
            /* Skip this block if we haven't skipped all the startblocks yet */
            if(*startblock > 0) {
                /* Decrement the starting block */
                (*startblock)--;
            } /* end if */
            /* Process this block */
            else {
                /* Encode all the previous dimensions starting & ending points */

                /* Copy previous starting points */
                for(u = 0; u < rank; u++, (*buf)++)
                    HDmemcpy(*buf, &start[u], sizeof(hsize_t));

                /* Copy starting point for this span */
                HDmemcpy(*buf, &curr->low, sizeof(hsize_t));
                (*buf)++;

                /* Copy previous ending points */
                for(u = 0; u < rank; u++, (*buf)++)
                    HDmemcpy(*buf, &end[u], sizeof(hsize_t));

                /* Copy starting point for this span */
                HDmemcpy(*buf, &curr->high, sizeof(hsize_t));
                (*buf)++;

                /* Decrement the number of blocks processed */
                (*numblocks)--;
            } /* end else */
        } /* end else */

        /* Advance to next node */
        curr = curr->next;
    } /* end while */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_span_blocklist() */


/*--------------------------------------------------------------------------
 NAME
    H5S_get_select_hyper_blocklist
 PURPOSE
    Get the list of hyperslab blocks currently selected
 USAGE
    herr_t H5S_get_select_hyper_blocklist(space, startblock, numblocks, buf)
        H5S_t *space;           IN: Dataspace pointer of selection to query
        hsize_t startblock;     IN: Hyperslab block to start with
        hsize_t numblocks;      IN: Number of hyperslab blocks to get
        hsize_t *buf;           OUT: List of hyperslab blocks selected
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
        Puts a list of the hyperslab blocks into the user's buffer.  The blocks
    start with the 'startblock'th block in the list of blocks and put
    'numblocks' number of blocks into the user's buffer (or until the end of
    the list of blocks, whichever happens first)
        The block coordinates have the same dimensionality (rank) as the
    dataspace they are located within.  The list of blocks is formatted as
    follows: <"start" coordinate> immediately followed by <"opposite" corner
    coordinate>, followed by the next "start" and "opposite" coordinate, etc.
    until all the block information requested has been put into the user's
    buffer.
        No guarantee of any order of the blocks is implied.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_get_select_hyper_blocklist(H5S_t *space, hbool_t internal, hsize_t startblock, hsize_t numblocks, hsize_t *buf)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    HDassert(space);
    HDassert(buf);

    /* Rebuild diminfo if it is invalid and has not been confirmed to be
     * impossible */
    if(space->select.sel_info.hslab->diminfo_valid == H5S_DIMINFO_VALID_NO)
        (void)H5S_hyper_rebuild(space);

    /* Check for a "regular" hyperslab selection */
    if(space->select.sel_info.hslab->diminfo_valid == H5S_DIMINFO_VALID_YES) {
        const H5S_hyper_dim_t *diminfo; /* Alias for dataspace's diminfo information */
        hsize_t tmp_count[H5O_LAYOUT_NDIMS];    /* Temporary hyperslab counts */
        hsize_t offset[H5O_LAYOUT_NDIMS];      /* Offset of element in dataspace */
        unsigned fast_dim;          /* Rank of the fastest changing dimension for the dataspace */
        unsigned ndims;             /* Rank of the dataspace */
        hbool_t done;               /* Whether we are done with the iteration */
        unsigned u;                 /* Counter */

        /* Set some convienence values */
        ndims = space->extent.rank;
        fast_dim = ndims - 1;

        /* Check which set of dimension information to use */
        if(internal)
            /*
             * Use the "optimized dimension information" to pass back information
             * on the blocks set, not the "application information".
             */
            diminfo = space->select.sel_info.hslab->opt_diminfo;
        else
            /*
             * Use the "application dimension information" to pass back to the user
             * the blocks they set, not the optimized, internal information.
             */
            diminfo = space->select.sel_info.hslab->app_diminfo;

        /* Build the tables of count sizes as well as the initial offset */
        for(u = 0; u < ndims; u++) {
            tmp_count[u] = diminfo[u].count;
            offset[u] = diminfo[u].start;
        } /* end for */

        /* We're not done with the iteration */
        done = FALSE;

        /* Go iterate over the hyperslabs */
        while(!done && numblocks > 0) {
            hsize_t temp_off;           /* Offset in a given dimension */

            /* Iterate over the blocks in the fastest dimension */
            while(tmp_count[fast_dim] > 0 && numblocks > 0) {

                /* Check if we should copy this block information */
                if(startblock == 0) {
                    /* Copy the starting location */
                    HDmemcpy(buf, offset, sizeof(hsize_t) * ndims);
                    buf += ndims;

                    /* Compute the ending location */
                    HDmemcpy(buf, offset, sizeof(hsize_t) * ndims);
                    for(u = 0; u < ndims; u++)
                        buf[u] += (diminfo[u].block - 1);
                    buf += ndims;

                    /* Decrement the number of blocks to retrieve */
                    numblocks--;
                } /* end if */
                else
                    startblock--;

                /* Move the offset to the next sequence to start */
                offset[fast_dim] += diminfo[fast_dim].stride;

                /* Decrement the block count */
                tmp_count[fast_dim]--;
            } /* end while */

            /* Work on other dimensions if necessary */
            if(fast_dim > 0 && numblocks > 0) {
                int temp_dim;               /* Temporary rank holder */

                /* Reset the block counts */
                tmp_count[fast_dim] = diminfo[fast_dim].count;

                /* Bubble up the decrement to the slower changing dimensions */
                temp_dim = (int)(fast_dim - 1);
                while(temp_dim >= 0 && !done) {
                    /* Decrement the block count */
                    tmp_count[temp_dim]--;

                    /* Check if we have more blocks left */
                    if(tmp_count[temp_dim] > 0)
                        break;

                    /* Check for getting out of iterator */
                    if(temp_dim == 0)
                        done = TRUE;

                    /* Reset the block count in this dimension */
                    tmp_count[temp_dim] = diminfo[temp_dim].count;

                    /* Wrapped a dimension, go up to next dimension */
                    temp_dim--;
                } /* end while */
            } /* end if */

            /* Re-compute offset array */
            for(u = 0; u < ndims; u++) {
                temp_off = diminfo[u].start + diminfo[u].stride * (diminfo[u].count - tmp_count[u]);
                offset[u] = temp_off;
            } /* end for */
        } /* end while */
    } /* end if */
    else {
        hsize_t start[H5O_LAYOUT_NDIMS];   /* Location of start of hyperslab */
        hsize_t end[H5O_LAYOUT_NDIMS];     /* Location of end of hyperslab */

        ret_value = H5S_hyper_span_blocklist(space->select.sel_info.hslab->span_lst, start, end, (hsize_t)0, &startblock, &numblocks, &buf);
    } /* end else */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_get_select_hyper_blocklist() */


/*--------------------------------------------------------------------------
 NAME
    H5Sget_select_hyper_blocklist
 PURPOSE
    Get the list of hyperslab blocks currently selected
 USAGE
    herr_t H5Sget_select_hyper_blocklist(dsid, startblock, numblocks, buf)
        hid_t dsid;             IN: Dataspace ID of selection to query
        hsize_t startblock;     IN: Hyperslab block to start with
        hsize_t numblocks;      IN: Number of hyperslab blocks to get
        hsize_t buf[];          OUT: List of hyperslab blocks selected
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
        Puts a list of the hyperslab blocks into the user's buffer.  The blocks
    start with the 'startblock'th block in the list of blocks and put
    'numblocks' number of blocks into the user's buffer (or until the end of
    the list of blocks, whichever happen first)
        The block coordinates have the same dimensionality (rank) as the
    dataspace they are located within.  The list of blocks is formatted as
    follows: <"start" coordinate> immediately followed by <"opposite" corner
    coordinate>, followed by the next "start" and "opposite" coordinate, etc.
    until all the block information requested has been put into the user's
    buffer.
        No guarantee of any order of the blocks is implied.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
herr_t
H5Sget_select_hyper_blocklist(hid_t spaceid, hsize_t startblock,
    hsize_t numblocks, hsize_t buf[/*numblocks*/])
{
    H5S_t *space;               /* Dataspace to modify selection of */
    herr_t ret_value;           /* return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE4("e", "ihh*[a2]h", spaceid, startblock, numblocks, buf);

    /* Check args */
    if(buf == NULL)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid pointer")
    if(NULL == (space = (H5S_t *)H5I_object_verify(spaceid, H5I_DATASPACE)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a dataspace")
    if(H5S_GET_SELECT_TYPE(space)!=H5S_SEL_HYPERSLABS)
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a hyperslab selection")

    /* Go get the correct number of blocks */
    if(numblocks > 0)
        ret_value = H5S_get_select_hyper_blocklist(space, 0, startblock, numblocks, buf);
    else
        ret_value=SUCCEED;      /* Successfully got 0 blocks... */

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Sget_select_hyper_blocklist() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_bounds
 PURPOSE
    Gets the bounding box containing the selection.
 USAGE
    herr_t H5S_hyper_bounds(space, hsize_t *start, hsize_t *end)
        H5S_t *space;           IN: Dataspace pointer of selection to query
        hsize_t *start;         OUT: Starting coordinate of bounding box
        hsize_t *end;           OUT: Opposite coordinate of bounding box
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Retrieves the bounding box containing the current selection and places
    it into the user's buffers.  The start and end buffers must be large
    enough to hold the dataspace rank number of coordinates.  The bounding box
    exactly contains the selection, ie. if a 2-D element selection is currently
    defined with the following points: (4,5), (6,8) (10,7), the bounding box
    with be (4, 5), (10, 8).
        The bounding box calculations _does_ include the current offset of the
    selection within the dataspace extent.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_bounds(const H5S_t *space, hsize_t *start, hsize_t *end)
{
    unsigned u;                 /* Local index variable */
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Sanity check */
    HDassert(space);
    HDassert(start);
    HDassert(end);

    /* Loop over dimensions */
    for(u = 0; u < space->extent.rank; u++) {
        /* Sanity check */
        HDassert(space->select.low_bounds[u] <= space->select.high_bounds[u]);

        /* Check for offset moving selection negative */
        if(((hssize_t)space->select.low_bounds[u] + space->select.offset[u]) < 0)
            HGOTO_ERROR(H5E_DATASPACE, H5E_BADRANGE, FAIL, "offset moves selection out of bounds")

        /* Set the low & high bounds in this dimension */
        start[u] = (hsize_t)((hssize_t)space->select.low_bounds[u] + space->select.offset[u]);
        end[u] = (hsize_t)((hssize_t)space->select.high_bounds[u] + space->select.offset[u]);
    } /* end for */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_bounds() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_offset
 PURPOSE
    Gets the linear offset of the first element for the selection.
 USAGE
    herr_t H5S_hyper_offset(space, offset)
        const H5S_t *space;     IN: Dataspace pointer of selection to query
        hsize_t *offset;        OUT: Linear offset of first element in selection
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Retrieves the linear offset (in "units" of elements) of the first element
    selected within the dataspace.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
    Calling this function on a "none" selection returns fail.
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_offset(const H5S_t *space, hsize_t *offset)
{
    const hssize_t *sel_offset; /* Pointer to the selection's offset */
    const hsize_t *dim_size;    /* Pointer to a dataspace's extent */
    hsize_t accum;              /* Accumulator for dimension sizes */
    unsigned rank;              /* Dataspace rank */
    int i;                      /* index variable */
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    HDassert(space && space->extent.rank>0);
    HDassert(offset);

    /* Start at linear offset 0 */
    *offset = 0;

    /* Set up pointers to arrays of values */
    rank = space->extent.rank;
    sel_offset = space->select.offset;
    dim_size = space->extent.size;

    /* Check for a "regular" hyperslab selection */
    /* (No need to rebuild the dimension info yet -QAK) */
    if(space->select.sel_info.hslab->diminfo_valid == H5S_DIMINFO_VALID_YES) {
        const H5S_hyper_dim_t *diminfo = space->select.sel_info.hslab->opt_diminfo; /* Local alias for diminfo */

        /* Loop through starting coordinates, calculating the linear offset */
        accum = 1;
        for(i = (int)(rank - 1); i >= 0; i--) {
            hssize_t hyp_offset = (hssize_t)diminfo[i].start + sel_offset[i]; /* Hyperslab's offset in this dimension */

            /* Check for offset moving selection out of the dataspace */
            if(hyp_offset < 0 || (hsize_t)hyp_offset >= dim_size[i])
                HGOTO_ERROR(H5E_DATASPACE, H5E_BADRANGE, FAIL, "offset moves selection out of bounds")

            /* Add the hyperslab's offset in this dimension to the total linear offset */
            *offset += (hsize_t)(hyp_offset * (hssize_t)accum);

            /* Increase the accumulator */
            accum *= dim_size[i];
        } /* end for */
    } /* end if */
    else {
        const H5S_hyper_span_t *span;           /* Hyperslab span node */
        hsize_t dim_accum[H5S_MAX_RANK];        /* Accumulators, for each dimension */

        /* Calculate the accumulator for each dimension */
        accum = 1;
        for(i = (int)(rank - 1); i >= 0; i--) {
            /* Set the accumulator for this dimension */
            dim_accum[i] = accum;

            /* Increase the accumulator */
            accum *= dim_size[i];
        } /* end for */

        /* Get information for the first span, in the slowest changing dimension */
        span = space->select.sel_info.hslab->span_lst->head;

        /* Work down the spans, computing the linear offset */
        i = 0;
        while(span) {
            hssize_t hyp_offset = (hssize_t)span->low + sel_offset[i]; /* Hyperslab's offset in this dimension */

            /* Check for offset moving selection out of the dataspace */
            if(hyp_offset < 0 || (hsize_t)hyp_offset >= dim_size[i])
                HGOTO_ERROR(H5E_DATASPACE, H5E_BADRANGE, FAIL, "offset moves selection out of bounds")

            /* Add the hyperslab's offset in this dimension to the total linear offset */
            *offset += (hsize_t)(hyp_offset * (hssize_t)dim_accum[i]);

            /* Advance to first span in "down" dimension */
            if(span->down) {
                HDassert(span->down->head);
                span = span->down->head;
            } /* end if */
            else
                span = NULL;
            i++;
        } /* end while */
    } /* end else */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_offset() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_is_contiguous
 PURPOSE
    Check if a hyperslab selection is contiguous within the dataspace extent.
 USAGE
    htri_t H5S_hyper_is_contiguous(space)
        H5S_t *space;           IN: Dataspace pointer to check
 RETURNS
    TRUE/FALSE/FAIL
 DESCRIPTION
    Checks to see if the current selection in the dataspace is contiguous.
    This is primarily used for reading the entire selection in one swoop.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static htri_t
H5S_hyper_is_contiguous(const H5S_t *space)
{
    unsigned small_contiguous,      /* Flag for small contiguous block */
        large_contiguous;           /* Flag for large contiguous block */
    unsigned u;                     /* index variable */
    htri_t ret_value = FALSE;       /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    HDassert(space);

    /* Check for a "regular" hyperslab selection */
    /* (No need to rebuild the dimension info yet -QAK) */
    if(space->select.sel_info.hslab->diminfo_valid == H5S_DIMINFO_VALID_YES) {
        const H5S_hyper_dim_t *diminfo=space->select.sel_info.hslab->opt_diminfo; /* local alias for diminfo */

        /*
         * For a regular hyperslab to be contiguous, it must have only one
         * block (i.e. count==1 in all dimensions) and the block size must be
         * the same as the dataspace extent's in all but the slowest changing
         * dimension. (dubbed "large contiguous" block)
         *
         * OR
         *
         * The selection must have only one block (i.e. count==1) in all
         * dimensions and the block size must be 1 in all but the fastest
         * changing dimension. (dubbed "small contiguous" block)
         */

        /* Initialize flags */
        large_contiguous=TRUE;	/* assume true and reset if the dimensions don't match */
        small_contiguous=FALSE;	/* assume false initially */

        /* Check for a "large contigous" block */
        for(u=0; u<space->extent.rank; u++) {
            if(diminfo[u].count>1) {
                large_contiguous=FALSE;
                break;
            } /* end if */
            if(u>0 && diminfo[u].block!=space->extent.size[u]) {
                large_contiguous=FALSE;
                break;
            } /* end if */
        } /* end for */

        /* If we didn't find a large contiguous block, check for a small one */
        if(large_contiguous==FALSE) {
            small_contiguous=TRUE;
            for(u=0; u<space->extent.rank; u++) {
                if(diminfo[u].count>1) {
                    small_contiguous=FALSE;
                    break;
                } /* end if */
                if(u<(space->extent.rank-1) && diminfo[u].block!=1) {
                    small_contiguous=FALSE;
                    break;
                } /* end if */
            } /* end for */
        } /* end if */

        /* Indicate true if it's either a large or small contiguous block */
        if(large_contiguous || small_contiguous)
            ret_value=TRUE;
    } /* end if */
    else {
        H5S_hyper_span_info_t *spans;   /* Hyperslab span info node */
        H5S_hyper_span_t *span;         /* Hyperslab span node */

        /*
         * For a hyperslab to be contiguous, it must have only one block and
         * (either it's size must be the same as the dataspace extent's in all
         * but the slowest changing dimension
         * OR
         * block size must be 1 in all but the fastest changing dimension).
         */
        /* Initialize flags */
        large_contiguous=TRUE;	/* assume true and reset if the dimensions don't match */
        small_contiguous=FALSE;	/* assume false initially */

        /* Get information for slowest changing information */
        spans=space->select.sel_info.hslab->span_lst;
        span=spans->head;

        /* If there are multiple spans in the slowest changing dimension, the selection isn't contiguous */
        if(span->next!=NULL)
            large_contiguous=FALSE;
        else {
            /* Now check the rest of the dimensions */
            if(span->down!=NULL) {
                u=1;    /* Current dimension working on */

                /* Get the span information for the next fastest dimension */
                spans=span->down;

                /* Cycle down the spans until we run out of down spans or find a non-contiguous span */
                while(spans!=NULL) {
                    span=spans->head;

                    /* Check that this is the only span and it spans the entire dimension */
                    if(span->next!=NULL) {
                        large_contiguous=FALSE;
                        break;
                    } /* end if */
                    else {
                        /* If this span doesn't cover the entire dimension, then this selection isn't contiguous */
                        if(((span->high-span->low)+1)!=space->extent.size[u]) {
                            large_contiguous=FALSE;
                            break;
                        } /* end if */
                        else {
                            /* Walk down to the next span */
                            spans=span->down;

                            /* Increment dimension */
                            u++;
                        } /* end else */
                    } /* end else */
                } /* end while */
            } /* end if */
        } /* end else */

        /* If we didn't find a large contiguous block, check for a small one */
        if(large_contiguous==FALSE) {
            small_contiguous=TRUE;

            /* Get information for slowest changing information */
            spans=space->select.sel_info.hslab->span_lst;
            span=spans->head;

            /* Current dimension working on */
            u=0;

            /* Cycle down the spans until we run out of down spans or find a non-contiguous span */
            while(spans!=NULL) {
                span=spans->head;

                /* Check that this is the only span and it spans the entire dimension */
                if(span->next!=NULL) {
                    small_contiguous=FALSE;
                    break;
                } /* end if */
                else {
                    /* If this span doesn't cover the entire dimension, then this selection isn't contiguous */
                    if(u<(space->extent.rank-1) && ((span->high-span->low)+1)!=1) {
                        small_contiguous=FALSE;
                        break;
                    } /* end if */
                    else {
                        /* Walk down to the next span */
                        spans=span->down;

                        /* Increment dimension */
                        u++;
                    } /* end else */
                } /* end else */
            } /* end while */
        } /* end if */

        /* Indicate true if it's either a large or small contiguous block */
        if(large_contiguous || small_contiguous)
            ret_value=TRUE;
    } /* end else */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_is_contiguous() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_is_single
 PURPOSE
    Check if a hyperslab selection is a single block within the dataspace extent.
 USAGE
    htri_t H5S_hyper_is_single(space)
        H5S_t *space;           IN: Dataspace pointer to check
 RETURNS
    TRUE/FALSE/FAIL
 DESCRIPTION
    Checks to see if the current selection in the dataspace is a single block.
    This is primarily used for reading the entire selection in one swoop.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static htri_t
H5S_hyper_is_single(const H5S_t *space)
{
    H5S_hyper_span_info_t *spans;   /* Hyperslab span info node */
    H5S_hyper_span_t *span;         /* Hyperslab span node */
    unsigned u;                     /* index variable */
    htri_t ret_value=TRUE;         /* return value */

    FUNC_ENTER_NOAPI_NOERR

    HDassert(space);

    /* Check for a "single" hyperslab selection */
    /* (No need to rebuild the dimension info yet -QAK) */
    if(space->select.sel_info.hslab->diminfo_valid == H5S_DIMINFO_VALID_YES) {
        /*
         * For a regular hyperslab to be single, it must have only one
         * block (i.e. count==1 in all dimensions)
         */

        /* Check for a single block */
        for(u=0; u<space->extent.rank; u++) {
            if(space->select.sel_info.hslab->opt_diminfo[u].count>1)
                HGOTO_DONE(FALSE)
        } /* end for */
    } /* end if */
    else {
        /*
         * For a region to be single, it must have only one block
         */
        /* Get information for slowest changing information */
        spans=space->select.sel_info.hslab->span_lst;

        /* Cycle down the spans until we run out of down spans or find a non-contiguous span */
        while(spans!=NULL) {
            span=spans->head;

            /* Check that this is the only span and it spans the entire dimension */
            if(span->next!=NULL)
                HGOTO_DONE(FALSE)
            else
                /* Walk down to the next span */
                spans=span->down;
        } /* end while */
    } /* end else */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_is_single() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_is_regular
 PURPOSE
    Check if a hyperslab selection is "regular"
 USAGE
    htri_t H5S_hyper_is_regular(space)
        const H5S_t *space;     IN: Dataspace pointer to check
 RETURNS
    TRUE/FALSE/FAIL
 DESCRIPTION
    Checks to see if the current selection in a dataspace is the a regular
    pattern.
    This is primarily used for reading the entire selection in one swoop.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
    Doesn't check for "regular" hyperslab selections composed of spans
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static htri_t
H5S_hyper_is_regular(const H5S_t *space)
{
    htri_t ret_value;  /* return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Check args */
    HDassert(space);

    /* Rebuild diminfo if it is invalid and has not been confirmed to be
     * impossible */
    if(space->select.sel_info.hslab->diminfo_valid == H5S_DIMINFO_VALID_NO)
        /* Casting away const OK -NAF */
        (void)H5S_hyper_rebuild((H5S_t *)space);

    /* Only simple check for regular hyperslabs for now... */
    if(space->select.sel_info.hslab->diminfo_valid == H5S_DIMINFO_VALID_YES)
        ret_value = TRUE;
    else
        ret_value = FALSE;

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_is_regular() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_release
 PURPOSE
    Release hyperslab selection information for a dataspace
 USAGE
    herr_t H5S_hyper_release(space)
        H5S_t *space;       IN: Pointer to dataspace
 RETURNS
    Non-negative on success/Negative on failure
 DESCRIPTION
    Releases all hyperslab selection information for a dataspace
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
 * 	Robb Matzke, 1998-08-25
 *	The fields which are freed are set to NULL to prevent them from being
 *	freed again later.  This fixes some allocation problems where
 *	changing the hyperslab selection of one dataspace causes a core dump
 *	when closing some other dataspace.
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_release(H5S_t *space)
{
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI(FAIL)

    /* Check args */
    HDassert(space && H5S_SEL_HYPERSLABS == H5S_GET_SELECT_TYPE(space));

    /* Reset the number of points selected */
    space->select.num_elem = 0;

    /* Release irregular hyperslab information */
    if(space->select.sel_info.hslab->span_lst != NULL) {
        if(H5S_hyper_free_span_info(space->select.sel_info.hslab->span_lst) < 0)
            HGOTO_ERROR(H5E_INTERNAL, H5E_CANTFREE, FAIL, "failed to release hyperslab spans")
    } /* end if */

    /* Release space for the hyperslab selection information */
    space->select.sel_info.hslab = H5FL_FREE(H5S_hyper_sel_t, space->select.sel_info.hslab);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_release() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_recover_span
 PURPOSE
    Recover a generated span, if appropriate
 USAGE
    herr_t H5S_hyper_recover_span(recover, curr_span, next_span)
        hbool_t *recover;               IN/OUT: Pointer recover flag
        H5S_hyper_span_t **curr_span;   IN/OUT: Pointer to current span in list
        H5S_hyper_span_t *next_span;    IN: Pointer to next span
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Check if the current span needs to be recovered and free it if so.
    Set the current span to the next span in any case.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_recover_span(hbool_t *recover, H5S_hyper_span_t **curr_span, H5S_hyper_span_t *next_span)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    HDassert(recover);
    HDassert(curr_span);

    /* Check if the span should be recovered */
    if(*recover) {
        H5S_hyper_free_span(*curr_span);
        *recover = FALSE;
    } /* end if */

    /* Set the current span to next span */
    *curr_span = next_span;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5S_hyper_recover_span() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_coord_to_span
 PURPOSE
    Create a span tree for a single element
 USAGE
    H5S_hyper_span_t *H5S_hyper_coord_to_span(rank, coords)
        unsigned rank;                  IN: Number of dimensions of coordinate
        hsize_t *coords;               IN: Location of element
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Create a span tree for a single element
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static H5S_hyper_span_t *
H5S_hyper_coord_to_span(unsigned rank, const hsize_t *coords)
{
    H5S_hyper_span_t *new_span;         /* Pointer to new span tree for coordinate */
    H5S_hyper_span_info_t *down = NULL; /* Pointer to new span tree for next level down */
    H5S_hyper_span_t *ret_value = NULL; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(rank > 0);
    HDassert(coords);

    /* Search for location to insert new element in tree */
    if(rank > 1) {
        /* Allocate a span info node */
        if(NULL == (down = H5FL_CALLOC(H5S_hyper_span_info_t)))
            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")

        /* Build span tree for coordinates below this one */
        if(NULL == (down->head = H5S_hyper_coord_to_span(rank - 1, &coords[1])))
            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")

       /* Update the tail pointer of the down dimension, and it's a single span element */
       down->tail = down->head;
    } /* end if */

    /* Build span for this coordinate */
    if(NULL == (new_span = H5S_hyper_new_span(coords[0], coords[0], down, NULL)))
        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")

    /* Set return value */
    ret_value = new_span;

done:
    if(ret_value == NULL && down!=NULL)
        H5S_hyper_free_span_info(down);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_coord_to_span() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_add_span_element_helper
 PURPOSE
    Add a single elment to a span tree
 USAGE
    herr_t H5S_hyper_add_span_element_helper(prev_span, span_tree, rank, coords)
        H5S_hyper_span_info_t *span_tree;  IN/OUT: Pointer to span tree to append to
        unsigned rank;                  IN: Number of dimensions of coordinates
        hsize_t *coords;               IN: Location of element to add to span tree
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Add a single element to an existing span tree.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
    Assumes that the element is not already covered by the span tree

    NOTE: There's an assumption about the context of this function call.
    This function is only called  is only being called from
    H5D_chunk_mem_cb in src/H5Dchunk, when the library is
    iterating over a memory selection, so the coordinates passed
    to H5S_hyper_add_span_element will always be in increasing
    order (according to a row-major (i.e. C, not FORTRAN) scan
    over the dataset. Therefore, for every input of coordinates,
    only the last span element (i.e., the tail pointer) in one
    dimension is checked against the input.
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_add_span_element_helper(H5S_hyper_span_info_t *span_tree,
    unsigned rank, const hsize_t *coords)
{
    H5S_hyper_span_info_t *tspan_info;  /* Temporary pointer to span info */
    H5S_hyper_span_info_t *prev_span_info;  /* Pointer to span info for level above current position */
    H5S_hyper_span_t *tail_span;        /* Pointer to the tail span of one dimension */
    H5S_hyper_span_t *tmp2_span;        /* Another temporary pointer to a span */
    H5S_hyper_span_t *new_span;         /* New span created for element */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(span_tree);
    HDassert(rank > 0);
    HDassert(coords);

    /* Get pointer to last span in span tree */
    tail_span = span_tree->tail;

    /* Find last span tree which includes a portion of the coordinate */
    tspan_info = span_tree;
    prev_span_info = NULL;
    while(coords[0] >= tail_span->low && coords[0] <= tail_span->high) {
        /* Move rank & coordinate offset down a dimension */
        rank--;
        coords++;

        /* Remember the span tree we are descending into */
        prev_span_info = tspan_info;
        tspan_info = tail_span->down;
        HDassert(tspan_info);
        tail_span = tspan_info->tail;
    } /* end while */
    HDassert(rank > 0);

    /* Check if we made it all the way to the bottom span in the tree */
    if(rank > 1) {
        /* Before we create another span at this level in the tree, check if
         * the last span's "down tree" was equal to any other spans in this
         * list of spans in the span tree.
         *
         * If so, release last span information and make last span merge into
         * previous span (if possible), or at least share their "down tree"
         * information.
         */
        tmp2_span = tspan_info->head;
        while(tmp2_span != tail_span) {
            if(TRUE == H5S_hyper_cmp_spans(tmp2_span->down, tail_span->down)) {
                /* Check for merging into previous span */
                if(tmp2_span->high + 1 == tail_span->low) {
                    /* Release last span created */
                    H5S_hyper_free_span(tail_span);

                    /* Increase size of previous span */
                    tmp2_span->high++;
                    tmp2_span->nelem++;

                    /* Reset the 'tmp_span' for the rest of this block's algorithm */
                    tail_span = tmp2_span;
                } /* end if */
                /* Span is disjoint, but has the same "down tree" selection */
                else {
                    /* Release "down tree" information */
                    H5S_hyper_free_span_info(tail_span->down);

                    /* Point at earlier span's "down tree" */
                    tail_span->down = tmp2_span->down;

                    /* Increment reference count on shared "down tree" */
                    tail_span->down->count++;
                } /* end else */

                /* Found span to merge into, break out now */
                break;
            } /* end if */

            /* Advance to next span to check */
            tmp2_span = tmp2_span->next;
        } /* end while */

        /* Make span tree for current coordinates */
        if(NULL == (new_span = H5S_hyper_coord_to_span(rank, coords)))
            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

        /* Add new span tree as span */
        HDassert(tail_span);
        tail_span->next = new_span;
        HDassert(tspan_info);
        tspan_info->tail = new_span;

        /* Set the proper 'pstride' for new span */
        new_span->pstride = new_span->low - tail_span->low;
    } /* end if */
    else {
        /* Does new node adjoin existing node? */
        if(tail_span->high + 1 == coords[0]) {
            tail_span->high++;
            tail_span->nelem++;

            /* Check if this span tree should now be merged with a level higher in the tree */
            if(prev_span_info != NULL) {
                /* Before we create another span at this level in the tree, check if
                 * the last span's "down tree" was equal to any other spans in this
                 * list of spans in the span tree.
                 *
                 * If so, release last span information and make last span merge into
                 * previous span (if possible), or at least share their "down tree"
                 * information.
                 */
                tmp2_span = prev_span_info->head;
                tail_span = prev_span_info->tail;
                while(tmp2_span != tail_span) {
                    if(TRUE == H5S_hyper_cmp_spans(tmp2_span->down, tail_span->down)) {
                        /* Check for merging into previous span */
                        if(tmp2_span->high + 1 == tail_span->low) {
                            /* Release last span created */
                            H5S_hyper_free_span(tail_span);

                            /* Increase size of previous span */
                            tmp2_span->high++;
                            tmp2_span->nelem++;

                            /* Update pointers */
                            tmp2_span->next = NULL;
                            prev_span_info->tail = tmp2_span;
                        } /* end if */
                        /* Span is disjoint, but has the same "down tree" selection */
                        else {
                            /* Release "down tree" information */
                            H5S_hyper_free_span_info(tail_span->down);

                            /* Point at earlier span's "down tree" */
                            tail_span->down = tmp2_span->down;

                            /* Increment reference count on shared "down tree" */
                            tail_span->down->count++;
                        } /* end else */

                        /* Found span to merge into, break out now */
                        break;
                    } /* end if */

                    /* Advance to next span to check */
                    tmp2_span = tmp2_span->next;
                } /* end while */
            } /* end if */
        } /* end if */
        else {
            if(NULL == (new_span = H5S_hyper_new_span(coords[0], coords[0], NULL, NULL)))
                HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

            /* Add new span tree as span */
            HDassert(tail_span);
            tail_span->next = new_span;
            tspan_info->tail = new_span;

            /* Set the proper 'pstride' for new span */
            new_span->pstride = new_span->low - tail_span->low;
        } /* end else */
    } /* end else */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_add_span_element_helper() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_add_span_element
 PURPOSE
    Add a single elment to a span tree
 USAGE
    herr_t H5S_hyper_add_span_element(space, span_tree, rank, coords)
        H5S_t *space;           IN/OUT: Pointer to dataspace to add coordinate to
        unsigned rank;          IN: Number of dimensions of coordinates
        hsize_t *coords;       IN: Location of element to add to span tree
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Add a single element to an existing span tree.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
    Assumes that the element is not already in the dataspace's selection
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
herr_t
H5S_hyper_add_span_element(H5S_t *space, unsigned rank, const hsize_t *coords)
{
    H5S_hyper_span_info_t *head = NULL;    /* Pointer to new head of span tree */
    herr_t      ret_value = SUCCEED;       /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(space);
    HDassert(rank > 0);
    HDassert(coords);
    HDassert(space->extent.rank == rank);

    /* Check if this is the first element in the selection */
    if(NULL == space->select.sel_info.hslab) {
        /* Allocate a span info node */
        if(NULL == (head = H5FL_CALLOC(H5S_hyper_span_info_t)))
            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

        /* Set the reference count */
        head->count = 1;

        /* Build span tree for this coordinate */
        if(NULL == (head->head = H5S_hyper_coord_to_span(rank, coords)))
            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

        /* Update the tail pointer of this newly created span in dimension "rank" */
        head->tail = head->head;

        /* Allocate selection info */
        if(NULL == (space->select.sel_info.hslab = H5FL_MALLOC(H5S_hyper_sel_t)))
            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab info")

        /* Set the selection to the new span tree */
        space->select.sel_info.hslab->span_lst = head;

        /* Set selection type */
        space->select.type = H5S_sel_hyper;

        /* Reset "regular" hyperslab flag */
        space->select.sel_info.hslab->diminfo_valid = H5S_DIMINFO_VALID_NO;

        /* Set # of elements in selection */
        space->select.num_elem = 1;
    } /* end if */
    else {
        if(H5S_hyper_add_span_element_helper(space->select.sel_info.hslab->span_lst, rank, coords) < 0)
            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

        /* Increment # of elements in selection */
        space->select.num_elem++;
    } /* end else */

done:
    if(ret_value < 0)
        if(head)
            H5S_hyper_free_span_info(head);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_add_span_element() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_reset_scratch
 PURPOSE
    Reset the scratch information for span tree
 USAGE
    herr_t H5S_hyper_reset_scratch(space)
        H5S_t *space;           IN/OUT: Pointer to dataspace to reset scratch pointers
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Resets the "scratch" pointers used for various tasks in computing hyperslab
    spans.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
herr_t
H5S_hyper_reset_scratch(H5S_t *space)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    HDassert(space);

    /* Check if there are spans in the span tree */
    if(space->select.sel_info.hslab->span_lst != NULL)
        /* Reset the scratch pointers for the next routine which needs them */
        H5S_hyper_span_scratch(space->select.sel_info.hslab->span_lst, NULL);

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5S_hyper_reset_scratch() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_convert
 PURPOSE
    Convert a compatible selection to span tree form
 USAGE
    herr_t H5S_hyper_convert(space)
        H5S_t *space;           IN/OUT: Pointer to dataspace to convert
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Converts a compatible selection (currently only "all" selections) to the
    span-tree form of a hyperslab selection. (Point and "none" selection aren't
    currently supported and hyperslab selection always have the span-tree form
    available).
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
herr_t
H5S_hyper_convert(H5S_t *space)
{
    herr_t      ret_value=SUCCEED;       /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(space);

    /* Check the type of selection */
    switch(H5S_GET_SELECT_TYPE(space)) {
        case H5S_SEL_ALL:    /* All elements selected in dataspace */
            /* Convert current "all" selection to "real" hyperslab selection */
            {
                hsize_t tmp_start[H5O_LAYOUT_NDIMS];   /* Temporary start information */
                hsize_t tmp_stride[H5O_LAYOUT_NDIMS];   /* Temporary stride information */
                hsize_t tmp_count[H5O_LAYOUT_NDIMS];    /* Temporary count information */
                hsize_t tmp_block[H5O_LAYOUT_NDIMS];    /* Temporary block information */
                unsigned u;                             /* Local index variable */

                /* Fill in temporary information for the dimensions */
                for(u=0; u<space->extent.rank; u++) {
                    tmp_start[u]=0;
                    tmp_stride[u]=1;
                    tmp_count[u]=1;
                    tmp_block[u]=space->extent.size[u];
                } /* end for */

                /* Convert to hyperslab selection */
                if(H5S_select_hyperslab(space,H5S_SELECT_SET,tmp_start,tmp_stride,tmp_count,tmp_block)<0)
                    HGOTO_ERROR(H5E_DATASPACE, H5E_CANTDELETE, FAIL, "can't convert selection")
            } /* end case */
            break;

        case H5S_SEL_HYPERSLABS:        /* Hyperslab selection */
            break;

        case H5S_SEL_NONE:   /* No elements selected in dataspace */
        case H5S_SEL_POINTS: /* Point selection */
        case H5S_SEL_ERROR:  /* Selection error */
        case H5S_SEL_N:      /* Selection count */
        default:
            HGOTO_ERROR(H5E_ARGS, H5E_UNSUPPORTED, FAIL, "can't convert to span tree selection")
    } /* end switch */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_convert() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_intersect_block_helper
 PURPOSE
    Helper routine to detect intersections in span trees
 USAGE
    htri_t H5S_hyper_intersect_block_helper(spans, start, end)
        H5S_hyper_span_info_t *spans;     IN: First span tree to operate with
        hssize_t *offset;   IN: Selection offset coordinate
        hsize_t *start;    IN: Starting coordinate for block
        hsize_t *end;      IN: Ending coordinate for block
 RETURN
    Non-negative on success, negative on failure
 DESCRIPTION
    Quickly detect intersections between span tree and block
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static htri_t
H5S_hyper_intersect_block_helper(const H5S_hyper_span_info_t *spans,
    const hsize_t *start, const hsize_t *end)
{
    H5S_hyper_span_t *curr;     /* Pointer to current span in 1st span tree */
    htri_t status;              /* Status from recursive call */
    htri_t ret_value=FALSE;     /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Sanity check */
    HDassert(spans);
    HDassert(start);
    HDassert(end);

    /* Get the span list for spans in this tree */
    curr=spans->head;

    /* Iterate over the spans in the tree */
    while(curr!=NULL) {
        /* Check for span entirely before block */
        if(curr->high < *start)
            /* Advance to next span in this dimension */
            curr=curr->next;
        /* If this span is past the end of the block, then we're done in this dimension */
        else if(curr->low > *end)
            HGOTO_DONE(FALSE)
        /* block & span overlap */
        else {
            if(curr->down==NULL)
                HGOTO_DONE(TRUE)
            else {
                /* Recursively check spans in next dimension down */
                if((status=H5S_hyper_intersect_block_helper(curr->down,start+1,end+1))<0)
                    HGOTO_ERROR(H5E_DATASPACE, H5E_BADSELECT, FAIL, "can't perform hyperslab intersection check")

                /* If there is a span intersection in the down dimensions, the span trees overlap */
                if(status==TRUE)
                    HGOTO_DONE(TRUE);

                /* No intersection in down dimensions, advance to next span */
                curr=curr->next;
            } /* end else */
        } /* end else */
    } /* end while */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_intersect_block_helper() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_intersect_block
 PURPOSE
    Detect intersections in span trees
 USAGE
    htri_t H5S_hyper_intersect_block(space, start, end)
        H5S_t *space;       IN: First dataspace to operate on span tree
        hssize_t *start;    IN: Starting coordinate for block
        hssize_t *end;      IN: Ending coordinate for block
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Quickly detect intersections between span tree and block
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
htri_t
H5S_hyper_intersect_block(H5S_t *space, const hsize_t *start, const hsize_t *end)
{
    htri_t ret_value = FAIL;      /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Sanity check */
    HDassert(space);
    HDassert(start);
    HDassert(end);

    /* Check for 'all' selection, instead of a hyperslab selection */
    /* (Technically, this shouldn't be in the "hyperslab" routines...) */
    if(H5S_GET_SELECT_TYPE(space) == H5S_SEL_ALL)
        HGOTO_DONE(TRUE);

    /* Check that the space selection has a span tree */
    if(space->select.sel_info.hslab->span_lst == NULL)
        if(H5S_hyper_generate_spans(space) < 0)
            HGOTO_ERROR(H5E_DATASPACE, H5E_UNINITIALIZED, FAIL, "dataspace does not have span tree")

    /* Perform the span-by-span intersection check */
    if((ret_value = H5S_hyper_intersect_block_helper(space->select.sel_info.hslab->span_lst, start, end)) < 0)
        HGOTO_ERROR(H5E_DATASPACE, H5E_BADSELECT, FAIL, "can't perform hyperslab intersection check")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_intersect_block() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_adjust_u_helper
 PURPOSE
    Helper routine to adjust offsets in span trees
 USAGE
    herr_t H5S_hyper_adjust_u_helper(spans, offset)
        H5S_hyper_span_info_t *spans;   IN: Span tree to operate with
        const hsize_t *offset;         IN: Offset to subtract
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Adjust the location of the spans in a span tree by subtracting an offset
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_adjust_u_helper(H5S_hyper_span_info_t *spans, const hsize_t *offset)
{
    H5S_hyper_span_t *span;     /* Pointer to current span in span tree */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    HDassert(spans);
    HDassert(offset);

    /* Check if we've already set this down span tree */
    if(spans->scratch!=(H5S_hyper_span_info_t *)~((size_t)NULL)) {
        /* Set the tree's scratch pointer */
        spans->scratch=(H5S_hyper_span_info_t *)~((size_t)NULL);

        /* Get the span lists for each span in this tree */
        span=spans->head;

        /* Iterate over the spans in tree */
        while(span!=NULL) {
            /* Adjust span offset */
            HDassert(span->low>=*offset);
            span->low-=*offset;
            span->high-=*offset;

            /* Recursively adjust spans in next dimension down */
            if(span->down!=NULL)
                H5S_hyper_adjust_u_helper(span->down,offset+1);

            /* Advance to next span in this dimension */
            span=span->next;
        } /* end while */
    } /* end if */

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5S_hyper_adjust_u_helper() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_adjust_u
 PURPOSE
    Adjust a hyperslab selection by subtracting an offset
 USAGE
    herr_t H5S_hyper_adjust_u(space,offset)
        H5S_t *space;           IN/OUT: Pointer to dataspace to adjust
        const hsize_t *offset; IN: Offset to subtract
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Moves a hyperslab selection by subtracting an offset from it.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_adjust_u(H5S_t *space, const hsize_t *offset)
{
    hbool_t update_bound = FALSE;       /* Indicate whether to update bound */
    unsigned u;                         /* Local index variable */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(space);
    HDassert(offset);

    /* Subtract the offset from the "regular" coordinates, if they exist */
    /* (No need to rebuild the dimension info yet -QAK) */
    if(space->select.sel_info.hslab->diminfo_valid == H5S_DIMINFO_VALID_YES) {
        for(u = 0; u < space->extent.rank; u++) {
            HDassert(space->select.sel_info.hslab->opt_diminfo[u].start >= offset[u]);
            space->select.sel_info.hslab->opt_diminfo[u].start -= offset[u];
        } /* end for */

        update_bound = TRUE;
    } /* end if */

    /* Subtract the offset from the span tree coordinates, if they exist */
    if(space->select.sel_info.hslab->span_lst) {
        if(H5S_hyper_adjust_u_helper(space->select.sel_info.hslab->span_lst, offset) < 0)
            HGOTO_ERROR(H5E_DATASPACE, H5E_BADSELECT, FAIL, "can't perform hyperslab offset adjustment")

        /* Reset the scratch pointers for the next routine which needs them */
        H5S_hyper_span_scratch(space->select.sel_info.hslab->span_lst, NULL);

        update_bound = TRUE;
    } /* end if */

    if(update_bound) {
        /* Update the bound box of this hyperslab */
        for(u = 0; u < space->extent.rank; u++) {
            HDassert(space->select.low_bounds[u] >= offset[u]);
            space->select.low_bounds[u] -= offset[u];
            space->select.high_bounds[u] -= offset[u];
        } /* end for */
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_adjust_u() */


/*-------------------------------------------------------------------------
 * Function:	H5S_hyper_project_scalar
 *
 * Purpose:	Projects a single element hyperslab selection into a scalar
 *              dataspace
 *
 * Return:	non-negative on success, negative on failure.
 *
 * Programmer:	Quincey Koziol
 *              Sunday, July 18, 2010
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5S_hyper_project_scalar(const H5S_t *space, hsize_t *offset)
{
    hsize_t block[H5S_MAX_RANK];     /* Block selected in base dataspace */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Check args */
    HDassert(space && H5S_SEL_HYPERSLABS == H5S_GET_SELECT_TYPE(space));
    HDassert(offset);

    /* Check for a "regular" hyperslab selection */
    /* (No need to rebuild the dimension info yet -QAK) */
    if(space->select.sel_info.hslab->diminfo_valid == H5S_DIMINFO_VALID_YES) {
        const H5S_hyper_dim_t *diminfo = space->select.sel_info.hslab->opt_diminfo; /* Alias for dataspace's diminfo information */
        unsigned u;                 /* Counter */

        /* Build the table of the initial offset */
        for(u = 0; u < space->extent.rank; u++) {
            block[u] = diminfo[u].start;

            /* Check for more than one hyperslab */
            if(diminfo[u].count > 1 || diminfo[u].block > 1)
                HGOTO_ERROR(H5E_DATASPACE, H5E_BADRANGE, FAIL, "hyperslab selection of one element has more than one node!")
        } /* end for */
    } /* end if */
    else {
        const H5S_hyper_span_t *curr;           /* Pointer to current hyperslab span */
        unsigned curr_dim;                      /* Current dimension being operated on */

        /* Advance down selected spans */
        curr = space->select.sel_info.hslab->span_lst->head;
        curr_dim = 0;
        while(curr) {
            /* Check for more than one span */
            if(curr->next || curr->low != curr->high)
                HGOTO_ERROR(H5E_DATASPACE, H5E_BADRANGE, FAIL, "hyperslab selection of one element has more than one node!")

            /* Save the location of the selection in current dimension */
            block[curr_dim] = curr->low;

            /* Advance down to next dimension */
            curr = curr->down->head;
            curr_dim++;
        } /* end while */
    } /* end else */

    /* Calculate offset of selection in projected buffer */
    *offset = H5VM_array_offset(space->extent.rank, space->extent.size, block);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_project_scalar() */


/*-------------------------------------------------------------------------
 * Function:	H5S_hyper_project_simple_lower
 *
 * Purpose:	Projects a hyperslab selection onto/into a simple dataspace
 *              of a lower rank
 *
 * Return:	non-negative on success, negative on failure.
 *
 * Programmer:	Quincey Koziol
 *              Sunday, July 18, 2010
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5S_hyper_project_simple_lower(const H5S_t *base_space, H5S_t *new_space)
{
    H5S_hyper_span_info_t *down;        /* Pointer to list of spans */
    unsigned curr_dim;                  /* Current dimension being operated on */
    unsigned u;                         /* Local index variable */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Check args */
    HDassert(base_space && H5S_SEL_HYPERSLABS == H5S_GET_SELECT_TYPE(base_space));
    HDassert(new_space);
    HDassert(new_space->extent.rank < base_space->extent.rank);

    /* Walk down the span tree until we reach the selection to project */
    down = base_space->select.sel_info.hslab->span_lst;
    curr_dim = 0;
    while(down && curr_dim < (base_space->extent.rank - new_space->extent.rank)) {
        /* Sanity check */
        HDassert(NULL == down->head->next);

        /* Advance down to next dimension */
        down = down->head->down;
        curr_dim++;
    } /* end while */
    HDassert(down);

    /* Update the bounding box */
    for(u = 0; u < new_space->extent.rank; u++) {
        new_space->select.low_bounds[u] = base_space->select.low_bounds[u + curr_dim];
        new_space->select.high_bounds[u] = base_space->select.high_bounds[u + curr_dim];
    } /* end for */

    /* Share the underlying hyperslab span information */
    new_space->select.sel_info.hslab->span_lst = down;
    new_space->select.sel_info.hslab->span_lst->count++;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5S_hyper_project_simple_lower() */


/*-------------------------------------------------------------------------
 * Function:	H5S_hyper_project_simple_higher
 *
 * Purpose:	Projects a hyperslab selection onto/into a simple dataspace
 *              of a higher rank
 *
 * Return:	non-negative on success, negative on failure.
 *
 * Programmer:	Quincey Koziol
 *              Sunday, July 18, 2010
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5S_hyper_project_simple_higher(const H5S_t *base_space, H5S_t *new_space)
{
    H5S_hyper_span_t *prev_span = NULL; /* Pointer to previous list of spans */
    unsigned curr_dim;                  /* Current dimension being operated on */
    unsigned u;                         /* Local index variable */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Check args */
    HDassert(base_space && H5S_SEL_HYPERSLABS == H5S_GET_SELECT_TYPE(base_space));
    HDassert(new_space);
    HDassert(new_space->extent.rank > base_space->extent.rank);

    /* Create nodes until reaching the correct # of dimensions */
    new_space->select.sel_info.hslab->span_lst = NULL;
    curr_dim = 0;
    while(curr_dim < (new_space->extent.rank - base_space->extent.rank)) {
        H5S_hyper_span_info_t *new_span_info;  /* Pointer to list of spans */
        H5S_hyper_span_t *new_span;     /* Temporary hyperslab span */

        /* Allocate a new span_info node */
        if(NULL == (new_span_info = H5FL_CALLOC(H5S_hyper_span_info_t))) {
            if(prev_span)
                if(H5S_hyper_free_span(prev_span) < 0)
                    HERROR(H5E_DATASPACE, H5E_CANTFREE, "can't free hyperslab span");
            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate hyperslab span info")
        } /* end if */

        /* Check for linking into higher span */
        if(prev_span)
            prev_span->down = new_span_info;

        /* Allocate a new node */
        if(NULL == (new_span = H5S_hyper_new_span((hsize_t)0, (hsize_t)0, NULL, NULL))) {
            HDassert(new_span_info);
            if(!prev_span)
                (void)H5FL_FREE(H5S_hyper_span_info_t, new_span_info);
            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, FAIL, "can't allocate hyperslab span")
        } /* end if */

        /* Set the span_info information */
        new_span_info->count = 1;
        new_span_info->head = new_span;
        new_span_info->tail = new_span;

        /* Attach to new space, if top span info */
        if(NULL == new_space->select.sel_info.hslab->span_lst)
            new_space->select.sel_info.hslab->span_lst = new_span_info;

        /* Remember previous span info */
        prev_span = new_span;

        /* Advance to next dimension */
        curr_dim++;
    } /* end while */
    HDassert(new_space->select.sel_info.hslab->span_lst);
    HDassert(prev_span);

    /* Update the bounding box */
    for(u = 0; u < curr_dim; u++) {
        new_space->select.low_bounds[u] = 0;
        new_space->select.high_bounds[u] = 0;
    } /* end for */
    for(; u < new_space->extent.rank; u++) {
        new_space->select.low_bounds[u] = base_space->select.low_bounds[u - curr_dim];
        new_space->select.high_bounds[u] = base_space->select.high_bounds[u - curr_dim];
    } /* end for */

    /* Share the underlying hyperslab span information */
    prev_span->down = base_space->select.sel_info.hslab->span_lst;
    prev_span->down->count++;

done:
    if(ret_value < 0 && new_space->select.sel_info.hslab->span_lst) {
        if(new_space->select.sel_info.hslab->span_lst->head)
            if(H5S_hyper_free_span(
                    new_space->select.sel_info.hslab->span_lst->head) < 0)
                HDONE_ERROR(H5E_DATASPACE, H5E_CANTFREE, FAIL, "can't free hyperslab span")

        new_space->select.sel_info.hslab->span_lst = H5FL_FREE(H5S_hyper_span_info_t, new_space->select.sel_info.hslab->span_lst);
    } /* end if */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_project_simple_higher() */


/*-------------------------------------------------------------------------
 * Function:	H5S_hyper_project_simple
 *
 * Purpose:	Projects a hyperslab selection onto/into a simple dataspace
 *              of a different rank
 *
 * Return:	non-negative on success, negative on failure.
 *
 * Programmer:	Quincey Koziol
 *              Sunday, July 18, 2010
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5S_hyper_project_simple(const H5S_t *base_space, H5S_t *new_space, hsize_t *offset)
{
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Check args */
    HDassert(base_space && H5S_SEL_HYPERSLABS == H5S_GET_SELECT_TYPE(base_space));
    HDassert(new_space);
    HDassert(offset);

    /* We are setting a new selection, remove any current selection in new dataspace */
    if(H5S_SELECT_RELEASE(new_space) < 0)
        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTDELETE, FAIL, "can't release selection")

    /* Allocate space for the hyperslab selection information */
    if(NULL == (new_space->select.sel_info.hslab = H5FL_MALLOC(H5S_hyper_sel_t)))
        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab info")

    /* Check for a "regular" hyperslab selection */
    /* (No need to rebuild the dimension info yet -QAK) */
    if(base_space->select.sel_info.hslab->diminfo_valid == H5S_DIMINFO_VALID_YES) {
        unsigned base_space_dim;    /* Current dimension in the base dataspace */
        unsigned new_space_dim;     /* Current dimension in the new dataspace */
        unsigned u;                 /* Local index variable */

        /* Check if the new space's rank is < or > base space's rank */
        if(new_space->extent.rank < base_space->extent.rank) {
            const H5S_hyper_dim_t *opt_diminfo = base_space->select.sel_info.hslab->opt_diminfo; /* Alias for dataspace's diminfo information */
            hsize_t block[H5S_MAX_RANK];     /* Block selected in base dataspace */

            /* Compute the offset for the down-projection */
            HDmemset(block, 0, sizeof(block));
            for(u = 0; u < (base_space->extent.rank - new_space->extent.rank); u++)
                block[u] = opt_diminfo[u].start;
            *offset = H5VM_array_offset(base_space->extent.rank, base_space->extent.size, block);

            /* Set the correct dimensions for the base & new spaces */
            base_space_dim = base_space->extent.rank - new_space->extent.rank;
            new_space_dim = 0;
        } /* end if */
        else {
            HDassert(new_space->extent.rank > base_space->extent.rank);

            /* The offset is zero when projected into higher dimensions */
            *offset = 0;

            /* Set the diminfo information for the higher dimensions */
            for(new_space_dim = 0; new_space_dim < (new_space->extent.rank - base_space->extent.rank); new_space_dim++) {
                new_space->select.sel_info.hslab->app_diminfo[new_space_dim].start = 0;
                new_space->select.sel_info.hslab->app_diminfo[new_space_dim].stride = 1;
                new_space->select.sel_info.hslab->app_diminfo[new_space_dim].count = 1;
                new_space->select.sel_info.hslab->app_diminfo[new_space_dim].block = 1;

                new_space->select.sel_info.hslab->opt_diminfo[new_space_dim].start = 0;
                new_space->select.sel_info.hslab->opt_diminfo[new_space_dim].stride = 1;
                new_space->select.sel_info.hslab->opt_diminfo[new_space_dim].count = 1;
                new_space->select.sel_info.hslab->opt_diminfo[new_space_dim].block = 1;
            } /* end for */

            /* Start at beginning of base space's dimension info */
            base_space_dim = 0;
        } /* end else */

        /* Copy the diminfo */
        while(base_space_dim < base_space->extent.rank) {
            new_space->select.sel_info.hslab->app_diminfo[new_space_dim].start =
                    base_space->select.sel_info.hslab->app_diminfo[base_space_dim].start;
            new_space->select.sel_info.hslab->app_diminfo[new_space_dim].stride =
                    base_space->select.sel_info.hslab->app_diminfo[base_space_dim].stride;
            new_space->select.sel_info.hslab->app_diminfo[new_space_dim].count =
                    base_space->select.sel_info.hslab->app_diminfo[base_space_dim].count;
            new_space->select.sel_info.hslab->app_diminfo[new_space_dim].block =
                    base_space->select.sel_info.hslab->app_diminfo[base_space_dim].block;

            new_space->select.sel_info.hslab->opt_diminfo[new_space_dim].start =
                    base_space->select.sel_info.hslab->opt_diminfo[base_space_dim].start;
            new_space->select.sel_info.hslab->opt_diminfo[new_space_dim].stride =
                    base_space->select.sel_info.hslab->opt_diminfo[base_space_dim].stride;
            new_space->select.sel_info.hslab->opt_diminfo[new_space_dim].count =
                    base_space->select.sel_info.hslab->opt_diminfo[base_space_dim].count;
            new_space->select.sel_info.hslab->opt_diminfo[new_space_dim].block =
                    base_space->select.sel_info.hslab->opt_diminfo[base_space_dim].block;

            /* Advance to next dimensions */
            base_space_dim++;
            new_space_dim++;
        } /* end for */

        /* Update the bounding box */
        for(u = 0; u < new_space->extent.rank; u++) {
            new_space->select.low_bounds[u] = new_space->select.sel_info.hslab->opt_diminfo[u].start;
            new_space->select.high_bounds[u] = new_space->select.low_bounds[u] + new_space->select.sel_info.hslab->opt_diminfo[u].stride * (new_space->select.sel_info.hslab->opt_diminfo[u].count - 1) + (new_space->select.sel_info.hslab->opt_diminfo[u].block - 1);
        } /* end for */

        /* Indicate that the dimension information is valid */
        new_space->select.sel_info.hslab->diminfo_valid = H5S_DIMINFO_VALID_YES;

        /* Indicate that there's no slab information */
        new_space->select.sel_info.hslab->span_lst = NULL;
    } /* end if */
    else {
        /* Check if the new space's rank is < or > base space's rank */
        if(new_space->extent.rank < base_space->extent.rank) {
            const H5S_hyper_span_t *curr;    /* Pointer to current hyperslab span */
            hsize_t block[H5S_MAX_RANK];     /* Block selected in base dataspace */
            unsigned curr_dim;               /* Current dimension being operated on */

            /* Clear the block buffer */
            HDmemset(block, 0, sizeof(block));

            /* Advance down selected spans */
            curr = base_space->select.sel_info.hslab->span_lst->head;
            curr_dim = 0;
            while(curr && curr_dim < (base_space->extent.rank - new_space->extent.rank)) {
                /* Save the location of the selection in current dimension */
                block[curr_dim] = curr->low;

                /* Advance down to next dimension */
                curr = curr->down->head;
                curr_dim++;
            } /* end while */

            /* Compute the offset for the down-projection */
            *offset = H5VM_array_offset(base_space->extent.rank, base_space->extent.size, block);

            /* Project the base space's selection down in less dimensions */
            if(H5S_hyper_project_simple_lower(base_space, new_space) < 0)
                HGOTO_ERROR(H5E_DATASPACE, H5E_CANTSELECT, FAIL, "can't project hyperslab selection into less dimensions")
        } /* end if */
        else {
            HDassert(new_space->extent.rank > base_space->extent.rank);

            /* The offset is zero when projected into higher dimensions */
            *offset = 0;

            /* Project the base space's selection down in more dimensions */
            if(H5S_hyper_project_simple_higher(base_space, new_space) < 0)
                HGOTO_ERROR(H5E_DATASPACE, H5E_CANTSELECT, FAIL, "can't project hyperslab selection into less dimensions")
        } /* end else */

        /* Copy the status of the dimension information */
        new_space->select.sel_info.hslab->diminfo_valid = base_space->select.sel_info.hslab->diminfo_valid;
    } /* end else */

    /* Number of elements selected will be the same */
    new_space->select.num_elem = base_space->select.num_elem;

    /* Set selection type */
    new_space->select.type = H5S_sel_hyper;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_project_simple() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_adjust_s_helper
 PURPOSE
    Helper routine to adjust offsets in span trees
 USAGE
    herr_t H5S_hyper_adjust_s_helper(spans, offset)
        H5S_hyper_span_info_t *spans;   IN: Span tree to operate with
        const hssize_t *offset;         IN: Offset to subtract
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Adjust the location of the spans in a span tree by subtracting an offset
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_adjust_s_helper(H5S_hyper_span_info_t *spans, const hssize_t *offset)
{
    H5S_hyper_span_t *span;     /* Pointer to current span in span tree */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    HDassert(spans);
    HDassert(offset);

    /* Check if we've already set this down span tree */
    if(spans->scratch != (H5S_hyper_span_info_t *)~((size_t)NULL)) {
        /* Set the tree's scratch pointer */
        spans->scratch = (H5S_hyper_span_info_t *)~((size_t)NULL);

        /* Get the span lists for each span in this tree */
        span = spans->head;

        /* Iterate over the spans in tree */
        while(span != NULL) {
            /* Adjust span offset */
            HDassert((hssize_t)span->low >= *offset);
            span->low = (hsize_t)((hssize_t)span->low - *offset);
            span->high = (hsize_t)((hssize_t)span->high - *offset);

            /* Recursively adjust spans in next dimension down */
            if(span->down != NULL)
                H5S_hyper_adjust_s_helper(span->down, offset + 1);

            /* Advance to next span in this dimension */
            span = span->next;
        } /* end while */
    } /* end if */

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5S_hyper_adjust_s_helper() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_adjust_s
 PURPOSE
    Adjust a hyperslab selection by subtracting an offset
 USAGE
    herr_t H5S_hyper_adjust_s(space,offset)
        H5S_t *space;           IN/OUT: Pointer to dataspace to adjust
        const hssize_t *offset; IN: Offset to subtract
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Moves a hyperslab selection by subtracting an offset from it.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
herr_t
H5S_hyper_adjust_s(H5S_t *space, const hssize_t *offset)
{
    hbool_t update_bound = FALSE;       /* Indicate whether to update bound */
    unsigned u;                         /* Local index variable */
    herr_t      ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(space);
    HDassert(offset);

    /* Subtract the offset from the "regular" coordinates, if they exist */
    /* (No need to rebuild the dimension info yet -QAK) */
    if(space->select.sel_info.hslab->diminfo_valid == H5S_DIMINFO_VALID_YES) {
        for(u = 0; u < space->extent.rank; u++) {
            HDassert((hssize_t)space->select.sel_info.hslab->opt_diminfo[u].start >= offset[u]);
            space->select.sel_info.hslab->opt_diminfo[u].start = (hsize_t)((hssize_t)space->select.sel_info.hslab->opt_diminfo[u].start - offset[u]);
        } /* end for */

        update_bound = TRUE;
    } /* end if */

    /* Subtract the offset from the span tree coordinates, if they exist */
    if(space->select.sel_info.hslab->span_lst) {
        if(H5S_hyper_adjust_s_helper(space->select.sel_info.hslab->span_lst, offset) < 0)
            HGOTO_ERROR(H5E_DATASPACE, H5E_BADSELECT, FAIL, "can't perform hyperslab offset adjustment")

        /* Reset the scratch pointers for the next routine which needs them */
        H5S_hyper_span_scratch(space->select.sel_info.hslab->span_lst, NULL);

        update_bound = TRUE;
    } /* end if */

    if(update_bound) {
        /* Update the bound box of this hyperslab */
        for(u = 0; u < space->extent.rank; u++) {
            hssize_t tmp = (hssize_t)space->select.low_bounds[u];

            HDassert(tmp >= offset[u]);
            space->select.low_bounds[u] = (hsize_t)(tmp - offset[u]);
            tmp = (hssize_t)space->select.high_bounds[u];
            space->select.high_bounds[u] = (hsize_t)(tmp - offset[u]);
        } /* end for */
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_adjust_s() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_normalize_offset
 PURPOSE
    "Normalize" a hyperslab selection by adjusting it's coordinates by the
    amount of the selection offset.
 USAGE
    herr_t H5S_hyper_normalize_offset(space, old_offset)
        H5S_t *space;           IN/OUT: Pointer to dataspace to move
        hssize_t *old_offset;   OUT: Pointer to space to store old offset
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Copies the current selection offset into the array provided, then
    inverts the selection offset, subtracts the offset from the hyperslab
    selection and resets the offset to zero.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
htri_t
H5S_hyper_normalize_offset(H5S_t *space, hssize_t *old_offset)
{
    unsigned u;                         /* Local index variable */
    herr_t ret_value = FALSE;           /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(space);

    /* Check for hyperslab selection & offset changed */
    if(H5S_GET_SELECT_TYPE(space) == H5S_SEL_HYPERSLABS && space->select.offset_changed) {
        /* Copy & invert the selection offset */
        for(u = 0; u<space->extent.rank; u++) {
            old_offset[u] = space->select.offset[u];
            space->select.offset[u] = -space->select.offset[u];
        } /* end for */

        /* Call the existing 'adjust' routine */
        if(H5S_hyper_adjust_s(space, space->select.offset) < 0)
            HGOTO_ERROR(H5E_DATASPACE, H5E_BADSELECT, FAIL, "can't perform hyperslab normalization")

        /* Zero out the selection offset */
        HDmemset(space->select.offset, 0, sizeof(hssize_t) * space->extent.rank);

        /* Indicate that the offset was normalized */
        ret_value = TRUE;
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_normalize_offset() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_denormalize_offset
 PURPOSE
    "Denormalize" a hyperslab selection by reverse adjusting it's coordinates
    by the amount of the former selection offset.
 USAGE
    herr_t H5S_hyper_normalize_offset(space, old_offset)
        H5S_t *space;           IN/OUT: Pointer to dataspace to move
        hssize_t *old_offset;   IN: Pointer to old offset array
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Subtracts the old offset from the current selection (canceling out the
    effect of the "normalize" routine), then restores the old offset into
    the dataspace.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
herr_t
H5S_hyper_denormalize_offset(H5S_t *space, const hssize_t *old_offset)
{
    herr_t      ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(space);
    HDassert(H5S_GET_SELECT_TYPE(space) == H5S_SEL_HYPERSLABS);

    /* Call the existing 'adjust' routine */
    if(H5S_hyper_adjust_s(space, old_offset) < 0)
        HGOTO_ERROR(H5E_DATASPACE, H5E_BADSELECT, FAIL, "can't perform hyperslab normalization")

    /* Copy the selection offset over */
    HDmemcpy(space->select.offset, old_offset, sizeof(hssize_t) * space->extent.rank);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_denormalize_offset() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_append_span
 PURPOSE
    Create a new span and append to span list
 USAGE
    herr_t H5S_hyper_append_span(prev_span, span_tree, low, high, down, next)
        H5S_hyper_span_t **prev_span;    IN/OUT: Pointer to previous span in list
        H5S_hyper_span_info_t **span_tree;  IN/OUT: Pointer to span tree to append to
        hsize_t low, high;         IN: Low and high bounds for new span node
        H5S_hyper_span_info_t *down;     IN: Down span tree for new node
        H5S_hyper_span_t *next;     IN: Next span for new node
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Create a new span node and append to a span list.  Update the previous
    span in the list also.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_append_span(H5S_hyper_span_t **prev_span, H5S_hyper_span_info_t **span_tree,
    hsize_t low, hsize_t high, H5S_hyper_span_info_t *down, H5S_hyper_span_t *next)
{
    H5S_hyper_span_t *new_span = NULL;
    herr_t      ret_value = SUCCEED;       /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(prev_span);
    HDassert(span_tree);

    /* Check for adding first node to merged spans */
    if(*prev_span == NULL) {
        /* Allocate new span node to append to list */
        if(NULL == (new_span = H5S_hyper_new_span(low, high, down, next)))
            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

        /* Make first node in span list */

        /* Check that we haven't already allocated a span tree */
        HDassert(*span_tree == NULL);

        /* Allocate a new span_info node */
        if(NULL == (*span_tree = H5FL_CALLOC(H5S_hyper_span_info_t)))
            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

        /* Set the span tree's basic information */
        (*span_tree)->count = 1;
        (*span_tree)->head = new_span;
        (*span_tree)->tail = new_span;

        /* Update previous merged span */
        *prev_span = new_span;
    } /* end if */
    /* Merge or append to existing merged spans list */
    else {
        /* Check if span can just extend the previous merged span */
        if((((*prev_span)->high + 1) == low) &&
                TRUE == H5S_hyper_cmp_spans(down, (*prev_span)->down)) {
            /* Extend previous merged span to include new high bound */
            (*prev_span)->high = high;
            (*prev_span)->nelem += (high - low) + 1;
        } /* end if */
        else {
            /* Allocate new span node to append to list */
            if(NULL == (new_span = H5S_hyper_new_span(low, high, down, next)))
                HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

            /* Check if there is actually a down span */
            if(new_span->down) {
                /* Check if the down spans for the new span node are the same as the previous span node */
                if(TRUE == H5S_hyper_cmp_spans(new_span->down, (*prev_span)->down)) {
                    /* Release the down span for the new node */
                    H5S_hyper_free_span_info(new_span->down);

                    /* Point the new node's down span at the previous node's down span */
                    new_span->down = (*prev_span)->down;

                    /* Increment the reference count to the shared down span */
                    new_span->down->count++;
                } /* end if */
            } /* end if */

            /* Indicate elements from previous span */
            new_span->pstride = low - (*prev_span)->low;

            /* Append to end of merged spans list */
            (*prev_span)->next = new_span;
            *prev_span = new_span;

            (*span_tree)->tail = new_span;
        } /* end else */
    } /* end else */

done:
    if(ret_value < 0)
        if(new_span && H5S_hyper_free_span(new_span) < 0)
            HDONE_ERROR(H5E_DATASPACE, H5E_CANTFREE, FAIL, "failed to release new hyperslab span")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_append_span() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_clip_spans
 PURPOSE
    Clip a new span tree against the current spans in the hyperslab selection
 USAGE
    herr_t H5S_hyper_clip_spans(span_a, span_b, selector, cur_dim, dim_size,
                                span_a_b_bounds[4], all_clips_bound,
                                a_not_b, a_and_b, b_not_a)
        H5S_hyper_span_t *a_spans;    IN: Span tree 'a' to clip with.
        H5S_hyper_span_t *b_spans;    IN: Span tree 'b' to clip with.
        unsigned int selector;        IN: The parameter deciding which output is needed
                                          (only considering the last three bits ABC:
                                           If A is set, then a_not_b is needed;
                                           If B is set, then a_and_b is needed;
                                           If C is set, then b_not_a is needed;
                                           )
        unsigned int cur_dim;         IN: The dimension in which this clip operation
                                          is performed.
        unsigned int dim_size;        IN: The dimension size.
        const hsize_t *span_a_b_bounds[4];  IN: The bound box of span a and span b.
        hsize_t *all_clips_bounds;    OUT: The array containing the bounds box
                                              of all three clipped span tree in the order
                                              a_not_b, a_and_b and b_not_a.
        H5S_hyper_span_t **a_not_b;  OUT: Span tree of 'a' hyperslab spans which
                                            doesn't overlap with 'b' hyperslab
                                            spans.
        H5S_hyper_span_t **a_and_b;  OUT: Span tree of 'a' hyperslab spans which
                                            overlaps with 'b' hyperslab spans.
        H5S_hyper_span_t **b_not_a;  OUT: Span tree of 'b' hyperslab spans which
                                            doesn't overlap with 'a' hyperslab
                                            spans.
 RETURNS
    non-negative on success, negative on failure
 DESCRIPTION
    Clip one span tree ('a') against another span tree ('b').  Creates span
    trees for the area defined by the 'a' span tree which does not overlap the
    'b' span tree, the area defined by the overlap of the 'a' hyperslab span
    tree and the 'b' span tree, and the area defined by the 'b' hyperslab span
    tree which does not overlap the 'a' span tree.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_clip_spans(H5S_hyper_span_info_t *a_spans, H5S_hyper_span_info_t *b_spans,
    unsigned int selector, unsigned cur_dim, unsigned dim_size,
    const hsize_t *span_a_b_bounds[4], hsize_t *all_clips_bounds,
    H5S_hyper_span_info_t **a_not_b, H5S_hyper_span_info_t **a_and_b,
    H5S_hyper_span_info_t **b_not_a)
{
    H5S_hyper_span_t *span_a;       /* Pointer to a node in span tree 'a' */
    H5S_hyper_span_t *span_b;       /* Pointer to a node in span tree 'b' */
    const hsize_t *span_a_low_bounds;   /* The low bound array for span a */
    const hsize_t *span_a_high_bounds;  /* The high bound array for span a */
    const hsize_t *span_b_low_bounds;   /* The low bound array for span b */
    const hsize_t *span_b_high_bounds;  /* The high bound array for span b */
    hsize_t *a_not_b_low_bounds;    /* The low bound array for a_not_b */
    hsize_t *a_not_b_high_bounds;   /* The high bound array for a_not_b */
    hsize_t *a_and_b_low_bounds;    /* The low bound array for a_and_b */
    hsize_t *a_and_b_high_bounds;   /* The high bound array for a_and_b */
    hsize_t *b_not_a_low_bounds;    /* The low bound array for b_not_a */
    hsize_t *b_not_a_high_bounds;   /* The high bound array for b_not_a */
    hbool_t need_a_not_b = FALSE;   /* Whether to generate a_not_b list */
    hbool_t need_a_and_b = FALSE;   /* Whether to generate a_and_b list */
    hbool_t need_b_not_a = FALSE;   /* Whether to generate b_not_a list */
    unsigned u;                     /* Local index variable */
    herr_t ret_value = SUCCEED;     /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Check args */
    HDassert(a_spans);
    HDassert(b_spans);
    HDassert(a_not_b);
    HDassert(a_and_b);
    HDassert(b_not_a);
    HDassert(cur_dim < dim_size);
    HDassert(all_clips_bounds);
    HDassert(span_a_b_bounds);

    /* Set the values of bound box pointers */
    span_a_low_bounds = span_a_b_bounds[0];
    span_a_high_bounds = span_a_b_bounds[1];
    span_b_low_bounds = span_a_b_bounds[2];
    span_b_high_bounds = span_a_b_bounds[3];
    a_not_b_low_bounds = all_clips_bounds;
    a_not_b_high_bounds = all_clips_bounds + dim_size;
    a_and_b_low_bounds = all_clips_bounds + 2 * dim_size;
    a_and_b_high_bounds = all_clips_bounds + 3 * dim_size;
    b_not_a_low_bounds = all_clips_bounds + 4 * dim_size;
    b_not_a_high_bounds = all_clips_bounds + 5 * dim_size;

    /* Set which list(s) to be generated, based on selector */
    if(H5S_HYPER_COMPUTE_B_NOT_A == (selector & H5S_HYPER_COMPUTE_B_NOT_A))
        need_b_not_a = TRUE;
    if(H5S_HYPER_COMPUTE_A_AND_B == (selector & H5S_HYPER_COMPUTE_A_AND_B))
        need_a_and_b = TRUE;
    if(H5S_HYPER_COMPUTE_A_NOT_B == (selector & H5S_HYPER_COMPUTE_A_NOT_B))
        need_a_not_b = TRUE;

    /* Check if both span trees are not defined */
    if(a_spans == NULL && b_spans == NULL) {
        *a_not_b = NULL;
        *a_and_b = NULL;
        *b_not_a = NULL;
    } /* end if */
    /* If span 'a' is not defined, but 'b' is, copy 'b' and set the other return span trees to empty */
    else if(a_spans == NULL) {
        *a_not_b = NULL;
        *a_and_b = NULL;
        if(need_b_not_a) {
            if(NULL == (*b_not_a = H5S_hyper_copy_span(b_spans)))
                HGOTO_ERROR(H5E_INTERNAL, H5E_CANTCOPY, FAIL, "can't copy hyperslab span tree")

            /* update b_not_a's bound box based on the values from span_b's bound box */
            for(u = cur_dim; u < dim_size; u++) {
                b_not_a_low_bounds[u] = MIN(b_not_a_low_bounds[u], span_b_low_bounds[u]);
                b_not_a_high_bounds[u] = MAX(b_not_a_high_bounds[u], span_b_high_bounds[u]);
            } /* end for */
        } /* end if */
        else
            *b_not_a = NULL;
    } /* end if */
    /* If span 'b' is not defined, but 'a' is, copy 'a' and set the other return span trees to empty */
    else if(b_spans == NULL) {
        *a_and_b = NULL;
        *b_not_a = NULL;
        if(need_a_not_b) {
            if(NULL == (*a_not_b = H5S_hyper_copy_span(a_spans)))
                HGOTO_ERROR(H5E_INTERNAL, H5E_CANTCOPY, FAIL, "can't copy hyperslab span tree")

            /* update b_not_a's bound box based on the values from span_a's bound box */
            for(u = cur_dim; u < dim_size; u++) {
                a_not_b_low_bounds[u] = MIN(a_not_b_low_bounds[u], span_a_low_bounds[u]);
                a_not_b_high_bounds[u] = MAX(a_not_b_high_bounds[u], span_a_high_bounds[u]);
            } /* end for */
        } /* end if */
        else
            *a_not_b = NULL;
    } /* end if */
    /* If span 'a' and 'b' are both defined, calculate the proper span trees */
    else {
        /* Check if both span trees completely overlap */
        if(H5S_hyper_cmp_spans(a_spans, b_spans) == TRUE) {
            *a_not_b = NULL;
            *b_not_a = NULL;
            if(need_a_and_b) {
                if(NULL == (*a_and_b = H5S_hyper_copy_span(a_spans)))
                    HGOTO_ERROR(H5E_INTERNAL, H5E_CANTCOPY, FAIL, "can't copy hyperslab span tree")

                /* update a_and_b's bound box based on the values from span_a's bound box */
                for(u = cur_dim; u < dim_size; u++) {
                    a_and_b_low_bounds[u] = MIN(a_and_b_low_bounds[u], span_a_low_bounds[u]);
                    a_and_b_high_bounds[u] = MAX(a_and_b_high_bounds[u], span_a_high_bounds[u]);
                } /* end for */
            } /* end if */
            else
                *a_and_b = NULL;
        } /* end if */
        else {
            H5S_hyper_span_t *last_a_not_b;   /* Pointer to previous node in span tree 'a_not_b' */
            H5S_hyper_span_t *last_a_and_b;   /* Pointer to previous node in span tree 'a_and_b' */
            H5S_hyper_span_t *last_b_not_a;   /* Pointer to previous node in span tree 'b_not_a' */
            hbool_t update_a_not_b_bounds, update_b_not_a_bounds;        /* Flags to indicate that bounds should be updated */
            hbool_t recover_a, recover_b;     /* Flags to indicate when to recover temporary spans */

            /* Get the pointers to the new and old span lists */
            span_a = a_spans->head;
            span_b = b_spans->head;

            /* Reset the pointers to the previous spans */
            last_a_not_b = last_a_and_b = last_b_not_a = NULL;

            /* No spans to recover yet */
            recover_a = recover_b = FALSE;

            /* No bounds to update yet */
            update_a_not_b_bounds = update_b_not_a_bounds = FALSE;

            /* Work through the list of spans in the new list */
            while(span_a != NULL && span_b != NULL) {
                H5S_hyper_span_info_t *down_a_not_b; /* Temporary pointer to a_not_b span tree of down spans for overlapping nodes */
                H5S_hyper_span_info_t *down_a_and_b; /* Temporary pointer to a_and_b span tree of down spans for overlapping nodes */
                H5S_hyper_span_info_t *down_b_not_a; /* Temporary pointer to b_and_a span tree of down spans for overlapping nodes */
                H5S_hyper_span_t *tmp_span; /* Temporary pointer to new span */

                /* Check if span 'a' is completely before span 'b' */
                /*    AAAAAAA                            */
                /* <-----------------------------------> */
                /*             BBBBBBBBBB                */
                if(span_a->high < span_b->low) {
                    /* Copy span 'a' and add to a_not_b list */

                    /* Merge/add span 'a' with/to a_not_b list */
                    if(need_a_not_b) {
                        if(H5S_hyper_append_span(&last_a_not_b, a_not_b, span_a->low, span_a->high, span_a->down, NULL) < 0)
                            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                        /* Indicate that the a_not_b bounds should be updated */
                        update_a_not_b_bounds = TRUE;
                    } /* end if */

                    /* Advance span 'a', leave span 'b' */
                    H5S_hyper_recover_span(&recover_a, &span_a, span_a->next);
                } /* end if */
                /* Check if span 'a' overlaps only the lower bound */
                /*  of span 'b' , up to the upper bound of span 'b' */
                /*    AAAAAAAAAAAA                       */
                /* <-----------------------------------> */
                /*             BBBBBBBBBB                */
                else if(span_a->low < span_b->low && (span_a->high >= span_b->low && span_a->high <= span_b->high)) {
                    /* Split span 'a' into two parts at the low bound of span 'b' */

                    /* Merge/add lower part of span 'a' with/to a_not_b list */
                    if(need_a_not_b) {
                        if(H5S_hyper_append_span(&last_a_not_b, a_not_b, span_a->low, span_b->low - 1, span_a->down, NULL) < 0)
                            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                        /* Indicate that the a_not_b bounds should be updated */
                        update_a_not_b_bounds = TRUE;
                    } /* end if */

                    /* Check for overlaps between upper part of span 'a' and lower part of span 'b' */

                    /* Make certain both spans either have a down span or both don't have one */
                    HDassert((span_a->down != NULL && span_b->down != NULL) || (span_a->down == NULL && span_b->down == NULL));

                    /* If there are no down spans, just add the overlapping area to the a_and_b list */
                    if(span_a->down == NULL) {
                        /* Merge/add overlapped part with/to a_and_b list */
                        if(need_a_and_b)
                            if(H5S_hyper_append_span(&last_a_and_b, a_and_b, span_b->low, span_a->high, NULL, NULL) < 0)
                                HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")
                    } /* end if */
                    /* If there are down spans, check for the overlap in them and add to each appropriate list */
                    else {
                        /* NULL out the temporary pointers to clipped areas in down spans */
                        down_a_not_b = NULL;
                        down_a_and_b = NULL;
                        down_b_not_a = NULL;

                        /* Check for overlaps in the 'down spans' of span 'a' & 'b' */
                        /** Note: since the bound box of remaining dimensions
                         *  has been updated in the following clip function (via
                         *  all_clips_bounds), there's no need updating the bound box
                         *  after each append call in the following codes */
                        if(H5S_hyper_clip_spans(span_a->down, span_b->down, selector, cur_dim + 1, dim_size, span_a_b_bounds, all_clips_bounds, &down_a_not_b, &down_a_and_b, &down_b_not_a) < 0)
                            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTCLIP, FAIL, "can't clip hyperslab information")

                        /* Check for additions to the a_not_b list */
                        if(down_a_not_b) {
                            HDassert(need_a_not_b == TRUE);

                            /* Merge/add overlapped part with/to a_not_b list */
                            if(H5S_hyper_append_span(&last_a_not_b, a_not_b, span_b->low, span_a->high, down_a_not_b, NULL) < 0)
                                HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                            /* Release the down span tree generated */
                            H5S_hyper_free_span_info(down_a_not_b);
                        } /* end if */

                        /* Check for additions to the a_and_b list */
                        if(down_a_and_b) {
                            HDassert(need_a_and_b == TRUE);

                            /* Merge/add overlapped part with/to a_and_b list */
                            if(H5S_hyper_append_span(&last_a_and_b, a_and_b, span_b->low, span_a->high, down_a_and_b, NULL) < 0)
                                HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                            /* Release the down span tree generated */
                            H5S_hyper_free_span_info(down_a_and_b);
                        } /* end if */

                        /* Check for additions to the b_not_a list */
                        if(down_b_not_a) {
                            HDassert(need_b_not_a == TRUE);

                            /* Merge/add overlapped part with/to b_not_a list */
                            if(H5S_hyper_append_span(&last_b_not_a, b_not_a, span_b->low, span_a->high, down_b_not_a, NULL) < 0)
                                HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                            /* Release the down span tree generated */
                            H5S_hyper_free_span_info(down_b_not_a);
                        } /* end if */
                    } /* end else */

                    /* Split off upper part of span 'b' at upper span of span 'a' */

                    /* Check if there is actually an upper part of span 'b' to split off */
                    if(span_a->high < span_b->high) {
                        /* Allocate new span node for upper part of span 'b' */
                        if(NULL == (tmp_span = H5S_hyper_new_span(span_a->high + 1, span_b->high, span_b->down, span_b->next)))
                            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                        /* Advance span 'a' */
                        H5S_hyper_recover_span(&recover_a, &span_a, span_a->next);

                        /* Make upper part of span 'b' into new span 'b' */
                        H5S_hyper_recover_span(&recover_b, &span_b, tmp_span);
                        recover_b = TRUE;
                    } /* end if */
                    /* No upper part of span 'b' to split */
                    else {
                        /* Advance both 'a' and 'b' */
                        H5S_hyper_recover_span(&recover_a, &span_a, span_a->next);
                        H5S_hyper_recover_span(&recover_b, &span_b, span_b->next);
                    } /* end else */
                } /* end if */
                /* Check if span 'a' overlaps the lower & upper bound */
                /*  of span 'b' */
                /*    AAAAAAAAAAAAAAAAAAAAA              */
                /* <-----------------------------------> */
                /*             BBBBBBBBBB                */
                else if(span_a->low < span_b->low && span_a->high > span_b->high) {
                    /* Split off lower part of span 'a' at lower span of span 'b' */

                    /* Merge/add lower part of span 'a' with/to a_not_b list */
                    if(need_a_not_b) {
                        if(H5S_hyper_append_span(&last_a_not_b, a_not_b, span_a->low, span_b->low - 1, span_a->down, NULL) < 0)
                            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                        /* Indicate that the a_not_b bounds should be updated */
                        update_a_not_b_bounds = TRUE;
                    } /* end if */

                    /* Check for overlaps between middle part of span 'a' and span 'b' */

                    /* Make certain both spans either have a down span or both don't have one */
                    HDassert((span_a->down != NULL && span_b->down != NULL) || (span_a->down == NULL && span_b->down == NULL));

                    /* If there are no down spans, just add the overlapping area to the a_and_b list */
                    if(span_a->down == NULL) {
                        /* Merge/add overlapped part with/to a_and_b list */
                        if(need_a_and_b)
                            if(H5S_hyper_append_span(&last_a_and_b, a_and_b, span_b->low, span_b->high, NULL, NULL) < 0)
                                HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")
                    } /* end if */
                    /* If there are down spans, check for the overlap in them and add to each appropriate list */
                    else {
                        /* NULL out the temporary pointers to clipped areas in down spans */
                        down_a_not_b = NULL;
                        down_a_and_b = NULL;
                        down_b_not_a = NULL;

                        /* Check for overlaps in the 'down spans' of span 'a' & 'b' */
                        if(H5S_hyper_clip_spans(span_a->down, span_b->down, selector, cur_dim + 1, dim_size, span_a_b_bounds, all_clips_bounds, &down_a_not_b, &down_a_and_b, &down_b_not_a) < 0)
                            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTCLIP, FAIL, "can't clip hyperslab information")

                        /* Check for additions to the a_not_b list */
                        if(down_a_not_b) {
                            HDassert(need_a_not_b == TRUE);

                            /* Merge/add overlapped part with/to a_not_b list */
                            if(H5S_hyper_append_span(&last_a_not_b, a_not_b,span_b->low, span_b->high, down_a_not_b, NULL) < 0)
                                HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                            /* Release the down span tree generated */
                            H5S_hyper_free_span_info(down_a_not_b);
                        } /* end if */

                        /* Check for additions to the a_and_b list */
                        if(down_a_and_b) {
                            HDassert(need_a_and_b == TRUE);

                            /* Merge/add overlapped part with/to a_and_b list */
                            if(H5S_hyper_append_span(&last_a_and_b, a_and_b, span_b->low, span_b->high, down_a_and_b, NULL) < 0)
                                HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                            /* Release the down span tree generated */
                            H5S_hyper_free_span_info(down_a_and_b);
                        } /* end if */

                        /* Check for additions to the b_not_a list */
                        if(down_b_not_a) {
                            HDassert(need_b_not_a == TRUE);

                            /* Merge/add overlapped part with/to b_not_a list */
                            if(H5S_hyper_append_span(&last_b_not_a, b_not_a, span_b->low, span_b->high, down_b_not_a, NULL) < 0)
                                HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                            /* Release the down span tree generated */
                            H5S_hyper_free_span_info(down_b_not_a);
                        } /* end if */
                    } /* end else */

                    /* Split off upper part of span 'a' at upper span of span 'b' */

                    /* Allocate new span node for upper part of span 'a' */
                    if(NULL == (tmp_span = H5S_hyper_new_span(span_b->high + 1, span_a->high, span_a->down, span_a->next)))
                        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                    /* Make upper part of span 'a' the new span 'a' */
                    H5S_hyper_recover_span(&recover_a, &span_a, tmp_span);
                    recover_a = TRUE;

                    /* Advance span 'b' */
                    H5S_hyper_recover_span(&recover_b, &span_b, span_b->next);
                } /* end if */
                /* Check if span 'a' is entirely within span 'b' */
                /*                AAAAA                  */
                /* <-----------------------------------> */
                /*             BBBBBBBBBB                */
                else if(span_a->low >= span_b->low && span_a->high <= span_b->high) {
                    /* Split off lower part of span 'b' at lower span of span 'a' */

                    /* Check if there is actually a lower part of span 'b' to split off */
                    if(span_a->low > span_b->low) {
                        /* Merge/add lower part of span 'b' with/to b_not_a list */
                        if(need_b_not_a) {
                            if(H5S_hyper_append_span(&last_b_not_a, b_not_a, span_b->low, span_a->low - 1, span_b->down, NULL) < 0)
                                HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                            /* Indicate that the b_not_a bounds should be updated */
                            update_b_not_a_bounds = TRUE;
                        } /* end if */
                    } /* end if */
                    else {
                        /* Keep going, nothing to split off */
                    } /* end else */

                    /* Check for overlaps between span 'a' and midle of span 'b' */

                    /* Make certain both spans either have a down span or both don't have one */
                    HDassert((span_a->down != NULL && span_b->down != NULL) || (span_a->down == NULL && span_b->down == NULL));

                    /* If there are no down spans, just add the overlapping area to the a_and_b list */
                    if(span_a->down == NULL) {
                        /* Merge/add overlapped part with/to a_and_b list */
                        if(need_a_and_b)
                            if(H5S_hyper_append_span(&last_a_and_b, a_and_b, span_a->low, span_a->high, NULL, NULL) < 0)
                                HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")
                    } /* end if */
                    /* If there are down spans, check for the overlap in them and add to each appropriate list */
                    else {
                        /* NULL out the temporary pointers to clipped areas in down spans */
                        down_a_not_b = NULL;
                        down_a_and_b = NULL;
                        down_b_not_a = NULL;

                        /* Check for overlaps in the 'down spans' of span 'a' & 'b' */
                        if(H5S_hyper_clip_spans(span_a->down, span_b->down, selector, cur_dim + 1, dim_size, span_a_b_bounds, all_clips_bounds, &down_a_not_b, &down_a_and_b, &down_b_not_a) < 0)
                            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTCLIP, FAIL, "can't clip hyperslab information")

                        /* Check for additions to the a_not_b list */
                        if(down_a_not_b) {
                            HDassert(need_a_not_b == TRUE);

                            /* Merge/add overlapped part with/to a_not_b list */
                            if(H5S_hyper_append_span(&last_a_not_b, a_not_b, span_a->low, span_a->high, down_a_not_b, NULL) < 0)
                                HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                            /* Release the down span tree generated */
                            H5S_hyper_free_span_info(down_a_not_b);
                        } /* end if */

                        /* Check for additions to the a_and_b list */
                        if(down_a_and_b) {
                            HDassert(need_a_and_b == TRUE);

                            /* Merge/add overlapped part with/to a_and_b list */
                            if(H5S_hyper_append_span(&last_a_and_b, a_and_b, span_a->low, span_a->high, down_a_and_b, NULL) < 0)
                                HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                            /* Release the down span tree generated */
                            H5S_hyper_free_span_info(down_a_and_b);
                        } /* end if */

                        /* Check for additions to the b_not_a list */
                        if(down_b_not_a) {
                            HDassert(need_b_not_a == TRUE);

                            /* Merge/add overlapped part with/to b_not_a list */
                            if(H5S_hyper_append_span(&last_b_not_a, b_not_a, span_a->low, span_a->high, down_b_not_a, NULL) < 0)
                                HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                            /* Release the down span tree generated */
                            H5S_hyper_free_span_info(down_b_not_a);
                        } /* end if */
                    } /* end else */

                    /* Check if there is actually an upper part of span 'b' to split off */
                    if(span_a->high < span_b->high) {
                        /* Split off upper part of span 'b' at upper span of span 'a' */

                        /* Allocate new span node for upper part of spans 'a' */
                        if(NULL == (tmp_span = H5S_hyper_new_span(span_a->high + 1, span_b->high, span_b->down, span_b->next)))
                            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                        /* And advance span 'a' */
                        H5S_hyper_recover_span(&recover_a, &span_a, span_a->next);

                        /* Make upper part of span 'b' the new span 'b' */
                        H5S_hyper_recover_span(&recover_b, &span_b, tmp_span);
                        recover_b = TRUE;
                    } /* end if */
                    else {
                        /* Advance both span 'a' & span 'b' */
                        H5S_hyper_recover_span(&recover_a, &span_a, span_a->next);
                        H5S_hyper_recover_span(&recover_b, &span_b, span_b->next);
                    } /* end else */
                } /* end if */
                /* Check if span 'a' overlaps only the upper bound */
                /*  of span 'b' */
                /*                AAAAAAAAAA             */
                /* <-----------------------------------> */
                /*             BBBBBBBBBB                */
                else if((span_a->low >= span_b->low && span_a->low <= span_b->high) && span_a->high > span_b->high) {
                    /* Check if there is actually a lower part of span 'b' to split off */
                    if(span_a->low > span_b->low) {
                        /* Split off lower part of span 'b' at lower span of span 'a' */

                        /* Merge/add lower part of span 'b' with/to b_not_a list */
                        if(need_b_not_a) {
                            if(H5S_hyper_append_span(&last_b_not_a, b_not_a, span_b->low, span_a->low - 1, span_b->down, NULL) < 0)
                                HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                            /* Indicate that the b_not_a bounds should be updated */
                            update_b_not_a_bounds = TRUE;
                        } /* end if */
                    } /* end if */
                    else {
                        /* Keep going, nothing to split off */
                    } /* end else */

                    /* Check for overlaps between lower part of span 'a' and upper part of span 'b' */

                    /* Make certain both spans either have a down span or both don't have one */
                    HDassert((span_a->down != NULL && span_b->down != NULL) || (span_a->down == NULL && span_b->down == NULL));

                    /* If there are no down spans, just add the overlapping area to the a_and_b list */
                    if(span_a->down == NULL) {
                        /* Merge/add overlapped part with/to a_and_b list */
                        if(need_a_and_b)
                            if(H5S_hyper_append_span(&last_a_and_b, a_and_b, span_a->low, span_b->high, NULL, NULL) < 0)
                                HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")
                    } /* end if */
                    /* If there are down spans, check for the overlap in them and add to each appropriate list */
                    else {
                        /* NULL out the temporary pointers to clipped areas in down spans */
                        down_a_not_b = NULL;
                        down_a_and_b = NULL;
                        down_b_not_a = NULL;

                        /* Check for overlaps in the 'down spans' of span 'a' & 'b' */
                        if(H5S_hyper_clip_spans(span_a->down, span_b->down, selector, cur_dim + 1, dim_size, span_a_b_bounds, all_clips_bounds, &down_a_not_b, &down_a_and_b, &down_b_not_a) < 0)
                            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTCLIP, FAIL, "can't clip hyperslab information")

                        /* Check for additions to the a_not_b list */
                        if(down_a_not_b) {
                            HDassert(need_a_not_b == TRUE);

                            /* Merge/add overlapped part with/to a_not_b list */
                            if(H5S_hyper_append_span(&last_a_not_b, a_not_b, span_a->low, span_b->high, down_a_not_b, NULL) < 0)
                                HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                            /* Release the down span tree generated */
                            H5S_hyper_free_span_info(down_a_not_b);
                        } /* end if */

                        /* Check for additions to the a_and_b list */
                        if(down_a_and_b) {
                            HDassert(need_a_and_b == TRUE);

                            /* Merge/add overlapped part with/to a_and_b list */
                            if(H5S_hyper_append_span(&last_a_and_b, a_and_b, span_a->low, span_b->high, down_a_and_b, NULL) < 0)
                                HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                            /* Release the down span tree generated */
                            H5S_hyper_free_span_info(down_a_and_b);
                        } /* end if */

                        /* Check for additions to the b_not_a list */
                        if(down_b_not_a) {
                            HDassert(need_b_not_a == TRUE);

                            /* Merge/add overlapped part with/to b_not_a list */
                            if(H5S_hyper_append_span(&last_b_not_a, b_not_a, span_a->low, span_b->high, down_b_not_a, NULL) < 0)
                                HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                            /* Release the down span tree generated */
                            H5S_hyper_free_span_info(down_b_not_a);
                        } /* end if */
                    } /* end else */

                    /* Split off upper part of span 'a' at upper span of span 'b' */

                    /* Allocate new span node for upper part of span 'a' */
                    if(NULL == (tmp_span = H5S_hyper_new_span(span_b->high + 1, span_a->high, span_a->down, span_a->next)))
                        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                    /* Make upper part of span 'a' into new span 'a' */
                    H5S_hyper_recover_span(&recover_a, &span_a, tmp_span);
                    recover_a = TRUE;

                    /* Advance span 'b' */
                    H5S_hyper_recover_span(&recover_b, &span_b, span_b->next);
                } /* end if */
                /* span 'a' must be entirely above span 'b' */
                /*                         AAAAA         */
                /* <-----------------------------------> */
                /*             BBBBBBBBBB                */
                else {
                    /* Copy span 'b' and add to b_not_a list */

                    /* Merge/add span 'b' with/to b_not_a list */
                    if(need_b_not_a) {
                        if(H5S_hyper_append_span(&last_b_not_a, b_not_a, span_b->low, span_b->high, span_b->down, NULL) < 0)
                            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                        /* Indicate that the b_not_a bounds should be updated */
                        update_b_not_a_bounds = TRUE;
                    } /* end if */

                    /* Advance span 'b', leave span 'a' */
                    H5S_hyper_recover_span(&recover_b, &span_b, span_b->next);
                } /* end else */
            } /* end while */

            /* Clean up 'a' spans which haven't been covered yet */
            if(span_a != NULL && span_b == NULL) {
                while(span_a != NULL) {
                    /* Copy span 'a' and add to a_not_b list */

                    /* Merge/add span 'a' with/to a_not_b list */
                    if(need_a_not_b) {
                        if(H5S_hyper_append_span(&last_a_not_b, a_not_b, span_a->low, span_a->high, span_a->down, NULL) < 0)
                            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                        /* Indicate that the a_not_b bounds should be updated */
                        update_a_not_b_bounds = TRUE;
                    } /* end if */

                    /* Advance to the next 'a' span */
                    H5S_hyper_recover_span(&recover_a, &span_a, span_a->next);
                } /* end while */
            } /* end if */
            /* Clean up 'b' spans which haven't been covered yet */
            else if(span_a == NULL && span_b != NULL) {
                while(span_b != NULL) {
                    /* Copy span 'b' and add to b_not_a list */

                    /* Merge/add span 'b' with/to b_not_a list */
                    if(need_b_not_a) {
                        if(H5S_hyper_append_span(&last_b_not_a, b_not_a, span_b->low, span_b->high, span_b->down, NULL) < 0)
                            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab span")

                        /* Indicate that the b_not_a bounds should be updated */
                        update_b_not_a_bounds = TRUE;
                    } /* end if */

                    /* Advance to the next 'b' span */
                    H5S_hyper_recover_span(&recover_b, &span_b, span_b->next);
                } /* end while */
            } /* end if */

            /* Check for needing to update each set of bounds */
            if(update_a_not_b_bounds) {
                /* Update bounding box of a_not_b for remaining dimensions as a span was appended */
                for(u = cur_dim + 1; u < dim_size; u++) {
                    a_not_b_low_bounds[u] = MIN(a_not_b_low_bounds[u], span_a_low_bounds[u]);
                    a_not_b_high_bounds[u] = MAX(a_not_b_high_bounds[u], span_a_high_bounds[u]);
                } /* end for */
            } /* end if */
            if(update_b_not_a_bounds) {
                /* Update bounding box of b_not_a for remaining dimensions as a span was appended */
                for(u = cur_dim + 1; u < dim_size; u++) {
                    b_not_a_low_bounds[u] = MIN(b_not_a_low_bounds[u], span_b_low_bounds[u]);
                    b_not_a_high_bounds[u] = MAX(b_not_a_high_bounds[u], span_b_high_bounds[u]);
                } /* end for */
            } /* end if */

            /**
             *  Up to this point, the clip operation has been applied to one
             *  span list in one dimension. So it's time to update the bound
             *  box of the clipped span trees.
             */
            if(need_a_not_b && *a_not_b != NULL) {
                a_not_b_low_bounds[cur_dim] = MIN(a_not_b_low_bounds[cur_dim], (*a_not_b)->head->low);
                a_not_b_high_bounds[cur_dim] = MAX(a_not_b_high_bounds[cur_dim], (*a_not_b)->tail->high);
            } /* end if */
            if(need_a_and_b && *a_and_b != NULL) {
                a_and_b_low_bounds[cur_dim] = MIN(a_and_b_low_bounds[cur_dim], (*a_and_b)->head->low);
                a_and_b_high_bounds[cur_dim] = MAX(a_and_b_high_bounds[cur_dim], (*a_and_b)->tail->high);
            } /* end if */
            if(need_b_not_a && *b_not_a != NULL) {
                b_not_a_low_bounds[cur_dim] = MIN(b_not_a_low_bounds[cur_dim], (*b_not_a)->head->low);
                b_not_a_high_bounds[cur_dim] = MAX(b_not_a_high_bounds[cur_dim], (*b_not_a)->tail->high);
            } /* end if */
        } /* end else */
    } /* end else */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_clip_spans() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_merge_spans_helper
 PURPOSE
    Merge two hyperslab span tree together
 USAGE
    H5S_hyper_span_info_t *H5S_hyper_merge_spans_helper(a_spans, b_spans)
        H5S_hyper_span_info_t *a_spans; IN: First hyperslab spans to merge
                                                together
        H5S_hyper_span_info_t *b_spans; IN: Second hyperslab spans to merge
                                                together
 RETURNS
    Pointer to span tree containing the merged spans on success, NULL on failure
 DESCRIPTION
    Merge two sets of hyperslab spans together and return the span tree from
    the merged set.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static H5S_hyper_span_info_t *
H5S_hyper_merge_spans_helper(H5S_hyper_span_info_t *a_spans, H5S_hyper_span_info_t *b_spans)
{
    H5S_hyper_span_info_t *merged_spans = NULL; /* Pointer to the merged span tree */
    H5S_hyper_span_info_t *ret_value;   /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Make certain both 'a' & 'b' spans have down span trees or neither does */
    HDassert((a_spans != NULL && b_spans != NULL) || (a_spans == NULL && b_spans == NULL));

    /* Check if the span trees for the 'a' span and the 'b' span are the same */
    if(H5S_hyper_cmp_spans(a_spans, b_spans) == TRUE) {
        if(a_spans == NULL)
            merged_spans = NULL;
        else {
            /* Copy one of the span trees to return */
            if(NULL == (merged_spans = H5S_hyper_copy_span(a_spans)))
                HGOTO_ERROR(H5E_INTERNAL, H5E_CANTCOPY, NULL, "can't copy hyperslab span tree")
        } /* end else */
    } /* end if */
    else {
        H5S_hyper_span_t *span_a;           /* Pointer to current span 'a' working on */
        H5S_hyper_span_t *span_b;           /* Pointer to current span 'b' working on */
        H5S_hyper_span_t *prev_span_merge;  /* Pointer to previous merged span */
        hbool_t recover_a, recover_b;       /* Flags to indicate when to recover temporary spans */

        /* Get the pointers to the 'a' and 'b' span lists */
        span_a = a_spans->head;
        span_b = b_spans->head;

        /* Set the pointer to the previous spans */
        prev_span_merge = NULL;

        /* No spans to recover yet */
        recover_a = recover_b = FALSE;

        /* Work through the list of spans in the new list */
        while(span_a!=NULL && span_b!=NULL) {
            H5S_hyper_span_info_t *tmp_spans;   /* Pointer to temporary new span tree */
            H5S_hyper_span_t *tmp_span;         /* Pointer to temporary new span */

            /* Check if the 'a' span is completely before 'b' span */
            /*    AAAAAAA                            */
            /* <-----------------------------------> */
            /*             BBBBBBBBBB                */
            if(span_a->high < span_b->low) {
                /* Merge/add span 'a' with/to the merged spans */
                if(H5S_hyper_append_span(&prev_span_merge,&merged_spans,span_a->low,span_a->high,span_a->down,NULL)==FAIL)
                    HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")

                /* Advance span 'a' */
                H5S_hyper_recover_span(&recover_a, &span_a, span_a->next);
            } /* end if */
            /* Check if span 'a' overlaps only the lower bound */
            /*  of span 'b', up to the upper bound of span 'b' */
            /*    AAAAAAAAAAAA                       */
            /* <-----------------------------------> */
            /*             BBBBBBBBBB                */
            else if(span_a->low < span_b->low && (span_a->high >= span_b->low && span_a->high <= span_b->high)) {
                /* Check if span 'a' and span 'b' down spans are equal */
                if(H5S_hyper_cmp_spans(span_a->down,span_b->down)==TRUE) {
                    /* Merge/add copy of span 'a' with/to merged spans */
                    if(H5S_hyper_append_span(&prev_span_merge,&merged_spans,span_a->low,span_a->high,span_a->down,NULL)==FAIL)
                        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")
                } /* end if */
                else {
                    /* Merge/add lower part of span 'a' with/to merged spans */
                    if(H5S_hyper_append_span(&prev_span_merge,&merged_spans,span_a->low,span_b->low-1,span_a->down,NULL)==FAIL)
                        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")

                    /* Get merged span tree for overlapped section */
                    tmp_spans=H5S_hyper_merge_spans_helper(span_a->down,span_b->down);

                    /* Merge/add overlapped section to merged spans */
                    if(H5S_hyper_append_span(&prev_span_merge,&merged_spans,span_b->low,span_a->high,tmp_spans,NULL)==FAIL)
                        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")

                    /* Release merged span tree for overlapped section */
                    H5S_hyper_free_span_info(tmp_spans);
                } /* end else */

                /* Check if there is an upper part of span 'b' */
                if(span_a->high < span_b->high) {
                    /* Copy upper part of span 'b' as new span 'b' */

                    /* Allocate new span node to append to list */
                    if((tmp_span = H5S_hyper_new_span(span_a->high+1,span_b->high,span_b->down,span_b->next))==NULL)
                        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")

                    /* Advance span 'a' */
                    H5S_hyper_recover_span(&recover_a, &span_a, span_a->next);

                    /* Set new span 'b' to tmp_span */
                    H5S_hyper_recover_span(&recover_b, &span_b, tmp_span);
                    recover_b = TRUE;
                } /* end if */
                else {
                    /* Advance both span 'a' & 'b' */
                    H5S_hyper_recover_span(&recover_a, &span_a, span_a->next);
                    H5S_hyper_recover_span(&recover_b, &span_b, span_b->next);
                } /* end else */
            } /* end if */
            /* Check if span 'a' overlaps the lower & upper bound */
            /*  of span 'b' */
            /*    AAAAAAAAAAAAAAAAAAAAA              */
            /* <-----------------------------------> */
            /*             BBBBBBBBBB                */
            else if(span_a->low < span_b->low && span_a->high > span_b->high) {
                /* Check if span 'a' and span 'b' down spans are equal */
                if(H5S_hyper_cmp_spans(span_a->down,span_b->down)==TRUE) {
                    /* Merge/add copy of lower & middle parts of span 'a' to merged spans */
                    if(H5S_hyper_append_span(&prev_span_merge,&merged_spans,span_a->low,span_b->high,span_a->down,NULL)==FAIL)
                        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")
                } /* end if */
                else {
                    /* Merge/add lower part of span 'a' to merged spans */
                    if(H5S_hyper_append_span(&prev_span_merge,&merged_spans,span_a->low,span_b->low-1,span_a->down,NULL)==FAIL)
                        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")

                    /* Get merged span tree for overlapped section */
                    tmp_spans=H5S_hyper_merge_spans_helper(span_a->down,span_b->down);

                    /* Merge/add overlapped section to merged spans */
                    if(H5S_hyper_append_span(&prev_span_merge,&merged_spans,span_b->low,span_b->high,tmp_spans,NULL)==FAIL)
                        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")

                    /* Release merged span tree for overlapped section */
                    H5S_hyper_free_span_info(tmp_spans);
                } /* end else */

                /* Copy upper part of span 'a' as new span 'a' (remember to free) */

                /* Allocate new span node to append to list */
                if((tmp_span = H5S_hyper_new_span(span_b->high+1,span_a->high,span_a->down,span_a->next))==NULL)
                    HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")

                /* Set new span 'a' to tmp_span */
                H5S_hyper_recover_span(&recover_a, &span_a, tmp_span);
                recover_a = TRUE;

                /* Advance span 'b' */
                H5S_hyper_recover_span(&recover_b, &span_b, span_b->next);
            } /* end if */
            /* Check if span 'a' is entirely within span 'b' */
            /*                AAAAA                  */
            /* <-----------------------------------> */
            /*             BBBBBBBBBB                */
            else if(span_a->low >= span_b->low && span_a->high <= span_b->high) {
                /* Check if span 'a' and span 'b' down spans are equal */
                if(H5S_hyper_cmp_spans(span_a->down,span_b->down)==TRUE) {
                    /* Merge/add copy of lower & middle parts of span 'b' to merged spans */
                    if(H5S_hyper_append_span(&prev_span_merge,&merged_spans,span_b->low,span_a->high,span_a->down,NULL)==FAIL)
                        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")
                } /* end if */
                else {
                    /* Check if there is a lower part of span 'b' */
                    if(span_a->low > span_b->low) {
                        /* Merge/add lower part of span 'b' to merged spans */
                        if(H5S_hyper_append_span(&prev_span_merge,&merged_spans,span_b->low,span_a->low-1,span_b->down,NULL)==FAIL)
                            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")
                    } /* end if */
                    else {
                        /* No lower part of span 'b' , keep going... */
                    } /* end else */

                    /* Get merged span tree for overlapped section */
                    tmp_spans=H5S_hyper_merge_spans_helper(span_a->down,span_b->down);

                    /* Merge/add overlapped section to merged spans */
                    if(H5S_hyper_append_span(&prev_span_merge,&merged_spans,span_a->low,span_a->high,tmp_spans,NULL)==FAIL)
                        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")

                    /* Release merged span tree for overlapped section */
                    H5S_hyper_free_span_info(tmp_spans);
                } /* end else */

                /* Check if there is an upper part of span 'b' */
                if(span_a->high < span_b->high) {
                    /* Copy upper part of span 'b' as new span 'b' (remember to free) */

                    /* Allocate new span node to append to list */
                    if((tmp_span = H5S_hyper_new_span(span_a->high+1,span_b->high,span_b->down,span_b->next))==NULL)
                        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")

                    /* Advance span 'a' */
                    H5S_hyper_recover_span(&recover_a, &span_a, span_a->next);

                    /* Set new span 'b' to tmp_span */
                    H5S_hyper_recover_span(&recover_b, &span_b, tmp_span);
                    recover_b = TRUE;
                } /* end if */
                else {
                    /* Advance both spans */
                    H5S_hyper_recover_span(&recover_a, &span_a, span_a->next);
                    H5S_hyper_recover_span(&recover_b, &span_b, span_b->next);
                } /* end else */
            } /* end if */
            /* Check if span 'a' overlaps only the upper bound */
            /*  of span 'b' */
            /*                AAAAAAAAAA             */
            /* <-----------------------------------> */
            /*             BBBBBBBBBB                */
            else if((span_a->low >= span_b->low && span_a->low <= span_b->high) && span_a->high > span_b->high) {
                /* Check if span 'a' and span 'b' down spans are equal */
                if(H5S_hyper_cmp_spans(span_a->down,span_b->down)==TRUE) {
                    /* Merge/add copy of span 'b' to merged spans if so */
                    if(H5S_hyper_append_span(&prev_span_merge,&merged_spans,span_b->low,span_b->high,span_b->down,NULL)==FAIL)
                        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")
                } /* end if */
                else {
                    /* Check if there is a lower part of span 'b' */
                    if(span_a->low > span_b->low) {
                        /* Merge/add lower part of span 'b' to merged spans */
                        if(H5S_hyper_append_span(&prev_span_merge,&merged_spans,span_b->low,span_a->low-1,span_b->down,NULL)==FAIL)
                            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")
                    } /* end if */
                    else {
                        /* No lower part of span 'b' , keep going... */
                    } /* end else */

                    /* Get merged span tree for overlapped section */
                    tmp_spans=H5S_hyper_merge_spans_helper(span_a->down,span_b->down);

                    /* Merge/add overlapped section to merged spans */
                    if(H5S_hyper_append_span(&prev_span_merge,&merged_spans,span_a->low,span_b->high,tmp_spans,NULL)==FAIL)
                        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")

                    /* Release merged span tree for overlapped section */
                    H5S_hyper_free_span_info(tmp_spans);
                } /* end else */

                /* Copy upper part of span 'a' as new span 'a' */

                /* Allocate new span node to append to list */
                if((tmp_span = H5S_hyper_new_span(span_b->high+1,span_a->high,span_a->down,span_a->next))==NULL)
                    HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")

                /* Set new span 'a' to tmp_span */
                H5S_hyper_recover_span(&recover_a, &span_a, tmp_span);
                recover_a = TRUE;

                /* Advance span 'b' */
                H5S_hyper_recover_span(&recover_b, &span_b, span_b->next);
            } /* end if */
            /* Span 'a' must be entirely above span 'b' */
            /*                         AAAAA         */
            /* <-----------------------------------> */
            /*             BBBBBBBBBB                */
            else {
                /* Merge/add span 'b' with the merged spans */
                if(H5S_hyper_append_span(&prev_span_merge,&merged_spans,span_b->low,span_b->high,span_b->down,NULL)==FAIL)
                    HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")

                /* Advance span 'b' */
                H5S_hyper_recover_span(&recover_b, &span_b, span_b->next);
            } /* end else */
        } /* end while */

        /* Clean up 'a' spans which haven't been added to the list of merged spans */
        if(span_a!=NULL && span_b==NULL) {
            while(span_a!=NULL) {
                /* Merge/add all 'a' spans into the merged spans */
                if(H5S_hyper_append_span(&prev_span_merge,&merged_spans,span_a->low,span_a->high,span_a->down,NULL)==FAIL)
                    HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")

                /* Advance to next 'a' span, until all processed */
                H5S_hyper_recover_span(&recover_a, &span_a, span_a->next);
            } /* end while */
        } /* end if */

        /* Clean up 'b' spans which haven't been added to the list of merged spans */
        if(span_a==NULL && span_b!=NULL) {
            while(span_b!=NULL) {
                /* Merge/add all 'b' spans into the merged spans */
                if(H5S_hyper_append_span(&prev_span_merge,&merged_spans,span_b->low,span_b->high,span_b->down,NULL)==FAIL)
                    HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "can't allocate hyperslab span")

                /* Advance to next 'b' span, until all processed */
                H5S_hyper_recover_span(&recover_b, &span_b, span_b->next);
            } /* end while */
        } /* end if */
    } /* end else */

    /* Set return value */
    ret_value = merged_spans;

done:
    if(ret_value == NULL)
        if(merged_spans && H5S_hyper_free_span_info(merged_spans) < 0)
            HDONE_ERROR(H5E_INTERNAL, H5E_CANTFREE, NULL, "failed to release merged hyperslab spans")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_merge_spans_helper() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_merge_spans
 PURPOSE
    Merge new hyperslab spans to existing hyperslab selection
 USAGE
    herr_t H5S_hyper_merge_spans(space, new_spans, can_own)
        H5S_t *space;             IN: Dataspace to add new spans to hyperslab
                                        selection.
        H5S_hyper_span_t *new_spans;    IN: Span tree of new spans to add to
                                            hyperslab selection
 RETURNS
    non-negative on success, negative on failure
 DESCRIPTION
    Add a set of hyperslab spans to an existing hyperslab selection.  The
    new spans are required to be non-overlapping with the existing spans in
    the dataspace's current hyperslab selection.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_merge_spans(H5S_t *space, H5S_hyper_span_info_t *new_spans)
{
    herr_t ret_value = SUCCEED;     /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Check args */
    HDassert(space);
    HDassert(new_spans);

    /* If this is the first span tree in the hyperslab selection, just use it */
    if(space->select.sel_info.hslab->span_lst == NULL)
        space->select.sel_info.hslab->span_lst = new_spans;
    else {
        H5S_hyper_span_info_t *merged_spans;

        /* Get the merged spans */
        if(NULL == (merged_spans = H5S_hyper_merge_spans_helper(space->select.sel_info.hslab->span_lst, new_spans)))
            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTMERGE, FAIL, "can't merge hyperslab spans")

        /* Free the previous spans */
        if(H5S_hyper_free_span_info(space->select.sel_info.hslab->span_lst) < 0)
            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTFREE, FAIL, "failed to release hyperslab spans")

        /* Point to the new merged spans */
        space->select.sel_info.hslab->span_lst = merged_spans;
    } /* end else */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_merge_spans() */

/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_spans_nelem
 PURPOSE
    Count the number of elements in a span tree
 USAGE
    hsize_t H5S_hyper_spans_nelem(spans)
        const H5S_hyper_span_info_t *spans; IN: Hyperslan span tree to count elements of
 RETURNS
    Number of elements in span tree on success; negative on failure
 DESCRIPTION
    Counts the number of elements described by the spans in a span tree.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static hsize_t
H5S_hyper_spans_nelem(const H5S_hyper_span_info_t *spans)
{
    hsize_t ret_value = 0;          /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Count the number of elements in the span tree */
    if(spans != NULL) {
        const H5S_hyper_span_t *span;     /* Hyperslab span */

        span = spans->head;
        while(span != NULL) {
            /* If there are down spans, multiply the size of this span by the total down span elements */
            if(span->down != NULL)
                ret_value += span->nelem * H5S_hyper_spans_nelem(span->down);
            /* If there are no down spans, just count the elements in this span */
            else
                ret_value += span->nelem;

            /* Advance to next span */
            span = span->next;
        } /* end while */
    } /* end else */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_spans_nelem() */

/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_add_disjoint_spans
 PURPOSE
    Add new hyperslab spans to existing hyperslab selection in the case the
    new hyperslab spans don't overlap or border with the existing hyperslab
    selection
 USAGE
    herr_t H5S_hyper_add_disjoint_spans(space, new_spans)
        H5S_t *space;             IN: Dataspace to add new spans to hyperslab
                                        selection.
        H5S_hyper_span_t *new_spans;    IN: Span tree of new spans to add to
                                            hyperslab selection
 RETURNS
    non-negative on success, negative on failure
 DESCRIPTION
    Add a set of hyperslab spans to an existing hyperslab selection.  The
    new spans are required not to overlap or borderwith the existing spans
    in the dataspace's current hyperslab selection in terms of bound box.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_add_disjoint_spans(H5S_t *space, H5S_hyper_span_info_t *new_spans)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Check args */
    HDassert(space);
    HDassert(new_spans);

    /* update the number of elements in the selection */
    space->select.num_elem += H5S_hyper_spans_nelem(new_spans);

    /* If this is the first span tree in the hyperslab selection, just use it */
    if(space->select.sel_info.hslab->span_lst == NULL)
        space->select.sel_info.hslab->span_lst = new_spans;
    else {
       H5S_hyper_span_info_t *old_spans = space->select.sel_info.hslab->span_lst;
       H5S_hyper_span_info_t *high_order_spans = NULL;
       H5S_hyper_span_info_t *low_order_spans = NULL;

       /* Combine the new_spans with the old_spans only in the highest-order of dimension */
       /* Example:
        * old_spans:           ------------------> X
        *            | (1,2)--->(1,3)--->(6,4)--->||
        *            |   |        ^
        *            |   |        |
        *      Y     |   V        |
        *            | (5,2)------.
        *            |   |
        *            |   |
        *            |   V
        *            |   =
        *            V
        * new_spans:           --------------------> X
        *            | (8,2)--->(12,3)--->(17,4)--->||
        *            |   |
        *      Y     |   |
        *            |   V
        *            |   =
        *            V
        *
        * After merging, the old_spans becomes
        *
        * old_spans:           ------------------> X
        *            | (1,2)--->(1,3)--->(6,4)--->||
        *            |   |        ^
        *            |   |        |
        *            |   V        |
        *            | (5,2)------.
        *            |   |
        *      Y     |   |
        *            | (8,2)--->(12,3)--->(17,4)--->||
        *            |   |
        *            |   |
        *            |   V
        *            |   =
        *            V
        */

       /* Make sure the tail pointers are updated correctly */
       HDassert(old_spans->tail);
       HDassert(NULL == old_spans->tail->next);
       HDassert(new_spans->tail);
       HDassert(NULL == new_spans->tail->next);

       /* Check old_spans and new_spans to see which one is of higher order,
        * and append the higher order spans to the lower order ones.
        */
       if(new_spans->head->low > old_spans->tail->high) {
           high_order_spans = new_spans;
           low_order_spans = old_spans;
       } /* end if */
       else{
           high_order_spans = old_spans;
           low_order_spans = new_spans;
       } /* end else */

       /* The high_order_spans should be appended to low_order_spans */
       low_order_spans->tail->next = high_order_spans->head;

       /* "pstride" is only updated in IO operations, so it needs no update here */
       /*high_order_spans->head->pstride = high_order_spans->head->low - low_order_spans->tail->low;*/
       low_order_spans->tail = high_order_spans->tail;

       /* Fix the selection's 1st span info node */
       space->select.sel_info.hslab->span_lst = low_order_spans;

       /* free the memory space for "new_spans" node */
       high_order_spans = H5FL_FREE(H5S_hyper_span_info_t, high_order_spans);
    } /* end else */

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5S_hyper_add_disjoint_spans */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_make_spans
 PURPOSE
    Create a span tree
 USAGE
    H5S_hyper_span_t *H5S_hyper_make_spans(rank, start, stride, count, block)
        unsigned rank;               IN: # of dimensions of the space
        const hsize_t *start;    IN: Starting location of the hyperslabs
        const hsize_t *stride;    IN: Stride from the beginning of one block to
                                        the next
        const hsize_t *count;     IN: Number of blocks
        const hsize_t *block;     IN: Size of hyperslab block
 RETURNS
    Pointer to new span tree on success, NULL on failure
 DESCRIPTION
    Generates a new span tree for the hyperslab parameters specified.
    Each span tree has a list of the elements spanned in each dimension, with
    each span node containing a pointer to the list of spans in the next
    dimension down.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static H5S_hyper_span_info_t *
H5S_hyper_make_spans(unsigned rank, const hsize_t *start, const hsize_t *stride,
    const hsize_t *count, const hsize_t *block)
{
    H5S_hyper_span_info_t *down = NULL;     /* Pointer to spans in next dimension down */
    H5S_hyper_span_t      *last_span;       /* Current position in hyperslab span list */
    H5S_hyper_span_t      *head = NULL;     /* Head of new hyperslab span list */
    hsize_t                stride_iter;     /* Iterator over the stride values */
    int                    i;               /* Counters */
    unsigned               u;               /* Counters */
    H5S_hyper_span_info_t *ret_value;       /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Check args */
    HDassert(rank > 0);
    HDassert(start);
    HDassert(stride);
    HDassert(count);
    HDassert(block);

    /* Start creating spans in fastest changing dimension */
    for(i = (int)(rank - 1); i >= 0; i--) {

        /* Sanity check */
        if(0 == count[i])
            HGOTO_ERROR(H5E_DATASPACE, H5E_BADVALUE, NULL, "count == 0 is invalid")

        /* Start a new list in this dimension */
        head = NULL;
        last_span = NULL;

        /* Generate all the span segments for this dimension */
        for(u = 0, stride_iter = 0; u < count[i]; u++, stride_iter += stride[i]) {
            H5S_hyper_span_t      *span;            /* New hyperslab span */

            /* Allocate a span node */
            if(NULL == (span = H5FL_MALLOC(H5S_hyper_span_t)))
                HGOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, NULL, "can't allocate hyperslab span")

            /* Set the span's basic information */
            span->low = start[i] + stride_iter;
            span->high = span->low + (block[i] - 1);
            span->nelem = block[i];
            span->pstride = stride[i];
            span->next = NULL;

            /* Append to the list of spans in this dimension */
            if(head == NULL)
                head = span;
            else
                last_span->next = span;

            /* Move current pointer */
            last_span = span;

            /* Set the information for the next dimension down's spans, if appropriate */
            if(down != NULL) {
                span->down = down;
                down->count++;  /* Increment reference count for shared span */
            } /* end if */
            else
                span->down = NULL;
        } /* end for */

        /* Allocate a span info node */
        if(NULL == (down = H5FL_CALLOC(H5S_hyper_span_info_t)))
            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTALLOC, NULL, "can't allocate hyperslab span")

        /* Keep the pointer to the next dimension down's completed list */
        down->head = head;

        /* Keep the tail pointer to the next dimension down's completed list */
        down->tail = last_span;
    } /* end for */

    /* Indicate that there is a pointer to this tree */
    down->count = 1;

    /* Success!  Return the head of the list in the slowest changing dimension */
    ret_value = down;

done:
    /* cleanup if error (ret_value will be NULL) */
    if(!ret_value) {
        if(head || down) {
            if(head && down)
                if(down->head != head)
                    down = NULL;

            do {
                if(down) {
                    head = down->head;
                    down = H5FL_FREE(H5S_hyper_span_info_t, down);
                } /* end if */
                down = head->down;

                while(head) {
                    last_span = head->next;
                    head = H5FL_FREE(H5S_hyper_span_t, head);
                    head = last_span;
                } /* end while */
            } while(down);
        } /* end if */
    } /* end if */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_make_spans() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_update_diminfo
 PURPOSE
    Attempt to update optimized hyperslab information quickly.  (It can be
    recovered with regular selection).  If this algorithm cannot determine
    the optimized dimension info quickly, this function will simply mark it
    as invalid and unknown if it can be built (H5S_DIMINFO_VALID_NO), so
    H5S_hyper_rebuild can be run later to determine for sure.
 USAGE
    herr_t H5S_hyper_update_diminfo(space, op, new_hyper_diminfo)
        H5S_t *space;       IN: Dataspace to check
        H5S_seloper_t op;   IN: The operation being performed on the
                                selection
        const H5S_hyper_dim_t new_hyper_diminfo; IN: The new selection that
                                                     is being combined with
                                                     the current
 RETURNS
    >=0 on success, <0 on failure
 DESCRIPTION
    Examine the span tree for a hyperslab selection and rebuild
    the start/stride/count/block information for the selection, if possible.

 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
    NF, 2014/6/6
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_update_diminfo(H5S_t *space, H5S_seloper_t op,
    const H5S_hyper_dim_t *new_hyper_diminfo)
{
    H5S_hyper_dim_t tmp_diminfo[H5S_MAX_RANK];  /* Temporary dimension info */
    hbool_t found_nonidentical_dim = FALSE;
    unsigned curr_dim;
    herr_t ret_value = SUCCEED;      /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Check args */
    HDassert(space);
    HDassert(new_hyper_diminfo);

    /* Check for conditions that prevent us from using the fast algorithm here
     * (and instead require H5S_hyper_rebuild) */
    if(!((op == H5S_SELECT_OR) || (op == H5S_SELECT_XOR))
            || space->select.sel_info.hslab->diminfo_valid != H5S_DIMINFO_VALID_YES
            || !space->select.sel_info.hslab->span_lst->head) {
        space->select.sel_info.hslab->diminfo_valid = H5S_DIMINFO_VALID_NO;
        HGOTO_DONE(SUCCEED)
    } /* end if */

    /* Copy current opt_diminfo values */
    for(curr_dim = 0; curr_dim < space->extent.rank; curr_dim++) {
        tmp_diminfo[curr_dim].start  = space->select.sel_info.hslab->opt_diminfo[curr_dim].start;
        tmp_diminfo[curr_dim].stride = space->select.sel_info.hslab->opt_diminfo[curr_dim].stride;
        tmp_diminfo[curr_dim].count  = space->select.sel_info.hslab->opt_diminfo[curr_dim].count;
        tmp_diminfo[curr_dim].block  = space->select.sel_info.hslab->opt_diminfo[curr_dim].block;
    } /* end for */

    /* Loop over dimensions */
    for(curr_dim = 0; curr_dim < space->extent.rank; curr_dim++) {
        /* Check for this being identical */
        if((tmp_diminfo[curr_dim].start != new_hyper_diminfo[curr_dim].start)
                || (tmp_diminfo[curr_dim].stride != new_hyper_diminfo[curr_dim].stride)
                || (tmp_diminfo[curr_dim].count != new_hyper_diminfo[curr_dim].count)
                || (tmp_diminfo[curr_dim].block != new_hyper_diminfo[curr_dim].block)) {
            hsize_t high_start, high_count, high_block;         /* The start, count & block values for the higher block */

            /* Dimension is not identical */
            /* Check if we already found a nonidentical dim - only one is
             * allowed */
            if(found_nonidentical_dim) {
                space->select.sel_info.hslab->diminfo_valid = H5S_DIMINFO_VALID_NO;
                break;
            } /* end if */

            /* Check that strides are the same, or count is 1 for one of the
             * slabs */
            if((tmp_diminfo[curr_dim].stride != new_hyper_diminfo[curr_dim].stride)
                    && (tmp_diminfo[curr_dim].count > 1)
                    && (new_hyper_diminfo[curr_dim].count > 1)) {
                space->select.sel_info.hslab->diminfo_valid = H5S_DIMINFO_VALID_NO;
                break;
            } /* end if */

            /* Patch tmp_diminfo.stride if its count is 1 */
            if((tmp_diminfo[curr_dim].count == 1)
                    && (new_hyper_diminfo[curr_dim].count > 1))
                tmp_diminfo[curr_dim].stride = new_hyper_diminfo[curr_dim].stride;

            /* Determine lowest start, and set tmp_diminfo.start, count and
             *  block to use the lowest, and high_start, high_count and
             *  high_block to use the highest
             */
            if(tmp_diminfo[curr_dim].start < new_hyper_diminfo[curr_dim].start) {
                high_start = new_hyper_diminfo[curr_dim].start;
                high_count = new_hyper_diminfo[curr_dim].count;
                high_block = new_hyper_diminfo[curr_dim].block;
            } /* end if */
            else {
                high_start = tmp_diminfo[curr_dim].start;
                tmp_diminfo[curr_dim].start = new_hyper_diminfo[curr_dim].start;
                high_count = tmp_diminfo[curr_dim].count;
                tmp_diminfo[curr_dim].count = new_hyper_diminfo[curr_dim].count;
                high_block = tmp_diminfo[curr_dim].block;
                tmp_diminfo[curr_dim].block = new_hyper_diminfo[curr_dim].block;
            } /* end else */

            /* If count is 1 for both slabs, take different actions */
            if((tmp_diminfo[curr_dim].count == 1) && (high_count == 1)) {
                /* Check for overlap */
                if((tmp_diminfo[curr_dim].start + tmp_diminfo[curr_dim].block)
                        > high_start) {
                    /* Check operation type */
                    if(op == H5S_SELECT_OR)
                        /* Merge blocks */
                        tmp_diminfo[curr_dim].block = ((high_start + high_block)
                                >= (tmp_diminfo[curr_dim].start + tmp_diminfo[curr_dim].block))
                                ? (high_start + high_block - tmp_diminfo[curr_dim].start)
                                : tmp_diminfo[curr_dim].block;
                    else {
                        /* Block values must be the same */
                        if(tmp_diminfo[curr_dim].block != high_block) {
                            space->select.sel_info.hslab->diminfo_valid = H5S_DIMINFO_VALID_NO;
                            break;
                        } /* end if */

                        /* XOR - overlap creates 2 blocks */
                        tmp_diminfo[curr_dim].stride = high_block;
                        tmp_diminfo[curr_dim].count = 2;
                        tmp_diminfo[curr_dim].block = high_start - tmp_diminfo[curr_dim].start;
                    } /* end else */
                } /* end if */
                else if((tmp_diminfo[curr_dim].start + tmp_diminfo[curr_dim].block)
                        == high_start)
                    /* Blocks border, merge them */
                    tmp_diminfo[curr_dim].block += high_block;
                else {
                    /* Distinct blocks */
                    /* Block values must be the same */
                    if(tmp_diminfo[curr_dim].block != high_block) {
                        space->select.sel_info.hslab->diminfo_valid = H5S_DIMINFO_VALID_NO;
                        break;
                    } /* end if */

                    /* Create strided selection */
                    tmp_diminfo[curr_dim].stride = high_start - tmp_diminfo[curr_dim].start;
                    tmp_diminfo[curr_dim].count = 2;
                } /* end else */
            } /* end if */
            else {
                /* Check if block values are the same */
                if(tmp_diminfo[curr_dim].block != new_hyper_diminfo[curr_dim].block) {
                    space->select.sel_info.hslab->diminfo_valid = H5S_DIMINFO_VALID_NO;
                    break;
                } /* end if */

                /* Check phase of strides */
                if((tmp_diminfo[curr_dim].start % tmp_diminfo[curr_dim].stride)
                        != (new_hyper_diminfo[curr_dim].start % tmp_diminfo[curr_dim].stride)) {
                    space->select.sel_info.hslab->diminfo_valid = H5S_DIMINFO_VALID_NO;
                    break;
                } /* end if */

                /* Check operation type */
                if(op == H5S_SELECT_OR) {
                    /* Make sure the slabs border or overlap */
                    if(high_start > (tmp_diminfo[curr_dim].start
                            + (tmp_diminfo[curr_dim].count
                            * tmp_diminfo[curr_dim].stride))) {
                        space->select.sel_info.hslab->diminfo_valid = H5S_DIMINFO_VALID_NO;
                        break;
                    } /* end if */
                } /* end if */
                else
                    /* XOR: Make sure the slabs border */
                    if(high_start != (tmp_diminfo[curr_dim].start
                            + (tmp_diminfo[curr_dim].count
                            * tmp_diminfo[curr_dim].stride))) {
                        space->select.sel_info.hslab->diminfo_valid = H5S_DIMINFO_VALID_NO;
                        break;
                    } /* end if */

                /* Set count for combined selection */
                tmp_diminfo[curr_dim].count = ((high_start
                        - tmp_diminfo[curr_dim].start)
                        / tmp_diminfo[curr_dim].stride) + high_count;
            } /* end else */

            /* Indicate that we found a nonidentical dim */
            found_nonidentical_dim = TRUE;
        } /* end if */
    } /* end for */

    /* Check if we succeeded, if so, set the new diminfo values */
    if(space->select.sel_info.hslab->diminfo_valid == H5S_DIMINFO_VALID_YES)
        for(curr_dim = 0; curr_dim < space->extent.rank; curr_dim++) {
            space->select.sel_info.hslab->app_diminfo[curr_dim].start  = space->select.sel_info.hslab->opt_diminfo[curr_dim].start = tmp_diminfo[curr_dim].start;
            space->select.sel_info.hslab->app_diminfo[curr_dim].stride = space->select.sel_info.hslab->opt_diminfo[curr_dim].stride = tmp_diminfo[curr_dim].stride;
            space->select.sel_info.hslab->app_diminfo[curr_dim].count  = space->select.sel_info.hslab->opt_diminfo[curr_dim].count = tmp_diminfo[curr_dim].count;
            space->select.sel_info.hslab->app_diminfo[curr_dim].block  = space->select.sel_info.hslab->opt_diminfo[curr_dim].block = tmp_diminfo[curr_dim].block;
        } /* end for */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_update_diminfo() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_rebuild_helper
 PURPOSE
    Helper routine to rebuild optimized hyperslab information if possible.
    (It can be recovered with regular selection)
 USAGE
    herr_t H5S_hyper_rebuild_helper(space)
        const H5S_hyper_span_t *span;   IN: Portion of span tree to check
        H5S_hyper_dim_t span_slab[];    OUT: Rebuilt section of hyperslab description
        unsigned rank;                  IN: Current dimension to work on
 RETURNS
    >=0 on success, <0 on failure
 DESCRIPTION
    Examine the span tree for a hyperslab selection and rebuild
    the start/stride/count/block information for the selection, if possible.

 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
    To be able to recover the optimized information, the span tree must conform
    to span tree able to be generated from a single H5S_SELECT_SET operation.

 EXAMPLES
 REVISION LOG
    KY, 2005/9/22
--------------------------------------------------------------------------*/
static hbool_t
H5S_hyper_rebuild_helper(const H5S_hyper_span_t *span, H5S_hyper_dim_t span_slab_info[],
    unsigned rank)
{
    hbool_t ret_value = TRUE;   /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    if(span) {
        H5S_hyper_dim_t canon_down_span_slab_info[H5S_MAX_RANK];
        hsize_t curr_stride;
        hsize_t curr_block;
        hsize_t curr_start;
        hsize_t curr_low;
        size_t outcount;        /* Number of spans encountered in this dimension */

        /* Initialization */
        curr_stride     = 1;
        curr_low        = 0;
        outcount        = 0;

        /* Get "canonical" down span information */
        if(span->down) {
            HDassert(span->down->head);

            /* Go to the next down span and check whether the selection can be rebuilt.*/
            if(!H5S_hyper_rebuild_helper(span->down->head, span_slab_info, rank - 1))
                HGOTO_DONE(FALSE)

            HDmemcpy(canon_down_span_slab_info, span_slab_info, sizeof(H5S_hyper_dim_t) * rank);
        } /* end if */

        /* Assign the initial starting point & block size */
        curr_start = span->low;
        curr_block = (span->high - span->low) + 1;

        /* Loop the span */
        while(span) {
            hsize_t next_stride;
            hsize_t next_block;

            if(outcount > 0) {
                if(span->down) {
                    H5S_hyper_dim_t *curr_down_span_slab_info;
                    unsigned u;                 /* Local index variable */

                    HDassert(span->down->head);

                    /* Go to the next down span and check whether the selection can be rebuilt.*/
                    if(!H5S_hyper_rebuild_helper(span->down->head, span_slab_info, rank - 1))
                        HGOTO_DONE(FALSE)

                    /* Compare the slab information of the adjacent spans in the down span tree.
                       We have to compare all the sub-tree slab information with the canon_down_span_slab_info.*/

                    for(u = 0; u < rank - 1; u++) {
                       curr_down_span_slab_info = &span_slab_info[u];

                       if(curr_down_span_slab_info->count > 0 && canon_down_span_slab_info[u].count > 0) {
                          if(curr_down_span_slab_info->start != canon_down_span_slab_info[u].start
                              || curr_down_span_slab_info->stride != canon_down_span_slab_info[u].stride
                              || curr_down_span_slab_info->block != canon_down_span_slab_info[u].block
                              || curr_down_span_slab_info->count != canon_down_span_slab_info[u].count)
                          HGOTO_DONE(FALSE)
                       } /* end if */
                       else if(!((curr_down_span_slab_info->count == 0) && (canon_down_span_slab_info[u].count == 0)))
                          HGOTO_DONE(FALSE)
                    } /* end for */
                } /* end if */
            } /* end if */

            /* Obtain values for stride and block */
            next_stride  = span->low  - curr_low;
            next_block   = (span->high - span->low) + 1;

            /* Compare stride and block in this span, to compare stride,
             * three spans are needed. Ignore the first two spans.
             */
            if(outcount  > 1  && curr_stride != next_stride)
                HGOTO_DONE(FALSE)
            if(outcount != 0  && next_block  != curr_block)
                HGOTO_DONE(FALSE)

            /* Keep the isolated stride to be 1 */
            if(outcount != 0)
                curr_stride = next_stride;

            /* Keep current starting point */
            curr_low    = span->low;

            /* Advance to next span */
            span = span->next;
            outcount++;
        } /* end while */

        /* Save the span information. */
        span_slab_info[rank - 1].start  = curr_start;
        span_slab_info[rank - 1].count  = outcount;
        span_slab_info[rank - 1].block  = curr_block;
        span_slab_info[rank - 1].stride = curr_stride;
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_rebuild_helper() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_rebuild
 PURPOSE
    Rebuild optimized hyperslab information if possible.
    (It can be recovered with regular selection)
 USAGE
    herr_t H5S_hyper_rebuild(space)
        const H5S_t *space;     IN: Dataspace to check
 RETURNS
    >=0 on success, <0 on failure
 DESCRIPTION
    Examine the span tree for a hyperslab selection and rebuild
    the start/stride/count/block information for the selection, if possible.

 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
    To be able to recover the optimized information, the span tree must conform
    to span tree able to be generated from a single H5S_SELECT_SET operation.

 EXAMPLES
 REVISION LOG
    This routine is the optimization of the old version. The previous version
    can only detect a singluar selection. This version is general enough to
    detect any regular selection.
    KY, 2005/9/22
--------------------------------------------------------------------------*/
htri_t
H5S_hyper_rebuild(H5S_t *space)
{
    H5S_hyper_dim_t top_span_slab_info[H5O_LAYOUT_NDIMS];
    unsigned rank;              /* Dataspace rank */
    htri_t ret_value = TRUE;    /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    /* Check args */
    HDassert(space);
    HDassert(space->select.sel_info.hslab->span_lst);

    /* Check the rank of space */
    rank = space->extent.rank;

    /* Check whether the slab can be rebuilt. Only regular selection can be rebuilt. If yes, fill in correct values.*/
    if(!H5S_hyper_rebuild_helper(space->select.sel_info.hslab->span_lst->head, top_span_slab_info, rank)) {
        space->select.sel_info.hslab->diminfo_valid = H5S_DIMINFO_VALID_IMPOSSIBLE;
        HGOTO_DONE(FALSE)
    } /* end if */
    else {
        H5S_hyper_dim_t *opt_diminfo;   /* Convenience pointer to optimized dimension info */
        H5S_hyper_dim_t *app_diminfo;   /* Convenience pointer to application dimension info */
        unsigned u;                     /* Local index variable */

        /* Set the convenience pointers */
        opt_diminfo = space->select.sel_info.hslab->opt_diminfo;
        app_diminfo = space->select.sel_info.hslab->app_diminfo;

        /* Set the dimension info for the dataspace, from the rebuilt info */
        for(u = 0; u < rank; u++) {
            app_diminfo[(rank - u) - 1].start  = opt_diminfo[(rank - u) - 1].start = top_span_slab_info[u].start;
            app_diminfo[(rank - u) - 1].stride = opt_diminfo[(rank - u) - 1].stride = top_span_slab_info[u].stride;
            app_diminfo[(rank - u) - 1].count  = opt_diminfo[(rank - u) - 1].count = top_span_slab_info[u].count;
            app_diminfo[(rank - u) - 1].block  = opt_diminfo[(rank - u) - 1].block = top_span_slab_info[u].block;
        } /* end for */

        space->select.sel_info.hslab->diminfo_valid = H5S_DIMINFO_VALID_YES;
    } /* end else */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_rebuild() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_generate_spans
 PURPOSE
    Create span tree for a regular hyperslab selection
 USAGE
    herr_t H5S_hyper_generate_spans(space)
        H5S_t *space;           IN/OUT: Pointer to dataspace
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
    Create a span tree representation of a regular hyperslab selection and
    add it to the information for the hyperslab selection.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_generate_spans(H5S_t *space)
{
    hsize_t tmp_start[H5O_LAYOUT_NDIMS];    /* Temporary start information */
    hsize_t tmp_stride[H5O_LAYOUT_NDIMS];   /* Temporary stride information */
    hsize_t tmp_count[H5O_LAYOUT_NDIMS];    /* Temporary count information */
    hsize_t tmp_block[H5O_LAYOUT_NDIMS];    /* Temporary block information */
    unsigned u;                             /* Local index variable */
    herr_t ret_value = SUCCEED;             /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(space);
    HDassert(H5S_GET_SELECT_TYPE(space) == H5S_SEL_HYPERSLABS);

    /* Get the diminfo */
    for(u = 0; u < space->extent.rank; u++) {
        tmp_start[u] = space->select.sel_info.hslab->opt_diminfo[u].start;
        tmp_stride[u] = space->select.sel_info.hslab->opt_diminfo[u].stride;
        tmp_count[u] = space->select.sel_info.hslab->opt_diminfo[u].count;
        tmp_block[u] = space->select.sel_info.hslab->opt_diminfo[u].block;
    } /* end for */

    /* Build the hyperslab information also */
    if(H5S_generate_hyperslab(space, H5S_SELECT_SET, tmp_start, tmp_stride, tmp_count, tmp_block) < 0)
        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINSERT, FAIL, "can't generate hyperslabs")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_generate_spans() */

/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_update_bound_box
 PURPOSE
    Update the bounding box of the hyperslab depending on the selection operator
 USAGE
    herr_t H5S_hyper_update_bound_box(space, op, low_bounds, high_bounds, overlapped)
        H5S_t *space;              IN/OUT: Dataspace containing the 1st space
        H5S_seloper_t op;          IN: Selection operator
        const hszie_t low_bounds;  IN: The lower bounds of the 2nd space
        const hsize_t high_bounds; IN: The higher bounds of the 2nd space
 RETURNS
    Non-negative on success, negative on failure
 DESCRIPTION
 GLOBAL VARIABLES
 COMMENTS, BUGS
 ASSUMPTIONS
    This function assumes that the two bound boxes are not overlapped. If two bound
    boxes are overlapped, the "space"'s bound box should be calculated recursively,
    which has been done in H5S_fill_in_new_space function.
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_update_bound_box(H5S_t *space, H5S_seloper_t op,
    const hsize_t low_bounds[], const hsize_t high_bounds[])
{
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(space);
    HDassert(H5S_GET_SELECT_TYPE(space) == H5S_SEL_HYPERSLABS);

    /* The offset of the dataspace is ignored here */
    switch(op) {
        case H5S_SELECT_OR:
        case H5S_SELECT_XOR:
            {
                unsigned u;     /* Local index variable */

                for(u = 0; u < space->extent.rank; u++) {
                    /* choose the smaller value for the low bound as the op is (X)OR */
                    space->select.low_bounds[u] = MIN(low_bounds[u], space->select.low_bounds[u]);

                    /* choose the larger value for the high bound as the op is (X)OR */
                    space->select.high_bounds[u] = MAX(high_bounds[u], space->select.high_bounds[u]);
                } /* end for */
            }
            break;

        case H5S_SELECT_AND:
            {
                hsize_t tmp = HSIZET_MAX;

                H5VM_array_fill(space->select.low_bounds, &tmp, sizeof(hsize_t), space->extent.rank);
                HDmemset(space->select.high_bounds, 0, sizeof(hsize_t) * space->extent.rank);
            }
            break;

        case H5S_SELECT_NOTB:
            /* Since the two spaces don't overlap, the bound box needs no update */
            break;

        case H5S_SELECT_NOTA:
            /* Since the two spaces don't overlap, the bound box is same with 2nd space */
            HDmemcpy(space->select.low_bounds, low_bounds, sizeof(hsize_t) * space->extent.rank);
            HDmemcpy(space->select.high_bounds, high_bounds, sizeof(hsize_t) * space->extent.rank);
            break;

        case H5S_SELECT_NOOP:
        case H5S_SELECT_SET:
        case H5S_SELECT_APPEND:
        case H5S_SELECT_PREPEND:
        case H5S_SELECT_INVALID:
        default:
            HGOTO_ERROR(H5E_ARGS, H5E_UNSUPPORTED, FAIL, "invalid selection operation")
    } /* end switch(op) */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_update_bound_box() */


/*--------------------------------------------------------------------------
 NAME
    H5S_fill_in_new_space
 PURPOSE
    Combine two span lists, one from an existing dataspace and the
    other from input arguments, into a new selection depending on the
    selection operator. The new selection is put into a resulting dataspace
    which could be allocated inside the function.
 USAGE
    herr_t H5S_fill_in_new_space(space1, op, space2_span_lst, space2_low_bound,
                                space2_high_bound, can_own_span2,
                                span2_owned, result)
        H5S_t *space1;           IN: Dataspace containing the first span list
        H5S_seloper_t op;        IN: Selection operation
        H5S_hyper_span_info_t *space2_span_lst; IN: Second span list
        const hsize_t *span2_low_bound; IN: The lower bound box for 2nd span list
        const hsize_t *span2_high_bound; IN: The higher bound box for 2nd span list
        hbool_t can_own_span2;   IN: Indicates whether the 2nd span list could be
                                     owned by the result. If not, the 2nd span list
                                     has to be copied.
        hbool_t *span2_owned;  OUT: Indicates if the 2nd span list is actually owned
        H5S_t **result;  OUT: The dataspace containing the the new selection. It
                              could be same with the 1st dataspace.
 RETURNS
    Non-negative on success, negative on failure
 PROGRAMMER
    Chao Mei July 8, 2011
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
    The function is extracted from H5S_generate_hyperslab, and then modified
    to be general enough for hyperslab selection operations.
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_fill_in_new_space(H5S_t *space1, H5S_seloper_t op,
    H5S_hyper_span_info_t *space2_span_lst, const hsize_t *space2_low_bound,
    const hsize_t *space2_high_bound, hbool_t can_own_span2, hbool_t *span2_owned,
    hbool_t *updated_spans, H5S_t **result)
{
    H5S_hyper_span_info_t *a_not_b = NULL;      /* Span tree for hyperslab spans in old span tree and not in new span tree */
    H5S_hyper_span_info_t *a_and_b = NULL;      /* Span tree for hyperslab spans in both old and new span trees */
    H5S_hyper_span_info_t *b_not_a = NULL;      /* Span tree for hyperslab spans in new span tree and not in old span tree */
    hsize_t *space1_low_bound = space1->select.low_bounds;
    hsize_t *space1_high_bound = space1->select.high_bounds;
    hbool_t overlapped = FALSE;
    hbool_t is_result_new = FALSE;
    unsigned u;                         /* Local index variable */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    HDassert(space1);
    HDassert(space2_span_lst);
    HDassert(op >= H5S_SELECT_OR && op <= H5S_SELECT_NOTA);
    /* The result is either a to-be-created space or an empty one */
    HDassert(*result == NULL || *result == space1);
    HDassert(space1->select.sel_info.hslab->span_lst);
    HDassert(span2_owned);

    /* Reset flags to return */
    *span2_owned = FALSE;
    *updated_spans = FALSE;

    /* The result shares the same info from space1 */
    if(*result == NULL) {
        if(NULL == ((*result) = H5S_copy(space1, TRUE, TRUE)))
            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINIT, FAIL, "unable to copy dataspace")
        space1->select.sel_info.hslab->span_lst->count--;
        (*result)->select.sel_info.hslab->span_lst = NULL;
        is_result_new = TRUE;
    } /* end if */

    /* Check bound box of both spaces to see if they overlap or border */
    for(u = 0; u < space1->extent.rank; u++) {
        /*
         * Three conditions for overlapping/bordering:
         * 1. the lower bound of the new span is between the lower and
         * higher bound of the existing one. In other words, the left
         * part of new span will at least overlap or border with the
         * existing one.
         * 2. the higher bound of the new span is between the lower and
         * higher bound of the exisiting one. In other words, the right
         * part of new span will at least overlap or border with the
         * existing one.
         * 3. The new span includes the existing one where the lower bound
         * is smaller than that of the existing one, and the higher bound
         * is larger than that of the existing one.
         */
            /* condition 1 */
        if((space1_low_bound[u] >= space2_low_bound[u] &&
                    space1_low_bound[u] <= (space2_high_bound[u] + 1)) ||
                /* condition 2 */
               ((space1_high_bound[u] + 1) >= space2_low_bound[u] &&
                    space1_high_bound[u] <= space2_high_bound[u]) ||
                /* condition 3 */
               (space1_low_bound[u] < space2_low_bound[u] &&
                    space1_high_bound[u] > space2_high_bound[u])) {
            overlapped = TRUE;
            break;
        } /* end if */
    } /* end for */

    /* If two spans are disjoint in terms of bound box, then work could be much simplified. */
    if(!overlapped) {
        switch(op) {
            case H5S_SELECT_OR:
            case H5S_SELECT_XOR:
                /* Add the new disjoint spans to the space */
                /* A copy of space1's spans to *result, and another copy of space2's spans */
                if(is_result_new)
                    (*result)->select.sel_info.hslab->span_lst = H5S_hyper_copy_span(space1->select.sel_info.hslab->span_lst);
                if(!can_own_span2) {
                    b_not_a = H5S_hyper_copy_span(space2_span_lst);
                    if(H5S_hyper_add_disjoint_spans(*result, b_not_a) < 0)
                        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINSERT, FAIL, "can't append hyperslabs")

                    /* The new_spans is now owned by space, so its memory space should not be released */
                    b_not_a = NULL;
                } /* end if */
                else {
                    if(H5S_hyper_add_disjoint_spans(*result, space2_span_lst) < 0)
                        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINSERT, FAIL, "can't append hyperslabs")
                    *span2_owned = TRUE;
                } /* end else */

                /* Indicate that the spans changed */
                *updated_spans = TRUE;
                break;

            case H5S_SELECT_AND:
                /* Convert *result to "none" selection */
                if(H5S_select_none(*result) < 0)
                    HGOTO_ERROR(H5E_DATASPACE, H5E_CANTSELECT, FAIL, "can't convert selection")
                HGOTO_DONE(SUCCEED);

            case H5S_SELECT_NOTB:
                /* Copy space1's spans to *result */
                if(is_result_new)
                    (*result)->select.sel_info.hslab->span_lst = H5S_hyper_copy_span(space1->select.sel_info.hslab->span_lst);

                /* Indicate that the spans changed */
                *updated_spans = TRUE;
                break;

            case H5S_SELECT_NOTA:
                if(!is_result_new) {
                    HDassert(space1 == *result);

                    /* Free the current selection */
                    if(H5S_hyper_free_span_info(space1->select.sel_info.hslab->span_lst) < 0)
                        HGOTO_ERROR(H5E_INTERNAL, H5E_CANTFREE, FAIL, "failed to release hyperslab spans")
                } /* end if */

                /* Copy space2's spans to *result */
                if(!can_own_span2)
                    (*result)->select.sel_info.hslab->span_lst = H5S_hyper_copy_span(space2_span_lst);
                else {
                    (*result)->select.sel_info.hslab->span_lst = space2_span_lst;
                    *span2_owned = TRUE;
                } /* end else */

                /* Reset the number of items in selection */
                (*result)->select.num_elem = H5S_hyper_spans_nelem(space2_span_lst);

                /* Indicate that the spans changed */
                *updated_spans = TRUE;
                break;

            case H5S_SELECT_NOOP:
            case H5S_SELECT_SET:
            case H5S_SELECT_APPEND:
            case H5S_SELECT_PREPEND:
            case H5S_SELECT_INVALID:
            default:
                HGOTO_ERROR(H5E_ARGS, H5E_UNSUPPORTED, FAIL, "invalid selection operation")
        } /* end switch */

        /* update bound box of the selection result */
        if(H5S_hyper_update_bound_box(*result, op, space2_low_bound, space2_high_bound) < 0)
            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTSELECT, FAIL, "can't update the bound box")
    } /* end if */
    else {
        const hsize_t *span_a_b_bounds[4];      /* Pointers to space1 & space2 bound boxes */
        hsize_t bounds_array[6 * H5S_MAX_RANK]; /* Array for bounding boxes */
        hsize_t *tmp_bounds_array;              /* Temp. pointer to array for bounding boxes */
        hsize_t tmp = HSIZET_MAX;
        unsigned selector = 0;                  /* Select which clipping spans to generate */

        /* Generate mask for clip operation depending on the op */
        switch(op) {
            case H5S_SELECT_OR:
                selector = H5S_HYPER_COMPUTE_B_NOT_A; /* a + b_not_a */
                break;

            case H5S_SELECT_XOR: /* a_not_b + b_not_a */
                selector = H5S_HYPER_COMPUTE_A_NOT_B | H5S_HYPER_COMPUTE_B_NOT_A;
                break;

            case H5S_SELECT_AND: /* a_and_b */
                selector = H5S_HYPER_COMPUTE_A_AND_B;
                break;

            case H5S_SELECT_NOTB: /* a_not_b */
                selector = H5S_HYPER_COMPUTE_A_NOT_B;
                break;

            case H5S_SELECT_NOTA: /* b_not_a */
                selector = H5S_HYPER_COMPUTE_B_NOT_A;
                break;

            case H5S_SELECT_NOOP:
            case H5S_SELECT_SET:
            case H5S_SELECT_APPEND:
            case H5S_SELECT_PREPEND:
            case H5S_SELECT_INVALID:
            default:
                HGOTO_ERROR(H5E_ARGS, H5E_UNSUPPORTED, FAIL, "invalid selection operation")
        } /* end switch */

        /* Initialize arrays for 3 pairs of low/high bounds to default values */
        tmp_bounds_array = bounds_array;
        for(u = 0; u < 3; u++) {
            /* set the bounds to be the default value */
            H5VM_array_fill(tmp_bounds_array, &tmp, sizeof(hsize_t), space1->extent.rank);
            tmp_bounds_array += space1->extent.rank;
            HDmemset(tmp_bounds_array, 0, sizeof(hsize_t) * space1->extent.rank);
            tmp_bounds_array += space1->extent.rank;
        } /* end for */

        /* Initialize array of pointers to space1 & space2's bound arrays */
        span_a_b_bounds[0] = space1->select.low_bounds;
        span_a_b_bounds[1] = space1->select.high_bounds;
        span_a_b_bounds[2] = space2_low_bound;
        span_a_b_bounds[3] = space2_high_bound;

        /* Generate lists of spans which overlap and don't overlap */
        if(H5S_hyper_clip_spans(space1->select.sel_info.hslab->span_lst,
                space2_span_lst, selector, 0, space1->extent.rank,
                span_a_b_bounds, bounds_array, &a_not_b, &a_and_b, &b_not_a) < 0)
            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTCLIP, FAIL, "can't clip hyperslab information")
        switch(op) {
            case H5S_SELECT_OR:
                if(is_result_new)
                    (*result)->select.sel_info.hslab->span_lst = H5S_hyper_copy_span(space1->select.sel_info.hslab->span_lst);
                break;

            case H5S_SELECT_AND:
            case H5S_SELECT_XOR:
            case H5S_SELECT_NOTB:
            case H5S_SELECT_NOTA:
                if(!is_result_new) {
                    HDassert(space1 == *result);

                    /* Free the current selection */
                    if(H5S_hyper_free_span_info(space1->select.sel_info.hslab->span_lst) < 0)
                        HGOTO_ERROR(H5E_INTERNAL, H5E_CANTFREE, FAIL, "failed to release hyperslab spans")
                    space1->select.sel_info.hslab->span_lst = NULL;
                } /* end if */

                /* Reset the number of items in selection */
                (*result)->select.num_elem = 0;
                break;

            case H5S_SELECT_NOOP:
            case H5S_SELECT_SET:
            case H5S_SELECT_APPEND:
            case H5S_SELECT_PREPEND:
            case H5S_SELECT_INVALID:
            default:
                HGOTO_ERROR(H5E_ARGS, H5E_UNSUPPORTED, FAIL, "invalid selection operation")
        } /* end switch */

        /* Check if there are any non-overlapped selections */
        if(a_not_b) {
            hsize_t *a_not_b_low_bounds = bounds_array;
            hsize_t *a_not_b_high_bounds = bounds_array + space1->extent.rank;

            /* Other than OR, the span_lst is set to NULL. And in OR, a_not_b is not needed */
            HDassert(NULL == (*result)->select.sel_info.hslab->span_lst);

            if(H5S_hyper_merge_spans(*result, a_not_b) < 0)
                HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINSERT, FAIL, "can't insert hyperslabs")

            /* Update the bound box: just use a_not_b's bound box as no span tree is in result */
            HDmemcpy((*result)->select.low_bounds, a_not_b_low_bounds, sizeof(hsize_t) * space1->extent.rank);
            HDmemcpy((*result)->select.high_bounds, a_not_b_high_bounds, sizeof(hsize_t) * space1->extent.rank);

            /* Update the number of elements in current selection */
            (*result)->select.num_elem += H5S_hyper_spans_nelem(a_not_b);

            /* Indicate that the spans were updated */
            *updated_spans = TRUE;

            /* Indicate that the a_not_b spans are owned */
            a_not_b = NULL;
        } /* end if */

        if(a_and_b) {
            hsize_t *a_and_b_low_bounds = bounds_array + 2 * space1->extent.rank;
            hsize_t *a_and_b_high_bounds = bounds_array + 3 * space1->extent.rank;

            /**
             * 1. Other than OR, the span_lst is set to NULL. And in OR,
             * a_and_b is not needed
             * 2. a_not_b will never be computed together with a_and_b
             * because merging these two equals to a.
             */
            HDassert(NULL == (*result)->select.sel_info.hslab->span_lst);

            if(H5S_hyper_merge_spans(*result, a_and_b) < 0)
                HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINSERT, FAIL, "can't insert hyperslabs")

            /* Update the bound box: just use a_not_b's bound box as no span tree is in result */
            HDmemcpy((*result)->select.low_bounds, a_and_b_low_bounds, sizeof(hsize_t) * space1->extent.rank);
            HDmemcpy((*result)->select.high_bounds, a_and_b_high_bounds, sizeof(hsize_t) * space1->extent.rank);

            /* Update the number of elements in current selection */
            (*result)->select.num_elem += H5S_hyper_spans_nelem(a_and_b);

            /* Indicate that the spans were updated */
            *updated_spans = TRUE;

            /* Indicate that the a_and_b spans are owned */
            a_and_b = NULL;
        } /* end if */

        if(b_not_a) {
            hsize_t *b_not_a_low_bounds = bounds_array + 4 * space1->extent.rank;
            hsize_t *b_not_a_high_bounds = bounds_array + 5 * space1->extent.rank;
            hbool_t has_span_tree = (NULL != (*result)->select.sel_info.hslab->span_lst);

            if(H5S_hyper_merge_spans(*result, b_not_a) < 0)
                HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINSERT, FAIL, "can't insert hyperslabs")

            /* Update the bound box */
            if(has_span_tree) {
                /* Since two span trees are merged, the update on bound box is like an OR operation */
                for(u = 0; u < space1->extent.rank; u++) {
                    (*result)->select.low_bounds[u] = MIN((*result)->select.low_bounds[u], b_not_a_low_bounds[u]);
                    (*result)->select.high_bounds[u] = MAX((*result)->select.high_bounds[u], b_not_a_high_bounds[u]);
                } /* end for */
            } /* end if */
            else {
                /* Just use b_not_a's bound box as no span tree is in result */
                HDmemcpy((*result)->select.low_bounds, b_not_a_low_bounds, sizeof(hsize_t) * space1->extent.rank);
                HDmemcpy((*result)->select.high_bounds, b_not_a_high_bounds, sizeof(hsize_t) * space1->extent.rank);
            } /* end else */

            /* Update the number of elements in current selection */
            (*result)->select.num_elem += H5S_hyper_spans_nelem(b_not_a);

            /* Indicate that the spans were updated */
            *updated_spans = TRUE;

            /* Indicate that the b_not_a spans are owned */
            b_not_a = NULL;
        } /* end if */
    } /* end else for the case the new span overlaps with the old (i.e. space) */

    /* Check if the spans weren't updated, and reset selection if so */
    if(!*updated_spans) {
        /** If updated_spans remains FALSE as in this branch, it
         *  means the result has been cleared in cases
         *  XOR/AND/NOTB/NOTA, and the result is a copy of the
         *  dataspace in the case OR.
         *
         *  If two dataspaces have generated any of the three clipped
         *  span trees (i.e. a_not_b, a_and_b, and b_not_a), the
         *  updated_spans must be TRUE.
         */
        if(H5S_SELECT_OR != op) {
            /* Convert *result to "none" selection */
            if(H5S_select_none(*result) < 0)
                HGOTO_ERROR(H5E_DATASPACE, H5E_CANTSELECT, FAIL, "can't convert selection")
        } /* end else */
    } /* end if */

done:
    /* Free resources */
    if(a_not_b)
        if(H5S_hyper_free_span_info(a_not_b) < 0)
            HDONE_ERROR(H5E_INTERNAL, H5E_CANTFREE, FAIL, "failed to release temporary hyperslab spans")
    if(a_and_b)
        if(H5S_hyper_free_span_info(a_and_b) < 0)
            HDONE_ERROR(H5E_INTERNAL, H5E_CANTFREE, FAIL, "failed to release temporary hyperslab spans")
    if(b_not_a)
        if(H5S_hyper_free_span_info(b_not_a) < 0)
            HDONE_ERROR(H5E_INTERNAL, H5E_CANTFREE, FAIL, "failed to release temporary hyperslab spans")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_fill_in_new_space() */


/*-------------------------------------------------------------------------
 * Function:	H5S_generate_hyperlab
 *
 * Purpose:	Generate hyperslab information from H5S_select_hyperslab()
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *              Tuesday, September 12, 2000
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5S_generate_hyperslab(H5S_t *space, H5S_seloper_t op, const hsize_t start[],
    const hsize_t stride[], const hsize_t count[], const hsize_t block[])
{
    H5S_hyper_span_info_t *new_spans=NULL;  /* Span tree for new hyperslab */
    hsize_t new_spans_low_bounds[H5S_MAX_RANK]; /* The lower bound box for the new hyperslab */
    hsize_t new_spans_high_bounds[H5S_MAX_RANK]; /* The upper bound box for the new hyperslab */
    unsigned u;                         /* Local index variable */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Check args */
    HDassert(space);
    HDassert(op > H5S_SELECT_NOOP && op < H5S_SELECT_INVALID);
    HDassert(start);
    HDassert(stride);
    HDassert(count);
    HDassert(block);

    /* Generate span tree for new hyperslab information */
    if(NULL == (new_spans = H5S_hyper_make_spans(space->extent.rank, start, stride, count, block)))
        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINSERT, FAIL, "can't create hyperslab information")

    /* Generate the bound box for the new hyperslab */
    for(u = 0; u < space->extent.rank; u++) {
        new_spans_low_bounds[u] = start[u];
        new_spans_high_bounds[u] = new_spans_low_bounds[u] + stride[u] * (count[u] - 1) + (block[u] - 1);
    } /* end for */

    /* Generate list of blocks to add/remove based on selection operation */
    if(op == H5S_SELECT_SET) {
        H5S_hyper_span_info_t *existing_spans = space->select.sel_info.hslab->span_lst;

        /* Add new spans to current selection */
        if(NULL != existing_spans)
            if(H5S_hyper_free_span_info(existing_spans) < 0)
                HDONE_ERROR(H5E_INTERNAL, H5E_CANTFREE, FAIL, "failed to release existing hyperslab spans")

        space->select.sel_info.hslab->span_lst = new_spans;

        /* Set the number of elements in current selection */
        space->select.num_elem = H5S_hyper_spans_nelem(new_spans);

        /* Update the bound box of this hyperslab */
        HDmemcpy(space->select.low_bounds, new_spans_low_bounds, sizeof(hsize_t) * space->extent.rank);
        HDmemcpy(space->select.high_bounds, new_spans_high_bounds, sizeof(hsize_t) * space->extent.rank);

        /* Indicate that the new_spans are owned */
        new_spans = NULL;
    } /* end if */
    else {
        hbool_t new_spans_owned;
        hbool_t updated_spans;

        /* Generate new spans for space */
        if(H5S_fill_in_new_space(space, op, new_spans, new_spans_low_bounds, new_spans_high_bounds, TRUE, &new_spans_owned, &updated_spans, &space) < 0)
            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTSELECT, FAIL, "can't generate the specified hyperslab")

        /* Check if the spans were updated by H5S_fill_in_new_space */
        if(updated_spans) {
            H5S_hyper_dim_t new_hyper_diminfo[H5S_MAX_RANK];

            /* Sanity check */
            HDassert(space->select.sel_info.hslab->span_lst->head);

            /* Build diminfo struct */
            for(u = 0; u <space->extent.rank; u++) {
                new_hyper_diminfo[u].start = start[u];
                new_hyper_diminfo[u].stride = stride[u];
                new_hyper_diminfo[u].count = count[u];
                new_hyper_diminfo[u].block = block[u];
            } /* end for */

            /* Update space's dim info */
            if(H5S_hyper_update_diminfo(space, op, new_hyper_diminfo) < 0)
                HGOTO_ERROR(H5E_DATASPACE, H5E_CANTCOUNT, FAIL, "can't update hyperslab info")
        } /* end if */

        /* Indicate that the new_spans are owned, there's no need to free */
        if(new_spans_owned)
            new_spans = NULL;
    } /* end else */

done:
    if(new_spans)
        if(H5S_hyper_free_span_info(new_spans) < 0)
            HDONE_ERROR(H5E_INTERNAL, H5E_CANTFREE, FAIL, "failed to release temporary hyperslab spans")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_generate_hyperslab() */


/*-------------------------------------------------------------------------
 * Function:	H5S_select_hyperslab
 *
 * Purpose:	Internal version of H5Sselect_hyperslab().
 *
 * Return:	Non-negative on success/Negative on failure
 *
 * Programmer:	Quincey Koziol
 *              Wednesday, January 10, 2001
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5S_select_hyperslab(H5S_t *space, H5S_seloper_t op, const hsize_t start[],
    const hsize_t *stride, const hsize_t count[], const hsize_t *block)
{
    hsize_t int_stride[H5O_LAYOUT_NDIMS];   /* Internal storage for stride information */
    hsize_t int_count[H5O_LAYOUT_NDIMS];    /* Internal storage for count information */
    hsize_t int_block[H5O_LAYOUT_NDIMS];    /* Internal storage for block information */
    const hsize_t *opt_stride;          /* Optimized stride information */
    const hsize_t *opt_count;           /* Optimized count information */
    const hsize_t *opt_block;           /* Optimized block information */
    unsigned u;                         /* Local index variable */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Check args */
    HDassert(space);
    HDassert(start);
    HDassert(count);
    HDassert(op > H5S_SELECT_NOOP && op < H5S_SELECT_INVALID);

    /* Point to the correct stride values */
    if(stride==NULL)
        stride = _ones;

    /* Point to the correct block values */
    if(block==NULL)
        block = _ones;

    /*
     * Check new selection.
     */
    for(u=0; u<space->extent.rank; u++) {
        /* Check for overlapping hyperslab blocks in new selection. */
        if(count[u]>1 && stride[u]<block[u])
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "hyperslab blocks overlap")

        /* Detect zero-sized hyperslabs in new selection */
        if(count[u] == 0 || block[u] == 0) {
            switch(op) {
                case H5S_SELECT_SET:   /* Select "set" operation */
                case H5S_SELECT_AND:   /* Binary "and" operation for hyperslabs */
                case H5S_SELECT_NOTA:  /* Binary "B not A" operation for hyperslabs */
                    /* Convert to "none" selection */
                    if(H5S_select_none(space) < 0)
                        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTSELECT, FAIL, "can't convert selection")
                    HGOTO_DONE(SUCCEED);

                case H5S_SELECT_OR:    /* Binary "or" operation for hyperslabs */
                case H5S_SELECT_XOR:   /* Binary "xor" operation for hyperslabs */
                case H5S_SELECT_NOTB:  /* Binary "A not B" operation for hyperslabs */
                    HGOTO_DONE(SUCCEED);        /* Selection stays same */

                case H5S_SELECT_NOOP:
                case H5S_SELECT_APPEND:
                case H5S_SELECT_PREPEND:
                case H5S_SELECT_INVALID:
                default:
                    HGOTO_ERROR(H5E_ARGS, H5E_UNSUPPORTED, FAIL, "invalid selection operation")
            } /* end switch */
        } /* end if */
    } /* end for */

    /* Optimize hyperslab parameters to merge contiguous blocks, etc. */
    if(stride == _ones && block == _ones) {
        /* Point to existing arrays */
        opt_stride = _ones;
        opt_count = _ones;
        opt_block = count;
    } /* end if */
    else {
        /* Point to local arrays */
        opt_stride = int_stride;
        opt_count = int_count;
        opt_block = int_block;
        for(u=0; u<space->extent.rank; u++) {
            /* contiguous hyperslabs have the block size equal to the stride */
            if(stride[u]==block[u]) {
                int_count[u]=1;
                int_stride[u]=1;
                if(block[u]==1)
                    int_block[u]=count[u];
                else
                    int_block[u]=block[u]*count[u];
            } /* end if */
            else {
                if(count[u]==1)
                    int_stride[u]=1;
                else {
                    HDassert(stride[u] > block[u]);
                    int_stride[u]=stride[u];
                } /* end else */
                int_count[u]=count[u];
                int_block[u]=block[u];
            } /* end else */
        } /* end for */
    } /* end else */

    /* Fixup operation for non-hyperslab selections */
    switch(H5S_GET_SELECT_TYPE(space)) {
        case H5S_SEL_NONE:   /* No elements selected in dataspace */
            switch(op) {
                case H5S_SELECT_SET:   /* Select "set" operation */
                    /* Change "none" selection to hyperslab selection */
                    break;

                case H5S_SELECT_OR:    /* Binary "or" operation for hyperslabs */
                case H5S_SELECT_XOR:   /* Binary "xor" operation for hyperslabs */
                case H5S_SELECT_NOTA:  /* Binary "B not A" operation for hyperslabs */
                    op=H5S_SELECT_SET; /* Maps to "set" operation when applied to "none" selection */
                    break;

                case H5S_SELECT_AND:   /* Binary "and" operation for hyperslabs */
                case H5S_SELECT_NOTB:  /* Binary "A not B" operation for hyperslabs */
                    HGOTO_DONE(SUCCEED);        /* Selection stays "none" */

                case H5S_SELECT_NOOP:
                case H5S_SELECT_APPEND:
                case H5S_SELECT_PREPEND:
                case H5S_SELECT_INVALID:
                default:
                    HGOTO_ERROR(H5E_ARGS, H5E_UNSUPPORTED, FAIL, "invalid selection operation")
            } /* end switch */
            break;

        case H5S_SEL_ALL:    /* All elements selected in dataspace */
            switch(op) {
                case H5S_SELECT_SET:   /* Select "set" operation */
                    /* Change "all" selection to hyperslab selection */
                    break;

                case H5S_SELECT_OR:    /* Binary "or" operation for hyperslabs */
                    HGOTO_DONE(SUCCEED);        /* Selection stays "all" */

                case H5S_SELECT_AND:   /* Binary "and" operation for hyperslabs */
                    op=H5S_SELECT_SET; /* Maps to "set" operation when applied to "none" selection */
                    break;

                case H5S_SELECT_XOR:   /* Binary "xor" operation for hyperslabs */
                case H5S_SELECT_NOTB:  /* Binary "A not B" operation for hyperslabs */
                    /* Convert current "all" selection to "real" hyperslab selection */
                    /* Then allow operation to proceed */
                    {
                        hsize_t tmp_start[H5O_LAYOUT_NDIMS];   /* Temporary start information */
                        hsize_t tmp_stride[H5O_LAYOUT_NDIMS];   /* Temporary stride information */
                        hsize_t tmp_count[H5O_LAYOUT_NDIMS];    /* Temporary count information */
                        hsize_t tmp_block[H5O_LAYOUT_NDIMS];    /* Temporary block information */

                        /* Fill in temporary information for the dimensions */
                        for(u=0; u<space->extent.rank; u++) {
                            tmp_start[u]=0;
                            tmp_stride[u]=1;
                            tmp_count[u]=1;
                            tmp_block[u]=space->extent.size[u];
                        } /* end for */

                        /* Convert to hyperslab selection */
                        if(H5S_select_hyperslab(space,H5S_SELECT_SET,tmp_start,tmp_stride,tmp_count,tmp_block)<0)
                            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTDELETE, FAIL, "can't convert selection")
                    } /* end case */
                    break;

                case H5S_SELECT_NOTA:  /* Binary "B not A" operation for hyperslabs */
                    /* Convert to "none" selection */
                    if(H5S_select_none(space) < 0)
                        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTSELECT, FAIL, "can't convert selection")
                    HGOTO_DONE(SUCCEED);

                case H5S_SELECT_NOOP:
                case H5S_SELECT_APPEND:
                case H5S_SELECT_PREPEND:
                case H5S_SELECT_INVALID:
                default:
                    HGOTO_ERROR(H5E_ARGS, H5E_UNSUPPORTED, FAIL, "invalid selection operation")
            } /* end switch */
            break;

        case H5S_SEL_HYPERSLABS:
            /* Hyperslab operation on hyperslab selection, OK */
            break;

        case H5S_SEL_POINTS: /* Can't combine hyperslab operations and point selections currently */
            if(op==H5S_SELECT_SET)      /* Allow only "set" operation to proceed */
                break;
            /* Else fall through to error */

        case H5S_SEL_ERROR:
        case H5S_SEL_N:
        default:
            HGOTO_ERROR(H5E_ARGS, H5E_UNSUPPORTED, FAIL, "invalid selection operation")
    } /* end switch */

    if(op == H5S_SELECT_SET) {
        /* If we are setting a new selection, remove current selection first */
        if(H5S_SELECT_RELEASE(space) < 0)
            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTDELETE, FAIL, "can't release selection")

        /* Allocate space for the hyperslab selection information */
        if(NULL == (space->select.sel_info.hslab = H5FL_MALLOC(H5S_hyper_sel_t)))
            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "can't allocate hyperslab info")

        /* Save the diminfo */
        space->select.num_elem = 1;
        for(u = 0; u < space->extent.rank; u++) {
            space->select.sel_info.hslab->app_diminfo[u].start = start[u];
            space->select.sel_info.hslab->app_diminfo[u].stride = stride[u];
            space->select.sel_info.hslab->app_diminfo[u].count = count[u];
            space->select.sel_info.hslab->app_diminfo[u].block = block[u];

            space->select.sel_info.hslab->opt_diminfo[u].start = start[u];
            space->select.sel_info.hslab->opt_diminfo[u].stride = opt_stride[u];
            space->select.sel_info.hslab->opt_diminfo[u].count = opt_count[u];
            space->select.sel_info.hslab->opt_diminfo[u].block = opt_block[u];

            /* update bound box of the hyperslab selection */
            space->select.low_bounds[u] = start[u];
            space->select.high_bounds[u] = space->select.low_bounds[u] + stride[u] * (count[u] - 1) + (block[u] - 1);

            space->select.num_elem *= (opt_count[u] * opt_block[u]);
        } /* end for */

        /* Indicate that the dimension information is valid */
        space->select.sel_info.hslab->diminfo_valid = H5S_DIMINFO_VALID_YES;

        /* Indicate that there's no slab information */
        space->select.sel_info.hslab->span_lst = NULL;

        /* Set selection type */
        space->select.type = H5S_sel_hyper;
    } /* end if */
    else if(op >= H5S_SELECT_OR && op <= H5S_SELECT_NOTA) {
        /* Sanity check */
        HDassert(H5S_GET_SELECT_TYPE(space) == H5S_SEL_HYPERSLABS);

        /* Check if there's no hyperslab span information currently */
        if(NULL == space->select.sel_info.hslab->span_lst)
            if(H5S_hyper_generate_spans(space) < 0)
                HGOTO_ERROR(H5E_DATASPACE, H5E_UNINITIALIZED, FAIL, "dataspace does not have span tree")

        /* Set selection type */
        /* (Could be overridden by resetting selection to 'none', below) */
        space->select.type = H5S_sel_hyper;

        /* Add in the new hyperslab information */
        if(H5S_generate_hyperslab(space, op, start, opt_stride, opt_count, opt_block) < 0)
            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINSERT, FAIL, "can't generate hyperslabs")
    } /* end if */
    else
        HGOTO_ERROR(H5E_ARGS, H5E_UNSUPPORTED, FAIL, "invalid selection operation")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_select_hyperslab() */


/*--------------------------------------------------------------------------
 NAME
    H5Sselect_hyperslab
 PURPOSE
    Specify a hyperslab to combine with the current hyperslab selection
 USAGE
    herr_t H5Sselect_hyperslab(dsid, op, start, stride, count, block)
        hid_t dsid;             IN: Dataspace ID of selection to modify
        H5S_seloper_t op;       IN: Operation to perform on current selection
        const hsize_t *start;        IN: Offset of start of hyperslab
        const hsize_t *stride;       IN: Hyperslab stride
        const hsize_t *count;        IN: Number of blocks included in hyperslab
        const hsize_t *block;        IN: Size of block in hyperslab
 RETURNS
    Non-negative on success/Negative on failure
 DESCRIPTION
    Combines a hyperslab selection with the current selection for a dataspace.
    If the current selection is not a hyperslab, it is freed and the hyperslab
    parameters passed in are combined with the H5S_SEL_ALL hyperslab (ie. a
    selection composing the entire current extent).  If STRIDE or BLOCK is
    NULL, they are assumed to be set to all '1'.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
herr_t
H5Sselect_hyperslab(hid_t space_id, H5S_seloper_t op, const hsize_t start[],
    const hsize_t stride[], const hsize_t count[], const hsize_t block[])
{
    H5S_t *space;               /* Dataspace to modify selection of */
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE6("e", "iSs*h*h*h*h", space_id, op, start, stride, count, block);

    /* Check args */
    if(NULL == (space = (H5S_t *)H5I_object_verify(space_id, H5I_DATASPACE)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a dataspace")
    if(H5S_SCALAR == H5S_GET_EXTENT_TYPE(space))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "hyperslab doesn't support H5S_SCALAR space")
    if(H5S_NULL == H5S_GET_EXTENT_TYPE(space))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "hyperslab doesn't support H5S_NULL space")
    if(start == NULL || count == NULL)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "hyperslab not specified")
    if(!(op > H5S_SELECT_NOOP && op < H5S_SELECT_INVALID))
        HGOTO_ERROR(H5E_ARGS, H5E_UNSUPPORTED, FAIL, "invalid selection operation")
    if(stride != NULL) {
        unsigned u;             /* Local index variable */

        /* Check for 0-sized strides */
        for(u = 0; u < space->extent.rank; u++) {
            if(stride[u] == 0)
                HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid stride==0 value")
        } /* end for */
    } /* end if */

    if(H5S_select_hyperslab(space, op, start, stride, count, block) < 0)
        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINIT, FAIL, "unable to set hyperslab selection")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Sselect_hyperslab() */


/*--------------------------------------------------------------------------
 NAME
    H5S_fill_in_hyperslab
 PURPOSE
    Specify a hyperslab to combine with the current hyperslab selection, and
    store the result in the new hyperslab selection.
 USAGE
    herr_t H5S_fill_in_hyperslab(new_space, old_space, op, start, stride, count, block)
        H5S_t old_space;             IN: The old space the selection is performed on
        H5S_seloper_t op;            IN: Operation to perform on current selection
        const hsize_t *start;        IN: Offset of start of hyperslab
        const hsize_t *stride;       IN: Hyperslab stride
        const hsize_t *count;        IN: Number of blocks included in hyperslab
        const hsize_t *block;        IN: Size of block in hyperslab
        H5S_t *new_space;            OUT: The new dataspace to store the selection result
 RETURNS
    Non-negative on success/Negative on failure
 DESCRIPTION
    Combines a hyperslab selection with the current selection for a dataspace.
    If STRIDE or BLOCK is NULL, they are assumed to be set to all '1'.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
    In some cases, the whole span tree in the old_space could be avoided from copying
    into the new_space. In order to deal with such cases, H5S_select_hyperslab could
    not be directly called. Otherwise, this function equals two steps:
    1. copy the whole span tree from old_space into new_space
    2. call H5S_select_hyperslab with the new_space.
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_fill_in_hyperslab(H5S_t *old_space, H5S_seloper_t op, const hsize_t start[],
    const hsize_t *stride, const hsize_t count[], const hsize_t *block,
    H5S_t **new_space)
{
    unsigned u;                     /* Local index variable */
    herr_t ret_value = SUCCEED;     /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Check args */
    HDassert(old_space);
    HDassert(start);
    HDassert(count);
    HDassert(op >= H5S_SELECT_SET && op <= H5S_SELECT_NOTA);
    HDassert(new_space);
    HDassert(*new_space == NULL);

    /* Point to the correct stride values */
    if(stride == NULL)
        stride = _ones;

    /* Point to the correct block values */
    if(block == NULL)
        block = _ones;

    /* Check new selection. */
    for(u = 0; u < old_space->extent.rank; u++) {
        /* Check for overlapping hyperslab blocks in new selection. */
        if(count[u] > 1 && stride[u] < block[u])
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "hyperslab blocks overlap")

        /* Detect zero-sized hyperslabs in new selection */
        if(count[u] == 0 || block[u] == 0) {
            switch(op) {
                case H5S_SELECT_AND:   /* Binary "and" operation for hyperslabs */
                case H5S_SELECT_NOTA:  /* Binary "B not A" operation for hyperslabs */
                    /* Convert to "none" selection */
                    /* Copy the first dataspace without sharing the list of spans */
                    if(NULL == ((*new_space) = H5S_copy(old_space, TRUE, TRUE)))
                        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINIT, FAIL, "unable to copy dataspace")
                    if(H5S_select_none((*new_space)) < 0)
                        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTSELECT, FAIL, "can't convert selection")
                    HGOTO_DONE(SUCCEED);

                case H5S_SELECT_OR:    /* Binary "or" operation for hyperslabs */
                case H5S_SELECT_XOR:   /* Binary "xor" operation for hyperslabs */
                case H5S_SELECT_NOTB:  /* Binary "A not B" operation for hyperslabs */
                    /* Copy the first dataspace with sharing the list of spans */
                    if(NULL == ((*new_space) = H5S_copy(old_space, FALSE, TRUE)))
                        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINIT, FAIL, "unable to copy dataspace")
                    HGOTO_DONE(SUCCEED);        /* Selection stays same */

                case H5S_SELECT_NOOP:
                case H5S_SELECT_SET:
                case H5S_SELECT_APPEND:
                case H5S_SELECT_PREPEND:
                case H5S_SELECT_INVALID:
                default:
                    HGOTO_ERROR(H5E_ARGS, H5E_UNSUPPORTED, FAIL, "invalid selection operation")
            } /* end switch */
        } /* end if */
    } /* end for */

    if(H5S_GET_SELECT_TYPE(old_space) == H5S_SEL_HYPERSLABS) {
        hsize_t new_low_bound[H5S_MAX_RANK];
        hsize_t new_high_bound[H5S_MAX_RANK];
        hbool_t overlapped = FALSE;

        /* Generate bounding box for hyperslab parameters */
        for(u = 0; u < old_space->extent.rank; u++) {
            new_low_bound[u] = start[u];
            new_high_bound[u] = new_low_bound[u] + stride[u] * (count[u]-1) + (block[u]-1);
        } /* end for */

        /* Check bounding box to see if two are disjoint */
        for(u = 0; u < old_space->extent.rank; u++) {
            /* Note: offset of this old_space is neglected */
            hsize_t old_low_bound = old_space->select.low_bounds[u];
            hsize_t old_high_bound = old_space->select.high_bounds[u];

            /* The following condition tests are copied from H5S_fill_in_new_space */
            if((new_low_bound[u] >= old_low_bound && new_low_bound[u] <= old_high_bound) ||
                   (new_high_bound[u] >= old_low_bound && new_high_bound[u] <= old_high_bound) ||
                   (new_low_bound[u] <= old_low_bound && new_high_bound[u] >= old_high_bound)) {
                overlapped = TRUE;
                break;
            } /* end if */
        } /* end for */

        if(!overlapped) {
            H5S_hyper_span_info_t *new_spans = NULL;
            H5S_hyper_dim_t new_hyper_diminfo[H5S_MAX_RANK];

            if(NULL == ((*new_space) = H5S_copy(old_space, TRUE, TRUE)))
                HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINIT, FAIL, "unable to copy dataspace")
            if(NULL != (*new_space)->select.sel_info.hslab->span_lst) {
                old_space->select.sel_info.hslab->span_lst->count--;
                (*new_space)->select.sel_info.hslab->span_lst = NULL;
            } /* end if */

            /* Generate hyperslab info for new space */
            switch(op) {
                case H5S_SELECT_OR:
                case H5S_SELECT_XOR:
                    /* Add the new space to the space */
                    if(NULL == (new_spans = H5S_hyper_make_spans(old_space->extent.rank, start, stride, count, block)))
                        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINSERT, FAIL, "can't create hyperslab information")
                    if(NULL != old_space->select.sel_info.hslab->span_lst)
                        (*new_space)->select.sel_info.hslab->span_lst = H5S_hyper_copy_span(old_space->select.sel_info.hslab->span_lst);
                    if(H5S_hyper_add_disjoint_spans(*new_space, new_spans) < 0)
                        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINSERT, FAIL, "can't append hyperslabs")
                    break;

                case H5S_SELECT_AND:
                    if(H5S_select_none((*new_space)) < 0)
                        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTSELECT, FAIL, "can't convert selection")
                    HGOTO_DONE(SUCCEED);

                case H5S_SELECT_NOTB:
                    if(old_space->select.sel_info.hslab->span_lst != NULL)
                        (*new_space)->select.sel_info.hslab->span_lst = H5S_hyper_copy_span(old_space->select.sel_info.hslab->span_lst);
                    else {
                        if(H5S_select_none((*new_space)) < 0)
                            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTSELECT, FAIL, "can't convert selection")
                        HGOTO_DONE(SUCCEED);
                    } /* end else */
                    break;

                case H5S_SELECT_NOTA:
                    if(NULL == (new_spans = H5S_hyper_make_spans(old_space->extent.rank, start, stride, count, block)))
                        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINSERT, FAIL, "can't create hyperslab information")
                    (*new_space)->select.sel_info.hslab->span_lst = new_spans;
                    (*new_space)->select.num_elem = H5S_hyper_spans_nelem(new_spans);
                    break;

                case H5S_SELECT_NOOP:
                case H5S_SELECT_SET:
                case H5S_SELECT_APPEND:
                case H5S_SELECT_PREPEND:
                case H5S_SELECT_INVALID:
                default:
                    HGOTO_ERROR(H5E_ARGS, H5E_UNSUPPORTED, FAIL, "invalid selection operation")
            } /* end switch */

            /* Update bound box */
            if(H5S_hyper_update_bound_box(*new_space, op, new_low_bound, new_high_bound)<0)
                HGOTO_ERROR(H5E_DATASPACE, H5E_CANTSELECT, FAIL, "can't update the bound box")

            /* Build diminfo struct */
            for(u = 0; u < (*new_space)->extent.rank; u++) {
                new_hyper_diminfo[u].start = start[u];
                new_hyper_diminfo[u].stride = stride[u];
                new_hyper_diminfo[u].count = count[u];
                new_hyper_diminfo[u].block = block[u];
            } /* end for */

            /* Update space's dim info */
            if(H5S_hyper_update_diminfo(*new_space, op, new_hyper_diminfo) < 0)
                HGOTO_ERROR(H5E_DATASPACE, H5E_CANTCOUNT, FAIL, "can't update hyperslab info")

            HGOTO_DONE(SUCCEED);
        } /* end if(!overlapped) */
    } /* end if the selection of old space is H5S_SEL_HYPERSLABS*/

    /* Copy the first dataspace with sharing the list of spans */
    if(NULL == ((*new_space) = H5S_copy(old_space, FALSE, TRUE)))
        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINIT, FAIL, "unable to copy dataspace")

    /* Note: a little overhead in calling the function as some conditions are checked again */
    if(H5S_select_hyperslab(*new_space, op, start, stride, count, block) < 0)
        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINIT, FAIL, "unable to set hyperslab selection")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_fill_in_hyperslab() */


/*-------------------------------------------------------------------------
 * Function:	H5S_fill_in_select
 *
 * Purpose:	Combines two hyperslabs with an operation, putting the
 *              result into a third hyperslab selection
 *
 * Return:	non-negative on success/NULL on failure
 *
 * Programmer:	Chao Mei
 *              Tuesday, July 5, 2011
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5S_fill_in_select(H5S_t *space1, H5S_seloper_t op, H5S_t *space2, H5S_t **result)
{
    hbool_t span2_owned;
    hbool_t updated_spans;
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Sanity check */
    HDassert(space1);
    HDassert(space2);
    HDassert(op >= H5S_SELECT_OR && op <= H5S_SELECT_NOTA);
    HDassert(space1->extent.rank == space2->extent.rank);
    /* The result is either a to-be-created space or an empty one */
    HDassert(NULL == *result || *result == space1);
    HDassert(space1->select.sel_info.hslab->span_lst);
    HDassert(space2->select.sel_info.hslab->span_lst);

    /* Note: the offset of space2 is not considered here for bounding box */
    if(H5S_fill_in_new_space(space1, op, space2->select.sel_info.hslab->span_lst, space2->select.low_bounds, space2->select.high_bounds, FALSE, &span2_owned, &updated_spans, result) < 0)
        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTSELECT, FAIL, "can't create the specified selection")

    /* Update diminfo if space2's diminfo was valid, otherwise just mark it as
     * invalid if the spans were updated */
    HDassert(result);
    if(updated_spans) {
        if(space2->select.sel_info.hslab->diminfo_valid == H5S_DIMINFO_VALID_YES) {
            if(H5S_hyper_update_diminfo(*result, op, space2->select.sel_info.hslab->opt_diminfo) < 0)
                HGOTO_ERROR(H5E_DATASPACE, H5E_CANTCOUNT, FAIL, "can't update hyperslab info")
        } /* end if */
        else
            (*result)->select.sel_info.hslab->diminfo_valid = H5S_DIMINFO_VALID_NO;
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_fill_in_select() */


/*--------------------------------------------------------------------------
 NAME
    H5Scombine_hyperslab
 PURPOSE
    Specify a hyperslab to combine with the current hyperslab selection and
    return a new dataspace with the combined selection as the selection in the
    new dataspace.
 USAGE
    hid_t H5Srefine_hyperslab(dsid, op, start, stride, count, block)
        hid_t dsid;             IN: Dataspace ID of selection to use
        H5S_seloper_t op;       IN: Operation to perform on current selection
        const hsize_t *start;        IN: Offset of start of hyperslab
        const hsize_t *stride;       IN: Hyperslab stride
        const hsize_t *count;        IN: Number of blocks included in hyperslab
        const hsize_t *block;        IN: Size of block in hyperslab
 RETURNS
    Dataspace ID on success/Negative on failure
 DESCRIPTION
    Combines a hyperslab selection with the current selection for a dataspace,
    creating a new dataspace to return the generated selection.
    If the current selection is not a hyperslab, it is freed and the hyperslab
    parameters passed in are combined with the H5S_SEL_ALL hyperslab (ie. a
    selection composing the entire current extent).  If STRIDE or BLOCK is
    NULL, they are assumed to be set to all '1'.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
hid_t
H5Scombine_hyperslab(hid_t space_id, H5S_seloper_t op, const hsize_t start[],
    const hsize_t stride[], const hsize_t count[], const hsize_t block[])
{
    H5S_t	*space;                 /* Dataspace to modify selection of */
    H5S_t	*new_space = NULL;      /* New dataspace created */
    hid_t	ret_value;              /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE6("i", "iSs*h*h*h*h", space_id, op, start, stride, count, block);

    /* Check args */
    if(NULL == (space = (H5S_t *)H5I_object_verify(space_id, H5I_DATASPACE)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a dataspace")
    if(start == NULL || count == NULL)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "hyperslab not specified")
    if(!(op >= H5S_SELECT_SET && op <= H5S_SELECT_NOTA))
        HGOTO_ERROR(H5E_ARGS, H5E_UNSUPPORTED, FAIL, "invalid selection operation")

    /* Generate new space, with combination of selections */
    if(H5S_fill_in_hyperslab(space, op, start, stride, count, block, &new_space) < 0)
        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINIT, FAIL, "unable to set hyperslab selection")

    /* Atomize */
    if((ret_value = H5I_register(H5I_DATASPACE, new_space, TRUE)) < 0)
        HGOTO_ERROR(H5E_ATOM, H5E_CANTREGISTER, FAIL, "unable to register dataspace atom")

done:
    if(ret_value < 0 && new_space)
        H5S_close(new_space);

    FUNC_LEAVE_API(ret_value)
} /* end H5Scombine_hyperslab() */


/*-------------------------------------------------------------------------
 * Function:	H5S_combine_select
 *
 * Purpose:	Internal version of H5Scombine_select().
 *
 * Return:	New dataspace on success/NULL on failure
 *
 * Programmer:	Quincey Koziol
 *              Tuesday, October 30, 2001
 *
 *              Chao Mei
 *              Wednesday, June 29, 2011
 *
 *-------------------------------------------------------------------------
 */
static H5S_t *
H5S_combine_select(H5S_t *space1, H5S_seloper_t op, H5S_t *space2)
{
    H5S_t *new_space = NULL;    /* New dataspace generated */
    H5S_t *ret_value = NULL;    /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Check args */
    HDassert(space1);
    HDassert(space2);
    HDassert(op >= H5S_SELECT_OR && op <= H5S_SELECT_NOTA);

    /* Check that if space1 selections has span trees */
    if(NULL == space1->select.sel_info.hslab->span_lst)
        if(H5S_hyper_generate_spans(space1) < 0)
            HGOTO_ERROR(H5E_DATASPACE, H5E_UNINITIALIZED, NULL, "dataspace does not have span tree")

    if(NULL == space2->select.sel_info.hslab->span_lst) {
        hsize_t tmp_start[H5S_MAX_RANK];
        hsize_t tmp_stride[H5S_MAX_RANK];
        hsize_t tmp_count[H5S_MAX_RANK];
        hsize_t tmp_block[H5S_MAX_RANK];
        unsigned u;

        for(u = 0; u < space2->extent.rank; u++) {
            tmp_start[u] = space2->select.sel_info.hslab->opt_diminfo[u].start;
            tmp_stride[u] = space2->select.sel_info.hslab->opt_diminfo[u].stride;
            tmp_count[u] = space2->select.sel_info.hslab->opt_diminfo[u].count;
            tmp_block[u] = space2->select.sel_info.hslab->opt_diminfo[u].block;
        } /* end for */

        /* call H5S_select_hyperslab directly */
        if(H5S_fill_in_hyperslab(space1, op, tmp_start, tmp_stride, tmp_count, tmp_block, &new_space) < 0)
            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINIT, NULL, "unable to set hyperslab selection")
    } /* end if */
    else{
        /* Combine new_space (a copy of space 1) & space2, with the result in new_space */
        if(H5S_fill_in_select(space1, op, space2, &new_space) < 0)
            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTCLIP, NULL, "can't clip hyperslab information")
    } /* end else */

    /* Set return value */
    ret_value = new_space;

done:
    if(ret_value == NULL && new_space)
        H5S_close(new_space);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_combine_select() */


/*--------------------------------------------------------------------------
 NAME
    H5Scombine_select
 PURPOSE
    Combine two hyperslab selections with an operation, returning a dataspace
    with the resulting selection.
 USAGE
    hid_t H5Scombine_select(space1, op, space2)
        hid_t space1;           IN: First Dataspace ID
        H5S_seloper_t op;       IN: Selection operation
        hid_t space2;           IN: Second Dataspace ID
 RETURNS
    Dataspace ID on success/Negative on failure
 DESCRIPTION
    Combine two existing hyperslab selections with an operation, returning
    a new dataspace with the resulting selection.  The dataspace extent from
    space1 is copied for the dataspace extent of the newly created dataspace.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
hid_t
H5Scombine_select(hid_t space1_id, H5S_seloper_t op, hid_t space2_id)
{
    H5S_t	*space1;                /* First Dataspace */
    H5S_t	*space2;                /* Second Dataspace */
    H5S_t	*new_space = NULL;      /* New Dataspace */
    hid_t	ret_value;              /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE3("i", "iSsi", space1_id, op, space2_id);

    /* Check args */
    if(NULL == (space1 = (H5S_t *)H5I_object_verify(space1_id, H5I_DATASPACE)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a dataspace")
    if(NULL == (space2 = (H5S_t *)H5I_object_verify(space2_id, H5I_DATASPACE)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a dataspace")
    if(!(op >= H5S_SELECT_OR && op <= H5S_SELECT_NOTA))
        HGOTO_ERROR(H5E_ARGS, H5E_UNSUPPORTED, FAIL, "invalid selection operation")

    /* Check that both dataspaces have the same rank */
    if(space1->extent.rank != space2->extent.rank)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "dataspaces not same rank")

    /* Note: currently, the offset of each dataspace is ignored */
#if 0
    /* Check that both dataspaces have the same offset */
    /* Same note as in H5Smodify_select */
    for(u=0; u<space1->extent.rank; u++) {
        if(space1->select.offset[u] != space2->select.offset[u])
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "dataspaces not same offset")
    } /* end for */
#endif

    /* Check that both dataspaces have hyperslab selections */
    if(H5S_GET_SELECT_TYPE(space1) != H5S_SEL_HYPERSLABS || H5S_GET_SELECT_TYPE(space2) != H5S_SEL_HYPERSLABS)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "dataspaces don't have hyperslab selections")

    /* Go combine the dataspaces */
    if(NULL == (new_space = H5S_combine_select(space1, op, space2)))
        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINIT, FAIL, "unable to create hyperslab selection")

    /* Atomize */
    if((ret_value = H5I_register(H5I_DATASPACE, new_space, TRUE)) < 0)
        HGOTO_ERROR(H5E_ATOM, H5E_CANTREGISTER, FAIL, "unable to register dataspace atom")

done:
    if(ret_value < 0 && new_space)
        H5S_close(new_space);

    FUNC_LEAVE_API(ret_value)
} /* end H5Scombine_select() */


/*-------------------------------------------------------------------------
 * Function:	H5S_modify_select
 *
 * Purpose:	Internal version of H5Smodify_select().
 *
 * Return:	New dataspace on success/NULL on failure
 *
 * Programmer:	Quincey Koziol
 *              Tuesday, October 30, 2001
 *
 *              Chao Mei
 *              Wednesday, June 29, 2011
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5S_modify_select(H5S_t *space1, H5S_seloper_t op, H5S_t *space2)
{
    herr_t      ret_value = SUCCEED;       /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Check args */
    HDassert(space1);
    HDassert(space2);
    HDassert(op >= H5S_SELECT_OR && op <= H5S_SELECT_NOTA);

    /* Check that the space selections both have span trees */
    if(NULL == space1->select.sel_info.hslab->span_lst)
        if(H5S_hyper_generate_spans(space1) < 0)
            HGOTO_ERROR(H5E_DATASPACE, H5E_UNINITIALIZED, FAIL, "dataspace does not have span tree")

    if(NULL == space2->select.sel_info.hslab->span_lst) {
        hsize_t tmp_start[H5S_MAX_RANK];
        hsize_t tmp_stride[H5S_MAX_RANK];
        hsize_t tmp_count[H5S_MAX_RANK];
        hsize_t tmp_block[H5S_MAX_RANK];
        unsigned u;

        for(u = 0; u < space2->extent.rank; u++) {
            tmp_start[u] = space2->select.sel_info.hslab->opt_diminfo[u].start;
            tmp_stride[u] = space2->select.sel_info.hslab->opt_diminfo[u].stride;
            tmp_count[u] = space2->select.sel_info.hslab->opt_diminfo[u].count;
            tmp_block[u] = space2->select.sel_info.hslab->opt_diminfo[u].block;
        } /* end for */

        /* Call H5S_select_hyperslab directly */
        if(H5S_select_hyperslab(space1, op, tmp_start, tmp_stride, tmp_count, tmp_block) < 0)
            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINIT, FAIL, "unable to set hyperslab selection")
    } /* end if */
    else {
        /* Combine spans from space1 & spans from space2, with the result in space1 */
        if(H5S_fill_in_select(space1, op, space2, &space1) < 0)
            HGOTO_ERROR(H5E_DATASPACE, H5E_CANTCLIP, FAIL, "can't perform operation on two selections")
    } /* end else*/

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_modify_select() */


/*--------------------------------------------------------------------------
 NAME
    H5Smodify_select
 PURPOSE
    Refine a hyperslab selection with an operation using a second hyperslab
    to modify it
 USAGE
    herr_t H5Smodify_select(space1, op, space2)
        hid_t space1;           IN/OUT: First Dataspace ID
        H5S_seloper_t op;       IN: Selection operation
        hid_t space2;           IN: Second Dataspace ID
 RETURNS
    Non-negative on success/Negative on failure
 DESCRIPTION
    Refine an existing hyperslab selection with an operation, using a second
    hyperslab.  The first selection is modified to contain the result of
    space1 operated on by space2.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
herr_t
H5Smodify_select(hid_t space1_id, H5S_seloper_t op, hid_t space2_id)
{
    H5S_t	*space1;                /* First Dataspace */
    H5S_t	*space2;                /* Second Dataspace */
    herr_t      ret_value = SUCCEED;    /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE3("e", "iSsi", space1_id, op, space2_id);

    /* Check args */
    if(NULL == (space1 = (H5S_t *)H5I_object_verify(space1_id, H5I_DATASPACE)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a dataspace")
    if(NULL == (space2 = (H5S_t *)H5I_object_verify(space2_id, H5I_DATASPACE)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a dataspace")
    if(!(op >= H5S_SELECT_OR && op <= H5S_SELECT_NOTA))
        HGOTO_ERROR(H5E_ARGS, H5E_UNSUPPORTED, FAIL, "invalid selection operation")

    /* Check that both dataspaces have the same rank */
    if(space1->extent.rank != space2->extent.rank)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "dataspaces not same rank")

    /* Check that both dataspaces have the same offset */
    /** Note that this is a tricky part of this function. It's
     *  possible that two dataspaces have different "offset". If the
     *  space2 has smaller offset value than that of space1 in a
     *  dimension, then the span elements of this dimension in
     *  space2 could have negative "low" and "high" values relative
     *  to the offset in space1. In other words, if the bounds of
     *  span elements in space2 are adjusted relative to the offset
     *  in space1, then every span element's bound is computed as
     *  "origin_bound+offset2-offset1". Therefore, if offset2 (the
     *  offset of space2) is smaller, then
     *  "origin_bound+offset2-offset1" could be negative which is
     *  not allowed by the bound type declaration as hsize_t!
     *  As a result, if the op is an OR selection, then the final
     *  result may contain span elements that have negative bound!
     *  So right now, the difference in the offset is totally
     *  ignored!!
     */
#if 0
    for(u=0; u<space1->extent.rank; u++) {
        if(space1->select.offset[u] != space2->select.offset[u])
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "dataspaces not same offset")
    } /* end for */
#endif

    /* Check that both dataspaces have hyperslab selections */
    if(H5S_GET_SELECT_TYPE(space1) != H5S_SEL_HYPERSLABS || H5S_GET_SELECT_TYPE(space2) != H5S_SEL_HYPERSLABS)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "dataspaces don't have hyperslab selections")

    /* Go refine the first selection */
    if(H5S_modify_select(space1, op, space2) < 0)
        HGOTO_ERROR(H5E_DATASPACE, H5E_CANTINIT, FAIL, "unable to modify hyperslab selection")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Smodify_select() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_get_seq_list_gen
 PURPOSE
    Create a list of offsets & lengths for a selection
 USAGE
    herr_t H5S_select_hyper_get_file_list_gen(space,iter,maxseq,maxelem,nseq,nelem,off,len)
        H5S_t *space;           IN: Dataspace containing selection to use.
        H5S_sel_iter_t *iter;   IN/OUT: Selection iterator describing last
                                    position of interest in selection.
        size_t maxseq;          IN: Maximum number of sequences to generate
        size_t maxelem;         IN: Maximum number of elements to include in the
                                    generated sequences
        size_t *nseq;           OUT: Actual number of sequences generated
        size_t *nelem;          OUT: Actual number of elements in sequences generated
        hsize_t *off;           OUT: Array of offsets
        size_t *len;            OUT: Array of lengths
 RETURNS
    Non-negative on success/Negative on failure.
 DESCRIPTION
    Use the selection in the dataspace to generate a list of byte offsets and
    lengths for the region(s) selected.  Start/Restart from the position in the
    ITER parameter.  The number of sequences generated is limited by the MAXSEQ
    parameter and the number of sequences actually generated is stored in the
    NSEQ parameter.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_get_seq_list_gen(const H5S_t *space, H5S_sel_iter_t *iter,
    size_t maxseq, size_t maxelem, size_t *nseq, size_t *nelem,
    hsize_t *off, size_t *len)
{
    H5S_hyper_span_t *curr_span;    /* Current hyperslab span node */
    H5S_hyper_span_t **ispan;       /* Iterator's hyperslab span nodes */
    hsize_t slab[H5O_LAYOUT_NDIMS]; /* Cumulative size of each dimension in bytes */
    hsize_t acc;       /* Accumulator for computing cumulative sizes */
    hsize_t loc_off;   /* Element offset in the dataspace */
    hsize_t last_span_end = 0; /* The offset of the end of the last span */
    hsize_t *abs_arr;  /* Absolute hyperslab span position */
    const hssize_t *off_arr;  /* Offset within the dataspace extent */
    size_t span_size = 0; /* Number of bytes in current span to actually process */
    size_t io_left;    /* Number of elements left to process */
    size_t io_bytes_left;   /* Number of bytes left to process */
    size_t io_used;    /* Number of elements processed */
    size_t curr_seq = 0; /* Number of sequence/offsets stored in the arrays */
    size_t elem_size;  /* Size of each element iterating over */
    unsigned ndims;    /* Number of dimensions of dataset */
    unsigned fast_dim; /* Rank of the fastest changing dimension for the dataspace */
    int curr_dim;      /* Current dimension being operated on */
    unsigned u;        /* Index variable */
    int i;             /* Index variable */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Check args */
    HDassert(space);
    HDassert(iter);
    HDassert(maxseq > 0);
    HDassert(maxelem > 0);
    HDassert(nseq);
    HDassert(nelem);
    HDassert(off);
    HDassert(len);

    /* Set the rank of the fastest changing dimension */
    ndims = space->extent.rank;
    fast_dim = (ndims - 1);

    /* Get the pointers to the current span info and span nodes */
    curr_span = iter->u.hyp.span[fast_dim];
    abs_arr = iter->u.hyp.off;
    off_arr = space->select.offset;
    ispan = iter->u.hyp.span;
    elem_size = iter->elmt_size;

    /* Set the amount of elements to perform I/O on, etc. */
    H5_CHECK_OVERFLOW(iter->elmt_left, hsize_t, size_t);
    io_left = MIN(maxelem, (size_t)iter->elmt_left);
    io_bytes_left = io_left * elem_size;

    /* Compute the cumulative size of dataspace dimensions */
    for(i = (int)fast_dim, acc = elem_size; i >= 0; i--) {
        slab[i] = acc;
        acc *= space->extent.size[i];
    } /* end for */

    /* Set the offset of the first element iterated on */
    for(u = 0, loc_off = 0; u < ndims; u++)
        /* Compute the sequential element offset */
        loc_off += ((hsize_t)((hssize_t)abs_arr[u] + off_arr[u])) * slab[u];

    /* Range check against number of elements left in selection */
    HDassert(io_bytes_left <= (iter->elmt_left * elem_size));

    /* Take care of any partial spans leftover from previous I/Os */
    if(abs_arr[fast_dim]!=curr_span->low) {

        /* Finish the span in the fastest changing dimension */

        /* Compute the number of bytes to attempt in this span */
        H5_ASSIGN_OVERFLOW(span_size,((curr_span->high-abs_arr[fast_dim])+1)*elem_size,hsize_t,size_t);

        /* Check number of bytes against upper bounds allowed */
        if(span_size>io_bytes_left)
            span_size=io_bytes_left;

        /* Add the partial span to the list of sequences */
        off[curr_seq]=loc_off;
        len[curr_seq]=span_size;

        /* Increment sequence count */
        curr_seq++;

        /* Set the location of the last span's end */
        last_span_end=loc_off+span_size;

        /* Decrement I/O left to perform */
        io_bytes_left-=span_size;

        /* Advance the hyperslab iterator */
        /* Check if we are done */
        if(io_bytes_left > 0) {
            /* Move to next span in fastest changing dimension */
            curr_span = curr_span->next;

            if(NULL != curr_span) {
                /* Move location offset of destination */
                loc_off += (curr_span->low - abs_arr[fast_dim]) * elem_size;

                /* Move iterator for fastest changing dimension */
                abs_arr[fast_dim] = curr_span->low;
            } /* end if */
        } /* end if */
        else {
            abs_arr[fast_dim] += span_size / elem_size;

            /* Check if we are still within the span */
            if(abs_arr[fast_dim] <= curr_span->high) {
                iter->u.hyp.span[fast_dim] = curr_span;
            } /* end if */
            /* If we walked off that span, advance to the next span */
            else {
                /* Advance span in this dimension */
                curr_span = curr_span->next;

                /* Check if we have a valid span in this dimension still */
                if(NULL != curr_span) {
                    /* Reset absolute position */
                    abs_arr[fast_dim] = curr_span->low;
                    iter->u.hyp.span[fast_dim] = curr_span;
                } /* end if */
            } /* end else */
        } /* end else */

        /* Adjust iterator pointers */

        if(NULL == curr_span) {
/* Same as code in main loop */
            /* Start at the next fastest dim */
            curr_dim = (int)(fast_dim - 1);

            /* Work back up through the dimensions */
            while(curr_dim >= 0) {
                /* Reset the current span */
                curr_span = iter->u.hyp.span[curr_dim];

                /* Increment absolute position */
                abs_arr[curr_dim]++;

                /* Check if we are still within the span */
                if(abs_arr[curr_dim] <= curr_span->high) {
                    break;
                } /* end if */
                /* If we walked off that span, advance to the next span */
                else {
                    /* Advance span in this dimension */
                    curr_span = curr_span->next;

                    /* Check if we have a valid span in this dimension still */
                    if(NULL != curr_span) {
                        /* Reset the span in the current dimension */
                        ispan[curr_dim] = curr_span;

                        /* Reset absolute position */
                        abs_arr[curr_dim] = curr_span->low;

                        break;
                    } /* end if */
                    else {
                        /* If we finished the span list in this dimension, decrement the dimension worked on and loop again */
                        curr_dim--;
                    } /* end else */
                } /* end else */
            } /* end while */

            /* Check if we have more spans in the tree */
            if(curr_dim >= 0) {
                /* Walk back down the iterator positions, reseting them */
                while((unsigned)curr_dim < fast_dim) {
                    HDassert(curr_span);
                    HDassert(curr_span->down);
                    HDassert(curr_span->down->head);

                    /* Increment current dimension */
                    curr_dim++;

                    /* Set the new span_info & span for this dimension */
                    iter->u.hyp.span[curr_dim] = curr_span->down->head;

                    /* Advance span down the tree */
                    curr_span = curr_span->down->head;

                    /* Reset the absolute offset for the dim */
                    abs_arr[curr_dim] = curr_span->low;
                } /* end while */

                /* Verify that the curr_span points to the fastest dim */
                HDassert(curr_span == iter->u.hyp.span[fast_dim]);

                /* Reset the buffer offset */
                for(u = 0, loc_off = 0; u < ndims; u++)
                    loc_off += ((hsize_t)((hssize_t)abs_arr[u] + off_arr[u])) * slab[u];
            } /* end else */
            else
                /* We had better be done with I/O or bad things are going to happen... */
                HDassert(io_bytes_left == 0);
        } /* end if */
    } /* end if */

    /* Perform the I/O on the elements, based on the position of the iterator */
    while(io_bytes_left > 0 && curr_seq < maxseq) {
        /* Sanity check */
        HDassert(curr_span);

        /* Adjust location offset of destination to compensate for initial increment below */
        loc_off -= curr_span->pstride;

        /* Loop over all the spans in the fastest changing dimension */
        while(curr_span != NULL) {
            /* Move location offset of destination */
            loc_off += curr_span->pstride;

            /* Compute the number of elements to attempt in this span */
            H5_ASSIGN_OVERFLOW(span_size, curr_span->nelem, hsize_t, size_t);

            /* Check number of elements against upper bounds allowed */
            if(span_size >= io_bytes_left) {
                /* Trim the number of bytes to output */
                span_size = io_bytes_left;
                io_bytes_left = 0;

/* COMMON */
                /* Store the I/O information for the span */

                /* Check if this is appending onto previous sequence */
                if(curr_seq > 0 && last_span_end == loc_off)
                    len[curr_seq - 1] += span_size;
                else {
                    off[curr_seq] = loc_off;
                    len[curr_seq] = span_size;

                    /* Increment the number of sequences in arrays */
                    curr_seq++;
                } /* end else */

                /* Set the location of the last span's end */
                last_span_end = loc_off + span_size;
/* end COMMON */

                /* Break out now, we are finished with I/O */
                break;
            } /* end if */
            else {
                /* Decrement I/O left to perform */
                io_bytes_left -= span_size;

/* COMMON */
                /* Store the I/O information for the span */

                /* Check if this is appending onto previous sequence */
                if(curr_seq > 0 && last_span_end == loc_off)
                    len[curr_seq-1]+=span_size;
                else {
                    off[curr_seq] = loc_off;
                    len[curr_seq] = span_size;

                    /* Increment the number of sequences in arrays */
                    curr_seq++;
                } /* end else */

                /* Set the location of the last span's end */
                last_span_end = loc_off + span_size;
/* end COMMON */

                /* If the sequence & offset arrays are full, do what? */
                if(curr_seq >= maxseq) {
                    /* Break out now, we are finished with sequences */
                    break;
                } /* end else */
            } /* end else */

	    /* Move to next span in fastest changing dimension */
	    curr_span=curr_span->next;
        } /* end while */

        /* Check if we are done */
        if(io_bytes_left==0 || curr_seq>=maxseq) {
            HDassert(curr_span);
            abs_arr[fast_dim]=curr_span->low+(span_size/elem_size);

            /* Check if we are still within the span */
            if(abs_arr[fast_dim]<=curr_span->high) {
                iter->u.hyp.span[fast_dim]=curr_span;
                break;
            } /* end if */
            /* If we walked off that span, advance to the next span */
            else {
                /* Advance span in this dimension */
                curr_span=curr_span->next;

                /* Check if we have a valid span in this dimension still */
                if(curr_span!=NULL) {
                    /* Reset absolute position */
                    abs_arr[fast_dim]=curr_span->low;
                    iter->u.hyp.span[fast_dim]=curr_span;
                    break;
                } /* end if */
            } /* end else */
        } /* end if */

        /* Adjust iterator pointers */

        /* Start at the next fastest dim */
        curr_dim = (int)(fast_dim - 1);

        /* Work back up through the dimensions */
        while(curr_dim >= 0) {
            /* Reset the current span */
	    curr_span=iter->u.hyp.span[curr_dim];

            /* Increment absolute position */
            abs_arr[curr_dim]++;

            /* Check if we are still within the span */
            if(abs_arr[curr_dim]<=curr_span->high) {
                break;
            } /* end if */
            /* If we walked off that span, advance to the next span */
            else {
                /* Advance span in this dimension */
                curr_span=curr_span->next;

                /* Check if we have a valid span in this dimension still */
                if(curr_span!=NULL) {
                    /* Reset the span in the current dimension */
                    ispan[curr_dim]=curr_span;

                    /* Reset absolute position */
                    abs_arr[curr_dim]=curr_span->low;

                    break;
                } /* end if */
                else {
                    /* If we finished the span list in this dimension, decrement the dimension worked on and loop again */
                    curr_dim--;
                } /* end else */
            } /* end else */
        } /* end while */

        /* Check if we are finished with the spans in the tree */
        if(curr_dim < 0) {
            /* We had better be done with I/O or bad things are going to happen... */
            HDassert(io_bytes_left == 0);
            break;
        } /* end if */
        else {
            /* Walk back down the iterator positions, reseting them */
            while((unsigned)curr_dim < fast_dim) {
                HDassert(curr_span);
                HDassert(curr_span->down);
                HDassert(curr_span->down->head);

                /* Increment current dimension to the next dimension down */
                curr_dim++;

                /* Set the new span for the next dimension down */
                iter->u.hyp.span[curr_dim] = curr_span->down->head;

                /* Advance span down the tree */
                curr_span = curr_span->down->head;

                /* Reset the absolute offset for the dim */
                abs_arr[curr_dim] = curr_span->low;
            } /* end while */

            /* Verify that the curr_span points to the fastest dim */
            HDassert(curr_span == iter->u.hyp.span[fast_dim]);
        } /* end else */

        /* Reset the buffer offset */
        for(u = 0, loc_off = 0; u < ndims; u++)
            loc_off += ((hsize_t)((hssize_t)abs_arr[u] + off_arr[u])) * slab[u];
    } /* end while */

    /* Decrement number of elements left in iterator */
    io_used = (io_left - (io_bytes_left / elem_size));
    iter->elmt_left -= io_used;

    /* Set the number of sequences generated */
    *nseq = curr_seq;

    /* Set the number of elements used */
    *nelem = io_used;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5S_hyper_get_seq_list_gen() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_get_seq_list_opt
 PURPOSE
    Create a list of offsets & lengths for a selection
 USAGE
    herr_t H5S_select_hyper_get_file_list_opt(space,iter,maxseq,maxelem,nseq,nelem,off,len)
        H5S_t *space;           IN: Dataspace containing selection to use.
        H5S_sel_iter_t *iter;   IN/OUT: Selection iterator describing last
                                    position of interest in selection.
        size_t maxseq;          IN: Maximum number of sequences to generate
        size_t maxelem;         IN: Maximum number of elements to include in the
                                    generated sequences
        size_t *nseq;           OUT: Actual number of sequences generated
        size_t *nelem;          OUT: Actual number of elements in sequences generated
        hsize_t *off;           OUT: Array of offsets
        size_t *len;            OUT: Array of lengths
 RETURNS
    Non-negative on success/Negative on failure.
 DESCRIPTION
    Use the selection in the dataspace to generate a list of byte offsets and
    lengths for the region(s) selected.  Start/Restart from the position in the
    ITER parameter.  The number of sequences generated is limited by the MAXSEQ
    parameter and the number of sequences actually generated is stored in the
    NSEQ parameter.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_get_seq_list_opt(const H5S_t *space, H5S_sel_iter_t *iter,
    size_t maxseq, size_t maxelem, size_t *nseq, size_t *nelem,
    hsize_t *off, size_t *len)
{
    hsize_t *mem_size;                  /* Size of the source buffer */
    hsize_t slab[H5O_LAYOUT_NDIMS];     /* Hyperslab size */
    const hssize_t *sel_off;            /* Selection offset in dataspace */
    hsize_t offset[H5O_LAYOUT_NDIMS];   /* Coordinate offset in dataspace */
    hsize_t tmp_count[H5O_LAYOUT_NDIMS];/* Temporary block count */
    hsize_t tmp_block[H5O_LAYOUT_NDIMS];/* Temporary block offset */
    hsize_t wrap[H5O_LAYOUT_NDIMS];     /* Bytes to wrap around at the end of a row */
    hsize_t skip[H5O_LAYOUT_NDIMS];     /* Bytes to skip between blocks */
    const H5S_hyper_dim_t *tdiminfo;    /* Temporary pointer to diminfo information */
    hsize_t fast_dim_start,    /* Local copies of fastest changing dimension info */
        fast_dim_stride,
        fast_dim_block,
        fast_dim_offset;
    size_t fast_dim_buf_off;    /* Local copy of amount to move fastest dimension buffer offset */
    size_t fast_dim_count;      /* Number of blocks left in fastest changing dimension */
    size_t tot_blk_count;       /* Total number of blocks left to output */
    size_t act_blk_count;       /* Actual number of blocks to output */
    size_t total_rows;          /* Total number of entire rows to output */
    size_t curr_rows;           /* Current number of entire rows to output */
    unsigned fast_dim;  /* Rank of the fastest changing dimension for the dataspace */
    unsigned ndims;     /* Number of dimensions of dataset */
    int temp_dim;       /* Temporary rank holder */
    hsize_t acc;	/* Accumulator */
    hsize_t loc;        /* Coordinate offset */
    size_t curr_seq = 0; /* Current sequence being operated on */
    size_t actual_elem; /* The actual number of elements to count */
    size_t actual_bytes;/* The actual number of bytes to copy */
    size_t io_left;     /* The number of elements left in I/O operation */
    size_t start_io_left; /* The initial number of elements left in I/O operation */
    size_t elem_size;   /* Size of each element iterating over */
    unsigned u;         /* Local index variable */
    int i;              /* Local index variable */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Check args */
    HDassert(space);
    HDassert(iter);
    HDassert(maxseq > 0);
    HDassert(maxelem > 0);
    HDassert(nseq);
    HDassert(nelem);
    HDassert(off);
    HDassert(len);

    /* Set the local copy of the diminfo pointer */
    tdiminfo = iter->u.hyp.diminfo;

    /* Check if this is a "flattened" regular hyperslab selection */
    if(iter->u.hyp.iter_rank != 0 && iter->u.hyp.iter_rank < space->extent.rank) {
        /* Set the aliases for a few important dimension ranks */
        ndims = iter->u.hyp.iter_rank;
        fast_dim = ndims - 1;

        /* Set the local copy of the selection offset */
        sel_off = iter->u.hyp.sel_off;

        /* Set up the pointer to the size of the memory space */
        mem_size = iter->u.hyp.size;
    } /* end if */
    else {
        /* Set the aliases for a few important dimension ranks */
        ndims = space->extent.rank;
        fast_dim = ndims - 1;

        /* Set the local copy of the selection offset */
        sel_off = space->select.offset;

        /* Set up the pointer to the size of the memory space */
        mem_size = space->extent.size;
    } /* end else */

    /* initialize row sizes for each dimension */
    elem_size = iter->elmt_size;
    for(i = (int)fast_dim, acc = elem_size; i >= 0; i--) {
        slab[i] = acc;
        acc *= mem_size[i];
    } /* end for */

    /* Calculate the number of elements to sequence through */
    H5_CHECK_OVERFLOW(iter->elmt_left, hsize_t, size_t);
    io_left = MIN((size_t)iter->elmt_left, maxelem);

    /* Sanity check that there aren't any "remainder" sequences in process */
    HDassert(!((iter->u.hyp.off[fast_dim] - tdiminfo[fast_dim].start) % tdiminfo[fast_dim].stride != 0 ||
            ((iter->u.hyp.off[fast_dim] != tdiminfo[fast_dim].start) && tdiminfo[fast_dim].count == 1)));

    /* We've cleared the "remainder" of the previous fastest dimension
     * sequence before calling this routine, so we must be at the beginning of
     * a sequence.  Use the fancy algorithm to compute the offsets and run
     * through as many as possible, until the buffer fills up.
     */

    /* Keep the number of elements we started with */
    start_io_left = io_left;

    /* Compute the arrays to perform I/O on */

    /* Copy the location of the point to get */
    /* (Add in the selection offset) */
    for(u = 0; u < ndims; u++)
        offset[u] = (hsize_t)((hssize_t)iter->u.hyp.off[u] + sel_off[u]);

    /* Compute the current "counts" for this location */
    for(u = 0; u < ndims; u++) {
        if(tdiminfo[u].count == 1) {
            tmp_count[u] = 0;
            tmp_block[u] = iter->u.hyp.off[u] - tdiminfo[u].start;
        } /* end if */
        else {
            tmp_count[u] = (iter->u.hyp.off[u] - tdiminfo[u].start) / tdiminfo[u].stride;
            tmp_block[u] = (iter->u.hyp.off[u] - tdiminfo[u].start) % tdiminfo[u].stride;
        } /* end else */
    } /* end for */

    /* Compute the initial buffer offset */
    for(u = 0, loc = 0; u < ndims; u++)
        loc += offset[u] * slab[u];

    /* Set the number of elements to write each time */
    H5_ASSIGN_OVERFLOW(actual_elem, tdiminfo[fast_dim].block, hsize_t, size_t);

    /* Set the number of actual bytes */
    actual_bytes = actual_elem * elem_size;

    /* Set local copies of information for the fastest changing dimension */
    fast_dim_start = tdiminfo[fast_dim].start;
    fast_dim_stride = tdiminfo[fast_dim].stride;
    fast_dim_block = tdiminfo[fast_dim].block;
    H5_ASSIGN_OVERFLOW(fast_dim_buf_off, slab[fast_dim] * fast_dim_stride, hsize_t, size_t);
    fast_dim_offset = (hsize_t)((hssize_t)fast_dim_start + sel_off[fast_dim]);

    /* Compute the number of blocks which would fit into the buffer */
    H5_CHECK_OVERFLOW(io_left / fast_dim_block, hsize_t, size_t);
    tot_blk_count = (size_t)(io_left / fast_dim_block);

    /* Don't go over the maximum number of sequences allowed */
    tot_blk_count = MIN(tot_blk_count, (maxseq - curr_seq));

    /* Compute the amount to wrap at the end of each row */
    for(u = 0; u < ndims; u++)
        wrap[u] = (mem_size[u] - (tdiminfo[u].stride * tdiminfo[u].count)) * slab[u];

    /* Compute the amount to skip between blocks */
    for(u = 0; u < ndims; u++)
        skip[u] = (tdiminfo[u].stride - tdiminfo[u].block) * slab[u];

    /* Check if there is a partial row left (with full blocks) */
    if(tmp_count[fast_dim] > 0) {
        /* Get number of blocks in fastest dimension */
        H5_ASSIGN_OVERFLOW(fast_dim_count, tdiminfo[fast_dim].count - tmp_count[fast_dim], hsize_t, size_t);

        /* Make certain this entire row will fit into buffer */
        fast_dim_count = MIN(fast_dim_count, tot_blk_count);

        /* Number of blocks to sequence over */
        act_blk_count = fast_dim_count;

        /* Loop over all the blocks in the fastest changing dimension */
        while(fast_dim_count > 0) {
            /* Store the sequence information */
            off[curr_seq] = loc;
            len[curr_seq] = actual_bytes;

            /* Increment sequence count */
            curr_seq++;

            /* Increment information to reflect block just processed */
            loc += fast_dim_buf_off;

            /* Decrement number of blocks */
            fast_dim_count--;
        } /* end while */

        /* Decrement number of elements left */
        io_left -= actual_elem * act_blk_count;

        /* Decrement number of blocks left */
        tot_blk_count -= act_blk_count;

        /* Increment information to reflect block just processed */
        tmp_count[fast_dim] += act_blk_count;

        /* Check if we finished the entire row of blocks */
        if(tmp_count[fast_dim] >= tdiminfo[fast_dim].count) {
            /* Increment offset in destination buffer */
            loc += wrap[fast_dim];

            /* Increment information to reflect block just processed */
            offset[fast_dim] = fast_dim_offset;    /* reset the offset in the fastest dimension */
            tmp_count[fast_dim] = 0;

            /* Increment the offset and count for the other dimensions */
            temp_dim = (int)fast_dim - 1;
            while(temp_dim >= 0) {
                /* Move to the next row in the curent dimension */
                offset[temp_dim]++;
                tmp_block[temp_dim]++;

                /* If this block is still in the range of blocks to output for the dimension, break out of loop */
                if(tmp_block[temp_dim] < tdiminfo[temp_dim].block)
                    break;
                else {
                    /* Move to the next block in the current dimension */
                    offset[temp_dim] += (tdiminfo[temp_dim].stride - tdiminfo[temp_dim].block);
                    loc += skip[temp_dim];
                    tmp_block[temp_dim] = 0;
                    tmp_count[temp_dim]++;

                    /* If this block is still in the range of blocks to output for the dimension, break out of loop */
                    if(tmp_count[temp_dim] < tdiminfo[temp_dim].count)
                        break;
                    else {
                        offset[temp_dim] = (hsize_t)((hssize_t)tdiminfo[temp_dim].start + sel_off[temp_dim]);
                        loc += wrap[temp_dim];
                        tmp_count[temp_dim] = 0; /* reset back to the beginning of the line */
                        tmp_block[temp_dim] = 0;
                    } /* end else */
                } /* end else */

                /* Decrement dimension count */
                temp_dim--;
            } /* end while */
        } /* end if */
        else {
            /* Update the offset in the fastest dimension */
            offset[fast_dim] += (fast_dim_stride * act_blk_count);
        } /* end else */
    } /* end if */

    /* Compute the number of entire rows to read in */
    H5_CHECK_OVERFLOW(tot_blk_count / tdiminfo[fast_dim].count, hsize_t, size_t);
    curr_rows = total_rows = (size_t)(tot_blk_count / tdiminfo[fast_dim].count);

    /* Reset copy of number of blocks in fastest dimension */
    H5_ASSIGN_OVERFLOW(fast_dim_count, tdiminfo[fast_dim].count, hsize_t, size_t);

    /* Read in data until an entire sequence can't be written out any longer */
    while(curr_rows > 0) {

#define DUFF_GUTS							      \
/* Store the sequence information */				      \
off[curr_seq] = loc;						      \
len[curr_seq] = actual_bytes;					      \
                                                                          \
/* Increment sequence count */					      \
curr_seq++;								      \
                                                                          \
/* Increment information to reflect block just processed */		      \
loc += fast_dim_buf_off;

#ifdef NO_DUFFS_DEVICE
        /* Loop over all the blocks in the fastest changing dimension */
        while(fast_dim_count > 0) {
            DUFF_GUTS

            /* Decrement number of blocks */
            fast_dim_count--;
        } /* end while */
#else /* NO_DUFFS_DEVICE */
        {
            size_t duffs_index; /* Counting index for Duff's device */

            duffs_index = (fast_dim_count + 7) / 8;
            switch (fast_dim_count % 8) {
                default:
                    HDassert(0 && "This Should never be executed!");
                    break;
                case 0:
                    do
                      {
                        DUFF_GUTS
                case 7:
                        DUFF_GUTS
                case 6:
                        DUFF_GUTS
                case 5:
                        DUFF_GUTS
                case 4:
                        DUFF_GUTS
                case 3:
                        DUFF_GUTS
                case 2:
                        DUFF_GUTS
                case 1:
                        DUFF_GUTS
                  } while (--duffs_index > 0);
            } /* end switch */
        }
#endif /* NO_DUFFS_DEVICE */
#undef DUFF_GUTS

        /* Increment offset in destination buffer */
        loc += wrap[fast_dim];

        /* Increment the offset and count for the other dimensions */
        temp_dim = (int)fast_dim - 1;
        while(temp_dim >= 0) {
            /* Move to the next row in the curent dimension */
            offset[temp_dim]++;
            tmp_block[temp_dim]++;

            /* If this block is still in the range of blocks to output for the dimension, break out of loop */
            if(tmp_block[temp_dim] < tdiminfo[temp_dim].block)
                break;
            else {
                /* Move to the next block in the current dimension */
                offset[temp_dim] += (tdiminfo[temp_dim].stride - tdiminfo[temp_dim].block);
                loc += skip[temp_dim];
                tmp_block[temp_dim] = 0;
                tmp_count[temp_dim]++;

                /* If this block is still in the range of blocks to output for the dimension, break out of loop */
                if(tmp_count[temp_dim] < tdiminfo[temp_dim].count)
                    break;
                else {
                    offset[temp_dim] = (hsize_t)((hssize_t)tdiminfo[temp_dim].start + sel_off[temp_dim]);
                    loc += wrap[temp_dim];
                    tmp_count[temp_dim] = 0; /* reset back to the beginning of the line */
                    tmp_block[temp_dim] = 0;
                } /* end else */
            } /* end else */

            /* Decrement dimension count */
            temp_dim--;
        } /* end while */

        /* Decrement the number of rows left */
        curr_rows--;
    } /* end while */

    /* Adjust the number of blocks & elements left to transfer */

    /* Decrement number of elements left */
    H5_CHECK_OVERFLOW(actual_elem * (total_rows * tdiminfo[fast_dim].count), hsize_t, size_t);
    io_left -= (size_t)(actual_elem * (total_rows * tdiminfo[fast_dim].count));

    /* Decrement number of blocks left */
    H5_CHECK_OVERFLOW((total_rows * tdiminfo[fast_dim].count), hsize_t, size_t);
    tot_blk_count -= (size_t)(total_rows * tdiminfo[fast_dim].count);

    /* Read in partial row of blocks */
    if(io_left > 0 && curr_seq < maxseq) {
        /* Get remaining number of blocks left to output */
        fast_dim_count = tot_blk_count;

        /* Loop over all the blocks in the fastest changing dimension */
        while(fast_dim_count > 0) {
            /* Store the sequence information */
            off[curr_seq] = loc;
            len[curr_seq] = actual_bytes;

            /* Increment sequence count */
            curr_seq++;

            /* Increment information to reflect block just processed */
            loc += fast_dim_buf_off;

            /* Decrement number of blocks */
            fast_dim_count--;
        } /* end while */

        /* Decrement number of elements left */
        io_left -= actual_elem * tot_blk_count;

        /* Increment information to reflect block just processed */
        offset[fast_dim] += (fast_dim_stride * tot_blk_count);    /* move the offset in the fastest dimension */

        /* Handle any leftover, partial blocks in this row */
        if(io_left > 0 && curr_seq < maxseq) {
            actual_elem = io_left;
            actual_bytes = actual_elem * elem_size;

            /* Store the sequence information */
            off[curr_seq] = loc;
            len[curr_seq] = actual_bytes;

            /* Increment sequence count */
            curr_seq++;

            /* Decrement the number of elements left */
            io_left -= actual_elem;

            /* Increment buffer correctly */
            offset[fast_dim] += actual_elem;
        } /* end if */

        /* don't bother checking slower dimensions */
        HDassert(io_left == 0 || curr_seq == maxseq);
    } /* end if */

    /* Update the iterator */

    /* Update the iterator with the location we stopped */
    /* (Subtract out the selection offset) */
    for(u = 0; u < ndims; u++)
        iter->u.hyp.off[u] = (hsize_t)((hssize_t)offset[u] - sel_off[u]);

    /* Decrement the number of elements left in selection */
    iter->elmt_left -= (start_io_left - io_left);

    /* Increment the number of sequences generated */
    *nseq += curr_seq;

    /* Increment the number of elements used */
    *nelem += start_io_left - io_left;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5S_hyper_get_seq_list_opt() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_get_seq_list_single
 PURPOSE
    Create a list of offsets & lengths for a selection
 USAGE
    herr_t H5S_hyper_get_seq_list_single(space, flags, iter, maxseq, maxelem, nseq, nelem, off, len)
        H5S_t *space;           IN: Dataspace containing selection to use.
        unsigned flags;         IN: Flags for extra information about operation
        H5S_sel_iter_t *iter;   IN/OUT: Selection iterator describing last
                                    position of interest in selection.
        size_t maxseq;          IN: Maximum number of sequences to generate
        size_t maxelem;         IN: Maximum number of elements to include in the
                                    generated sequences
        size_t *nseq;           OUT: Actual number of sequences generated
        size_t *nelem;          OUT: Actual number of elements in sequences generated
        hsize_t *off;           OUT: Array of offsets
        size_t *len;            OUT: Array of lengths
 RETURNS
    Non-negative on success/Negative on failure.
 DESCRIPTION
    Use the selection in the dataspace to generate a list of byte offsets and
    lengths for the region(s) selected.  Start/Restart from the position in the
    ITER parameter.  The number of sequences generated is limited by the MAXSEQ
    parameter and the number of sequences actually generated is stored in the
    NSEQ parameter.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_get_seq_list_single(const H5S_t *space, H5S_sel_iter_t *iter,
    size_t maxseq, size_t maxelem, size_t *nseq, size_t *nelem,
    hsize_t *off, size_t *len)
{
    const H5S_hyper_dim_t *tdiminfo;    /* Temporary pointer to diminfo information */
    const hssize_t *sel_off;    /* Selection offset in dataspace */
    hsize_t *mem_size;      /* Size of the source buffer */
    hsize_t base_offset[H5O_LAYOUT_NDIMS];   /* Base coordinate offset in dataspace */
    hsize_t offset[H5O_LAYOUT_NDIMS];   /* Coordinate offset in dataspace */
    hsize_t slab[H5O_LAYOUT_NDIMS];     /* Hyperslab size */
    hsize_t fast_dim_block;     /* Local copies of fastest changing dimension info */
    hsize_t acc;	        /* Accumulator */
    hsize_t loc;                /* Coordinate offset */
    size_t tot_blk_count;       /* Total number of blocks left to output */
    size_t elem_size;           /* Size of each element iterating over */
    size_t io_left;             /* The number of elements left in I/O operation */
    size_t actual_elem;         /* The actual number of elements to count */
    unsigned ndims;             /* Number of dimensions of dataset */
    unsigned fast_dim;          /* Rank of the fastest changing dimension for the dataspace */
    unsigned skip_dim;          /* Rank of the dimension to skip along */
    unsigned u;                 /* Local index variable */
    int i;                      /* Local index variable */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Check args */
    HDassert(space);
    HDassert(iter);
    HDassert(maxseq > 0);
    HDassert(maxelem > 0);
    HDassert(nseq);
    HDassert(nelem);
    HDassert(off);
    HDassert(len);

    /* Set a local copy of the diminfo pointer */
    tdiminfo = iter->u.hyp.diminfo;

    /* Check if this is a "flattened" regular hyperslab selection */
    if(iter->u.hyp.iter_rank != 0 && iter->u.hyp.iter_rank < space->extent.rank) {
        /* Set the aliases for a few important dimension ranks */
        ndims = iter->u.hyp.iter_rank;

        /* Set the local copy of the selection offset */
        sel_off = iter->u.hyp.sel_off;

        /* Set up the pointer to the size of the memory space */
        mem_size = iter->u.hyp.size;
    } /* end if */
    else {
        /* Set the aliases for a few important dimension ranks */
        ndims = space->extent.rank;

        /* Set the local copy of the selection offset */
        sel_off = space->select.offset;

        /* Set up the pointer to the size of the memory space */
        mem_size = space->extent.size;
    } /* end else */
    fast_dim = ndims - 1;

    /* initialize row sizes for each dimension */
    elem_size = iter->elmt_size;
    for(i = (int)fast_dim, acc = elem_size; i >= 0; i--) {
        slab[i] = acc;
        acc *= mem_size[i];
    } /* end for */

    /* Copy the base location of the block */
    /* (Add in the selection offset) */
    for(u = 0; u < ndims; u++)
        base_offset[u] = (hsize_t)((hssize_t)tdiminfo[u].start + sel_off[u]);

    /* Copy the location of the point to get */
    /* (Add in the selection offset) */
    for(u = 0; u < ndims; u++)
        offset[u] = (hsize_t)((hssize_t)iter->u.hyp.off[u] + sel_off[u]);

    /* Compute the initial buffer offset */
    for(u = 0, loc = 0; u < ndims; u++)
        loc += offset[u] * slab[u];

    /* Set local copies of information for the fastest changing dimension */
    fast_dim_block = tdiminfo[fast_dim].block;

    /* Calculate the number of elements to sequence through */
    H5_CHECK_OVERFLOW(iter->elmt_left, hsize_t, size_t);
    io_left = MIN((size_t)iter->elmt_left, maxelem);

    /* Compute the number of blocks which would fit into the buffer */
    H5_CHECK_OVERFLOW(io_left / fast_dim_block, hsize_t, size_t);
    tot_blk_count = (size_t)(io_left / fast_dim_block);

    /* Don't go over the maximum number of sequences allowed */
    tot_blk_count = MIN(tot_blk_count, maxseq);

    /* Set the number of elements to write each time */
    H5_ASSIGN_OVERFLOW(actual_elem, fast_dim_block, hsize_t, size_t);

    /* Check for blocks to operate on */
    if(tot_blk_count > 0) {
        size_t actual_bytes;        /* The actual number of bytes to copy */

        /* Set the number of actual bytes */
        actual_bytes = actual_elem * elem_size;

        /* Check for 1-dim selection */
        if(0 == fast_dim) {
            /* Sanity checks */
            HDassert(1 == tot_blk_count);
            HDassert(io_left == actual_elem);

            /* Store the sequence information */
            *off++ = loc;
            *len++ = actual_bytes;
        } /* end if */
        else {
            hsize_t skip_slab;          /* Temporary copy of slab[fast_dim - 1] */
            size_t blk_count;           /* Total number of blocks left to output */

            /* Find first dimension w/block >1 */
            skip_dim = fast_dim;
            for(i = (int)(fast_dim - 1); i >= 0; i--)
                if(tdiminfo[i].block > 1) {
                    skip_dim = (unsigned)i;
                    break;
                } /* end if */
            skip_slab = slab[skip_dim];

            /* Check for being able to use fast algorithm for 1-D */
            if(0 == skip_dim) {
                /* Create sequences until an entire row can't be used */
                blk_count = tot_blk_count;
                while(blk_count > 0) {
                    /* Store the sequence information */
                    *off++ = loc;
                    *len++ = actual_bytes;

                    /* Increment offset in destination buffer */
                    loc += skip_slab;

                    /* Decrement block count */
                    blk_count--;
                } /* end while */

                /* Move to the next location */
                offset[skip_dim] += tot_blk_count;
            } /* end if */
            else {
                hsize_t tmp_block[H5O_LAYOUT_NDIMS];/* Temporary block offset */
                hsize_t skip[H5O_LAYOUT_NDIMS];     /* Bytes to skip between blocks */
                int temp_dim;               /* Temporary rank holder */

                /* Set the starting block location */
                for(u = 0; u < ndims; u++)
                    tmp_block[u] = iter->u.hyp.off[u] - tdiminfo[u].start;

                /* Compute the amount to skip between sequences */
                for(u = 0; u < ndims; u++)
                    skip[u] = (mem_size[u] - tdiminfo[u].block) * slab[u];

                /* Create sequences until an entire row can't be used */
                blk_count = tot_blk_count;
                while(blk_count > 0) {
                    /* Store the sequence information */
                    *off++ = loc;
                    *len++ = actual_bytes;

                    /* Set temporary dimension for advancing offsets */
                    temp_dim = (int)skip_dim;

                    /* Increment offset in destination buffer */
                    loc += skip_slab;

                    /* Increment the offset and count for the other dimensions */
                    while(temp_dim >= 0) {
                        /* Move to the next row in the curent dimension */
                        offset[temp_dim]++;
                        tmp_block[temp_dim]++;

                        /* If this block is still in the range of blocks to output for the dimension, break out of loop */
                        if(tmp_block[temp_dim] < tdiminfo[temp_dim].block)
                            break;
                        else {
                            offset[temp_dim] = base_offset[temp_dim];
                            loc += skip[temp_dim];
                            tmp_block[temp_dim] = 0;
                        } /* end else */

                        /* Decrement dimension count */
                        temp_dim--;
                    } /* end while */

                    /* Decrement block count */
                    blk_count--;
                } /* end while */
            } /* end else */
        } /* end else */

        /* Update the iterator, if there were any blocks used */

        /* Decrement the number of elements left in selection */
        iter->elmt_left -= tot_blk_count * actual_elem;

        /* Check if there are elements left in iterator */
        if(iter->elmt_left > 0) {
            /* Update the iterator with the location we stopped */
            /* (Subtract out the selection offset) */
            for(u = 0; u < ndims; u++)
                iter->u.hyp.off[u] = (hsize_t)((hssize_t)offset[u] - sel_off[u]);
        } /* end if */

        /* Increment the number of sequences generated */
        *nseq += tot_blk_count;

        /* Increment the number of elements used */
        *nelem += tot_blk_count * actual_elem;
    } /* end if */

    /* Check for partial block, with room for another sequence */
    if(io_left > (tot_blk_count * actual_elem) && tot_blk_count < maxseq) {
        size_t elmt_remainder;  /* Elements remaining */

        /* Compute elements left */
        elmt_remainder = io_left - (tot_blk_count * actual_elem);
        HDassert(elmt_remainder < fast_dim_block);
        HDassert(elmt_remainder > 0);

        /* Store the sequence information */
        *off++ = loc;
        *len++ = elmt_remainder * elem_size;

        /* Update the iterator with the location we stopped */
        iter->u.hyp.off[fast_dim] += (hsize_t)elmt_remainder;

        /* Decrement the number of elements left in selection */
        iter->elmt_left -= elmt_remainder;

        /* Increment the number of sequences generated */
        (*nseq)++;

        /* Increment the number of elements used */
        *nelem += elmt_remainder;
    } /* end if */

    /* Sanity check */
    HDassert(*nseq > 0);
    HDassert(*nelem > 0);

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5S_hyper_get_seq_list_single() */


/*--------------------------------------------------------------------------
 NAME
    H5S_hyper_get_seq_list
 PURPOSE
    Create a list of offsets & lengths for a selection
 USAGE
    herr_t H5S_hyper_get_seq_list(space,flags,iter,maxseq,maxelem,nseq,nelem,off,len)
        H5S_t *space;           IN: Dataspace containing selection to use.
        unsigned flags;         IN: Flags for extra information about operation
        H5S_sel_iter_t *iter;   IN/OUT: Selection iterator describing last
                                    position of interest in selection.
        size_t maxseq;          IN: Maximum number of sequences to generate
        size_t maxelem;         IN: Maximum number of elements to include in the
                                    generated sequences
        size_t *nseq;           OUT: Actual number of sequences generated
        size_t *nelem;          OUT: Actual number of elements in sequences generated
        hsize_t *off;           OUT: Array of offsets
        size_t *len;            OUT: Array of lengths
 RETURNS
    Non-negative on success/Negative on failure.
 DESCRIPTION
    Use the selection in the dataspace to generate a list of byte offsets and
    lengths for the region(s) selected.  Start/Restart from the position in the
    ITER parameter.  The number of sequences generated is limited by the MAXSEQ
    parameter and the number of sequences actually generated is stored in the
    NSEQ parameter.
 GLOBAL VARIABLES
 COMMENTS, BUGS, ASSUMPTIONS
 EXAMPLES
 REVISION LOG
--------------------------------------------------------------------------*/
static herr_t
H5S_hyper_get_seq_list(const H5S_t *space, unsigned UNUSED flags, H5S_sel_iter_t *iter,
    size_t maxseq, size_t maxelem, size_t *nseq, size_t *nelem,
    hsize_t *off, size_t *len)
{
    herr_t ret_value;      /* return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Check args */
    HDassert(space);
    HDassert(iter);
    HDassert(iter->elmt_left > 0);
    HDassert(maxseq > 0);
    HDassert(maxelem > 0);
    HDassert(nseq);
    HDassert(nelem);
    HDassert(off);
    HDassert(len);

    /* Rebuild diminfo if it is invalid and has not been confirmed to be
     * impossible */
    if(space->select.sel_info.hslab->diminfo_valid == H5S_DIMINFO_VALID_NO)
        /* Casting away const OK -NAF */
        (void)H5S_hyper_rebuild((H5S_t *)space);

    /* Check for the special case of just one H5Sselect_hyperslab call made */
    if(space->select.sel_info.hslab->diminfo_valid == H5S_DIMINFO_VALID_YES) {
        const H5S_hyper_dim_t *tdiminfo;    /* Temporary pointer to diminfo information */
        const hssize_t *sel_off;    /* Selection offset in dataspace */
        hsize_t *mem_size;      /* Size of the source buffer */
        unsigned ndims;         /* Number of dimensions of dataset */
        unsigned fast_dim;      /* Rank of the fastest changing dimension for the dataspace */
        hbool_t single_block;   /* Whether the selection is a single block */
        unsigned u;             /* Local index variable */

        /* Set a local copy of the diminfo pointer */
        tdiminfo = iter->u.hyp.diminfo;

        /* Check if this is a "flattened" regular hyperslab selection */
        if(iter->u.hyp.iter_rank != 0 && iter->u.hyp.iter_rank < space->extent.rank) {
            /* Set the aliases for a few important dimension ranks */
            ndims = iter->u.hyp.iter_rank;

            /* Set the local copy of the selection offset */
            sel_off = iter->u.hyp.sel_off;

            /* Set up the pointer to the size of the memory space */
            mem_size = iter->u.hyp.size;
        } /* end if */
        else {
            /* Set the aliases for a few important dimension ranks */
            ndims = space->extent.rank;

            /* Set the local copy of the selection offset */
            sel_off = space->select.offset;

            /* Set up the pointer to the size of the memory space */
            mem_size = space->extent.size;
        } /* end else */
        fast_dim = ndims - 1;

        /* Check if we stopped in the middle of a sequence of elements */
        if((iter->u.hyp.off[fast_dim] - tdiminfo[fast_dim].start) % tdiminfo[fast_dim].stride != 0 ||
                ((iter->u.hyp.off[fast_dim] != tdiminfo[fast_dim].start) && tdiminfo[fast_dim].count == 1)) {
            hsize_t slab[H5O_LAYOUT_NDIMS];     /* Hyperslab size */
            hsize_t loc;                /* Coordinate offset */
            hsize_t acc;	        /* Accumulator */
            size_t leftover;            /* The number of elements left over from the last sequence */
            size_t actual_elem;         /* The actual number of elements to count */
            size_t elem_size;           /* Size of each element iterating over */
            int i;                      /* Local index variable */


            /* Calculate the number of elements left in the sequence */
            if(tdiminfo[fast_dim].count == 1) {
                H5_ASSIGN_OVERFLOW(leftover, tdiminfo[fast_dim].block - (iter->u.hyp.off[fast_dim] - tdiminfo[fast_dim].start), hsize_t, size_t);
            } /* end if */
            else {
                H5_ASSIGN_OVERFLOW(leftover, tdiminfo[fast_dim].block - ((iter->u.hyp.off[fast_dim] - tdiminfo[fast_dim].start) % tdiminfo[fast_dim].stride), hsize_t, size_t);
            } /* end else */

            /* Make certain that we don't write too many */
            actual_elem = MIN3(leftover, (size_t)iter->elmt_left, maxelem);

            /* initialize row sizes for each dimension */
            elem_size = iter->elmt_size;
            for(i = (int)fast_dim, acc = elem_size; i >= 0; i--) {
                slab[i] = acc;
                acc *= mem_size[i];
            } /* end for */

            /* Compute the initial buffer offset */
            for(u = 0, loc = 0; u < ndims; u++)
                loc += ((hsize_t)((hssize_t)iter->u.hyp.off[u] + sel_off[u])) * slab[u];

            /* Add a new sequence */
            off[0] = loc;
            H5_ASSIGN_OVERFLOW(len[0], actual_elem * elem_size, hsize_t, size_t);

            /* Increment sequence array locations */
            off++;
            len++;

            /* Advance the hyperslab iterator */
            H5S_hyper_iter_next(iter, actual_elem);

            /* Decrement the number of elements left in selection */
            iter->elmt_left -= actual_elem;

            /* Decrement element/sequence limits */
            maxelem -= actual_elem;
            maxseq--;

            /* Set the number of sequences generated and elements used */
            *nseq = 1;
            *nelem = actual_elem;

            /* Check for using up all the sequences/elements */
            if(0 == iter->elmt_left || 0 == maxelem || 0 == maxseq)
                return(SUCCEED);
        } /* end if */
        else {
            /* Reset the number of sequences generated and elements used */
            *nseq = 0;
            *nelem = 0;
        } /* end else */

        /* Check for a single block selected */
        single_block = TRUE;
        for(u = 0; u < ndims; u++)
            if(1 != tdiminfo[u].count) {
                single_block = FALSE;
                break;
            } /* end if */

        /* Check for single block selection */
        if(single_block)
            /* Use single-block optimized call to generate sequence list */
            ret_value = H5S_hyper_get_seq_list_single(space, iter, maxseq, maxelem, nseq, nelem, off, len);
        else
            /* Use optimized call to generate sequence list */
            ret_value = H5S_hyper_get_seq_list_opt(space, iter, maxseq, maxelem, nseq, nelem, off, len);
    } /* end if */
    else
        /* Call the general sequence generator routine */
        ret_value = H5S_hyper_get_seq_list_gen(space, iter, maxseq, maxelem, nseq, nelem, off, len);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5S_hyper_get_seq_list() */


/*--------------------------------------------------------------------------
 NAME
    H5Sselect_is_regular
 PURPOSE
    Check if hyperslab selection is a regular selection
 RETURNS
    TRUE/FALSE/FAIL
--------------------------------------------------------------------------*/
htri_t
H5Sselect_is_regular(hid_t space_id)
{
    H5S_t		   *space;	/* dataspace to modify */
    htri_t		    ret_value;

    FUNC_ENTER_API(FAIL)
    H5TRACE1("t", "i", space_id);

    /* Check args and all the boring stuff. */
    if ((space = (H5S_t *)H5I_object_verify(space_id,H5I_DATASPACE)) == NULL)
	HGOTO_ERROR(H5E_ATOM, H5E_BADATOM, FAIL, "not a dataspace")

    if(H5S_GET_SELECT_TYPE(space) != H5S_SEL_HYPERSLABS)
        HGOTO_ERROR(H5E_ARGS, H5E_UNSUPPORTED, FAIL, "not a hyperslab selection")

    if(space->select.sel_info.hslab->diminfo_valid)
        ret_value = TRUE;
    else
        ret_value = FALSE;

done:
    FUNC_LEAVE_API(ret_value)
}


/*--------------------------------------------------------------------------
 NAME
    H5Sget_reg_hyperslab_params
 PURPOSE
    retrieve the start, stride, count, block arrays of a regular 
    hyperslab selection
 RETURNS
    TRUE/FALSE/FAIL
--------------------------------------------------------------------------*/
herr_t
H5Sget_reg_hyperslab_params(hid_t space_id, hsize_t start[], hsize_t stride[],
                            hsize_t count[], hsize_t block[])
{
    H5S_t *space = NULL;  /* Dataspace to get selection of */
    const H5S_hyper_dim_t *diminfo; /* Alias for dataspace's diminfo information */
    unsigned ndims; /* Rank of the dataspace */
    unsigned u;
    herr_t ret_value=SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE5("e", "i*h*h*h*h", space_id, start, stride, count, block);

    /* Check args */
        if (NULL == (space = (H5S_t *)H5I_object_verify(space_id, H5I_DATASPACE)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a data space")
    if(!space->select.sel_info.hslab->diminfo_valid)
        HGOTO_ERROR(H5E_ARGS, H5E_UNSUPPORTED, FAIL, "not a regular hyperslab selection")

    diminfo = space->select.sel_info.hslab->opt_diminfo;
    ndims = space->extent.rank;

    for(u=0 ; u<ndims; u++) {
        start[u] = diminfo[u].start;
        stride[u] = diminfo[u].stride;
        count[u] = diminfo[u].count;
        block[u] = diminfo[u].block;
    }

done:
    FUNC_LEAVE_API(ret_value)
}
