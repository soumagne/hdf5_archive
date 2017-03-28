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
 * Purpose:	The public header file for the DAOS-M VOL plugin.
 */
#ifndef H5VLdaos_H
#define H5VLdaos_H

/* Public headers needed by this file */
#include "H5public.h"
#include "H5Ipublic.h"

#define H5VL_DAOS (H5VL_daos_init())
#define HDF5_VOL_DAOS_VERSION_1   1   /* Version number of DAOS VOL plugin */

#ifdef __cplusplus
extern "C" {
#endif

H5_DLL hid_t H5VL_daos_init(void);
H5_DLL herr_t H5Pset_fapl_daos(hid_t fapl_id, MPI_Comm comm, MPI_Info info,
    const char *pool_uuid, const char *pool_grp);

#ifdef __cplusplus
}
#endif

#endif /* H5VLdaos_H */
