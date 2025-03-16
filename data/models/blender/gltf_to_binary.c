/*
    convert_glb_to_bin.c
    Compile with TCC (e.g. tcc -o convert_glb_to_bin.exe convert_glb_to_bin.c)
    This script processes all .glb files in the current folder and writes a .bin file
    for each into a folder called "bin". It uses only standard C, cgltf.h, and minimal
    directory routines, and fixes the bone transforms so the vertex shader can do
    FinalVertex = BoneMatrix * Vertex without extra inverse-bind multiplication.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <limits.h>  // For UCHAR_MAX

#ifdef _WIN32
  #include <io.h>
  #include <direct.h>
#else
  #include <dirent.h>
  #include <sys/stat.h>
#endif

// Uncomment to enable debug prints of bones
//#define DEBUG_BONES

// Include cgltf (make sure cgltf.h is in the same folder)
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

// Binary file header.
#pragma pack(push, 1)
typedef struct {
    unsigned int vertexCount;
    unsigned int indexCount;
    unsigned int boneCount;    // Always MAX_BONES if skin exists
    unsigned int frameCount;
    unsigned int vertexArrayOffset;
    unsigned int indexArrayOffset;
    unsigned int boneFramesArrayOffset;
} AnimatedMeshHeader;
#pragma pack(pop)

// Vertex structure.
typedef struct {
    unsigned int data[4];         // 16 bytes (unused placeholder)
    float position[3];            // 12 bytes
    unsigned char normal[4];      // 4 bytes (normalized)
    unsigned char tangent[4];     // 4 bytes (normalized)
    unsigned short uv[2];         // 4 bytes (16-bit texcoords)
    unsigned char bone_weights[4]; // 4 bytes (0–255 weights)
    unsigned char bone_indices[4]; // 4 bytes (bone indices)
} Vertex;

#define MAX_BONES 64

// Convert a float in [0,1] to an 8-bit unsigned normalized value.
static unsigned char float_to_unorm8(float v) {
    int n = (int)(v * 255.0f + 0.5f);
    if (n < 0)   n = 0;
    if (n > 255) n = 255;
    return (unsigned char)n;
}

// Multiply two 4x4 matrices (a and b) assumed to be in column-major order.
// out = a * b
static void multiply_matrix4x4(const float a[16], const float b[16], float out[16]) {
    // In column-major order, the element at row i, column j is stored at index j*4 + i.
    for (int i = 0; i < 4; i++) {       // row
        for (int j = 0; j < 4; j++) {   // column
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                // a(i,k) = a[k*4 + i], b(k,j) = b[j*4 + k]
                sum += a[k*4 + i] * b[j*4 + k];
            }
            out[j*4 + i] = sum;
        }
    }
}

// Invert a 4x4 matrix. Returns 1 on success, 0 on failure.
static int invert_matrix4x4(const float m[16], float invOut[16]) {
    float inv[16], det;
    int i;
    inv[0] =   m[5]*m[10]*m[15]
             - m[5]*m[11]*m[14]
             - m[9]*m[6]*m[15]
             + m[9]*m[7]*m[14]
             + m[13]*m[6]*m[11]
             - m[13]*m[7]*m[10];

    inv[4] =  -m[4]*m[10]*m[15]
             + m[4]*m[11]*m[14]
             + m[8]*m[6]*m[15]
             - m[8]*m[7]*m[14]
             - m[12]*m[6]*m[11]
             + m[12]*m[7]*m[10];

    inv[8] =   m[4]*m[9]*m[15]
             - m[4]*m[11]*m[13]
             - m[8]*m[5]*m[15]
             + m[8]*m[7]*m[13]
             + m[12]*m[5]*m[11]
             - m[12]*m[7]*m[9];

    inv[12] = -m[4]*m[9]*m[14]
             + m[4]*m[10]*m[13]
             + m[8]*m[5]*m[14]
             - m[8]*m[6]*m[13]
             - m[12]*m[5]*m[10]
             + m[12]*m[6]*m[9];

    inv[1] =  -m[1]*m[10]*m[15]
             + m[1]*m[11]*m[14]
             + m[9]*m[2]*m[15]
             - m[9]*m[3]*m[14]
             - m[13]*m[2]*m[11]
             + m[13]*m[3]*m[10];

    inv[5] =   m[0]*m[10]*m[15]
             - m[0]*m[11]*m[14]
             - m[8]*m[2]*m[15]
             + m[8]*m[3]*m[14]
             + m[12]*m[2]*m[11]
             - m[12]*m[3]*m[10];

    inv[9] =  -m[0]*m[9]*m[15]
             + m[0]*m[11]*m[13]
             + m[8]*m[1]*m[15]
             - m[8]*m[3]*m[13]
             - m[12]*m[1]*m[11]
             + m[12]*m[3]*m[9];

    inv[13] =  m[0]*m[9]*m[14]
             - m[0]*m[10]*m[13]
             - m[8]*m[1]*m[14]
             + m[8]*m[2]*m[13]
             + m[12]*m[1]*m[10]
             - m[12]*m[2]*m[9];

    inv[2] =   m[1]*m[6]*m[15]
             - m[1]*m[7]*m[14]
             - m[5]*m[2]*m[15]
             + m[5]*m[3]*m[14]
             + m[13]*m[2]*m[7]
             - m[13]*m[3]*m[6];

    inv[6] =  -m[0]*m[6]*m[15]
             + m[0]*m[7]*m[14]
             + m[4]*m[2]*m[15]
             - m[4]*m[3]*m[14]
             - m[12]*m[2]*m[7]
             + m[12]*m[3]*m[6];

    inv[10] =  m[0]*m[5]*m[15]
             - m[0]*m[7]*m[13]
             - m[4]*m[1]*m[15]
             + m[4]*m[3]*m[13]
             + m[12]*m[1]*m[7]
             - m[12]*m[3]*m[5];

    inv[14] = -m[0]*m[5]*m[14]
             + m[0]*m[6]*m[13]
             + m[4]*m[1]*m[14]
             - m[4]*m[2]*m[13]
             - m[12]*m[1]*m[6]
             + m[12]*m[2]*m[5];

    inv[3] =  -m[1]*m[6]*m[11]
             + m[1]*m[7]*m[10]
             + m[5]*m[2]*m[11]
             - m[5]*m[3]*m[10]
             - m[9]*m[2]*m[7]
             + m[9]*m[3]*m[6];

    inv[7] =   m[0]*m[6]*m[11]
             - m[0]*m[7]*m[10]
             - m[4]*m[2]*m[11]
             + m[4]*m[3]*m[10]
             + m[8]*m[2]*m[7]
             - m[8]*m[3]*m[6];

    inv[11] = -m[0]*m[5]*m[11]
             + m[0]*m[7]*m[9]
             + m[4]*m[1]*m[11]
             - m[4]*m[3]*m[9]
             - m[8]*m[1]*m[7]
             + m[8]*m[3]*m[5];

    inv[15] =  m[0]*m[5]*m[10]
             - m[0]*m[6]*m[9]
             - m[4]*m[1]*m[10]
             + m[4]*m[2]*m[9]
             + m[8]*m[1]*m[6]
             - m[8]*m[2]*m[5];

    det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (fabs(det) < 1e-12) {
        return 0; // Not invertible
    }
    det = 1.0f / det;
    for (i = 0; i < 16; i++)
        invOut[i] = inv[i] * det;
    return 1;
}

// Spherical linear interpolation (slerp) for quaternions.
static void slerp(const float q0[4], const float q1[4], float t, float out[4]) {
    float dot = q0[0]*q1[0] + q0[1]*q1[1] + q0[2]*q1[2] + q0[3]*q1[3];
    float q1_copy[4];
    if (dot < 0.0f) {
        dot = -dot;
        for (int i = 0; i < 4; i++)
            q1_copy[i] = -q1[i];
    } else {
        memcpy(q1_copy, q1, sizeof(q1_copy));
    }
    const float DOT_THRESHOLD = 0.9995f;
    if (dot > DOT_THRESHOLD) {
        // Linear interpolate, then normalize
        for (int i = 0; i < 4; i++)
            out[i] = q0[i] + t * (q1_copy[i] - q0[i]);
        float len = 0.0f;
        for (int i = 0; i < 4; i++)
            len += out[i]*out[i];
        len = sqrt(len);
        if (len > 1e-10f)
            for (int i = 0; i < 4; i++)
                out[i] /= len;
        return;
    }
    float theta_0 = acos(dot);
    float theta = theta_0 * t;
    float sin_theta = sin(theta);
    float sin_theta_0 = sin(theta_0);
    float s0 = cos(theta) - dot * sin_theta / sin_theta_0;
    float s1 = sin_theta / sin_theta_0;
    for (int i = 0; i < 4; i++) {
        out[i] = s0 * q0[i] + s1 * q1_copy[i];
    }
}

// Compute a node's local transform in column-major order.
static void compute_node_local_transform(cgltf_node *node, cgltf_animation *anim, float t, float out[16]) {
    // If the node defines a full matrix, use it directly.
    if (node->has_matrix) {
        memcpy(out, node->matrix, 16 * sizeof(float));
        return;
    }

    // Build TRS from the node’s data.
    float translation[3] = {
        node->has_translation ? node->translation[0] : 0.0f,
        node->has_translation ? node->translation[1] : 0.0f,
        node->has_translation ? node->translation[2] : 0.0f
    };
    float rotation[4] = {
        node->has_rotation ? node->rotation[0] : 0.0f,
        node->has_rotation ? node->rotation[1] : 0.0f,
        node->has_rotation ? node->rotation[2] : 0.0f,
        node->has_rotation ? node->rotation[3] : 1.0f
    };
    float scale[3] = {
        node->has_scale ? node->scale[0] : 1.0f,
        node->has_scale ? node->scale[1] : 1.0f,
        node->has_scale ? node->scale[2] : 1.0f
    };

    // Apply animation channels if available.
    if (anim) {
        for (size_t c = 0; c < anim->channels_count; c++) {
            cgltf_animation_channel* channel = &anim->channels[c];
            if (channel->target_node != node)
                continue;
            cgltf_accessor* input = channel->sampler->input;
            cgltf_accessor* output = channel->sampler->output;
            size_t key_count = input->count;
            if (key_count == 0)
                continue;
            size_t key0 = 0, key1 = 0;
            float t0 = 0.f, t1 = 0.f;
            for (size_t k = 0; k < key_count; k++) {
                float timeVal;
                cgltf_accessor_read_float(input, k, &timeVal, 1);
                if (timeVal <= t) { key0 = k; t0 = timeVal; }
                if (timeVal >= t) { key1 = k; cgltf_accessor_read_float(input, k, &t1, 1); break; }
            }
            float factor = (t1 - t0) > 1e-10f ? (t - t0) / (t1 - t0) : 0.0f;
            if (channel->target_path == cgltf_animation_path_type_translation) {
                float val0[3], val1[3];
                cgltf_accessor_read_float(output, key0, val0, 3);
                cgltf_accessor_read_float(output, key1, val1, 3);
                for (int j = 0; j < 3; j++)
                    translation[j] = val0[j] * (1.0f - factor) + val1[j] * factor;
            } else if (channel->target_path == cgltf_animation_path_type_rotation) {
                float q0[4], q1[4];
                cgltf_accessor_read_float(output, key0, q0, 4);
                cgltf_accessor_read_float(output, key1, q1, 4);
                slerp(q0, q1, factor, rotation);
            } else if (channel->target_path == cgltf_animation_path_type_scale) {
                float val0[3], val1[3];
                cgltf_accessor_read_float(output, key0, val0, 3);
                cgltf_accessor_read_float(output, key1, val1, 3);
                for (int j = 0; j < 3; j++)
                    scale[j] = val0[j] * (1.0f - factor) + val1[j] * factor;
            }
        }
    }

    // Build TRS matrices in column-major order.
    float T[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        translation[0], translation[1], translation[2], 1
    };

    float S[16] = {
        scale[0], 0,        0,       0,
        0,        scale[1], 0,       0,
        0,        0,        scale[2],0,
        0,        0,        0,       1
    };

    float x = rotation[0], y = rotation[1], z = rotation[2], w = rotation[3];
    float R[16] = {
         1 - 2*y*y - 2*z*z,    2*x*y + 2*z*w,       2*x*z - 2*y*w,       0,
         2*x*y - 2*z*w,        1 - 2*x*x - 2*z*z,   2*y*z + 2*x*w,       0,
         2*x*z + 2*y*w,        2*y*z - 2*x*w,       1 - 2*x*x - 2*y*y,   0,
         0,                    0,                   0,                   1
    };

    float RS[16];
    multiply_matrix4x4(R, S, RS);
    float TR[16];
    multiply_matrix4x4(T, RS, TR);
    memcpy(out, TR, 16 * sizeof(float));
}

// Recursively compute the global transform for a node.
static void compute_global_transform(cgltf_node *node, cgltf_animation *anim, float t, float out[16]) {
    float local[16];
    compute_node_local_transform(node, anim, t, local);

    if (node->parent) {
       float parent_global[16];
       compute_global_transform(node->parent, anim, t, parent_global);

       float global[16];
       multiply_matrix4x4(parent_global, local, global);
       memcpy(out, global, 16*sizeof(float));
    } else {
       memcpy(out, local, 16*sizeof(float));
    }
}

static void process_file(const char* filename) {
    printf("Processing file: %s\n", filename);

    cgltf_options options = {0};
    cgltf_data* data = NULL;
    cgltf_result result = cgltf_parse_file(&options, filename, &data);
    if (result != cgltf_result_success) {
        printf("  [Error] Failed to parse file: %s\n", filename);
        return;
    }
    result = cgltf_load_buffers(&options, data, filename);
    if (result != cgltf_result_success) {
        printf("  [Error] Failed to load buffers in %s\n", filename);
        cgltf_free(data);
        return;
    }
    if (data->meshes_count < 1) {
        printf("  [Warning] No meshes found in %s\n", filename);
        cgltf_free(data);
        return;
    }

    cgltf_mesh* mesh = &data->meshes[0];
    if (mesh->primitives_count < 1) {
        printf("  [Warning] No primitives in mesh of %s\n", filename);
        cgltf_free(data);
        return;
    }
    cgltf_primitive* prim = &mesh->primitives[0];

    // Find needed accessors.
    const cgltf_accessor* pos_accessor    = cgltf_find_accessor(prim, cgltf_attribute_type_position, 0);
    const cgltf_accessor* norm_accessor   = cgltf_find_accessor(prim, cgltf_attribute_type_normal, 0);
    const cgltf_accessor* tang_accessor   = cgltf_find_accessor(prim, cgltf_attribute_type_tangent, 0);
    const cgltf_accessor* tex_accessor    = cgltf_find_accessor(prim, cgltf_attribute_type_texcoord, 0);
    const cgltf_accessor* joints_accessor = cgltf_find_accessor(prim, cgltf_attribute_type_joints, 0);
    const cgltf_accessor* weights_accessor= cgltf_find_accessor(prim, cgltf_attribute_type_weights, 0);

    // Position is required.
    if (!pos_accessor) {
        printf("  [Error] Missing required POSITION attribute in %s\n", filename);
        cgltf_free(data);
        return;
    }
    if (!norm_accessor)
        printf("  [Warning] No NORMAL attribute found in %s; using default normals.\n", filename);
    if (!tex_accessor)
        printf("  [Warning] No TEXCOORD attribute found in %s; using default texcoords.\n", filename);
    if (!joints_accessor)
        printf("  [Warning] No JOINTS attribute found in %s; using default bone indices.\n", filename);
    if (!weights_accessor)
        printf("  [Warning] No WEIGHTS attribute found in %s; using default bone weights.\n", filename);

    // Load vertices.
    unsigned int vertexCount = (unsigned int) pos_accessor->count;
    Vertex* vertices = (Vertex*) calloc(vertexCount, sizeof(Vertex));
    if (!vertices) {
        printf("  [Error] Out of memory for vertices\n");
        cgltf_free(data);
        return;
    }

    for (unsigned int i = 0; i < vertexCount; i++) {
        // Position (required).
        float pos[3] = {0};
        cgltf_accessor_read_float(pos_accessor, i, pos, 3);
        memcpy(vertices[i].position, pos, sizeof(pos));

        // Normal.
        if (norm_accessor) {
            float norm[3] = {0};
            cgltf_accessor_read_float(norm_accessor, i, norm, 3);
            for (int j = 0; j < 3; j++)
                vertices[i].normal[j] = float_to_unorm8(norm[j]);
        } else {
            float default_norm[3] = {0.0f, 0.0f, 1.0f};
            for (int j = 0; j < 3; j++)
                vertices[i].normal[j] = float_to_unorm8(default_norm[j]);
        }
        vertices[i].normal[3] = 255;

        // Tangent.
        if (tang_accessor) {
            float tang[4];
            cgltf_accessor_read_float(tang_accessor, i, tang, 4);
            for (int j = 0; j < 4; j++)
                vertices[i].tangent[j] = float_to_unorm8(tang[j]);
        } else {
            vertices[i].tangent[0] = float_to_unorm8(1.0f);
            vertices[i].tangent[1] = float_to_unorm8(0.0f);
            vertices[i].tangent[2] = float_to_unorm8(0.0f);
            vertices[i].tangent[3] = float_to_unorm8(1.0f);
        }

        // Texcoord.
        if (tex_accessor) {
            float uv[2] = {0};
            cgltf_accessor_read_float(tex_accessor, i, uv, 2);
            unsigned int uu = (unsigned int)(uv[0] * 65535.0f + 0.5f);
            unsigned int vv = (unsigned int)(uv[1] * 65535.0f + 0.5f);
            if (uu > 65535) uu = 65535;
            if (vv > 65535) vv = 65535;
            vertices[i].uv[0] = (unsigned short)uu;
            vertices[i].uv[1] = (unsigned short)vv;
        } else {
            vertices[i].uv[0] = 0;
            vertices[i].uv[1] = 0;
        }

        // Joints.
        if (joints_accessor) {
            unsigned short joints[4] = {0};
            cgltf_accessor_read_uint(joints_accessor, i, (unsigned int*)joints, 4);
            for (int j = 0; j < 4; j++) {
                vertices[i].bone_indices[j] = (unsigned char)(joints[j]);
            }
        } else {
            for (int j = 0; j < 4; j++)
                vertices[i].bone_indices[j] = 0;
        }

        // Weights.
        float w4[4] = {0, 0, 0, 0};
        if (weights_accessor) {
            cgltf_accessor_read_float(weights_accessor, i, w4, 4);
        } else {
            w4[0] = 1.0f; w4[1] = w4[2] = w4[3] = 0.0f;
        }
        float sum = w4[0] + w4[1] + w4[2] + w4[3];
        if (sum < 1e-12f) {
            w4[0] = 1.0f; w4[1] = w4[2] = w4[3] = 0.0f;
            sum = 1.0f;
        }
        float invSum = 1.0f / sum;
        w4[0] *= invSum;
        w4[1] *= invSum;
        w4[2] *= invSum;
        w4[3] *= invSum;

        int iw[4];
        int total = 0;
        for (int j = 0; j < 4; j++) {
            iw[j] = (int)roundf(w4[j] * 255.0f);
            total += iw[j];
        }
        int diff = 255 - total;
        if (diff != 0) {
            int maxidx = 0;
            float maxval = w4[0];
            for (int j = 1; j < 4; j++) {
                if (w4[j] > maxval) { maxidx = j; maxval = w4[j]; }
            }
            iw[maxidx] += diff;
            if (iw[maxidx] < 0)   iw[maxidx] = 0;
            if (iw[maxidx] > 255) iw[maxidx] = 255;
        }
        for (int j = 0; j < 4; j++) {
            vertices[i].bone_weights[j] = (unsigned char)iw[j];
        }
    }

    // Indices.
    unsigned int indexCount = 0;
    unsigned int* indices = NULL;
    if (prim->indices) {
        indexCount = (unsigned int)prim->indices->count;
        indices = (unsigned int*)malloc(indexCount * sizeof(unsigned int));
        if (!indices) {
            printf("  [Error] Out of memory for indices\n");
            free(vertices);
            cgltf_free(data);
            return;
        }
        for (unsigned int i = 0; i < indexCount; i++) {
            indices[i] = (unsigned int)cgltf_accessor_read_index(prim->indices, i);
        }
        // Reverse triangle winding order from CW to CCW by swapping the second and third index in each triangle.
        if (indexCount % 3 == 0) {
            for (unsigned int i = 0; i < indexCount; i += 3) {
                unsigned int temp = indices[i+1];
                indices[i+1] = indices[i+2];
                indices[i+2] = temp;
            }
        }
    }

    // Skins / Bones.
    cgltf_skin* skin = NULL;
    unsigned int usedBoneCount = 0;
    if (data->skins_count > 0) {
        skin = &data->skins[0];
        usedBoneCount = (unsigned int)skin->joints_count;
    }

    // Prepare inverse bind arrays.
    float* corrected_invBind = NULL;
    if (skin && usedBoneCount > 0) {
        corrected_invBind = (float*)malloc(usedBoneCount * 16 * sizeof(float));
        if (!corrected_invBind) {
            printf("  [Error] Out of memory for corrected inverse bind matrices\n");
            free(indices);
            free(vertices);
            cgltf_free(data);
            return;
        }
        for (unsigned int b = 0; b < usedBoneCount; b++) {
            cgltf_node* bone_node = skin->joints[b];
            float G_bind[16];
            compute_global_transform(bone_node, NULL, 0.0f, G_bind);
            float invG[16];
            if (!invert_matrix4x4(G_bind, invG)) {
                for (int i = 0; i < 16; i++)
                    invG[i] = (i % 5 == 0) ? 1.0f : 0.0f;
            }
            memcpy(&corrected_invBind[b * 16], invG, 16 * sizeof(float));
#ifdef DEBUG_BONES
            printf("Bone %u inverseBind:\n", b);
            for (int i = 0; i < 4; i++) {
                printf("  [ %f %f %f %f ]\n",
                       invG[i*4+0], invG[i*4+1],
                       invG[i*4+2], invG[i*4+3]);
            }
#endif
        }
    }

    // Animations.
    cgltf_animation* anim = NULL;
    unsigned int frameCount = 1;
    float anim_start = 0.f, anim_end = 0.f;
    double fps = 30.0;
    if (skin && data->animations_count > 0) {
        anim = &data->animations[0];
        anim_start = 1e30f;
        anim_end   = -1e30f;
        for (size_t i = 0; i < anim->samplers_count; i++) {
            cgltf_accessor* input = anim->samplers[i].input;
            if (input->count > 0) {
                float tmin = input->min[0];
                float tmax = input->max[0];
                if (tmin < anim_start) anim_start = tmin;
                if (tmax > anim_end)   anim_end   = tmax;
            }
        }
        frameCount = (unsigned int)((anim_end - anim_start) * fps) + 1;
        if (frameCount < 1) frameCount = 1;
    }

    // For each frame, compute final bone transforms.
    float* boneFrames = NULL;
    if (skin && usedBoneCount > 0) {
        boneFrames = (float*)calloc(frameCount * MAX_BONES * 16, sizeof(float));
        if (!boneFrames) {
            printf("  [Error] Out of memory for bone frames\n");
            free(corrected_invBind);
            free(indices);
            free(vertices);
            cgltf_free(data);
            return;
        }
        float flipX[16] = {
            -1, 0,  0, 0,
             0, 1,  0, 0,
             0, 0,  1, 0,
             0, 0,  0, 1
        };

        for (unsigned int f = 0; f < frameCount; f++) {
            float t = anim ? (anim_start + (float)f / (float)fps) : 0.0f;
            for (unsigned int b = 0; b < MAX_BONES; b++) {
                float finalBone[16];
                if (b < usedBoneCount) {
                    cgltf_node* bone_node = skin->joints[b];
                    float G_current[16];
                    compute_global_transform(bone_node, anim, t, G_current);
float combined[16];
multiply_matrix4x4(G_current, &corrected_invBind[b * 16], combined);
memcpy(finalBone, combined, 16 * sizeof(float));
#ifdef DEBUG_BONES
                    if (f==0) {
                        printf("Bone %u final transform (frame=0):\n", b);
                        for (int rr = 0; rr < 4; rr++) {
                            printf("  [ %f %f %f %f ]\n",
                                   finalBone[rr*4+0], finalBone[rr*4+1],
                                   finalBone[rr*4+2], finalBone[rr*4+3]);
                        }
                    }
#endif
                } else {
                    for (int i = 0; i < 16; i++)
                        finalBone[i] = (i%5==0) ? 1.0f : 0.0f;
                }
                memcpy(&boneFrames[(f * MAX_BONES + b) * 16], finalBone, 16*sizeof(float));
            }
        }
    }

    // Build output filename.
    char out_filename[256];
    const char* dot = strrchr(filename, '.');
    if (dot) {
        size_t len = (size_t)(dot - filename);
        if (len > sizeof(out_filename)-5) len = sizeof(out_filename)-5;
        strncpy(out_filename, filename, len);
        out_filename[len] = '\0';
    } else {
        strncpy(out_filename, filename, sizeof(out_filename)-1);
        out_filename[sizeof(out_filename)-1] = '\0';
    }
    strncat(out_filename, ".bin", sizeof(out_filename) - strlen(out_filename) - 1);

    char bin_path[256];
    snprintf(bin_path, sizeof(bin_path), "bin/%s", out_filename);

    FILE* out = fopen(bin_path, "wb");
    if (!out) {
        printf("  [Error] Failed to open output file: %s\n", bin_path);
        free(corrected_invBind);
        free(boneFrames);
        free(indices);
        free(vertices);
        cgltf_free(data);
        return;
    }

    // Write header.
    AnimatedMeshHeader header;
    memset(&header, 0, sizeof(header));
    header.vertexCount            = vertexCount;
    header.indexCount             = indexCount;
    header.boneCount              = (boneFrames ? MAX_BONES : 0);
    header.frameCount             = frameCount;

    unsigned int headerSize       = (unsigned int)sizeof(AnimatedMeshHeader);
    unsigned int vertexArraySize  = vertexCount * (unsigned int)sizeof(Vertex);
    unsigned int indexArraySize   = indexCount  * (unsigned int)sizeof(unsigned int);
    unsigned int boneFramesSize   = (boneFrames ? (frameCount*MAX_BONES*16*sizeof(float)) : 0);

    header.vertexArrayOffset      = headerSize;
    header.indexArrayOffset       = header.vertexArrayOffset + vertexArraySize;
    header.boneFramesArrayOffset  = header.indexArrayOffset   + indexArraySize;

    fwrite(&header, sizeof(header), 1, out);
    fwrite(vertices, sizeof(Vertex), vertexCount, out);
    if (indices && indexCount > 0) {
        fwrite(indices, sizeof(unsigned int), indexCount, out);
    }
    if (boneFrames && boneFramesSize > 0) {
        fwrite(boneFrames, 1, boneFramesSize, out);
    }

    fclose(out);
    printf("  Wrote output file: %s\n", bin_path);

    free(corrected_invBind);
    free(boneFrames);
    free(indices);
    free(vertices);
    cgltf_free(data);
}

int main(void) {
#ifdef _WIN32
    _mkdir("bin");
#else
    mkdir("bin", 0777);
#endif

#ifdef _WIN32
    struct _finddata_t fileinfo;
    intptr_t handle = _findfirst("*.glb", &fileinfo);
    if (handle == -1L) {
        printf("No .glb files found in current directory.\n");
        return 0;
    }
    do {
        process_file(fileinfo.name);
    } while (_findnext(handle, &fileinfo) == 0);
    _findclose(handle);
#else
    DIR *dir = opendir(".");
    if (!dir) {
        perror("Could not open directory");
        return 1;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".glb")) {
            process_file(entry->d_name);
        }
    }
    closedir(dir);
#endif
    return 0;
}
