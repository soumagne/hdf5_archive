cmake_minimum_required (VERSION 2.8.10)
project (HDF5_FF_TEST_DRIVER CXX)

set (CMAKE_C_FLAGS "")
set (CMAKE_CXX_FLAGS "")
set (KWSYS_NAMESPACE h5ff_sys)
set (KWSYS_USE_SystemTools 1)
set (KWSYS_USE_Process 1)
set (KWSYS_USE_RegularExpression 1)
add_subdirectory (kwsys)
include_directories (${CMAKE_CURRENT_BINARY_DIR}/kwsys)

add_executable (h5ff_test_driver h5ff_test_driver.cxx)
target_link_libraries (h5ff_test_driver h5ff_sys)
