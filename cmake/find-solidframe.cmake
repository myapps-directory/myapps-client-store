set(SolidFrame_DIR "${EXTERNAL_DIR}/lib/cmake/SolidFrame" CACHE PATH "SolidFrame CMake configuration dir")
find_package(SolidFrame 7)

if(SolidFrame_FOUND)
    message("\n-- SolidFrame version: ${SolidFrame_VERSION}\n")
else()
    message(FATAL_ERROR "\nSolidFrame not found!\n")
endif()
