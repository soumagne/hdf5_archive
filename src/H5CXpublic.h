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
 * This file contains function prototypes for each exported function in the
 * H5CX module.
 */
#ifndef _H5CXpublic_H
#define _H5CXpublic_H

/* System headers needed by this file */

/* Public headers needed by this file */
#include "H5public.h"
#include "H5Ipublic.h"

/*****************/
/* Public Macros */
/*****************/

/*******************/
/* Public Typedefs */
/*******************/

/********************/
/* Public Variables */
/********************/

#ifdef __cplusplus
extern "C" {
#endif

/*********************/
/* Public Prototypes */
/*********************/

/* API wrappers */
H5_DLL hid_t  H5CXcreate(void /* Property list eventually */);
H5_DLL herr_t H5CXclose(hid_t context_id);
H5_DLL herr_t H5CXwait(hid_t context_id, unsigned int timeout, hbool_t *flag);
H5_DLL herr_t H5CXcancel(hid_t context_id);

#ifdef __cplusplus
}
#endif

#endif /* _H5CXpublic_H */
