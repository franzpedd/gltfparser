cmake_minimum_required(VERSION 3.10)

set(SOURCES
    include/gltfparser_defines.h
    source/gltfparser_json.c include/gltfparser_json.h
    include/gltfparser_types.h
    source/gltfparser_util.c include/gltfparser_util.h
    source/gltfparser.c include/gltfparser.h 
    include/jsmn.h source/jsmn.c
)

# add the include directory
include_directories(include)

# propagate the assertions to the code
if(GLTF_ENABLE_ASSERTS)
    add_compile_definitions(GLTF_ENABLE_ASSERTS)
endif()

# propagate the definitions to the dynamic library
if(GLTF_SHARED_LIBRARY)
    add_compile_definitions(GLTF_SHARED_LIBRARY)
    add_compile_definitions(GLTF_SHARED_LIBRARY_EXPORT)
    add_library(GLTFParser SHARED ${SOURCES})
else()
    add_library(GLTFParser ${SOURCES})
endif()

target_include_directories(GLTFParser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)