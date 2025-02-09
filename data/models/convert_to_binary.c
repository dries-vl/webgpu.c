#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <windows.h>
#include <direct.h>  // for _mkdir

#define MAX_VERTICES 1000000
#define MAX_LINE 256
#define MAX_PATH_LEN 1024

// --- Vertex structure (36 bytes total) ---
typedef struct {
    float position[3]; // 12 bytes
    float normal[3];   // 12 bytes
    float uv[2];       // 8 bytes
    char color[3];     // 3 bytes
    char pad;          // 1 byte padding => total 36 bytes
} Vertex;

// --- Binary file header ---
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
// We use 1-based indexing so that index 0 is unused.
float (*temp_vertices)[3] = NULL;
float (*temp_uvs)[2] = NULL;
float (*temp_normals)[3] = NULL;
int v_def_count = 1;   // Count of "v" lines (starting at 1)
int vt_def_count = 1;  // Count of "vt" lines (starting at 1)
int vn_def_count = 1;  // Count of "vn" lines (starting at 1)

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

// --- In this design we want one unique vertex per "v" line (in order).
// For each vertex, fill in position from the v line,
// uv from vt if available (otherwise default to {1,1}),
// normal from vn if available (otherwise {0,0,0}),
// and default color {0,0,0}.
void build_unique_vertices() {
    for (int i = 1; i < v_def_count; i++) {
        uniqueVertices[i-1].position[0] = temp_vertices[i][0];
        uniqueVertices[i-1].position[1] = temp_vertices[i][1];
        uniqueVertices[i-1].position[2] = temp_vertices[i][2];
        if (vt_def_count > i) {
            uniqueVertices[i-1].uv[0] = temp_uvs[i][0];
            uniqueVertices[i-1].uv[1] = temp_uvs[i][1];
        } else {
            uniqueVertices[i-1].uv[0] = 1.0f;  // default value
            uniqueVertices[i-1].uv[1] = 1.0f;
        }
        if (vn_def_count > i) {
            uniqueVertices[i-1].normal[0] = temp_normals[i][0];
            uniqueVertices[i-1].normal[1] = temp_normals[i][1];
            uniqueVertices[i-1].normal[2] = temp_normals[i][2];
        } else {
            uniqueVertices[i-1].normal[0] = 0;
            uniqueVertices[i-1].normal[1] = 0;
            uniqueVertices[i-1].normal[2] = 0;
        }
        memset(uniqueVertices[i-1].color, 0, 3);
        uniqueVertices[i-1].pad = 0;
    }
    uniqueVertexCount = v_def_count - 1;
}

// In the second pass we read face lines and build the indices array.
// Here we extract the vertex index from each face token.
// (We ignore the texture and normal indices for the purpose of indexing.)
void process_face_line_indices(const char *line) {
    char buffer[MAX_LINE];
    strncpy(buffer, line, MAX_LINE);
    buffer[MAX_LINE - 1] = '\0';
    
    // Tokenize the line.
    char *token = strtok(buffer, " \t\r\n"); // should be "f"
    int faceIndices[10]; // support up to 10 vertices per face
    int count = 0;
    while ((token = strtok(NULL, " \t\r\n")) != NULL) {
        // The vertex index is the substring before the first '/'
        char *slash = strchr(token, '/');
        if (slash) *slash = '\0';
        int vi = atoi(token);
        faceIndices[count++] = vi - 1; // convert to 0-based
    }
    // Reverse the order of the indices for this face to fix winding.
    // (This is our initial reversal; we will later reassign positions.)
    if (count == 3) {
        indices[indexCount++] = faceIndices[2];
        indices[indexCount++] = faceIndices[1];
        indices[indexCount++] = faceIndices[0];
    } else {
        // Fan triangulation (reversed).
        for (int i = 1; i < count - 1; i++) {
            indices[indexCount++] = faceIndices[count - 1];
            indices[indexCount++] = faceIndices[i];
            indices[indexCount++] = faceIndices[i - 1];
        }
    }
}

// Process all face lines in the OBJ file (second pass).
void process_faces_in_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file)
        return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), file)) {
        char *pline = line;
        while (*pline == ' ' || *pline == '\t') pline++;
        if (pline[0] == 'f') {
            process_face_line_indices(pline);
        }
    }
    fclose(file);
}

// First pass: read the OBJ definitions (v, vt, vn).
void read_obj_definitions(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file)
        return;
    
    temp_vertices = malloc(MAX_VERTICES * sizeof(*temp_vertices));
    temp_uvs = malloc(MAX_VERTICES * sizeof(*temp_uvs));
    temp_normals = malloc(MAX_VERTICES * sizeof(*temp_normals));
    if (!temp_vertices || !temp_uvs || !temp_normals) {
        fprintf(stderr, "Memory allocation failed for temporary arrays.\n");
        fclose(file);
        exit(1);
    }
    memset(temp_vertices, 0, MAX_VERTICES * sizeof(*temp_vertices));
    memset(temp_uvs, 0, MAX_VERTICES * sizeof(*temp_uvs));
    memset(temp_normals, 0, MAX_VERTICES * sizeof(*temp_normals));
    v_def_count = 1;
    vt_def_count = 1;
    vn_def_count = 1;
    
    char line[MAX_LINE];
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
        } else if (strncmp(pline, "vt ", 3) == 0) {
            if (vt_def_count < MAX_VERTICES) {
                sscanf(pline, "vt %f %f",
                       &temp_uvs[vt_def_count][0],
                       &temp_uvs[vt_def_count][1]);
                vt_def_count++;
            }
        } else if (strncmp(pline, "vn ", 3) == 0) {
            if (vn_def_count < MAX_VERTICES) {
                sscanf(pline, "vn %f %f %f",
                       &temp_normals[vn_def_count][0],
                       &temp_normals[vn_def_count][1],
                       &temp_normals[vn_def_count][2]);
                vn_def_count++;
            }
        }
    }
    fclose(file);
}

// --- Reorder indices so that each vertex (from the original unique list)
// always appears in the same triangle slot (0, 1, or 2).
// If an original vertex appears in different slots in different triangles,
// we duplicate it so that each final vertex always occupies one fixed slot.
void reorder_indices() {
    // Allocate a mapping table: mapping[original_vertex][slot] = new vertex index.
    int (*mapping)[3] = malloc(uniqueVertexCount * 3 * sizeof(int));
    if (!mapping) {
        fprintf(stderr, "Mapping allocation failed\n");
        exit(1);
    }
    for (int i = 0; i < uniqueVertexCount; i++) {
        mapping[i][0] = mapping[i][1] = mapping[i][2] = -1;
    }
    
    // Allocate temporary arrays for final vertices and final indices.
    // Worst-case: every index gets its own copy.
    Vertex *finalVertices = malloc(MAX_VERTICES * 3 * sizeof(Vertex));
    if (!finalVertices) {
        fprintf(stderr, "Final vertices allocation failed\n");
        exit(1);
    }
    uint32_t *finalIndices = malloc(indexCount * sizeof(uint32_t));
    if (!finalIndices) {
        fprintf(stderr, "Final indices allocation failed\n");
        exit(1);
    }
    
    int newVertexCount = 0;
    // Process each triangle in the current indices array.
    // We assume indices array length is a multiple of 3.
    for (int i = 0; i < indexCount; i += 3) {
        for (int slot = 0; slot < 3; slot++) {
            int origIndex = indices[i + slot]; // original index in [0, uniqueVertexCount-1]
            if (mapping[origIndex][slot] == -1) {
                mapping[origIndex][slot] = newVertexCount;
                finalVertices[newVertexCount] = uniqueVertices[origIndex]; // copy vertex
                newVertexCount++;
            }
            finalIndices[i + slot] = mapping[origIndex][slot];
        }
    }
    
    uniqueVertexCount = newVertexCount;
    // Copy final vertices and indices back into global arrays.
    memcpy(uniqueVertices, finalVertices, uniqueVertexCount * sizeof(Vertex));
    memcpy(indices, finalIndices, indexCount * sizeof(uint32_t));
    
    free(mapping);
    free(finalVertices);
    free(finalIndices);
}

// Write the binary mesh file.
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

// Read back the binary file and print summary info.
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
    printf("Final vertex count: %u\n", header.vertexCount);
    printf("Index count: %u\n", header.indexCount);
    printf("Expected binary file size: %.2f KB\n", fileSizeKB);
    free(vertices_in);
    free(indices_in);
}

// Given an OBJ filename (with path), produce a binary filename in the "meshes" folder.
void make_bin_filename(const char *objFilename, char *binFilename, size_t size) {
    const char *base = strrchr(objFilename, '\\');
    if (!base)
        base = strrchr(objFilename, '/');
    if (base)
        base++;
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
                // Reset global state.
                uniqueVertexCount = 0;
                indexCount = 0;
                
                // First pass: read v, vt, vn definitions.
                read_obj_definitions(fullPath);
                // Build unique vertices from v definitions.
                build_unique_vertices();
                // Second pass: process face lines to build indices.
                process_faces_in_file(fullPath);
                // Now reorder indices so that each original vertex always appears
                // in the same slot (first, second, or third) in every triangle.
                reorder_indices();
                
                // Build output binary filename.
                char binBase[MAX_PATH_LEN];
                make_bin_filename(fullPath, binBase, sizeof(binBase));
                // Extract only the base name.
                const char *baseName = strrchr(binBase, '\\');
                if (baseName)
                    baseName++;
                else
                    baseName = binBase;
                char finalBinFilename[MAX_PATH_LEN];
                snprintf(finalBinFilename, sizeof(finalBinFilename), "meshes\\%s", baseName);
                
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
    _mkdir("meshes");
    process_directory(".");
    return 0;
}
