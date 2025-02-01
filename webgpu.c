#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "wgpu.h"

// Minimal vertex definition (must match main.c)
typedef struct {
    float position[3];
    float color[3];
} Vertex;

// Limits
#define MAX_PIPELINES             16
#define MAX_MESHES                128
#define MAX_UNIFORMS              32
#define MAX_UNIFORMS_PER_PIPELINE 4

// -----------------------------------------------------------------------------
// Structures
// -----------------------------------------------------------------------------

// A pipeline is now “dumb” – it just holds its render pipeline.
typedef struct {
    WGPURenderPipeline pipeline;
    bool               used;
} PipelineData;

// Each mesh stores its vertex buffer and which pipeline it uses.
typedef struct {
    WGPUBuffer vertexBuffer;
    int        vertexCount;
    int        pipelineID;
    bool       used;
} MeshData;

// Each uniform now owns its uniform buffer and bind group. (We allocate 16 bytes
// per uniform to satisfy alignment requirements even though we only store one float.)
typedef struct {
    float       value;
    int         pipelineID; // Associated pipeline id
    bool        used;
    WGPUBuffer  uniformBuffer;
    WGPUBindGroup bindGroup;
} UniformData;

// Global WebGPU context
typedef struct {
    WGPUInstance             instance;
    WGPUSurface              surface;
    WGPUAdapter              adapter;
    WGPUDevice               device;
    WGPUQueue                queue;
    WGPUSurfaceConfiguration config;
    bool                     initialized;
    PipelineData             pipelines[MAX_PIPELINES];
    MeshData                 meshes[MAX_MESHES];
    UniformData              uniforms[MAX_UNIFORMS];
} WebGPUContext;

static WebGPUContext g_wgpu = {0};

// -----------------------------------------------------------------------------
// Global uniform bind group layout (shared by all uniform bind groups)
// -----------------------------------------------------------------------------
static WGPUBindGroupLayout s_uniformBindGroupLayout = NULL;
// Default values for unused bindgroups
static WGPUBuffer      s_defaultUniformBuffer = NULL;
static WGPUBindGroup   s_defaultUniformBindGroup = NULL;

// Forward declarations
static void handle_request_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message, void* userdata);
static void handle_request_device(WGPURequestDeviceStatus status, WGPUDevice device, const char* message, void* userdata);
static WGPUShaderModule loadWGSL(WGPUDevice device, const char* filePath);

// Current frame objects (global for simplicity)
static WGPUSurfaceTexture    g_currentSurfaceTexture;
static WGPUTextureView       g_currentView;
static WGPUCommandEncoder    g_currentEncoder;
static WGPURenderPassEncoder g_currentPass;

// -----------------------------------------------------------------------------
// wgpuInit
// -----------------------------------------------------------------------------
void wgpuInit(HINSTANCE hInstance, HWND hwnd, int width, int height)
{
    memset(&g_wgpu, 0, sizeof(g_wgpu));
    // All uniforms are zeroed (used==false) via memset.

    // --- Extras: Specify the backend as OpenGL ---
    WGPUInstanceExtras extras;
    memset(&extras, 0, sizeof(extras));
    extras.chain.sType = WGPUSType_InstanceExtras;
    extras.backends   = WGPUInstanceBackend_GL;
    extras.flags      = 0;
    extras.dx12ShaderCompiler = WGPUDx12Compiler_Undefined;
    extras.gles3MinorVersion  = WGPUGles3MinorVersion_Automatic;
    extras.dxilPath = NULL;
    extras.dxcPath  = NULL;

    WGPUInstanceDescriptor instDesc;
    memset(&instDesc, 0, sizeof(instDesc));
    instDesc.nextInChain = (const WGPUChainedStruct*)&extras;
    g_wgpu.instance = wgpuCreateInstance(&instDesc);
    assert(g_wgpu.instance);
    // --- End extras setup ---

    // Create a surface from the Win32 HWND.
    WGPUSurfaceDescriptorFromWindowsHWND chained_desc;
    memset(&chained_desc, 0, sizeof(chained_desc));
    chained_desc.chain.sType = WGPUSType_SurfaceDescriptorFromWindowsHWND;
    chained_desc.hwnd        = hwnd;
    chained_desc.hinstance   = hInstance;

    WGPUSurfaceDescriptor surface_desc;
    memset(&surface_desc, 0, sizeof(surface_desc));
    surface_desc.nextInChain = (const WGPUChainedStruct*)&chained_desc;
    g_wgpu.surface = wgpuInstanceCreateSurface(g_wgpu.instance, &surface_desc);
    assert(g_wgpu.surface);

    // Request adapter and device.
    WGPURequestAdapterOptions adapter_opts;
    memset(&adapter_opts, 0, sizeof(adapter_opts));
    adapter_opts.compatibleSurface = g_wgpu.surface;
    wgpuInstanceRequestAdapter(g_wgpu.instance, &adapter_opts, handle_request_adapter, NULL);
    assert(g_wgpu.adapter);

    wgpuAdapterRequestDevice(g_wgpu.adapter, NULL, handle_request_device, NULL);
    assert(g_wgpu.device);

    g_wgpu.queue = wgpuDeviceGetQueue(g_wgpu.device);
    assert(g_wgpu.queue);

    // Create the global uniform bind group layout (one uniform per bind group).
    if (!s_uniformBindGroupLayout) {
        WGPUBindGroupLayoutEntry entry;
        memset(&entry, 0, sizeof(entry));
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        entry.buffer.type = WGPUBufferBindingType_Uniform;
        entry.buffer.minBindingSize = 16; // 16 bytes (for one float padded to 16 bytes)
        
        WGPUBindGroupLayoutDescriptor bglDesc;
        memset(&bglDesc, 0, sizeof(bglDesc));
        bglDesc.entryCount = 1;
        bglDesc.entries = &entry;
        s_uniformBindGroupLayout = wgpuDeviceCreateBindGroupLayout(g_wgpu.device, &bglDesc);
    }

    { // Set the default values for all the possible bindgroups
        WGPUBufferDescriptor bufDesc;
        memset(&bufDesc, 0, sizeof(bufDesc));
        bufDesc.size  = 16; // 16 bytes for one float padded to 16 bytes.
        bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        s_defaultUniformBuffer = wgpuDeviceCreateBuffer(g_wgpu.device, &bufDesc);
        assert(s_defaultUniformBuffer);

        float zeros[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        wgpuQueueWriteBuffer(g_wgpu.queue, s_defaultUniformBuffer, 0, zeros, sizeof(zeros));

        WGPUBindGroupEntry entry;
        memset(&entry, 0, sizeof(entry));
        entry.binding = 0;
        entry.buffer  = s_defaultUniformBuffer;
        entry.offset  = 0;
        entry.size    = 16;

        WGPUBindGroupDescriptor bgDesc;
        memset(&bgDesc, 0, sizeof(bgDesc));
        bgDesc.layout     = s_uniformBindGroupLayout;
        bgDesc.entryCount = 1;
        bgDesc.entries    = &entry;

        s_defaultUniformBindGroup = wgpuDeviceCreateBindGroup(g_wgpu.device, &bgDesc);
        assert(s_defaultUniformBindGroup);
    }

    // Query surface capabilities and choose a texture format.
    WGPUSurfaceCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    wgpuSurfaceGetCapabilities(g_wgpu.surface, g_wgpu.adapter, &caps);
    WGPUTextureFormat chosenFormat = WGPUTextureFormat_BGRA8Unorm;
    if (caps.formatCount > 0) {
        chosenFormat = caps.formats[0];
    }

    // Configure the surface.
    memset(&g_wgpu.config, 0, sizeof(g_wgpu.config));
    g_wgpu.config.device      = g_wgpu.device;
    g_wgpu.config.format      = chosenFormat;
    g_wgpu.config.width       = width;
    g_wgpu.config.height      = height;
    g_wgpu.config.usage       = WGPUTextureUsage_RenderAttachment;
    g_wgpu.config.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(g_wgpu.surface, &g_wgpu.config);

    g_wgpu.initialized = true;
    printf("[webgpu.c] wgpuInit done.\n");
}

// -----------------------------------------------------------------------------
// wgpuShutdown
// -----------------------------------------------------------------------------
void wgpuShutdown()
{
    if (!g_wgpu.initialized) return;

    // Release meshes.
    for (int i = 0; i < MAX_MESHES; i++) {
        if (g_wgpu.meshes[i].used) {
            wgpuBufferRelease(g_wgpu.meshes[i].vertexBuffer);
            g_wgpu.meshes[i].used = false;
        }
    }
    // Release pipelines.
    for (int i = 0; i < MAX_PIPELINES; i++) {
        if (g_wgpu.pipelines[i].used) {
            wgpuRenderPipelineRelease(g_wgpu.pipelines[i].pipeline);
            g_wgpu.pipelines[i].used = false;
        }
    }
    // Release uniforms.
    for (int i = 0; i < MAX_UNIFORMS; i++) {
        if (g_wgpu.uniforms[i].used) {
            if (g_wgpu.uniforms[i].uniformBuffer) {
                wgpuBufferRelease(g_wgpu.uniforms[i].uniformBuffer);
            }
            if (g_wgpu.uniforms[i].bindGroup) {
                wgpuBindGroupRelease(g_wgpu.uniforms[i].bindGroup);
            }
            g_wgpu.uniforms[i].used = false;
        }
    }
    // Release the global uniform bind group layout.
    if (s_uniformBindGroupLayout) {
        wgpuBindGroupLayoutRelease(s_uniformBindGroupLayout);
        s_uniformBindGroupLayout = NULL;
    }

    if (g_wgpu.queue)   { wgpuQueueRelease(g_wgpu.queue);   g_wgpu.queue   = NULL; }
    if (g_wgpu.device)  { wgpuDeviceRelease(g_wgpu.device); g_wgpu.device  = NULL; }
    if (g_wgpu.adapter) { wgpuAdapterRelease(g_wgpu.adapter); g_wgpu.adapter = NULL; }
    if (g_wgpu.surface) { wgpuSurfaceRelease(g_wgpu.surface); g_wgpu.surface = NULL; }
    if (g_wgpu.instance){ wgpuInstanceRelease(g_wgpu.instance); g_wgpu.instance = NULL; }

    g_wgpu.initialized = false;
    printf("[webgpu.c] wgpuShutdown done.\n");
}

// -----------------------------------------------------------------------------
// wgpuCreatePipeline
// -----------------------------------------------------------------------------
int wgpuCreatePipeline(const char* shaderPath)
{
    if (!g_wgpu.initialized) {
        fprintf(stderr, "[webgpu.c] wgpuCreatePipeline called before init!\n");
        return -1;
    }
    int pipelineID = -1;
    for (int i = 0; i < MAX_PIPELINES; i++) {
        if (!g_wgpu.pipelines[i].used) {
            pipelineID = i;
            g_wgpu.pipelines[i].used = true;
            break;
        }
    }
    if (pipelineID < 0) {
        fprintf(stderr, "[webgpu.c] No more pipeline slots!\n");
        return -1;
    }

    WGPUShaderModule shaderModule = loadWGSL(g_wgpu.device, shaderPath);
    if (!shaderModule) {
        fprintf(stderr, "[webgpu.c] Failed to load shader: %s\n", shaderPath);
        g_wgpu.pipelines[pipelineID].used = false;
        return -1;
    }

    // Create an array of uniform bind group layouts for the pipeline.
    // We support up to MAX_UNIFORMS_PER_PIPELINE uniforms.
    WGPUBindGroupLayout bgls[MAX_UNIFORMS_PER_PIPELINE];
    for (int i = 0; i < MAX_UNIFORMS_PER_PIPELINE; i++) {
        bgls[i] = s_uniformBindGroupLayout;
    }
    WGPUPipelineLayoutDescriptor layoutDesc;
    memset(&layoutDesc, 0, sizeof(layoutDesc));
    layoutDesc.bindGroupLayoutCount = MAX_UNIFORMS_PER_PIPELINE;
    layoutDesc.bindGroupLayouts = bgls;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(g_wgpu.device, &layoutDesc);
    assert(pipelineLayout);

    WGPURenderPipelineDescriptor rpDesc;
    memset(&rpDesc, 0, sizeof(rpDesc));
    rpDesc.layout = pipelineLayout;

    // Vertex stage.
    rpDesc.vertex.module = shaderModule;
    rpDesc.vertex.entryPoint = "vs_main";

    // A basic vertex buffer layout.
    WGPUVertexAttribute attributes[2];
    memset(attributes, 0, sizeof(attributes));
    attributes[0].format = WGPUVertexFormat_Float32x3;
    attributes[0].offset = 0;
    attributes[0].shaderLocation = 0;
    attributes[1].format = WGPUVertexFormat_Float32x3;
    attributes[1].offset = sizeof(float)*3;
    attributes[1].shaderLocation = 1;

    WGPUVertexBufferLayout vbl;
    memset(&vbl, 0, sizeof(vbl));
    vbl.arrayStride    = sizeof(Vertex);
    vbl.attributeCount = 2;
    vbl.attributes     = attributes;

    rpDesc.vertex.bufferCount = 1;
    rpDesc.vertex.buffers     = &vbl;

    // Fragment stage.
    WGPUFragmentState fragState;
    memset(&fragState, 0, sizeof(fragState));
    fragState.module      = shaderModule;
    fragState.entryPoint  = "fs_main";
    fragState.targetCount = 1;
    WGPUColorTargetState colorTarget;
    memset(&colorTarget, 0, sizeof(colorTarget));
    colorTarget.format    = g_wgpu.config.format;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    fragState.targets     = &colorTarget;
    rpDesc.fragment = &fragState;

    // Primitive state.
    WGPUPrimitiveState prim;
    memset(&prim, 0, sizeof(prim));
    prim.topology = WGPUPrimitiveTopology_TriangleList;
    rpDesc.primitive = prim;

    // Multisample state.
    WGPUMultisampleState ms;
    memset(&ms, 0, sizeof(ms));
    ms.count = 1;
    ms.mask  = 0xFFFFFFFF;
    rpDesc.multisample = ms;

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(g_wgpu.device, &rpDesc);
    g_wgpu.pipelines[pipelineID].pipeline = pipeline;

    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);

    printf("[webgpu.c] Created pipeline %d from shader: %s\n", pipelineID, shaderPath);
    return pipelineID;
}

// -----------------------------------------------------------------------------
// wgpuCreateMesh
// -----------------------------------------------------------------------------
int wgpuCreateMesh(int pipelineID, const Vertex *vertices, int vertexCount)
{
    if (!g_wgpu.initialized) {
        fprintf(stderr, "[webgpu.c] wgpuCreateMesh called before init!\n");
        return -1;
    }
    if (pipelineID < 0 || pipelineID >= MAX_PIPELINES || !g_wgpu.pipelines[pipelineID].used) {
        fprintf(stderr, "[webgpu.c] Invalid pipeline ID %d!\n", pipelineID);
        return -1;
    }
    int meshID = -1;
    for (int i = 0; i < MAX_MESHES; i++) {
        if (!g_wgpu.meshes[i].used) {
            meshID = i;
            g_wgpu.meshes[i].used = true;
            break;
        }
    }
    if (meshID < 0) {
        fprintf(stderr, "[webgpu.c] No more mesh slots!\n");
        return -1;
    }
    size_t dataSize = sizeof(Vertex) * vertexCount;
    WGPUBufferDescriptor bufDesc;
    memset(&bufDesc, 0, sizeof(bufDesc));
    bufDesc.size  = dataSize;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(g_wgpu.device, &bufDesc);
    assert(buffer);
    wgpuQueueWriteBuffer(g_wgpu.queue, buffer, 0, vertices, dataSize);
    g_wgpu.meshes[meshID].vertexBuffer = buffer;
    g_wgpu.meshes[meshID].vertexCount  = vertexCount;
    g_wgpu.meshes[meshID].pipelineID   = pipelineID;
    printf("[webgpu.c] Created mesh %d with %d vertices for pipeline %d\n",
           meshID, vertexCount, pipelineID);
    return meshID;
}

// -----------------------------------------------------------------------------
// wgpuAddUniform
// -----------------------------------------------------------------------------
int wgpuAddUniform(int pipelineID, float initialValue)
{
    if (!g_wgpu.initialized) {
        fprintf(stderr, "[webgpu.c] wgpuAddUniform called before init!\n");
        return -1;
    }
    if (pipelineID < 0 || pipelineID >= MAX_PIPELINES || !g_wgpu.pipelines[pipelineID].used) {
        fprintf(stderr, "[webgpu.c] Invalid pipeline ID for uniform: %d\n", pipelineID);
        return -1;
    }
    int uniformID = -1;
    for (int i = 0; i < MAX_UNIFORMS; i++) {
        if (!g_wgpu.uniforms[i].used) {
            uniformID = i;
            g_wgpu.uniforms[i].used = true;
            break;
        }
    }
    if (uniformID < 0) {
        fprintf(stderr, "[webgpu.c] No more uniform slots!\n");
        return -1;
    }
    g_wgpu.uniforms[uniformID].value = initialValue;
    g_wgpu.uniforms[uniformID].pipelineID = pipelineID;

    // Create a uniform buffer (16 bytes for one float padded to 16 bytes).
    WGPUBufferDescriptor uniformDesc;
    memset(&uniformDesc, 0, sizeof(uniformDesc));
    uniformDesc.size = 16; // todo: hardcoded (is also hardcoded 16 bytes in global descriptor for uniform bindgroup layout)
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(g_wgpu.device, &uniformDesc);
    assert(uniformBuffer);
    g_wgpu.uniforms[uniformID].uniformBuffer = uniformBuffer;

    // Write the initial uniform value.
    float data[4] = { initialValue, 0.0f, 0.0f, 0.0f };
    wgpuQueueWriteBuffer(g_wgpu.queue, uniformBuffer, 0, data, sizeof(data));

    // Create a bind group for this uniform buffer.
    WGPUBindGroupEntry bgEntry;
    memset(&bgEntry, 0, sizeof(bgEntry));
    bgEntry.binding = 0;
    bgEntry.buffer = uniformBuffer;
    bgEntry.offset = 0;
    bgEntry.size = 16;
    WGPUBindGroupDescriptor bgDesc;
    memset(&bgDesc, 0, sizeof(bgDesc));
    bgDesc.layout = s_uniformBindGroupLayout;
    bgDesc.entryCount = 1;
    bgDesc.entries = &bgEntry;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(g_wgpu.device, &bgDesc);
    assert(bindGroup);
    g_wgpu.uniforms[uniformID].bindGroup = bindGroup;

    printf("[webgpu.c] Added uniform %d with initial value %f to pipeline %d\n",
           uniformID, initialValue, pipelineID);
    return uniformID;
}

// -----------------------------------------------------------------------------
// wgpuSetUniformValue
// -----------------------------------------------------------------------------
void wgpuSetUniformValue(int uniformID, float newValue)
{
    if (!g_wgpu.initialized) {
        fprintf(stderr, "[webgpu.c] wgpuSetUniformValue called before init!\n");
        return;
    }
    if (uniformID < 0 || uniformID >= MAX_UNIFORMS || !g_wgpu.uniforms[uniformID].used) {
        fprintf(stderr, "[webgpu.c] Invalid uniform ID: %d\n", uniformID);
        return;
    }
    g_wgpu.uniforms[uniformID].value = newValue;
    printf("[webgpu.c] Uniform %d set to %f\n", uniformID, newValue);
}

// -----------------------------------------------------------------------------
// Rendering: wgpuStartFrame, wgpuDrawPipeline, wgpuEndFrame
// -----------------------------------------------------------------------------
void wgpuStartFrame()
{
    wgpuSurfaceGetCurrentTexture(g_wgpu.surface, &g_currentSurfaceTexture);
    if (g_currentSurfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success)
        return;
    g_currentView = wgpuTextureCreateView(g_currentSurfaceTexture.texture, NULL);
    WGPUCommandEncoderDescriptor encDesc;
    memset(&encDesc, 0, sizeof(encDesc));
    g_currentEncoder = wgpuDeviceCreateCommandEncoder(g_wgpu.device, &encDesc);
    WGPURenderPassColorAttachment colorAtt;
    memset(&colorAtt, 0, sizeof(colorAtt));
    colorAtt.view    = g_currentView;
    colorAtt.loadOp  = WGPULoadOp_Clear;
    colorAtt.storeOp = WGPUStoreOp_Store;
    colorAtt.clearValue = (WGPUColor){0.1, 0.2, 0.3, 1.0};
    WGPURenderPassDescriptor passDesc;
    memset(&passDesc, 0, sizeof(passDesc));
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments     = &colorAtt;
    g_currentPass = wgpuCommandEncoderBeginRenderPass(g_currentEncoder, &passDesc);
}

void wgpuDrawPipeline(int pipelineID)
{
    if (!g_currentPass) return;
    if (pipelineID < 0 || pipelineID >= MAX_PIPELINES || !g_wgpu.pipelines[pipelineID].used)
        return;

    // Set the render pipeline.
    wgpuRenderPassEncoderSetPipeline(g_currentPass, g_wgpu.pipelines[pipelineID].pipeline);

    // Bind uniforms that belong to this pipeline.
    // (We assume the pipeline layout reserves MAX_UNIFORMS_PER_PIPELINE slots.)
    int uniformSlot = 0;
    for (int i = 0; i < MAX_UNIFORMS; i++) {
        if (g_wgpu.uniforms[i].used && g_wgpu.uniforms[i].pipelineID == pipelineID) {
            // Update the uniform buffer with the latest value.
            float data[4] = { g_wgpu.uniforms[i].value, 0.0f, 0.0f, 0.0f };
            wgpuQueueWriteBuffer(g_wgpu.queue, g_wgpu.uniforms[i].uniformBuffer, 0, data, sizeof(data));
            wgpuRenderPassEncoderSetBindGroup(g_currentPass, uniformSlot, g_wgpu.uniforms[i].bindGroup, 0, NULL);
            uniformSlot++;
        }
    }

    // Bind the default bind group for any remaining slots.
    for (; uniformSlot < MAX_UNIFORMS_PER_PIPELINE; uniformSlot++) {
        wgpuRenderPassEncoderSetBindGroup(g_currentPass, uniformSlot, s_defaultUniformBindGroup, 0, NULL);
    }

    // Draw every mesh assigned to this pipeline.
    for (int i = 0; i < MAX_MESHES; i++) {
        if (g_wgpu.meshes[i].used && g_wgpu.meshes[i].pipelineID == pipelineID) {
            wgpuRenderPassEncoderSetVertexBuffer(g_currentPass, 0, g_wgpu.meshes[i].vertexBuffer, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderDraw(g_currentPass, g_wgpu.meshes[i].vertexCount, 1, 0, 0);
        }
    }
}


void wgpuEndFrame()
{
    if (!g_currentPass)
        return;
    wgpuRenderPassEncoderEnd(g_currentPass);
    wgpuRenderPassEncoderRelease(g_currentPass);
    g_currentPass = NULL;
    WGPUCommandBufferDescriptor cmdDesc;
    memset(&cmdDesc, 0, sizeof(cmdDesc));
    WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(g_currentEncoder, &cmdDesc);
    wgpuCommandEncoderRelease(g_currentEncoder);
    wgpuQueueSubmit(g_wgpu.queue, 1, &cmdBuf);
    wgpuCommandBufferRelease(cmdBuf);
    wgpuSurfacePresent(g_wgpu.surface);
    wgpuTextureViewRelease(g_currentView);
    g_currentView = NULL;
    wgpuTextureRelease(g_currentSurfaceTexture.texture);
    g_currentSurfaceTexture.texture = NULL;
}

// -----------------------------------------------------------------------------
// Internal helper functions
// -----------------------------------------------------------------------------
static WGPUShaderModule loadWGSL(WGPUDevice device, const char* filePath)
{
    FILE* fp = fopen(filePath, "rb");
    if (!fp) {
        fprintf(stderr, "[webgpu.c] Failed to open WGSL file: %s\n", filePath);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char* wgslSource = (char*)malloc((size_t)size + 1u);
    if (!wgslSource) {
        fclose(fp);
        return NULL;
    }
    fread(wgslSource, 1, (size_t)size, fp);
    wgslSource[size] = '\0';
    fclose(fp);
    WGPUShaderModuleWGSLDescriptor wgslDesc;
    memset(&wgslDesc, 0, sizeof(wgslDesc));
    wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDesc.code = wgslSource;
    WGPUShaderModuleDescriptor desc;
    memset(&desc, 0, sizeof(desc));
    desc.nextInChain = (const WGPUChainedStruct*)&wgslDesc;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &desc);
    free(wgslSource);
    return module;
}

static void handle_request_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message, void* userdata)
{
    (void)userdata;
    if (status == WGPURequestAdapterStatus_Success)
        g_wgpu.adapter = adapter;
    else
        fprintf(stderr, "[webgpu.c] RequestAdapter failed: %s\n", message);
}

static void handle_request_device(WGPURequestDeviceStatus status, WGPUDevice device, const char* message, void* userdata)
{
    (void)userdata;
    if (status == WGPURequestDeviceStatus_Success)
        g_wgpu.device = device;
    else
        fprintf(stderr, "[webgpu.c] RequestDevice failed: %s\n", message);
}
