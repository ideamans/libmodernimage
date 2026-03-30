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

# libwebp libraries
set(WEBP_LIBS
    ${WEBP_BUILD}/libwebp.a
    ${WEBP_BUILD}/libwebpmux.a
    ${WEBP_BUILD}/libwebpdemux.a
    ${WEBP_BUILD}/libsharpyuv.a
    ${WEBP_BUILD}/libimagedec.a
    ${WEBP_BUILD}/libimageenc.a
    ${WEBP_BUILD}/libimageioutil.a
    ${WEBP_BUILD}/libexampleutil.a
)

# libavif libraries
set(AVIF_LIBS
    ${AVIF_BUILD}/libavif_internal.a
    ${AVIF_BUILD}/libavif_apps.a
    ${AOM_BUILD}/libaom.a
)

# ---------- Static library (thin - just our objects) ----------

add_library(modernimage_objs STATIC ${MODERNIMAGE_SOURCES})
target_include_directories(modernimage_objs PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${MODERNIMAGE_INCLUDE_DIRS}
)
target_compile_definitions(modernimage_objs PRIVATE WEBP_HAVE_GIF WEBP_HAVE_PNG WEBP_HAVE_JPEG)
set_target_properties(modernimage_objs PROPERTIES OUTPUT_NAME modernimage_objs)

# ---------- Fat static library (all deps bundled) ----------
# Uses libtool (macOS) or ar scripts (Linux) to merge all .a into one

set(ALL_STATIC_LIBS
    $<TARGET_FILE:modernimage_objs>
    ${WEBP_LIBS}
    ${AVIF_LIBS}
)

set(FAT_STATIC_LIB ${CMAKE_BINARY_DIR}/libmodernimage.a)

if(APPLE)
    add_custom_command(
        OUTPUT ${FAT_STATIC_LIB}
        COMMAND libtool -static -o ${FAT_STATIC_LIB} ${ALL_STATIC_LIBS}
        DEPENDS modernimage_objs
        COMMENT "Creating fat static library with libtool"
    )
else()
    # Linux: use ar to merge
    add_custom_command(
        OUTPUT ${FAT_STATIC_LIB}
        COMMAND ${CMAKE_COMMAND} -E rm -f ${FAT_STATIC_LIB}
        COMMAND ${CMAKE_COMMAND} -E echo "CREATE ${FAT_STATIC_LIB}" > ${CMAKE_BINARY_DIR}/merge.mri
        COMMAND ${CMAKE_COMMAND} -E echo "ADDLIB $<TARGET_FILE:modernimage_objs>" >> ${CMAKE_BINARY_DIR}/merge.mri
        # Append each dep lib via a script generated at build time
        COMMAND bash -c "for lib in ${WEBP_LIBS} ${AVIF_LIBS}\; do echo ADDLIB $$lib >> ${CMAKE_BINARY_DIR}/merge.mri\; done"
        COMMAND ${CMAKE_COMMAND} -E echo "SAVE" >> ${CMAKE_BINARY_DIR}/merge.mri
        COMMAND ${CMAKE_COMMAND} -E echo "END" >> ${CMAKE_BINARY_DIR}/merge.mri
        COMMAND ${CMAKE_AR} -M < ${CMAKE_BINARY_DIR}/merge.mri
        DEPENDS modernimage_objs
        COMMENT "Creating fat static library with ar MRI script"
    )
endif()

add_custom_target(modernimage_static ALL DEPENDS ${FAT_STATIC_LIB})

# ---------- Shared library ----------

add_library(modernimage_shared SHARED ${MODERNIMAGE_SOURCES})
target_include_directories(modernimage_shared PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${MODERNIMAGE_INCLUDE_DIRS}
)
target_compile_definitions(modernimage_shared PRIVATE WEBP_HAVE_GIF WEBP_HAVE_PNG WEBP_HAVE_JPEG)
set_target_properties(modernimage_shared PROPERTIES OUTPUT_NAME modernimage)

# Link all dependencies into the shared library
target_link_libraries(modernimage_shared PRIVATE
    ${WEBP_LIBS}
    ${AVIF_LIBS}
    ${PNG_LIBRARIES}
    ${JPEG_LIBRARIES}
    ${GIF_LIBRARY}
    Threads::Threads
    m
)

# ---------- Tests ----------

# System libs still needed (not bundled in fat .a)
set(SYSTEM_LIBS
    ${PNG_LIBRARIES}
    ${JPEG_LIBRARIES}
    ${GIF_LIBRARY}
    Threads::Threads
    m
)

# Test: binary equivalence
add_executable(test_binary_equiv tests/test_binary_equiv.c)
target_include_directories(test_binary_equiv PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_dependencies(test_binary_equiv modernimage_static)
target_link_libraries(test_binary_equiv PRIVATE
    ${FAT_STATIC_LIB}
    ${SYSTEM_LIBS}
)

# Test: thread safety and memory
add_executable(test_thread_safety tests/test_thread_safety.c)
target_include_directories(test_thread_safety PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_dependencies(test_thread_safety modernimage_static)
target_link_libraries(test_thread_safety PRIVATE
    ${FAT_STATIC_LIB}
    ${SYSTEM_LIBS}
)

# CTest
enable_testing()
add_test(NAME binary_equiv COMMAND test_binary_equiv)
add_test(NAME thread_safety COMMAND test_thread_safety)
