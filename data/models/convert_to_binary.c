#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <windows.h>
#include <direct.h>  // for _mkdir

#define MAX_VERTICES 1000000
#define MAX_LINE 256
#define MAX_PATH_LEN 1024

// Vertex structure with optimal field ordering (total 36 bytes).
typedef struct {
    float position[3]; // 12 bytes
    float normal[3];   // 12 bytes
    float uv[2];       // 8 bytes
    char color[3];     // 3 bytes
    char pad;          // 1 byte of padding so the total size is 36 bytes.
} Vertex;

// Header stored in the binary file.
typedef struct {
    uint32_t vertexCount;
    uint32_t indexCount;
    uint32_t vertexArrayOffset;
    uint32_t indexArrayOffset;
} MeshHeader;

// Global arrays for the unique vertices and indices.
Vertex uniqueVertices[MAX_VERTICES];
int uniqueVertexCount = 0;
uint32_t indices[MAX_VERTICES * 3];
int indexCount = 0;

// Temporary storage for OBJ definitions.
float (*temp_vertices)[3] = NULL;
float (*temp_normals)[3] = NULL;
int v_def_count = 1;   // OBJ indices start at 1.
int vn_def_count = 1;

// Compare two Vertex structs for equality.
int vertex_equal(const Vertex *a, const Vertex *b) {
    return (a->position[0] == b->position[0] &&
            a->position[1] == b->position[1] &&
            a->position[2] == b->position[2] &&
            a->normal[0]   == b->normal[0]   &&
            a->normal[1]   == b->normal[1]   &&
            a->normal[2]   == b->normal[2]   &&
            a->uv[0]       == b->uv[0]       &&
            a->uv[1]       == b->uv[1]       &&
            memcmp(a->color, b->color, 3) == 0);
}

// Search for an existing vertex equal to v in uniqueVertices.
// Returns its index if found, or -1 if not.
int find_vertex(const Vertex *v) {
    for (int i = 0; i < uniqueVertexCount; i++) {
        if (vertex_equal(v, &uniqueVertices[i]))
            return i;
    }
    return -1;
}

// Process a face line of the form: "f v//vn v//vn v//vn"
void process_face_line(const char *line) {
    int vIdx[3], vnIdx[3];
    if (sscanf(line, "f %d//%d %d//%d %d//%d",
               &vIdx[0], &vnIdx[0],
               &vIdx[1], &vnIdx[1],
               &vIdx[2], &vnIdx[2]) != 6)
    {
        return;
    }
    for (int i = 0; i < 3; i++) {
        Vertex temp;
        temp.position[0] = temp_vertices[vIdx[i]][0];
        temp.position[1] = temp_vertices[vIdx[i]][1];
        temp.position[2] = temp_vertices[vIdx[i]][2];
        // No UV info is provided; default to 0.
        temp.uv[0] = 0;
        temp.uv[1] = 0;
        if (vnIdx[i] < vn_def_count) {
            temp.normal[0] = temp_normals[vnIdx[i]][0];
            temp.normal[1] = temp_normals[vnIdx[i]][1];
            temp.normal[2] = temp_normals[vnIdx[i]][2];
        } else {
            temp.normal[0] = temp.normal[1] = temp.normal[2] = 0;
        }
        // Default color to 0 (all zeros).
        memset(temp.color, 0, 3);
        temp.pad = 0; // ensure pad is set

        int found = find_vertex(&temp);
        if (found == -1) {
            if (uniqueVertexCount >= MAX_VERTICES) {
                fprintf(stderr, "Unique vertex buffer overflow!\n");
                exit(1);
            }
            uniqueVertices[uniqueVertexCount] = temp;
            found = uniqueVertexCount;
            uniqueVertexCount++;
        }
        indices[indexCount++] = (uint32_t) found;
    }
}

// Parse an OBJ file.
void parse_obj_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file)
        return;

    char line[MAX_LINE];
    // Allocate temporary arrays.
    temp_vertices = malloc(MAX_VERTICES * sizeof(*temp_vertices));
    temp_normals  = malloc(MAX_VERTICES * sizeof(*temp_normals));
    if (!temp_vertices || !temp_normals) {
        fprintf(stderr, "Memory allocation failed for temporary arrays.\n");
        fclose(file);
        free(temp_vertices);
        free(temp_normals);
        exit(1);
    }
    memset(temp_vertices, 0, MAX_VERTICES * sizeof(*temp_vertices));
    memset(temp_normals, 0, MAX_VERTICES * sizeof(*temp_normals));
    v_def_count = 1;
    vn_def_count = 1;

    while (fgets(line, sizeof(line), file)) {
        char *pline = line;
        while (*pline == ' ' || *pline == '\t') pline++;
        if (strncmp(pline, "v ", 2) == 0) {
            if (v_def_count < MAX_VERTICES) {
                sscanf(pline, "v %f %f %f",
                       &temp_vertices[v_def_count][0],
                       &temp_vertices[v_def_count][1],
                       &temp_vertices[v_def_count][2]);
                v_def_count++;
            }
        } else if (strncmp(pline, "vn ", 3) == 0) {
            if (vn_def_count < MAX_VERTICES) {
                sscanf(pline, "vn %f %f %f",
                       &temp_normals[vn_def_count][0],
                       &temp_normals[vn_def_count][1],
                       &temp_normals[vn_def_count][2]);
                vn_def_count++;
            }
        } else if (pline[0] == 'f') {
            process_face_line(pline);
        }
    }
    fclose(file);
    free(temp_vertices);
    free(temp_normals);
}

// Write the binary mesh file (header, vertices, indices) to disk.
void write_mesh_binary(const char *binFilename) {
    MeshHeader header;
    header.vertexCount = uniqueVertexCount;
    header.indexCount = indexCount;
    header.vertexArrayOffset = sizeof(MeshHeader);
    header.indexArrayOffset = header.vertexArrayOffset + uniqueVertexCount * sizeof(Vertex);

    FILE *file = fopen(binFilename, "wb");
    if (!file) {
        fprintf(stderr, "Failed to open %s for writing\n", binFilename);
        exit(1);
    }
    fwrite(&header, sizeof(MeshHeader), 1, file);
    fwrite(uniqueVertices, sizeof(Vertex), uniqueVertexCount, file);
    fwrite(indices, sizeof(uint32_t), indexCount, file);
    fclose(file);
}

// Read back the binary mesh file and print summary info.
void read_mesh_binary(const char *binFilename) {
    FILE *file = fopen(binFilename, "rb");
    if (!file) {
        fprintf(stderr, "Failed to open %s for reading\n", binFilename);
        exit(1);
    }
    MeshHeader header;
    fread(&header, sizeof(MeshHeader), 1, file);
    Vertex *vertices_in = malloc(header.vertexCount * sizeof(Vertex));
    if (!vertices_in) {
        fprintf(stderr, "Memory allocation failed for vertices_in\n");
        exit(1);
    }
    uint32_t *indices_in = malloc(header.indexCount * sizeof(uint32_t));
    if (!indices_in) {
        fprintf(stderr, "Memory allocation failed for indices_in\n");
        exit(1);
    }
    fseek(file, header.vertexArrayOffset, SEEK_SET);
    fread(vertices_in, sizeof(Vertex), header.vertexCount, file);
    fseek(file, header.indexArrayOffset, SEEK_SET);
    fread(indices_in, sizeof(uint32_t), header.indexCount, file);
    fclose(file);
    size_t fileSize = header.indexArrayOffset + header.indexCount * sizeof(uint32_t);
    double fileSizeKB = fileSize / 1024.0;
    printf("Size of struct Vertex: %zu bytes\n", sizeof(Vertex));
    printf("Unique vertex count: %u\n", header.vertexCount);
    printf("Index count: %u\n", header.indexCount);
    printf("Expected binary file size: %.2f KB\n", fileSizeKB);
    free(vertices_in);
    free(indices_in);
}

// Given an OBJ filename (with path), produce a binary filename in the "meshes" folder.
void make_bin_filename(const char *objFilename, char *binFilename, size_t size) {
    // Extract the base file name.
    const char *base = strrchr(objFilename, '\\');
    if (!base)
        base = strrchr(objFilename, '/');
    if (base)
        base++; // skip separator
    else
        base = objFilename;
    snprintf(binFilename, size, "%s", base);
    char *dot = strrchr(binFilename, '.');
    if (dot)
        strcpy(dot, ".bin");
    else
        strcat(binFilename, ".bin");
}

// Recursive function to process all files in a directory tree.
void process_directory(const char *dirPath) {
    char searchPath[MAX_PATH_LEN];
    snprintf(searchPath, sizeof(searchPath), "%s\\*", dirPath);
    WIN32_FIND_DATA findFileData;
    HANDLE hFind = FindFirstFile(searchPath, &findFileData);
    if (hFind == INVALID_HANDLE_VALUE)
        return;
    
    do {
        if (strcmp(findFileData.cFileName, ".") == 0 ||
            strcmp(findFileData.cFileName, "..") == 0)
            continue;
        
        char fullPath[MAX_PATH_LEN];
        snprintf(fullPath, sizeof(fullPath), "%s\\%s", dirPath, findFileData.cFileName);
        
        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            process_directory(fullPath);
        } else {
            char *ext = strrchr(findFileData.cFileName, '.');
            if (ext && (_stricmp(ext, ".obj") == 0)) {
                uniqueVertexCount = 0;
                indexCount = 0;
                parse_obj_file(fullPath);
                char binBase[MAX_PATH_LEN];
                make_bin_filename(findFileData.cFileName, binBase, sizeof(binBase));
                char finalBinFilename[MAX_PATH_LEN];
                snprintf(finalBinFilename, sizeof(finalBinFilename), "meshes\\%s", binBase);
                write_mesh_binary(finalBinFilename);
                printf("Processed %s -> %s\n", fullPath, finalBinFilename);
                read_mesh_binary(finalBinFilename);
                printf("\n");
            }
        }
    } while (FindNextFile(hFind, &findFileData) != 0);
    FindClose(hFind);
}

int main(void) {
    // Create the output folder "meshes" if it doesn't exist.
    _mkdir("meshes");
    process_directory(".");
    return 0;
}
