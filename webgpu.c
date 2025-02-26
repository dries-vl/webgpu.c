#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "game_data.h" // todo: remove this header, this library should be receiving no game-specific structs at all
#include "wgpu.h"

#pragma region PREDEFINED DATA
// Multiple possible layouts for textures
#define NUM_BIND_GROUP_LAYOUTS 1
#define STANDARD_TEXTURE {.sampleType = WGPUTextureSampleType_Float, .viewDimension = WGPUTextureViewDimension_2D, .multisampled = false}
// todo: this is a global variable that is set in init wgpu!
#define STANDARD_MAX_TEXTURES 4
static unsigned char get_texture_layout_index(enum MaterialFlags flags) {
    if (flags & STANDARD_TEXTURE_LAYOUT) {
        return 0;
    }
    return 0;
}
static const WGPUBindGroupLayoutDescriptor TEXTURE_LAYOUT_DESCRIPTORS[NUM_BIND_GROUP_LAYOUTS] = {
    { // TEXTURE_LAYOUT_STANDARD
        .entryCount = 1 + STANDARD_MAX_TEXTURES,
        .entries = (WGPUBindGroupLayoutEntry[]){
            // Sampler at binding = 0
            {
                .binding = 0,
                .visibility = WGPUShaderStage_Fragment,
                .sampler = { .type = WGPUSamplerBindingType_Filtering }
            },
            // todo: is there no way to just specify the amount of textures and use that to dyn. add these?
            {.binding = 1, .visibility = WGPUShaderStage_Fragment, .texture = STANDARD_TEXTURE},
            {.binding = 2, .visibility = WGPUShaderStage_Fragment, .texture = STANDARD_TEXTURE},
            {.binding = 3, .visibility = WGPUShaderStage_Fragment, .texture = STANDARD_TEXTURE},
            {.binding = 4, .visibility = WGPUShaderStage_Fragment, .texture = STANDARD_TEXTURE},
        }
    }
};
// Vertex layout consts
static unsigned char get_vertex_layout_index(enum MaterialFlags flags) {
    if (flags & STANDARD_VERTEX_LAYOUT) {
        return 0;
    } else if (flags & HUD_VERTEX_LAYOUT) {
        return 1;
    }
    return 0;
}
static const WGPUVertexBufferLayout VERTEX_LAYOUTS[2][2] = {
    { // STANDARD LAYOUT
        {   .arrayStride = sizeof(struct Vertex),
            .stepMode = WGPUVertexStepMode_Vertex,
            .attributeCount = 6,
            .attributes = (const WGPUVertexAttribute[]) {
                { .format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = 0 },
                { .format = WGPUVertexFormat_Snorm8x4, .offset = 12, .shaderLocation = 1 },
                { .format = WGPUVertexFormat_Snorm8x4, .offset = 16, .shaderLocation = 2 },
                { .format = WGPUVertexFormat_Float16x2, .offset = 20, .shaderLocation = 3 },
                { .format = WGPUVertexFormat_Unorm8x4, .offset = 24, .shaderLocation = 4 },
                { .format = WGPUVertexFormat_Uint8x4, .offset = 28, .shaderLocation = 5 }
            }
        },
        {   .arrayStride = sizeof(struct Instance),
            .stepMode = WGPUVertexStepMode_Instance,
            .attributeCount = 1,
            .attributes = (const WGPUVertexAttribute[]) {
                { .format = WGPUVertexFormat_Float32x3, .offset = 0, .shaderLocation = 6 }
            }
        }
    },
    { // HUD LAYOUT
        {   .arrayStride = sizeof(struct Vertex2D),
            .stepMode = WGPUVertexStepMode_Vertex,
            .attributeCount = 2,
            .attributes = (const WGPUVertexAttribute[]) {
                { .format = WGPUVertexFormat_Float32x2, .offset = 0,                 .shaderLocation = 0 },
                { .format = WGPUVertexFormat_Float32x2, .offset = sizeof(float) * 2, .shaderLocation = 1 }
            }
        },
        {   .arrayStride = sizeof(struct CharInstance),
            .stepMode = WGPUVertexStepMode_Instance,
            .attributeCount = 2,
            .attributes = (const WGPUVertexAttribute[]) {
                { .format = WGPUVertexFormat_Sint32,   .offset = 0,                 .shaderLocation = 2 },
                { .format = WGPUVertexFormat_Sint32,   .offset = sizeof(int),       .shaderLocation = 3 }
            }
        }
    }
};
#pragma endregion

#pragma region STRUCT DEFINITIONS
typedef struct {
    WGPURenderPipeline pipeline;
    bool               used;
    WGPUBuffer         uniformBuffer;
    WGPUBindGroup      uniformBindGroup;
    WGPUSampler        textureSampler; // sampler for textures reused between meshes

    enum MaterialFlags flags;
    unsigned char vertex_layout_index;
    unsigned char texture_layout_index;

    int current_uniform_offset;
    unsigned char uniform_data[UNIFORM_BUFFER_CAPACITY];
} GpuMaterial;

typedef struct {
    bool       used;
    int        material_id;

    WGPUBuffer vertexBuffer; // vertices are not kept in RAM, (memory mapping)
    int        vertexCount;
    WGPUBuffer indexBuffer; // indices are not kept in RAM (memory mapping)
    int        indexCount;
    WGPUBuffer instanceBuffer;
    void      *instances; // instances in RAM
    int        instance_count;

    WGPUBindGroup      textureBindGroup;
    WGPUTexture        *textureObjects;
    WGPUTextureView    *textureViews;
    int                textureCount; // keep track of max amount of textures allowed (set per pipeline)
} GpuMesh;

typedef struct {
    bool                     initialized;
    WGPUInstance             instance;
    WGPUSurface              surface;
    WGPUAdapter              adapter;
    WGPUDevice               device;
    WGPUQueue                queue;
    WGPUSurfaceConfiguration config;
    GpuMaterial materials[MAX_MATERIALS];
    GpuMesh     meshes[MAX_MESHES];
    // Current frame objects (global for simplicity)
    WGPUSurfaceTexture    currentSurfaceTexture;
    WGPUTextureView       currentView;
    WGPUCommandEncoder    currentEncoder;
    WGPURenderPassEncoder currentPass;
    // Default 1 pixel texture global to assign to every empty texture slot for new meshes
    WGPUTexture      defaultTexture;
    WGPUTextureView  defaultTextureView;
    // global depth texture
    WGPUDepthStencilState depthStencilState; 
    WGPURenderPassDepthStencilAttachment depthAttachment;
    // One standard uniform layout for all pipelines
    WGPUBindGroupLayout uniform_layout;
    WGPUBindGroupLayout texture_layouts[NUM_BIND_GROUP_LAYOUTS];
} WebGPUContext;
#pragma endregion

static void handle_request_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message, void* userdata) {
    WebGPUContext *context = (WebGPUContext *)userdata;
    if (status == WGPURequestAdapterStatus_Success)
        context->adapter = adapter;
    else
        fprintf(stderr, "[webgpu.c] RequestAdapter failed: %s\n", message);
}
static void handle_request_device(WGPURequestDeviceStatus status, WGPUDevice device, const char* message, void* userdata) {
    WebGPUContext *context = (WebGPUContext *)userdata;
    if (status == WGPURequestDeviceStatus_Success)
        context->device = device;
    else
        fprintf(stderr, "[webgpu.c] RequestDevice failed: %s\n", message);
}
void *createGPUContext(void *hInstance, void *hwnd, int width, int height) {
    static WebGPUContext context = {0}; // initialize all fields to zero

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
    context.instance = wgpuCreateInstance(&instDesc);
    assert(context.instance);

    /* WINDOWS SPECIFIC */
    // todo: hide windows specific
    WGPUSurfaceDescriptorFromWindowsHWND chained_desc = {0};
    chained_desc.chain.sType = WGPUSType_SurfaceDescriptorFromWindowsHWND;
    chained_desc.hwnd = hwnd;
    chained_desc.hinstance = hInstance;
    /* WINDOWS SPECIFIC */

    WGPUSurfaceDescriptor surface_desc = {0};
    surface_desc.nextInChain = (const WGPUChainedStruct*)&chained_desc;
    context.surface = wgpuInstanceCreateSurface(context.instance, &surface_desc);
    assert(context.surface);

    WGPURequestAdapterOptions adapter_opts = {0};
    adapter_opts.compatibleSurface = context.surface;
    wgpuInstanceRequestAdapter(context.instance, &adapter_opts, handle_request_adapter, &context);
    assert(context.adapter);
    wgpuAdapterRequestDevice(context.adapter, NULL, handle_request_device, &context);
    assert(context.device);
    context.queue = wgpuDeviceGetQueue(context.device);
    assert(context.queue);

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
        context.uniform_layout = wgpuDeviceCreateBindGroupLayout(context.device, &bglDesc);
    }
    
    // 2) Initialize all the texture group layouts in wgpu
    {
        for (int i = 0; i < NUM_BIND_GROUP_LAYOUTS; i++) {
            context.texture_layouts[i] = wgpuDeviceCreateBindGroupLayout(context.device, &TEXTURE_LAYOUT_DESCRIPTORS[i]);
        }
    }

    // 3) Create a 1×1 texture for empty texture slots
    {
        unsigned char whitePixel[4] = {127,127,127,127};
        WGPUTextureDescriptor td = {0};
        td.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
        td.dimension = WGPUTextureDimension_2D;
        td.format = WGPUTextureFormat_RGBA8Unorm;
        td.size.width  = 1;
        td.size.height = 1;
        td.size.depthOrArrayLayers = 1;
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        context.defaultTexture = wgpuDeviceCreateTexture(context.device, &td);

        // Copy data
        WGPUImageCopyTexture ict = {0};
        ict.texture = context.defaultTexture;
        WGPUTextureDataLayout tdl = {0};
        tdl.bytesPerRow    = 4;
        tdl.rowsPerImage   = 1;
        WGPUExtent3D extent = {1,1,1};
        wgpuQueueWriteTexture(context.queue, &ict, whitePixel, 4, &tdl, &extent);

        // Create a view
        context.defaultTextureView = wgpuTextureCreateView(context.defaultTexture, NULL);
    }

    // Create a depth texture
    {
        WGPUTextureDescriptor depthTextureDesc = {
            .usage = WGPUTextureUsage_RenderAttachment,
            .dimension = WGPUTextureDimension_2D,
            .size = { .width = width, .height = height, .depthOrArrayLayers = 1 },
            .format = WGPUTextureFormat_Depth24Plus, // Or Depth32Float if supported
            .mipLevelCount = 1,
            .sampleCount = 1,
            .nextInChain = NULL,
        };
        WGPUTexture depthTexture = wgpuDeviceCreateTexture(context.device, &depthTextureDesc);
        // 2. Create a texture view for the depth texture
        WGPUTextureViewDescriptor depthViewDesc = {
            .format = depthTextureDesc.format,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = 0,
            .mipLevelCount = 1,
            .baseArrayLayer = 0,
            .arrayLayerCount = 1,
            .nextInChain = NULL,
        };
        WGPUTextureView depthTextureView = wgpuTextureCreateView(depthTexture, &depthViewDesc);
        // 3. Define the depth-stencil state for the pipeline
        WGPUStencilFaceState defaultStencilState = {
            .compare = WGPUCompareFunction_Always,  // Use a valid compare function
            .failOp = WGPUStencilOperation_Keep,
            .depthFailOp = WGPUStencilOperation_Keep,
            .passOp = WGPUStencilOperation_Keep,
        };
        context.depthStencilState = (WGPUDepthStencilState){
            .format = depthTextureDesc.format,
            .depthWriteEnabled = true,
            .depthCompare = WGPUCompareFunction_Less, // Pass fragments with lesser depth values
            // For stencil operations (if unused, defaults are fine):
            .stencilFront = defaultStencilState, // Properly initialized
            .stencilBack = defaultStencilState,  // Same for the back face
            .stencilReadMask = 0xFF,
            .stencilWriteMask = 0xFF,
            .depthBias = 0,
            .depthBiasSlopeScale = 0.0f,
            .depthBiasClamp = 0.0f,
        };
        // 4. depth attachment for render pass
        context.depthAttachment = (WGPURenderPassDepthStencilAttachment){
            .view = depthTextureView,
            .depthLoadOp = WGPULoadOp_Clear,   // Clear depth at start of pass
            .depthStoreOp = WGPUStoreOp_Store,  // Optionally store depth results
            .depthClearValue = 1.0f,            // Clear value (far plane)
            // Set stencil values if using stencil; otherwise, leave them out.
        };
    }

    WGPUSurfaceCapabilities caps = {0};
    wgpuSurfaceGetCapabilities(context.surface, context.adapter, &caps);
    WGPUTextureFormat chosenFormat = WGPUTextureFormat_BGRA8Unorm;
    if (caps.formatCount > 0) {
        chosenFormat = caps.formats[0];
    }

    context.config = (WGPUSurfaceConfiguration){
        .device = context.device,
        .format = chosenFormat,
        .width = width,
        .height = height,
        .usage = WGPUTextureUsage_RenderAttachment,
        .presentMode = WGPUPresentMode_Fifo // *info* use fifo for vsync
    };
    wgpuSurfaceConfigure(context.surface, &context.config);

    context.initialized = true;
    printf("[webgpu.c] wgpuInit done.\n");
    return (void *) &context;
}

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
int createGPUMaterial(WebGPUContext *context, enum MaterialFlags flags, const char *shader) {
    if (!context->initialized) {
        fprintf(stderr, "[webgpu.c] wgpuCreatePipeline called before init!\n");
        return -1;
    }
    int material_id = -1;
    for (int i = 0; i < MAX_MATERIALS; i++) {
        if (!context->materials[i].used) {
            material_id = i;
            context->materials[i] = (GpuMaterial) {0};
            context->materials[i].used = true;
            context->materials[i].flags = flags;
            context->materials[i].current_uniform_offset = 0;
            memset(context->materials[i].uniform_data, 0, UNIFORM_BUFFER_CAPACITY);
            break;
        }
    }
    
    if (material_id < 0) {
        fprintf(stderr, "[webgpu.c] No more pipeline slots!\n");
        return -1;
    }
    
    WGPUShaderModule shaderModule = loadWGSL(context->device, shader);
    if (!shaderModule) {
        fprintf(stderr, "[webgpu.c] Failed to load shader: %s\n", shader);
        context->materials[material_id].used = false;
        return -1;
    }

    GpuMaterial *material = &context->materials[material_id];
    material->vertex_layout_index = get_vertex_layout_index(flags);
    material->texture_layout_index = get_texture_layout_index(flags);

    // Create a pipeline layout with 2 bind groups:
    // group 0: uniform buffer, group 1: textures. 
    WGPUBindGroupLayout bgls[2] = { context->uniform_layout, context->texture_layouts[get_texture_layout_index(flags)] };
    WGPUPipelineLayoutDescriptor layoutDesc = {0};
    layoutDesc.bindGroupLayoutCount = 2;
    layoutDesc.bindGroupLayouts = bgls;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(context->device, &layoutDesc);
    assert(pipelineLayout);
    
    WGPURenderPipelineDescriptor rpDesc = {0};
    rpDesc.layout = pipelineLayout;
    
    // Vertex stage.
    rpDesc.vertex.module = shaderModule;
    rpDesc.vertex.entryPoint = "vs_main";

    rpDesc.vertex.bufferCount = 2;
    rpDesc.vertex.buffers = VERTEX_LAYOUTS[get_vertex_layout_index(flags)];
    
    // Fragment stage.
    WGPUFragmentState fragState = {0};
    fragState.module = shaderModule;
    fragState.entryPoint = "fs_main";
    fragState.targetCount = 1;
    WGPUColorTargetState colorTarget = {0};
    colorTarget.format = context->config.format;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    // --- enable alpha blending ---
    if (flags & USE_ALPHA) {
        colorTarget.blend = (WGPUBlendState[1]) {(WGPUBlendState){
            .color = (WGPUBlendComponent){
                .srcFactor = WGPUBlendFactor_SrcAlpha,
                .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
                .operation = WGPUBlendOperation_Add,
            },
            .alpha = (WGPUBlendComponent){
                .srcFactor = WGPUBlendFactor_One,
                .dstFactor = WGPUBlendFactor_Zero,
                .operation = WGPUBlendOperation_Add,
            }
        }};
    }
    fragState.targets = &colorTarget;
    rpDesc.fragment = &fragState;
    
    WGPUPrimitiveState prim = {0};
    prim.topology = WGPUPrimitiveTopology_TriangleList; // *info* use LineStrip to see the wireframe (line width?)
    //prim.cullMode = WGPUCullMode_Back;
    prim.frontFace = WGPUFrontFace_CW;
    rpDesc.primitive = prim;
    WGPUMultisampleState ms = {0};
    ms.count = 1;
    ms.mask = 0xFFFFFFFF;
    rpDesc.multisample = ms;
    // add depth texture
    rpDesc.depthStencil = &context->depthStencilState;

    // todo: this has exception when running with windows compiler...
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(context->device, &rpDesc);
    material->pipeline = pipeline;
    
    // Create uniform buffer.
    WGPUBufferDescriptor ubDesc = {0};
    ubDesc.size = UNIFORM_BUFFER_CAPACITY;
    ubDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    material->uniformBuffer = wgpuDeviceCreateBuffer(context->device, &ubDesc);
    assert(material->uniformBuffer);
    
    // Create uniform bind group.
    WGPUBindGroupEntry uEntry = {0};
    uEntry.binding = 0;
    uEntry.buffer = material->uniformBuffer;
    uEntry.offset = 0;
    uEntry.size = UNIFORM_BUFFER_CAPACITY;
    WGPUBindGroupDescriptor uBgDesc = {0};
    uBgDesc.layout = context->uniform_layout;
    uBgDesc.entryCount = 1;
    uBgDesc.entries = &uEntry;
    material->uniformBindGroup = wgpuDeviceCreateBindGroup(context->device, &uBgDesc);
    
    // Create a sampler
    WGPUSamplerDescriptor samplerDesc = {0};
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    samplerDesc.lodMinClamp = 0;
    samplerDesc.lodMaxClamp = 0;
    samplerDesc.maxAnisotropy = 1;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    material->textureSampler = wgpuDeviceCreateSampler(context->device, &samplerDesc);
    assert(material->textureSampler != NULL);
    
    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);
    
    printf("[webgpu.c] Created pipeline %d from shader: %s\n", material_id, shader);
    return material_id;
}

int createGPUMesh(WebGPUContext *context, int material_id, void *v, int vc, void *i, int ic, void *ii, int iic) {
    if (!context->initialized) {
        fprintf(stderr, "[webgpu.c] wgpuCreateInstancedMesh called before init!\n");
        return -1;
    }
    if (material_id < 0 || material_id >= MAX_MATERIALS || !context->materials[material_id].used) {
        fprintf(stderr, "[webgpu.c] Invalid pipeline ID %d!\n", material_id);
        return -1;
    }
    int mesh_id = -1;
    for (int i = 0; i < MAX_MESHES; i++) {
        if (!context->meshes[i].used) {
            mesh_id = i;
            context->meshes[i].used = true;
            break;
        }
    }
    if (mesh_id < 0) {
        fprintf(stderr, "[webgpu.c] No more mesh slots!\n");
        return -1;
    }
    
    GpuMesh *mesh = &context->meshes[mesh_id];
    GpuMaterial material = context->materials[material_id];
    // Create vertex buffer (same as in wgpuCreateMesh)
    size_t vertexDataSize = VERTEX_LAYOUTS[material.vertex_layout_index][0].arrayStride * vc;
    WGPUBufferDescriptor vertexBufDesc = {0};
    vertexBufDesc.size = vertexDataSize;
    vertexBufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
    WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(context->device, &vertexBufDesc);
    assert(vertexBuffer);
    wgpuQueueWriteBuffer(context->queue, vertexBuffer, 0, v, vertexDataSize);
    mesh->vertexBuffer = vertexBuffer;
    mesh->vertexCount = vc;
    
    // Create index buffer
    size_t indexDataSize = sizeof(uint32_t) * ic;
    WGPUBufferDescriptor indexBufDesc = {0};
    indexBufDesc.size = indexDataSize;
    indexBufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index;
    WGPUBuffer indexBuffer = wgpuDeviceCreateBuffer(context->device, &indexBufDesc);
    assert(indexBuffer);
    wgpuQueueWriteBuffer(context->queue, indexBuffer, 0, i, indexDataSize);
    mesh->indexBuffer = indexBuffer;
    mesh->indexCount = ic;
    
    // Create instance buffer
    size_t instanceDataSize = VERTEX_LAYOUTS[material.vertex_layout_index][1].arrayStride * iic;
    WGPUBufferDescriptor instBufDesc = {0};
    instBufDesc.size = instanceDataSize;
    instBufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
    WGPUBuffer instanceBuffer = wgpuDeviceCreateBuffer(context->device, &instBufDesc);
    assert(instanceBuffer);
    wgpuQueueWriteBuffer(context->queue, instanceBuffer, 0, ii, instanceDataSize);
    mesh->instanceBuffer = instanceBuffer;
    mesh->instances = ii;
    mesh->instance_count = iic;

    // Initialize all available textures to default
    mesh->textureCount = 0;
    // todo: memory leak, we need to free this malloc stuff
    // todo: would it make more sense to just have a few separate structs where the size is fully known up front
    // todo: then we never have to malloc, and can put everything in a big static array
    int max_textures = TEXTURE_LAYOUT_DESCRIPTORS[material.texture_layout_index].entryCount-1;
    mesh->textureObjects = malloc(sizeof(WGPUTexture) * max_textures);
    mesh->textureViews = malloc(sizeof(WGPUTextureView) * max_textures);
    for (int i=0; i<max_textures; i++) {
        mesh->textureObjects[i] = context->defaultTexture;
        mesh->textureViews[i]   = context->defaultTextureView;
    }
    
    {
        int totalEntries = max_textures + 1; // entry 0 is the sampler
        WGPUBindGroupEntry *entries = calloc(totalEntries, sizeof(WGPUBindGroupEntry));
        // Sampler at binding=0
        entries[0].binding = 0;
        entries[0].sampler = material.textureSampler;
        for (int i=0; i<max_textures; i++) {
            entries[i+1].binding = i + 1;
            entries[i+1].textureView = mesh->textureViews[i];
        }
        assert(material.textureSampler != NULL);
        for (int i = 0; i < max_textures; i++) {
            assert(mesh->textureViews[i] != NULL);
        }

        if (mesh->textureBindGroup) {
            wgpuBindGroupRelease(mesh->textureBindGroup);
        }
        WGPUBindGroupDescriptor bgDesc = {0};
        bgDesc.layout     = context->texture_layouts[material.texture_layout_index];
        bgDesc.entryCount = totalEntries;
        bgDesc.entries    = entries;
        mesh->textureBindGroup = wgpuDeviceCreateBindGroup(context->device, &bgDesc);

        free(entries);
    }
    
    mesh->material_id = material_id;
    printf("[webgpu.c] Created instanced mesh %d with %d vertices, %d indices, and %d instances for pipeline %d\n",
           mesh_id, vc, ic, iic, material_id);
    return mesh_id;
}

int createGPUTexture(WebGPUContext *context, int mesh_id, void *data, int w, int h) {
    GpuMesh* mesh = &context->meshes[mesh_id];
    GpuMaterial* material = &context->materials[mesh->material_id];
    int max_textures = TEXTURE_LAYOUT_DESCRIPTORS[material->texture_layout_index].entryCount-1;
    if (mesh->textureCount >= max_textures) {
        fprintf(stderr, "No more texture slots in mesh!");
        return -1;
    }
    int slot = mesh->textureCount; // e.g. 0 => binding=1, etc.

    // Create texture with the given dimensions and RGBA format.
    WGPUTextureDescriptor td = {0};
    td.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
    td.dimension = WGPUTextureDimension_2D;
    td.format    = WGPUTextureFormat_RGBA8Unorm;
    td.size.width  = w;
    td.size.height = h;
    td.size.depthOrArrayLayers = 1;
    td.mipLevelCount = 1;
    td.sampleCount   = 1;
    WGPUTexture tex = wgpuDeviceCreateTexture(context->device, &td);

    // Upload the pixel data.
    WGPUImageCopyTexture ict = {0};
    ict.texture = tex;
    WGPUTextureDataLayout tdl = {0};
    tdl.bytesPerRow  = 4 * w; // 4 bytes per pixel (RGBA)
    tdl.rowsPerImage = h;
    WGPUExtent3D ext = { .width = (uint32_t)w, .height = (uint32_t)h, .depthOrArrayLayers = 1 };
    wgpuQueueWriteTexture(context->queue, &ict, data, (size_t)(4 * w * h), &tdl, &ext);

    // Create view
    WGPUTextureView view = wgpuTextureCreateView(tex, NULL);

    mesh->textureObjects[slot] = tex;
    mesh->textureViews[slot]   = view;
    mesh->textureCount++;

    // Now rebuild the bind group
    int totalEntries = 1 + max_textures; // + 1 is because we have at index 0 the sampler binding
    WGPUBindGroupEntry* e = calloc(totalEntries, sizeof(WGPUBindGroupEntry));
    e[0].binding = 0;
    e[0].sampler = material->textureSampler;
    for (int i=0; i<max_textures; i++) {
        e[i+1].binding = i+1;
        e[i+1].textureView = (i < mesh->textureCount) 
                             ? mesh->textureViews[i]
                             : context->defaultTextureView;
    }
    if (mesh->textureBindGroup) {
        wgpuBindGroupRelease(mesh->textureBindGroup);
    }
    WGPUBindGroupDescriptor bgDesc = {0};
    bgDesc.layout     = context->texture_layouts[material->texture_layout_index];
    bgDesc.entryCount = totalEntries;
    bgDesc.entries    = e;
    mesh->textureBindGroup = wgpuDeviceCreateBindGroup(context->device, &bgDesc);
    free(e);

    printf("Added texture to mesh %d at slot %d (binding=%d)\n", mesh_id, slot, slot+1);

    return slot;
}


#pragma region UNIFORMS
int addGPUUniform(WebGPUContext *context, int material_id, const void* data, int data_size) {
    GpuMaterial *material = &context->materials[material_id];
    // Inline alignment determination using ternary operators
    int alignment = (data_size <= 4) ? 4 :
                    (data_size <= 8) ? 8 :
                    16; // Default for vec3, vec4, mat4x4, or larger
    // Align the offset to the correct boundary (based on WGSL rules)
    int aligned_offset = (material->current_uniform_offset + (alignment - 1)) & ~(alignment - 1);
    // Check if the new offset exceeds buffer capacity
    if (aligned_offset + data_size > UNIFORM_BUFFER_CAPACITY) {
        // todo: print warning on screen or in log that this failed
        return -1;
    }
    // Copy the data into the aligned buffer
    memcpy(material->uniform_data + aligned_offset, data, data_size);
    // Update the current offset
    material->current_uniform_offset = aligned_offset + data_size;
    // todo: print on screen that uniform changed
    return aligned_offset;
}

void setGPUUniformValue(WebGPUContext *context, int material_id, int offset, const void* data, int dataSize) {
    GpuMaterial *material = &context->materials[material_id];
    if (offset < 0 || offset + dataSize > material->current_uniform_offset) {
        // todo: print warning on screen or in log that this failed
        return;
    }
    memcpy(material->uniform_data + offset, data, dataSize);
}
#pragma endregion


void setGPUInstanceBuffer(WebGPUContext *context, int mesh_id, void* ii, int iic) {
    // freeing the previous buffer is the responsibility of the caller
    GpuMesh *mesh = &context->meshes[mesh_id];
    mesh->instances = ii;
    mesh->instance_count = iic;
}

static void fenceCallback(WGPUQueueWorkDoneStatus status, WGPU_NULLABLE void * userdata) {
    bool *done = (bool*)userdata;
    *done = true;
}
static float fenceAndWait(WebGPUContext *context) {
    volatile bool workDone = false;

    // Request notification when the GPU work is done.
    wgpuQueueOnSubmittedWorkDone(context->queue, fenceCallback, (void*)&workDone);

    // Busy-wait until the flag is set.
    int time_before_ns = time(NULL);
    while (!workDone) {
        wgpuDevicePoll(context->device, false, NULL);
        // Sleep(0);
    }
    int time_after_ns = time(NULL);
    float ms_waited_on_gpu = ((float) (time_before_ns / 1000) / (float) (time_after_ns / 1000));
    return ms_waited_on_gpu;
}
float drawGPUFrame(WebGPUContext *context) {
    // Start the frame.
    wgpuSurfaceGetCurrentTexture(context->surface, &context->currentSurfaceTexture);
    if (context->currentSurfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success)
        return 0.0f;
    context->currentView = wgpuTextureCreateView(context->currentSurfaceTexture.texture, NULL);
    WGPUCommandEncoderDescriptor encDesc = {0};
    context->currentEncoder = wgpuDeviceCreateCommandEncoder(context->device, &encDesc);
    WGPURenderPassColorAttachment colorAtt = {0};
    colorAtt.view = context->currentView;
    colorAtt.loadOp = WGPULoadOp_Clear;
    colorAtt.storeOp = WGPUStoreOp_Store;
    colorAtt.clearValue = (WGPUColor){0.1, 0.2, 0.3, 1.0};
    WGPURenderPassDescriptor passDesc = {0};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAtt;
    passDesc.depthStencilAttachment = &context->depthAttachment,
    context->currentPass = wgpuCommandEncoderBeginRenderPass(context->currentEncoder, &passDesc);

    // Loop through all materials and draw each one.
    for (int material_id = 0; material_id < MAX_MATERIALS; material_id++) {
        if (context->materials[material_id].used) {

            GpuMaterial *gpu_material = &context->materials[material_id];
            // Write CPU–side uniform data to GPU. // todo: does every material need to update uniforms every frame?
            wgpuQueueWriteBuffer(context->queue, gpu_material->uniformBuffer, 0, gpu_material->uniform_data, UNIFORM_BUFFER_CAPACITY);
            // Bind uniform bind group (group 0).
            // todo: potentially we could do per-mesh uniform values like we do for textures
            wgpuRenderPassEncoderSetBindGroup(context->currentPass, 0, gpu_material->uniformBindGroup, 0, NULL);
            // Set the render pipeline.
            wgpuRenderPassEncoderSetPipeline(context->currentPass, gpu_material->pipeline);

            // Iterate over all meshes that use this material. // todo: more efficient if material has array of meshes instead
            for (int i = 0; i < MAX_MESHES; i++) {
                if (context->meshes[i].used && context->meshes[i].material_id == material_id) {
                    // If the material requires instance data updates, update the instance buffer.
                    if (gpu_material->flags & UPDATE_INSTANCES) {
                        context->meshes[i].instance_count = context->meshes[i].instance_count;
                        size_t instanceDataSize = VERTEX_LAYOUTS[gpu_material->vertex_layout_index][1].arrayStride *
                                                  context->meshes[i].instance_count;
                        // write RAM instances to GPU instances
                        wgpuQueueWriteBuffer(context->queue,context->meshes[i].instanceBuffer,0,context->meshes[i].instances,instanceDataSize);
                    }
                    // todo: some meshes could share the same textures so we could avoid to switch always
                    wgpuRenderPassEncoderSetBindGroup(context->currentPass, 1, context->meshes[i].textureBindGroup, 0, NULL); // group 1 for textures
                    // Instanced mesh: bind its vertex + instance buffer (slots 0 and 1) and draw with instance_count.
                    wgpuRenderPassEncoderSetVertexBuffer(context->currentPass, 0, context->meshes[i].vertexBuffer, 0, WGPU_WHOLE_SIZE);
                    wgpuRenderPassEncoderSetVertexBuffer(context->currentPass, 1, context->meshes[i].instanceBuffer, 0, WGPU_WHOLE_SIZE);
                    wgpuRenderPassEncoderSetIndexBuffer(context->currentPass, context->meshes[i].indexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
                    wgpuRenderPassEncoderDrawIndexed(context->currentPass, context->meshes[i].indexCount,context->meshes[i].instance_count,0,0,0);
                }
            }
        }
    }

    // End the render pass.
    wgpuRenderPassEncoderEnd(context->currentPass);
    wgpuRenderPassEncoderRelease(context->currentPass);
    context->currentPass = NULL;

    // Finish command encoding and submit.
    WGPUCommandBufferDescriptor cmdDesc = {0};
    WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(context->currentEncoder, &cmdDesc);
    wgpuCommandEncoderRelease(context->currentEncoder);
    wgpuQueueSubmit(context->queue, 1, &cmdBuf);

    // Wait on the fence to measure GPU work time.
    float ms_waited_on_gpu = fenceAndWait(context);

    // Release command buffer and present the surface.
    wgpuCommandBufferRelease(cmdBuf);
    wgpuSurfacePresent(context->surface);
    wgpuTextureViewRelease(context->currentView);
    context->currentView = NULL;
    wgpuTextureRelease(context->currentSurfaceTexture.texture);
    context->currentSurfaceTexture.texture = NULL;

    return ms_waited_on_gpu;
}
