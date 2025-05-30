#ifndef GAME_DATA_H_
#define GAME_DATA_H_

// todo: either pass as param, or put in webgpu.c as global, but not in header file
static const int FORCE_GPU_CHOICE = 1;
static const int DISCRETE_GPU = 0; // 0 for forcing integrated, 1 for forcing discrete
static const int MSAA_ENABLED = 1;
static const int SHADOWS_ENABLED = 1;
static const int POST_PROCESSING_ENABLED = 0;

#define TEXTURE_SIZE 512
#define ENV_TEXTURE_SIZE 1024
#define GLOBAL_UNIFORM_CAPACITY 1024  // bytes per pipeline uniform buffer
#define UNIFORM_BUFFER_MAX_SIZE 65536 // this cannot be bigger than 65536 bytes
#define MAX_PIPELINES 2 // todo: remove, only one pipeline
#define MAX_MESHES 1024
#define MAX_MATERIALS (UNIFORM_BUFFER_MAX_SIZE / sizeof(struct MaterialUniforms)) // 256 bytes x 256 materials limit -> reuse material for different mesh by using atlas for textures + instance atlas uv
#define MAX_BONES 64
#define MAX_FRAMES 32
#define SKELETON_SIZE (MAX_BONES * 64) // 4096 bytes (16 byte rgba32 -> 256 pixels)
#define ANIMATION_SIZE (SKELETON_SIZE * MAX_FRAMES) // 131k bytes (16 byte rgba32 -> 8192 pixels)
#define ANIMATION_TEXTURE_WIDTH (ANIMATION_SIZE / 16)

struct MaterialUniforms { // 256 bytes (is ideal offset for uniforms)
    // 16+ byte elements must align to 16 byte offsets (!) 
    unsigned int shader; // 0-4
    float reflective; // 4-8
    unsigned int animated; // 8-12
    unsigned char padding[244]; // 12-256
};
struct GlobalUniforms { // 1024 bytes
    // 16+ byte elements must align to 16 byte offsets (!)
    float brightness; // 0-4
    float time; // 0-8
    unsigned int shadows; // 8-12
    float pad_camera_world; // 12-16
    float camera_world_space[4]; // 16-32
    float view[16]; // 32-96
    float projection[16]; // 96-160
    float light_view_proj[16]; // 160-224
    unsigned char padding[800]; // 224-1024
};

enum MeshFlags {
    MESH_ANIMATED = 1 << 0,
    MESH_CAST_SHADOWS = 1 << 1
};

struct draw_result {
    int surface_not_available;
    double present_wait_ms;
    double get_surface_ms;
    double write_buffer_ms;
    double setup_ms;
    double shadowmap_ms;
    double main_pass_ms;
    double submit_ms;
    double cpu_ms;
};

// todo: add DX12 which allows for more lightweight setup on windows + VRS for high resolution screens
// todo: add functions to remove meshes from the scene, and automatically remove pipelines/pipelines that have no meshes anymore (?)
/* GRAPHICS LAYER API */
#ifdef __EMSCRIPTEN__
void *createGPUContext(void (*callback)(), int width, int height, int viewport_width, int viewport_height);
#else
void *createGPUContext(void *hInstance, void *hwnd, int width, int height, int viewport_width, int viewport_height);
#endif
int   create_main_pipeline(void *context, const char *shader);
void  create_shadow_pipeline(void *context);
void  create_postprocessing_pipeline(void *context, int viewport_width, int viewport_height);
int   set_env_cube(void *context_ptr, void *data[6], int face_size);
int   createGPUMesh(void *context, int material_id, enum MeshFlags flags, void *v, int vc, void *i, int ic, void *ii, int iic);
void  setGPUMeshBoneData(void *context_ptr, int mesh_id, float *bf[MAX_BONES][16], int bc, int fc);
int   createGPUTexture(void *context, int mesh_id, void *data, int w, int h);
void  setGPUInstanceBuffer(void *context, int mesh_id, void* ii, int iic);
struct draw_result drawGPUFrame(void *context, struct Platform *p, int offset_x, int offset_y, int viewport_width, int viewport_height, int save_to_disk, char *filename,struct GlobalUniforms *global_uniforms, struct MaterialUniforms material_uniforms[MAX_MATERIALS]);
double block_on_gpu_queue(void *context, struct Platform *p);

struct Vertex { // 48 bytes
    unsigned int data[4]; // 16 bytes u32 // *info* raw data
    float position[3]; // 12 bytes f32
    char normal[4];   // 4 bytes n8
    char tangent[4];   // 4 bytes n8
    unsigned short uv[2];       // 4 bytes n16
    unsigned char bone_weights[4]; // 4 bytes n8
    unsigned char bone_indices[4]; // 4 bytes u8 // *info* max 256 bones
};
struct Instance { // 96 bytes
    float transform[16]; // 64 bytes f32 // *info* translation + rotation + scale
    unsigned int data[3]; // 12 bytes u32 // *info* texture + shader + material
    unsigned short norms[4]; // 8 bytes n16 // *info* (?) + (?) + (?) + (?)
    unsigned int animation; // 4 bytes u32
    float frame; // 4 bytes f32
    unsigned short atlas_uv[2]; // 4 bytes n16 // *info* the texture index is a per-mesh uniform, and this picks within that texture for atlases
};








// todo: one big static array of memory
// todo: keep track of how much of it is used, and by what
// todo: use different blocks for different stuff, to be able to see what is using the memory

// todo: static predefined structs/arrays; is it actually faster than just doing init in main?
// todo: is there a faster way to store the struct/array in the executable without needing to init it ever?

// todo: always use memory mapping for loading files in, make a function for it to reuse everywhere

// todo: already prepare for multiplayer where server hosts game loop in ticks
// todo: eg. we would have a permanent socket opened to send commands to server, which processes each tick what was received in order
// todo: structure the code according to this similar to how platform/gpu layer is separate from game code
// todo: presumably the server sends back updates to the game; eg. draw commands, or just an update delta to the game-state (?)
// option 1: fully local simulation execution
    // -> instant latency, but need to manage rollbacks when checksum is not identical
    // one player is host and determines the correct checksum
    // host player computes checksum for every tick (=changes local + later changes from server events for that same tick)
// option 2: fully server simulation execution
    // -> latency to wait for response for a tick, but can simply send deltas that are the same everywhere
    // can mask latency by doing initial prediction eg. for movement, assuming the delta from server won't differ much
    // -> server delta arrives: substract predicted delta for that tick from the server delta
    // does this always lead to the correct state? (assuming deterministic simulation)
// use this same prediction code in local singleplayer to reduce latency
// eg. if we press a button, the event goes to the list for the next tick, it is processed next tick
// we still have to wait for the current tick to end, so say 10-50ms delay
// the tick then processes and updates the game state with a delta
// we can avoid this latency by immediately predicting a delta based on one instant event
// Q: how to predict? run the simulation every frame? defeats the point of using ticks...
// A: do only for latency-critical things that are cheap to predict: movement, ...

/*
    do all the logic client-side, use the server as broker, execute locally produced events immediately,
     send to server, receive list of events for a tick from the server, execute what was not local in that tick,
      and every ten ticks create a checksum of the local game state, and compare with the checksum from the server. 
      if it does not match, roll back local game state to last correct version (eg. two ticks ago) and run the correct order 
      of events from that point to get to the correct state (with interpolation)
*/

// todo: gpu backend: no global variables; maybe use a single object with all the functions in it?
// todo: 

// todo: much more high level text files with mesh + textures + instances + scene
// todo: pipelines are still in C for now
// todo; script that converts those text files to binary data that we load in at runtime

#endif