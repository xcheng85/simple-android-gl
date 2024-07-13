MESSAGE(STATUS "FUCK ${PLATFORM}")

set(KTX_DIR ../../../../../ktx)
set(KTX_SOURCES
        ${KTX_DIR}/lib/texture.c
        ${KTX_DIR}/lib/hashlist.c
        ${KTX_DIR}/lib/checkheader.c
        ${KTX_DIR}/lib/swap.c
        ${KTX_DIR}/lib/memstream.c
        ${KTX_DIR}/lib/filestream.c
)
set(KTX_INCLUDE
        ${KTX_DIR}/include
        ${KTX_DIR}/lib
        ${KTX_DIR}/other_include
)

add_library(ktx ${KTX_SOURCES})
target_include_directories(ktx PUBLIC ${KTX_INCLUDE})

file(GLOB BASE_SRC "*.cpp" "*.hpp" "*.h")
file(GLOB BASE_HEADERS "*.hpp" "*.h")

add_library(infra SHARED ${BASE_SRC} ${BASE_HEADERS})
target_include_directories(infra PUBLIC .)
target_link_libraries(
        infra
        ktx
        android
        log
)
