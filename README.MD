## About

This is a [GLTF 2.0](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html) parser written in C based on the header-only
[CGLTF](https://github.com/jkuhlmann/cgltf).
This parser handles both GLB and GLTF formats but without any support for extensions, but it may be latter added with proper testing.
It uses CMake for the solution generation and default builds to a Static Library but can be built as a Dynamic Library as well. A header-only version is also available but be carefull with it, since it's automatically generated using the [developer tools](devtools.c).

## Building
The following defines are available for building process: 
   * GLTF_SHARED_LIBRARY : Changes the build to a dynamic library (default is static library)
   * GLTF_ENABLE_ASSERTS : Assets and stop the application when a parsing error has occured. This undefined (default) will only logs the errors.
   * GLTF_LOG_BUFFER_SIZE : Sets a custom size for the errors encontered when parsing, default is 2048 characters, increase it if many errors happens.
   * GLTF_BUILD_EXAMPLE : Builds an example on how to use the library.
   * GLTF_BUILD_TOOLS : Builds the developer tool that creates a header-only version of the library.

In order to build the library statically, clone this repo and:
   1) Create a folder to build the library into, like ```mkdir build```
   2) Go into such folder and run ```cmake ..```
   3) Either a solution or the project will be built in the desired folder

The dynamic library can be built as following:
   1) Create a folder to build the library into, like ```mkdir build```
   2) Go into such folder and run ```cmake .. -DGLTF_SHARED_LIBRARY```
   3) Either a solution or the project will be built in the desired folder

The header-only version is located [here](library/header_only/gltfparser.h) and it's generated using the [developer tools](devtools.c)

## Usage
You may want to check the [example.c](example.c) program or:
* Call ```GLTF_ParseFromFile(const char* path)``` to parse a gltf file from a filepath and recieve it's <b>GLTF_Data</b>.
* Check ```GLTF_GetErrors()``` to see any parsing error. When <b>GLTF_ENABLE_ASSERTS</b> is defined, any parsing error will lead to a holt in the program, causing it to stop. 
* Finally don't forget to call ```GLTF_Free()``` in order to free the resources used internally by the parser.

## License
This project is based on [CGLTF](https://github.com/jkuhlmann/cgltf), wich uses [JSMN](https://github.com/zserge/jsmn) for the json parser. Both of them uses MIT license, therefore it's only fair
that this library also uses the MIT license.