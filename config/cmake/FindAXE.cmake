#  Try to find the AXE library and headers.
#  This file sets the following variables:
#
#  AXE_INCLUDE_DIR, where to find axe.h, etc.
#  AXE_LIBRARIES, the libraries to link against
#  AXE_FOUND, If false, do not try to use AXE.
#
# Also defined, but not for general use are:
#  AXE_LIBRARY, the full path to the axe library.

find_path(AXE_INCLUDE_DIR axe.h
  HINTS /usr/local/include /usr/include)

find_library(AXE_LIBRARY NAMES axe
  PATHS /usr/local/lib /usr/lib)

set(AXE_INCLUDE_DIRS ${AXE_INCLUDE_DIR})
set(AXE_LIBRARIES ${AXE_LIBRARY})

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set AXE_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(AXE DEFAULT_MSG
                                  AXE_INCLUDE_DIR AXE_LIBRARY)

mark_as_advanced(AXE_INCLUDE_DIR AXE_LIBRARY)

