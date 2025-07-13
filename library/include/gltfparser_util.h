#ifndef GLTFPARSER_UTILS_INCLUDED
#define GLTFPARSER_UTILS_INCLUDED

#include "gltfparser_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////// memory

/// @brief allocates memory using default malloc, this is usefull if allocating functions are to be modified
/// @param size how many bytes to be allocated
/// @param empty erases all contents within specified size
/// @return the memory's address
GLTF_API void* gltfmemory_allocate(unsigned long long size, int empty);

/// @brief dealocates memory previously allocated
/// @param ptr address to the memory's block
GLTF_API void gltfmemory_deallocate(void* ptr);

/// @brief reallocates previouly allocated memory to fit a new size
/// @param ptr address to the memory
/// @param size new size of the memory's block
/// @return the new ptr of the memory
GLTF_API void* gltfmemory_reallocate(void* ptr, unsigned long long size);

/// @brief erases all content exists in the given address
/// @param ptr memory's address
/// @param size how many bytes to be erased
GLTF_API void gltfmemory_zero(void* ptr, unsigned long long size);

/// @brief copies a block of memory to a new address
/// @param dest where the memory will be moved to
/// @param src where memory previously resided
/// @param size how many bytes the memory occupies
/// @return dest's address
GLTF_API void* gltfmemory_copy(void* dest, const void* src, unsigned long long size);

/// @brief compares two blocks of memory
/// @param s1 the first block of memory
/// @param s2 the second block of memory
/// @param n compares until n bytes
/// @return 0 if both are equal, negative if s1 is less than s2, positve if s1 is greater than s2
GLTF_API int gltfmemory_cmp(const void* s1, const void* s2, unsigned long long n);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////// others

/// @brief reads the contents of a file
/// @param path the path on disk of the file
/// @param size the file output size
/// @param data the read data
/// @return 1 on success, 0 on failure
GLTF_API int platform_fileread(const char* path, unsigned long long* size, void** data);

/// @brief copies a string into another string
/// @param dest destiny string
/// @param src source string
/// @param size size of the  
/// @return the dest address
GLTF_API void* strncpy_impl(void* dest, const char* src, unsigned long long size);

/// @brief performs a lexicographical comparation between two strings
/// @param s1 the first string to compare
/// @param s2 the second string to compare
/// @param n only compares up to n characters
GLTF_API int strncmp_impl(const char* s1, const char* s2, unsigned long long n);

/// @brief searches for the first occurrence of a character in a string
/// @param str the string to be searched in
/// @param c the character to search
GLTF_API char* strchr_impl(const char* str, int c);

#ifdef __cplusplus
}
#endif

#endif // GLTFPARSER_UTILS_INCLUDED