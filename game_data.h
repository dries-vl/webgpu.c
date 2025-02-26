#ifndef GAME_DATA_H_
#define GAME_DATA_H_
// todo: avoid this header file, just have it in one of the C files, ideally game.c (it compiles like that, but ide errors annoying) 

#define MAX_MATERIALS             16
#define MAX_MESHES                128
#define UNIFORM_BUFFER_CAPACITY   1024  // bytes per pipeline’s uniform buffer

struct Vertex { // 32 bytes
    float position[3]; // 12 bytes
    unsigned char normal[4];   // 4 bytes
    unsigned char tangent[4];   // 4 bytes
    unsigned short uv[2];       // 4 bytes
    unsigned char bone_weights[4]; // 4 bytes
    unsigned char bone_indices[4]; // 4 bytes
};
struct Instance { // 12 bytes
    float position[3];
};
struct Vertex2D { // 8 bytes
    float position[2];
    float uv[2];
};
struct CharInstance { // 8 bytes
    int i_pos;
    int i_char;
};

struct MappedMemory {
    void *data;     // Base pointer to mapped file data
    void *mapping;  // Opaque handle for the mapping (ex. Windows HANDLE)
};

// todo: for createTexture: don't pass struct, just data ptr, and free mapping yourself
// todo: maybe use trick from SBarrett, to avoid keeping counts of arrays everywhere
enum MaterialFlags {
    USE_ALPHA               = 1 << 0,//1
    USE_TEXTURES            = 1 << 1,//2
    USE_UNIFORMS            = 1 << 2,//4
    UPDATE_INSTANCES        = 1 << 3,//8
    STANDARD_VERTEX_LAYOUT  = 1 << 4,//16
    HUD_VERTEX_LAYOUT       = 1 << 5,//32
    STANDARD_TEXTURE_LAYOUT = 1 << 6 //64
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