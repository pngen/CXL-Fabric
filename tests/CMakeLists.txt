# CXL Fabric tests. No test timeouts are set anywhere.

function(cxl_fabric_add_test name)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${name}.cpp")
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE CXLFabric::core CXLFabric::synthetic CXLFabric::system)
    cxl_fabric_set_warnings(${name})
    cxl_fabric_apply_asan(${name})
    add_test(NAME ${name} COMMAND ${name})
  endif()
endfunction()

foreach(t IN ITEMS test_core test_capabilities test_persistence test_protocol
                test_property test_concurrency)
  cxl_fabric_add_test(${t})
endforeach()
# The multiprocess proof drives the real coordinator/worker executables.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_multiprocess.cpp")
  add_executable(test_multiprocess test_multiprocess.cpp)
  target_link_libraries(test_multiprocess PRIVATE CXLFabric::core CXLFabric::synthetic CXLFabric::system)
  cxl_fabric_set_warnings(test_multiprocess)
  cxl_fabric_apply_asan(test_multiprocess)
  if(MSVC)
    target_link_libraries(test_multiprocess PRIVATE ws2_32)
  endif()
  add_test(NAME test_multiprocess
           COMMAND test_multiprocess
                   $<TARGET_FILE:cxl_fabric_coordinator>
                   $<TARGET_FILE:cxl_fabric_worker>)
endif()