
# Strip quotes from all path variables if they exist
string(REPLACE "\"" "" EXECUTABLE_PATH "${EXECUTABLE_PATH}")
string(REPLACE "\"" "" OUTPUT_DIR "${OUTPUT_DIR}")
string(REPLACE "\"" "" QT_BIN_PATH "${QT_BIN_PATH}")
string(REPLACE "\"" "" MSYS2_BIN_PATH "${MSYS2_BIN_PATH}")
if(QML_SOURCE_DIR)
    string(REPLACE "\"" "" QML_SOURCE_DIR "${QML_SOURCE_DIR}")
endif()
string(REPLACE "\"" "" PROJECT_SOURCE_DIR "${PROJECT_SOURCE_DIR}")

message("=== Starting Windows deployment for: ${EXECUTABLE_PATH} ===")
message("Debug info:")
message("  EXECUTABLE_PATH: ${EXECUTABLE_PATH}")
message("  OUTPUT_DIR: ${OUTPUT_DIR}")
message("  QT_BIN_PATH: ${QT_BIN_PATH}")
message("  MSYS2_BIN_PATH: ${MSYS2_BIN_PATH}")

if(NOT EXISTS ${EXECUTABLE_PATH})
    message(STATUS "Executable not found: ${EXECUTABLE_PATH}")
    
    # Try to find the executable with different path formats
    get_filename_component(DIR_PATH ${EXECUTABLE_PATH} DIRECTORY)
    get_filename_component(FILE_NAME ${EXECUTABLE_PATH} NAME)
    
    message(STATUS "Directory path: ${DIR_PATH}")
    message(STATUS "File name: ${FILE_NAME}")
    
    # List contents of the directory
    if(EXISTS ${DIR_PATH})
        message(STATUS "Directory exists, listing contents:")
        file(GLOB DIR_CONTENTS "${DIR_PATH}/*")
        foreach(ITEM ${DIR_CONTENTS})
            message(STATUS "  Found: ${ITEM}")
        endforeach()
    else()
        message(STATUS "Directory does not exist: ${DIR_PATH}")
    endif()
    
    message(FATAL_ERROR "Executable not found: ${EXECUTABLE_PATH}")
endif()

message("SUCCESS: Executable found at: ${EXECUTABLE_PATH}")

message("Running windeployqt6 to deploy Qt dependencies (without compiler runtime)...")


# required if Qt is installed via MSYS2
set(ENV{PATH} "/c/msys64/mingw64/bin:/c/msys64/mingw64/share/qt6/bin:$ENV{PATH}")


message("Executing: ${QT_BIN_PATH}/windeployqt6.exe --qmldir ${QML_SOURCE_DIR} --dir ${OUTPUT_DIR} --plugindir ${OUTPUT_DIR}/plugins ${EXECUTABLE_PATH}")
execute_process(
    COMMAND ${QT_BIN_PATH}/windeployqt6.exe --qmldir ${QML_SOURCE_DIR} --dir ${OUTPUT_DIR} --plugindir ${OUTPUT_DIR}/plugins ${EXECUTABLE_PATH}
    RESULT_VARIABLE WINDEPLOYQT_RESULT
    OUTPUT_VARIABLE WINDEPLOYQT_OUTPUT
    ERROR_VARIABLE WINDEPLOYQT_ERROR
)

if(NOT WINDEPLOYQT_RESULT EQUAL 0)
    message(WARNING "windeployqt6 failed: ${WINDEPLOYQT_ERROR}")
else()
    message("windeployqt6 completed successfully")
endif()

# Step 2: Find and copy runtime dependencies using GET_RUNTIME_DEPENDENCIES
message("Analyzing runtime dependencies for: ${EXECUTABLE_PATH}")

# Get the build directory to exclude DLLs already there
get_filename_component(BUILD_DIR ${EXECUTABLE_PATH} DIRECTORY)

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES ${EXECUTABLE_PATH}
    RESOLVED_DEPENDENCIES_VAR DLLS
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-" "^AVRT" "^avrt" "^MSVCP" "^VCRUNTIME" "^ucrtbase" "^libgcc_s_seh-1\\.dll$" "^libstdc\\+\\+-6\\.dll$" "^libwinpthread-1\\.dll$" "^Qt.*\\.dll$" "^libgstreamer-1\\.0-0\\.dll$" "^libgstbase-1\\.0-0\\.dll$" "^libgobject-2\\.0-0\\.dll$" "^libglib-2\\.0-0\\.dll$" "^libintl-8\\.dll$" "^libiconv-2\\.dll$"
    POST_EXCLUDE_REGEXES ".*system32/.*\\.dll" ".*SysWOW64/.*\\.dll" ".*Windows/.*\\.dll" ".*Microsoft.VC.*" ".*Qt.*\\.dll$"
    DIRECTORIES ${BUILD_DIR} ${QT_BIN_PATH} ${MSYS2_BIN_PATH} "C:/lxqt/lib" $ENV{PATH}
)

set(COPIED_DLLS 0)
foreach(DLL ${DLLS})
    get_filename_component(DLL_NAME ${DLL} NAME)
    get_filename_component(DLL_DIR ${DLL} DIRECTORY)
    
    # Skip if DLL is from the build directory (avoid copying to itself)
    if("${DLL_DIR}" STREQUAL "${BUILD_DIR}")
        message("  Skipping ${DLL_NAME} (already in build directory)")
        continue()
    endif()
    
    set(DEST_FILE "${OUTPUT_DIR}/${DLL_NAME}")
    
    # Check if we need to copy (file doesn't exist or is different)
    set(SHOULD_COPY TRUE)
    if(EXISTS ${DEST_FILE})
        file(SIZE ${DLL} SOURCE_SIZE)
        file(SIZE ${DEST_FILE} DEST_SIZE)
        if(SOURCE_SIZE EQUAL DEST_SIZE)
            file(TIMESTAMP ${DLL} SOURCE_TIME)
            file(TIMESTAMP ${DEST_FILE} DEST_TIME)
            if(NOT SOURCE_TIME IS_NEWER_THAN DEST_TIME)
                set(SHOULD_COPY FALSE)
            endif()
        endif()
    endif()
    
    if(SHOULD_COPY)
        message("  Copying dependency: ${DLL_NAME}")
        file(COPY ${DLL} DESTINATION ${OUTPUT_DIR})
        math(EXPR COPIED_DLLS "${COPIED_DLLS} + 1")
    else()
        message("  Skipping ${DLL_NAME} (already up to date)")
    endif()
endforeach()

list(LENGTH DLLS TOTAL_DLLS)
message("Processed ${TOTAL_DLLS} runtime dependencies, copied ${COPIED_DLLS} files")

message("Copying GStreamer plugins...")
set(GSTREAMER_PLUGIN_DIR "${MSYS2_BIN_PATH}/../lib/gstreamer-1.0")

set(WANTED_PLUGINS
    "libgstaudioconvert"
    "libgstvolume"
    "libgstcoreelements"
    "libgstautodetect"
    "libgstdirectsound"
    "libgstlibav"
    "libgstapp"
    "libgstlevel"
    "libgstwasapi"
    "libgstplayback"
    "libgstaudioresample"
    "libgstaudiomixer"
    "libgstaudiotestsrc"
    "libgstmediafoundation"
    "libgstdecodebin"
    "libgsttypefindfunctions"
    "libgstvideoscale"
    "libgstvideoconvert"
    "libgstvideorate"
    "libgstoverlaycomposition"
    "libgstfaad"
    "libgstvideoparsersbad"
    "libgstvideofilter"
    "libgstvideoconvertscale"
    "libgstmultifile"
    "libgstjpeg"
)

file(MAKE_DIRECTORY "${OUTPUT_DIR}/gstreamer-1.0")
set(COPIED_PLUGIN_COUNT 0)
foreach(BASENAME ${WANTED_PLUGINS})
    # match any versioned filename starting with the basename
    file(GLOB MATCHES "${GSTREAMER_PLUGIN_DIR}/${BASENAME}*.dll")
    if(MATCHES)
        foreach(PLUGIN_PATH ${MATCHES})
            get_filename_component(PLUGIN_NAME ${PLUGIN_PATH} NAME)
            message("Copying GStreamer plugin: ${PLUGIN_NAME}")
            file(COPY "${PLUGIN_PATH}" DESTINATION "${OUTPUT_DIR}/gstreamer-1.0")
            math(EXPR COPIED_PLUGIN_COUNT "${COPIED_PLUGIN_COUNT} + 1")
        endforeach()
    else()
        message(WARNING "Requested GStreamer plugin not found: ${BASENAME} (searched ${GSTREAMER_PLUGIN_DIR})")
    endif()
endforeach()

message("Successfully copied ${COPIED_PLUGIN_COUNT} requested GStreamer plugins")

set(ADDITIONAL_DLLS
    "libgcc_s_seh-1.dll"
    "libstdc++-6.dll"
    "libwinpthread-1.dll"
    "libgstreamer-1.0-0.dll"
    "libgstbase-1.0-0.dll"
    "libgstcodecparsers-1.0-0.dll"
    "libgstcodecs-1.0-0.dll"
    "libgobject-2.0-0.dll"
    "libglib-2.0-0.dll"
    "libintl-8.dll"
    "libiconv-2.dll"
    "libfdk-aac-2.dll"
    "libfaad-2.dll"
    "avcodec-61.dll"
    "avformat-61.dll"
    "avutil-59.dll"
    "swresample-5.dll"
    "swscale-8.dll"
    "avfilter-11.dll"
    "avfilter-10.dll"
    "libopenal-1.dll"
    "libgstaudio-1.0-0.dll"
    "libgstvideo-1.0-0.dll"
    "liborc-0.4-0.dll"
    "libgstpbutils-1.0-0.dll"
    "libgsttag-1.0-0.dll"
    "libgstlibav.dll"
    "libass-9.dll"
    "libfontconfig-1.dll"
    "libharfbuzz-0.dll"
    "libexpat-1.dll"
    "libfreetype-6.dll"
    "libpng16-16.dll"
    "libgraphite2.dll"
    "libfribidi-0.dll"
    "libunibreak-6.dll"
    "liblcms2-2.dll"
    "libvpl-2.dll"
    "libzimg-2.dll"
    "libdovi.dll"
    "libshaderc_shared.dll"
    "vulkan-1.dll"
    "libvidstab.dll"
    "libgomp-1.dll"
    "postproc-58.dll"
    "libplacebo-351.dll"
    "libspirv-cross-c-shared.dll"
    "libva.dll"
    "libxml2-16.dll"
    "libva_win32.dll"
    "libpcre2-8-0.dll"
    "libffi-8.dll"
    "libgmodule-2.0-0.dll"
    "libhwy.dll"
    "libmp3lame-0.dll"
    "librsvg-2-2.dll"
    "libwebp-7.dll"
    "libthai-0.dll"
    "libjxl.dll"
    "libdatrie-1.dll"
    "libwebpmux-3.dll"
    "libx264-164.dll"
    "libtasn1-6.dll"
    "libgsm.dll"
    "libcairo-gobject-2.dll"
    "libvorbis-0.dll"
    "libgio-2.0-0.dll"
    "libgmp-10.dll"
    "libmodplug-1.dll"
    "libopus-0.dll"
    "libpangowin32-1.0-0.dll"
    "libspeex-1.dll"
    "libogg-0.dll"
    "libzvbi-0.dll"
    "libpixman-1-0.dll"
    "libsrt.dll"
    "libjxl_threads.dll"
    "libgnutls-30.dll"
    "libp11-kit-0.dll"
    "libopencore-amrwb-0.dll"
    "libtheoradec-2.dll"
    "libvpx-1.dll"
    "libgme.dll"
    "libhogweed-6.dll"
    "liblc3-1.dll"
    "libpango-1.0-0.dll"
    "xvidcore.dll"
    "libopencore-amrnb-0.dll"
    "libtiff-6.dll"
    "libxml2-2.dll"
    "libjbig-0.dll"
    "libLerc.dll"
    "libjxl_cms.dll"
    "libgdk_pixbuf-2.0-0.dll"
    "libvorbisenc-2.dll"
    "libsoxr.dll"
    "librtmp-1.dll"
    "libcairo-2.dll"
    "libdeflate.dll"
    "libpangocairo-1.0-0.dll"
    "libpangoft2-1.0-0.dll"
    "libtheoraenc-2.dll"
    "libbluray-2.dll"
    "libnettle-8.dll"
    "libunistring-5.dll"
    "libidn2-0.dll"
)

message("Copying additional MinGW runtime DLLs from MSYS2...")
foreach(DLL_NAME ${ADDITIONAL_DLLS})
    set(DLL_PATH "${MSYS2_BIN_PATH}/${DLL_NAME}")
    if(EXISTS ${DLL_PATH})
        message("Copying additional DLL: ${DLL_NAME}")
        file(COPY ${DLL_PATH} DESTINATION ${OUTPUT_DIR})
    else()
        message(WARNING "Additional DLL not found: ${DLL_NAME} (searched ${MSYS2_BIN_PATH})")
    endif()
endforeach()


# Required for win-ifuse and iproxy since we moved from libimobiledevice
# and these are not dependencies of the main executable
set(LIBIMOBILEDEVICE_DLLS
    "libimobiledevice-1.0.dll"
    "libimobiledevice-glue-1.0.dll"
    "libusbmuxd-2.0.dll"
    "libplist-2.0.dll"
    "libssl-3-x64.dll"
)

foreach(DLL_NAME ${LIBIMOBILEDEVICE_DLLS})
    set(DLL_PATH "${MSYS2_BIN_PATH}/${DLL_NAME}")
    if(EXISTS ${DLL_PATH})
        message("Copying libimobiledevice DLL: ${DLL_NAME}")
        file(COPY ${DLL_PATH} DESTINATION ${OUTPUT_DIR})
    else()
        message(WARNING "libimobiledevice DLL not found: ${DLL_NAME} (searched ${MSYS2_BIN_PATH})")
    endif()
endforeach()


message("Copying GStreamer helper executables...")
set(GST_LIBEXEC_PATH "${MSYS2_BIN_PATH}/../libexec/gstreamer-1.0")
file(COPY "${GST_LIBEXEC_PATH}/gst-plugin-scanner.exe" DESTINATION "${OUTPUT_DIR}/gstreamer-1.0/libexec")

message("Copying executables")
file(COPY C:/msys64/mingw64/bin/iproxy.exe DESTINATION ${OUTPUT_DIR})

message("Copying required scripts")
file(COPY "${PROJECT_SOURCE_DIR}/install-apple-drivers.ps1" DESTINATION ${OUTPUT_DIR})
file(COPY "${PROJECT_SOURCE_DIR}/install-win-fsp.silent.bat" DESTINATION ${OUTPUT_DIR})

message("Copying win-ifuse executable")
file(COPY "${WIN_IFUSE}" DESTINATION ${OUTPUT_DIR})

message("Copying winfsp-x64.dll")
file(COPY "C:/Program Files (x86)/WinFsp/bin/winfsp-x64.dll" DESTINATION ${OUTPUT_DIR})

message("=== Windows deployment completed ===")
