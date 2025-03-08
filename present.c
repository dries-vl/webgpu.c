#include "game_data.h" // todo: rename to webgpu.c

#include <stdio.h> // REMOVE, for debugging only

#pragma region GLOBALS
// todo: this needs to be passed to platform, graphics AND presentation layer somehow
int FORCE_RESOLUTION = 0;
int FULLSCREEN = 1;
int WINDOW_WIDTH = 1280; // todo: fps degrades massively when at higher resolution, even with barely any fragment shader logic
int WINDOW_HEIGHT = 720; // todo: make this global variable that can be modified
int VIEWPORT_WIDTH = 1280;
int VIEWPORT_HEIGHT = 720;
int OFFSET_X = 0; // offset to place smaller-than-window viewport at centre of screen
int OFFSET_Y = 0;
float ASPECT_RATIO = 1.77;
#pragma endregion

#include "game.c"

#pragma region GRAPHICS
struct Graphics {
    void *context;
    int   (*createGPUPipeline)(void *context, const char *shader);
    int   (*createGPUMesh)(void *context, int material_id, void *v, int vc, void *i, int ic, void *ii, int iic);
    int   (*createGPUTexture)(void *context, int mesh_id, void *data, int w, int h);
    int   (*addGPUGlobalUniform)(void *context, int pipeline_id, const void* data, int data_size);
    void  (*setGPUGlobalUniformValue)(void *context, int pipeline_id, int offset, const void* data, int dataSize);
    int   (*addGPUMaterialUniform)(void *context, int material_id, const void* data, int data_size);
    void  (*setGPUMaterialUniformValue)(void *context, int material_id, int offset, const void* data, int dataSize);
    void  (*setGPUInstanceBuffer)(void *context, int mesh_id, void* ii, int iic);
    float (*drawGPUFrame)(void *context, int offset_x, int offset_y, int viewport_width, int viewport_height);
};
#pragma endregion

#pragma region PLATFORM
struct MappedMemory {
    void *data;     // Base pointer to mapped file data
    void *mapping;  // Opaque handle for the mapping (ex. Windows HANDLE)
};
struct Platform {
    struct MappedMemory (*map_file)(const char *filename);
    void (*unmap_file)(struct MappedMemory *mm);
    double (*current_time_ms)();
};
/* MEMORY MAPPING MESH */
typedef struct {
    unsigned int vertexCount;
    unsigned int indexCount;
    unsigned int vertexArrayOffset;
    unsigned int indexArrayOffset;
} MeshHeader;
struct MappedMemory load_mesh(struct Platform *p, const char *filename, void** v, int *vc, void** i, int *ic) {
    struct MappedMemory mm = p->map_file(filename);
    
    MeshHeader *header = (MeshHeader*)mm.data;
    // Set pointers into the mapped memory using the header's offsets
    *vc = header->vertexCount;
    *v = (unsigned char*)mm.data + header->vertexArrayOffset;
    *ic  = header->indexCount;
    *i  = (unsigned int*)((unsigned char*)mm.data + header->indexArrayOffset);
    
    return mm;
}
/* MEMORY MAPPING TEXTURE */
typedef struct {
    int width;
    int height;
} ImageHeader;  
struct MappedMemory load_texture(struct Platform *p, const char *filename, int *out_width, int *out_height) {
    struct MappedMemory mm = p->map_file(filename);

    ImageHeader *header = (ImageHeader*)mm.data;
    *out_width  = header->width;
    *out_height = header->height;
    return mm;
}
#pragma endregion

// todo: we need a much better way to manage meshes etc.
// todo: use tsoding's nob.h header to build to avoid bat files
int tick(struct Platform *p, struct Graphics *g) {

    static int init_done = 0;

    static double ms = 0;
    static double ms_last_frame = 0;
    ms_last_frame = init_done ? p->current_time_ms() - ms : 0;
    ms = p->current_time_ms();

    // todo: lighting
    // todo: cubemap sky
    // todo: character mesh
    // todo: animate the character mesh (skeleton?)
    // todo: use precompiled shader for faster loading
    
    static int main_pipeline;
    
    static int teapot_mesh_id;
    static int cube_mesh_id;

    // todo: separate material from mesh -> set material when creating mesh, and set shader once in material
    // todo: RGB 3x8bit textures, no alpha
    static int hud_shader_id = 0;
    static int base_shader_id = 1;

    static int ground_mesh_id;
    static int quad_mesh_id;

    static int cube_texture_id;
    static int quad_texture_id;
    static int ground_texture_id;

    float view[16] = {
        1.0 / (tan(fov / 2.0) * ASPECT_RATIO), 0.0f,  0.0f,                               0.0f,
        0.0f,  1.0 / tan(fov / 2.0),          0.0f,                               0.0f,
        0.0f,  0.0f, -(farClip + nearClip) / (farClip - nearClip), -(2 * farClip * nearClip) / (farClip - nearClip),
        0.0f,  0.0f, -1.0f,                               0.0f
    };
    static int brightnessOffset;
    static int timeOffset;
    static int cameraOffset; 
    static int viewOffset;

    // instance data cannot go out of scope!
    static struct Instance teapot = {
        .transform = {
            1., 0, 0, 0,
            0, 1., 0, 0,
            0, 0, 1., 0,
            0, 0, -200, 1
        }
    };
    static struct Instance cube = {
        .transform = {
            100, 0, 0, 0,
            0, 100, 0, 0,
            0, 0, 100, 0,
            0, 0, 0, 1
        },
        .data = {7, 0, 0},
    };

    if (!init_done) {
        init_done = 1;
        // CREATE MATERIALS
        main_pipeline = createGPUPipeline(g->context, "data/shaders/shader.wgsl");
        // hud_material_id = createGPUPipeline(g->context, "data/shaders/hud.wgsl");

        // LOAD MESHES FROM DISK
        int vc, ic; void *v, *i;
        struct MappedMemory teapot_mm = load_mesh(p, "data/models/bin/teapot.bin", &v, &vc, &i, &ic);
        teapot_mesh_id = createGPUMesh(g->context, main_pipeline, v, vc, i, ic, &teapot, 1);
        p->unmap_file(&teapot_mm);

        struct MappedMemory cube_mm = load_mesh(p, "data/models/bin/cube.bin", &v, &vc, &i, &ic);
        cube_mesh_id = createGPUMesh(g->context, main_pipeline, v, vc, i, ic, &cube, 1);
        p->unmap_file(&cube_mm);

        // PREDEFINED MESHES
        ground_mesh_id = createGPUMesh(g->context, main_pipeline, &quad_vertices, 4, &quad_indices, 6, &ground_instance, 1);
        quad_mesh_id = createGPUMesh(g->context, main_pipeline, &quad_vertices, 4, &quad_indices, 6, &char_instances, MAX_CHAR_ON_SCREEN);
        addGPUMaterialUniform(g->context, quad_mesh_id, &hud_shader_id, sizeof(hud_shader_id));
        // todo: one shared material
        // todo: why are functions directly available here (should use struct?) ~Instance struct should be known, functions not somehow
        addGPUMaterialUniform(g->context, teapot_mesh_id, &base_shader_id, sizeof(base_shader_id));
        addGPUMaterialUniform(g->context, cube_mesh_id, &base_shader_id, sizeof(base_shader_id));
        addGPUMaterialUniform(g->context, ground_mesh_id, &base_shader_id, sizeof(base_shader_id));

        // TEXTURE
        int w, h = 0;
        struct MappedMemory china_texture_mm = load_texture(p, "data/textures/bin/china.bin", &w, &h);
        cube_texture_id = createGPUTexture(g->context, cube_mesh_id, china_texture_mm.data, w, h);
        struct MappedMemory font_texture_mm = load_texture(p, "data/textures/bin/font_atlas_small.bin", &w, &h);
        quad_texture_id = createGPUTexture(g->context, quad_mesh_id, font_texture_mm.data, w, h);
        p->unmap_file(&font_texture_mm);
        p->unmap_file(&china_texture_mm);

        struct MappedMemory ground_texture_mm = load_texture(p, "data/textures/bin/stone.bin", &w, &h);
        ground_texture_id = createGPUTexture(g->context, ground_mesh_id, ground_texture_mm.data, w, h);
        p->unmap_file(&ground_texture_mm);

        // UNIFORMS
        brightnessOffset = addGPUGlobalUniform(g->context, main_pipeline, &brightness, sizeof(float));
        timeOffset = addGPUGlobalUniform(g->context, main_pipeline, &timeVal, sizeof(float));
        cameraOffset = addGPUGlobalUniform(g->context, main_pipeline, camera, sizeof(camera));
        viewOffset = addGPUGlobalUniform(g->context, main_pipeline, view, sizeof(view));

        // gamestate shit
        initGamestate(&gameState);
        gameState.objects[0].instance = &cube;
    }

    // Update uniforms
    timeVal += 0.016f; // pretend 16ms per frame
    //yaw(0.001f * ms_last_frame, camera);
    playerMovement(movementSpeed, ms_last_frame, &gameState.player);
    float playerLocation[3] = {gameState.player.instance->transform[3], gameState.player.instance->transform[7], gameState.player.instance->transform[11]};
    applyGravity(&gameState.player.velocity, playerLocation, ms_last_frame);
    camera[3] = gameState.player.instance->transform[3];
    camera[7] = gameState.player.instance->transform[7];
    camera[11] = gameState.player.instance->transform[11];
    //collisionDetectionCamera(cubeCollisionBox);
    // struct Vector3 separation = detectCollision(cameraCollisionBox, cubeCollisionBox);
    //printf("Collision detected: %4.2f\n", separation.x);
    //camera[7] = cameraLocation[1];
    float inv[16];
    inverseViewMatrix(camera, inv);
    setGPUGlobalUniformValue(g->context, main_pipeline, timeOffset, &timeVal, sizeof(float));
    setGPUGlobalUniformValue(g->context, main_pipeline, cameraOffset, &inv, sizeof(camera));

    // update the instances of the text
    setGPUInstanceBuffer(g->context, quad_mesh_id, &char_instances, screen_chars_index);

    g->drawGPUFrame(g->context, OFFSET_X, OFFSET_Y, VIEWPORT_WIDTH, VIEWPORT_HEIGHT);

    {
        screen_chars_index = 0;
        current_screen_char = 0; // todo: replace with function that resets instead of spaghetti
        memset(char_instances, 0, sizeof(char_instances));
        setGPUInstanceBuffer(g->context, quad_mesh_id, &char_instances, screen_chars_index);
    }

    return 0;
}