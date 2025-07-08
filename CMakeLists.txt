cmake_minimum_required(VERSION 3.22)
project(jucyaudio VERSION 0.3.0)

# C++20 requirement
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find or fetch JUCE
include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.8
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

# Platform-specific library paths and includes
if(WIN32)
    # Windows paths from jucer file
    set(AUBIO_LIB_PATH "C:/Projects/jucyaudio_deps_win32_x64/aubio/lib")
    set(SPDLOG_LIB_PATH_DEBUG "C:/Projects/jucyaudio_deps_win32_x64/spdlog/out/install/x64-Debug/lib")
    set(SPDLOG_LIB_PATH_RELEASE "C:/Projects/jucyaudio_deps_win32_x64/spdlog/out/install/x64-Release/lib")
    set(TAGLIB_LIB_PATH_DEBUG "C:/Projects/jucyaudio_deps_win32_x64/taglib/out/install/x64-Debug/lib")
    set(TAGLIB_LIB_PATH_RELEASE "C:/Projects/jucyaudio_deps_win32_x64/taglib/out/install/x64-Release/lib")
    set(LAME_LIB_PATH "C:/Projects/jucyaudio_deps_win32_x64/lame")
    set(PTHREADS_LIB_PATH "C:/Projects/jucyaudio_deps_win32_x64/pthreads-w32/lib")
    set(FFTW_LIB_PATH "C:/Projects/jucyaudio_deps_win32_x64/fftw")
    set(LIBSNDFILE_LIB_PATH "C:/Projects/jucyaudio_deps_win32_x64/libsndfile/lib")
    set(FFMPEG_LIB_PATH "C:/Projects/jucyaudio_deps_win32_x64/ffmpeg/lib")  # Adjust path as needed

    
    set(AUBIO_INCLUDE_PATH "C:/Projects/jucyaudio_deps_win32_x64/aubio/include")
    set(SPDLOG_INCLUDE_PATH_DEBUG "C:/Projects/jucyaudio_deps_win32_x64/spdlog/out/install/x64-Debug/include")
    set(SPDLOG_INCLUDE_PATH_RELEASE "C:/Projects/jucyaudio_deps_win32_x64/spdlog/out/install/x64-Release/include")
    set(TAGLIB_INCLUDE_PATH_DEBUG "C:/Projects/jucyaudio_deps_win32_x64/taglib/out/install/x64-Debug/include")
    set(TAGLIB_INCLUDE_PATH_RELEASE "C:/Projects/jucyaudio_deps_win32_x64/taglib/out/install/x64-Release/include")
    set(LAME_INCLUDE_PATH "C:/Projects/jucyaudio_deps_win32_x64/lame/include")
    set(TOMLPLUSPLUS_INCLUDE_PATH "C:/Projects/jucyaudio_deps_win32_x64/tomlplusplus/include")
    set(PTHREADS_INCLUDE_PATH "C:/Projects/jucyaudio_deps_win32_x64/pthreads-w32/include")
    set(FFTW_INCLUDE_PATH "C:/Projects/jucyaudio_deps_win32_x64/fftw")
    set(LIBSNDFILE_INCLUDE_PATH "C:/Projects/libsndfile/include")
    set(FFMPEG_INCLUDE_PATH "C:/Projects/jucyaudio_deps_win32_x64/ffmpeg/include")
    
    # Use debug or release paths based on build type
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(SPDLOG_LIB_PATH ${SPDLOG_LIB_PATH_DEBUG})
        set(TAGLIB_LIB_PATH ${TAGLIB_LIB_PATH_DEBUG})
        set(SPDLOG_INCLUDE_PATH ${SPDLOG_INCLUDE_PATH_DEBUG})
        set(TAGLIB_INCLUDE_PATH ${TAGLIB_INCLUDE_PATH_DEBUG})
    else()
        set(SPDLOG_LIB_PATH ${SPDLOG_LIB_PATH_RELEASE})
        set(TAGLIB_LIB_PATH ${TAGLIB_LIB_PATH_RELEASE})
        set(SPDLOG_INCLUDE_PATH ${SPDLOG_INCLUDE_PATH_RELEASE})
        set(TAGLIB_INCLUDE_PATH ${TAGLIB_INCLUDE_PATH_RELEASE})
    endif()
    
    # Find libraries on Windows
    find_library(AUBIO_LIBRARY aubio PATHS ${AUBIO_LIB_PATH} REQUIRED)
    find_library(SPDLOG_LIBRARY spdlog PATHS ${SPDLOG_LIB_PATH} REQUIRED)
    find_library(TAGLIB_LIBRARY tag PATHS ${TAGLIB_LIB_PATH} REQUIRED)
    find_library(PTHREADS_LIBRARY pthreadVC2 PATHS ${PTHREADS_LIB_PATH} REQUIRED)
    find_library(LAME_LIBRARY mp3lame PATHS ${LAME_LIB_PATH} REQUIRED)
    find_library(FFTW3F_LIBRARY libfftw3f-3 PATHS ${FFTW_LIB_PATH} REQUIRED)
    find_library(FFTW3_LIBRARY libfftw3-3 PATHS ${FFTW_LIB_PATH})
    find_library(LIBSNDFILE_LIBRARY libsndfile-1 PATHS ${LIBSNDFILE_LIB_PATH} REQUIRED)
    
    # Find FFmpeg libraries
    find_library(AVCODEC_LIBRARY avcodec PATHS ${FFMPEG_LIB_PATH} REQUIRED)
    find_library(AVFORMAT_LIBRARY avformat PATHS ${FFMPEG_LIB_PATH} REQUIRED)
    find_library(AVUTIL_LIBRARY avutil PATHS ${FFMPEG_LIB_PATH} REQUIRED)
    find_library(SWRESAMPLE_LIBRARY swresample PATHS ${FFMPEG_LIB_PATH} REQUIRED)
    find_library(LAME_LIBRARY libmp3lame PATHS ${LAME_LIB_PATH} REQUIRED)

    # Windows external includes
    set(EXTERNAL_INCLUDES
        ${AUBIO_INCLUDE_PATH}
        ${SPDLOG_INCLUDE_PATH}
        ${TAGLIB_INCLUDE_PATH}
        ${LAME_INCLUDE_PATH}
        ${TOMLPLUSPLUS_INCLUDE_PATH}
        ${PTHREADS_INCLUDE_PATH}
        ${FFTW_INCLUDE_PATH}
        ${LIBSNDFILE_INCLUDE_PATH}
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    
    # Find pkg-config only on non-Windows platforms
else()
    find_package(PkgConfig REQUIRED)
    
    # macOS/Linux paths (original)
    find_library(AUBIO_LIBRARY aubio PATHS /Users/gersonkurz/development/aubio_install_macos/lib)
    find_library(SPDLOG_LIBRARY spdlog PATHS /Users/gersonkurz/development/spdlog/build)
    find_library(TAGLIB_LIBRARY tag PATHS /Users/gersonkurz/development/taglib_install/lib)
    find_library(MP3LAME_LIBRARY mp3lame PATHS /opt/homebrew/lib)
    
    # macOS/Linux external includes
    set(EXTERNAL_INCLUDES
        /Users/gersonkurz/development/aubio_install_macos/include
        /Users/gersonkurz/development/spdlog/include
        /Users/gersonkurz/development/taglib_install/include
        /Users/gersonkurz/development/tomlplusplus-master/include
        /opt/homebrew/include
    )
endif()

# Create the main application
juce_add_gui_app(jucyaudio
    PRODUCT_NAME "jucyaudio"
    BUNDLE_ID "com.p-nand-q.jucyaudio"
    VERSION 0.3.0
)

# Platform-specific deployment target
if(APPLE)
    set_target_properties(jucyaudio PROPERTIES
        MACOS_DEPLOYMENT_TARGET 15.0
    )
endif()

# Add all source files
target_sources(jucyaudio PRIVATE
    # Config files
    Config/section.cpp
    Config/config_backend.h
    Config/toml_backend.h
    Config/section.h
    Config/typed_value.h
    Config/typed_vector_value.h
    Config/value_interface.h
    
    # Audio files
    Audio/ExportMixToWav.cpp
    Audio/AudioLibrary.h
    Audio/ExportMixImplementation.cpp
    Audio/ExportMixImplementation.h
    Audio/ExportMixToMp3.cpp
    Audio/ExportMixToMp3.h
    Audio/ExportMixToWav.cpp
    Audio/ExportMixToWav.h
    Audio/MixExporter.cpp
    Audio/MixExporter.h
    Audio/Includes/IMixExporter.h
    Audio/MixProjectLoader.cpp
    Audio/MixProjectLoader.h
    
    # Utils files
    Utils/AssortedUtils.cpp
    Utils/StringWriter.cpp
    Utils/UiUtils.cpp
    Utils/AssortedUtils.h
    Utils/StringWriter.h
    Utils/UiUtils.h
    
    # Database files
    Database/TrackLibrary.cpp
    Database/TrackScanner.cpp
    Database/TrackLibrary.h
    Database/TrackScanner.h
    Database/BackgroundService.cpp
    Database/BackgroundService.h
    
    # Background Tasks
    Database/BackgroundTasks/BpmAnalysis.cpp
    Database/BackgroundTasks/BpmAnalysis.h
    
    # Database includes
    UI/ILongRunningTask.h
    Database/Includes/Constants.h
    Database/Includes/DataColumn.h
    Database/Includes/FolderInfo.h
    Database/Includes/IBackgroundTask.h
    Database/Includes/IFolderDatabase.h
    Database/Includes/IMixManager.h
    Database/Includes/INavigationNode.h
    Database/Includes/IRefCounted.h
    Database/Includes/ITagManager.h
    Database/Includes/ITrackDatabase.h
    Database/Includes/ITrackInfoScanner.h
    Database/Includes/IWorkingSetManager.h
    Database/Includes/MixInfo.h
    Database/Includes/TrackInfo.h
    Database/Includes/TrackQueryArgs.h
    
    # SQLite files
    Database/Sqlite/sqlite3.c
    Database/Sqlite/sqlite3.h
    Database/Sqlite/sqlite3ext.h
    Database/Sqlite/SqliteDatabase.cpp
    Database/Sqlite/SqliteDatabase.h
    Database/Sqlite/SqliteFolderDatabase.cpp
    Database/Sqlite/SqliteFolderDatabase.h
    Database/Sqlite/SqliteMixManager.cpp
    Database/Sqlite/SqliteMixManager.h
    Database/Sqlite/SqliteStatement.cpp
    Database/Sqlite/SqliteStatement.h
    Database/Sqlite/SqliteStatementConstruction.cpp
    Database/Sqlite/SqliteStatementConstruction.h
    Database/Sqlite/SqliteTagManager.cpp
    Database/Sqlite/SqliteTagManager.h
    Database/Sqlite/SqliteTrackDatabase.cpp
    Database/Sqlite/SqliteTrackDatabase.h
    Database/Sqlite/SqliteTransaction.cpp
    Database/Sqlite/SqliteTransaction.h
    Database/Sqlite/SqliteWorkingSetManager.cpp
    Database/Sqlite/SqliteWorkingSetManager.h
    
    # Database Nodes
    Database/Nodes/WorkingSetsOverview.cpp
    Database/Nodes/BaseNode.cpp
    Database/Nodes/LibraryNode.cpp
    Database/Nodes/MixesOverview.cpp
    Database/Nodes/LogicalFolderNode.cpp
    Database/Nodes/MixNode.cpp
    Database/Nodes/RootNode.cpp
    Database/Nodes/WorkingSetNode.cpp
    Database/Nodes/TypedItemsOverview.h
    Database/Nodes/TypedOverviewNode.h
    Database/Nodes/WorkingSetsOverview.h
    Database/Nodes/BaseNode.h
    Database/Nodes/LibraryNode.h
    Database/Nodes/MixesOverview.h
    Database/Nodes/LogicalFolderNode.h
    Database/Nodes/MixNode.h
    Database/Nodes/RootNode.h
    Database/Nodes/TypedContainerNode.h
    Database/Nodes/WorkingSetNode.h
    
    # Database Scanners
    Database/Scanners/AubioScanner.cpp
    Database/Scanners/AubioScanner.h
    Database/Scanners/Id3TagScanner.cpp
    Database/Scanners/Id3TagScanner.h
    
    # UI files
    UI/ColumnConfiguratorDialog.cpp
    UI/DynamicColumnManager.cpp
    UI/Settings.cpp
    UI/CreateMixDialogComponent.cpp
    UI/CreateWorkingSetDialogComponent.cpp
    UI/DataViewComponent.cpp
    UI/DividerComponent.cpp
    UI/DynamicToolbarComponent.cpp
    UI/TaskDialog.cpp
    UI/Main.cpp
    UI/MainComponent.cpp
    UI/MainPlaybackAndStatusComponent.cpp
    UI/NavigationPanelComponent.cpp
    UI/PlaybackController.cpp
    UI/PlaybackToolbarComponent.cpp
    UI/ScanDialogComponent.cpp
    UI/ColumnConfiguratorDialog.h
    UI/DynamicColumnManager.h
    UI/Settings.h
    UI/CreateMixDialogComponent.h
    UI/CreateWorkingSetDialogComponent.h
    UI/DataViewComponent.h
    UI/DividerComponent.h
    UI/DynamicToolbarComponent.h
    UI/TaskDialog.h
    UI/MainComponent.h
    UI/MainPlaybackAndStatusComponent.h
    UI/NavigationPanelComponent.h
    UI/PlaybackController.h
    UI/PlaybackToolbarComponent.h
    UI/ScanDialogComponent.h
    UI/ThemeManager.cpp
    UI/ThemeManager.h
    UI/MenuManager.cpp
    UI/MenuManager.h
    UI/MenuPresenter.cpp
    UI/MenuPresenter.h
    UI/MixEditorComponent.cpp
    UI/MixEditorComponent.h
    UI/MixTrackComponent.cpp
    UI/MixTrackComponent.h
    UI/TimelineComponent.cpp
    UI/TimelineComponent.h
)

# Add binary resources (SVG files)
juce_add_binary_data(jucyaudio_assets
    SOURCES
        Assets/pause.svg
        Assets/play_arrow.svg
        Assets/repeat.svg
        Assets/repeat_all.svg
        Assets/repeat_one.svg
        Assets/shuffle.svg
        Assets/shuffle_on.svg
        Assets/stop.svg
        Assets/volume_down.svg
        Assets/volume_off.svg
        Assets/volume_up.svg
)

# Platform-specific library linking
if(WIN32)
    target_link_libraries(jucyaudio PRIVATE
        # JUCE modules
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        
        # Binary resources
        jucyaudio_assets
        
        # External libraries (Windows)
        ${AUBIO_LIBRARY}
        ${SPDLOG_LIBRARY}
        ${TAGLIB_LIBRARY}
        ${PTHREADS_LIBRARY}
        ${LAME_LIBRARY}
        ${FFTW3F_LIBRARY}
        ${LIBSNDFILE_LIBRARY}

    )
else()
    target_link_libraries(jucyaudio PRIVATE
        # JUCE modules
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        
        # Binary resources
        jucyaudio_assets
        
        # External libraries (macOS/Linux)
        ${AUBIO_LIBRARY}
        ${SPDLOG_LIBRARY}
        ${TAGLIB_LIBRARY}
        ${MP3LAME_LIBRARY}
        
        z  # zlib
    )
endif()

# Include directories
target_include_directories(jucyaudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${EXTERNAL_INCLUDES}
)

# Platform-specific preprocessor definitions
if(WIN32)
    target_compile_definitions(jucyaudio PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:jucyaudio,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:jucyaudio,JUCE_VERSION>"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_USE_MP3AUDIOFORMAT=1
        SPDLOG_COMPILED_LIB
        SPDLOG_USE_STD_FORMAT
        SPDLOG_MSVC_UTF8
        TAGLIB_STATIC
        PROJECT_NAME="${PROJECT_NAME}"
        PROJECT_VERSION="${PROJECT_VERSION}"
    )
    
    # Windows-specific compiler flags
    target_compile_options(jucyaudio PRIVATE
        /utf-8
    )
else()
    target_compile_definitions(jucyaudio PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:jucyaudio,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:jucyaudio,JUCE_VERSION>"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_USE_MP3AUDIOFORMAT=1
        SPDLOG_COMPILED_LIB
        TAGLIB_STATIC
        PROJECT_NAME="${PROJECT_NAME}"
        PROJECT_VERSION="${PROJECT_VERSION}"
    )
endif()

# Platform-specific settings
if(APPLE)
    target_compile_definitions(jucyaudio PRIVATE
        JUCE_USE_OGGVORBIS=1
    )
endif()

# Compiler flags for non-Windows platforms
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(jucyaudio PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

# Special handling for sqlite3.c to avoid warnings
set_source_files_properties(Database/Sqlite/sqlite3.c PROPERTIES
    COMPILE_FLAGS "$<IF:$<PLATFORM_ID:Windows>,-w,-w>"
)

# --- Post-Build Step: Copy Resources (Modern Approach) ---

# Define a variable for the source directory for clarity
set(THEMES_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/themes")

if(APPLE)
    # On macOS, the generator expression for the Resources directory is clean.
    set(THEMES_DEST_DIR "$<TARGET_FILE_DIR:jucyaudio>/../Resources/themes")
elseif(WIN32)
    # On Windows, we want it next to the .exe file.
    set(THEMES_DEST_DIR "$<TARGET_FILE_DIR:jucyaudio>/themes")
else()
    # Sensible Linux fallback
    set(THEMES_DEST_DIR "$<TARGET_FILE_DIR:jucyaudio>/themes")
endif()

# Add a custom command that runs after the build.
# It uses generator expressions to get the final paths at build time.
add_custom_command(
    TARGET jucyaudio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${THEMES_DEST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${THEMES_SOURCE_DIR}" # Source
            "${THEMES_DEST_DIR}"   # Destination
    COMMENT "Copying themes to application bundle..."
)