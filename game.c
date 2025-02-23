#include "game_data.h"
#include "game_data.c"

/* GAME STRUCTS */
struct game_data {
    struct Mesh meshes[MAX_MESHES];
    struct Material materials[MAX_MATERIALS];
};

struct Rigid_Body {
    struct Vector3 position;
    struct Vector3 *vertices;
    struct Vector3 *normals;
    int vertex_count;
    int normal_count;
    float radius;
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


// ------------------------------------------------------FYSICS--------------------------------------------------------------

float dot(struct Vector3 a, struct Vector3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
// Helper: Add two vectors
struct Vector3 add(struct Vector3 a, struct Vector3 b) {
    struct Vector3 result = { a.x + b.x, a.y + b.y, a.z + b.z };
    return result;
}
// normalise vector
struct Vector3 normalise(struct Vector3 a) {
    if (a.x == 0.0f && a.y == 0.0f && a.z == 0.0f) { // division by zero
        return a;
    }
    float length = sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    struct Vector3 result = { a.x / length, a.y / length, a.z / length };
    return result;
}

int detectRadialCollision(struct Vector3 position1, struct Vector3 position2, float radius1, float radius2) { // returns boolean
    float distance = (position1.x - position2.x) * (position1.x - position2.x) +
                           (position1.y - position2.y) * (position1.y - position2.y) +
                           (position1.z - position2.z) * (position1.z - position2.z);
    return distance < (radius1 + radius2) * (radius1 + radius2);
}
struct Vector3 detectCollision(struct Rigid_Body body1, struct Rigid_Body body2) { // is passed by value // maybe make version that stops at non collision
    int normal_count = body1.normal_count + body2.normal_count;
    int collision = 1; // assume collision, is boolean
    float separation = -1000.0f; // make bigger!!!!!!!!!!!!!!!!
    struct Vector3 collisionNormal = {0.0f, 0.0f, 0.0f};

    for (int n = 0; n < normal_count; n++) {
        struct Vector3 normal = n < body1.normal_count ? body1.normals[n] : body2.normals[n - body1.normal_count];
        normal = normalise(normal);
        float smallest1 = dot(add(body1.vertices[0], body1.position), normal);
        float largest1 = smallest1;
        for (int v = 1; v < body1.vertex_count; v++) {
            struct Vector3 vertex = add(body1.vertices[v], body1.position);
            float projection = dot(vertex, normal);
            if (projection < smallest1) {
                smallest1 = projection;
            } else if (projection > largest1) {
                largest1 = projection;
            }
        }
        float smallest2 = dot(add(body2.vertices[0], body2.position), normal);
        float largest2 = smallest2;
        for (int v = 1; v < body2.vertex_count; v++) {
            struct Vector3 vertex = add(body2.vertices[v], body2.position);
            float projection = dot(vertex, normal);
            if (projection < smallest2) {
                smallest2 = projection;
            } else if (projection > largest2) {
                largest2 = projection;
            }
        }
        if (largest1 < smallest2 || largest2 < smallest1) {
            if (collision == 1) {
                collision = 0;
                separation = 1000.0f; // make bigger!!!!!!!!!!!!!!!!
                collisionNormal = (struct Vector3){0.0f, 0.0f, 0.0f};
                return collisionNormal; // temporary no collision return 0,0,0
            }
            float separationTemp = largest1 < smallest2 ? smallest2 - largest1 : smallest1 - largest2;
            if (separationTemp < separation) {
                separation = separationTemp;
                collisionNormal.x = normal.x;
                collisionNormal.y = normal.y;
                collisionNormal.z = normal.z;
            }
        }
        else if (collision == 1){
            float overlap1 = largest1 - smallest2;
            float overlap2 = largest2 - smallest1;
            overlap1 = fabs(overlap1);
            overlap2 = fabs(overlap2);
            float overlap = overlap1 < overlap2 ? overlap1 : overlap2;
            if (-overlap > separation) { // remember that separation is negative
                separation = -overlap;
                collisionNormal.x = normal.x;
                collisionNormal.y = normal.y;
                collisionNormal.z = normal.z;
            }
        }
    }
    collisionNormal = normalise(collisionNormal);
    collisionNormal.x *= separation;
    collisionNormal.y *= separation;
    collisionNormal.z *= separation;
    collisionNormal.x = body1.position.x > collisionNormal.x ? -collisionNormal.x : collisionNormal.x;
    collisionNormal.y = body1.position.y > collisionNormal.y ? -collisionNormal.y : collisionNormal.y;
    collisionNormal.z = body1.position.z > collisionNormal.z ? -collisionNormal.z : collisionNormal.z;
    printf("collision: %f, %f, %f\n", collisionNormal.x, collisionNormal.y, collisionNormal.z);
    return collisionNormal;

}; // SAT