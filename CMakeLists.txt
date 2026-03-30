cmake_minimum_required(VERSION 3.22)
project(modernimage C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------- Paths ----------

set(WEBP_DIR ${CMAKE_SOURCE_DIR}/deps/libwebp)
set(WEBP_BUILD ${WEBP_DIR}/build)
set(AVIF_DIR ${CMAKE_SOURCE_DIR}/deps/libavif)
set(AVIF_BUILD ${AVIF_DIR}/build)
set(AOM_BUILD ${AVIF_DIR}/ext/aom/build.libavif)

# ---------- Find system libraries ----------

find_package(Threads REQUIRED)
find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)
find_library(GIF_LIBRARY gif PATHS /opt/homebrew/lib)
find_path(GIF_INCLUDE_DIR gif_lib.h PATHS /opt/homebrew/include)

# ---------- libmodernimage sources ----------

set(MODERNIMAGE_SOURCES
    src/modernimage.c
    src/bridge_cwebp.c
    src/bridge_gif2webp.c
    src/bridge_avifenc.c
    # gifdec.c is not part of any libwebp static lib - compile it directly
    ${WEBP_DIR}/examples/gifdec.c
)

# ---------- Include directories ----------

set(MODERNIMAGE_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    # libwebp headers
    ${WEBP_DIR}
    ${WEBP_DIR}/src
    ${WEBP_DIR}/examples
    ${WEBP_DIR}/imageio
    # libavif headers
    ${AVIF_DIR}/include
    ${AVIF_DIR}/apps/shared
    ${AVIF_DIR}/third_party/iccjpeg
    # System
    ${PNG_INCLUDE_DIRS}
    ${JPEG_INCLUDE_DIR}
    ${GIF_INCLUDE_DIR}
)

# ---------- Static libraries to link ----------

# Order matters on Linux (ld processes left-to-right):
# libraries that USE symbols must come before libraries that DEFINE them
set(WEBP_LIBS
    ${WEBP_BUILD}/libimagedec.a
    ${WEBP_BUILD}/libimageenc.a
    ${WEBP_BUILD}/libimageioutil.a
    ${WEBP_BUILD}/libexampleutil.a
    ${WEBP_BUILD}/libwebpmux.a
    ${WEBP_BUILD}/libwebpdemux.a
    ${WEBP_BUILD}/libwebp.a
    ${WEBP_BUILD}/libwebpdecoder.a
    ${WEBP_BUILD}/libsharpyuv.a
)

set(AVIF_LIBS
    ${AVIF_BUILD}/libavif_internal.a
    ${AVIF_BUILD}/libavif_apps.a
    ${AOM_BUILD}/libaom.a
)

# ---------- Thin static library (our objects only) ----------

add_library(modernimage_objs STATIC ${MODERNIMAGE_SOURCES})
target_include_directories(modernimage_objs PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${MODERNIMAGE_INCLUDE_DIRS}
)
target_compile_definitions(modernimage_objs PRIVATE WEBP_HAVE_GIF WEBP_HAVE_PNG WEBP_HAVE_JPEG)
set_target_properties(modernimage_objs PROPERTIES OUTPUT_NAME modernimage_objs)

# ---------- Fat static library via cmake --install ----------
# Uses install(CODE) to combine archives at install time (reliable across platforms)

set(FAT_STATIC_LIB ${CMAKE_BINARY_DIR}/libmodernimage.a)

install(CODE "
    message(STATUS \"Combining static libraries into libmodernimage.a...\")

    set(LIBS_TO_COMBINE
        \"${CMAKE_BINARY_DIR}/libmodernimage_objs.a\"
        \"${WEBP_BUILD}/libwebp.a\"
        \"${WEBP_BUILD}/libwebpmux.a\"
        \"${WEBP_BUILD}/libwebpdemux.a\"
        \"${WEBP_BUILD}/libsharpyuv.a\"
        \"${WEBP_BUILD}/libimagedec.a\"
        \"${WEBP_BUILD}/libimageenc.a\"
        \"${WEBP_BUILD}/libimageioutil.a\"
        \"${WEBP_BUILD}/libexampleutil.a\"
        \"${AVIF_BUILD}/libavif_internal.a\"
        \"${AVIF_BUILD}/libavif_apps.a\"
        \"${AOM_BUILD}/libaom.a\"
    )

    if(APPLE)
        execute_process(
            COMMAND libtool -static -o \"${FAT_STATIC_LIB}\" \${LIBS_TO_COMBINE}
        )
    else()
        set(MRI_SCRIPT \"${CMAKE_BINARY_DIR}/combine.mri\")
        file(WRITE \"\${MRI_SCRIPT}\" \"CREATE ${FAT_STATIC_LIB}\\n\")
        foreach(LIB \${LIBS_TO_COMBINE})
            file(APPEND \"\${MRI_SCRIPT}\" \"ADDLIB \${LIB}\\n\")
        endforeach()
        file(APPEND \"\${MRI_SCRIPT}\" \"SAVE\\nEND\\n\")
        execute_process(
            COMMAND ar -M
            INPUT_FILE \"\${MRI_SCRIPT}\"
        )
        execute_process(
            COMMAND ranlib \"${FAT_STATIC_LIB}\"
        )
    endif()

    message(STATUS \"Created: ${FAT_STATIC_LIB}\")
")

# ---------- Shared library ----------

add_library(modernimage_shared SHARED ${MODERNIMAGE_SOURCES})
target_include_directories(modernimage_shared PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${MODERNIMAGE_INCLUDE_DIRS}
)
target_compile_definitions(modernimage_shared PRIVATE WEBP_HAVE_GIF WEBP_HAVE_PNG WEBP_HAVE_JPEG)
set_target_properties(modernimage_shared PROPERTIES OUTPUT_NAME modernimage)

target_link_libraries(modernimage_shared PRIVATE
    ${WEBP_LIBS}
    ${AVIF_LIBS}
    ${PNG_LIBRARIES}
    ${JPEG_LIBRARIES}
    ${GIF_LIBRARY}
    Threads::Threads
)
# -lm is not available on Windows
if(NOT WIN32)
    target_link_libraries(modernimage_shared PRIVATE m)
endif()

# ---------- Tests ----------

set(SYSTEM_LIBS
    ${PNG_LIBRARIES}
    ${JPEG_LIBRARIES}
    ${GIF_LIBRARY}
    Threads::Threads
)
if(NOT WIN32)
    list(APPEND SYSTEM_LIBS m)
endif()

# Tests link against the thin .a + dep .a directly (fat .a is created at install time)
set(ALL_STATIC_LIBS
    modernimage_objs
    ${WEBP_LIBS}
    ${AVIF_LIBS}
)

add_executable(test_binary_equiv tests/test_binary_equiv.c)
target_include_directories(test_binary_equiv PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_binary_equiv PRIVATE ${ALL_STATIC_LIBS} ${SYSTEM_LIBS})

add_executable(test_thread_safety tests/test_thread_safety.c)
target_include_directories(test_thread_safety PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_thread_safety PRIVATE ${ALL_STATIC_LIBS} ${SYSTEM_LIBS})

enable_testing()
add_test(NAME binary_equiv COMMAND test_binary_equiv)
add_test(NAME thread_safety COMMAND test_thread_safety)
