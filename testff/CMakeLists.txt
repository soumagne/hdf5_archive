cmake_minimum_required (VERSION 2.8.10)
project (HDF5_TEST_FF)

#-----------------------------------------------------------------------------
# Apply Definitions to compiler in this directory and below
#-----------------------------------------------------------------------------
add_definitions (${HDF5_EXTRA_C_FLAGS})

include_directories (${HDF5_TEST_SRC_DIR})
include_directories (${HDF5_TOOLS_SRC_DIR}/lib )

#------------------------------------------------------------------------------
# Compile kwsys library and setup TestDriver
#------------------------------------------------------------------------------
configure_file (
  ${CMAKE_CURRENT_SOURCE_DIR}/h5ff_test_config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/h5ff_test_config.h
)

include_directories (${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory (driver)

#------------------------------------------------------------------------------
# Set up test macros
#------------------------------------------------------------------------------
# build_h5ff_test : build and create executable from target file
macro (build_h5ff_test  file)
  add_executable (${file} ${HDF5_TEST_FF_SOURCE_DIR}/${file}.c)
  TARGET_NAMING (${file} STATIC)
  target_link_libraries (${file}
    ${HDF5_TEST_LIB_TARGET} ${HDF5_LIB_TARGET} ${LINK_LIBS})
  set_target_properties (${file} PROPERTIES FOLDER test/ff)
endmacro (build_h5ff_test )

# ADD_H5FF_TEST: add parallel test from target file
macro (ADD_H5FF_TEST file)
  add_test (NAME TEST_FF_${file} COMMAND ${MPIEXEC} ${MPIEXEC_PREFLAGS} 
    ${MPIEXEC_NUMPROC_FLAG} ${MPIEXEC_MAX_NUMPROCS} ${MPIEXEC_POSTFLAGS}
    $<TARGET_FILE:${file}>)
endmacro (ADD_H5FF_TEST)

# add_h5ff_driver_test : add client/server test from target server and target client
macro (add_h5ff_driver_test  test_name server client)
  add_test (NAME TEST_FF_${test_name}
    COMMAND $<TARGET_FILE:h5ff_test_driver>
    --server $<TARGET_FILE:${server}>
    --client $<TARGET_FILE:${client}>
  )
endmacro (add_h5ff_driver_test )

# Save that for later if we need to launch using MPMD run
#MACRO (ADD_H5EFF_MPMD_TEST test_name server client)
#  ADD_TEST (NAME "${test_name}"
#    COMMAND ${MPIEXEC} ${MPIEXEC_NUMPROC_FLAG} 1 ${MPIEXEC_PREFLAGS}
#    $<TARGET_FILE:${server}> : ${MPIEXEC_NUMPROC_FLAG}
#    ${MPIEXEC_MAX_NUMPROCS} ${MPIEXEC_PREFLAGS} $<TARGET_FILE:${client}>
#  )
#MACRO (ADD_H5EFF_MPMD_TEST)

#-----------------------------------------------------------------------------
# Build Tests
#-----------------------------------------------------------------------------
# hello
build_h5ff_test (hello_server)
build_h5ff_test (hello_client)

build_h5ff_test (h5ff_query)

# h5ff
# add_h5ff_driver_test (<target>)
build_h5ff_test (h5ff_server)
build_h5ff_test (h5ff_client_analysis)
build_h5ff_test (h5ff_client_adv_analysis)
build_h5ff_test (h5ff_client_attr)
build_h5ff_test (h5ff_client_dset)
build_h5ff_test (h5ff_client_evict_deltas)
if (HDF5_ENABLE_INDEXING)
  build_h5ff_test (h5ff_client_index)
endif (HDF5_ENABLE_INDEXING)
build_h5ff_test (h5ff_client_links)
build_h5ff_test (h5ff_client_M6.2_demo)
build_h5ff_test (h5ff_client_M7.2-pep_demo)
build_h5ff_test (h5ff_client_map)
build_h5ff_test (h5ff_client_multiple_cont)
build_h5ff_test (h5ff_client_obj)
build_h5ff_test (h5ff_client_prefetch)
build_h5ff_test (h5ff_client_ref)
build_h5ff_test (h5ff_client_time_datasets)
build_h5ff_test (h5ff_client_timings)
build_h5ff_test (h5ff_client_trans)
build_h5ff_test (h5ff_client_view)
build_h5ff_test (h5ff_client_vl_data)
if (HDF5_ENABLE_PYTHON)
  build_h5ff_test (h5ff_python_exec)
endif (HDF5_ENABLE_PYTHON)
# build_h5ff_test (h5ff_client_container_open)

#-----------------------------------------------------------------------------
# Define Tests
#-----------------------------------------------------------------------------
# hello
add_h5ff_driver_test (hello hello_server hello_client)

# h5ff
# add_h5ff_driver_test (<test_name> <server_target> <client_target>)
add_h5ff_driver_test (h5ff_attr h5ff_server h5ff_client_attr)
add_h5ff_driver_test (h5ff_dset h5ff_server h5ff_client_dset)
add_h5ff_driver_test (h5ff_links h5ff_server h5ff_client_links)
add_h5ff_driver_test (h5ff_map h5ff_server h5ff_client_map)
add_h5ff_driver_test (h5ff_multiple_count h5ff_server h5ff_client_multiple_cont)
add_h5ff_driver_test (h5ff_obj h5ff_server h5ff_client_obj)
add_h5ff_driver_test (h5ff_trans h5ff_server h5ff_client_trans)

# add_h5ff_driver_test (h5ff_container_open h5ff_server h5ff_client_container_open)

# TODO make a list of test and loop over list with foreach
#SET (H5FF_TESTS
#    attr
#    dset
#    links
#    ...
#)

#FOREACH (testff ${H5FF_TESTS})
#  add_h5ff_driver_test (${testff} ${testff}_server ${testff}_client)
#ENDFOREACH (testff ${H5FF_TESTS})

