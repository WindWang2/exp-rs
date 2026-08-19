# Vendored LibSVM for OTB Learning (TrainImagesClassifier, etc.)
# NOTE (BUILD-12): not added via add_subdirectory() in the main build; vendor mode expects
# system libsvm. To use, add `add_subdirectory(cmake/LibSVM)` behind an option.
cmake_minimum_required(VERSION 3.13)
project(libsvm_vendored LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Upstream layout: svm.cpp + svm.h at the package root.
if(NOT LIBSVM_SOURCE_DIR)
    message(FATAL_ERROR "LIBSVM_SOURCE_DIR must point at the libsvm source tree")
endif()

add_library(svm SHARED "${LIBSVM_SOURCE_DIR}/svm.cpp")
target_include_directories(svm PUBLIC
    $<BUILD_INTERFACE:${LIBSVM_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)
set_target_properties(svm PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME svm
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# libsvm uses math
find_library(M_LIBRARY NAMES m)
if(M_LIBRARY)
    target_link_libraries(svm PRIVATE ${M_LIBRARY})
endif()

install(TARGETS svm
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(FILES "${LIBSVM_SOURCE_DIR}/svm.h" DESTINATION include)
