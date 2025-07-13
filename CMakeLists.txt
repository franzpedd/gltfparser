cmake_minimum_required(VERSION 3.10)
project(Solution)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(GLTF_SHARED_LIBRARY "Dynamic Library build" OFF) # optionally builds as a dynamic library
option(GLTF_BUILD_EXAMPLE "Build the example application" OFF) # optionally builds the example application
option(GLTF_BUILD_TOOLS "Build the developer tools" OFF) # optionally builds the developer tools for the library

add_subdirectory(library) # the library itself

# the example application that helps me further develop the library, also an example on how to use it
if(GLTF_BUILD_EXAMPLE)

    # must check if we're testing a dynamic or static library
    if(GLTF_SHARED_LIBRARY)
        add_compile_definitions(GLTF_SHARED_LIBRARY)
    endif()

    add_executable(Example example.c)
    target_link_libraries(Example PRIVATE GLTFParser)
endif()

# the developer tools for the library
if(GLTF_BUILD_TOOLS)

    # must check if we're testing a dynamic or static library
    if(GLTF_SHARED_LIBRARY)
        add_compile_definitions(GLTF_SHARED_LIBRARY)
    endif()

    add_executable(Tools devtools.c)
    target_link_libraries(Tools PRIVATE GLTFParser)
endif()