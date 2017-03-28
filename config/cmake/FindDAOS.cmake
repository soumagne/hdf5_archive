# - Try to find DAOS
# Once done this will define
#  DAOS_FOUND - System has DAOS
#  DAOS_INCLUDE_DIRS - The DAOS include directories
#  DAOS_LIBRARIES - The libraries needed to use DAOS

find_path(DAOS_INCLUDE_DIR daos.h
  HINTS /usr/local/include /usr/include /opt/daos/default/include)

find_library(DAOS_LIBRARY NAMES daos
  HINTS /usr/local/lib /usr/lib /opt/daos/default/lib)

find_library(DAOS_COMMON_LIBRARY NAMES daos_common
  HINTS /usr/local/lib /usr/lib /opt/daos/default/lib)

find_library(DAOS_TIER_LIBRARY NAMES daos_tier
  HINTS /usr/local/lib /usr/lib /opt/daos/default/lib)

find_path(CART_INCLUDE_DIR crt_api.h
  HINTS /usr/local/include /usr/include /opt/cart/default/include)

find_library(CART_LIBRARY NAMES crt
  HINTS /usr/local/lib /usr/lib /opt/cart/default/lib)


set(DAOS_LIBRARIES ${DAOS_LIBRARY} ${CART_LIBRARY} ${DAOS_COMMON_LIBRARY} ${DAOS_TIER_LIBRARY})
set(DAOS_INCLUDE_DIRS ${DAOS_INCLUDE_DIR} ${CART_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set DAOS_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(DAOS DEFAULT_MSG
                                  DAOS_LIBRARY DAOS_INCLUDE_DIR
                                  DAOS_COMMON_LIBRARY DAOS_TIER_LIBRARY
                                  CART_LIBRARY CART_INCLUDE_DIR)

mark_as_advanced(DAOS_INCLUDE_DIR DAOS_LIBRARY DAOS_COMMON_LIBRARY DAOS_TIER_LIBRARY CART_INCLUDE_DIR CART_LIBRARY)
