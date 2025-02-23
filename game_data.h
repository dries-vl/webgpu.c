#ifndef GAME_DATA_H_
#define GAME_DATA_H_
// todo: avoid this header file, just have it in one of the C files, ideally game.c (it compiles like that, but ide errors annoying) 

#define MAX_MATERIALS             16
#define MAX_MESHES                128
#define UNIFORM_BUFFER_CAPACITY   1024  // bytes per pipeline’s uniform buffer

struct Vertex {
    float position[3]; // 12 bytes
    float normal[3];   // 12 bytes
    float uv[2];       // 8 bytes
    char color[3];     // 3 bytes
    char pad;          // 1 byte of padding so the total size is 36 bytes
};
struct Instance {
    float position[3];
};
struct vert2 {
    float position[2];
    float uv[2];
};
struct char_instance {
    int i_pos;
    int i_char;
};
// enum that refers to the index in the list of predefined vertex layouts
enum VertexLayout {
    STANDARD_LAYOUT,
    HUD_LAYOUT
};

struct TextureLayout {
    int layout_index;
    int max_textures;
};
#define STANDARD_MAX_TEXTURES 4
static const struct TextureLayout TEXTURE_LAYOUT_STANDARD = {.layout_index=0, .max_textures=STANDARD_MAX_TEXTURES};

struct MappedMemory {
    void *data;     // Base pointer to mapped file data
    void *mapping;  // Opaque handle for the mapping (ex. Windows HANDLE)
};

struct Material {
    int used;
    int hash; // unique hash based on url/name of material
    int index; // index in material array
    int use_alpha;
    int use_textures;
    int use_uniforms;
    int update_instances;
    enum VertexLayout vertex_layout;
    struct TextureLayout *texture_layout; // ptr to static struct
    const char* shader; // todo: pre-compile
    unsigned char uniformData[UNIFORM_BUFFER_CAPACITY];
    int uniformCurrentOffset;
};

struct Mesh {
    struct Material *material;
    unsigned int *indices;
    void *vertices;
    void *instances;
    unsigned char texture_ids[16]; // assume there will never be more than 16 textures on a mesh
    unsigned int indexCount;
    unsigned int vertexCount;
    unsigned int instanceCount;
    struct MappedMemory mm;
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
// todo: materials are still in C for now
// todo; script that converts those text files to binary data that we load in at runtime
struct LoadMesh { // todo: isn't it wasteful to put this in static memory when it's only used for loading in?
    struct Material *material;
    const char* model;
    const char** textures; // 0 as last string to stop the loop
};

#endif