// Declare the WebGPU API functions from JavaScript
extern void  wgpuInit(int width, int height);
extern int   wgpuCreateMaterial(void* material);
extern int   wgpuCreateMesh(int materialID, void* mesh);
extern int   wgpuAddTexture(int mesh_id, const char* texturePath);
extern float drawGPUFrame(void);

#ifdef __wasm__
#define sqrt(x) (1 + 0.5*((x)-1) - 0.125*(((x)-1)*((x)-1)))
#define sin(x) ((x) - ((x)*(x)*(x))/6.0 + ((x)*(x)*(x)*(x)*(x))/120.0 - ((x)*(x)*(x)*(x)*(x)*(x)*(x))/5040.0)
#define cos(x) (1 - ((x)*(x))/2.0 + ((x)*(x)*(x)*(x))/24.0 - ((x)*(x)*(x)*(x)*(x)*(x))/720.0)
#define tan(x) (sin(x)/cos(x))
void *memset(void *ptr, int value, unsigned long num) {
    unsigned char *p = (unsigned char *)ptr;
    for (long i = 0; i < num; i++) {
        p[i] = (unsigned char)value;
    }
    return ptr;
}
void *memcpy(void *dest, const void *src, unsigned long n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    for (unsigned long i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}
#endif

#include "game.c"

#ifdef __wasm__
#include <stddef.h> // For offsetof
void get_material_struct_offsets(int* offsets) {
    offsets[0] = offsetof(struct Material, used);
    offsets[1] = offsetof(struct Material, hash);
    offsets[2] = offsetof(struct Material, index);
    offsets[3] = offsetof(struct Material, use_alpha);
    offsets[4] = offsetof(struct Material, use_textures);
    offsets[5] = offsetof(struct Material, use_uniforms);
    offsets[6] = offsetof(struct Material, update_instances);
    offsets[7] = offsetof(struct Material, vertex_layout);
    offsets[8] = offsetof(struct Material, texture_layout);
    offsets[9] = offsetof(struct Material, shader);
    offsets[10] = offsetof(struct Material, uniformData);
    offsets[11] = offsetof(struct Material, uniformCurrentOffset);
}
void get_mesh_struct_offsets(int* offsets) {
    offsets[0] = offsetof(struct Mesh, material);
    offsets[1] = offsetof(struct Mesh, indices);
    offsets[2] = offsetof(struct Mesh, vertices);
    offsets[3] = offsetof(struct Mesh, instances);
    offsets[4] = offsetof(struct Mesh, texture_ids);
    offsets[5] = offsetof(struct Mesh, indexCount);
    offsets[6] = offsetof(struct Mesh, vertexCount);
    offsets[7] = offsetof(struct Mesh, instance_count);
}
#endif

// Entry point for Wasm
int main(void) {
    // Add a projection matrix (a 4x4 matrix).  
    float view[16] = {
    1.0 / (tan(fov / 2.0) * aspect_ratio), 0.0f,  0.0f,                               0.0f,
    0.0f,  1.0 / tan(fov / 2.0),          0.0f,                               0.0f,
    0.0f,  0.0f, -(farClip + nearClip) / (farClip - nearClip), -(2 * farClip * nearClip) / (farClip - nearClip),
    0.0f,  0.0f, -1.0f,                               0.0f
    };

    int basic_material_id = wgpuCreateMaterial(&basic_material);
    int hud_material_id = wgpuCreateMaterial(&hud_material);

    int ground_mesh_id = wgpuCreateMesh(basic_material_id, &ground_mesh);
    int quad_mesh_id = wgpuCreateMesh(hud_material_id, &quad_mesh);

    int font_atlas_texture_slot = wgpuAddTexture(quad_mesh_id, "data/textures/bin/font_atlas.bin");

    int aspect_ratio_uniform = addUniform(&hud_material, &aspect_ratio, sizeof(float));
    int brightnessOffset = addUniform(&basic_material, &brightness, sizeof(float));
    int timeOffset = addUniform(&basic_material, &timeVal, sizeof(float));
    int cameraOffset = addUniform(&basic_material, camera, sizeof(camera));
    int viewOffset = addUniform(&basic_material, view, sizeof(view));

    // Main render loop (simplified for demonstration)
    for (int i = 0; i < 3; i++) {
        drawGPUFrame();
    }
    return 0;
}
