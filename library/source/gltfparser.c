#include "gltfparser.h"

#include "gltfparser_json.h"
#include "gltfparser_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/// @macro for checking token type
#define CHECK_TKTYPE(tk, tktype) if ((tk).type != (tktype)) { return GLTF_JsonInvalid; }

/// @macro for checking token key
#define CHECK_TKKEY(tk) if ((tk).type != JSMN_STRING || (tk).size == 0) { return GLTF_JsonInvalid; }

/// @macro expected GLB header size
#define GLB_HEADER_SIZE 12

/// @macro expected GLB header chunk size
#define GLB_CHUNK_HEADER_SIZE 8

/// @macro little-endian "glTF" 
#define GLB_MAGIC 0x46546C67 

/// @macro "JSON" em little-endian
#define JSON_CHUNK_TYPE 0x4E4F534A

/// @macro little-endian "BIN"
#define BIN_CHUNK_TYPE 0x004E4942

/// @brief hold all errors that has happened when parsing
static char s_gErrors[GLTF_LOG_BUFFER_SIZE] = { 0 };

// log an error message (appends to the buffer with newline)
void internal_log_error(const char* format, ...) {
	va_list args;
	va_start(args, format);

	size_t currentLen = strlen(s_gErrors);
	size_t remainingSpace = GLTF_LOG_BUFFER_SIZE - currentLen - 1; // -1 for null terminator

	if (remainingSpace > 1) {  // need space for at least 1 char + null terminator
		if (currentLen > 0) {
			s_gErrors[currentLen] = '\n';
			currentLen++;
			remainingSpace--;
		}

		vsnprintf(s_gErrors + currentLen, remainingSpace, format, args);
		s_gErrors[GLTF_LOG_BUFFER_SIZE - 1] = '\0'; // ensure null-termination
	}
	va_end(args);
}

// @brief assertions
#ifdef GLTF_ENABLE_ASSERTS
#include <assert.h>
#define GLTF_ASSERT(exp, msg) \
    do { \
        if (!(exp)) { \
			internal_log_error(msg); \
            fprintf(stderr, "[GLTF:Assertion] %s\n", msg); \
            abort(); \
        } \
    } while (0)
#else
#define GLTF_ASSERT(exp, msg) \
	if(!(exp)) { \
		internal_log_error(msg); \
		return -1; \
	}
#endif

/// @macro for converting index into a pointer 
#define PTR_TO_INDEX(type, idx) (type*)((unsigned long long)idx + 1)

/// @macro null check, bounds check and pointer adjustment.
#define PTR_FIX(var, data, size) do { \
    if (var) { \
        unsigned long long index = (size_t)((uintptr_t)var - 1); \
        if (index >= size) { \
            internal_log_error("Invalid pointer index %zu (max %zu)", index, size); \
            return 0; \
        } \
        var = &(data)[index]; \
    } \
} while (0)

/// @macro same as PTR_FIX but enforces not nullptr, for required fields
#define PTR_FIX_REQUIRED(var, data, size) do { \
    size_t index = (size_t)((uintptr_t)var - 1); \
    if (!var || index >= size) { \
        internal_log_error("Required pointer invalid (index %zu, max %zu)", index, size); \
        return 0; \
    } \
    var = &(data)[index]; \
} while (0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// parsing GLTF objects
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// @brief converts the read integer to a component type
/// @param data the json entire data
/// @param tok the json token to read
/// @return the component
static GLTF_ComponentType internal_json_to_component_type(const char* data, const jsmntok_t* tok) {
	int type = json_to_int(data, tok);

	switch (type)
	{
	case 5120: return ComponentType_R8;
	case 5121: return ComponentType_R8_UNSIGNED;
	case 5122: return ComponentType_R16;
	case 5123: return ComponentType_R16_UNSIGNED;
	case 5125: return ComponentType_R32_UNSIGNED;
	case 5126: return ComponentType_R32_FLOAT;
	}

	internal_log_error("An invalid component type has been parsed");
	return (GLTF_ComponentType)0;
}

/// @brief any extra information a gltf object may have will be dealed as a char*
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outExtra the output
/// @return the next token index to be analyzed
static int internal_parse_extras(const char* data, const jsmntok_t* tokens, int tkindex, char** outExtra) {
	unsigned long long start = tokens[tkindex].start;
	unsigned long long size = tokens[tkindex].end - start;
	*outExtra = (char*)gltfmemory_allocate(size + 1, 0);
	GLTF_ASSERT(outExtra, "Failed to allocate memory for extra data parsing");

	strncpy_impl(*outExtra, (const char*)data + start, size);
	(*outExtra)[size] = '\0';  // Fixed parentheses

	tkindex = json_parse_skip(tokens, tkindex);
	return tkindex;
}

/// @brief parses the extensions that are not processed yet
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outExtension the output extension info
/// @return the next token index to be analyzed
static int internal_parse_unprocessed_extension(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_Extension* outExtension) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING, "Unpprocessed extension token is not a string");
	GLTF_ASSERT(tokens[tkindex + 1].type == JSMN_OBJECT, "Unprocessed extension token value is not an object");

	unsigned long long nameLength = tokens[tkindex].end - tokens[tkindex].start;
	outExtension->name = (char*)gltfmemory_allocate(nameLength + 1, 0);
	GLTF_ASSERT(outExtension->name, "Failed to allocate memory for unprocessed extension name");

	strncpy_impl(outExtension->name, (const char*)data + tokens[tkindex].start, nameLength);
	outExtension->name[nameLength] = 0;
	tkindex++;

	unsigned long long start = tokens[tkindex].start;
	unsigned long long size = tokens[tkindex].end - start;
	outExtension->data = (char*)gltfmemory_allocate(size + 1, 0);
	GLTF_ASSERT(outExtension->data, "Failed to allocate memory for unprocessed extension data");

	strncpy_impl(outExtension->data, (const char*)data + start, size);
	outExtension->data[size] = '\0';
	tkindex = json_parse_skip(tokens, tkindex);
	return tkindex;
}

/// @brief handles unprocessed extensions
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outextensionsCount how many extensions the gltf file has
/// @param outExtensions the output
/// @return the next token index to be analyzed
static int internal_parse_unprocessed_extensions(const char* data, const jsmntok_t* tokens, int tkindex, unsigned long long* outextensionsCount, GLTF_Extension** outExtensions) {
	++tkindex;
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");

	int extensionsSize = tokens[tkindex].size;
	*outextensionsCount = 0;
	*outExtensions = (GLTF_Extension*)gltfmemory_allocate(sizeof(GLTF_Extension) * extensionsSize, 0);
	GLTF_ASSERT((*outExtensions), "Failed to allocate memory for extensions");

	++tkindex;
	for (int j = 0; j < extensionsSize; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		unsigned long long extensionIndex = (*outextensionsCount)++;
		GLTF_Extension* extension = &((*outExtensions)[extensionIndex]);
		tkindex = internal_parse_unprocessed_extension(data, tokens, tkindex, extension);

		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses a sparse-accessor field from the json data
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outAccessor the output acessor
/// @return the next token index to be analyzed
static int internal_json_parse_accessor_sparse(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_SparseAccessor* outAccessor) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	for (int j = 0; j < size; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "count") == 0) { 
			++tkindex; outAccessor->count = json_to_size(data, tokens + tkindex); ++tkindex; 
		}
		else if (json_strncmp(data, tokens + tkindex, "indices") == 0) {
			++tkindex;
			GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");

			int indicesSize = tokens[tkindex].size;
			++tkindex;

			for (int k = 0; k < indicesSize; ++k) {
				GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

				if (json_strncmp(data, tokens + tkindex, "bufferView") == 0) {
					++tkindex;
					outAccessor->indicesBufferView = PTR_TO_INDEX(GLTF_BufferView, json_to_int(data, tokens + tkindex)); 
					++tkindex;
				}
				else if (json_strncmp(data, tokens + tkindex, "byteOffset") == 0) { 
					++tkindex;
					outAccessor->indicesByteOffset = json_to_size(data, tokens + tkindex);
					++tkindex;
				}
				else if (json_strncmp(data, tokens + tkindex, "componentType") == 0) {
					++tkindex;
					outAccessor->indicesComponentType = internal_json_to_component_type(data, tokens + tkindex);
					++tkindex;
				}
				else { 
					tkindex = json_parse_skip(tokens, tkindex + 1);
				}

				if (tkindex < 0) return tkindex;
			}
		}
		else if (json_strncmp(data, tokens + tkindex, "values") == 0) {
			++tkindex;
			GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");

			int valuesSize = tokens[tkindex].size;
			++tkindex;

			for (int k = 0; k < valuesSize; ++k) {
				GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

				if (json_strncmp(data, tokens + tkindex, "bufferView") == 0) { 
					++tkindex; 
					outAccessor->valuesBufferView = PTR_TO_INDEX(GLTF_BufferView, json_to_int(data, tokens + tkindex)); 
					++tkindex;
				}
				else if (json_strncmp(data, tokens + tkindex, "byteOffset") == 0) { 
					++tkindex;
					outAccessor->valueByteOffset = json_to_size(data, tokens + tkindex);
					++tkindex;
				}
				else { 
					tkindex = json_parse_skip(tokens, tkindex + 1); 
				}

				if (tkindex < 0) return tkindex;
			}
		}
		else { 
			tkindex = json_parse_skip(tokens, tkindex + 1); 
		}

		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses an accessor field from the json data
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outAccessor the output acessor
/// @return the next token index to be analyzed
static int internal_parse_json_accessor(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_Accessor* outAccessor) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	for (int j = 0; j < size; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "name") == 0) { 
			tkindex = json_parse_string(data, tokens, tkindex + 1, &outAccessor->name);
		}
		else if (json_strncmp(data, tokens + tkindex, "bufferView") == 0) { 
			++tkindex;
			outAccessor->bufferView = PTR_TO_INDEX(GLTF_BufferView, json_to_int(data, tokens + tkindex));
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "byteOffset") == 0) {
			++tkindex;
			outAccessor->offset = json_to_size(data, tokens + tkindex); 
			++tkindex; 
		}
		else if (json_strncmp(data, tokens + tkindex, "componentType") == 0) {
			++tkindex; 
			outAccessor->componentType = internal_json_to_component_type(data, tokens + tkindex); 
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "normalized") == 0) { 
			++tkindex;
			outAccessor->normalized = json_to_bool(data, tokens + tkindex); 
			++tkindex; 
		}
		else if (json_strncmp(data, tokens + tkindex, "count") == 0) { 
			++tkindex;
			outAccessor->count = json_to_size(data, tokens + tkindex); 
			++tkindex; 
		}
		else if (json_strncmp(data, tokens + tkindex, "type") == 0) {
			++tkindex;
			if (json_strncmp(data, tokens + tkindex, "SCALAR") == 0) {
				outAccessor->type = Type_Scalar; 
			}
			else if (json_strncmp(data, tokens + tkindex, "VEC2") == 0) { 
				outAccessor->type = Type_Vec2;
			}
			else if (json_strncmp(data, tokens + tkindex, "VEC3") == 0) { 
				outAccessor->type = Type_Vec3;
			}
			else if (json_strncmp(data, tokens + tkindex, "VEC4") == 0) { 
				outAccessor->type = Type_Vec4;
			}
			else if (json_strncmp(data, tokens + tkindex, "MAT2") == 0) {
				outAccessor->type = Type_Mat2; 
			}
			else if (json_strncmp(data, tokens + tkindex, "MAT3") == 0) { 
				outAccessor->type = Type_Mat3; 
			}
			else if (json_strncmp(data, tokens + tkindex, "MAT4") == 0) {
				outAccessor->type = Type_Mat4;
			}
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "sparse") == 0) { 
			outAccessor->isSparse = 1;
			tkindex = internal_json_parse_accessor_sparse(data, tokens, tkindex + 1, &outAccessor->sparse);
		}
		else if (json_strncmp(data, tokens + tkindex, "min") == 0) { 
			++tkindex; 
			outAccessor->hasMin = 1;
			tkindex = json_parse_array_float(data, tokens, tkindex, outAccessor->min, tokens[tkindex].size > 16 ? 16 : tokens[tkindex].size);
		}
		else if (json_strncmp(data, tokens + tkindex, "max") == 0) { 
			++tkindex;
			outAccessor->hasMax = 1;
			tkindex = json_parse_array_float(data, tokens, tkindex, outAccessor->max, tokens[tkindex].size > 16 ? 16 : tokens[tkindex].size);
		}
		else if (json_strncmp(data, tokens + tkindex, "extras") == 0) { 
			tkindex = internal_parse_extras(data, tokens, tkindex + 1, &outAccessor->extras);
		}
		else if (json_strncmp(data, tokens + tkindex, "extensions") == 0) {
			tkindex = internal_parse_unprocessed_extensions(data, tokens, tkindex, &outAccessor->extensionsCount, &outAccessor->extensions);
		}
		else { 
			tkindex = json_parse_skip(tokens, tkindex + 1); 
		}

		if (tkindex < 0) return tkindex;
	}

	return tkindex;
}

/// @brief parses the accessors field
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outData the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_accessors(const char* data, const jsmntok_t* tokens, int tkindex, GLTF2* outData) {
	tkindex = json_parse_array(data, tokens, tkindex, sizeof(GLTF_Accessor), (void**)&outData->accessors, &outData->accessorsCount);
	if (tkindex < 0) return tkindex;

	for (unsigned long long j = 0; j < outData->accessorsCount; ++j) {
		tkindex = internal_parse_json_accessor(data, tokens, tkindex, &outData->accessors[j]);
		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses the asset structure
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outAsset the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_asset(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_Asset* outAsset) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	for (int j = 0; j < size; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "copyright") == 0) { 
			tkindex = json_parse_string(data, tokens, tkindex + 1, &outAsset->copyright); 
		}
		else if (json_strncmp(data, tokens + tkindex, "generator") == 0) { 
			tkindex = json_parse_string(data, tokens, tkindex + 1, &outAsset->generator);
		}
		else if (json_strncmp(data, tokens + tkindex, "version") == 0) { 
			tkindex = json_parse_string(data, tokens, tkindex + 1, &outAsset->version);
		}
		else if (json_strncmp(data, tokens + tkindex, "minVersion") == 0) {
			tkindex = json_parse_string(data, tokens, tkindex + 1, &outAsset->minVersion);
		}
		else if (json_strncmp(data, tokens + tkindex, "extras") == 0) {
			tkindex = internal_parse_extras(data, tokens, tkindex + 1, &outAsset->extras);
		}
		else if (json_strncmp(data, tokens + tkindex, "extensions") == 0) {
			tkindex = internal_parse_unprocessed_extensions(data, tokens, tkindex, &outAsset->extensionsCount, &outAsset->extensions);
		}
		else {
			tkindex = json_parse_skip(tokens, tkindex + 1); 
		}

		if (tkindex < 0) return tkindex;
	}
	GLTF_ASSERT(!(outAsset->version && atof(outAsset->version) < 2), "Legacy GLTF is not supported");
	return tkindex;
}

/// @brief parses the buffer view
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outBufferView the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_buffer_view(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_BufferView* outBufferView) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	for (int j = 0; j < size; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "name") == 0) { 
			tkindex = json_parse_string(data, tokens, tkindex + 1, &outBufferView->name);
		}
		else if (json_strncmp(data, tokens + tkindex, "buffer") == 0) {
			++tkindex; 
			outBufferView->buffer = PTR_TO_INDEX(GLTF_Buffer, json_to_int(data, tokens + tkindex)); 
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "byteOffset") == 0) { 
			++tkindex; 
			outBufferView->offset = json_to_size(data, tokens + tkindex); 
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "byteLength") == 0) { 
			++tkindex; outBufferView->size = json_to_size(data, tokens + tkindex); ++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "byteStride") == 0) { 
			++tkindex;
			outBufferView->stride = json_to_size(data, tokens + tkindex);
			++tkindex; 
		}
		else if (json_strncmp(data, tokens + tkindex, "target") == 0) { 
			++tkindex; 
			outBufferView->type = (GLTF_BufferViewType)json_to_int(data, tokens + tkindex); 
			tkindex++;
		}
		else if (json_strncmp(data, tokens + tkindex, "extras") == 0) {
			tkindex = internal_parse_extras(data, tokens, tkindex + 1, &outBufferView->extras);
		}
		else if (json_strncmp(data, tokens + tkindex, "extensions") == 0) {
			tkindex = internal_parse_unprocessed_extensions(data, tokens, tkindex, &outBufferView->extensionsCount, &outBufferView->extensions);
		}
		else { 
			tkindex = json_parse_skip(tokens, tkindex + 1);
		}

		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses the buffer views
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outBufferView the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_bufferviews(const char* data, const jsmntok_t* tokens, int i, GLTF2* outData) {
	i = json_parse_array(data, tokens, i, sizeof(GLTF_BufferView), (void**)&outData->bufferViews, &outData->bufferViewsCount);
	if (i < 0) return i;

	for (unsigned long long j = 0; j < outData->bufferViewsCount; ++j) {
		i = internal_parse_buffer_view(data, tokens, i, &outData->bufferViews[j]);
		if (i < 0) return i;
	}
	return i;
}

/// @brief parses a buffer
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outBuffer the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_buffer(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_Buffer* outBuffer) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	for (int j = 0; j < size; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "name") == 0) {
			tkindex = json_parse_string(data, tokens, tkindex + 1, &outBuffer->name);
		}
		else if (json_strncmp(data, tokens + tkindex, "byteLength") == 0) {
			++tkindex;
			outBuffer->size = json_to_size(data, tokens + tkindex);
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "uri") == 0) {
			tkindex = json_parse_string(data, tokens, tkindex + 1, &outBuffer->URI);
		}
		else if (json_strncmp(data, tokens + tkindex, "extras") == 0) { 
			tkindex = internal_parse_extras(data, tokens, tkindex + 1, &outBuffer->extras);
		}
		else if (json_strncmp(data, tokens + tkindex, "extensions") == 0) {
			tkindex = internal_parse_unprocessed_extensions(data, tokens, tkindex, &outBuffer->extensionsCount, &outBuffer->extensions);
		}
		else { 
			tkindex = json_parse_skip(tokens, tkindex + 1);
		}

		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses the buffers
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outData the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_buffers(const char* data, const jsmntok_t* tokens, int tkindex, GLTF2* outData) {
	tkindex = json_parse_array(data, tokens, tkindex, sizeof(GLTF_Buffer), (void**)&outData->buffers, &outData->buffersCount);
	if (tkindex < 0) return tkindex;

	for (unsigned long long j = 0; j < outData->buffersCount; ++j) {
		tkindex = internal_parse_buffer(data, tokens, tkindex, &outData->buffers[j]);
		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses an animation sampler
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outSampler the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_animation_sampler(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_AnimationSampler* outSampler) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	for (int j = 0; j < size; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "input") == 0) { 
			++tkindex; 
			outSampler->input = PTR_TO_INDEX(GLTF_Accessor, json_to_int(data, tokens + tkindex)); 
			++tkindex; 
		}
		else if (json_strncmp(data, tokens + tkindex, "output") == 0) { 
			++tkindex;
			outSampler->output = PTR_TO_INDEX(GLTF_Accessor, json_to_int(data, tokens + tkindex));
			++tkindex; 
		}
		else if (json_strncmp(data, tokens + tkindex, "interpolation") == 0) {
			++tkindex;
			if (json_strncmp(data, tokens + tkindex, "LINEAR") == 0) {
				outSampler->interpolation = InterpolationType_Linear;
			}
			else if (json_strncmp(data, tokens + tkindex, "STEP") == 0) {
				outSampler->interpolation = InterpolationType_Step;
			}
			else if (json_strncmp(data, tokens + tkindex, "CUBICSPLINE") == 0) {
				outSampler->interpolation = InterpolationType_CubicSpline; 
			}
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "extras") == 0) { 
			tkindex = internal_parse_extras(data, tokens, tkindex + 1, &outSampler->extras);
		}
		else if (json_strncmp(data, tokens + tkindex, "extensions") == 0) { 
			tkindex = internal_parse_unprocessed_extensions(data, tokens, tkindex, &outSampler->extensionsCount, &outSampler->extensions);
		}
		else {
			tkindex = json_parse_skip(tokens, tkindex + 1); 
		}

		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses an animation channel
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outChannel the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_animation_channel(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_AnimationChannel* outChannel) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	for (int j = 0; j < size; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "sampler") == 0) { 
			++tkindex; outChannel->sampler = PTR_TO_INDEX(GLTF_AnimationSampler, json_to_int(data, tokens + tkindex));
			++tkindex; 
		}
		else if (json_strncmp(data, tokens + tkindex, "target") == 0) {
			++tkindex;
			GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
			int target_size = tokens[tkindex].size;
			++tkindex;

			for (int k = 0; k < target_size; ++k) {
				GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

				if (json_strncmp(data, tokens + tkindex, "node") == 0) { 
					++tkindex; outChannel->targetNode = PTR_TO_INDEX(GLTF_Node, json_to_int(data, tokens + tkindex)); ++tkindex; }
				else if (json_strncmp(data, tokens + tkindex, "path") == 0) {
					++tkindex;
					if (json_strncmp(data, tokens + tkindex, "translation") == 0) { 
						outChannel->targetPath = AnimationPathType_Translation; 
					}
					else if (json_strncmp(data, tokens + tkindex, "rotation") == 0) { 
						outChannel->targetPath = AnimationPathType_Rotation; 
					}
					else if (json_strncmp(data, tokens + tkindex, "scale") == 0) { 
						outChannel->targetPath = AnimationPathType_Scale;
					}
					else if (json_strncmp(data, tokens + tkindex, "weights") == 0) {
						outChannel->targetPath = AnimationPathType_Weights; 
					}
					++tkindex;
				}
				else if (json_strncmp(data, tokens + tkindex, "extras") == 0) { 
					tkindex = internal_parse_extras(data, tokens, tkindex + 1, &outChannel->extras);
				}
				else if (json_strncmp(data, tokens + tkindex, "extensions") == 0) { 
					tkindex = internal_parse_unprocessed_extensions(data, tokens, tkindex, &outChannel->extensionsCount, &outChannel->extensions);
				}
				else { 
					tkindex = json_parse_skip(tokens, tkindex + 1);
				}

				if (tkindex < 0) return tkindex;
			}
		}
		else { 
			tkindex = json_parse_skip(tokens, tkindex + 1);
		}

		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses an animation
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outAnimation the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_animation(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_Animation* outAnimation) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	for (int j = 0; j < size; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "name") == 0) { 
			tkindex = json_parse_string(data, tokens, tkindex + 1, &outAnimation->name);
		}
		else if (json_strncmp(data, tokens + tkindex, "samplers") == 0) {
			tkindex = json_parse_array(data, tokens, tkindex + 1, sizeof(GLTF_AnimationSampler), (void**)&outAnimation->samplers, &outAnimation->samplersCount);
			if (tkindex < 0) return tkindex;

			for (unsigned long long k = 0; k < outAnimation->samplersCount; ++k) {
				tkindex = internal_parse_animation_sampler(data, tokens, tkindex, &outAnimation->samplers[k]);
				if (tkindex < 0) return tkindex;
			}
		}
		else if (json_strncmp(data, tokens + tkindex, "channels") == 0) {
			tkindex = json_parse_array(data, tokens, tkindex + 1, sizeof(GLTF_AnimationChannel), (void**)&outAnimation->channels, &outAnimation->channelsCount);
			if (tkindex < 0) return tkindex;

			for (unsigned long long k = 0; k < outAnimation->channelsCount; ++k) {
				tkindex = internal_parse_animation_channel(data, tokens, tkindex, &outAnimation->channels[k]);
				if (tkindex < 0) return tkindex;
			}
		}
		else if (json_strncmp(data, tokens + tkindex, "extras") == 0) { 
			tkindex = internal_parse_extras(data, tokens, tkindex + 1, &outAnimation->extras);
		}
		else if (json_strncmp(data, tokens + tkindex, "extensions") == 0) { 
			tkindex = internal_parse_unprocessed_extensions(data, tokens, tkindex, &outAnimation->extensionsCount, &outAnimation->extensions);
		}
		else { 
			tkindex = json_parse_skip(tokens, tkindex + 1);
		}

		if (tkindex < 0) { return tkindex; }
	}
	return tkindex;
}

/// @brief parses the animations field from json data
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outData the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_animations(const char* data, const jsmntok_t* tokens, int tkindex, GLTF2* outData) {
	tkindex = json_parse_array(data, tokens, tkindex, sizeof(GLTF_Animation), (void**)&outData->animations, &outData->animationsCount);
	if (tkindex < 0) return tkindex;

	for (unsigned long long j = 0; j < outData->animationsCount; ++j) {
		tkindex = internal_parse_animation(data, tokens, tkindex, &outData->animations[j]);
		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses a camera field from json data
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outCamera the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_camera(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_Camera* outCamera) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	for (int j = 0; j < size; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "name") == 0) { 
			tkindex = json_parse_string(data, tokens, tkindex + 1, &outCamera->name);
		}
		else if (json_strncmp(data, tokens + tkindex, "perspective") == 0) {
			++tkindex;
			GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
			int data_size = tokens[tkindex].size;
			++tkindex;

			outCamera->type = CameraType_Perspective;

			for (int k = 0; k < data_size; ++k) {
				GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

				if (json_strncmp(data, tokens + tkindex, "aspectRatio") == 0) {
					++tkindex;
					outCamera->data.perspective.hasAspectRatio = 1;
					outCamera->data.perspective.aspectRatio = json_to_float(data, tokens + tkindex);
					++tkindex;
				}
				else if (json_strncmp(data, tokens + tkindex, "yfov") == 0) { 
					++tkindex;
					outCamera->data.perspective.yFOV = json_to_float(data, tokens + tkindex);
					++tkindex;
				}
				else if (json_strncmp(data, tokens + tkindex, "zfar") == 0) {
					++tkindex;
					outCamera->data.perspective.hasZFar = 1;
					outCamera->data.perspective.zFar = json_to_float(data, tokens + tkindex);
					++tkindex;
				}
				else if (json_strncmp(data, tokens + tkindex, "znear") == 0) { 
					++tkindex;
					outCamera->data.perspective.zNear = json_to_float(data, tokens + tkindex); 
					++tkindex; 
				}
				else if (json_strncmp(data, tokens + tkindex, "extras") == 0) { 
					tkindex = internal_parse_extras(data, tokens, tkindex + 1, &outCamera->data.perspective.extras);
				}
				else {
					tkindex = json_parse_skip(tokens, tkindex + 1); 
				}

				if (tkindex < 0)  return tkindex;
			}
		}
		else if (json_strncmp(data, tokens + tkindex, "orthographic") == 0) {
			++tkindex;
			GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
			int data_size = tokens[tkindex].size;
			++tkindex;
			outCamera->type = CameraType_Orthographic;

			for (int k = 0; k < data_size; ++k) {
				GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

				if (json_strncmp(data, tokens + tkindex, "xmag") == 0) { 
					++tkindex; 
					outCamera->data.orthographic.xMag = json_to_float(data, tokens + tkindex);
					++tkindex;
				}
				else if (json_strncmp(data, tokens + tkindex, "ymag") == 0) {
					++tkindex; 
					outCamera->data.orthographic.yMag = json_to_float(data, tokens + tkindex); 
					++tkindex;
				}
				else if (json_strncmp(data, tokens + tkindex, "zfar") == 0) { 
					++tkindex; 
					outCamera->data.orthographic.zFar = json_to_float(data, tokens + tkindex); 
					++tkindex; 
				}
				else if (json_strncmp(data, tokens + tkindex, "znear") == 0) {
					++tkindex; 
					outCamera->data.orthographic.zNear = json_to_float(data, tokens + tkindex);
					++tkindex;
				}
				else if (json_strncmp(data, tokens + tkindex, "extras") == 0) {
					tkindex = internal_parse_extras(data, tokens, tkindex + 1, &outCamera->data.orthographic.extras);
				}
				else { 
					tkindex = json_parse_skip(tokens, tkindex + 1);
				}

				if (tkindex < 0) return tkindex;
			}
		}
		else if (json_strncmp(data, tokens + tkindex, "extras") == 0) {
			tkindex = internal_parse_extras(data, tokens, tkindex + 1, &outCamera->extras);
		}
		else if (json_strncmp(data, tokens + tkindex, "extensions") == 0) { 
			tkindex = internal_parse_unprocessed_extensions(data, tokens, tkindex, &outCamera->extensionsCount, &outCamera->extensions);
		}
		else { 
			tkindex = json_parse_skip(tokens, tkindex + 1);
		}

		if (tkindex < 0)  return tkindex;
	}
	return tkindex;
}

/// @brief parses the cameras field of a json data
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outData the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_cameras(const char* data, const jsmntok_t* tokens, int tkindex, GLTF2* outData) {
	tkindex = json_parse_array(data, tokens, tkindex, sizeof(GLTF_Camera), (void**)&outData->cameras, &outData->camerasCount);
	if (tkindex < 0) return tkindex;

	for (unsigned long long j = 0; j < outData->camerasCount; ++j) {
		tkindex = internal_parse_camera(data, tokens, tkindex, &outData->cameras[j]);
		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses an image
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outImage the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_image(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_Image* outImage) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	for (int j = 0; j < size; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "uri") == 0) { 
			tkindex = json_parse_string(data, tokens, tkindex + 1, &outImage->URI);
		}
		else if (json_strncmp(data, tokens + tkindex, "bufferView") == 0) { 
			++tkindex; 
			outImage->bufferView = PTR_TO_INDEX(GLTF_BufferView, json_to_int(data, tokens + tkindex)); 
			++tkindex; 
		}
		else if (json_strncmp(data, tokens + tkindex, "mimeType") == 0) { 
			tkindex = json_parse_string(data, tokens, tkindex + 1, &outImage->mime_type); 
		}
		else if (json_strncmp(data, tokens + tkindex, "name") == 0) { 
			tkindex = json_parse_string(data, tokens, tkindex + 1, &outImage->name); 
		}
		else if (json_strncmp(data, tokens + tkindex, "extras") == 0) { 
			tkindex = internal_parse_extras(data , tokens, tkindex + 1, &outImage->extras);
		}
		else if (json_strncmp(data, tokens + tkindex, "extensions") == 0) { 
			tkindex = internal_parse_unprocessed_extensions(data, tokens, tkindex, &outImage->extensionsCount, &outImage->extensions);
		}
		else { 
			tkindex = json_parse_skip(tokens, tkindex + 1);
		}

		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses the json images
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outData the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_images(const char* data, const jsmntok_t* tokens, int tkindex, GLTF2* outData) {
	tkindex = json_parse_array(data, tokens, tkindex, sizeof(GLTF_Image), (void**)&outData->images, &outData->imagesCount);
	if (tkindex < 0) return tkindex;

	for (unsigned long long j = 0; j < outData->imagesCount; ++j) {
		tkindex = internal_parse_image(data, tokens, tkindex, &outData->images[j]);
		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses the texture view field of json data
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param textureView the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_texture_view(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_TextureView* textureView) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	textureView->scale = 1.0f;

	for (int j = 0; j < size; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "index") == 0) { 
			++tkindex; textureView->texture = PTR_TO_INDEX(GLTF_Texture, json_to_int(data, tokens + tkindex)); 
			++tkindex; 
		}
		else if (json_strncmp(data, tokens + tkindex, "texCoord") == 0) { 
			++tkindex; 
			textureView->texCoord = json_to_int(data, tokens + tkindex);
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "scale") == 0) { 
			++tkindex; 
			textureView->scale = json_to_float(data, tokens + tkindex); ++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "strength") == 0) { 
			++tkindex; 
			textureView->scale = json_to_float(data, tokens + tkindex); ++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "extensions") == 0) { 
			tkindex = internal_parse_unprocessed_extensions(data, tokens, tkindex, &textureView->extensionsCount, &textureView->extensions);
		}
		else { 
			tkindex = json_parse_skip(tokens, tkindex + 1); 
		}

		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses the physically based rendering, metallic-roughness pipeline
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param textureView the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_pbr_metallic_roughness(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_PBRMetallicRoughness* pbr) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	for (int j = 0; j < size; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "metallicFactor") == 0) {
			++tkindex;
			pbr->metallicFactor = json_to_float(data, tokens + tkindex);
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "roughnessFactor") == 0) {
			++tkindex;
			pbr->roughnessFactor = json_to_float(data, tokens + tkindex);
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "baseColorFactor") == 0) {
			tkindex = json_parse_array_float(data, tokens, tkindex + 1, pbr->baseColor, 4);
		}
		else if (json_strncmp(data, tokens + tkindex, "baseColorTexture") == 0) {
			tkindex = internal_parse_texture_view(data, tokens, tkindex + 1, &pbr->baseColorTexture);
		}
		else if (json_strncmp(data, tokens + tkindex, "metallicRoughnessTexture") == 0) {
			tkindex = internal_parse_texture_view(data, tokens, tkindex + 1, &pbr->metallicRoughnessTexture);
		}
		else {
			tkindex = json_parse_skip(tokens, tkindex + 1);
		}

		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses a single material
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param textureView the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_material(const char* data, jsmntok_t const* tokens, int tkindex, GLTF_Material* material) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	for (int j = 0; j < 4; ++j) {
		material->PBRmetallicRoughness.baseColor[j] = 1.0f;
	}

	material->PBRmetallicRoughness.metallicFactor = 1.0f;
	material->PBRmetallicRoughness.roughnessFactor = 1.0f;
	material->alphaCutoff = 0.5f;

	for (int j = 0; j < size; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "name") == 0) { 
			tkindex = json_parse_string(data, tokens, tkindex + 1, &material->name);
		}
		else if (json_strncmp(data, tokens + tkindex, "pbrMetallicRoughness") == 0) { 
			tkindex = internal_parse_pbr_metallic_roughness(data, tokens, tkindex + 1, &material->PBRmetallicRoughness);
		}
		else if (json_strncmp(data, tokens + tkindex, "emissiveFactor") == 0) { 
			tkindex = json_parse_array_float(data, tokens, tkindex + 1, material->emissiveFactor, 3);
		}
		else if (json_strncmp(data, tokens + tkindex, "normalTexture") == 0) { 
			tkindex = internal_parse_texture_view(data, tokens, tkindex + 1, &material->normalTexture);
		}
		else if (json_strncmp(data, tokens + tkindex, "occlusionTexture") == 0) { 
			tkindex = internal_parse_texture_view(data, tokens, tkindex + 1, &material->occlusionTexture);
		}
		else if (json_strncmp(data, tokens + tkindex, "emissiveTexture") == 0) { 
			tkindex = internal_parse_texture_view(data, tokens, tkindex + 1, &material->emissiveTexture);
		}
		else if (json_strncmp(data, tokens + tkindex, "alphaCutoff") == 0) {
			++tkindex; 
			material->alphaCutoff = json_to_float(data, tokens + tkindex); 
			++tkindex; 
		}
		else if (json_strncmp(data, tokens + tkindex, "doubleSided") == 0) { 
			++tkindex;
			material->doubleSided = json_to_bool(data, tokens + tkindex);
			++tkindex; 
		}
		else if (json_strncmp(data, tokens + tkindex, "alphaMode") == 0) {
			++tkindex;
			if (json_strncmp(data, tokens + tkindex, "OPAQUE") == 0) { 
				material->alphaMode = AlphaMode_Opaque; 
			}
			else if (json_strncmp(data, tokens + tkindex, "MASK") == 0) { 
				material->alphaMode = AlphaMode_Mask; 
			}
			else if (json_strncmp(data, tokens + tkindex, "BLEND") == 0) {
				material->alphaMode = AlphaMode_Blend;
			}
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "extras") == 0) { 
			tkindex = internal_parse_extras(data, tokens, tkindex + 1, &material->extras);
		}
		else if (json_strncmp(data, tokens + tkindex, "extensions") == 0) {
			tkindex = internal_parse_unprocessed_extensions(data, tokens, tkindex, &material->extensionsCount, &material->extensions);
		}
		else {
			tkindex = json_parse_skip(tokens, tkindex + 1); 
		}

		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses the json materials
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outData the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_materials(const char* data, const jsmntok_t* tokens, int tkindex, GLTF2* outData) {
	tkindex = json_parse_array(data, tokens, tkindex, sizeof(GLTF_Material), (void**)&outData->materials, &outData->materialsCount);
	if (tkindex < 0) return tkindex;

	for (unsigned long long j = 0; j < outData->materialsCount; ++j) {
		tkindex = internal_parse_material(data, tokens, tkindex, &outData->materials[j]);
		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief converts attribute type given it's string name
/// @param name the string attribute name
/// @param type the parsed attribute type
/// @param index the output index value
static void internal_parse_attribute_type(const char* name, GLTF_AttributeType* type, int* index) {
	if (*name == '_') {
		*type = AttributeType_Custom;
		return;
	}

	const char* us = strchr_impl(name, '_');
	unsigned long long len = us ? (unsigned long long)(us - name) : strlen(name);

	if (len == 8 && strncmp_impl(name, "POSITION", 8) == 0) {
		*type = AttributeType_Position;
	}
	else if (len == 6 && strncmp_impl(name, "NORMAL", 6) == 0) {
		*type = AttributeType_Normal;
	}
	else if (len == 7 && strncmp_impl(name, "TANGENT", 7) == 0) {
		*type = AttributeType_Tangent;
	}
	else if (len == 8 && strncmp_impl(name, "TEXCOORD", 8) == 0) {
		*type = AttributeType_TexCoord;
	}
	else if (len == 5 && strncmp_impl(name, "COLOR", 5) == 0){
		*type = AttributeType_Color;
	}
	else if (len == 6 && strncmp_impl(name, "JOINTS", 6) == 0) {
		*type = AttributeType_Joints;
	}
	else if (len == 7 && strncmp_impl(name, "WEIGHTS", 7) == 0) {
		*type = AttributeType_Weights;
	}
	else {
		*type = AttributeType_Invalid;
	}

	if (us && *type != AttributeType_Invalid) {
		*index = atoi(us + 1);
		if (*index < 0) {
			*type = *type = AttributeType_Invalid;
			*index = 0;
		}
	}
}

/// @brief parses the attribute lists
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param attributes the output data with the parsed information
/// @param attributesCount the parsed attributes count
/// @return the next token index to be analyzed
static int internal_parse_attribute_list(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_Attribute** attributes, unsigned long long* attributesCount) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");

	*attributesCount = tokens[tkindex].size;
	*attributes = (GLTF_Attribute*)gltfmemory_allocate(sizeof(GLTF_Attribute) * (*attributesCount), 1);
	++tkindex;

	for (unsigned long long j = 0; j < *attributesCount; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		tkindex = json_parse_string(data, tokens, tkindex, &(*attributes)[j].name);
		GLTF_ASSERT(attributes > 0, "There was an error when parsing attributes");

		internal_parse_attribute_type((*attributes)[j].name, &(*attributes)[j].type, &(*attributes)[j].index);
		(*attributes)[j].data = PTR_TO_INDEX(GLTF_Accessor, json_to_int(data, tokens + tkindex));
		++tkindex;
	}
	return tkindex;
}

/// @brief parses the json primitive
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param primitive the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_primitive(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_Primitive* primitive) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	primitive->type = PrimitiveType_Triangles;

	for (int j = 0; j < size; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "mode") == 0) {
			++tkindex;
			primitive->type = (GLTF_PrimitiveType)json_to_int(data, tokens + tkindex);
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "indices") == 0) {
			++tkindex;
			primitive->indices = PTR_TO_INDEX(GLTF_Accessor, json_to_int(data, tokens + tkindex));
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "material") == 0) {
			++tkindex;
			primitive->material = PTR_TO_INDEX(GLTF_Material, json_to_int(data, tokens + tkindex));
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "attributes") == 0) {
			tkindex = internal_parse_attribute_list(data, tokens, tkindex + 1, &primitive->attributes, &primitive->attributesCount);
		}
		else if (json_strncmp(data, tokens + tkindex, "targets") == 0) {
			tkindex = json_parse_array(data, tokens, tkindex + 1, sizeof(GLTF_MorphTarget), (void**)&primitive->targets, &primitive->targetsCount);
			if (tkindex  < 0) return tkindex;

			for (unsigned long long k = 0; k < primitive->targetsCount; ++k) {
				tkindex = internal_parse_attribute_list(data, tokens, tkindex, &primitive->targets[k].attributes, &primitive->targets[k].attributesCount);
				if (tkindex < 0) return tkindex;
			}
		}
		else if (json_strncmp(data, tokens + tkindex, "extras") == 0) {
			tkindex = internal_parse_extras(data, tokens, tkindex + 1, &primitive->extras);
		}
		else if (json_strncmp(data, tokens + tkindex, "extensions") == 0) {
			tkindex = internal_parse_unprocessed_extensions(data, tokens, tkindex, &primitive->extensionsCount, &primitive->extensions);
		}
		else {
			tkindex = json_parse_skip(tokens, tkindex + 1);
		}

		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses a single mesh
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param mesh the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_mesh(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_Mesh* mesh) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	for (int j = 0; j < size; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "name") == 0) {
			tkindex = json_parse_string(data, tokens, tkindex + 1, &mesh->name);
		}
		else if (json_strncmp(data, tokens + tkindex, "primitives") == 0) {
			tkindex = json_parse_array(data , tokens, tkindex + 1, sizeof(GLTF_Primitive), (void**)&mesh->primitives, &mesh->primitivesCount);
			if (tkindex < 0) return tkindex;

			for (unsigned long long prim_index = 0; prim_index < mesh->primitivesCount; ++prim_index) {
				tkindex = internal_parse_primitive(data, tokens, tkindex, &mesh->primitives[prim_index]);
				if (tkindex < 0) return tkindex;
			}
		}
		else if (json_strncmp(data, tokens + tkindex, "weights") == 0) {
			tkindex = json_parse_array(data, tokens, tkindex + 1, sizeof(float), (void**)&mesh->weights, &mesh->weightsCount);
			if (tkindex < 0) return tkindex;
			tkindex = json_parse_array_float(data, tokens, tkindex - 1, mesh->weights, (int)mesh->weightsCount);
		}
		else if (json_strncmp(data, tokens + tkindex, "extras") == 0) { 
			tkindex = internal_parse_extras(data, tokens, tkindex + 1, &mesh->extras);
		}
		else if (json_strncmp(data, tokens + tkindex, "extensions") == 0) {
			tkindex = internal_parse_unprocessed_extensions(data, tokens, tkindex, &mesh->extensionsCount, &mesh->extensions); 
		}
		else {
			tkindex = json_parse_skip(tokens, tkindex + 1); 
		}

		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses the meshes
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outData the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_meshes(const char* data, const jsmntok_t* tokens, int tkindex, GLTF2* outData) {
	tkindex = json_parse_array(data, tokens, tkindex, sizeof(GLTF_Mesh), (void**)&outData->meshes, &outData->meshesCount);
	if (tkindex < 0) return tkindex;

	for (unsigned long long j = 0; j < outData->meshesCount; ++j) {
		tkindex = internal_parse_mesh(data, tokens, tkindex, &outData->meshes[j]);
		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses a single node
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param node the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_node(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_Node* node) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	node->rotation[3] = 1.0f;
	node->scale[0] = 1.0f;
	node->scale[1] = 1.0f;
	node->scale[2] = 1.0f;
	node->matrix[0] = 1.0f;
	node->matrix[5] = 1.0f;
	node->matrix[10] = 1.0f;
	node->matrix[15] = 1.0f;

	for (int j = 0; j < size; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "name") == 0) {
			tkindex = json_parse_string(data, tokens, tkindex + 1, &node->name);
		}
		else if (json_strncmp(data, tokens + tkindex, "children") == 0) {
			tkindex = json_parse_array(data, tokens, tkindex + 1, sizeof(GLTF_Node*), (void**)&node->children, &node->childrenCount);
			if (tkindex < 0) return tkindex;

			for (unsigned long long k = 0; k < node->childrenCount; ++k) {
				node->children[k] = PTR_TO_INDEX(GLTF_Node, json_to_int(data, tokens + tkindex));
				++tkindex;
			}
		}
		else if (json_strncmp(data, tokens + tkindex, "mesh") == 0) {
			++tkindex;
			GLTF_ASSERT(tokens[tkindex].type == JSMN_PRIMITIVE, "The expected unprocessed extension is not a json valid primitive");
			node->mesh = PTR_TO_INDEX(GLTF_Mesh, json_to_int(data, tokens + tkindex));
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "skin") == 0) {
			++tkindex;
			GLTF_ASSERT(tokens[tkindex].type == JSMN_PRIMITIVE, "The expected unprocessed extension is not a json valid primitive");
			node->skin = PTR_TO_INDEX(GLTF_Skin, json_to_int(data, tokens + tkindex));
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "camera") == 0) {
			++tkindex;
			GLTF_ASSERT(tokens[tkindex].type == JSMN_PRIMITIVE, "The expected unprocessed extension is not a json valid primitive");
			node->camera = PTR_TO_INDEX(GLTF_Camera, json_to_int(data, tokens + tkindex));
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "translation") == 0) { 
			tkindex = json_parse_array_float(data, tokens, tkindex + 1, node->translation, 3);
		}
		else if (json_strncmp(data, tokens + tkindex, "rotation") == 0) { 
			tkindex = json_parse_array_float(data, tokens, tkindex + 1, node->rotation, 4);
		}
		else if (json_strncmp(data, tokens + tkindex, "scale") == 0) { 
			tkindex = json_parse_array_float(data, tokens, tkindex + 1, node->scale, 3);
		}
		else if (json_strncmp(data, tokens + tkindex, "matrix") == 0) { 
			tkindex = json_parse_array_float(data, tokens, tkindex + 1, node->matrix, 16);
		}
		else if (json_strncmp(data, tokens + tkindex, "weights") == 0) {
			tkindex = json_parse_array(data, tokens, tkindex + 1, sizeof(float), (void**)&node->weights, &node->weightsCount);
			if (tkindex < 0) return tkindex;
			tkindex = json_parse_array_float(data, tokens, tkindex - 1, node->weights, (int)node->weightsCount);
		}
		else if (json_strncmp(data, tokens + tkindex, "extras") == 0) {
			tkindex = internal_parse_extras(data, tokens, tkindex + 1, &node->extras);
		}
		else if (json_strncmp(data, tokens + tkindex, "extensions") == 0) {
			tkindex = internal_parse_unprocessed_extensions(data, tokens, tkindex, &node->extensionsCount, &node->extensions);
		}
		else { 
			tkindex = json_parse_skip(tokens, tkindex + 1); 
		}

		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses the nodes
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outData the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_nodes(const char* data, const jsmntok_t* tokens, int tkindex, GLTF2* outData) {
	tkindex = json_parse_array(data, tokens, tkindex, sizeof(GLTF_Node), (void**)&outData->nodes, &outData->nodesCount);
	if (tkindex < 0) return tkindex;

	for (unsigned long long j = 0; j < outData->nodesCount; ++j) {
		tkindex = internal_parse_node(data, tokens, tkindex, &outData->nodes[j]);
		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses a single sampler
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param sampler the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_image_sampler(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_ImageSampler* sampler) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	sampler->wrapS = WrapMode_Repeat;
	sampler->wrapT = WrapMode_Repeat;

	for (int j = 0; j < size; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "name") == 0) {
			tkindex = json_parse_string(data, tokens, tkindex + 1, &sampler->name);
		}
		else if (json_strncmp(data, tokens + tkindex, "magFilter") == 0) {
			++tkindex; 
			sampler->magFilter = (GLTF_FilterType)json_to_int(data, tokens + tkindex); 
			++tkindex; 
		}
		else if (json_strncmp(data, tokens + tkindex, "minFilter") == 0) {
			++tkindex; sampler->minFilter = (GLTF_FilterType)json_to_int(data, tokens + tkindex); 
			++tkindex; 
		}
		else if (json_strncmp(data, tokens + tkindex, "wrapS") == 0) { 
			++tkindex; 
			sampler->wrapS = (GLTF_WrapMode)json_to_int(data, tokens + tkindex); 
			++tkindex; 
		}
		else if (json_strncmp(data, tokens + tkindex, "wrapT") == 0) { 
			++tkindex; 
			sampler->wrapT = (GLTF_WrapMode)json_to_int(data, tokens + tkindex); 
			++tkindex; 
		}
		else if (json_strncmp(data, tokens + tkindex, "extras") == 0) {
			tkindex = internal_parse_extras(data, tokens, tkindex + 1, &sampler->extras);
		}
		else if (json_strncmp(data, tokens + tkindex, "extensions") == 0) { 
			tkindex = internal_parse_unprocessed_extensions(data, tokens, tkindex, &sampler->extensionsCount, &sampler->extensions);
		}
		else { 
			tkindex = json_parse_skip(tokens, tkindex + 1); 
		}

		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses the samplers
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outData the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_image_samplers(const char* data, const jsmntok_t* tokens, int tkindex, GLTF2* outData) {
	tkindex = json_parse_array(data, tokens, tkindex, sizeof(GLTF_ImageSampler), (void**)&outData->imageSamplers, &outData->imageSamplersCount);
	if (tkindex < 0) return tkindex;

	for (unsigned long long j = 0; j < outData->imageSamplersCount; ++j) {
		tkindex = internal_parse_image_sampler(data, tokens, tkindex, &outData->imageSamplers[j]);
		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses a skin
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param skin the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_skin(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_Skin* skin) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	for (int j = 0; j < size; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "name") == 0) {
			tkindex = json_parse_string(data, tokens, tkindex + 1, &skin->name);
		}
		else if (json_strncmp(data, tokens + tkindex, "joints") == 0) {
			tkindex = json_parse_array(data, tokens, tkindex + 1, sizeof(GLTF_Node*), (void**)&skin->joints, &skin->jointsCount);
			if (tkindex < 0) return tkindex;

			for (unsigned long long k = 0; k < skin->jointsCount; ++k) {
				skin->joints[k] = PTR_TO_INDEX(GLTF_Node, json_to_int(data, tokens + tkindex));
				++tkindex;
			}
		}
		else if (json_strncmp(data, tokens + tkindex, "skeleton") == 0) {
			++tkindex;
			GLTF_ASSERT(tokens[tkindex].type == JSMN_PRIMITIVE, "The expected unprocessed extension is not a json valid primitive");
			skin->skeleton = PTR_TO_INDEX(GLTF_Node, json_to_int(data, tokens + tkindex));
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "inverseBindMatrices") == 0) {
			++tkindex;
			GLTF_ASSERT(tokens[tkindex].type == JSMN_PRIMITIVE, "The expected unprocessed extension is not a json valid primitive");
			skin->inverseBindMatrices = PTR_TO_INDEX(GLTF_Accessor, json_to_int(data, tokens + tkindex));
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "extras") == 0) {
			tkindex = internal_parse_extras(data, tokens, tkindex + 1, &skin->extras);
		}
		else if (json_strncmp(data, tokens + tkindex, "extensions") == 0) {
			tkindex = internal_parse_unprocessed_extensions(data, tokens, tkindex, &skin->extensionsCount, &skin->extensions);
		}
		else {
			tkindex = json_parse_skip(tokens, tkindex + 1);
		}

		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses the skins
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outData the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_skins(const char* data, const jsmntok_t* tokens, int tkindex, GLTF2* outData) {
	tkindex = json_parse_array(data, tokens, tkindex, sizeof(GLTF_Skin), (void**)&outData->skins, &outData->skinsCount);
	if (tkindex < 0) return tkindex;

	for (unsigned long long j = 0; j < outData->skinsCount; ++j) {
		tkindex = internal_parse_skin(data, tokens, tkindex, &outData->skins[j]);
		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses a scene
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param scene the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_scene(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_Scene* scene) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	for (int j = 0; j < size; ++j){
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "name") == 0) {
			tkindex = json_parse_string(data, tokens, tkindex + 1, &scene->name);
		}
		else if (json_strncmp(data, tokens + tkindex, "nodes") == 0) {
			tkindex = json_parse_array(data, tokens, tkindex + 1, sizeof(GLTF_Node*), (void**)&scene->nodes, &scene->nodesCount);
			if (tkindex < 0) return tkindex;

			for (unsigned long long k = 0; k < scene->nodesCount; ++k) {
				scene->nodes[k] = PTR_TO_INDEX(GLTF_Node, json_to_int(data, tokens + tkindex));
				++tkindex;
			}
		}
		else if (json_strncmp(data, tokens + tkindex, "extras") == 0) {
			tkindex = internal_parse_extras(data, tokens, tkindex + 1, &scene->extras);
		}
		else if (json_strncmp(data, tokens + tkindex, "extensions") == 0) {
			tkindex = internal_parse_unprocessed_extensions(data, tokens, tkindex, &scene->extensionsCount, &scene->extensions);
		}
		else {
			tkindex = json_parse_skip(tokens, tkindex + 1);
		}

		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses the scenes
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outData the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_scenes(const char* data, const jsmntok_t* tokens, int tkindex, GLTF2* outData) {
	tkindex = json_parse_array(data, tokens, tkindex, sizeof(GLTF_Scene), (void**)&outData->scenes, &outData->scenesCount);
	if (tkindex < 0) return tkindex;

	for (unsigned long long j = 0; j < outData->scenesCount; ++j) {
		tkindex = internal_parse_scene(data, tokens, tkindex, &outData->scenes[j]);
		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses a texture
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param texture the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_texture(const char* data, const jsmntok_t* tokens, int tkindex, GLTF_Texture* texture) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	for (int j = 0; j < size; ++j){
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "name") == 0) {
			tkindex = json_parse_string(data, tokens, tkindex + 1, &texture->name);
		}
		else if (json_strncmp(data, tokens + tkindex, "sampler") == 0) {
			++tkindex;
			texture->sampler = PTR_TO_INDEX(GLTF_ImageSampler, json_to_int(data, tokens + tkindex));
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "source") == 0) {
			++tkindex;
			texture->image = PTR_TO_INDEX(GLTF_Image, json_to_int(data, tokens + tkindex));
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "extras") == 0) {
			tkindex = internal_parse_extras(data, tokens, tkindex + 1, &texture->extras);
		}
		else if (json_strncmp(data, tokens + tkindex, "extensions") == 0) {
			tkindex = internal_parse_unprocessed_extensions(data, tokens, tkindex, &texture->extensionsCount, &texture->extensions);
		}
		else {
			tkindex = json_parse_skip(tokens, tkindex + 1);
		}

		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses the textures
/// @param data the json entire data
/// @param tokens the json token to read
/// @param tkindex the json token index
/// @param outData the output data with the parsed information
/// @return the next token index to be analyzed
static int internal_parse_textures(const char* data, const jsmntok_t* tokens, int tkindex, GLTF2* outData) {
	tkindex = json_parse_array(data, tokens, tkindex, sizeof(GLTF_Texture), (void**)&outData->textures, &outData->texturesCount);
	if (tkindex < 0) return tkindex;

	for (unsigned long long j = 0; j < outData->texturesCount; ++j) {
		tkindex = internal_parse_texture(data, tokens, tkindex, &outData->textures[j]);
		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}

/// @brief parses the json by it's root
/// @param data the json entire data
/// @param tokens the json tokens
/// @param tkindex the initial token index to start parsing from
/// @param outData the output data with the parsed information
/// @return 1 on success, 0 on failure
static int internal_parse_jsonroot(const char* data, const jsmntok_t* tokens, int tkindex, GLTF2* outData) {
	GLTF_ASSERT(tokens[tkindex].type == JSMN_OBJECT, "The expected unprocessed extension is not a json valid object");
	int size = tokens[tkindex].size;
	++tkindex;

	for (int j = 0; j < size; ++j) {
		GLTF_ASSERT(tokens[tkindex].type == JSMN_STRING || tokens[tkindex].size != 0, "The expected json data is not a string");

		if (json_strncmp(data, tokens + tkindex, "accessors") == 0) {
			tkindex = internal_parse_accessors(data, tokens, tkindex + 1, outData);
		}
		else if(json_strncmp(data, tokens + tkindex, "animation") == 0) {
			tkindex = internal_parse_animations(data, tokens, tkindex + 1, outData);
		}
		else if (json_strncmp(data, tokens + tkindex, "asset") == 0) {
			tkindex = internal_parse_asset(data, tokens, tkindex + 1, &outData->asset);
		}
		else if (json_strncmp(data, tokens + tkindex, "bufferViews") == 0) {
			tkindex = internal_parse_bufferviews(data, tokens, tkindex + 1, outData);
		}
		else if (json_strncmp(data, tokens + tkindex, "buffers") == 0) {
			tkindex = internal_parse_buffers(data, tokens, tkindex + 1, outData);
		}
		else if (json_strncmp(data, tokens + tkindex, "cameras") == 0) {
			tkindex = internal_parse_cameras(data, tokens, tkindex + 1, outData);
		}
		else if (json_strncmp(data, tokens + tkindex, "images") == 0) {
			tkindex = internal_parse_images(data, tokens, tkindex + 1, outData);
		}
		else if (json_strncmp(data, tokens + tkindex, "materials") == 0) {
			tkindex = internal_parse_materials(data, tokens, tkindex + 1, outData);
		}
		else if (json_strncmp(data, tokens + tkindex, "meshes") == 0) {
			tkindex = internal_parse_meshes(data, tokens, tkindex + 1, outData);
		}
		else if (json_strncmp(data, tokens + tkindex, "nodes") == 0) {
			tkindex = internal_parse_nodes(data, tokens, tkindex + 1, outData);
		}
		else if (json_strncmp(data, tokens + tkindex, "samplers") == 0) {
			tkindex = internal_parse_image_samplers(data, tokens, tkindex + 1, outData);
		}
		else if (json_strncmp(data, tokens + tkindex, "scenes") == 0){
			tkindex = internal_parse_scenes(data, tokens, tkindex + 1, outData);
		}
		else if (json_strncmp(data, tokens + tkindex, "scene") == 0) {
			++tkindex;
			outData->scene = PTR_TO_INDEX(GLTF_Scene, json_to_int(data, tokens + tkindex));
			++tkindex;
		}
		else if (json_strncmp(data, tokens + tkindex, "skins") == 0) {
			tkindex = internal_parse_skins(data, tokens, tkindex + 1, outData);
		}
		else if (json_strncmp(data, tokens + tkindex, "textures") == 0) {
			tkindex = internal_parse_textures(data, tokens, tkindex + 1, outData);
		}
		else if (json_strncmp(data, tokens + tkindex, "extensions") == 0) {
			tkindex = internal_parse_unprocessed_extensions(data, tokens, tkindex + 1, &outData->extensionsCount, &outData->extensions);
		}
		else if (json_strncmp(data, tokens + tkindex, "extensionsUsed") == 0){
			tkindex = json_parse_array_string(data, tokens, tkindex + 1, &outData->extensionsUsed, &outData->extensionsUsedCount);
		}
		else if (json_strncmp(data, tokens + tkindex, "extensionsRequired") == 0) {
			tkindex = json_parse_array_string(data, tokens, tkindex + 1, &outData->extensionsRequired, &outData->extensionsRequiredCount);
		}
		else if (json_strncmp(data, tokens + tkindex, "extras") == 0) {
			tkindex = internal_parse_extras(data, tokens, tkindex + 1, &outData->extras);
		}
		else {
			tkindex = json_parse_skip(tokens, tkindex + 1);
		}

		if (tkindex < 0) return tkindex;
	}
	return tkindex;
}


// @brief returns the number of components for a given gltf data type
unsigned long long internal_get_components_number(GLTF_Type type) {
	switch (type)
	{
	case Type_Vec2: return 2;
	case Type_Vec3: return 3;
	case Type_Vec4: return 4;
	case Type_Mat2: return 4;
	case Type_Mat3: return 9;
	case Type_Mat4: return 16;
	case Type_Scalar: return 1;
	}
	return 1;
}

// returns the size in bytes of a single component based on its data type
unsigned long long internal_component_size(GLTF_ComponentType componentType) {
	switch (componentType)
	{
	case ComponentType_R8:
	case ComponentType_R8_UNSIGNED:
		return 1;
	case ComponentType_R16:
	case ComponentType_R16_UNSIGNED:
		return 2;
	case ComponentType_R32_UNSIGNED:
	case ComponentType_R32_FLOAT:
		return 4;
	}
	return 0; // invalid components
}

/// @brief calculates the total byte size for a gltf attribute by combining component count and component size
unsigned long long internal_calculate_attribute_size(GLTF_Type type, GLTF_ComponentType componentType) {
	unsigned long long componentSize = internal_component_size(componentType);

	if (type == Type_Mat2 && componentSize == 1) return 8 * componentSize;
	else if (type == Type_Mat3 && (componentSize == 1 || componentSize == 2)) return 12 * componentSize;

	return componentSize * internal_get_components_number(type);
}

/// @brief conrrects the pointer of loaded members of a gltf file
/// @param data the gltf parsed data
/// @return 1 on success, 0 otherwise
static int internal_fix_pointers(GLTF2* data) {

	// meshes pointers
	for (unsigned long long i = 0; i < data->meshesCount; ++i) {
		for (unsigned long long j = 0; j < data->meshes[i].primitivesCount; ++j) {
			PTR_FIX(data->meshes[i].primitives[j].indices, data->accessors, data->accessorsCount);
			PTR_FIX(data->meshes[i].primitives[j].material, data->materials, data->materialsCount);

			for (unsigned long long k = 0; k < data->meshes[i].primitives[j].attributesCount; ++k) {
				PTR_FIX(data->meshes[i].primitives[j].attributes[k].data, data->accessors, data->accessorsCount);
			}

			for (unsigned long long k = 0; k < data->meshes[i].primitives[j].targetsCount; ++k) {
				for (unsigned long long m = 0; m < data->meshes[i].primitives[j].targets[k].attributesCount; ++m) {
					PTR_FIX_REQUIRED(data->meshes[i].primitives[j].targets[k].attributes[m].data, data->accessors, data->accessorsCount);
				}
			}
		}
	}

	// accessors
	for (unsigned long long i = 0; i < data->accessorsCount; ++i) {
		PTR_FIX(data->accessors[i].bufferView, data->bufferViews, data->bufferViewsCount);

		if (data->accessors[i].isSparse) {
			PTR_FIX_REQUIRED(data->accessors[i].sparse.indicesBufferView, data->bufferViews, data->bufferViewsCount);
			PTR_FIX_REQUIRED(data->accessors[i].sparse.valuesBufferView, data->bufferViews, data->bufferViewsCount);
		}

		if (data->accessors[i].bufferView) {
			data->accessors[i].stride = data->accessors[i].bufferView->stride;
		}

		if (data->accessors[i].stride == 0) {
			data->accessors[i].stride = internal_calculate_attribute_size(data->accessors[i].type, data->accessors[i].componentType);
		}
	}

	// textures
	for (unsigned long long i = 0; i < data->texturesCount; ++i) {
		PTR_FIX(data->textures[i].image, data->images, data->imagesCount);
		PTR_FIX(data->textures[i].sampler, data->imageSamplers, data->imageSamplersCount);
	}

	// images
	for (unsigned long long i = 0; i < data->imagesCount; ++i) {
		PTR_FIX(data->images[i].bufferView, data->bufferViews, data->bufferViewsCount);
	}

	// materials
	for (unsigned long long i = 0; i < data->materialsCount; ++i) {
		PTR_FIX(data->materials[i].normalTexture.texture, data->textures, data->texturesCount);
		PTR_FIX(data->materials[i].emissiveTexture.texture, data->textures, data->texturesCount);
		PTR_FIX(data->materials[i].occlusionTexture.texture, data->textures, data->texturesCount);

		PTR_FIX(data->materials[i].PBRmetallicRoughness.baseColorTexture.texture, data->textures, data->texturesCount);
		PTR_FIX(data->materials[i].PBRmetallicRoughness.metallicRoughnessTexture.texture, data->textures, data->texturesCount);
	}

	// buffer views
	for (unsigned long long i = 0; i < data->bufferViewsCount; ++i) {
		PTR_FIX_REQUIRED(data->bufferViews[i].buffer, data->buffers, data->buffersCount);
	}

	// skins
	for (unsigned long long i = 0; i < data->skinsCount; ++i) {
		for (unsigned long long j = 0; j < data->skins[i].jointsCount; ++j) {
			PTR_FIX_REQUIRED(data->skins[i].joints[j], data->nodes, data->nodesCount);
		}

		PTR_FIX(data->skins[i].skeleton, data->nodes, data->nodesCount);
		PTR_FIX(data->skins[i].inverseBindMatrices, data->accessors, data->accessorsCount);
	}

	// nodes
	for (unsigned long long i = 0; i < data->nodesCount; ++i) {
		for (unsigned long long j = 0; j < data->nodes[i].childrenCount; ++j) {
			PTR_FIX_REQUIRED(data->nodes[i].children[j], data->nodes, data->nodesCount);

			if (data->nodes[i].children[j]->parent) {
				return -1;
			}

			data->nodes[i].children[j]->parent = &data->nodes[i];
		}

		PTR_FIX(data->nodes[i].mesh, data->meshes, data->meshesCount);
		PTR_FIX(data->nodes[i].skin, data->skins, data->skinsCount);
		PTR_FIX(data->nodes[i].camera, data->cameras, data->camerasCount);
	}

	// scenes
	for (unsigned long long i = 0; i < data->scenesCount; ++i) {
		for (unsigned long long j = 0; j < data->scenes[i].nodesCount; ++j) {
			PTR_FIX_REQUIRED(data->scenes[i].nodes[j], data->nodes, data->nodesCount);

			if (data->scenes[i].nodes[j]->parent) {
				return -1;
			}
		}
	}
	PTR_FIX(data->scene, data->scenes, data->scenesCount);

	// animations
	for (unsigned long long i = 0; i < data->animationsCount; ++i) {
		for (unsigned long long j = 0; j < data->animations[i].samplersCount; ++j) {
			PTR_FIX_REQUIRED(data->animations[i].samplers[j].input, data->accessors, data->accessorsCount);
			PTR_FIX_REQUIRED(data->animations[i].samplers[j].output, data->accessors, data->accessorsCount);
		}

		for (unsigned long long j = 0; j < data->animations[i].channelsCount; ++j) {
			PTR_FIX_REQUIRED(data->animations[i].channels[j].sampler, data->animations[i].samplers, data->animations[i].samplersCount);
			PTR_FIX(data->animations[i].channels[j].targetNode, data->nodes, data->nodesCount);
		}
	}

	return 1;
}

/// @brief begins the parsing of the GLFW
static int internal_parse_json(const char* data, unsigned long long size, GLTF2* outData) {
	jsmn_parser parser = { 0, 0, 0 };

	// get json token count
	if (outData->fileInfo.jsonTkCount == 0) {
		int tkCount = jsmn_parse(&parser, data, size, NULL, 0);
		if (tkCount <= 0) return -1;
		outData->fileInfo.jsonTkCount = tkCount;
	}

	// allocate memory for the parsing
	jsmntok_t* tokens = (jsmntok_t*)gltfmemory_allocate(sizeof(jsmntok_t) * (outData->fileInfo.jsonTkCount + 1), 0);
	if (!tokens) return -1;

	// parse
	jsmn_init(&parser);
	int tokenCount = jsmn_parse(&parser, data, size, tokens, outData->fileInfo.jsonTkCount);
	if (tokenCount <= 0) {
		gltfmemory_deallocate(tokens);
		return -1;
	}

	// this makes sure that we always have an UNDEFINED token at the end of the stream, for invalid JSON inputs this makes sure we don't perform out of bound reads of token data
	tokens[tokenCount].type = JSMN_UNDEFINED;

	// begins the parsing at the root-level
	int i = internal_parse_jsonroot(data, tokens, 0, outData);
	gltfmemory_deallocate(tokens);

	if (i < 0) {
		GLTF_Free(outData);
		return -1;
	}

	if (internal_fix_pointers(outData) < 0) {
		GLTF_Free(outData);
		return -1;
	}

	outData->fileInfo.json = data;
	outData->fileInfo.jsonSize = size;

	return 1;
}

GLTF2 GLTF_ParseFromFile(const char* path) {
	s_gErrors[0] = '\0';
	GLTF2 parsedData = { 0 };

	if (!path || !path[0]) {
		internal_log_error("Invalid GLTF path (NULL or empty)");
		return parsedData;
	}

	parsedData.fileInfo.path = path;

	// read file
	void* data = NULL;
	unsigned long long size = 0;
	if (!platform_fileread(path, &size, &data)) {
		internal_log_error("Failed to read file: %s", path);
		return parsedData;
	}

	// validate minimum file size
	if (size < 4) {
		internal_log_error("File too small to be valid GLTF/GLB");
		gltfmemory_deallocate(data);
		return parsedData;
	}

	const unsigned char* ptr = (const unsigned char*)data;
	int isGlb = strncmp_impl((const char*)ptr, "glTF", 4) == 0 ? 1 : 0;

	if (isGlb) {
		parsedData.fileInfo.type = 0; // GLB

		// validate GLB header
		if (size < GLB_HEADER_SIZE) {
			internal_log_error("GLB file too small (header incomplete)");
			gltfmemory_deallocate(data);
			return parsedData;
		}

		// check version
		unsigned int version;
		memcpy(&version, ptr + 4, 4);
		if (version != 2) {
			internal_log_error("Unsupported GLB version: %u (expected 2)", version);
			gltfmemory_deallocate(data);
			return parsedData;
		}

		// verify total length
		unsigned int totalLength;
		memcpy(&totalLength, ptr + 8, 4);
		if (totalLength != size) {
			internal_log_error("GLB size mismatch (header: %u, actual: %llu)", totalLength, size);
			gltfmemory_deallocate(data);
			return parsedData;
		}

		// Process chunks
		unsigned int offset = GLB_HEADER_SIZE;

		// JSON chunk (required)
		if (offset + GLB_CHUNK_HEADER_SIZE > size) {
			internal_log_error("Invalid GLB chunk header (out of bounds)");
			gltfmemory_deallocate(data);
			return parsedData;
		}

		unsigned int jsonLength;
		memcpy(&jsonLength, ptr + offset, 4);
		offset += 4;

		unsigned int jsonType;
		memcpy(&jsonType, ptr + offset, 4);
		offset += 4;

		if (jsonType != JSON_CHUNK_TYPE) {
			internal_log_error("Missing JSON chunk (found type: 0x%X)", jsonType);
			gltfmemory_deallocate(data);
			return parsedData;
		}

		if (offset + jsonLength > size) {
			internal_log_error("JSON chunk size overflow (%u > %llu)", jsonLength, size - offset);
			gltfmemory_deallocate(data);
			return parsedData;
		}

		// parse JSON
		int parseResult = internal_parse_json((const char*)ptr + offset, jsonLength, &parsedData);
		if (parseResult < 0) {
			internal_log_error("JSON parsing failed with error: %d", parseResult);
			gltfmemory_deallocate(data);
			GLTF_Free(&parsedData); // Clean up any partial parsing
			memset(&parsedData, 0, sizeof(parsedData)); // Ensure we return clean state
			return parsedData;
		}
		offset += jsonLength;

		// BIN chunk (optional)
		if (offset < size) {
			if (offset + GLB_CHUNK_HEADER_SIZE > size) {
				internal_log_error("Invalid BIN chunk header (out of bounds)");
				gltfmemory_deallocate(data);
				GLTF_Free(&parsedData);
				return parsedData;
			}

			unsigned int binLength;
			memcpy(&binLength, ptr + offset, 4);
			offset += 4;

			unsigned int binType;
			memcpy(&binType, ptr + offset, 4);
			offset += 4;

			if (binType == BIN_CHUNK_TYPE) {
				if (offset + binLength > size) {
					internal_log_error("BIN chunk size overflow (%u > %llu)", binLength, size - offset);
					gltfmemory_deallocate(data);
					GLTF_Free(&parsedData);
					return parsedData;
				}

				parsedData.fileInfo.bin = gltfmemory_allocate(binLength, 0);
				if (!parsedData.fileInfo.bin) {
					internal_log_error("Failed to allocate memory for BIN chunk");
					gltfmemory_deallocate(data);
					GLTF_Free(&parsedData);
					return parsedData;
				}
				memcpy(parsedData.fileInfo.bin, ptr + offset, binLength);
				parsedData.fileInfo.binSize = binLength;
			}
		}
	}
	else {
		// regular GLTF (JSON)
		parsedData.fileInfo.type = 1;

		int parseResult = internal_parse_json((const char*)data, size, &parsedData);
		if (parseResult < 0) {
			internal_log_error("GLTF JSON parsing failed with error: %d", parseResult);
			gltfmemory_deallocate(data);
			GLTF_Free(&parsedData);
			memset(&parsedData, 0, sizeof(parsedData));
			return parsedData;
		}
	}
	
	gltfmemory_deallocate(data);
	return parsedData;
}

void GLTF_Free(GLTF2* data) {

	if (!data) return;

	// asset
	gltfmemory_deallocate(data->asset.copyright);
	gltfmemory_deallocate(data->asset.generator);
	gltfmemory_deallocate(data->asset.version);
	gltfmemory_deallocate(data->asset.minVersion);
	gltfmemory_deallocate(data->asset.extras);
	for (unsigned long long i = 0; i < data->asset.extensionsCount; i++) {
		gltfmemory_deallocate(data->asset.extensions[i].name);
		gltfmemory_deallocate(data->asset.extensions[i].data);
	}

	// accessors
	for (unsigned long long i = 0; i < data->accessorsCount; i++){
		gltfmemory_deallocate(data->accessors[i].name);
		gltfmemory_deallocate(&data->accessors[i].extras);
		for (unsigned long long j = 0; j < data->accessors[i].extensionsCount; j++) {
			gltfmemory_deallocate(data->accessors[i].extensions[j].name);
			gltfmemory_deallocate(data->accessors[i].extensions[j].data);
		}
	}
	gltfmemory_deallocate(data->accessors);

	// buffer views
	for (unsigned long long i = 0; i < data->bufferViewsCount; i++){
		gltfmemory_deallocate(data->bufferViews[i].name);
		gltfmemory_deallocate(data->bufferViews[i].data);
		gltfmemory_deallocate(&data->bufferViews[i].extras);
		for (unsigned long long j = 0; j < data->bufferViews[i].extensionsCount; j++) {
			gltfmemory_deallocate(data->bufferViews[i].extensions[j].name);
			gltfmemory_deallocate(data->bufferViews[i].extensions[j].data);
		}
	}
	gltfmemory_deallocate(data->bufferViews);

	// buffers
	for (unsigned long long i = 0; i < data->buffersCount; i++) {
		gltfmemory_deallocate(data->buffers[i].name);
		gltfmemory_deallocate(data->buffers[i].URI);
		gltfmemory_deallocate(&data->buffers[i].extras);
		for (unsigned long long j = 0; j < data->buffers[i].extensionsCount; j++) {
			gltfmemory_deallocate(data->buffers[i].extensions[j].name);
			gltfmemory_deallocate(data->buffers[i].extensions[j].data);
		}
	}
	gltfmemory_deallocate(data->buffers);

	// meshes
	for (unsigned long long i = 0; i < data->meshesCount; i++) {
		gltfmemory_deallocate(data->meshes[i].name);
		// primitives
		for (unsigned long long j = 0; j < data->meshes[i].primitivesCount; j++) {
			// attributes
			for (unsigned long long k = 0; k < data->meshes[i].primitives[j].attributesCount; k++){
				gltfmemory_deallocate(data->meshes[i].primitives[j].attributes[k].name);
			}
			gltfmemory_deallocate(data->meshes[i].primitives[j].attributes);
			// targets
			for (unsigned long long k = 0; k < data->meshes[i].primitives[j].targetsCount; k++) {
				for (unsigned long long m = 0; m < data->meshes[i].primitives[j].targets[k].attributesCount; ++m) {
					gltfmemory_deallocate(data->meshes[i].primitives[j].targets[k].attributes[m].name);
				}
				gltfmemory_deallocate(data->meshes[i].primitives[j].targets[k].attributes);
			}
			gltfmemory_deallocate(data->meshes[i].primitives[j].targets);
			// extras and extensions
			gltfmemory_deallocate(data->meshes[i].primitives[j].extras);
			for (unsigned long long k = 0; k < data->meshes[i].primitives[j].extensionsCount; k++) {
				gltfmemory_deallocate(data->meshes[i].primitives[j].extensions[k].name);
				gltfmemory_deallocate(data->meshes[i].primitives[j].extensions[k].data);
			}
		}
		gltfmemory_deallocate(data->meshes[i].primitives);
		gltfmemory_deallocate(data->meshes[i].weights);
		// target names
		for (unsigned long long j = 0; j < data->meshes[i].targetNamesCount; j++) {
			gltfmemory_deallocate(data->meshes[i].targetNames[j]);
		}
		// extras and extensions
		gltfmemory_deallocate(data->meshes[i].extras);
		for (unsigned long long k = 0; k < data->meshes[i].extensionsCount; k++) {
			gltfmemory_deallocate(data->meshes[i].extensions[k].name);
			gltfmemory_deallocate(data->meshes[i].extensions[k].data);
		}
		gltfmemory_deallocate(data->meshes[i].targetNames);
	}
	gltfmemory_deallocate(data->meshes);

	// materials
	for (unsigned long long i = 0; i < data->materialsCount; i++) {
		gltfmemory_deallocate(data->materials[i].name);
		gltfmemory_deallocate(data->materials[i].extras);
		for (unsigned long long j = 0; j < data->materials[i].extensionsCount; j++) {
			gltfmemory_deallocate(data->materials[i].extensions[j].name);
			gltfmemory_deallocate(data->materials[i].extensions[j].data);
		}
	}
	gltfmemory_deallocate(data->materials);

	// images
	for (unsigned long long i = 0; i < data->imagesCount; i++) {
		gltfmemory_deallocate(data->images[i].name);
		gltfmemory_deallocate(data->images[i].URI);
		gltfmemory_deallocate(data->images[i].mime_type);
		gltfmemory_deallocate(data->images[i].extras);
		for (unsigned long long j = 0; j < data->images[i].extensionsCount; j++) {
			gltfmemory_deallocate(data->images[i].extensions[j].name);
			gltfmemory_deallocate(data->images[i].extensions[j].data);
		}
	}
	gltfmemory_deallocate(data->images);

	// textures
	for (unsigned long long i = 0; i < data->texturesCount; i++){
		gltfmemory_deallocate(data->textures[i].name);
		gltfmemory_deallocate(data->textures[i].extras);
		for (unsigned long long j = 0; j < data->textures[i].extensionsCount; j++) {
			gltfmemory_deallocate(data->textures[i].extensions[j].name);
			gltfmemory_deallocate(data->textures[i].extensions[j].data);
		}
	}
	gltfmemory_deallocate(data->textures);

	// image samplers
	for (unsigned long long i = 0; i < data->imageSamplersCount; i++) {
		gltfmemory_deallocate(data->imageSamplers[i].name);
		gltfmemory_deallocate(data->imageSamplers[i].extras);
		for (unsigned long long j = 0; j < data->imageSamplers[i].extensionsCount; j++) {
			gltfmemory_deallocate(data->imageSamplers[i].extensions[j].name);
			gltfmemory_deallocate(data->imageSamplers[i].extensions[j].data);
		}
	}
	gltfmemory_deallocate(data->imageSamplers);

	// skins
	for (unsigned long long i = 0; i < data->skinsCount; i++) {
		gltfmemory_deallocate(data->skins[i].name);
		gltfmemory_deallocate(data->skins[i].joints);
		gltfmemory_deallocate(data->skins[i].extras);
		for (unsigned long long j = 0; j < data->skins[i].extensionsCount; j++) {
			gltfmemory_deallocate(data->skins[i].extensions[j].name);
			gltfmemory_deallocate(data->skins[i].extensions[j].data);
		}
	}
	gltfmemory_deallocate(data->skins);

	// cameras
	for (unsigned long long i = 0; i < data->camerasCount; i++) {
		gltfmemory_deallocate(data->cameras[i].name);
		if (data->cameras[i].type == CameraType_Perspective) {
			gltfmemory_deallocate(&data->cameras[i].data.perspective.extras);
		}
		else if (data->cameras[i].type == CameraType_Orthographic) {
			gltfmemory_deallocate(&data->cameras[i].data.orthographic.extras);
		}

		gltfmemory_deallocate(data->cameras[i].extras);
		for (unsigned long long j = 0; j < data->cameras[i].extensionsCount; j++) {
			gltfmemory_deallocate(data->cameras[i].extensions[j].name);
			gltfmemory_deallocate(data->cameras[i].extensions[j].data);
		}
	}
	gltfmemory_deallocate(data->cameras);

	// nodes
	for (unsigned long long i = 0; i < data->nodesCount; i++) {
		gltfmemory_deallocate(data->nodes[i].name);
		gltfmemory_deallocate(data->nodes[i].children);
		gltfmemory_deallocate(data->nodes[i].weights);

		gltfmemory_deallocate(data->nodes[i].extras);
		for (unsigned long long j = 0; j < data->nodes[i].extensionsCount; j++) {
			gltfmemory_deallocate(data->nodes[i].extensions[j].name);
			gltfmemory_deallocate(data->nodes[i].extensions[j].data);
		}
	}
	gltfmemory_deallocate(data->nodes);

	// scenes
	for (unsigned long long i = 0; i < data->scenesCount; i++) {
		gltfmemory_deallocate(data->scenes[i].name);
		gltfmemory_deallocate(data->scenes[i].nodes);

		gltfmemory_deallocate(data->scenes[i].extras);
		for (unsigned long long j = 0; j < data->scenes[i].extensionsCount; j++) {
			gltfmemory_deallocate(data->scenes[i].extensions[j].name);
			gltfmemory_deallocate(data->scenes[i].extensions[j].data);
		}
	}
	gltfmemory_deallocate(data->scenes);

	// animations
	for (unsigned long long i = 0; i < data->animationsCount; i++) {
		gltfmemory_deallocate(data->animations[i].name);

		for (unsigned long long j = 0; j < data->animations[i].samplersCount; j++) {
			gltfmemory_deallocate(data->animations[i].samplers[j].extras);
			for (unsigned long long k = 0; k < data->animations[i].samplers[j].extensionsCount; k++) {
				gltfmemory_deallocate(data->animations[i].samplers[j].extensions[k].name);
				gltfmemory_deallocate(data->animations[i].samplers[j].extensions[k].data);
			}
		}
		gltfmemory_deallocate(data->animations[i].samplers);

		for (unsigned long long j = 0; j < data->animations[i].channelsCount; j++) {
			for (unsigned long long j = 0; j < data->animations[i].channelsCount; j++) {
				gltfmemory_deallocate(data->animations[i].channels[j].extras);
				for (unsigned long long k = 0; k < data->animations[i].channels[j].extensionsCount; j++) {
					gltfmemory_deallocate(data->animations[i].channels[j].extensions[k].name);
					gltfmemory_deallocate(data->animations[i].channels[j].extensions[k].data);
				}
			}
		}
		gltfmemory_deallocate(data->animations[i].channels);

		gltfmemory_deallocate(data->animations[i].extras);
		for (unsigned long long j = 0; j < data->animations[i].extensionsCount; j++) {
			gltfmemory_deallocate(data->animations[i].extensions[j].name);
			gltfmemory_deallocate(data->animations[i].extensions[j].data);
		}
	}
	gltfmemory_deallocate(data->animations);

	// extensions
	gltfmemory_deallocate(data->extras);
	for (unsigned long long i = 0; i < data->extensionsCount; i++) {
		gltfmemory_deallocate(data->extensions[i].name);
		gltfmemory_deallocate(data->extensions[i].data);
	}

	for (unsigned long long i = 0; i < data->extensionsUsedCount; i++){
		gltfmemory_deallocate(data->extensionsUsed[i]);
	}
	gltfmemory_deallocate(data->extensionsUsed);

	for (unsigned long long i = 0; i < data->extensionsRequiredCount; i++) {
		gltfmemory_deallocate(data->extensionsRequired[i]);
	}
	gltfmemory_deallocate(data->extensionsRequired);

	// binary values if opened .glb instead of a .gltf
	if (data->fileInfo.type == 0) {
		gltfmemory_deallocate(data->fileInfo.bin);
	}

	gltfmemory_deallocate(data);
}

GLTF_API const char* GLTF_GetErrors() {
	return s_gErrors;
}
