#ifndef GLTFPARSER_INCLUDED
#define GLTFPARSER_INCLUDED

#include "gltfparser_defines.h"
#include "gltfparser_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief attempts to parse a gltf formatted file
/// @param path the disk path of the gltf file
/// @return a parsed output data
GLTF_API GLTF2 GLTF_ParseFromFile(const char* path);

/// @brief release the resources used by a GLTF2 object
/// @param data the gltf2 data
GLTF_API void GLTF_Free(GLTF2* data);

/// @brief get all errors that has happened since last parse has occured
/// @return the list of errors
GLTF_API const char* GLTF_GetErrors();

#ifdef __cplusplus
}
#endif

#endif // GLTFPARSER_INCLUDED