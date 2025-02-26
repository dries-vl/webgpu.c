#include "game_data.h"

#define WINDOW_WIDTH 1920 // todo: fps degrades massively when at higher resolution, even with barely any fragment shader logic
#define WINDOW_HEIGHT 1080 // todo: make this global variable that can be modified

// Basic material
enum MaterialFlags basic_material = 
    USE_TEXTURES | USE_UNIFORMS | UPDATE_INSTANCES | STANDARD_VERTEX_LAYOUT | STANDARD_TEXTURE_LAYOUT;
enum MaterialFlags hud_material = 
    USE_ALPHA | USE_TEXTURES | USE_UNIFORMS | UPDATE_INSTANCES | HUD_VERTEX_LAYOUT | STANDARD_TEXTURE_LAYOUT;

// Ground mesh data
static struct Vertex ground_verts[4] = {
    {.position = {-10000.0, -15.0, 10000.0}, .normal = {0}, .uv = {-1., 1.}},
    {.position = {10000.0, -15.0, 10000.0}, .normal = {0}, .uv = {1., 1.}},
    {.position = {-10000.0, -15.0, -10000.0}, .normal = {0}, .uv = {-1., -1.}},
    {.position = {10000.0, -15.0, -10000.0}, .normal = {0}, .uv = {1., -1.}}
};

static unsigned int ground_indices[6] = {0, 2, 1, 2, 3, 1};

static struct Instance ground_instance = {.position = {0., 0., 0.}};

// HUD mesh data
static struct Vertex2D quad_vertices[4] = {
    {.position = {0.0, 1.0}, .uv = {0.0, 1.0}},
    {.position = {1.0, 1.0}, .uv = {1.0, 1.0}},
    {.position = {0.0, 0.0}, .uv = {0.0, 0.0}},
    {.position = {1.0, 0.0}, .uv = {1.0, 0.0}}
};

static unsigned int quad_indices[6] = {0, 2, 1, 2, 3, 1};

// HUD mesh
#define CHAR_WIDTH_SCREEN (48 * 2) // todo: this is duplicated in shader code...
#define CHAR_HEIGHT_SCREEN (24 * 2)
#define MAX_CHAR_ON_SCREEN (48 * 24 * 2)
struct CharInstance char_instances[MAX_CHAR_ON_SCREEN] = {{0,'H'},{1,'e'},{2,'l'},{3,'l'},{4,'o'}};

