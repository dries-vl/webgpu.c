#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "wgpu.h"

// Minimal "vertex" definition must match main.c
typedef struct {
    float position[2];
    float color[3];
} Vertex;

#define MAX_PIPELINES  16
#define MAX_MESHES    128

typedef struct {
    WGPURenderPipeline pipeline;
    float dummyUniformValue;
    bool used;
} PipelineData;

typedef struct {
    WGPUBuffer vertexBuffer;
    int        vertexCount;
    int        pipelineID;
    bool       used;
} MeshData;

typedef struct {
    WGPUInstance             instance;
    WGPUSurface              surface;
    WGPUAdapter              adapter;
    WGPUDevice               device;
    WGPUQueue                queue;

    WGPUSurfaceConfiguration config;
    bool                     initialized;

    PipelineData pipelines[MAX_PIPELINES];
    MeshData     meshes[MAX_MESHES];

} WebGPUContext;

static WebGPUContext g_wgpu;

// Forward declarations
static void handle_request_adapter(
    WGPURequestAdapterStatus status,
    WGPUAdapter adapter,
    const char* message,
    void* userdata);
static void handle_request_device(
    WGPURequestDeviceStatus status,
    WGPUDevice device,
    const char* message,
    void* userdata);

static WGPUShaderModule loadWGSL(WGPUDevice device, const char* filePath);

// Current frame objects (global for simplicity)
static WGPUSurfaceTexture  g_currentSurfaceTexture;
static WGPUTextureView     g_currentView;
static WGPUCommandEncoder  g_currentEncoder;
static WGPURenderPassEncoder g_currentPass;

void wgpuInit(HINSTANCE hInstance, HWND hwnd, int width, int height)
{
    memset(&g_wgpu, 0, sizeof(g_wgpu));

    // --- Use extras to specify the backend as OpenGL ---
    WGPUInstanceExtras extras;
    memset(&extras, 0, sizeof(extras));
    extras.chain.sType = WGPUSType_InstanceExtras;
    extras.backends   = WGPUInstanceBackend_GL;  // Specify the OpenGL backend
    extras.flags      = 0;
    // Set other fields as needed (e.g. for shader compilers or GLES minor version)
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

    // Create surface from Win32 HWND
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

    // Request adapter
    WGPURequestAdapterOptions adapter_opts;
    memset(&adapter_opts, 0, sizeof(adapter_opts));
    adapter_opts.compatibleSurface = g_wgpu.surface;
    wgpuInstanceRequestAdapter(
        g_wgpu.instance,
        &adapter_opts,
        handle_request_adapter,
        NULL
    );
    assert(g_wgpu.adapter);

    // Request device
    wgpuAdapterRequestDevice(g_wgpu.adapter, NULL, handle_request_device, NULL);
    assert(g_wgpu.device);

    g_wgpu.queue = wgpuDeviceGetQueue(g_wgpu.device);
    assert(g_wgpu.queue);

    // Query surface capabilities
    WGPUSurfaceCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    wgpuSurfaceGetCapabilities(g_wgpu.surface, g_wgpu.adapter, &caps);

    // Pick a format (remove alpha-mode usage!)
    WGPUTextureFormat chosenFormat = WGPUTextureFormat_BGRA8Unorm;
    if (caps.formatCount > 0) {
        chosenFormat = caps.formats[0];
    }

    // If your older wgpu-native does not have wgpuSurfaceCapabilitiesFreeMembers, just skip it
    // e.g. 
    // if (wgpuSurfaceCapabilitiesFreeMembers) {
    //     wgpuSurfaceCapabilitiesFreeMembers(caps);
    // }

    // Prepare surface configuration
    memset(&g_wgpu.config, 0, sizeof(g_wgpu.config));
    g_wgpu.config.device      = g_wgpu.device;
    g_wgpu.config.format      = chosenFormat;
    g_wgpu.config.width       = width;
    g_wgpu.config.height      = height;
    g_wgpu.config.usage       = WGPUTextureUsage_RenderAttachment;
    g_wgpu.config.presentMode = WGPUPresentMode_Fifo; 
    // Removed alphaMode references for compatibility

    wgpuSurfaceConfigure(g_wgpu.surface, &g_wgpu.config);

    g_wgpu.initialized = true;
    printf("[webgpu.c] wgpuInit done.\n");
}

void wgpuShutdown()
{
    if (!g_wgpu.initialized) return;

    // Release meshes
    int i;
    for (i = 0; i < MAX_MESHES; i++) {
        if (g_wgpu.meshes[i].used) {
            wgpuBufferRelease(g_wgpu.meshes[i].vertexBuffer);
            g_wgpu.meshes[i].used = false;
        }
    }
    // Release pipelines
    for (i = 0; i < MAX_PIPELINES; i++) {
        if (g_wgpu.pipelines[i].used) {
            wgpuRenderPipelineRelease(g_wgpu.pipelines[i].pipeline);
            g_wgpu.pipelines[i].used = false;
        }
    }

    if (g_wgpu.queue)   { wgpuQueueRelease(g_wgpu.queue);   g_wgpu.queue   = NULL; }
    if (g_wgpu.device)  { wgpuDeviceRelease(g_wgpu.device); g_wgpu.device  = NULL; }
    if (g_wgpu.adapter) { wgpuAdapterRelease(g_wgpu.adapter); g_wgpu.adapter = NULL; }
    if (g_wgpu.surface) { wgpuSurfaceRelease(g_wgpu.surface); g_wgpu.surface = NULL; }
    if (g_wgpu.instance){ wgpuInstanceRelease(g_wgpu.instance); g_wgpu.instance = NULL; }

    g_wgpu.initialized = false;
    printf("[webgpu.c] wgpuShutdown done.\n");
}

int wgpuCreatePipeline(const char* shaderPath)
{
    if (!g_wgpu.initialized) {
        fprintf(stderr, "[webgpu.c] wgpuCreatePipeline called before init!\n");
        return -1;
    }

    int i;
    int pipelineID = -1;
    for (i = 0; i < MAX_PIPELINES; i++) {
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

    WGPUPipelineLayoutDescriptor layoutDesc;
    memset(&layoutDesc, 0, sizeof(layoutDesc));
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(g_wgpu.device, &layoutDesc);
    assert(pipelineLayout);

    WGPURenderPipelineDescriptor rpDesc;
    memset(&rpDesc, 0, sizeof(rpDesc));
    rpDesc.layout = pipelineLayout;

    // Vertex stage
    rpDesc.vertex.module = shaderModule;
    rpDesc.vertex.entryPoint = "vs_main";

    // Hardcode a basic vertex buffer layout
    WGPUVertexAttribute attributes[2];
    memset(attributes, 0, sizeof(attributes));
    attributes[0].format = WGPUVertexFormat_Float32x2;
    attributes[0].offset = 0;
    attributes[0].shaderLocation = 0;
    attributes[1].format = WGPUVertexFormat_Float32x3;
    attributes[1].offset = sizeof(float)*2;
    attributes[1].shaderLocation = 1;

    WGPUVertexBufferLayout vbl;
    memset(&vbl, 0, sizeof(vbl));
    vbl.arrayStride    = sizeof(Vertex);
    vbl.attributeCount = 2;
    vbl.attributes     = attributes;

    rpDesc.vertex.bufferCount = 1;
    rpDesc.vertex.buffers     = &vbl;

    // Fragment
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

    // Primitive
    WGPUPrimitiveState prim;
    memset(&prim, 0, sizeof(prim));
    prim.topology = WGPUPrimitiveTopology_TriangleList;
    rpDesc.primitive = prim;

    // Multisample
    WGPUMultisampleState ms;
    memset(&ms, 0, sizeof(ms));
    ms.count = 1;
    ms.mask  = 0xFFFFFFFF;
    rpDesc.multisample = ms;

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(g_wgpu.device, &rpDesc);
    g_wgpu.pipelines[pipelineID].pipeline = pipeline;
    g_wgpu.pipelines[pipelineID].dummyUniformValue = 0.0f;

    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);

    printf("[webgpu.c] Created pipeline %d from shader: %s\n", pipelineID, shaderPath);
    return pipelineID;
}

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

    int i;
    int meshID = -1;
    for (i = 0; i < MAX_MESHES; i++) {
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

    size_t dataSize = sizeof(Vertex)*vertexCount;
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

// Simple uniform setter
void wgpuSetUniform(int pipelineID, float someValue)
{
    if (pipelineID < 0 || pipelineID >= MAX_PIPELINES || !g_wgpu.pipelines[pipelineID].used) {
        fprintf(stderr, "[webgpu.c] Invalid pipeline ID for uniform: %d\n", pipelineID);
        return;
    }
    g_wgpu.pipelines[pipelineID].dummyUniformValue = someValue;
    // In real usage: update a uniform buffer/bind group
    printf("[webgpu.c] Pipeline %d uniform set to %f\n", pipelineID, someValue);
}

void wgpuStartFrame()
{
    // Acquire texture
    wgpuSurfaceGetCurrentTexture(g_wgpu.surface, &g_currentSurfaceTexture);
    // If older wgpu-native doesn’t have WGPUSurfaceGetCurrentTextureStatus, omit checks or handle differently

    if (g_currentSurfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success) {
        // Could reconfigure if lost/outdated, etc.
        return;
    }

    // Create a texture view
    g_currentView = wgpuTextureCreateView(g_currentSurfaceTexture.texture, NULL);

    // Create command encoder
    WGPUCommandEncoderDescriptor encDesc;
    memset(&encDesc, 0, sizeof(encDesc));
    g_currentEncoder = wgpuDeviceCreateCommandEncoder(g_wgpu.device, &encDesc);

    // Begin render pass
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
    if (pipelineID < 0 || pipelineID >= MAX_PIPELINES || !g_wgpu.pipelines[pipelineID].used) {
        return;
    }

    wgpuRenderPassEncoderSetPipeline(g_currentPass, g_wgpu.pipelines[pipelineID].pipeline);

    // If you had bind groups, set them here
    // wgpuRenderPassEncoderSetBindGroup(g_currentPass, 0, yourBindGroup, 0, NULL);

    int i;
    for (i = 0; i < MAX_MESHES; i++) {
        if (g_wgpu.meshes[i].used && g_wgpu.meshes[i].pipelineID == pipelineID) {
            // Bind vertex buffer
            wgpuRenderPassEncoderSetVertexBuffer(
                g_currentPass, 0, g_wgpu.meshes[i].vertexBuffer, 0, WGPU_WHOLE_SIZE);
            // Draw
            wgpuRenderPassEncoderDraw(g_currentPass, g_wgpu.meshes[i].vertexCount, 1, 0, 0);
        }
    }
}

void wgpuEndFrame()
{
    if (!g_currentPass) {
        // No pass if texture was lost/outdated
        return;
    }

    wgpuRenderPassEncoderEnd(g_currentPass);
    wgpuRenderPassEncoderRelease(g_currentPass);
    g_currentPass = NULL;

    WGPUCommandBufferDescriptor cmdDesc;
    memset(&cmdDesc, 0, sizeof(cmdDesc));
    WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(g_currentEncoder, &cmdDesc);
    wgpuCommandEncoderRelease(g_currentEncoder);

    wgpuQueueSubmit(g_wgpu.queue, 1, &cmdBuf);
    wgpuCommandBufferRelease(cmdBuf);

    // Present with the *global surface*, not from the texture
    wgpuSurfacePresent(g_wgpu.surface);

    wgpuTextureViewRelease(g_currentView);
    g_currentView = NULL;
    wgpuTextureRelease(g_currentSurfaceTexture.texture);
    g_currentSurfaceTexture.texture = NULL;
}

// -----------------------------------------------------------------------------
// Internal helpers
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

static void handle_request_adapter(
    WGPURequestAdapterStatus status,
    WGPUAdapter adapter,
    const char* message,
    void* userdata)
{
    (void)userdata;
    if (status == WGPURequestAdapterStatus_Success) {
        g_wgpu.adapter = adapter;
    } else {
        fprintf(stderr, "[webgpu.c] RequestAdapter failed: %s\n", message);
    }
}

static void handle_request_device(
    WGPURequestDeviceStatus status,
    WGPUDevice device,
    const char* message,
    void* userdata)
{
    (void)userdata;
    if (status == WGPURequestDeviceStatus_Success) {
        g_wgpu.device = device;
    } else {
        fprintf(stderr, "[webgpu.c] RequestDevice failed: %s\n", message);
    }
}
