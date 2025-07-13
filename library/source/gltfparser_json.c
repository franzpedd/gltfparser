#include "gltfparser_json.h"

#include "gltfparser_util.h"

#include <stdlib.h>
#include <string.h>

int json_strncmp(const char* data, const jsmntok_t* tok, const char* str) {
	if (tok->type != JSMN_STRING) return -1; 

	unsigned long long strLen = strlen(str);
	unsigned long long nameLength = (unsigned long long)(tok->end - tok->start);
	return strncmp_impl((const char*)data + tok->start, str, strLen);
}

int json_to_bool(const char* data, const jsmntok_t* tok) {
	int size = (int)(tok->end - tok->start);
	return size == 4 && gltfmemory_cmp(data + tok->start, "true", 4) == 0;
}

int json_to_int(const char* data, const jsmntok_t* tok) {
	if (tok->type != JSMN_PRIMITIVE) return -1;

	char tmp[128];
	int size = (unsigned long long)(tok->end - tok->start) < sizeof(tmp) ? (int)(tok->end - tok->start) : (int)(sizeof(tmp) - 1);
	strncpy_impl(tmp, (const char*)data + tok->start, size);
	tmp[size] = 0;
	return atoi(tmp);
}

float json_to_float(const char* data, const jsmntok_t* tok) {
	if (tok->type != JSMN_PRIMITIVE) return -1;

	char tmp[128];
	int size = (unsigned long long)(tok->end - tok->start) < sizeof(tmp) ? (int)(tok->end - tok->start) : (int)(sizeof(tmp) - 1);
	strncpy_impl(tmp, (const char*)data + tok->start, size);
	tmp[size] = 0;
	return (float)atof(tmp);
}

unsigned long long json_to_size(const char* data, const jsmntok_t* tok) {
	if (tok->type != JSMN_PRIMITIVE) return -1;

	char tmp[128];
	int size = (unsigned long long)(tok->end - tok->start) < sizeof(tmp) ? (int)(tok->end - tok->start) : (int)(sizeof(tmp) - 1);
	strncpy_impl(tmp, (const char*)data + tok->start, size);
	tmp[size] = 0;
	long long res = atoll(tmp);
	return res < 0 ? 0 : (unsigned long long)res;
}

int json_parse_string(const char* data, const jsmntok_t* tokens, int i, char** outString) {
	if (tokens[i].type != JSMN_STRING) return -1;

	int size = (int)(tokens[i].end - tokens[i].start);
	char* result = (char*)gltfmemory_allocate(size + 1, 0);
	if (!result) return -1;

	strncpy_impl(result, (const char*)data + tokens[i].start, size);
	result[size] = 0;
	*outString = result;
	return i + 1;
}

int json_parse_array(const char* data, const jsmntok_t* tokens, int i, unsigned long long elementSize, void** outArr, unsigned long long* outSize) { 
	if (tokens[i].type != JSMN_ARRAY) return -1;

	int size = tokens[i].size;
	void* result = gltfmemory_allocate(elementSize * size, 1);
	if (!result) return -1;

	*outArr = result;
	*outSize = size;
	return i + 1;
}

int json_parse_array_float(const char* data, const jsmntok_t* tokens, int i, float* outArray, int size) {
	if (tokens[i].type != JSMN_ARRAY) return -1;
	if (tokens[i].size != size) return -1;

	++i;
	for (int j = 0; j < size; ++j) {
		if (tokens[i].type != JSMN_PRIMITIVE) return -1;
		outArray[j] = json_to_float(data, tokens + i);
		++i;
	}
	return i;
}

int json_parse_array_string(const char* data, const jsmntok_t* tokens, int i, char*** outArr, unsigned long long* outSize) {
	if (tokens[i].type != JSMN_ARRAY) return -1;
	i = json_parse_array(data, tokens, i, sizeof(char*), (void**)outArr, outSize);
	if (i < 0) return i;

	for (unsigned long long j = 0; j < *outSize; ++j) {
		i = json_parse_string(data, tokens, i, j + (*outArr));
		if (i < 0) return i;
	}
	return i;
}

int json_parse_skip(const jsmntok_t* tokens, int i) {
	int end = i + 1;

	while (i < end) {
		switch (tokens[i].type)
		{
			case JSMN_OBJECT: { end += tokens[i].size * 2; break; }
			case JSMN_ARRAY: { end += tokens[i].size; break; }
			case JSMN_PRIMITIVE:
			case JSMN_STRING: break;
			default: return -1;
		}
		i++;
	}
	return i;
}
