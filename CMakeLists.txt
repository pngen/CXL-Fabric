cmake_minimum_required(VERSION 3.20)
project(CXLFabric VERSION 1.0.1
        DESCRIPTION "CXL Fabric - CXL-class memory infrastructure governance"
        LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(CXLFABRIC_BUILD_TESTS "Build the test suite" ON)
option(CXLFABRIC_BUILD_EXAMPLES "Build runnable examples" ON)
option(CXLFABRIC_BUILD_BENCHMARKS "Build benchmarks" ON)
option(CXLFABRIC_BUILD_CLI "Build the cxl-fabric CLI" ON)
option(CXLFABRIC_ENABLE_ASAN "Enable AddressSanitizer (MSVC CPU) where supported" OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

function(cxl_fabric_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /EHsc /permissive- /Zc:__cplusplus /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
  endif()
endfunction()

function(cxl_fabric_apply_asan target)
  if(CXLFABRIC_ENABLE_ASAN AND MSVC)
    target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
    target_link_options(${target} PRIVATE /fsanitize=address)
  elseif(CXLFABRIC_ENABLE_ASAN)
    target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(${target} PRIVATE -fsanitize=address)
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
set(CXLFABRIC_CORE_SRC
  src/core/fabric.cpp
  src/core/protocol.cpp
  src/core/persistence.cpp
)
add_library(cxl_fabric_core STATIC "")
foreach(src IN LISTS CXLFABRIC_CORE_SRC)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
    target_sources(cxl_fabric_core PRIVATE "${src}")
  endif()
endforeach()
target_include_directories(cxl_fabric_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
cxl_fabric_set_warnings(cxl_fabric_core)
cxl_fabric_apply_asan(cxl_fabric_core)
set_target_properties(cxl_fabric_core PROPERTIES EXPORT_NAME core)
add_library(CXLFabric::core ALIAS cxl_fabric_core)

# ---------------------------------------------------------------------------
# Optional backends
# ---------------------------------------------------------------------------
add_library(cxl_fabric_system STATIC src/backend/system_backend.cpp)
target_include_directories(cxl_fabric_system PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(cxl_fabric_system PUBLIC CXLFabric::core)
if(MSVC)
  target_link_libraries(cxl_fabric_system PUBLIC setupapi)
endif()
cxl_fabric_set_warnings(cxl_fabric_system)
cxl_fabric_apply_asan(cxl_fabric_system)
set_target_properties(cxl_fabric_system PROPERTIES EXPORT_NAME system)
add_library(CXLFabric::system ALIAS cxl_fabric_system)

add_library(cxl_fabric_synthetic STATIC src/backend/synthetic_backend.cpp)
target_include_directories(cxl_fabric_synthetic PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(cxl_fabric_synthetic PUBLIC CXLFabric::core)
cxl_fabric_set_warnings(cxl_fabric_synthetic)
cxl_fabric_apply_asan(cxl_fabric_synthetic)
set_target_properties(cxl_fabric_synthetic PROPERTIES EXPORT_NAME synthetic)
add_library(CXLFabric::synthetic ALIAS cxl_fabric_synthetic)

add_library(cxl_fabric_unsupported STATIC src/backend/unsupported_backend.cpp)
target_include_directories(cxl_fabric_unsupported PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(cxl_fabric_unsupported PUBLIC CXLFabric::core)
cxl_fabric_set_warnings(cxl_fabric_unsupported)
cxl_fabric_apply_asan(cxl_fabric_unsupported)
set_target_properties(cxl_fabric_unsupported PROPERTIES EXPORT_NAME unsupported)
add_library(CXLFabric::unsupported ALIAS cxl_fabric_unsupported)

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
if(CXLFABRIC_BUILD_CLI)
  add_executable(cxl-fabric src/cli/cxl_fabric_cli.cpp)
  target_link_libraries(cxl-fabric PRIVATE CXLFabric::synthetic CXLFabric::system
                        CXLFabric::unsupported CXLFabric::core)
  cxl_fabric_set_warnings(cxl-fabric)
  cxl_fabric_apply_asan(cxl-fabric)
endif()

# ---------------------------------------------------------------------------
# Multiprocess control plane
# ---------------------------------------------------------------------------
add_executable(cxl_fabric_coordinator src/multiprocess/coordinator_main.cpp)
target_link_libraries(cxl_fabric_coordinator PRIVATE CXLFabric::core)
if(MSVC)
  target_link_libraries(cxl_fabric_coordinator PRIVATE ws2_32)
endif()
cxl_fabric_set_warnings(cxl_fabric_coordinator)
cxl_fabric_apply_asan(cxl_fabric_coordinator)

add_executable(cxl_fabric_worker src/multiprocess/worker_main.cpp)
target_link_libraries(cxl_fabric_worker PRIVATE CXLFabric::core CXLFabric::synthetic)
if(MSVC)
  target_link_libraries(cxl_fabric_worker PRIVATE ws2_32)
endif()
cxl_fabric_set_warnings(cxl_fabric_worker)
cxl_fabric_apply_asan(cxl_fabric_worker)

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(CXLFABRIC_BUILD_EXAMPLES)
  set(CXLFABRIC_EXAMPLES
    basic_discovery capacity_accounting pool_selection locality_policy
    reservation_lifecycle stale_generation failover_selection persistence_recovery
    synthetic_cxl_pool)
  foreach(ex IN LISTS CXLFABRIC_EXAMPLES)
    add_executable("example_${ex}" "examples/${ex}.cpp")
    target_link_libraries("example_${ex}" PRIVATE CXLFabric::synthetic CXLFabric::system
                          CXLFabric::core)
    cxl_fabric_set_warnings("example_${ex}")
    cxl_fabric_apply_asan("example_${ex}")
  endforeach()
endif()

# ---------------------------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------------------------
if(CXLFABRIC_BUILD_BENCHMARKS)
  add_executable(cxl_fabric_bench benchmarks/cxl_fabric_bench.cpp)
  target_link_libraries(cxl_fabric_bench PRIVATE CXLFabric::core)
  cxl_fabric_set_warnings(cxl_fabric_bench)
  cxl_fabric_apply_asan(cxl_fabric_bench)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(CXLFABRIC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install / package export
# ---------------------------------------------------------------------------
install(TARGETS cxl_fabric_core cxl_fabric_system cxl_fabric_synthetic cxl_fabric_unsupported
        EXPORT CXLFabricTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(CXLFABRIC_BUILD_CLI)
  install(TARGETS cxl-fabric RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(DIRECTORY include/cxl_fabric DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

set(CXLFabric_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/CXLFabric")
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CXLFabricConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/CXLFabricConfig.cmake"
  INSTALL_DESTINATION "${CXLFabric_INSTALL_CMAKEDIR}")
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/CXLFabricConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/CXLFabricConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/CXLFabricConfigVersion.cmake"
  DESTINATION "${CXLFabric_INSTALL_CMAKEDIR}")
install(EXPORT CXLFabricTargets
  FILE CXLFabricTargets.cmake
  NAMESPACE CXLFabric::
  DESTINATION "${CXLFabric_INSTALL_CMAKEDIR}")