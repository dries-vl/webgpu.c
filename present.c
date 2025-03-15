#include "game_data.h" // todo: rename to webgpu.h

#include <stdio.h> // REMOVE, for debugging only

#pragma region GLOBALS
// todo: this needs to be passed to platform, graphics AND presentation layer somehow
#define FORCE_RESOLUTION 0
#define FULLSCREEN 1
#define WINDOWED 1
int WINDOW_WIDTH = 1280; // todo: fps degrades massively when at higher resolution, even with barely any fragment shader logic
int WINDOW_HEIGHT = 720; // todo: make this global variable that can be modified
int VIEWPORT_WIDTH = 1280;
int VIEWPORT_HEIGHT = 720;
int OFFSET_X = 0; // offset to place smaller-than-window viewport at centre of screen
int OFFSET_Y = 0;
float ASPECT_RATIO = 1.77;
#pragma endregion

#include "game_data.c" // todo: inline this here and remove file
#include "game.c" // todo: what to expose to game.c? print_on_screen

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
    unsigned int boneCount;
    unsigned int frameCount;
    unsigned int vertexArrayOffset;
    unsigned int indexArrayOffset;
    unsigned int boneFramesArrayOffset;
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
struct MappedMemory load_animated_mesh(struct Platform *p, const char *filename,
                                   void** vertices, int *vertexCount,
                                   void** indices, int *indexCount,
                                   void** boneFrames, int *boneCount,
                                   int *frameCount) {
    // Map the file into memory.
    struct MappedMemory mm = p->map_file(filename);
    if (!mm.data) {
        printf("Error: could not map file %s\n", filename);
        return mm;
    }
    
    // The file begins with an AnimatedMeshHeader.
    MeshHeader *header = (MeshHeader*) mm.data;
    
    // Set the vertex pointer and count.
    *vertexCount = header->vertexCount;
    *vertices = (unsigned char*) mm.data + header->vertexArrayOffset;
    
    // Set the index pointer and count.
    *indexCount = header->indexCount;
    *indices = (unsigned int*)((unsigned char*) mm.data + header->indexArrayOffset);
    
    // Set the bone frames pointer, bone count, and frame count.
    *boneCount = header->boneCount;
    *frameCount = header->frameCount;
    *boneFrames = (unsigned char*) mm.data + header->boneFramesArrayOffset;
    
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
int tick(struct Platform *p, void *context) {

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
    
    static int character_mesh_id;
    static int char2_mesh_id;
    static int cube_mesh_id;

    // todo: separate material from mesh -> set material when creating mesh, and set shader once in material
    // todo: RGB 3x8bit textures, no alpha
    static int hud_shader_id = 0;
    static int base_shader_id = 1;

    static int ground_mesh_id;
    static int quad_mesh_id;

    int pine_mesh_id[10];
    int pine_texture_id[10];
    struct GameObject pineo[10];

    static int cube_texture_id;
    static int quad_texture_id;
    static int ground_texture_id;
    static int colormap_texture_id;

    float f = 1.0f / tan(fov / 2.0f);
    float projection[16] = {
        f / ASPECT_RATIO, 0.0f,                          0.0f, 0.0f,
        0.0f,             f,                             0.0f, 0.0f,
        0.0f,             0.0f,      farClip / (farClip - nearClip), 1.0f,
        0.0f,             0.0f, (-nearClip * farClip) / (farClip - nearClip), 0.0f
    };
    
    static int brightnessOffset;
    static int timeOffset;
    static int viewOffset; 
    static int projectionOffset;

    // instance data cannot go out of scope!
    static struct Instance character = {
        .transform = {
            1., 0, 0, 0,
            0, 1., 0, 0,
            0, 0, 1., 0,
            0, 0, -2, 1
        },
        .atlas_uv = {0, 0}
    };
    static struct Instance character2 = {
        .transform = {
            1., 0, 0, 0,
            0, 1., 0, 0,
            0, 0, 1., 0,
            0, 0, 4, 1
        },
        .atlas_uv = {0, 0}
    };
    static struct Instance cube = {
        .transform = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 2, 1
        },
        .data = {7, 0, 0},
    };

    if (!init_done) {
        init_done = 1;
        // CREATE MATERIALS
        main_pipeline = createGPUPipeline(context, "data/shaders/shader.wgsl");
        // hud_material_id = createGPUPipeline(context, "data/shaders/hud.wgsl");

        // LOAD MESHES FROM DISK
        int vc, ic; void *v, *i;
        void *bf; int bc, fc;
        // 813 vertices, 2127 indices
        // 2127 vertices, 2127 indices
        struct MappedMemory character_mm = load_animated_mesh(p, "data/models/blender/bin/charA.bin", &v, &vc, &i, &ic, &bf, &bc, &fc);
        printf("frame count: %d, bone count: %d\n", fc, bc);
        character_mesh_id = createGPUMesh(context, main_pipeline, v, vc, i, ic, &character, 1);
        addGPUMaterialUniform(context, character_mesh_id, &base_shader_id, sizeof(base_shader_id));
        setGPUMeshBoneData(context, character_mesh_id, bf, bc, fc);
        // todo: we cannot unmap the bones data, maybe memcpy it here to make it persist
        // todo: fix script for correct UVs etc.
        // p->unmap_file(&character_mm);
        
        void *bf1; int bc1, fc1;
        struct MappedMemory char2_mm = load_animated_mesh(p, "data/models/blender/bin/charA2.bin", &v, &vc, &i, &ic, &bf1, &bc1, &fc1);
        printf("frame count: %d, bone count: %d\n", fc1, bc1);
        char2_mesh_id = createGPUMesh(context, main_pipeline, v, vc, i, ic, &character2, 1);
        addGPUMaterialUniform(context, char2_mesh_id, &base_shader_id, sizeof(base_shader_id));
        setGPUMeshBoneData(context, char2_mesh_id, bf1, bc1, fc1);
        // todo: we cannot unmap the bones data, maybe memcpy it here to make it persist
        // todo: fix script for correct UVs etc.
        // p->unmap_file(&char2_mm);
        
        struct MappedMemory cube_mm = load_mesh(p, "data/models/blender/bin/cube.bin", &v, &vc, &i, &ic);
        cube_mesh_id = createGPUMesh(context, main_pipeline, v, vc, i, ic, &cube, 1);
        addGPUMaterialUniform(context, cube_mesh_id, &base_shader_id, sizeof(base_shader_id));
        p->unmap_file(&cube_mm);

        // PREDEFINED MESHES
        ground_mesh_id = createGPUMesh(context, main_pipeline, &quad_vertices, 4, &quad_indices, 6, &ground_instance, 1);
        addGPUMaterialUniform(context, ground_mesh_id, &base_shader_id, sizeof(base_shader_id));
        quad_mesh_id = createGPUMesh(context, main_pipeline, &quad_vertices, 4, &quad_indices, 6, &char_instances, MAX_CHAR_ON_SCREEN);
        addGPUMaterialUniform(context, quad_mesh_id, &hud_shader_id, sizeof(hud_shader_id));
        // todo: one shared material
        // todo: why are functions directly available here (should use struct?) ~Instance struct should be known, functions not somehow

        // TEXTURE
        int w, h = 0;
        struct MappedMemory china_texture_mm = load_texture(p, "data/textures/bin/china.bin", &w, &h);
        cube_texture_id = createGPUTexture(context, cube_mesh_id, china_texture_mm.data, w, h);
        struct MappedMemory font_texture_mm = load_texture(p, "data/textures/bin/font_atlas_small.bin", &w, &h);
        cube_texture_id = createGPUTexture(context, cube_mesh_id, font_texture_mm.data, w, h);
        quad_texture_id = createGPUTexture(context, quad_mesh_id, font_texture_mm.data, w, h);
        p->unmap_file(&font_texture_mm);
        p->unmap_file(&china_texture_mm);

        struct MappedMemory ground_texture_mm = load_texture(p, "data/textures/bin/stone.bin", &w, &h);
        ground_texture_id = createGPUTexture(context, ground_mesh_id, ground_texture_mm.data, w, h);
        p->unmap_file(&ground_texture_mm);

        struct MappedMemory colormap_mm = load_texture(p, "data/textures/bin/colormap.bin", &w, &h);
        colormap_texture_id = createGPUTexture(context, character_mesh_id, colormap_mm.data, w, h);
        colormap_texture_id = createGPUTexture(context, char2_mesh_id, colormap_mm.data, w, h);
        p->unmap_file(&colormap_mm);

        // UNIFORMS
        brightnessOffset = addGPUGlobalUniform(context, main_pipeline, &brightness, sizeof(float));
        timeOffset = addGPUGlobalUniform(context, main_pipeline, &timeVal, sizeof(float));
        viewOffset = addGPUGlobalUniform(context, main_pipeline, view, sizeof(view));
        projectionOffset = addGPUGlobalUniform(context, main_pipeline, projection, sizeof(projection));

        // gamestate shit
        initGamestate(&gameState);
        struct GameObject cube = {
            .collisionBox = cubeCollisionBox,
            .instance = &cube,
            .velocity = {0}
        };
        addGameObject(&gameState, &cube);
        for (int j = 0; j < 10; j++) {
            // instance data
            memcpy(&pines[j], &pine, sizeof(struct Instance));
            pines[j].transform[12] = 10.0 * cos(j * 0.314 * 2);
            pines[j].transform[14] = 10.0 * sin(j * 0.314 * 2);
            // mesh
            struct MappedMemory pine_mm = load_mesh(p, "data/models/bin/pine.bin", &v, &vc, &i, &ic);
            pine_mesh_id[j] = createGPUMesh(context, main_pipeline, v, vc, i, ic, &pines[j], 1);
            p->unmap_file(&pine_mm);
            // texture
            struct MappedMemory green_texture_mm = load_texture(p, "data/textures/bin/colormap.bin", &w, &h);
            pine_texture_id[j] = createGPUTexture(context, pine_mesh_id[j], green_texture_mm.data, w, h);
            addGPUMaterialUniform(context, pine_mesh_id[j], &base_shader_id, sizeof(base_shader_id));
            p->unmap_file(&green_texture_mm);
            pineo[j] = (struct GameObject){
                .collisionBox = {0},
                .instance = &pines[j],
                .velocity = {0}
            };
            memcpy(&pineo[j].collisionBox, &pineCollisionBox, sizeof(struct Rigid_Body));
            pineo[j].collisionBox.position.x = pines[j].transform[12];
            pineo[j].collisionBox.position.z = pines[j].transform[14];
            addGameObject(&gameState, &pineo[j]);
        }
    }

    // Update uniforms
    timeVal += 0.016f; // pretend 16ms per frame
    //yaw(0.001f * ms_last_frame, camera);
    playerMovement(movementSpeed, ms_last_frame, &gameState.player);
    float playerLocation[3] = {gameState.player.instance->transform[12], gameState.player.instance->transform[13], gameState.player.instance->transform[14]};
    applyGravity(&gameState.player.velocity, playerLocation, ms_last_frame);
    view[12] = gameState.player.instance->transform[12];
    view[13] = gameState.player.instance->transform[13];
    view[14] = gameState.player.instance->transform[14];
    //collisionDetectionCamera(cubeCollisionBox);
    // struct Vector3 separation = detectCollision(cameraCollisionBox, cubeCollisionBox);
    //printf("Collision detected: %4.2f\n", separation.x);
    //view[13] = cameraLocation[1];
    float inv[16];
    inverseViewMatrix(view, inv); // todo: why do we need the inverse here?
    setGPUGlobalUniformValue(context, main_pipeline, timeOffset, &timeVal, sizeof(float));
    setGPUGlobalUniformValue(context, main_pipeline, viewOffset, &inv, sizeof(view));

    // update the instances of the text
    setGPUInstanceBuffer(context, quad_mesh_id, &char_instances, screen_chars_index);

    drawGPUFrame(context, OFFSET_X, OFFSET_Y, VIEWPORT_WIDTH, VIEWPORT_HEIGHT);

    {
        screen_chars_index = 0;
        current_screen_char = 0; // todo: replace with function that resets instead of spaghetti
        memset(char_instances, 0, sizeof(char_instances));
        setGPUInstanceBuffer(context, quad_mesh_id, &char_instances, screen_chars_index);
    }

    return 0;
}