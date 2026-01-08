#include <stdio.h>

#include <string.h>

typedef struct ContentNode {
    const char* filePath;
    int beginingLine;
    int endLine;
} ContentNode;

void fprintf_content_node(FILE* output, ContentNode* node) {
    FILE* file = fopen(node->filePath, "r+");
    if (!file) return;

    char buffer[256];
    int currentLine = 0;
    
    while (currentLine < node->endLine) {
        memset(buffer, 0, sizeof(buffer));
        fgets(buffer, sizeof(buffer), file);

        if (currentLine >= node->beginingLine && currentLine < node->endLine) {
            fprintf(output, "%s", buffer);
        }

        currentLine++;
    }

    fclose(file);
}

/// @brief creates a header only version of the api
void create_header_only() {
    char header[] = {
        "#ifndef GLTFPARSER_INCLUDED\n"
        "#define GLTFPARSER_INCLUDED\n\n"
    };

    char libraries[] = {
        "// Standart libraries used\n\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <stdarg.h>\n"
        "#include <string.h>\n\n"
    };

    char separator0[] = "// Functions definitions\n\n";

    // header, begining line, end line, filepath
    ContentNode definesHeader; definesHeader.beginingLine = 4; definesHeader.endLine = 23; definesHeader.filePath = "../library/include/gltfparser_defines.h";
    ContentNode jsmnHeader; jsmnHeader.beginingLine = 29; jsmnHeader.endLine = 78; jsmnHeader.filePath = "../library/include/jsmn.h";
    ContentNode utilHeader; utilHeader.beginingLine = 4; utilHeader.endLine = 77; utilHeader.filePath = "../library/include/gltfparser_util.h";
    ContentNode typesHeader; typesHeader.beginingLine = 3; typesHeader.endLine = 473; typesHeader.filePath = "../library/include/gltfparser_types.h";
    ContentNode jsonHeader; jsonHeader.beginingLine = 6; jsonHeader.endLine = 44; jsonHeader.filePath = "../library/include/gltfparser_json.h";
    ContentNode parserHeader; parserHeader.beginingLine = 6; parserHeader.endLine = 27; parserHeader.filePath = "../library/include/gltfparser.h";

    char separator1[] = "// Functions implementation\n\n";
    char defineMacroStart[] = "#ifdef GLTFPARSER_IMPLEMENTATION\n\n";

    // source, begining line, end line, filepath
    ContentNode jsmnSource; jsmnSource.beginingLine = 2; jsmnSource.endLine = 359; jsmnSource.filePath = "../library/source/jsmn.c";
    ContentNode utilSource; utilSource.beginingLine = 8; utilSource.endLine = 142; utilSource.filePath = "../library/source/gltfparser_util.c";
    ContentNode jsonSource; jsonSource.beginingLine = 7; jsonSource.endLine = 118; jsonSource.filePath = "../library/source/gltfparser_json.c";
    ContentNode parserSource; parserSource.beginingLine = 10; parserSource.endLine = 2387; parserSource.filePath = "../library/source/gltfparser.c";

    char defineMacroEnd[] = "#endif // GLTFPARSER_IMPLEMENTATION\n\n";

    char footer[] = { "#endif // GLTFPARSER_INCLUDED\n\n" };

    FILE* outputFile = fopen("../library/header_only/gltfparser.h", "w");
    if (!outputFile) return;

    // write stuff
    fprintf(outputFile, "%s", header);
    fprintf(outputFile, "%s", libraries);
    fprintf(outputFile, "%s", separator0);

    fprintf_content_node(outputFile, &definesHeader);
    fprintf_content_node(outputFile, &jsmnHeader);
    fprintf_content_node(outputFile, &utilHeader);
    fprintf_content_node(outputFile, &typesHeader);
    fprintf_content_node(outputFile, &jsonHeader);
    fprintf_content_node(outputFile, &parserHeader);

    fprintf(outputFile, "%s", separator1);
    fprintf(outputFile, "%s", defineMacroStart);

    fprintf_content_node(outputFile, &jsmnSource);
    fprintf_content_node(outputFile, &utilSource);
    fprintf_content_node(outputFile, &jsonSource);
    fprintf_content_node(outputFile, &parserSource);

    fprintf(outputFile, "%s", defineMacroEnd);
    fprintf(outputFile, "%s", footer);

    fclose(outputFile);
}

int main(int argc, char* argv[]) {
    create_header_only();
    return 0;
}