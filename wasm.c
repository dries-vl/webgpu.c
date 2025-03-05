// *info* wasm.c is needed in the browser version in between platform and presentation layers to do two things:
// *... 1: create the main loop, which the platform layer does on native (but we don't want it in JS)
// *... 2: provide functions that are automatically available on native but not in wasm
// *...         -> ex. math.h (cos, tan, ...) stdlib.h (malloc, ...) string.h (memcpy, memset, ...)

// Define PI and provide simple sine/cosine approximations.
#define PI 3.14159265f

// Declare external functions (provided by our JS glue).
// todo: wouldn't these already be defined in game_data.h via present.c, so we can avoid the duplication (?)
extern void *createGPUContext(int width, int height);
extern int   createGPUPipeline(void *context, const char *shader);
extern int   createGPUMesh(void *context, int pipeline_id, void *v, int vc, void *i, int ic, void *ii, int iic);
extern int   createGPUTexture(void *context, int mesh_id, void *data, int w, int h);
extern int   addGPUGlobalUniform(void *context, int pipeline_id, const void* data, int data_size);
extern void  setGPUGlobalUniformValue(void *context, int pipeline_id, int offset, const void* data, int dataSize);
extern int   addGPUMaterialUniform(void *context, int material_id, const void* data, int data_size);
extern void  setGPUMaterialUniformValue(void *context, int material_id, int offset, const void* data, int dataSize);
extern void  setGPUInstanceBuffer(void *context, int mesh_id, void* ii, int iic);
extern float drawGPUFrame(void *context);

void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

void *memcpy(void *dest, const void *src, unsigned long n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

// Simple sine approximation (using a Taylor series, for small angles)
static float sin_approx(float x) {
    // Normalize x to [-PI, PI]
    while (x > PI)  x -= 2 * PI;
    while (x < -PI) x += 2 * PI;
    float x2 = x * x;
    float x3 = x2 * x;
    float x5 = x3 * x2;
    return x - (x3 / 6.0f) + (x5 / 120.0f);
}

// Simple cosine approximation (using a Taylor series)
static float cos_approx(float x) {
    // Normalize x to [-PI, PI]
    while (x > PI)  x -= 2 * PI;
    while (x < -PI) x += 2 * PI;
    float x2 = x * x;
    float x4 = x2 * x2;
    return 1.0f - (x2 / 2.0f) + (x4 / 24.0f);
}

// Define a packed vertex structure exactly 48 bytes in size.
// Layout:
//   - Attribute 0: 4 x unsigned int (16 bytes, dummy data)
//   - Attribute 1: 3 x float (12 bytes, vertex position)
//   - The remaining 20 bytes are dummy (unused) data.
#pragma pack(push, 1)
typedef struct {
    unsigned int data[4];    // attribute 0 (dummy)
    float position[3];       // attribute 1 (vertex position)
    unsigned char attr2[4];  // attribute 2 (unused)
    unsigned char attr3[4];  // attribute 3 (unused)
    unsigned short attr4[2]; // attribute 4 (unused)
    unsigned char attr5[4];  // attribute 5 (unused)
    unsigned char attr6[4];  // attribute 6 (unused)
} Vertex;
#pragma pack(pop)

// WGSL shader code as a string literal.
// The vertex shader reads three inputs: a dummy (location 0),
// the vertex position (location 1), and a dummy instance value (location 7).
const char shaderCode[] =
"@vertex\n"
"fn vs_main(\n"
"    @location(0) data: vec4<u32>,\n"
"    @location(1) position: vec3<f32>,\n"
"    @location(7) dummy: vec4<f32>\n"
") -> @builtin(position) vec4<f32> {\n"
"    return vec4<f32>(position, 1.0);\n"
"}\n"
"@fragment\n"
"fn fs_main() -> @location(0) vec4<f32> {\n"
"    return vec4<f32>(1.0, 0.0, 0.0, 1.0);\n"
"}\n";

int main() {
    // Create a GPU context with a 640x480 canvas.
    void *ctx = createGPUContext(640, 480);
    if (!ctx)
        return -1;

    // Create a render pipeline using our shader.
    int pipelineId = createGPUPipeline(ctx, shaderCode);
    if (pipelineId < 0)
        return -1;

    // --- Build vertex data for a star shape ---
    // We create a triangle fan with 11 vertices:
    //   - Vertex 0: center at (0, 0, 0)
    //   - Vertices 1..10: perimeter vertices arranged in a circle with alternating outer and inner radii.
    const int vertexCount = 11;
    Vertex vertices[vertexCount];
    int i, j;

    // Initialize center vertex (vertex 0).
    for (j = 0; j < 4; j++) {
        vertices[0].data[j] = 0;
        vertices[0].attr2[j] = 0;
        vertices[0].attr3[j] = 0;
        vertices[0].attr5[j] = 0;
        vertices[0].attr6[j] = 0;
    }
    vertices[0].position[0] = 0.0f;
    vertices[0].position[1] = 0.0f;
    vertices[0].position[2] = 0.0f;
    vertices[0].attr4[0] = 0;
    vertices[0].attr4[1] = 0;

    float outer = 0.8f;
    float inner = 0.4f;
    // Create 10 perimeter vertices (indices 1..10) with alternating radii.
    for (i = 0; i < 10; i++) {
        int idx = i + 1;
        float angle = (2.0f * PI * i) / 10.0f;
        float r = (i & 1) ? inner : outer;  // use inner radius for odd i
        for (j = 0; j < 4; j++) {
            vertices[idx].data[j] = 0;
            vertices[idx].attr2[j] = 0;
            vertices[idx].attr3[j] = 0;
            vertices[idx].attr5[j] = 0;
            vertices[idx].attr6[j] = 0;
        }
        vertices[idx].position[0] = r * cos_approx(angle);
        vertices[idx].position[1] = r * sin_approx(angle);
        vertices[idx].position[2] = 0.0f;
        vertices[idx].attr4[0] = 0;
        vertices[idx].attr4[1] = 0;
    }

    // --- Build index data for a triangle fan with reversed winding ---
    // Instead of (0, i+1, i+2) we use (0, i+2, i+1) so that the triangles are specified in clockwise order.
    const int indexCount = 10 * 3;
    unsigned int indices[indexCount];
    for (i = 0; i < 10; i++) {
        indices[i*3 + 0] = 0;                       // center vertex
        indices[i*3 + 1] = (i == 9) ? 1 : (i + 2);    // next perimeter vertex (wrap around)
        indices[i*3 + 2] = i + 1;                     // current perimeter vertex
    }

    // --- Build dummy instance data ---
    // The instance buffer expects 96 bytes per instance.
    const int instanceCount = 1;
    unsigned char instanceData[96];
    for (i = 0; i < 96; i++) {
        instanceData[i] = 0;
    }

    // Create the mesh using our vertex, index, and instance data.
    int meshId = createGPUMesh(ctx, pipelineId, vertices, vertexCount, indices, indexCount, instanceData, instanceCount);
    if (meshId < 0)
        return -1;

    // Render one frame.
    drawGPUFrame(ctx);

    return 0;
}
