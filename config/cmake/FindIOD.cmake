#  Try to find IOD library and headers.
#  This file sets the following variables:
#
#  IOD_INCLUDE_DIR, where to find iod.h, etc.
#  IOD_LIBRARIES, the libraries to link against
#  IOD_FOUND, If false, do not try to use IOD.
#
# Also defined, but not for general use are:
#  IOD_LIBRARY, the full path to the iod library.

find_path(IOD_INCLUDE_DIR iod_api.h
  HINTS /usr/local/include /usr/include)

find_library(IOD_LIBRARY NAMES iod
  PATHS /usr/local/lib /usr/lib)

find_library(DAOS_LIBRARY NAMES daos
  PATHS /usr/local/lib /usr/lib)

find_library(PLFS_LIBRARY NAMES plfs
  PATHS /usr/local/lib /usr/lib)

set(IOD_INCLUDE_DIRS ${IOD_INCLUDE_DIR})
set(IOD_LIBRARIES ${IOD_LIBRARY} ${DAOS_LIBRARY} ${PLFS_LIBRARY})

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set IOD_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(IOD DEFAULT_MSG
                                  IOD_INCLUDE_DIR IOD_LIBRARY DAOS_LIBRARY PLFS_LIBRARY)

mark_as_advanced(IOD_INCLUDE_DIR IOD_LIBRARY DAOS_LIBRARY PLFS_LIBRARY)
