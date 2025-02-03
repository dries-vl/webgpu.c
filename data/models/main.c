#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <stdint.h>

#define MAX_VERTICES 1000000

typedef struct {
    float x, y, z;    // Vertex Position
    float u, v;       // UV Coordinates
    float nx, ny, nz; // Normal
    uint32_t color;   // Color (default 0)
} Vertex;

Vertex vertices[MAX_VERTICES];
int vertex_count = 0;

// Parse a .obj file and extract vertices
void parse_obj_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) return;

    float temp_vertices[MAX_VERTICES][3] = {0};
    float temp_uvs[MAX_VERTICES][2] = {0};
    float temp_normals[MAX_VERTICES][3] = {0};

    int v_index = 1, vt_index = 1, vn_index = 1;
    char line[256];

    while (fgets(line, sizeof(line), file)) {
        if (line[0] == 'v' && line[1] == ' ') 
            sscanf(line, "v %f %f %f", &temp_vertices[v_index][0], &temp_vertices[v_index][1], &temp_vertices[v_index][2]), v_index++;
        else if (line[0] == 'v' && line[1] == 't') 
            sscanf(line, "vt %f %f", &temp_uvs[vt_index][0], &temp_uvs[vt_index][1]), vt_index++;
        else if (line[0] == 'v' && line[1] == 'n') 
            sscanf(line, "vn %f %f %f", &temp_normals[vn_index][0], &temp_normals[vn_index][1], &temp_normals[vn_index][2]), vn_index++;
        else if (line[0] == 'f') {
            int v[3], vt[3] = {0}, vn[3] = {0};
            int matches = sscanf(line, "f %d/%d/%d %d/%d/%d %d/%d/%d",
                                 &v[0], &vt[0], &vn[0], &v[1], &vt[1], &vn[1], &v[2], &vt[2], &vn[2]);

            if (matches < 3) continue;

            for (int i = 0; i < 3; i++, vertex_count++) {
                if (vertex_count >= MAX_VERTICES) break;
                vertices[vertex_count] = (Vertex){
                    temp_vertices[v[i]][0], temp_vertices[v[i]][1], temp_vertices[v[i]][2],
                    vt[i] ? temp_uvs[vt[i]][0] : 0, vt[i] ? temp_uvs[vt[i]][1] : 0,
                    vn[i] ? temp_normals[vn[i]][0] : 0, vn[i] ? temp_normals[vn[i]][1] : 0, vn[i] ? temp_normals[vn[i]][2] : 0,
                    0
                };
            }
        }
    }
    fclose(file);
}

// Scan the current directory for .obj files (Windows API)
void scan_directory() {
    WIN32_FIND_DATA findFileData;
    HANDLE hFind = FindFirstFile("*.obj", &findFileData);

    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        parse_obj_file(findFileData.cFileName);
    } while (FindNextFile(hFind, &findFileData) != 0);

    FindClose(hFind);
}

// Write binary output
void write_binary_output() {
    FILE *file = fopen("output.bin", "wb");
    if (!file) return;
    fwrite(vertices, sizeof(Vertex), vertex_count, file);
    fclose(file);
}

int main() {
    scan_directory();
    write_binary_output();
    return 0;
}
