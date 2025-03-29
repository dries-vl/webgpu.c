#ifndef GAME_DATA_H_
#define GAME_DATA_H_

extern const int MSAA_ENABLED = 1;
extern const int SHADOWS_ENABLED = 1;
extern const int POST_PROCESSING_ENABLED = 0;

#define MAX_PIPELINES             2 // todo: remove, only one pipeline
#define MAX_MESHES                128
#define MAX_MATERIALS          256 // todo: MAX_MATERIALS that matches with the 256 bytes x 256 materials limit -> can reuse material for different mesh by using atlas for textures + instance atlas uv
#define MATERIAL_UNIFORM_CAPACITY     256  // bytes per mesh in mesh uniform buffer (dynamic offset blocks in uniform buffer cannot be smaller than 256 bytes)
#define GLOBAL_UNIFORM_CAPACITY   1024  // bytes per pipeline uniform buffer
#define MATERIALS_UNIFORM_BUFFER_TOTAL_SIZE (MAX_MATERIALS * MATERIAL_UNIFORM_CAPACITY) // this cannot be bigger than 65536 bytes
#define MAX_BONES 64
#define BONE_FRAME_SIZE (MAX_BONES * 16)

enum MeshFlags {
    MESH_ANIMATED = 1 << 0,
    MESH_CAST_SHADOWS = 1 << 1
};

struct draw_result {
    int surface_not_available;
    double cpu_ms;
};

// todo: add DX12 which allows for more lightweight setup on windows + VRS for high resolution screens
// todo: add functions to remove meshes from the scene, and automatically remove pipelines/pipelines that have no meshes anymore (?)
/* GRAPHICS LAYER API */
// todo : platform provides these functions to presentation layer via a struct (then they don't need to be compiled together)
extern void *createGPUContext(void *hInstance, void *hwnd, int width, int height, int viewport_width, int viewport_height);
extern int   createGPUPipeline(void *context, const char *shader);
extern void  create_shadow_pipeline(void *context);
extern void  create_postprocessing_pipeline(void *context, int viewport_width, int viewport_height);
extern int   load_cube_map(void *context, void *data[6], int face_size);
extern int   createGPUMesh(void *context, int material_id, enum MeshFlags flags, void *v, int vc, void *i, int ic, void *ii, int iic);
extern void  setGPUMeshBoneData(void *context_ptr, int mesh_id, float *bf[MAX_BONES][16], int bc, int fc);
extern int   createGPUTexture(void *context, int mesh_id, void *data, int w, int h);
extern int   addGPUGlobalUniform(void *context, int pipeline_id, const void* data, int data_size);
extern void  setGPUGlobalUniformValue(void *context, int pipeline_id, int offset, const void* data, int dataSize);
extern int   addGPUMaterialUniform(void *context, int material_id, const void* data, int data_size);
extern void  setGPUMaterialUniformValue(void *context, int material_id, int offset, const void* data, int dataSize);
extern void  setGPUInstanceBuffer(void *context, int mesh_id, void* ii, int iic);
extern struct draw_result drawGPUFrame(void *context, struct Platform *p, int offset_x, int offset_y, int viewport_width, int viewport_height, int save_to_disk, char *filename);
extern double block_on_gpu_queue(void *context, struct Platform *p);

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
    unsigned int data[3]; // 12 bytes u32 // *info* raw data
    unsigned short norms[4]; // 8 bytes n16 // *info* raw normalized data (eg. metallic, etc.)
    unsigned int animation; // 4 bytes u32
    float animation_phase; // 4 bytes f32
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
struct LoadMesh { // todo: isn't it wasteful to put this in static memory when it's only used for loading in?
    struct Material *material;
    const char* model;
    const char** textures; // 0 as last string to stop the loop
};

#endif