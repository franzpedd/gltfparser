#ifndef GLTFPARSER_JSON_INCLUDED
#define GLTFPARSER_JSON_INCLUDED

#include "gltfparser_defines.h"
#include "jsmn.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief compares a string and the json string
GLTF_API int json_strncmp(const char* data, const jsmntok_t* tok, const char* str);

/// @brief converts a json data in to an integer bool
GLTF_API int json_to_bool(const char* data, const jsmntok_t* tok);

/// @brief converts json data into an integer
GLTF_API int json_to_int(const char* data, jsmntok_t const* tok);

/// @brief converts json data into a float
GLTF_API float json_to_float(const char* data, const jsmntok_t* tok);

/// @brief converts a json data into a size_t
GLTF_API unsigned long long json_to_size(const char* data, const jsmntok_t* tok);

/// @brief parses a json string
GLTF_API int json_parse_string(const char* data, const jsmntok_t* tokens, int i, char** outString);

/// @brief parses the json array
GLTF_API int json_parse_array(const char* data, const jsmntok_t* tokens, int i, unsigned long long elementSize, void** outArr, unsigned long long* outSize);

/// @brief parses a json float array
GLTF_API int json_parse_array_float(const char* data, const jsmntok_t* tokens, int i, float* outArray, int size);

/// @brief parses a json strign array
GLTF_API int json_parse_array_string(const char* data, const jsmntok_t* tokens, int i, char*** outArr, unsigned long long* outSize);

/// @brief skips the parsing to next iteration
GLTF_API int json_parse_skip(const jsmntok_t* tokens, int i);

#ifdef __cplusplus
}
#endif

#endif // GLTFPARSER_JSON_INCLUDED