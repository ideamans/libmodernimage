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
set(JPEG_DIR ${CMAKE_SOURCE_DIR}/deps/libjpeg-turbo)
set(JPEG_BUILD ${JPEG_DIR}/build)

# Self-built deps prefix (from scripts/build-deps.sh)
set(DEPS_PREFIX ${CMAKE_SOURCE_DIR}/deps-build)

# ---------- Find libraries ----------
# Prefer self-built static libraries in deps-build/, fall back to system

find_package(Threads REQUIRED)

# Look in our prefix first
# Check for self-built libpng (explicit .a path to avoid framework detection)
if(EXISTS "${DEPS_PREFIX}/lib/libpng16.a")
    set(PNG_LIBRARY "${DEPS_PREFIX}/lib/libpng16.a")
    set(PNG_INCLUDE_DIR "${DEPS_PREFIX}/include")
    message(STATUS "Using self-built libpng: ${PNG_LIBRARY}")
else()
    find_package(PNG REQUIRED)
    set(PNG_LIBRARY ${PNG_LIBRARIES})
endif()
find_path(PNG_INCLUDE_DIR png.h
    PATHS ${DEPS_PREFIX}/include NO_DEFAULT_PATH)

find_library(JPEG_LIBRARY NAMES libjpeg.a jpeg
    PATHS ${DEPS_PREFIX}/lib NO_DEFAULT_PATH)
find_path(JPEG_INCLUDE_DIR jpeglib.h
    PATHS ${DEPS_PREFIX}/include NO_DEFAULT_PATH)
if(NOT JPEG_LIBRARY)
    find_package(JPEG REQUIRED)
    set(JPEG_LIBRARY ${JPEG_LIBRARIES})
else()
    message(STATUS "Using self-built libjpeg: ${JPEG_LIBRARY}")
endif()

find_library(Z_LIBRARY NAMES libz.a libzlibstatic.a z
    PATHS ${DEPS_PREFIX}/lib NO_DEFAULT_PATH)
if(NOT Z_LIBRARY)
    find_library(Z_LIBRARY z)
endif()
message(STATUS "zlib: ${Z_LIBRARY}")

find_library(GIF_LIBRARY NAMES libgif.a gif
    PATHS ${DEPS_PREFIX}/lib NO_DEFAULT_PATH)
find_path(GIF_INCLUDE_DIR gif_lib.h
    PATHS ${DEPS_PREFIX}/include NO_DEFAULT_PATH)
if(NOT GIF_LIBRARY)
    find_library(GIF_LIBRARY gif PATHS /opt/homebrew/lib)
    find_path(GIF_INCLUDE_DIR gif_lib.h PATHS /opt/homebrew/include)
endif()
message(STATUS "giflib: ${GIF_LIBRARY}")

# ---------- libmodernimage sources ----------

set(MODERNIMAGE_SOURCES
    src/modernimage.c
    src/jpeg_orientation.c
    src/bridge_cwebp.c
    src/bridge_gif2webp.c
    src/bridge_avifenc.c
    src/bridge_jpegtran.c
    ${WEBP_DIR}/examples/gifdec.c
    ${JPEG_DIR}/src/cdjpeg.c
    ${JPEG_DIR}/src/rdswitch.c
    ${JPEG_DIR}/src/transupp.c
)

# ---------- Include directories ----------

set(MODERNIMAGE_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${WEBP_DIR}
    ${WEBP_DIR}/src
    ${WEBP_DIR}/examples
    ${WEBP_DIR}/imageio
    ${AVIF_DIR}/include
    ${AVIF_DIR}/apps/shared
    ${AVIF_DIR}/third_party/iccjpeg
    ${JPEG_DIR}/src
    ${JPEG_BUILD}
    ${PNG_INCLUDE_DIR}
    ${JPEG_INCLUDE_DIR}
    ${GIF_INCLUDE_DIR}
    ${DEPS_PREFIX}/include
)

# ---------- Static libraries to link ----------

# Order: consumers before providers (Linux ld is order-sensitive)
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
    ${AVIF_BUILD}/libavif_apps.a
    ${AVIF_BUILD}/libavif_internal.a
    ${AOM_BUILD}/libaom.a
)

# Image format libs (self-built or system)
set(IMAGE_LIBS
    ${PNG_LIBRARY}
    ${JPEG_LIBRARY}
    ${GIF_LIBRARY}
    ${Z_LIBRARY}
)

# ---------- Thin static library ----------

add_library(modernimage_objs STATIC ${MODERNIMAGE_SOURCES})
target_include_directories(modernimage_objs PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${MODERNIMAGE_INCLUDE_DIRS}
)
target_compile_definitions(modernimage_objs PRIVATE WEBP_HAVE_GIF WEBP_HAVE_PNG WEBP_HAVE_JPEG JPEG_INTERNAL_OPTIONS)
set_target_properties(modernimage_objs PROPERTIES OUTPUT_NAME modernimage_objs)

# ---------- Fat static library via cmake --install ----------

set(FAT_STATIC_LIB ${CMAKE_BINARY_DIR}/libmodernimage.a)

install(CODE "
    message(STATUS \"Combining static libraries into libmodernimage.a...\")

    set(LIBS_TO_COMBINE
        \"${CMAKE_BINARY_DIR}/libmodernimage_objs.a\"
        \"${WEBP_BUILD}/libimagedec.a\"
        \"${WEBP_BUILD}/libimageenc.a\"
        \"${WEBP_BUILD}/libimageioutil.a\"
        \"${WEBP_BUILD}/libexampleutil.a\"
        \"${WEBP_BUILD}/libwebpmux.a\"
        \"${WEBP_BUILD}/libwebpdemux.a\"
        \"${WEBP_BUILD}/libwebp.a\"
        \"${WEBP_BUILD}/libwebpdecoder.a\"
        \"${WEBP_BUILD}/libsharpyuv.a\"
        \"${AVIF_BUILD}/libavif_apps.a\"
        \"${AVIF_BUILD}/libavif_internal.a\"
        \"${AOM_BUILD}/libaom.a\"
        \"${PNG_LIBRARY}\"
        \"${JPEG_LIBRARY}\"
        \"${GIF_LIBRARY}\"
        \"${Z_LIBRARY}\"
    )

    # Step 1: Combine all static libraries into a raw fat archive
    set(RAW_FAT_LIB \"${CMAKE_BINARY_DIR}/libmodernimage_raw.a\")
    if(APPLE)
        execute_process(
            COMMAND libtool -static -o \"\${RAW_FAT_LIB}\" \${LIBS_TO_COMBINE}
        )
    else()
        set(MRI_SCRIPT \"${CMAKE_BINARY_DIR}/combine.mri\")
        file(WRITE \"\${MRI_SCRIPT}\" \"CREATE \${RAW_FAT_LIB}\\n\")
        foreach(LIB \${LIBS_TO_COMBINE})
            file(APPEND \"\${MRI_SCRIPT}\" \"ADDLIB \${LIB}\\n\")
        endforeach()
        file(APPEND \"\${MRI_SCRIPT}\" \"SAVE\\nEND\\n\")
        execute_process(
            COMMAND ar -M
            INPUT_FILE \"\${MRI_SCRIPT}\"
        )
        execute_process(COMMAND ranlib \"\${RAW_FAT_LIB}\")
    endif()

    # Step 2: Localize all symbols except modernimage_* public API.
    # Uses -all_load to force inclusion of all objects (avoids ar x duplicate name issue).
    message(STATUS \"Localizing internal symbols...\")
    execute_process(
        COMMAND bash \"${CMAKE_SOURCE_DIR}/scripts/localize-symbols.sh\"
            \"\${RAW_FAT_LIB}\" \"${FAT_STATIC_LIB}\"
        RESULT_VARIABLE LOCALIZE_RESULT
    )
    if(NOT LOCALIZE_RESULT EQUAL 0)
        message(FATAL_ERROR \"localize-symbols.sh failed with exit code \${LOCALIZE_RESULT}\")
    endif()
    file(REMOVE \"\${RAW_FAT_LIB}\")

    message(STATUS \"Created: ${FAT_STATIC_LIB} (symbols localized)\")
")

# ---------- Shared library ----------

add_library(modernimage_shared SHARED ${MODERNIMAGE_SOURCES})
target_include_directories(modernimage_shared PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${MODERNIMAGE_INCLUDE_DIRS}
)
target_compile_definitions(modernimage_shared PRIVATE WEBP_HAVE_GIF WEBP_HAVE_PNG WEBP_HAVE_JPEG JPEG_INTERNAL_OPTIONS)
set_target_properties(modernimage_shared PROPERTIES OUTPUT_NAME modernimage)

# Shared lib embeds all deps statically
target_link_libraries(modernimage_shared PRIVATE
    ${WEBP_LIBS}
    ${AVIF_LIBS}
    ${IMAGE_LIBS}
    Threads::Threads
)
if(NOT WIN32)
    target_link_libraries(modernimage_shared PRIVATE m)
endif()
if(UNIX AND NOT APPLE)
    target_link_options(modernimage_shared PRIVATE
        -Wl,--exclude-libs,ALL
        -static-libgcc
    )
elseif(MINGW)
    target_link_options(modernimage_shared PRIVATE -static-libgcc)
endif()

# ---------- Tests ----------

# Full link order: our code → tool libs → codec → image format libs → system
set(ALL_STATIC_LIBS
    modernimage_objs
    ${WEBP_LIBS}
    ${AVIF_LIBS}
    ${IMAGE_LIBS}
    ${WEBP_LIBS}
    ${IMAGE_LIBS}
    Threads::Threads
)
if(NOT WIN32)
    list(APPEND ALL_STATIC_LIBS m)
endif()

add_executable(test_binary_equiv tests/test_binary_equiv.c)
target_include_directories(test_binary_equiv PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_binary_equiv PRIVATE ${ALL_STATIC_LIBS})

add_executable(test_thread_safety tests/test_thread_safety.c)
target_include_directories(test_thread_safety PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_thread_safety PRIVATE ${ALL_STATIC_LIBS})

enable_testing()
add_test(NAME binary_equiv COMMAND test_binary_equiv)
add_test(NAME thread_safety COMMAND test_thread_safety)

# ---------- Fat static library link test ----------
# This test links ONLY against the installed fat libmodernimage.a (not individual deps).
# It catches symbol resolution issues that only appear for downstream consumers.
# Built as a custom command after cmake --install creates the fat .a.

set(FAT_LINK_TEST_SRC ${CMAKE_SOURCE_DIR}/tests/test_fat_link.c)
set(FAT_LINK_TEST_BIN ${CMAKE_BINARY_DIR}/test_fat_link)

set(FAT_LINK_SYSTEM_LIBS "")
if(APPLE)
    set(FAT_LINK_SYSTEM_LIBS "-lc++ -lpthread -lm")
elseif(MINGW)
    set(FAT_LINK_SYSTEM_LIBS "-lstdc++ -lws2_32 -lole32 -lshlwapi")
else()
    set(FAT_LINK_SYSTEM_LIBS "-lstdc++ -lpthread -lm")
endif()

install(CODE "
    message(STATUS \"Building fat link test...\")
    execute_process(
        COMMAND bash -c \"${CMAKE_C_COMPILER} -o '${FAT_LINK_TEST_BIN}' '${FAT_LINK_TEST_SRC}' -I'${CMAKE_SOURCE_DIR}/include' '${FAT_STATIC_LIB}' ${FAT_LINK_SYSTEM_LIBS}\"
        RESULT_VARIABLE FAT_LINK_RESULT
        OUTPUT_VARIABLE FAT_LINK_OUTPUT
        ERROR_VARIABLE FAT_LINK_ERROR
    )
    if(NOT FAT_LINK_RESULT EQUAL 0)
        message(FATAL_ERROR
            \"Fat link test FAILED to compile/link.\\n\"
            \"This means the fat libmodernimage.a has unresolved internal symbols.\\n\"
            \"Error:\\n\${FAT_LINK_ERROR}\")
    endif()
    message(STATUS \"Fat link test built successfully: ${FAT_LINK_TEST_BIN}\")
")
