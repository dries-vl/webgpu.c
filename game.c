#include <math.h>

/* GAME STRUCTS */
struct Vector3 {
    float x, y, z;
};
struct Speed {
    float x;
    float y;
    float z;
    float yaw;
    float pitch;
    float roll;
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

struct GameObject {
    struct Rigid_Body collisionBox;
    struct Instance *instance;
    struct Speed velocity;
};

struct GameState {
    struct GameObject player;
    struct GameObject objects[256];
    int object_count;
};
struct GameState gameState = {
    .player = {
        .collisionBox = {
            .position = {0.0f, 0.0f, 0.0f},
            .vertices = {0},
            .normals = {0},
            .vertex_count = 0,
            .normal_count = 0,
            .radius = 1.0f
        },
        .instance = &(struct Instance) {
            .transform = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            },
            .data = {0},
            .norms = {0},
            .animation = 0,
            .animation_phase = 0.0f,
            .atlas_uv = {0}
        },
        .velocity = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}
    },
    .objects = {0}
};
/* GAME STRUCTS */

/* GLOBAL STATE OF THE GAME */
float view[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
};
float cameraPos[16] = { // camera pos relative to player
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.8f, -2.5f, 1.0f
};
float cameraRotation[2] = {0.0f, 0.0f}; // yaw, pitch
struct ButtonState buttonState = {0, 0, 0, 0};

/* GLOBAL STATE OF THE GAME */

/* CONSTS */
float fov = 3.14f / 4.0f; // 45 degrees
float farClip = 2000.0f;
float nearClip = 0.01f;
static float brightness = 1.0f;
static float timeVal = 0.0f;
/* CONSTS */

static inline void mat4_multiply(const float *a, int row1, int col1,
                                 const float *b, int col2, float *d) {
    // a is m x n, b is n x p, result d is m x p, all in column-major order.
    float temp[row1 * col2]; // temporary storage for the result

    // Iterate over each column (j) of the result matrix
    for (int j = 0; j < col2; j++) {
        // Iterate over each row (i) of the result matrix
        for (int i = 0; i < row1; i++) {
            float sum = 0.0f;
            // Accumulate the dot product for element (i, j)
            for (int k = 0; k < col1; k++) {
                // a[i + k*m] is element (i,k) of A (column-major)
                // b[k + j*n] is element (k,j) of B (column-major)
                sum += a[i + k * row1] * b[k + j * col1];
            }
            // Store the computed value in the temporary result
            temp[i + j * row1] = sum;
        }
    }
    // Copy the temporary result into the output array d
    memcpy(d, temp, sizeof(float) * row1 * col2);
}
static inline void mat4_identity(float m[16]) {
    for (int i = 0; i < 16; i++) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void inverseViewMatrix(const float m[16], float inv[16]) {
    // Extract the 3x3 rotation matrix (stored in columns 0, 1, and 2)
    float rot[9] = {
        m[0], m[1], m[2],
        m[4], m[5], m[6],
        m[8], m[9], m[10]
    };

    // Transpose the rotation matrix (its inverse if orthonormal)
    float rotT[9] = {
        rot[0], rot[3], rot[6],
        rot[1], rot[4], rot[7],
        rot[2], rot[5], rot[8]
    };

    // Extract translation (stored in indices 12,13,14)
    float translation[3] = {-m[12], -m[13], -m[14]};
    float newTranslation[3];
    // Multiply the transposed rotation (3x3) by the translation (3x1)
    mat4_multiply(rotT, 3, 3, translation, 1, newTranslation);

    // Build the inverse view matrix in column-major order:
    inv[0]  = rotT[0]; inv[1]  = rotT[1]; inv[2]  = rotT[2];  inv[3]  = 0.0f;
    inv[4]  = rotT[3]; inv[5]  = rotT[4]; inv[6]  = rotT[5];  inv[7]  = 0.0f;
    inv[8]  = rotT[6]; inv[9]  = rotT[7]; inv[10] = rotT[8];  inv[11] = 0.0f;
    inv[12] = newTranslation[0];
    inv[13] = newTranslation[1];
    inv[14] = newTranslation[2];
    inv[15] = 1.0f;
}

void move(struct Vector3 move, float *matrix) {
    matrix[12] += move.x;
    matrix[13] += move.y;
    matrix[14] += move.z;
}

void yaw(float angle, float *matrix) {
    // Create the yaw rotation matrix.
    float yawRot[16] = {
         cos(angle), 0.0f, -sin(angle), 0.0f,
         0.0f,       1.0f,  0.0f,       0.0f,
         sin(angle), 0.0f,  cos(angle), 0.0f,
         0.0f,       0.0f,  0.0f,       1.0f
    };

    // Create a temporary matrix to hold the new rotation.
    float newRot[16] = {0};

    // mat4_multiply the yaw rotation by the current rotation part.
    // Since the rotation is stored in the upper 3x3 block, we mat4_multiply the full matrices
    // but only update the corresponding 3x3 part.
    mat4_multiply(yawRot, 4, 4, matrix, 4, newRot);

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
         1.0f, 0.0f,       0.0f,      0.0f,
         0.0f, cos(angle),  sin(angle), 0.0f,
         0.0f, -sin(angle), cos(angle), 0.0f,
         0.0f, 0.0f,       0.0f,      1.0f
    };

    // Create a temporary matrix to hold the new rotation.
    float newRot[16] = {0};

    // mat4_multiply the pitch rotation by the current rotation part.
    // Since the rotation is stored in the upper 3x3 block, we mat4_multiply the full matrices
    // but only update the corresponding 3x3 part.
    mat4_multiply(rotMatrix, 4, 4, matrix, 4, newRot);

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
        cos(angle + cameraRotation[0]), 0.0f, -sin(angle + cameraRotation[0]), 0.0f,
        0.0f,                         1.0f,  0.0f,                         0.0f,
        sin(angle + cameraRotation[0]), 0.0f,  cos(angle + cameraRotation[0]), 0.0f,
        0.0f,                         0.0f,  0.0f,                         1.0f
    };
    float pitchRot[16] = {
         1.0f, 0.0f,                          0.0f, 0.0f,
         0.0f, cos(cameraRotation[1]),         sin(cameraRotation[1]), 0.0f,
         0.0f, -sin(cameraRotation[1]),        cos(cameraRotation[1]), 0.0f,
         0.0f, 0.0f,                          0.0f, 1.0f
    };
    float rotMatrix[16] = {0};
    mat4_multiply(yawRot, 4, 4, pitchRot, 4, rotMatrix);
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
         1.0f, 0.0f,                           0.0f, 0.0f,
         0.0f, cos(angle + cameraRotation[1]),   sin(angle + cameraRotation[1]), 0.0f,
         0.0f, -sin(angle + cameraRotation[1]),  cos(angle + cameraRotation[1]), 0.0f,
         0.0f, 0.0f,                           0.0f, 1.0f
    };
    float yawRot[16] = {
        cos(cameraRotation[0]), 0.0f, -sin(cameraRotation[0]), 0.0f,
        0.0f,                   1.0f,  0.0f,                   0.0f,
        sin(cameraRotation[0]), 0.0f,  cos(cameraRotation[0]), 0.0f,
        0.0f,                   0.0f,  0.0f,                   1.0f
    };
    float rotMatrix[16] = {0};
    mat4_multiply(yawRot, 4, 4, pitchRot, 4, rotMatrix);
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

struct Speed cameraSpeed = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}; // only y-speed used
float movementSpeed = 0.005f;

// add cube collision box
struct Rigid_Body cubeCollisionBox = {
    .vertices = (struct Vector3[]) {
        {1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, -1.0f},
        {1.0f, -1.0f, 1.0f},
        {1.0f, -1.0f, -1.0f},
        {-1.0f, 1.0f, 1.0f},
        {-1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f, 1.0f},
        {-1.0f, -1.0f, -1.0f}
    },
    .normals = (struct Vector3[]) {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    },
    .normal_count = 3,
    .vertex_count = 8,
    .position = {0.0f, 0.0f, 2.0f},
    .radius = 3.0f
};

// add pine collision box
struct Rigid_Body pineCollisionBox = {
    .vertices = (struct Vector3[]) {
        {1.0f, 8.5f, 1.0f},
        {1.0f, 8.5f, -1.0f},
        {1.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, -1.0f},
        {-1.0f, 8.5f, 1.0f},
        {-1.0f, 8.5f, -1.0f},
        {-1.0f, 0.0f, 1.0f},
        {-1.0f, 0.0f, -1.0f}
    },
    .normals = (struct Vector3[]) {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    },
    .normal_count = 3,
    .vertex_count = 8,
    .position = {0.0f, 0.0f, 1000.0f},
    .radius = 3.0f // not used
};

struct Rigid_Body playerCollisionBox = {
    .vertices = (struct Vector3[]) {
        {0.4f, 0.6f, 0.4f},
        {0.4f, 0.6f, -0.4f},
        {0.4f, 0.0f, 0.4f},
        {0.4f, 0.0f, -0.4f},
        {-0.4f, 0.6f, 0.4f},
        {-0.4f, 0.6f, -0.4f},
        {-0.4f, 0.0f, 0.4f},
        {-0.4f, 0.0f, -0.4f}
    },
    .normals = (struct Vector3[]) {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    },
    .normal_count = 3,
    .vertex_count = 8,
    .position = {0.0f, 0.0f, -3.0f},
    .radius = 3.0f
};

#pragma region Fysics
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
    collisionNormal.x = (body1.position.x - body2.position.x) > collisionNormal.x ? -collisionNormal.x : collisionNormal.x;
    collisionNormal.y = (body1.position.y - body2.position.y) > collisionNormal.y ? -collisionNormal.y : collisionNormal.y;
    collisionNormal.z = (body1.position.z - body2.position.z) > collisionNormal.z ? -collisionNormal.z : collisionNormal.z;
    // printf("collision: %4.2f, %4.2f, %4.2f\n", collisionNormal.x, collisionNormal.y, collisionNormal.z);
    return collisionNormal;

}; // SAT

void collisionDetectionCamera(struct Rigid_Body cubeCollisionBox) { // nu enkel met onze collision box
    struct Vector3 separation = detectCollision(playerCollisionBox, cubeCollisionBox);
    if (separation.x != 0.0f || separation.y != 0.0f || separation.z != 0.0f) {
        view[12] += separation.x; // undo x
        view[13] += separation.y;
        view[14] += separation.z;        
        playerCollisionBox.position.x = view[12];
        playerCollisionBox.position.y = view[13];
        playerCollisionBox.position.z = view[14];        
        //float proj = dot(normalise(separation), (struct Vector3){cameraSpeed.x, cameraSpeed.y, cameraSpeed.z});
        cameraSpeed.x = 0; // proj * normalise(separation).x;
        cameraSpeed.y = 0; // proj * normalise(separation).y;
        cameraSpeed.z = 0; // proj * normalise(separation).z;
    }
    char output_string[256];
    snprintf(output_string, sizeof(output_string), "%4.2f,%4.2f,%4.2f\n", separation.x, separation.y, separation.z);
    print_on_screen(output_string);
    
    char output_string2[256];
    snprintf(output_string2, sizeof(output_string2), "%4.2f,%4.2f,%4.2f\n", view[12], view[13], view[14]);
    print_on_screen(output_string2);
}

void collision(struct GameObject *mover, struct GameObject *stator) {
    struct Vector3 separation = detectCollision(mover->collisionBox, stator->collisionBox);
    if (separation.x != 0.0f || separation.y != 0.0f || separation.z != 0.0f) { // if collision
        mover->instance->transform[12] += separation.x; // undo movement
        mover->instance->transform[13] += separation.y;
        mover->instance->transform[14] += separation.z;
        mover->collisionBox.position.x = mover->instance->transform[12];
        mover->collisionBox.position.y = mover->instance->transform[13];
        mover->collisionBox.position.z = mover->instance->transform[14];
        //float proj = dot(normalise(separation), (struct Vector3){cameraSpeed.x, cameraSpeed.y, cameraSpeed.z});
        mover->velocity.x = 0; // proj * normalise(separation).x;
        mover->velocity.y = 0; // proj * normalise(separation).y;
        mover->velocity.z = 0; // proj * normalise(separation).z;
    }
}

void cameraMovement(float *view, float speed, float ms) { // unused
    cameraSpeed.x = speed * (buttonState.right - buttonState.left);
    cameraSpeed.z = speed * (buttonState.forward - buttonState.backward);

    float yawRot[9] = {
        cos(cameraRotation[0]), 0.0f, -sin(cameraRotation[0]),
        0.0f,                   1.0f,  0.0f,
        sin(cameraRotation[0]), 0.0f,  cos(cameraRotation[0])
    };    
    float xSpeed = cameraSpeed.x * ms;
    float ySpeed = cameraSpeed.y * ms;
    float zSpeed = cameraSpeed.z * ms;
    float transSpeed[3] = {xSpeed, ySpeed, zSpeed};
    mat4_multiply(yawRot, 3, 3, transSpeed, 1, transSpeed); // in world coords
    struct Vector3 movit = {transSpeed[0], transSpeed[1], transSpeed[2]};
    move(movit, view);
    playerCollisionBox.position.x = view[12];
    playerCollisionBox.position.y = view[13];
    playerCollisionBox.position.z = view[14];
    
    collisionDetectionCamera(cubeCollisionBox);
    char output_string2[256];
    snprintf(output_string2, sizeof(output_string2), "%4.2f,%4.2f,%4.2f\n", cameraSpeed.x, cameraSpeed.y, cameraSpeed.z);
    print_on_screen(output_string2);
    /*
    char output_string[256];
    snprintf(output_string, sizeof(output_string), "%4.2f,%4.2f,%4.2f\n", playerCollisionBox.position.x, playerCollisionBox.position.y, playerCollisionBox.position.z);
    print_on_screen(output_string);
    char output_string2[256];
    snprintf(output_string2, sizeof(output_string2), "%4.2f,%4.2f,%4.2f\n", view[12], view[13], view[14]);
    print_on_screen(output_string2);
    */
}

void playerMovement(float speed, float ms, struct GameObject *player) {
    player->velocity.x = speed * (buttonState.right - buttonState.left);
    player->velocity.z = speed * (buttonState.forward - buttonState.backward);
    
    float yawRot[9] = {
        cos(cameraRotation[0]), 0.0f, -sin(cameraRotation[0]),
        0.0f,                   1.0f,  0.0f,
        sin(cameraRotation[0]), 0.0f,  cos(cameraRotation[0])
    };    
    float xSpeed = player->velocity.x * ms;
    float ySpeed = player->velocity.y * ms;
    float zSpeed = player->velocity.z * ms;
    float transSpeed[3] = {xSpeed, ySpeed, zSpeed};
    mat4_multiply(yawRot, 3, 3, transSpeed, 1, transSpeed); // in world coords
    struct Vector3 movit = {transSpeed[0], transSpeed[1], transSpeed[2]};
    move(movit, player->instance->transform);
    player->collisionBox.position.x = player->instance->transform[12];
    player->collisionBox.position.y = player->instance->transform[13];
    player->collisionBox.position.z = player->instance->transform[14];

    // set player rotation
    if (fabs(movit.x) > 0.00001 || fabs(movit.z) > 0.00001) { // if move update rotation
        // if (movit.z == 0.0f) {movit.z = 0.0001f;} // avoid division by zero JANK not needed for atan2???
        float charRot = atan2(movit.x, movit.z);
        player->instance->transform[0] = cos(charRot);
        player->instance->transform[1] = 0.0f;
        player->instance->transform[2] = -sin(charRot);
        player->instance->transform[4] = 0.0f;
        player->instance->transform[5] = 1.0f;
        player->instance->transform[6] = 0.0f;
        player->instance->transform[8] = sin(charRot);
        player->instance->transform[9] = 0.0f;
        player->instance->transform[10] = cos(charRot);
    }
    
    for (int i = 0; i < gameState.object_count; i++) {
        collision(player, &gameState.objects[i]);
    }
    char output_string2[256];
    snprintf(output_string2, sizeof(output_string2), "%4.2f,%4.2f,%4.2f\n", player->instance->transform[12], player->instance->transform[13], player->instance->transform[14]);
    print_on_screen(output_string2);
}

void applyGravity(struct Speed *speed, float *pos, float ms) { 
    float gravity = 9.81f * 0.0000025f;
    float gravitySpeed = gravity * ms;
    #define GROUND_LEVEL 0.0f // todo: this is arbitrary based on 'eye-height' set in camera
    if (pos[1] > GROUND_LEVEL){
        speed->y -= gravitySpeed;
        // if (pos[1] < 0.0f) {pos[1] = 0.0f;}; // this doesn't work
    }
    else { // some sort of hit the ground / collision detection
        speed->y = 0.0f;
        gameState.player.instance->transform[13] = GROUND_LEVEL;
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

#pragma endregion

#pragma region GameState
// ------------------------------------------------------GAME State-----------------------------------------------------------------

void initGamestate(struct GameState *gameState) {
    // player/camera
    gameState->player.collisionBox = playerCollisionBox;
    //memcpy(gameState->player.instance->transform, view, sizeof(view));
    gameState->player.velocity = cameraSpeed;
    // objects
    //gameState->objects[0].collisionBox = cubeCollisionBox;
};

void addGameObject(struct GameState *gameState, struct GameObject *gameObject) {
    gameState->objects[gameState->object_count] = *gameObject;
    gameState->object_count++;
}
#pragma endregion

// Helper: Multiply two 4x4 matrices (column-major)
void multiplyMatrices(float out[16], const float a[16], const float b[16]) {
    float temp[16];
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            temp[row + col * 4] = 0.0f;
            for (int i = 0; i < 4; i++) {
                temp[row + col * 4] += a[row + i * 4] * b[i + col * 4];
            }
        }
    }
    memcpy(out, temp, sizeof(temp));
}

// Helper: Build an orthographic projection matrix (column-major)
void orthoMatrix(float m[16],
                 float left, float right,
                 float bottom, float top,
                 float near_plane, float far_plane) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = 2.0f / (right - left);
    m[5] = 2.0f / (top - bottom);
    m[10] = -2.0f / (far_plane - near_plane);
    m[12] = -(right + left) / (right - left);
    m[13] = -(top + bottom) / (top - bottom);
    m[14] = -(far_plane + near_plane) / (far_plane - near_plane);
    m[15] = 1.0f;
}

// Helper: Build a look-at view matrix (column-major)
// Assumes eye, target, and up are 3-element arrays.
void lookAtMatrix(float m[16], const float eye[3], const float target[3], const float up[3]) {
    float f[3] = { target[0]-eye[0], target[1]-eye[1], target[2]-eye[2] };
    // Normalize f.
    float f_len = sqrt(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
    f[0] /= f_len; f[1] /= f_len; f[2] /= f_len;

    // Normalize up.
    float up_len = sqrt(up[0]*up[0] + up[1]*up[1] + up[2]*up[2]);
    float upN[3] = { up[0]/up_len, up[1]/up_len, up[2]/up_len };

    // s = f x up
    float s[3] = { f[1]*upN[2] - f[2]*upN[1],
                   f[2]*upN[0] - f[0]*upN[2],
                   f[0]*upN[1] - f[1]*upN[0] };
    // Normalize s.
    float s_len = sqrt(s[0]*s[0] + s[1]*s[1] + s[2]*s[2]);
    s[0] /= s_len; s[1] /= s_len; s[2] /= s_len;

    // u = s x f
    float u[3] = { s[1]*f[2] - s[2]*f[1],
                   s[2]*f[0] - s[0]*f[2],
                   s[0]*f[1] - s[1]*f[0] };

    // Column-major order
    m[0] = s[0];
    m[1] = u[0];
    m[2] = -f[0];
    m[3] = 0.0f;

    m[4] = s[1];
    m[5] = u[1];
    m[6] = -f[1];
    m[7] = 0.0f;

    m[8]  = s[2];
    m[9]  = u[2];
    m[10] = -f[2];
    m[11] = 0.0f;

    m[12] = - (s[0]*eye[0] + s[1]*eye[1] + s[2]*eye[2]);
    m[13] = - (u[0]*eye[0] + u[1]*eye[1] + u[2]*eye[2]);
    m[14] =   (f[0]*eye[0] + f[1]*eye[1] + f[2]*eye[2]);
    m[15] = 1.0f;
}
// Normalizes a 3-element vector in-place.
void normalize(float v[3]) {
    float len = sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (len > 0.0f) {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
}

// Compute a dynamic light view-projection matrix that covers a region
// around the camera (or player). 'playerLocation' is a 3-element array.
void computeDynamicLightViewProj(float light_view_proj[16], const float playerLocation[3]) {
    // 1. Define the region to cover.
    float left   = -20.0f;
    float right  =  20.0f;
    float bottom = -20.0f;
    float top    =  20.0f;
    float near_plane = -200.0f;
    float far_plane  = 200.0f;
    float ortho[16];
    orthoMatrix(ortho, left, right, bottom, top, near_plane, far_plane);

    // 2. Define a directional light.
    float lightDir[3] = { 0.5f, -0.8f, 0.5f };
    normalize(lightDir);  // ensure it's normalized

    // 3. Choose a center point—for example, the camera position or scene center.
    float center[3] = { 0.0f, 0.0f, 0.0f };  

    // 4. Position the light relative to the center.
    float distance = 50.0f;
    float lightPos[3] = { 
        center[0] - lightDir[0] * distance,
        center[1] - lightDir[1] * distance,
        center[2] - lightDir[2] * distance 
    };

    // 5. Build the light view matrix using a lookAt function.
    float lightView[16];
    lookAtMatrix(lightView, lightPos, center, /*up vector*/(float[]){ 0.0f, 1.0f, 0.0f });

    // 6. Compute the final light view–projection matrix.
    multiplyMatrices(light_view_proj, ortho, lightView);
}

// Generates six view matrices (each 16 floats, column-major)
// for a cubemap centered at 'pos'. The order is:
// +X, -X, +Y, -Y, +Z, -Z.
void generateCubemapViews(const float pos[3], float views[6][16]) {
    // Targets: pos + direction.
    // todo: these target vectors are inverted, +X looks at -1 instead of +1
    float targets[6][3] = {
        { pos[0]-10, pos[1],   pos[2]   }, // +X
        { pos[0]+10, pos[1],   pos[2]   }, // -X
        { pos[0],   pos[1]-10, pos[2]   }, // +Y
        { pos[0],   pos[1]+10, pos[2]   }, // -Y
        { pos[0],   pos[1],   pos[2]-10 }, // +Z
        { pos[0],   pos[1],   pos[2]+10 }  // -Z
    };
    // Up vectors chosen for proper orientation.
    float ups[6][3] = {
        { 0, 1, 0 }, // +X
        { 0, 1, 0 }, // -X
        { 0, 0, -1 }, // +Y
        { 0, 0, -1 }, // -Y
        { 0, 1, 0 }, // +Z
        { 0, 1, 0 }  // -Z
    };
    for (int i = 0; i < 6; i++)
        lookAtMatrix(views[i], pos, targets[i], ups[i]);
}

// Generates a cubemap projection matrix with a 90° FOV (in radians: pi/2),
// aspect ratio 1, and given near and far clip planes.
void generateCubemapProjection(float near, float far, float proj[16]) {
    float f = 1.0f / tan(3.14159265f / 4.0f); // 90° FOV -> half-angle = pi/4.
    proj[0]  = f;   proj[1]  = 0;   proj[2]  = 0;                 proj[3]  = 0;
    proj[4]  = 0;   proj[5]  = f;   proj[6]  = 0;                 proj[7]  = 0;
    proj[8]  = 0;   proj[9]  = 0;   proj[10] = far/(far-near);      proj[11] = 1;
    proj[12] = 0;   proj[13] = 0;   proj[14] = -near*far/(far-near); proj[15] = 0;
}
