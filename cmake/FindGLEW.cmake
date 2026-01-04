# Custom FindGLEW.cmake that uses our FetchContent-built GLEW
# This file is found before CMake's built-in FindGLEW.cmake

if(TARGET libglew_static)
    # GLEW was built via FetchContent (glew-cmake)
    set(GLEW_FOUND TRUE)

    # Create the expected target aliases if they don't exist
    if(NOT TARGET GLEW::glew_s)
        add_library(GLEW::glew_s ALIAS libglew_static)
    endif()
    if(NOT TARGET GLEW::GLEW)
        add_library(GLEW::GLEW ALIAS libglew_static)
    endif()

    # Get include directory from the target
    get_target_property(GLEW_INCLUDE_DIRS libglew_static INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT GLEW_INCLUDE_DIRS)
        # Fallback to source directory
        FetchContent_GetProperties(glew)
        set(GLEW_INCLUDE_DIRS "${glew_SOURCE_DIR}/include")
    endif()

    set(GLEW_LIBRARIES GLEW::glew_s)

    message(STATUS "Using FetchContent GLEW: ${GLEW_INCLUDE_DIRS}")
else()
    # Fall back to system GLEW
    include(${CMAKE_ROOT}/Modules/FindGLEW.cmake)
endif()
