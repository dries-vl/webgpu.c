#include "game_data.h"
#include "game_data.c"

#ifndef __wasm__
#include <math.h>
#endif

/* GAME STRUCTS */
struct game_data {
    struct Mesh meshes[MAX_MESHES];
    struct Material materials[MAX_MATERIALS];
};
struct Vector3 {
    float x, y, z;
};
struct ButtonState {
    int left;
    int right;
    int forward;
    int backward;
};
/* GAME STRUCTS */

/* GLOBAL STATE OF THE GAME */
static struct game_data game_data = {0};
float camera[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, -300.0f,
        0.0f, 0.0f, 0.0f, 1
};
float cameraRotation[2] = {0.0f, 0.0f}; // yaw, pitch
struct ButtonState buttonState = {0, 0, 0, 0};
/* GLOBAL STATE OF THE GAME */

/* CONSTS */
float fov = 3.14f / 4.0f; // 45 degrees
float farClip = 20000000.0f;
float nearClip = 1.0f;
/* CONSTS */

void multiply(const float *a, int row1, int col1,
              const float *b, int row2, int col2, float *d) {
    // Use a variable-length array allocated on the stack
    float temp[row1 * col2];

    for (int i = 0; i < row1; i++) {
        for (int j = 0; j < col2; j++) {
            float sum = 0.0f;
            for (int k = 0; k < col1; k++) {
                sum += a[i * col1 + k] * b[k * col2 + j];
            }
            temp[i * col2 + j] = sum;
        }
    }

    // Copy the results from temp to d
    for (int i = 0; i < row1 * col2; i++) {
        d[i] = temp[i];
    }
}

void inverseViewMatrix(const float m[16], float inv[16]) {
    // Extract the 3x3 rotation matrix
    float rot[9] = {
        m[0], m[1], m[2],
        m[4], m[5], m[6],
        m[8], m[9], m[10]
    };

    // Transpose the rotation matrix (which is its inverse if it's orthonormal)
    float rotT[9] = {
        rot[0], rot[3], rot[6],
        rot[1], rot[4], rot[7],
        rot[2], rot[5], rot[8]
    };

    // Extract and transform the translation vector
    float translation[3] = {-m[3], -m[7], -m[11]};
    float newTranslation[3];
    multiply(rotT, 3, 3, translation, 3, 1, newTranslation);

    // Construct the inverse matrix
    inv[0] = rotT[0]; inv[1] = rotT[1]; inv[2] = rotT[2]; inv[3] = newTranslation[0];
    inv[4] = rotT[3]; inv[5] = rotT[4]; inv[6] = rotT[5]; inv[7] = newTranslation[1];
    inv[8] = rotT[6]; inv[9] = rotT[7]; inv[10] = rotT[8]; inv[11] = newTranslation[2];
    inv[12] = 0.0f; inv[13] = 0.0f; inv[14] = 0.0f; inv[15] = 1.0f;
}

void move(struct Vector3 move, float *matrix) {
    matrix[3] += move.x;
    matrix[7] += move.y;
    matrix[11] += move.z;
}

void yaw(float angle, float *matrix) {
    // Create the yaw rotation matrix.
    float yawRot[16] = {
         cos(angle), 0.0f, sin(angle), 0.0f,
         0.0f,       1.0f, 0.0f,       0.0f,
        -sin(angle), 0.0f, cos(angle), 0.0f,
         0.0f,       0.0f, 0.0f,       1.0f
    };

    // Create a temporary matrix to hold the new rotation.
    float newRot[16] = {0};

    // Multiply the yaw rotation by the current rotation part.
    // Since the rotation is stored in the upper 3x3 block, we multiply the full matrices
    // but only update the corresponding 3x3 part.
    multiply(yawRot, 4, 4, matrix, 4, 4, newRot);

    // Update only the rotation portion (indices 0,1,2; 4,5,6; 8,9,10) of the original matrix.
    matrix[0] = newRot[0];
    matrix[1] = newRot[1];
    matrix[2] = newRot[2];

    matrix[4] = newRot[4];
    matrix[5] = newRot[5];
    matrix[6] = newRot[6];

    matrix[8] = newRot[8];
    matrix[9] = newRot[9];
    matrix[10] = newRot[10];

    // Leave indices 3, 7, and 11 (translation) unchanged.
}
void pitch(float angle, float *matrix) { // for rotating around itself
    float rotMatrix[16] = {
         1.0f, 0.0f, 0.0f, 0.0f,
         0.0f, cos(angle), -sin(angle), 0.0f,
         0.0f, sin(angle), cos(angle), 0.0f,
         0.0f, 0.0f, 0.0f, 1
    };

    // Create a temporary matrix to hold the new rotation.
    float newRot[16] = {0};

    // Multiply the yaw rotation by the current rotation part.
    // Since the rotation is stored in the upper 3x3 block, we multiply the full matrices
    // but only update the corresponding 3x3 part.
    multiply(rotMatrix, 4, 4, matrix, 4, 4, newRot);

    // Update only the rotation portion (indices 0,1,2; 4,5,6; 8,9,10) of the original matrix.
    matrix[0] = newRot[0];
    matrix[1] = newRot[1];
    matrix[2] = newRot[2];

    matrix[4] = newRot[4];
    matrix[5] = newRot[5];
    matrix[6] = newRot[6];

    matrix[8] = newRot[8];
    matrix[9] = newRot[9];
    matrix[10] = newRot[10];

    // Leave indices 3, 7, and 11 (translation) unchanged.
}

void absolute_yaw(float angle, float *matrix){
    float yawRot[16] = {
        cos(angle + cameraRotation[0]), 0.0f, sin(angle + cameraRotation[0]), 0.0f,
        0.0f,       1.0f, 0.0f,       0.0f,
       -sin(angle + cameraRotation[0]), 0.0f, cos(angle + cameraRotation[0]), 0.0f,
        0.0f,       0.0f, 0.0f,       1.0f
    };
    float pitchRot[16] = {
         1.0f, 0.0f, 0.0f, 0.0f,
         0.0f, cos(cameraRotation[1]), -sin(cameraRotation[1]), 0.0f,
         0.0f, sin(cameraRotation[1]), cos(cameraRotation[1]), 0.0f,
         0.0f, 0.0f, 0.0f, 1
    };
    float rotMatrix[16] = {0};
    multiply(yawRot, 4, 4, pitchRot, 4, 4, rotMatrix);
    matrix[0] = rotMatrix[0];
    matrix[1] = rotMatrix[1];
    matrix[2] = rotMatrix[2];
    matrix[4] = rotMatrix[4];
    matrix[5] = rotMatrix[5];
    matrix[6] = rotMatrix[6];
    matrix[8] = rotMatrix[8];
    matrix[9] = rotMatrix[9];
    matrix[10] = rotMatrix[10];
    cameraRotation[0] += angle;
}
void absolute_pitch(float angle, float *matrix){
    float pitchRot[16] = {
         1.0f, 0.0f, 0.0f, 0.0f,
         0.0f, cos(angle + cameraRotation[1]), -sin(angle + cameraRotation[1]), 0.0f,
         0.0f, sin(angle + cameraRotation[1]), cos(angle + cameraRotation[1]), 0.0f,
         0.0f, 0.0f, 0.0f, 1
    };
    float yawRot[16] = {
        cos(cameraRotation[0]), 0.0f, sin(cameraRotation[0]), 0.0f,
        0.0f,       1.0f, 0.0f,       0.0f,
       -sin(cameraRotation[0]), 0.0f, cos(cameraRotation[0]), 0.0f,
        0.0f,       0.0f, 0.0f,       1.0f
    };
    float rotMatrix[16] = {0};
    multiply(yawRot, 4, 4, pitchRot, 4, 4, rotMatrix);
    matrix[0] = rotMatrix[0];
    matrix[1] = rotMatrix[1];
    matrix[2] = rotMatrix[2];
    matrix[4] = rotMatrix[4];
    matrix[5] = rotMatrix[5];
    matrix[6] = rotMatrix[6];
    matrix[8] = rotMatrix[8];
    matrix[9] = rotMatrix[9];
    matrix[10] = rotMatrix[10];
    cameraRotation[1] += angle;
}

struct Speed {
    float x;
    float y;
    float z;
    float yaw;
    float pitch;
    float roll;
};
struct Speed cameraSpeed = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}; // only y-speed used
float movementSpeed = 7.5f;
void cameraMovement(float *camera, float speed, float ms) {
    float yawRot[16] = {
        cos(cameraRotation[0]), 0.0f, sin(cameraRotation[0]),
        0.0f,       1.0f, 0.0f,
       -sin(cameraRotation[0]), 0.0f, cos(cameraRotation[0])
    };
    float xSpeed = speed * ms * (buttonState.right - buttonState.left);
    float ySpeed = cameraSpeed.y * ms;
    float zSpeed = speed * ms * -(buttonState.forward - buttonState.backward);
    float transSpeed[3] = {xSpeed, ySpeed, zSpeed};
    multiply(yawRot, 3, 3, transSpeed, 3, 1, transSpeed); // in world coords
    struct Vector3 movit = {transSpeed[0], transSpeed[1], transSpeed[2]};
    move(movit, camera);
}
void applyGravity(struct Speed *speed, float *pos, float ms) { // gravity as velocity instead of acceleration
    float gravity = 9.81f * 0.001f;
    float gravitySpeed = gravity * ms;
    if (pos[1] > 0.0f){
        speed->y -= gravitySpeed;
        // if (pos[1] < 0.0f) {pos[1] = 0.0f;}; // this doesn't work
    }
    else { // some sort of hit the ground / collision detection
        speed->y = 0.0f;
    }
}

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
            if (game_data.materials[i].hash == hash) {
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