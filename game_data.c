#include "game_data.h"
#include <math.h>

#define WINDOW_WIDTH 1920 // todo: fps degrades massively when at higher resolution, even with barely any fragment shader logic
#define WINDOW_HEIGHT 1080 // todo: make this global variable that can be modified

// Basic material
static struct Material basic_material = {
    .vertex_layout = STANDARD_LAYOUT,
    .shader = "data/shaders/shader.wgsl",
    .texture_layout = &TEXTURE_LAYOUT_STANDARD,
    .use_alpha = 0,
    .use_textures = 1,
    .use_uniforms = 1,
    .update_instances = 0,
    .uniformCurrentOffset = 0,
    .uniformData = {0}
};

// Ground mesh data
static struct Vertex ground_verts[4] = {
    {.position = {-10000.0, -10.0, 10000.0}, .color = {.5, .5, .5}, .normal = {0}, .uv = {-1., 1.}},
    {.position = {10000.0, -10.0, 10000.0}, .color = {.5, .5, .5}, .normal = {0}, .uv = {1., 1.}},
    {.position = {-10000.0, -10.0, -10000.0}, .color = {.5, .5, .5}, .normal = {0}, .uv = {-1., -1.}},
    {.position = {10000.0, -10.0, -10000.0}, .color = {.5, .5, .5}, .normal = {0}, .uv = {1., -1.}}
};

static unsigned int ground_indices[6] = {2, 0, 1, 3, 2, 1};

static struct Instance ground_instance = {.position = {0., 0., 0.}};

static struct Mesh ground_mesh = {
    .material = &basic_material,
    .indexCount = 6,
    .indices = ground_indices,
    .vertexCount = 4,
    .vertices = ground_verts,
    .instanceCount = 1,
    .instances = &ground_instance
};

// HUD mesh data
static struct vert2 quad_vertices[4] = {
    {.position = {0.0, 1.0}, .uv = {0.0, 1.0}},
    {.position = {1.0, 1.0}, .uv = {1.0, 1.0}},
    {.position = {0.0, 0.0}, .uv = {0.0, 0.0}},
    {.position = {1.0, 0.0}, .uv = {1.0, 0.0}}
};

static unsigned int quad_indices[6] = {2, 0, 1, 3, 2, 1};

// HUD material
static struct Material hud_material = {
    .vertex_layout = HUD_LAYOUT,
    .shader = "data/shaders/hud.wgsl",
    .texture_layout = &TEXTURE_LAYOUT_STANDARD,
    .use_alpha = 1,
    .use_textures = 1,
    .use_uniforms = 1,
    .update_instances = 1,
    .uniformCurrentOffset = 0,
    .uniformData = {0}
};

// Uniforms
static float brightness = 1.0f;
static float timeVal = 0.0f;
static float aspect_ratio = ((float) WINDOW_WIDTH / (float) WINDOW_HEIGHT);

// HUD mesh
#define CHAR_WIDTH_SCREEN (48 * 2) // todo: this is duplicated in shader code...
#define CHAR_HEIGHT_SCREEN (24 * 2)
#define MAX_CHAR_ON_SCREEN (48 * 24 * 2)
struct char_instance screen_chars[MAX_CHAR_ON_SCREEN] = {0};
static struct Mesh quad_mesh = {
    .material = &hud_material,
    .indexCount = 6,
    .indices = quad_indices,
    .vertexCount = 4,
    .vertices = quad_vertices,
    .instanceCount = MAX_CHAR_ON_SCREEN,
    .instances = {screen_chars}
};
