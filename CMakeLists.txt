CMAKE_MINIMUM_REQUIRED(VERSION 3.10)

project(CNPYpp LANGUAGES CXX C VERSION 2.1.1)

add_library(cnpy++ "src/cnpy++.cpp" "src/buffer.cpp" "src/c_interface.c")

get_directory_property(hasParent PARENT_DIRECTORY)

# user can choose which implementation of span<T> to use
set(CNPYPP_SPAN_IMPL CACHE STRING "select implementation of cnpypp::span<T>")
set_property(CACHE CNPYPP_SPAN_IMPL PROPERTY STRINGS "MS_GSL" "GSL_LITE" "BOOST")
option(CNPYPP_USE_LIBZIP "require libzip to enable support for npz" ON)

set(minimum_boost_version 1.74)

include(FetchContent)

if(CNPYPP_SPAN_IMPL STREQUAL "MS_GSL")
  target_compile_definitions(cnpy++ PUBLIC "MSGSL_SPAN")
  find_package(Microsoft.GSL 3.1.0 QUIET)
  if (NOT Microsoft.GSL_FOUND)
    message(STATUS "Microsoft.GSL not found; downloading")
    FetchContent_Declare(GSL
      GIT_REPOSITORY "https://github.com/microsoft/GSL"
      GIT_TAG "v3.1.0"
      GIT_SHALLOW True
    )
    FetchContent_MakeAvailable(GSL)
    target_link_libraries(cnpy++ PUBLIC Microsoft.GSL::GSL)
  endif()

elseif(CNPYPP_SPAN_IMPL STREQUAL "GSL_LITE")
  target_compile_definitions(cnpy++ PUBLIC "GSL_LITE_SPAN")
  find_package(gsl-lite 0.40 QUIET)
  if (NOT gsl-lite_FOUND)
    message(STATUS "gsl-lite not found; downloading")
    FetchContent_Declare(GSLLite
      GIT_REPOSITORY "https://github.com/gsl-lite/gsl-lite"
      GIT_TAG "v0.40.0"
      GIT_SHALLOW True
    )
    FetchContent_MakeAvailable(GSLLite)
    target_link_libraries(cnpy++ PUBLIC gsl::gsl-lite-v1)
  endif()

elseif(CNPYPP_SPAN_IMPL STREQUAL "BOOST")
  target_compile_definitions(cnpy++ PUBLIC "BOOST_SPAN")
  set(minimum_boost_version 1.78)
endif()

include(GNUInstallDirs)

if(CNPYPP_USE_LIBZIP)
  find_package(libzip REQUIRED)
endif()
find_package(Boost ${minimum_boost_version} COMPONENTS filesystem iostreams REQUIRED)

target_compile_features(cnpy++ PUBLIC cxx_std_17)
set_property(TARGET cnpy++ PROPERTY CXX_EXTENSIONS OFF)
target_include_directories(cnpy++ PUBLIC ${Boost_INCLUDE_DIR})
target_include_directories(cnpy++ SYSTEM PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
target_include_directories(cnpy++ SYSTEM INTERFACE $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include>)
target_link_libraries(cnpy++ PRIVATE Boost::filesystem Boost::iostreams)
if(CNPYPP_USE_LIBZIP)
  target_link_libraries(cnpy++ PRIVATE libzip::zip)
else()
  target_compile_definitions(cnpy++ PUBLIC NO_LIBZIP)
endif()

if(MSVC)
  target_compile_options(cnpy++ PRIVATE /W4 /WX)
else()
  target_compile_options(cnpy++ PRIVATE -Wall -Wextra -pedantic -Wfatal-errors)
endif()

install(TARGETS "cnpy++" LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES "include/cnpy++/tuple_util.hpp"
    "include/cnpy++/stride_iterator.hpp"
    "include/cnpy++/map_type.hpp"
    "include/cnpy++/buffer.hpp" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cnpy++)
install(FILES "include/cnpy++.hpp" "include/cnpy++.h" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if (NOT hasParent)
  add_executable(example1 "examples/example1.cpp")
  target_link_libraries(example1 cnpy++)

  if (CNPYPP_USE_LIBZIP)
    add_executable(example2 "examples/example2.cpp")
    target_link_libraries(example2 cnpy++)
  endif()
  
  add_executable(example_c "examples/example_c.c")
  target_link_libraries(example_c cnpy++)
  
  add_executable(range_example "examples/range_example.cpp")
  target_link_libraries(range_example cnpy++)
  target_compile_features(range_example PRIVATE cxx_std_20)
  set_property(TARGET range_example PROPERTY CXX_EXTENSIONS OFF)
  
  find_package(range-v3 QUIET)
  if (NOT range-v3_FOUND)
    message (STATUS  "range-v3 not found; downloading")
    FetchContent_Declare(range-v3
        GIT_REPOSITORY "https://github.com/ericniebler/range-v3"
        GIT_TAG "0.12.0"
        GIT_SHALLOW True
    )
    FetchContent_MakeAvailable(range-v3)
  endif()
  
  add_executable(range_zip_example "examples/range_zip_example.cpp")
  target_link_libraries(range_zip_example cnpy++ range-v3::range-v3)
  target_compile_features(range_zip_example PRIVATE cxx_std_17)
  set_property(TARGET range_zip_example PROPERTY CXX_EXTENSIONS OFF)
endif()
