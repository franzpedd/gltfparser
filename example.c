#include <stdio.h>
#include "gltfparser.h"

#define CUBE_PATH "assets/cube/cube.gltf"

int main(int argc, char** argv) {

	GLTF2 cube = GLTF_ParseFromFile(CUBE_PATH);

	printf("%s\n", GLTF_GetErrors());

	GLTF_Free(&cube);
	return 0;
}