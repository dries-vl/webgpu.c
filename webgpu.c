// webgpu.c
// ----------------------------
// NOTE: This file now uses stb_image to load PNG files.
#define STBI_NO_SIMD  // Disable SSE/AVX intrinsics (doesn't work with TCC)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "wgpu.h"

// -----------------------------------------------------------------------------
// Update Vertex definition to include UV coordinates.
typedef struct {
    float position[3];
    float color[3];
    float uv[2]; // New: texture coordinates
} Vertex;

// -----------------------------------------------------------------------------
// Limits and capacities
#define MAX_PIPELINES             16
#define MAX_MESHES                128
#define UNIFORM_BUFFER_CAPACITY   1024  // bytes per pipeline’s uniform buffer
#define MAX_TEXTURES              16        // maximum textures per pipeline

// -----------------------------------------------------------------------------
// Pipeline Data: each pipeline now holds not only a uniform bind group but also a texture bind group.
typedef struct {
    WGPURenderPipeline pipeline;
    bool               used;
    // Uniform system.
    WGPUBuffer         uniformBuffer;
    WGPUBindGroup      uniformBindGroup;
    uint8_t            uniformData[UNIFORM_BUFFER_CAPACITY];
    int                uniformCurrentOffset;
    // --- NEW TEXTURE DATA ---
    WGPUBindGroup      textureBindGroup;
    WGPUSampler        textureSampler;
    WGPUTexture        textureObjects[MAX_TEXTURES];
    WGPUTextureView    textureViews[MAX_TEXTURES];
    int                textureCount;
} PipelineData;

// Mesh Data remains the same.
typedef struct {
    WGPUBuffer vertexBuffer;
    int        vertexCount;
    int        pipelineID;
    bool       used;
} MeshData;

// Global WebGPU context.
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

static WebGPUContext g_wgpu = {0};

// -----------------------------------------------------------------------------
// Global bind group layouts: one for the uniform buffer and one for textures.
// (For textures we create a bindgroup layout with one sampler (binding 0)
//  and MAX_TEXTURES individual texture bindings (bindings 1..MAX_TEXTURES).)
static WGPUBindGroupLayout s_uniformBindGroupLayout = NULL;
static WGPUBindGroupLayout s_textureBindGroupLayout = NULL;

// A 1×1 white texture for empty slots:
static WGPUTexture      g_defaultTexture = NULL;
static WGPUTextureView  g_defaultTextureView = NULL;

// Forward declarations
static void handle_request_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message, void* userdata);
static void handle_request_device(WGPURequestDeviceStatus status, WGPUDevice device, const char* message, void* userdata);
static WGPUShaderModule loadWGSL(WGPUDevice device, const char* filePath);

// -----------------------------------------------------------------------------
// Current frame objects (global for simplicity)
static WGPUSurfaceTexture    g_currentSurfaceTexture = {0};
static WGPUTextureView       g_currentView = NULL;
static WGPUCommandEncoder    g_currentEncoder = NULL;
static WGPURenderPassEncoder g_currentPass = NULL;

// -----------------------------------------------------------------------------
// wgpuInit: Initialize WebGPU, create global bind group layouts, default texture, etc.
void wgpuInit(HINSTANCE hInstance, HWND hwnd, int width, int height) {
    g_wgpu = (WebGPUContext){0}; // initialize all fields to zero

    // Instance creation (your instance extras, etc.)
    WGPUInstanceExtras extras = {0};
    extras.chain.sType = WGPUSType_InstanceExtras;
    extras.backends   = WGPUInstanceBackend_GL;
    extras.flags      = 0;
    extras.dx12ShaderCompiler = WGPUDx12Compiler_Undefined;
    extras.gles3MinorVersion  = WGPUGles3MinorVersion_Automatic;
    extras.dxilPath = NULL;
    extras.dxcPath  = NULL;

    WGPUInstanceDescriptor instDesc = {0};
    instDesc.nextInChain = (const WGPUChainedStruct*)&extras;
    g_wgpu.instance = wgpuCreateInstance(&instDesc);
    assert(g_wgpu.instance);

    WGPUSurfaceDescriptorFromWindowsHWND chained_desc = {0};
    chained_desc.chain.sType = WGPUSType_SurfaceDescriptorFromWindowsHWND;
    chained_desc.hwnd = hwnd;
    chained_desc.hinstance = hInstance;

    WGPUSurfaceDescriptor surface_desc = {0};
    surface_desc.nextInChain = (const WGPUChainedStruct*)&chained_desc;
    g_wgpu.surface = wgpuInstanceCreateSurface(g_wgpu.instance, &surface_desc);
    assert(g_wgpu.surface);

    WGPURequestAdapterOptions adapter_opts = {0};
    adapter_opts.compatibleSurface = g_wgpu.surface;
    wgpuInstanceRequestAdapter(g_wgpu.instance, &adapter_opts, handle_request_adapter, NULL);
    assert(g_wgpu.adapter);
    wgpuAdapterRequestDevice(g_wgpu.adapter, NULL, handle_request_device, NULL);
    assert(g_wgpu.device);
    g_wgpu.queue = wgpuDeviceGetQueue(g_wgpu.device);
    assert(g_wgpu.queue);

    // Create the global uniform bind group layout.
    {
        WGPUBindGroupLayoutEntry entry = {0};
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        entry.buffer.type = WGPUBufferBindingType_Uniform;
        entry.buffer.minBindingSize = UNIFORM_BUFFER_CAPACITY;
        WGPUBindGroupLayoutDescriptor bglDesc = {0};
        bglDesc.entryCount = 1;
        bglDesc.entries = &entry;
        s_uniformBindGroupLayout = wgpuDeviceCreateBindGroupLayout(g_wgpu.device, &bglDesc);
    }
    
        // 2) Create a texture bind group layout with 1 sampler + 16 textures
    {
        int totalEntries = 1 + MAX_TEXTURES; // binding0: sampler, binding1..16: textures
        WGPUBindGroupLayoutEntry *texEntries = calloc(totalEntries, sizeof(WGPUBindGroupLayoutEntry));
        // Sampler at binding=0
        texEntries[0].binding = 0;
        texEntries[0].visibility = WGPUShaderStage_Fragment;
        texEntries[0].sampler.type = WGPUSamplerBindingType_Filtering;

        // 16 texture bindings
        for (int i = 0; i < MAX_TEXTURES; i++) {
            texEntries[i+1].binding = i + 1;  // i+1
            texEntries[i+1].visibility = WGPUShaderStage_Fragment;
            texEntries[i+1].texture.sampleType = WGPUTextureSampleType_Float;
            texEntries[i+1].texture.viewDimension = WGPUTextureViewDimension_2D;
            texEntries[i+1].texture.multisampled = false;
        }
        WGPUBindGroupLayoutDescriptor texLayoutDesc = {0};
        texLayoutDesc.entryCount = totalEntries;
        texLayoutDesc.entries = texEntries;
        s_textureBindGroupLayout = wgpuDeviceCreateBindGroupLayout(g_wgpu.device, &texLayoutDesc);
        free(texEntries);
    }

    // 3) Create a 1×1 white texture for empty slots
    {
        uint8_t whitePixel[4] = {255,255,255,255};
        WGPUTextureDescriptor td = {0};
        td.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
        td.dimension = WGPUTextureDimension_2D;
        td.format = WGPUTextureFormat_RGBA8Unorm;
        td.size.width  = 1;
        td.size.height = 1;
        td.size.depthOrArrayLayers = 1;
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        g_defaultTexture = wgpuDeviceCreateTexture(g_wgpu.device, &td);

        // Copy data
        WGPUImageCopyTexture ict = {0};
        ict.texture = g_defaultTexture;
        WGPUTextureDataLayout tdl = {0};
        tdl.bytesPerRow    = 4;
        tdl.rowsPerImage   = 1;
        WGPUExtent3D extent = {1,1,1};
        wgpuQueueWriteTexture(g_wgpu.queue, &ict, whitePixel, 4, &tdl, &extent);

        // Create a view
        g_defaultTextureView = wgpuTextureCreateView(g_defaultTexture, NULL);
    }

    WGPUSurfaceCapabilities caps = {0};
    wgpuSurfaceGetCapabilities(g_wgpu.surface, g_wgpu.adapter, &caps);
    WGPUTextureFormat chosenFormat = WGPUTextureFormat_BGRA8Unorm;
    if (caps.formatCount > 0) {
        chosenFormat = caps.formats[0];
    }

    g_wgpu.config = (WGPUSurfaceConfiguration){
        .device = g_wgpu.device,
        .format = chosenFormat,
        .width = width,
        .height = height,
        .usage = WGPUTextureUsage_RenderAttachment,
        .presentMode = WGPUPresentMode_Fifo
    };
    wgpuSurfaceConfigure(g_wgpu.surface, &g_wgpu.config);

    g_wgpu.initialized = true;
    printf("[webgpu.c] wgpuInit done.\n");
}

// -----------------------------------------------------------------------------
// wgpuShutdown: Release resources.
void wgpuShutdown(void) {
    if (!g_wgpu.initialized) return;
    for (int i = 0; i < MAX_MESHES; i++) {
        if (g_wgpu.meshes[i].used) {
            wgpuBufferRelease(g_wgpu.meshes[i].vertexBuffer);
            g_wgpu.meshes[i].used = false;
        }
    }
    for (int i = 0; i < MAX_PIPELINES; i++) {
        if (g_wgpu.pipelines[i].used) {
            wgpuRenderPipelineRelease(g_wgpu.pipelines[i].pipeline);
            if (g_wgpu.pipelines[i].uniformBindGroup)
                wgpuBindGroupRelease(g_wgpu.pipelines[i].uniformBindGroup);
            if (g_wgpu.pipelines[i].uniformBuffer)
                wgpuBufferRelease(g_wgpu.pipelines[i].uniformBuffer);
            // --- Release texture resources ---
            if (g_wgpu.pipelines[i].textureBindGroup)
                wgpuBindGroupRelease(g_wgpu.pipelines[i].textureBindGroup);
            if (g_wgpu.pipelines[i].textureSampler)
                wgpuSamplerRelease(g_wgpu.pipelines[i].textureSampler);
            for (int j = 0; j < g_wgpu.pipelines[i].textureCount; j++) {
                wgpuTextureViewRelease(g_wgpu.pipelines[i].textureViews[j]);
                wgpuTextureRelease(g_wgpu.pipelines[i].textureObjects[j]);
            }
            g_wgpu.pipelines[i].used = false;
        }
    }
    if (s_uniformBindGroupLayout) { wgpuBindGroupLayoutRelease(s_uniformBindGroupLayout); s_uniformBindGroupLayout = NULL; }
    if (s_textureBindGroupLayout) { wgpuBindGroupLayoutRelease(s_textureBindGroupLayout); s_textureBindGroupLayout = NULL; }
    if (g_wgpu.queue) { wgpuQueueRelease(g_wgpu.queue); g_wgpu.queue = NULL; }
    if (g_wgpu.device) { wgpuDeviceRelease(g_wgpu.device); g_wgpu.device = NULL; }
    if (g_wgpu.adapter) { wgpuAdapterRelease(g_wgpu.adapter); g_wgpu.adapter = NULL; }
    if (g_wgpu.surface) { wgpuSurfaceRelease(g_wgpu.surface); g_wgpu.surface = NULL; }
    if (g_wgpu.instance) { wgpuInstanceRelease(g_wgpu.instance); g_wgpu.instance = NULL; }
    g_wgpu.initialized = false;
    printf("[webgpu.c] wgpuShutdown done.\n");
}

// -----------------------------------------------------------------------------
// wgpuCreatePipeline: Create a render pipeline plus a generic uniform and texture bindgroup.
int wgpuCreatePipeline(const char* shaderPath) {
    if (!g_wgpu.initialized) {
        fprintf(stderr, "[webgpu.c] wgpuCreatePipeline called before init!\n");
        return -1;
    }
    int pipelineID = -1;
    for (int i = 0; i < MAX_PIPELINES; i++) {
        if (!g_wgpu.pipelines[i].used) {
            pipelineID = i;
            g_wgpu.pipelines[i].used = true;
            g_wgpu.pipelines[i].uniformCurrentOffset = 0;
            memset(g_wgpu.pipelines[i].uniformData, 0, UNIFORM_BUFFER_CAPACITY);
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
    
    // Create a pipeline layout with 2 bind groups:
    // group 0: uniform buffer, group 1: textures.
    WGPUBindGroupLayout bgls[2] = { s_uniformBindGroupLayout, s_textureBindGroupLayout };
    WGPUPipelineLayoutDescriptor layoutDesc = {0};
    layoutDesc.bindGroupLayoutCount = 2;
    layoutDesc.bindGroupLayouts = bgls;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(g_wgpu.device, &layoutDesc);
    assert(pipelineLayout);
    
    WGPURenderPipelineDescriptor rpDesc = {0};
    rpDesc.layout = pipelineLayout;
    
    // Vertex stage.
    rpDesc.vertex.module = shaderModule;
    rpDesc.vertex.entryPoint = "vs_main";
    // --- Update vertex attributes to include UV ---
    WGPUVertexAttribute attributes[3] = {0};
    attributes[0].format = WGPUVertexFormat_Float32x3;
    attributes[0].offset = 0;
    attributes[0].shaderLocation = 0;
    attributes[1].format = WGPUVertexFormat_Float32x3;
    attributes[1].offset = sizeof(float) * 3;
    attributes[1].shaderLocation = 1;
    attributes[2].format = WGPUVertexFormat_Float32x2;
    attributes[2].offset = sizeof(float) * 6;
    attributes[2].shaderLocation = 2;
    WGPUVertexBufferLayout vbl = {0};
    vbl.arrayStride = sizeof(Vertex);
    vbl.attributeCount = 3;
    vbl.attributes = attributes;
    rpDesc.vertex.bufferCount = 1;
    rpDesc.vertex.buffers = &vbl;
    
    // Fragment stage.
    WGPUFragmentState fragState = {0};
    fragState.module = shaderModule;
    fragState.entryPoint = "fs_main";
    fragState.targetCount = 1;
    WGPUColorTargetState colorTarget = {0};
    colorTarget.format = g_wgpu.config.format;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    fragState.targets = &colorTarget;
    rpDesc.fragment = &fragState;
    
    WGPUPrimitiveState prim = {0};
    prim.topology = WGPUPrimitiveTopology_TriangleList;
    rpDesc.primitive = prim;
    WGPUMultisampleState ms = {0};
    ms.count = 1;
    ms.mask = 0xFFFFFFFF;
    rpDesc.multisample = ms;
    
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(g_wgpu.device, &rpDesc);
    g_wgpu.pipelines[pipelineID].pipeline = pipeline;
    
    // Create uniform buffer.
    WGPUBufferDescriptor ubDesc = {0};
    ubDesc.size = UNIFORM_BUFFER_CAPACITY;
    ubDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    g_wgpu.pipelines[pipelineID].uniformBuffer = wgpuDeviceCreateBuffer(g_wgpu.device, &ubDesc);
    assert(g_wgpu.pipelines[pipelineID].uniformBuffer);
    
    // Create uniform bind group.
    WGPUBindGroupEntry uEntry = {0};
    uEntry.binding = 0;
    uEntry.buffer = g_wgpu.pipelines[pipelineID].uniformBuffer;
    uEntry.offset = 0;
    uEntry.size = UNIFORM_BUFFER_CAPACITY;
    WGPUBindGroupDescriptor uBgDesc = {0};
    uBgDesc.layout = s_uniformBindGroupLayout;
    uBgDesc.entryCount = 1;
    uBgDesc.entries = &uEntry;
    g_wgpu.pipelines[pipelineID].uniformBindGroup = wgpuDeviceCreateBindGroup(g_wgpu.device, &uBgDesc);
    
    // Create a sampler
    PipelineData *pd = &g_wgpu.pipelines[pipelineID];
    WGPUSamplerDescriptor samplerDesc = {0};
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.maxAnisotropy = 1;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    pd->textureSampler = wgpuDeviceCreateSampler(g_wgpu.device, &samplerDesc);

    // Initialize all textures to default
    pd->textureCount = 0;
    for (int i=0; i<MAX_TEXTURES; i++) {
        pd->textureObjects[i] = NULL;  // will fill later
        pd->textureViews[i]   = g_defaultTextureView;
    }

    // Create the initial texture bind group
    {
        // We'll have 1 + 16 = 17 entries
        int totalEntries = 1 + MAX_TEXTURES;
        WGPUBindGroupEntry *entries = calloc(totalEntries, sizeof(WGPUBindGroupEntry));
        // Sampler at binding=0
        entries[0].binding = 0;
        entries[0].sampler = pd->textureSampler;
        // 16 textures at binding=1..16
        for (int i=0; i<MAX_TEXTURES; i++) {
            entries[i+1].binding = i + 1;
            entries[i+1].textureView = pd->textureViews[i];
        }

        if (pd->textureBindGroup) {
            wgpuBindGroupRelease(pd->textureBindGroup);
        }
        WGPUBindGroupDescriptor bgDesc = {0};
        bgDesc.layout     = s_textureBindGroupLayout;
        bgDesc.entryCount = totalEntries;
        bgDesc.entries    = entries;
        pd->textureBindGroup = wgpuDeviceCreateBindGroup(g_wgpu.device, &bgDesc);

        free(entries);
    }
    
    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);
    
    printf("[webgpu.c] Created pipeline %d from shader: %s\n", pipelineID, shaderPath);
    return pipelineID;
}

// -----------------------------------------------------------------------------
// wgpuCreateMesh: Create a mesh for a given pipeline.
int wgpuCreateMesh(int pipelineID, const Vertex *vertices, int vertexCount) {
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
    WGPUBufferDescriptor bufDesc = {0};
    bufDesc.size = dataSize;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(g_wgpu.device, &bufDesc);
    assert(buffer);
    wgpuQueueWriteBuffer(g_wgpu.queue, buffer, 0, vertices, dataSize);
    g_wgpu.meshes[meshID].vertexBuffer = buffer;
    g_wgpu.meshes[meshID].vertexCount = vertexCount;
    g_wgpu.meshes[meshID].pipelineID = pipelineID;
    printf("[webgpu.c] Created mesh %d with %d vertices for pipeline %d\n", meshID, vertexCount, pipelineID);
    return meshID;
}

// -----------------------------------------------------------------------------
// wgpuAddUniform: (unchanged)
int wgpuAddUniform(int pipelineID, const void* data, int dataSize) {
    if (pipelineID < 0 || pipelineID >= MAX_PIPELINES || !g_wgpu.pipelines[pipelineID].used) {
        fprintf(stderr, "[webgpu.c] Invalid pipeline ID for uniform: %d\n", pipelineID);
        return -1;
    }
    // Inline alignment determination using ternary operators
    int alignment = (dataSize <= 4) ? 4 :
                    (dataSize <= 8) ? 8 :
                    16; // Default for vec3, vec4, mat4x4, or larger

    PipelineData* pd = &g_wgpu.pipelines[pipelineID];
    // Align the offset to the correct boundary (based on WGSL rules)
    int alignedOffset = (pd->uniformCurrentOffset + (alignment - 1)) & ~(alignment - 1);
    // Check if the new offset exceeds buffer capacity
    if (alignedOffset + dataSize > UNIFORM_BUFFER_CAPACITY) {
        fprintf(stderr, "[webgpu.c] Uniform buffer capacity exceeded for pipeline %d\n", pipelineID);
        return -1;
    }
    // Copy the data into the aligned buffer
    memcpy(pd->uniformData + alignedOffset, data, dataSize);
    // Update the current offset
    pd->uniformCurrentOffset = alignedOffset + dataSize;
    printf("[webgpu.c] Added uniform at aligned offset %d (size %d) with alignment %d to pipeline %d\n", 
            alignedOffset, dataSize, alignment, pipelineID);
    return alignedOffset;
}

// -----------------------------------------------------------------------------
// wgpuSetUniformValue: Update a uniform’s data (at a given offset) in the CPU copy.
void wgpuSetUniformValue(int pipelineID, int offset, const void* data, int dataSize) {
    if (pipelineID < 0 || pipelineID >= MAX_PIPELINES || !g_wgpu.pipelines[pipelineID].used) {
        fprintf(stderr, "[webgpu.c] Invalid pipeline ID in wgpuSetUniformValue: %d\n", pipelineID);
        return;
    }
    PipelineData* pd = &g_wgpu.pipelines[pipelineID];
    if (offset < 0 || offset + dataSize > pd->uniformCurrentOffset) {
        fprintf(stderr, "[webgpu.c] Invalid uniform offset or size for pipeline %d\n", pipelineID);
        return;
    }
    memcpy(pd->uniformData + offset, data, dataSize);
    printf("[webgpu.c] Pipeline %d uniform at offset %d updated (size %d)\n", pipelineID, offset, dataSize);
}

// -----------------------------------------------------------------------------
// wgpuAddTexture: Load a PNG file and add it to the pipeline’s texture bind group.
int wgpuAddTexture(int pipelineID, const char* filename) {
    PipelineData* pd = &g_wgpu.pipelines[pipelineID];
    if (pd->textureCount >= MAX_TEXTURES) {
        fprintf(stderr, "No more texture slots!\n");
        return -1;
    }
    int slot = pd->textureCount; // e.g. 0 => binding=1, 1 => binding=2, etc.

    // Load from disk using stb_image
    int w,h,channels;
    unsigned char* data = stbi_load(filename, &w, &h, &channels, STBI_rgb_alpha);
    if (!data) {
        fprintf(stderr, "Failed to load %s\n", filename);
        return -1;
    }
    // Create texture
    WGPUTextureDescriptor td = {0};
    td.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
    td.dimension = WGPUTextureDimension_2D;
    td.format    = WGPUTextureFormat_RGBA8Unorm;
    td.size.width  = w;
    td.size.height = h;
    td.size.depthOrArrayLayers = 1;
    td.mipLevelCount = 1;
    td.sampleCount   = 1;
    WGPUTexture tex = wgpuDeviceCreateTexture(g_wgpu.device, &td);

    // Upload data
    WGPUImageCopyTexture ict = {0};
    ict.texture = tex;
    WGPUTextureDataLayout tdl = {0};
    tdl.bytesPerRow   = 4 * w;
    tdl.rowsPerImage  = h;
    WGPUExtent3D ext = {(uint32_t)w,(uint32_t)h,1};
    wgpuQueueWriteTexture(g_wgpu.queue, &ict, data, (size_t)(4*w*h), &tdl, &ext);
    stbi_image_free(data);

    // Create view
    WGPUTextureView view = wgpuTextureCreateView(tex, NULL);

    pd->textureObjects[slot] = tex;
    pd->textureViews[slot]   = view;
    pd->textureCount++;

    // Now rebuild the bind group
    int totalEntries = 1 + MAX_TEXTURES;
    WGPUBindGroupEntry* e = calloc(totalEntries, sizeof(WGPUBindGroupEntry));
    e[0].binding = 0;
    e[0].sampler = pd->textureSampler;
    for (int i=0; i<MAX_TEXTURES; i++) {
        e[i+1].binding = i+1;
        e[i+1].textureView = (i < pd->textureCount) 
                             ? pd->textureViews[i]
                             : g_defaultTextureView;
    }
    if (pd->textureBindGroup) {
        wgpuBindGroupRelease(pd->textureBindGroup);
    }
    WGPUBindGroupDescriptor bgDesc = {0};
    bgDesc.layout     = s_textureBindGroupLayout;
    bgDesc.entryCount = totalEntries;
    bgDesc.entries    = e;
    pd->textureBindGroup = wgpuDeviceCreateBindGroup(g_wgpu.device, &bgDesc);
    free(e);

    printf("Added texture %s to pipeline %d at slot %d (binding=%d)\n",
           filename, pipelineID, slot, slot+1);

    return slot;
}

// -----------------------------------------------------------------------------
// wgpuStartFrame: Begin a new frame.
void wgpuStartFrame(void) {
    wgpuSurfaceGetCurrentTexture(g_wgpu.surface, &g_currentSurfaceTexture);
    if (g_currentSurfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success)
        return;
    g_currentView = wgpuTextureCreateView(g_currentSurfaceTexture.texture, NULL);
    WGPUCommandEncoderDescriptor encDesc = {0};
    g_currentEncoder = wgpuDeviceCreateCommandEncoder(g_wgpu.device, &encDesc);
    WGPURenderPassColorAttachment colorAtt = {0};
    colorAtt.view = g_currentView;
    colorAtt.loadOp = WGPULoadOp_Clear;
    colorAtt.storeOp = WGPUStoreOp_Store;
    colorAtt.clearValue = (WGPUColor){0.1, 0.2, 0.3, 1.0};
    WGPURenderPassDescriptor passDesc = {0};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAtt;
    g_currentPass = wgpuCommandEncoderBeginRenderPass(g_currentEncoder, &passDesc);
}

// -----------------------------------------------------------------------------
// wgpuDrawPipeline: Update the uniform buffer, bind groups, and issue draw calls.
void wgpuDrawPipeline(int pipelineID) {
    if (!g_currentPass) return;
    if (pipelineID < 0 || pipelineID >= MAX_PIPELINES || !g_wgpu.pipelines[pipelineID].used)
        return;
    
    PipelineData* pd = &g_wgpu.pipelines[pipelineID];
    // Write CPU–side uniform data to GPU.
    wgpuQueueWriteBuffer(g_wgpu.queue, pd->uniformBuffer, 0, pd->uniformData, UNIFORM_BUFFER_CAPACITY);
    // Bind uniform bind group (group 0).
    wgpuRenderPassEncoderSetBindGroup(g_currentPass, 0, pd->uniformBindGroup, 0, NULL);
    // --- Bind texture bind group (group 1) ---
    wgpuRenderPassEncoderSetBindGroup(g_currentPass, 1, pd->textureBindGroup, 0, NULL);
    // Set the render pipeline.
    wgpuRenderPassEncoderSetPipeline(g_currentPass, pd->pipeline);
    
    // Draw every mesh belonging to this pipeline.
    for (int i = 0; i < MAX_MESHES; i++) {
        if (g_wgpu.meshes[i].used && g_wgpu.meshes[i].pipelineID == pipelineID) {
            wgpuRenderPassEncoderSetVertexBuffer(g_currentPass, 0, g_wgpu.meshes[i].vertexBuffer, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderDraw(g_currentPass, g_wgpu.meshes[i].vertexCount, 1, 0, 0);
        }
    }
}

// -----------------------------------------------------------------------------
// wgpuEndFrame: End the frame, submit commands, and present.
void wgpuEndFrame(void) {
    if (!g_currentPass)
        return;
    wgpuRenderPassEncoderEnd(g_currentPass);
    wgpuRenderPassEncoderRelease(g_currentPass);
    g_currentPass = NULL;
    WGPUCommandBufferDescriptor cmdDesc = {0};
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
// loadWGSL: Load a WGSL shader from file.
static WGPUShaderModule loadWGSL(WGPUDevice device, const char* filePath) {
    FILE* fp = fopen(filePath, "rb");
    if (!fp) {
        fprintf(stderr, "[webgpu.c] Failed to open WGSL file: %s\n", filePath);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char* wgslSource = malloc((size_t)size + 1);
    if (!wgslSource) {
        fclose(fp);
        return NULL;
    }
    fread(wgslSource, 1, (size_t)size, fp);
    wgslSource[size] = '\0';
    fclose(fp);
    WGPUShaderModuleWGSLDescriptor wgslDesc = {0};
    wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDesc.code = wgslSource;
    WGPUShaderModuleDescriptor desc = {0};
    desc.nextInChain = (const WGPUChainedStruct*)&wgslDesc;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &desc);
    free(wgslSource);
    return module;
}

static void handle_request_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message, void* userdata) {
    (void)userdata;
    if (status == WGPURequestAdapterStatus_Success)
        g_wgpu.adapter = adapter;
    else
        fprintf(stderr, "[webgpu.c] RequestAdapter failed: %s\n", message);
}

static void handle_request_device(WGPURequestDeviceStatus status, WGPUDevice device, const char* message, void* userdata) {
    (void)userdata;
    if (status == WGPURequestDeviceStatus_Success)
        g_wgpu.device = device;
    else
        fprintf(stderr, "[webgpu.c] RequestDevice failed: %s\n", message);
}
