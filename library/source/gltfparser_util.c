#include "gltfparser_util.h"

#include "gltfparser_defines.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void* gltfmemory_allocate(unsigned long long size, int empty) {
    void* ptr = malloc(size);
    if(empty == 1 && ptr != NULL) memset(ptr, 0, size);
    return ptr;
}

void gltfmemory_deallocate(void* ptr) {
    free(ptr);
}

void* gltfmemory_reallocate(void* ptr, unsigned long long size) {
    return realloc(ptr, size);
}

void gltfmemory_zero(void* ptr, unsigned long long size) {
    memset(ptr, 0, size);
}

void* gltfmemory_copy(void* dest, const void* src, unsigned long long size) {
    return memcpy(dest, src, size);
}

int gltfmemory_cmp(const void* s1, const void* s2, unsigned long long n) {
    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;

    if (n == 0) {
        return 0; // standard memcmp returns 0 if n is 0
    }

    while (n-- > 0) {
        if (*p1 != *p2) {
            return (int)(*p1 - *p2); // return the difference of the first differing bytes
        }
        p1++;
        p2++;
    }

    return 0; // all n bytes were equal
}

int platform_fileread(const char* path, unsigned long long* size, void** data) {
    if (!path || !size || !data) {
        return 0;
    }

    FILE* file = fopen(path, "rb");
    if (!file) {
        return 0;
    }

    // Detect file size if not provided
    unsigned long long fileSize = *size;
    if (fileSize == 0) {
        fseek(file, 0, SEEK_END);
        long long length = ftell(file);  // 64-bit offset
        if (length < 0) {
            fclose(file);
            return 0;
        }
        fseek(file, 0, SEEK_SET);
        fileSize = (unsigned long long)length;
    }

    if (fileSize > SIZE_MAX) {
        fclose(file);
        return 0;
    }

    char* fileData = (char*)gltfmemory_allocate(fileSize, 1);
    if (!fileData) {
        fclose(file);
        return 0;
    }

    unsigned long long readSize = fread(fileData, 1, (unsigned long long)fileSize, file);
    fclose(file);

    if (readSize != fileSize) {
        gltfmemory_deallocate(fileData);
        return 0;
    }

    *size = fileSize;
    *data = fileData;
    return 1;
}

void* strncpy_impl(void* dest, const char* src, unsigned long long size) {
    char* d = (char*)dest;
    unsigned long long i;

    for (i = 0; i < size && src[i] != '\0'; i++) {
        d[i] = src[i];
    }

    for (; i < size; i++) {
        d[i] = '\0';
    }

    return dest;
}

int strncmp_impl(const char* s1, const char* s2, unsigned long long n) {
    if (n == 0) return 0;

    // compare characters up to n
    while (n-- > 0) {
        if (*s1 != *s2++) return (*(unsigned char*)s1 - *(unsigned char*)--s2); // characters differ or we reach end of either string
        if (*s1++ == '\0') break; // null terminator
    }

    // strings are equal up to n characters
    return 0;
}

char* strchr_impl(const char* str, int c) {
    if (str == NULL) return NULL;

    while (*str != '\0') {
        if (*str == (char)c) {
            return (char*)str;
        }
        str++;
    }

    // check for the null terminator if c is '\0'
    if ((char)c == '\0') {
        return (char*)str;
    }

    return NULL;
}
