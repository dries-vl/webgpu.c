// obj2bin.c
// Compile with: cl /O2 obj2bin.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <windows.h>

// The target vertex struct.
typedef struct {
    float position[3]; // x,y,z
    float normal[3];   // x,y,z
    float uv[2];       // u,v
    char color[3];     // default all zero if not provided
} Vertex;

// The header that will be stored at the beginning of the binary.
typedef struct {
    uint32_t vertexCount;
    uint32_t indexCount;
    uint32_t vertexArrayOffset;
    uint32_t indexArrayOffset;
} MeshHeader;

// Helper vector types for storing OBJ data.
typedef struct {
    float x, y, z;
} Vec3;

typedef struct {
    float u, v;
} Vec2;

// Dynamic array types.
typedef struct {
    Vec3 *data;
    size_t count;
    size_t capacity;
} Vec3Array;

typedef struct {
    Vec2 *data;
    size_t count;
    size_t capacity;
} Vec2Array;

typedef struct {
    Vertex *data;
    size_t count;
    size_t capacity;
} VertexArray;

// Push-back functions for our dynamic arrays.
void push_back_Vec3(Vec3Array *arr, Vec3 v) {
    if(arr->count >= arr->capacity) {
        arr->capacity = arr->capacity ? arr->capacity * 2 : 8;
        arr->data = realloc(arr->data, arr->capacity * sizeof(Vec3));
        if (!arr->data) {
            fprintf(stderr, "Failed to allocate memory for Vec3Array\n");
            exit(1);
        }
    }
    arr->data[arr->count++] = v;
}

void push_back_Vec2(Vec2Array *arr, Vec2 v) {
    if(arr->count >= arr->capacity) {
        arr->capacity = arr->capacity ? arr->capacity * 2 : 8;
        arr->data = realloc(arr->data, arr->capacity * sizeof(Vec2));
        if (!arr->data) {
            fprintf(stderr, "Failed to allocate memory for Vec2Array\n");
            exit(1);
        }
    }
    arr->data[arr->count++] = v;
}

void push_back_Vertex(VertexArray *arr, Vertex v) {
    if(arr->count >= arr->capacity) {
        arr->capacity = arr->capacity ? arr->capacity * 2 : 8;
        arr->data = realloc(arr->data, arr->capacity * sizeof(Vertex));
        if (!arr->data) {
            fprintf(stderr, "Failed to allocate memory for VertexArray\n");
            exit(1);
        }
    }
    arr->data[arr->count++] = v;
}

// Process one OBJ file, parse it, and write out a binary file with header, vertex and index arrays.
void process_obj_file(const char *filepath) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        printf("Failed to open %s\n", filepath);
        return;
    }
    
    // Dynamic arrays for positions, texture coordinates, normals, and our output vertices.
    Vec3Array positions = {0};
    Vec2Array uvs = {0};
    Vec3Array normals = {0}; // using Vec3 for normals
    VertexArray vertices = {0};
    
    char line[1024];
    while(fgets(line, sizeof(line), fp)) {
        // Skip comments or blank lines.
        if(line[0]=='#' || line[0]=='\n')
            continue;
        
        // Process vertex positions.
        if (strncmp(line, "v ", 2) == 0) {
            Vec3 pos;
            if (sscanf(line+2, "%f %f %f", &pos.x, &pos.y, &pos.z) == 3)
                push_back_Vec3(&positions, pos);
        }
        // Process texture coordinates.
        else if (strncmp(line, "vt ", 3) == 0) {
            Vec2 uv;
            if (sscanf(line+3, "%f %f", &uv.u, &uv.v) == 2)
                push_back_Vec2(&uvs, uv);
        }
        // Process normals.
        else if (strncmp(line, "vn ", 3) == 0) {
            Vec3 norm;
            if (sscanf(line+3, "%f %f %f", &norm.x, &norm.y, &norm.z) == 3)
                push_back_Vec3(&normals, norm);
        }
        // Process faces.
        else if (strncmp(line, "f ", 2) == 0) {
            // Copy the face line (skip the "f ") into a buffer.
            char *faceLine = line + 2;
            // Tokenize the line (using space or tab as delimiter).
            char *tokens[16]; // assume a maximum of 16 vertices per face.
            int tokenCount = 0;
            char *token = strtok(faceLine, " \t\r\n");
            while(token && tokenCount < 16) {
                tokens[tokenCount++] = token;
                token = strtok(NULL, " \t\r\n");
            }
            if (tokenCount < 3)
                continue; // not enough vertices for a face
            
            // Each token can be in these forms:
            //   v
            //   v/vt
            //   v//vn
            //   v/vt/vn
            typedef struct {
                int v;
                int vt;
                int vn;
            } FaceIndices;
            FaceIndices faceIndices[16] = {0};
            
            for (int i = 0; i < tokenCount; i++) {
                int v = 0, vt = 0, vn = 0;
                if (strchr(tokens[i], '/') == NULL) {
                    // Only vertex index is provided.
                    v = atoi(tokens[i]);
                } else {
                    // Count the number of slashes.
                    int slashCount = 0;
                    for (char *c = tokens[i]; *c; c++) {
                        if (*c == '/')
                            slashCount++;
                    }
                    if (slashCount == 1) {
                        // Format: v/vt
                        sscanf(tokens[i], "%d/%d", &v, &vt);
                    } else if (slashCount == 2) {
                        if (strstr(tokens[i], "//") != NULL) {
                            // Format: v//vn (no uv)
                            sscanf(tokens[i], "%d//%d", &v, &vn);
                        } else {
                            // Format: v/vt/vn
                            sscanf(tokens[i], "%d/%d/%d", &v, &vt, &vn);
                        }
                    }
                }
                faceIndices[i].v  = v;
                faceIndices[i].vt = vt;
                faceIndices[i].vn = vn;
            }
            
            // For faces with more than 3 vertices, use fan triangulation:
            // Triangle vertices: index 0, i, i+1 for i=1..(n-2)
            for (int i = 1; i < tokenCount - 1; i++) {
                int idx[3] = {0, i, i+1};
                for (int j = 0; j < 3; j++) {
                    int k = idx[j];
                    Vertex vert;
                    // Look up position (OBJ indices are 1-based)
                    if (faceIndices[k].v != 0 && faceIndices[k].v <= (int)positions.count) {
                        Vec3 pos = positions.data[faceIndices[k].v - 1];
                        vert.position[0] = pos.x;
                        vert.position[1] = pos.y;
                        vert.position[2] = pos.z;
                    } else {
                        vert.position[0] = vert.position[1] = vert.position[2] = 0.0f;
                    }
                    // Look up uv coordinates if provided.
                    if (faceIndices[k].vt != 0 && faceIndices[k].vt <= (int)uvs.count) {
                        Vec2 uv = uvs.data[faceIndices[k].vt - 1];
                        vert.uv[0] = uv.u;
                        vert.uv[1] = uv.v;
                    } else {
                        vert.uv[0] = vert.uv[1] = 0.0f;
                    }
                    // Look up normals if provided.
                    if (faceIndices[k].vn != 0 && faceIndices[k].vn <= (int)normals.count) {
                        Vec3 norm = normals.data[faceIndices[k].vn - 1];
                        vert.normal[0] = norm.x;
                        vert.normal[1] = norm.y;
                        vert.normal[2] = norm.z;
                    } else {
                        vert.normal[0] = vert.normal[1] = vert.normal[2] = 0.0f;
                    }
                    // Color is not provided in OBJ; default to 0.
                    vert.color[0] = vert.color[1] = vert.color[2] = 0;
                    
                    push_back_Vertex(&vertices, vert);
                }
            }
        }
    } // end while reading file
    fclose(fp);
    
    // Create output folder "bin" is assumed to exist (or created by main)
    // Build the output file name: "bin/<basename>.bin"
    char outputPath[MAX_PATH];
    const char *filename = strrchr(filepath, '\\');
    if (!filename)
        filename = strrchr(filepath, '/');
    if (filename)
        filename++; // skip the slash
    else
        filename = filepath;
    char basename[256];
    strncpy(basename, filename, sizeof(basename));
    basename[sizeof(basename)-1] = '\0';
    char *dot = strrchr(basename, '.');
    if (dot)
        *dot = '\0';
    
    sprintf(outputPath, "bin\\%s.bin", basename);
    
    // Build a sequential index array.
    uint32_t *indices = malloc(vertices.count * sizeof(uint32_t));
    if (!indices) {
        fprintf(stderr, "Failed to allocate memory for indices\n");
        exit(1);
    }
    for (size_t i = 0; i < vertices.count; i++)
        indices[i] = (uint32_t)i;
    
    // Prepare the header.
    MeshHeader header;
    header.vertexCount = (uint32_t)vertices.count;
    header.indexCount  = (uint32_t)vertices.count; // each vertex is an index
    header.vertexArrayOffset = sizeof(MeshHeader);
    header.indexArrayOffset  = sizeof(MeshHeader) + vertices.count * sizeof(Vertex);
    
    // Write the binary file: first the header, then the vertices, then the indices.
    FILE *out = fopen(outputPath, "wb");
    if (!out) {
        printf("Failed to open output file %s\n", outputPath);
    } else {
        fwrite(&header, sizeof(MeshHeader), 1, out);
        fwrite(vertices.data, sizeof(Vertex), vertices.count, out);
        fwrite(indices, sizeof(uint32_t), vertices.count, out);
        fclose(out);
        printf("Wrote %u vertices and %u indices to %s\n", header.vertexCount, header.indexCount, outputPath);
    }
    
    // Clean up.
    free(indices);
    free(positions.data);
    free(uvs.data);
    free(normals.data);
    free(vertices.data);
}

// Recursively search the given directory for .obj files.
void search_directory(const char *basePath) {
    char searchPath[MAX_PATH];
    sprintf(searchPath, "%s\\*", basePath);
    
    WIN32_FIND_DATA findData;
    HANDLE hFind = FindFirstFile(searchPath, &findData);
    if(hFind == INVALID_HANDLE_VALUE)
        return;
    
    do {
        // Skip current and parent directory entries.
        if (strcmp(findData.cFileName, ".") == 0 ||
            strcmp(findData.cFileName, "..") == 0)
            continue;
        
        char fullPath[MAX_PATH];
        sprintf(fullPath, "%s\\%s", basePath, findData.cFileName);
        
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Recursively search subdirectories.
            search_directory(fullPath);
        } else {
            // Check if file extension is ".obj" (case-insensitive).
            char *ext = strrchr(findData.cFileName, '.');
            if (ext && (_stricmp(ext, ".obj") == 0)) {
                printf("Processing: %s\n", fullPath);
                process_obj_file(fullPath);
            }
        }
    } while (FindNextFile(hFind, &findData));
    
    FindClose(hFind);
}

int main(void) {
    // Create "bin" folder if it does not exist.
    CreateDirectory("bin", NULL);
    
    // Search the current directory and subdirectories.
    search_directory(".");
    
    return 0;
}
