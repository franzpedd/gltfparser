/*
 * MIT License
 *
 * Copyright (c) 2010 Serge Zaitsev
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef JSMN_INCLUDED
#define JSMN_INCLUDED

#include "gltfparser_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief all types of json field types
typedef enum {
	JSMN_UNDEFINED = 0,
	JSMN_OBJECT = 1 << 0,
	JSMN_ARRAY = 1 << 1,
	JSMN_STRING = 1 << 2,
	JSMN_PRIMITIVE = 1 << 3
} jsmntype_t;

/// @brief all types of errors when parsing 
enum jsmnerr {
	
	JSMN_ERROR_NOMEM = -1, // not enough tokens were provided
	JSMN_ERROR_INVAL = -2, // invalid char inside json string
	JSMN_ERROR_PART = -3 // string is no a full json packet, more were expected
};

/// @brief token structure
typedef struct jsmntok {
	jsmntype_t type;
	int start;
	int end;
	int size;
#ifdef JSMN_PARENT_LINKS
	int parent;
#endif
} jsmntok_t;

/// @brief parser structure
typedef struct jsmn_parser {
	unsigned int pos;     // offset in the JSON string
	unsigned int toknext; // next token to allocate
	int toksuper;         // superior token node, e.g. parent object or array
} jsmn_parser;

/// @brief create a JSON parser over an array of tokens
GLTF_API void jsmn_init(jsmn_parser* parser);

/// @brief parses a JSON data string into and array of tokens, each describing a single JSON object
GLTF_API int jsmn_parse(jsmn_parser* parser, const char* js, const unsigned long long len, jsmntok_t* tokens, const unsigned int num_tokens);

#ifdef __cplusplus
}
#endif

#endif // JSMN_INCLUDED