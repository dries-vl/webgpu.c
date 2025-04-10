#include "platform.h"
#include "graphics.h"

#include <stdio.h> // REMOVE, for debugging only

#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>

#pragma region GLOBALS
// todo: this needs to be passed to platform, graphics AND presentation layer somehow
#define FORCE_RESOLUTION 0 // force the resolution of the screen to the original width/height -> old-style flicker if changed
#define FORCE_ASPECT_RATIO 0
#define FULLSCREEN 1
#define WINDOWED 0
int SHOW_CURSOR = 0;
#define ORIGINAL_WIDTH 1280
#define ORIGINAL_HEIGHT 720
#define ORIGINAL_ASPECT_RATIO 1.77
int WINDOW_WIDTH = ORIGINAL_WIDTH; // todo: fps degrades massively when at higher resolution, even with barely any fragment shader logic
int WINDOW_HEIGHT = ORIGINAL_HEIGHT; // todo: make this global variable that can be modified at runtime
int VIEWPORT_WIDTH = ORIGINAL_WIDTH;
int VIEWPORT_HEIGHT = ORIGINAL_HEIGHT;
int OFFSET_X = 0; // offset to place smaller-than-window viewport at centre of screen
int OFFSET_Y = 0;
float ASPECT_RATIO = ORIGINAL_ASPECT_RATIO;
#pragma endregion

#define PRINT_MS(label, value, name) do { \
    static double avg_buf_##name[60] = {0}; \
    static int avg_i_##name = 0; \
    avg_buf_##name[avg_i_##name] = (value); \
    avg_i_##name = (avg_i_##name + 1) % 60; \
    double avg_##name = 0; \
    for (int i = 0; i < 60; i++) avg_##name += avg_buf_##name[i] / 60.; \
    double slowest = 0.0; \
    for (int i = 0; i < 60; i++) if (avg_buf_##name[i] > slowest) slowest = avg_buf_##name[i]; \
    char buf_##name[64]; \
    snprintf(buf_##name, sizeof(buf_##name), label "%4.2fms (%4.2f)\n", avg_##name, slowest); \
    print_on_screen(buf_##name); \
} while (0)

#pragma region GAME_DATA
// Helper macro to convert a float UV (assumed to be in the range [-1,1] or [0,1])
// to an unsigned short (mapping -1 => 0 and 1 => 65535).
#define FLOAT_TO_U16(x) ((uint16_t) x * 65535.0f)


static struct Instance ground_instance = {
    .transform = {
         100, 0, 0, 0,
         0, 0, 100, 0,
         0, 100, 0, 0,
         0, 0, 0, 1
    },
    .data = {20, 0, 0},
    .norms = {0},
    .animation = 0,
    .animation_phase = 0.0f,
    .atlas_uv = {0}
};

struct Instance pine = {
    .transform = {
        5.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 5.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 5.0f, 0.0f,
        0.0f, 0.0f, 10.0f, 1.0f
    },
    .data = {0},
    .norms = {0},
    .animation = 0,
    .animation_phase = 0.0f,
    .atlas_uv = {0}
};
struct Instance pines[10];
#pragma endregion

#pragma region PRINT_ON_SCREEN
// HUD quad (2D UI element)
static struct Vertex quad_vertices[4] = {
    {
        .data = {0},
        .position = {-0.5f, 0.5f, 0.0f},
        .normal = {0, 0, 127, 0},
        .tangent = {0},
        .uv = {FLOAT_TO_U16(0.0f), FLOAT_TO_U16(0.0f)},
        .bone_weights = {UCHAR_MAX, 0, 0, 0},
        .bone_indices = {0}
    },
    {
        .data = {0},
        .position = {0.5f, 0.5f, 0.0f},
        .normal = {0, 0, 127, 0},
        .tangent = {0},
        .uv = {FLOAT_TO_U16(1.0f), FLOAT_TO_U16(0.0f)},
        .bone_weights = {UCHAR_MAX, 0, 0, 0},
        .bone_indices = {0}
    },
    {
        .data = {0},
        .position = {-0.5f, -0.5f, 0.0f},
        .normal = {0, 0, 127, 0},
        .tangent = {0},
        .uv = {FLOAT_TO_U16(0.0f), FLOAT_TO_U16(1.0f)},
        .bone_weights = {UCHAR_MAX, 0, 0, 0},
        .bone_indices = {0}
    },
    {
        .data = {0},
        .position = {0.5f, -0.5f, 0.0f},
        .normal = {0, 0, 127, 0},
        .tangent = {0},
        .uv = {FLOAT_TO_U16(1.0f), FLOAT_TO_U16(1.0f)},
        .bone_weights = {UCHAR_MAX, 0, 0, 0},
        .bone_indices = {0}
    }
};
static uint32_t quad_indices[6] = {0, 2, 1, 2, 3, 1};

#define CHAR_COLUMNS 48 // columns of text that fit across half of the screen
#define CHAR_ROWS 24 // rows that fit across half of the screen
#define MAX_CHAR_ON_SCREEN (CHAR_COLUMNS * CHAR_ROWS)  // 2304 max characters
// screen coordinates goes from -1 to 1, so we need to account for this in some places
#define CHAR_HALF_COLUMNS (CHAR_COLUMNS/2)
#define CHAR_HALF_ROWS (CHAR_ROWS/2)
#define CHAR_WIDTH  (1.0 / CHAR_HALF_COLUMNS)  // width of one character
#define CHAR_HEIGHT (1.0 / CHAR_HALF_ROWS)  // height of one character

static struct Instance char_instances[MAX_CHAR_ON_SCREEN] = {0};
int screen_chars_index = 0;
int current_screen_char = 0;
void print_on_screen(const char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        // Prevent buffer overflow
        if (current_screen_char >= MAX_CHAR_ON_SCREEN)
            // todo: also break based on current_screen_char to account for skipped chars
            break;
        
        // Handle newline: jump to the beginning of the next row
        if (str[i] == '\n') {
            current_screen_char = ((current_screen_char / CHAR_COLUMNS) + 1) * CHAR_COLUMNS;
            continue;
        }
        
        // x and y on the 48 x 24 grid of possible characters on screen
        int col = current_screen_char % CHAR_COLUMNS;
        int row = current_screen_char / CHAR_COLUMNS;
        
        // convert to an actual position from 0 to 1
        float x = (col / (((float) CHAR_HALF_COLUMNS * 1.1)) / ASPECT_RATIO); // times 1.1 for slight kerning fix
        float y = row / (float) CHAR_HALF_ROWS;
        x -= 0.95; // x starts at -1 in topleft corner
        y = 0.95 - y; // y starts at 1 in topleft corner

        // Build a transform matrix that scales the quad and translates it
        // (Column-major order: first 4 floats = first column, etc.)
        float transform[16] = {
            // Column 0: scale X
            CHAR_WIDTH/ASPECT_RATIO, 0.0f, 0.0f, 0.0f,
            // Column 1: scale Y
            0.0f, CHAR_HEIGHT, 0.0f, 0.0f,
            // Column 2: Z remains unchanged
            0.0f, 0.0f, 1.0f, 0.0f,
            // Column 3: translation (x, y, 0) with homogeneous coordinate 1
            x,    y,    0.0f, 1.0f
        };
        
        // Create a new Instance for this character
        struct Instance *inst = &char_instances[screen_chars_index];
        for (int j = 0; j < 16; j++) {
            inst->transform[j] = transform[j];
        }
        
        // Map the ASCII code to an atlas cell in a 16x8 grid.
        // For example, 'A' (ASCII 65) will go to cell (65 % 16, 65 / 16)
        unsigned char ascii = str[i];
        unsigned char atlas_col = ascii % 16;
        unsigned char atlas_row = ascii / 16;
        inst->atlas_uv[0] = ((uint16_t) atlas_col) * 4096 + (4096 / 8); // todo: why do we have to add an eight (?)
        inst->atlas_uv[1] = ((uint16_t) atlas_row) * 8192;
        
        // (Other fields like data, norms, animation, etc. remain zero for now)

        screen_chars_index++;
        current_screen_char++;
    }
}
#pragma endregion

#pragma region PLATFORM
#pragma endregion

#include "game.c" // todo: put game.c very isolated like graphics, platform. // Q: what to expose to game.c? print_on_screen?

// todo: we need a much better way to manage meshes etc.
int tick(struct Platform *p, void *context) {

    static int init_done = 0;

    // keep track of tick and frame timing
    static double time_previous_frame = 0.0;
    static double delta = 0.0;
    double time_now = p->current_time_ms();
    delta = init_done ? time_now - time_previous_frame : 0.0;
    time_previous_frame = time_now;

    static double vsync_delay = 0.0;
    static double time_spent_anticipating_vsync = 0.0;
    
    // sleep the amount of time the last frame waited for vsync (to anticipate blocking time, and allow input events to still arrive for this frame)
    double time_before_wait = p->current_time_ms();
    static double time_to_wait = 0.0;
    if (vsync_delay > 1.0 && time_to_wait < 16.0) time_to_wait += 1.0;
    if (vsync_delay < 1.0 || time_to_wait > 16.0) time_to_wait -= 1.0;
    // todo: way to avoid tearing without exclusive fullscreen / branch here on that setting
    // printf("time to wait: %4.2f\n", time_to_wait);
    if (time_to_wait >= 1.0) p->sleep_ms(time_to_wait);
    time_spent_anticipating_vsync = p->current_time_ms() - time_before_wait;
    // if(time_spent_anticipating_vsync > 16.0) printf("time waited: %4.2f\n", time_spent_anticipating_vsync);

    // wait on the fence to measure GPU work time // *ONLY USE FOR DEBUG, OTHERWISE DON'T SYNC TO AVOID SLOWDOWN*
    // todo: do we still need this if we wait for vsync already (?)
    double gpu_ms = block_on_gpu_queue(context, p);

    double tick_start_ms = p->current_time_ms();
    
    // todo: use precompiled shader for faster loading
    
    #pragma region statics
    static int main_pipeline;
    
    static int character_mesh_id;
    static int character_shadow_id;
    static int char2_mesh_id;
    static int cube_mesh_id;
    static int sphere_id;
    static int env_cube_id;

    // todo: separate material from mesh -> set material when creating mesh, and set shader once in material
    // todo: RGB 3x8bit textures, no alpha
    static int hud_shader_id = 0;
    static int base_shader_id = 1;
    static int shadow_shader_id = 2;
    static int reflection_shader_id = 3;
    static int env_cube_shader = 4;

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
    static int shadowsOffset;
    static float camera_world_space_offset;
    static float camera_world_space[4] = {0};

    // instance data cannot go out of scope!
    static struct Instance character = {
        .transform = {
            1., 0, 0, 0,
            0, 1., 0, 0,
            0, 0, 1., 0,
            0, 0.5, -8, 1
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
    static struct Instance cube_i = {
        .transform = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 2, 1
        },
        .data = {7, 0, 0},
    };
    static struct Instance cube[1000] = {0};
    static struct Instance env_cube = {
        .transform = {
            100, 0, 0, 0,
            0, 100, 0, 0,
            0, 0, 100, 0,
            0, 0, 0, 1
        },
    };
    static struct Instance sphere = {
        .transform = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 2, 0, 1
        },
    };
    #pragma endregion

    #pragma region init
    if (!init_done) {
        init_done = 1;

        // {
        //     void *cube_data[6];
        //     int w, h = 0;
        //     cube_data[0] = load_texture(p, "data/textures/bin/cube_face_+X.bin", &w, &h).data;
        //     cube_data[1] = load_texture(p, "data/textures/bin/cube_face_-X.bin", &w, &h).data;
        //     cube_data[2] = load_texture(p, "data/textures/bin/cube_face_+Y.bin", &w, &h).data;
        //     cube_data[3] = load_texture(p, "data/textures/bin/cube_face_-Y.bin", &w, &h).data;
        //     cube_data[4] = load_texture(p, "data/textures/bin/cube_face_+Z.bin", &w, &h).data;
        //     cube_data[5] = load_texture(p, "data/textures/bin/cube_face_-Z.bin", &w, &h).data;
        //     load_cube_map(context, cube_data, w);
        // }
        
        {
            void *cube_data[6];
            int w, h = 0;
            cube_data[0] = load_texture(p, "data/textures/bin/bluecloud_ft.bin", &w, &h).data;
            cube_data[1] = load_texture(p, "data/textures/bin/bluecloud_bk.bin", &w, &h).data;
            cube_data[2] = load_texture(p, "data/textures/bin/bluecloud_up.bin", &w, &h).data;
            cube_data[3] = load_texture(p, "data/textures/bin/bluecloud_dn.bin", &w, &h).data;
            cube_data[4] = load_texture(p, "data/textures/bin/bluecloud_rt.bin", &w, &h).data;
            cube_data[5] = load_texture(p, "data/textures/bin/bluecloud_lf.bin", &w, &h).data;
            load_cube_map(context, cube_data, w);
        }

        main_pipeline = createGPUPipeline(context, "data/shaders/shader.wgsl");
         
        int vc, ic; void *v, *i;
        void *bf; int bc, fc;

        // ENVIRONMENT CUBE
        struct MappedMemory env_cube_mm = load_mesh(p, "data/models/blender/bin/env_cube.bin", &v, &vc, &i, &ic);
        env_cube_id = createGPUMesh(context, main_pipeline, 2, v, vc, i, ic, &env_cube, 1);
        addGPUMaterialUniform(context, env_cube_id, &env_cube_shader, sizeof(base_shader_id));
        p->unmap_file(&env_cube_mm);
 
        // PREDEFINED MESHES
        ground_mesh_id = createGPUMesh(context, main_pipeline, 0, &quad_vertices, 4, &quad_indices, 6, &ground_instance, 1);
        addGPUMaterialUniform(context, ground_mesh_id, &base_shader_id, sizeof(base_shader_id));
        quad_mesh_id = createGPUMesh(context, main_pipeline, 0, &quad_vertices, 4, &quad_indices, 6, &char_instances, MAX_CHAR_ON_SCREEN);
        addGPUMaterialUniform(context, quad_mesh_id, &hud_shader_id, sizeof(hud_shader_id));
 
        // LOAD MESHES FROM DISK
        struct MappedMemory character_mm = load_animated_mesh(p, "data/models/blender/bin/charA.bin", &v, &vc, &i, &ic, &bf, &bc, &fc);
        printf("frame count: %d, bone count: %d\n", fc, bc);
        character_mesh_id = createGPUMesh(context, main_pipeline, 2, v, vc, i, ic, &character, 1);
        character_shadow_id = createGPUMesh(context, main_pipeline, 2, v, vc, i, ic, &character, 1);
        addGPUMaterialUniform(context, character_mesh_id, &base_shader_id, sizeof(int));
        addGPUMaterialUniform(context, character_shadow_id, &shadow_shader_id, sizeof(int));
        setGPUMeshBoneData(context, character_mesh_id, bf, bc, fc);
        setGPUMeshBoneData(context, character_shadow_id, bf, bc, fc);
        // todo: we cannot unmap the bones data, maybe memcpy it here to make it persist
        // todo: fix script for correct UVs etc.
        // p->unmap_file(&character_mm);

        // void *bf1; int bc1, fc1;
        // struct MappedMemory char2_mm = load_animated_mesh(p, "data/models/blender/bin/charA2.bin", &v, &vc, &i, &ic, &bf1, &bc1, &fc1);
        // printf("frame count: %d, bone count: %d\n", fc1, bc1);
        // char2_mesh_id = createGPUMesh(context, main_pipeline, 2, v, vc, i, ic, &character2, 1);
        // addGPUMaterialUniform(context, char2_mesh_id, &shadow_shader_id, sizeof(shadow_shader_id));
        // setGPUMeshBoneData(context, char2_mesh_id, bf1, bc1, fc1);
        // // todo: we cannot unmap the bones data, maybe memcpy it here to make it persist
        // // todo: fix script for correct UVs etc.
        // p->unmap_file(&char2_mm);
        
        struct MappedMemory cube_mm = load_mesh(p, "data/models/bin/cube.bin", &v, &vc, &i, &ic);
        for (int c = 0;c<1;c++) {
            for (int j = 0; j < 1; j++) {
                cube[c] = cube_i;
                cube[c].transform[12] = (rand() % 50) - 25; // X
                cube[c].transform[13] = (rand() % 25); // Y
                cube[c].transform[14] = (rand() % 50) - 25; // Z
            }
            cube_mesh_id = createGPUMesh(context, main_pipeline, 2, v, vc, i, ic, &cube[c], 1);
            addGPUMaterialUniform(context, cube_mesh_id, &reflection_shader_id, sizeof(reflection_shader_id));
            float cube_reflectiveness = 0.5;
            addGPUMaterialUniform(context, cube_mesh_id, &cube_reflectiveness, sizeof(cube_reflectiveness));
        }
        p->unmap_file(&cube_mm);
       
        struct MappedMemory sphere_mm = load_mesh(p, "data/models/blender/bin/sphere.bin", &v, &vc, &i, &ic);
        sphere_id = createGPUMesh(context, main_pipeline, 2, v, vc, i, ic, &sphere, 1);
        addGPUMaterialUniform(context, sphere_id, &reflection_shader_id, sizeof(reflection_shader_id));
        float sphere_reflectiveness = 1.0;
        addGPUMaterialUniform(context, sphere_id, &sphere_reflectiveness, sizeof(sphere_reflectiveness));
        p->unmap_file(&sphere_mm);


        // TEXTURE
        // int w, h = 0;
        // struct MappedMemory china_texture_mm = load_texture(p, "data/textures/bin/china.bin", &w, &h);
        // cube_texture_id = createGPUTexture(context, cube_mesh_id, china_texture_mm.data, w, h);
        // p->unmap_file(&china_texture_mm);
        // struct MappedMemory font_texture_mm = load_texture(p, "data/textures/bin/font_atlas_small.bin", &w, &h);
        // cube_texture_id = createGPUTexture(context, cube_mesh_id, font_texture_mm.data, w, h);
        // quad_texture_id = createGPUTexture(context, quad_mesh_id, font_texture_mm.data, w, h);
        // p->unmap_file(&font_texture_mm);

        // struct MappedMemory ground_texture_mm = load_texture(p, "data/textures/bin/stone.bin", &w, &h);
        // ground_texture_id = createGPUTexture(context, ground_mesh_id, ground_texture_mm.data, w, h);
        // p->unmap_file(&ground_texture_mm);

        // struct MappedMemory colormap_mm = load_texture(p, "data/textures/bin/colormap.bin", &w, &h);
        // colormap_texture_id = createGPUTexture(context, character_mesh_id, colormap_mm.data, w, h);
        // colormap_texture_id = createGPUTexture(context, char2_mesh_id, colormap_mm.data, w, h);
        // p->unmap_file(&colormap_mm);

        // UNIFORMS
        brightnessOffset = addGPUGlobalUniform(context, main_pipeline, &brightness, sizeof(float));
        timeOffset = addGPUGlobalUniform(context, main_pipeline, &timeVal, sizeof(float));
        viewOffset = addGPUGlobalUniform(context, main_pipeline, view, sizeof(view));
        projectionOffset = addGPUGlobalUniform(context, main_pipeline, projection, sizeof(projection));
        shadowsOffset = addGPUGlobalUniform(context, main_pipeline, &SHADOWS_ENABLED, sizeof(SHADOWS_ENABLED));
        camera_world_space_offset = addGPUGlobalUniform(context, main_pipeline, &camera_world_space, sizeof(camera_world_space));

        // gamestate shit
        initGamestate(&gameState);
        struct GameObject cube = {
            .collisionBox = cubeCollisionBox,
            .instance = &cube,
            .velocity = {0}
        };
        addGameObject(&gameState, &cube);
        gameState.player.instance = &character;
        for (int j = 0; j < 10; j++) {
            // instance data
            memcpy(&pines[j], &pine, sizeof(struct Instance));
            pines[j].transform[12] = 10.0 * cos(j * 0.314 * 2);
            pines[j].transform[14] = 10.0 * sin(j * 0.314 * 2);
            // mesh
            struct MappedMemory pine_mm = load_mesh(p, "data/models/bin/pine.bin", &v, &vc, &i, &ic);
            pine_mesh_id[j] = createGPUMesh(context, main_pipeline, 2, v, vc, i, ic, &pines[j], 1);
            p->unmap_file(&pine_mm);
            // texture
            // struct MappedMemory green_texture_mm = load_texture(p, "data/textures/bin/colormap_2.bin", &w, &h);
            // pine_texture_id[j] = createGPUTexture(context, pine_mesh_id[j], green_texture_mm.data, w, h);
            addGPUMaterialUniform(context, pine_mesh_id[j], &base_shader_id, sizeof(base_shader_id));
            // p->unmap_file(&green_texture_mm);
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
    #pragma endregion

    // poll input events as late as possible (after sleeping for vsync and gpu waiting)
    p->poll_inputs();

    // Update uniforms
    timeVal += 0.016f; // pretend 16ms per frame
    //yaw(0.001f * ms_last_frame, camera);
    playerMovement(movementSpeed, delta, &gameState.player);
    float playerLocation[3] = {gameState.player.instance->transform[12], gameState.player.instance->transform[13], gameState.player.instance->transform[14]};
    applyGravity(&gameState.player.velocity, playerLocation, delta);
    view[12] = gameState.player.instance->transform[12];
    view[13] = gameState.player.instance->transform[13];
    view[14] = gameState.player.instance->transform[14];
    mat4_multiply(view, 4, 4, cameraPos, 4, view);
    //collisionDetectionCamera(cubeCollisionBox);
    // struct Vector3 separation = detectCollision(playerCollisionBox, cubeCollisionBox);
    //printf("Collision detected: %4.2f\n", separation.x);
    //view[13] = cameraLocation[1];
    float inv[16];
    inverseViewMatrix(view, inv);
    setGPUGlobalUniformValue(context, main_pipeline, timeOffset, &timeVal, sizeof(float));
    setGPUGlobalUniformValue(context, main_pipeline, viewOffset, &inv, sizeof(view));
    setGPUGlobalUniformValue(context, main_pipeline, projectionOffset, &projection, sizeof(projection));

    float new_[4] = {view[12], view[13], view[14], 1.0};
    memcpy(camera_world_space, &new_, sizeof(camera_world_space));
    setGPUGlobalUniformValue(context, main_pipeline, camera_world_space_offset, &camera_world_space, sizeof(camera_world_space));

    // SET SHADOWS
    if (SHADOWS_ENABLED) {
        static int light_proj_offset = -1;
        float light_view_proj[16]; // Q: should we pass view and proj separately instead for simplicity and flexibility?
        computeDynamicLightViewProj(light_view_proj, playerLocation);
        if (light_proj_offset == -1) light_proj_offset = addGPUGlobalUniform(context, 0, light_view_proj, sizeof(light_view_proj));
        setGPUGlobalUniformValue(context, main_pipeline, light_proj_offset, light_view_proj, sizeof(light_view_proj));
    }

    // update the instances of the text
    setGPUInstanceBuffer(context, quad_mesh_id, &char_instances, MAX_CHAR_ON_SCREEN);
    
    // keep track of how long the tick took to process
    double tick_ms = p->current_time_ms() - tick_start_ms;

    struct draw_result result = drawGPUFrame(context, p, OFFSET_X, OFFSET_Y, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, 0, 0);
    double cpu_ms = result.cpu_ms;

    // todo: pass postprocessing settings etc. as parameter -> no global, can switch instantly
    // draw cubemap around eye, and save to disk
    if (0) {
        float eye[3] = {0.,1.,0.};
        float cubemapViews[6][16];
        float cubemapProj[16];
        generateCubemapViews(eye, cubemapViews);
        generateCubemapProjection(0.01f, 2000.0f, cubemapProj);
        setGPUGlobalUniformValue(context, main_pipeline, projectionOffset, &cubemapProj, sizeof(projection));
        for (int i = 0; i < 6; i++) {
            setGPUGlobalUniformValue(context, main_pipeline, viewOffset, &cubemapViews[i], sizeof(view));
            char filename[64]; char *cube_faces[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
            snprintf(filename, sizeof(filename), "data/textures/cube/cube_face_%s.png", cube_faces[i]);    
            drawGPUFrame(context, p, 0, 0, 128, 128, 1, filename);
        }
    }

    // todo: create a central place for things that need to happen to initialize every frame iteration correctly
    // reset the debug text on screen
    {
        screen_chars_index = 0;
        current_screen_char = 0; // todo: replace with function that resets instead of spaghetti
        memset(char_instances, 0, sizeof(char_instances));
        setGPUInstanceBuffer(context, quad_mesh_id, &char_instances, screen_chars_index);
    }

    double total_tick_time = 0.0;

    // print the time spent sleeping in anticipation of frame acquiring time
    PRINT_MS("Anticipate waiting for frame: ", time_spent_anticipating_vsync, anticipate_vsync_time);
    total_tick_time += time_spent_anticipating_vsync;

    // print the gpu timing on screen
    PRINT_MS("Waiting for GPU to finish: ", gpu_ms, gpu_wait_time);
    total_tick_time += gpu_ms;

    // print the cpu tick timing
    PRINT_MS("CPU tick time: ", tick_ms, tick_cpu_time);
    total_tick_time += tick_ms;

    // print the time we waited to get access to the surface
    PRINT_MS("Acquire surface time: ", result.get_surface_ms, surface_time);
    total_tick_time += result.get_surface_ms;

    // print the total cpu draw call timing
    PRINT_MS("CPU draw time: ", result.cpu_ms, cpu_draw_call_time);
    total_tick_time += result.cpu_ms;

    PRINT_MS("-> setup time: ", result.setup_ms, setup_time);
    PRINT_MS("-> write buffers time: ", result.write_buffer_ms, buffer_write_time);
    PRINT_MS("-> shadowmap time: ", result.shadowmap_ms, shadowmap_time);
    PRINT_MS("-> main pass time: ", result.main_pass_ms, mainpass_time);
    PRINT_MS("-> submit time: ", result.submit_ms, submit_time);

    // print the time spent waiting to be able to present last frame
    PRINT_MS("Wait for present time: ", result.present_wait_ms, present_time);
    total_tick_time += result.present_wait_ms;

    // print the total time we spent on this tick
    PRINT_MS("Total tick time: ", total_tick_time, tick_time);
    PRINT_MS("Delta time: ", delta, delta_time);

    vsync_delay = result.present_wait_ms + result.get_surface_ms;

    return 0;
}