set(snappy_PREFIX ${CMAKE_BINARY_DIR}/external/snappy)

ExternalProject_Add(
    build-snappy
    EXCLUDE_FROM_ALL 1
    PREFIX ${snappy_PREFIX}
    URL https://github.com/google/snappy/archive/refs/tags/1.2.2.tar.gz
    DOWNLOAD_NO_PROGRESS ON
    CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/external -DCMAKE_INSTALL_LIBDIR=lib -DSNAPPY_BUILD_TESTS=OFF -DSNAPPY_BUILD_BENCHMARKS=OFF
    LOG_UPDATE ON
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
)
if(WIN32)
    set(SNAPPY_LIB ${CMAKE_BINARY_DIR}/external/lib/snappy.lib)
else()
    set(SNAPPY_LIB ${CMAKE_BINARY_DIR}/external/lib/libsnappy.a)
endif()
