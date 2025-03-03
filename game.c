// todo: do not import these here, either just have print_on_screen function passed in or defined here instead
#include "game_data.h"
#include "game_data.c"

#ifndef __wasm__
#include <math.h>
#endif

/* GAME STRUCTS */
struct Vector3 {
    float x, y, z;
};
struct ButtonState {
    int left;
    int right;
    int forward;
    int backward;
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
static float brightness = 1.0f;
static float timeVal = 0.0f;
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
float movementSpeed = 0.5f;

// add cube collision box
struct Rigid_Body cubeCollisionBox = {
    .vertices = (struct Vector3[]) {
        {100.0f, 100.0f, 100.0f},
        {100.0f, 100.0f, -100.0f},
        {100.0f, -100.0f, 100.0f},
        {100.0f, -100.0f, -100.0f},
        {-100.0f, 100.0f, 100.0f},
        {-100.0f, 100.0f, -100.0f},
        {-100.0f, -100.0f, 100.0f},
        {-100.0f, -100.0f, -100.0f}
    },
    .normals = (struct Vector3[]) {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {-1.0f, 0.0f, 0.0f},
        {0.0f, -1.0f, 0.0f},
        {0.0f, 0.0f, -1.0f}
    },
    .normal_count = 6,
    .vertex_count = 8,
    .position = {0.0f, 0.0f, 0.0f},
    .radius = 3.0f
};

struct Rigid_Body cameraCollisionBox = {
    .vertices = (struct Vector3[]) {
        {20.0f, 20.0f, 20.0f},
        {20.0f, 20.0f, -20.0f},
        {20.0f, -120.0f, 20.0f},
        {20.0f, -120.0f, -20.0f},
        {-20.0f, 20.0f, 20.0f},
        {-20.0f, 20.0f, -20.0f},
        {-20.0f, -120.0f, 20.0f},
        {-20.0f, -120.0f, -20.0f}
    },
    .normals = (struct Vector3[]) {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {-1.0f, 0.0f, 0.0f},
        {0.0f, -1.0f, 0.0f},
        {0.0f, 0.0f, -1.0f}
    },
    .normal_count = 6,
    .vertex_count = 8,
    .position = {0.0f, 0.0f, -300.0f},
    .radius = 3.0f
};

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
    return collisionNormal;

}; // SAT

void collisionDetectionCamera(struct Rigid_Body cubeCollisionBox) { // nu enkel met onze collision box
    struct Vector3 separation = detectCollision(cameraCollisionBox, cubeCollisionBox);
    if (separation.x != 0.0f || separation.y != 0.0f || separation.z != 0.0f) {
        camera[3] += separation.x; // undo movement
        camera[7] += separation.y;
        camera[11] += separation.z;
        cameraCollisionBox.position.x = camera[3];
        cameraCollisionBox.position.y = camera[7];
        cameraCollisionBox.position.z = camera[11];
        float proj = dot(normalise(separation), (struct Vector3){cameraSpeed.x, cameraSpeed.y, cameraSpeed.z});
        cameraSpeed.x = 0; // proj * normalise(separation).x;
        cameraSpeed.y = 0; // proj * normalise(separation).y;
        cameraSpeed.z = 0; // proj * normalise(separation).z;
    }
    char output_string[256];
    snprintf(output_string, sizeof(output_string), "%4.2f,%4.2f,%4.2f\n", separation.x, separation.y, separation.z);
    print_on_screen(output_string);
    
    char output_string2[256];
    snprintf(output_string2, sizeof(output_string2), "%4.2f,%4.2f,%4.2f\n", camera[3], camera[7], camera[11]);
    print_on_screen(output_string2);
}

void cameraMovement(float *camera, float speed, float ms) {
    cameraSpeed.x = speed * (buttonState.right - buttonState.left);
    cameraSpeed.z = speed * -(buttonState.forward - buttonState.backward);
    
    float yawRot[16] = {
        cos(cameraRotation[0]), 0.0f, sin(cameraRotation[0]),
        0.0f,       1.0f, 0.0f,
       -sin(cameraRotation[0]), 0.0f, cos(cameraRotation[0])
    };
    float xSpeed = cameraSpeed.x * ms;
    float ySpeed = cameraSpeed.y * ms;
    float zSpeed = cameraSpeed.z * ms;
    float transSpeed[3] = {xSpeed, ySpeed, zSpeed};
    multiply(yawRot, 3, 3, transSpeed, 3, 1, transSpeed); // in world coords
    struct Vector3 movit = {transSpeed[0], transSpeed[1], transSpeed[2]};
    move(movit, camera);
    cameraCollisionBox.position.x = camera[3];
    cameraCollisionBox.position.y = camera[7];
    cameraCollisionBox.position.z = camera[11];
    
    collisionDetectionCamera(cubeCollisionBox);
    char output_string2[256];
    snprintf(output_string2, sizeof(output_string2), "%4.2f,%4.2f,%4.2f\n", cameraSpeed.x, cameraSpeed.y, cameraSpeed.z);
    print_on_screen(output_string2);
    /*
    char output_string[256];
    snprintf(output_string, sizeof(output_string), "%4.2f,%4.2f,%4.2f\n", cameraCollisionBox.position.x, cameraCollisionBox.position.y, cameraCollisionBox.position.z);
    print_on_screen(output_string);
    char output_string2[256];
    snprintf(output_string2, sizeof(output_string2), "%4.2f,%4.2f,%4.2f\n", camera[3], camera[7], camera[11]);
    print_on_screen(output_string2);
    */
}

void applyGravity(struct Speed *speed, float *pos, float ms) { // gravity as velocity instead of acceleration
    float gravity = 9.81f * 0.0005f;
    float gravitySpeed = gravity * ms;
    if (pos[1] > 0.0f){
        speed->y -= gravitySpeed;
        // if (pos[1] < 0.0f) {pos[1] = 0.0f;}; // this doesn't work
    }
    else { // some sort of hit the ground / collision detection
        speed->y = 0.0f;
        camera[7] = 0.0f;
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

/*int find_material_index(const int hash) {
    int empty_index = -1;
    for (int i = 0; i < MAX_PIPELINES; i++) {
        if (game_data.pipelines[i].used) {
            if (game_data.pipelines[i].hash == hash) {
                return i; // already exists
            }
        } else if (empty_index == -1) {
            empty_index = i; // keep track of earliest empty slot
        }
    }
    return empty_index; // return empty slot (or -1 if array is full)
}*/
