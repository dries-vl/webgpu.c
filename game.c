#include "game_data.h"
#include "game_data.c"

/* GAME STRUCTS */
struct game_data {
    struct Mesh meshes[MAX_MESHES];
    struct Material materials[MAX_MATERIALS];
};
/* GAME STRUCTS */

/* GLOBAL STATE OF THE GAME */
static struct game_data game_data = {0};
/* GLOBAL STATE OF THE GAME */



// *info* fast hash that is not guaranteed to never clash
unsigned int fnv1a(const char *s) {
    unsigned int hash = 2166136261u;  // FNV offset basis for 32-bit
    while (*s) {
        hash ^= (unsigned char)*s++;
        hash *= 16777619;             // FNV prime for 32-bit
    }
    return hash;
}

int find_material_index(const int hash) {
    int empty_index = -1;
    for (int i = 0; i < MAX_MATERIALS; i++) {
        if (game_data.materials[i].used) {
            if (strcmp(game_data.materials[i].hash, hash) == 0) {
                return i; // already exists
            }
        } else if (empty_index == -1) {
            empty_index = i; // keep track of earliest empty slot
        }
    }
    return empty_index; // return empty slot (or -1 if array is full)
}

int wgpuAddUniform(struct Material *material, const void* data, int dataSize) {
    // Inline alignment determination using ternary operators
    int alignment = (dataSize <= 4) ? 4 :
                    (dataSize <= 8) ? 8 :
                    16; // Default for vec3, vec4, mat4x4, or larger
    // Align the offset to the correct boundary (based on WGSL rules)
    int alignedOffset = (material->uniformCurrentOffset + (alignment - 1)) & ~(alignment - 1);
    // Check if the new offset exceeds buffer capacity
    if (alignedOffset + dataSize > UNIFORM_BUFFER_CAPACITY) {
        // todo: print warning on screen or in log that this failed
        return -1;  
    }
    // Copy the data into the aligned buffer
    memcpy(material->uniformData + alignedOffset, data, dataSize);
    // Update the current offset
    material->uniformCurrentOffset = alignedOffset + dataSize;
    // todo: print on screen that uniform changed
    return alignedOffset;
}

void wgpuSetUniformValue(struct Material *material, int offset, const void* data, int dataSize) {
    if (offset < 0 || offset + dataSize > material->uniformCurrentOffset) {
        // todo: print warning on screen or in log that this failed
        return;
    }
    memcpy(material->uniformData + offset, data, dataSize);
}