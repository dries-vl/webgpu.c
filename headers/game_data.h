
#include <stdint.h>
typedef uint8_t u8;

#define MAX_MATERIALS             16
#define MAX_MESHES                128
#define UNIFORM_BUFFER_CAPACITY   1024
struct Vertex {
    float position[3]; // 12 bytes
    float normal[3];   // 12 bytes
    float uv[2];       // 8 bytes
    u8 color[3];     // 3 bytes
    u8 pad;          // 1 byte
};
