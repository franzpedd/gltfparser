#ifndef GLTFPARSER_DEFINES_INCLUDED
#define GLTFPARSER_DEFINES_INCLUDED

/// @brief support for building as a shared-library
#ifdef GLTF_SHARED_LIBRARY
#ifdef _MSC_VER
	#ifdef GLTF_SHARED_LIBRARY_EXPORT
		#define GLTF_API __declspec(dllexport)
	#else
		#define GLTF_API __declspec(dllimport)
#endif
	#else
		#define GLTF_API __attribute__((visibility("default")))
	#endif
#else
	#define GLTF_API
#endif

/// @brief sets how many characters the loging system can hold
#ifndef GLTF_LOG_BUFFER_SIZE
#define GLTF_LOG_BUFFER_SIZE 2048
#endif

#endif // GLTFPARSER_DEFINES_INCLUDED
