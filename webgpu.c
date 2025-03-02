#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "wgpu.h"

#pragma region PREDEFINED DATA
#define STANDARD_MAX_TEXTURES 4
static const WGPUBindGroupLayoutDescriptor TEXTURE_LAYOUT_DESCRIPTOR = {
    .entryCount = 1 + STANDARD_MAX_TEXTURES,
    .entries = (WGPUBindGroupLayoutEntry[]){
        // Sampler at binding = 0
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        },
        #define TEXTURE_2D_ARRAY {.sampleType = WGPUTextureSampleType_Float, .viewDimension = WGPUTextureViewDimension_2DArray, .multisampled = false}
        {.binding = 1, .visibility = WGPUShaderStage_Fragment, .texture = TEXTURE_2D_ARRAY},
        {.binding = 2, .visibility = WGPUShaderStage_Fragment, .texture = TEXTURE_2D_ARRAY},
        {.binding = 3, .visibility = WGPUShaderStage_Fragment, .texture = TEXTURE_2D_ARRAY},
        {.binding = 4, .visibility = WGPUShaderStage_Fragment, .texture = TEXTURE_2D_ARRAY},
    }
};
#include "game_data.c" // todo: just one big file instead
static const WGPUVertexBufferLayout VERTEX_LAYOUT[2] = {
    {   // Vertex layout
        .arrayStride = 48,
        .stepMode = WGPUVertexStepMode_Vertex,
        .attributeCount = 7,
        .attributes = (const WGPUVertexAttribute[]) {
            { .format = WGPUVertexFormat_Uint32x4, .offset = 0,  .shaderLocation = 0 }, // data[4]  (16 bytes)
            { .format = WGPUVertexFormat_Float32x3, .offset = 16, .shaderLocation = 1 }, // position[3] (12 bytes)
            { .format = WGPUVertexFormat_Unorm8x4,   .offset = 28, .shaderLocation = 2 }, // normal[4]  (4 bytes)
            { .format = WGPUVertexFormat_Unorm8x4,   .offset = 32, .shaderLocation = 3 }, // tangent[4] (4 bytes)
            { .format = WGPUVertexFormat_Unorm16x2,  .offset = 36, .shaderLocation = 4 }, // uv[2]      (4 bytes)
            { .format = WGPUVertexFormat_Unorm8x4,   .offset = 40, .shaderLocation = 5 }, // bone_weights[4] (4 bytes)
            { .format = WGPUVertexFormat_Uint8x4,    .offset = 44, .shaderLocation = 6 }  // bone_indices[4] (4 bytes)
        }
    },
    {   // Instance layout
        .arrayStride = 96,
        .stepMode = WGPUVertexStepMode_Instance,
        .attributeCount = 9,
        .attributes = (const WGPUVertexAttribute[]) {
            { .format = WGPUVertexFormat_Float32x4, .offset = 0,   .shaderLocation = 7  }, // transform row0 (16 bytes)
            { .format = WGPUVertexFormat_Float32x4, .offset = 16,  .shaderLocation = 8  }, // transform row1 (16 bytes)
            { .format = WGPUVertexFormat_Float32x4, .offset = 32,  .shaderLocation = 9  }, // transform row2 (16 bytes)
            { .format = WGPUVertexFormat_Float32x4, .offset = 48,  .shaderLocation = 10 }, // transform row3 (16 bytes)
            { .format = WGPUVertexFormat_Uint32x3,  .offset = 64,  .shaderLocation = 11 }, // data[3] (12 bytes)
            { .format = WGPUVertexFormat_Unorm16x4, .offset = 76,  .shaderLocation = 12 }, // norms[4] (8 bytes)
            { .format = WGPUVertexFormat_Uint32,    .offset = 84,  .shaderLocation = 13 }, // animation (4 bytes)
            { .format = WGPUVertexFormat_Float32,   .offset = 88,  .shaderLocation = 14 }, // animation_phase (4 bytes)
            { .format = WGPUVertexFormat_Unorm16x2, .offset = 92,  .shaderLocation = 15 }  // atlas_uv[2] (4 bytes)
        }
    }
};
#pragma endregion

#pragma region STRUCT DEFINITIONS
typedef struct {
    WGPURenderPipeline pipeline;
    bool               used;

    WGPUSampler        textureSampler;
    
    WGPUBindGroup      globalUniformBindGroup;
    WGPUBuffer         globalUniformBuffer;
    unsigned char global_uniform_data[GLOBAL_UNIFORM_CAPACITY]; // global uniform data in RAM
    int global_uniform_offset;
    
    WGPUBindGroup      meshUniformBindGroup;
    WGPUBuffer         meshUniformBuffer;

    WGPUBindGroup      textureBindGroup;
    WGPUTexture        *textureObjects; // static arrays instead with max_textures and default white pixel (?)
    WGPUTextureView    *textureViews;
    int                textureCount;
} GpuMaterial; // todo: no more material for pipeline, material should become the mesh uniform values instead

typedef struct {
    bool       used;
    int        material_id;

    WGPUBuffer vertexBuffer;
    int        vertexCount;
    WGPUBuffer indexBuffer;
    int        indexCount;
    WGPUBuffer instanceBuffer;
    void      *instances; // instances in RAM (verts and indices are not kept in RAM) // todo: Instance instead of void
    int        instance_count;
    unsigned char uniform_data[MESH_UNIFORM_CAPACITY]; // uniform data in RAM // todo: init
    int mesh_uniform_offset;

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
    WGPUTexture      defaultTextureArray;
    WGPUTextureView  defaultTextureView;
    // global depth texture
    WGPUDepthStencilState depthStencilState; 
    WGPURenderPassDepthStencilAttachment depthAttachment;
    // One standard uniform layout for all pipelines
    WGPUBindGroupLayout global_uniform_layout;
    WGPUBindGroupLayout mesh_uniforms_layout;
    WGPUBindGroupLayout texture_layout;
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

    // Create the global uniform bind group layout
    {
        WGPUBindGroupLayoutEntry entry = {0};
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        entry.buffer.type = WGPUBufferBindingType_Uniform;
        entry.buffer.minBindingSize = GLOBAL_UNIFORM_CAPACITY;
        entry.buffer.hasDynamicOffset = 0;
        WGPUBindGroupLayoutDescriptor bglDesc = {0};
        bglDesc.entryCount = 1;
        bglDesc.entries = &entry;
        context.global_uniform_layout = wgpuDeviceCreateBindGroupLayout(context.device, &bglDesc);
    }

    // Create the mesh uniforms bind group layout
    {
        WGPUBindGroupLayoutEntry entry = {0};
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        entry.buffer.type = WGPUBufferBindingType_Uniform;
        entry.buffer.minBindingSize = MESH_UNIFORM_CAPACITY;
        entry.buffer.hasDynamicOffset = 1; // don't forget this (!)
        WGPUBindGroupLayoutDescriptor bglDesc = {0};
        bglDesc.entryCount = 1;
        bglDesc.entries = &entry;
        context.mesh_uniforms_layout = wgpuDeviceCreateBindGroupLayout(context.device, &bglDesc);
    }
    
    // Create the texture bind group layout
    {
        context.texture_layout = wgpuDeviceCreateBindGroupLayout(context.device, &TEXTURE_LAYOUT_DESCRIPTOR);
    }

    // Create a 1×1 texture to use as default value for empty texture slots
    {
        #define TEXTURE_WIDTH 1200
        #define TEXTURE_HEIGHT 1200
        unsigned char whitePixel[4] = {127,127,127,127};
        WGPUTextureDescriptor td = {0};
        td.label = "Texture Array",
        td.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
        td.dimension = WGPUTextureDimension_2D;
        td.format = WGPUTextureFormat_RGBA8Unorm;
        td.size.width  = TEXTURE_WIDTH;
        td.size.height = TEXTURE_HEIGHT;
        td.size.depthOrArrayLayers = MAX_MESH_CONFIGS; // nr of elements in texture array
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        context.defaultTextureArray = wgpuDeviceCreateTexture(context.device, &td);

        // Create a view
        WGPUTextureViewDescriptor viewDesc = {
            .label = "Texture Array View",
            .format = WGPUTextureFormat_RGBA8Unorm,
            .dimension = WGPUTextureViewDimension_2DArray,  // 2D Array
            .baseMipLevel = 0,
            .mipLevelCount = 1,
            .baseArrayLayer = 0,
            .arrayLayerCount = MAX_MESH_CONFIGS,
            .aspect = WGPUTextureAspect_All,
        };
        context.defaultTextureView = wgpuTextureCreateView(context.defaultTextureArray, &viewDesc);

        // Copy data
        for (uint32_t layer = 0; layer < MAX_MESH_CONFIGS; ++layer) {
            WGPUImageCopyTexture ict = {0};
            ict.texture = context.defaultTextureArray;
            ict.origin = (WGPUOrigin3D) {0, 0, layer};
            WGPUTextureDataLayout tdl = {0};
            tdl.bytesPerRow    = TEXTURE_WIDTH * 4; // == width of texture * 4 bytes per pixel
            tdl.rowsPerImage   = TEXTURE_HEIGHT; // == height of texture
            // Write a much smaller 1x1 texture into the layer
            wgpuQueueWriteTexture(context.queue, &ict, whitePixel, 1 * 1 * 4, &tdl, &(WGPUExtent3D){1, 1, 1});
        }
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
    WGPUSupportedLimits limits = {0}; wgpuDeviceGetLimits(context.device, &limits);
    printf("[webgpu.c] max uniform buffer size: %d\n", limits.limits.maxUniformBufferBindingSize);
    printf("[webgpu.c] max buffer size: %d\n", limits.limits.maxBufferSize);
    printf("[webgpu.c] max nr of textures in array: %d\n", limits.limits.maxTextureArrayLayers);
    printf("[webgpu.c] max texture 2D dimension: %d\n", limits.limits.maxTextureDimension2D);
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
int createGPUMaterial(WebGPUContext *context, const char *shader) {
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
            context->materials[i].global_uniform_offset = 0;
            memset(context->materials[i].global_uniform_data, 0, GLOBAL_UNIFORM_CAPACITY);
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

    // Create a pipeline layout with 3 bind groups:
    // group 0 and 1: uniform buffers, group 2: textures
    WGPUBindGroupLayout bgls[3] = { context->global_uniform_layout, context->mesh_uniforms_layout, context->texture_layout };
    WGPUPipelineLayoutDescriptor layoutDesc = {0};
    layoutDesc.bindGroupLayoutCount = 3;
    layoutDesc.bindGroupLayouts = bgls;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(context->device, &layoutDesc);
    assert(pipelineLayout);
    
    WGPURenderPipelineDescriptor rpDesc = {0};
    rpDesc.layout = pipelineLayout;
    
    // Vertex stage.
    rpDesc.vertex.module = shaderModule;
    rpDesc.vertex.entryPoint = "vs_main";

    rpDesc.vertex.bufferCount = 2;
    rpDesc.vertex.buffers = VERTEX_LAYOUT;
    
    // Fragment stage.
    WGPUFragmentState fragState = {0};
    fragState.module = shaderModule;
    fragState.entryPoint = "fs_main";
    fragState.targetCount = 1;
    WGPUColorTargetState colorTarget = {0};
    colorTarget.format = context->config.format;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    // --- enable alpha blending --- // todo: maybe later if we want water (?)
    // {
    //     colorTarget.blend = (WGPUBlendState[1]) {(WGPUBlendState){
    //         .color = (WGPUBlendComponent){
    //             .srcFactor = WGPUBlendFactor_SrcAlpha,
    //             .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
    //             .operation = WGPUBlendOperation_Add,
    //         },
    //         .alpha = (WGPUBlendComponent){
    //             .srcFactor = WGPUBlendFactor_One,
    //             .dstFactor = WGPUBlendFactor_Zero,
    //             .operation = WGPUBlendOperation_Add,
    //         }
    //     }};
    // }
    fragState.targets = &colorTarget;
    rpDesc.fragment = &fragState;
    
    WGPUPrimitiveState prim = {0};
    prim.topology = WGPUPrimitiveTopology_TriangleList; // *info* use LineStrip to see the wireframe (line width?)
    prim.cullMode = WGPUCullMode_Back;
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
    
    // Create global uniform bind group
    {
        WGPUBufferDescriptor ubDesc = {0};
        ubDesc.size = GLOBAL_UNIFORM_CAPACITY;
        ubDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        material->globalUniformBuffer = wgpuDeviceCreateBuffer(context->device, &ubDesc);

        WGPUBindGroupEntry uEntry = {0};
        uEntry.binding = 0;
        uEntry.buffer = material->globalUniformBuffer;
        uEntry.offset = 0;
        uEntry.size = GLOBAL_UNIFORM_CAPACITY;
        WGPUBindGroupDescriptor uBgDesc = {0};
        uBgDesc.layout = context->global_uniform_layout;
        uBgDesc.entryCount = 1;
        uBgDesc.entries = &uEntry;
        material->globalUniformBindGroup = wgpuDeviceCreateBindGroup(context->device, &uBgDesc);
    }

    // Create mesh uniforms bind group
    {
        WGPUBufferDescriptor ubDesc = {0};
        ubDesc.size = MESH_UNIFORM_BUFFER_TOTAL_SIZE;
        ubDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        material->meshUniformBuffer = wgpuDeviceCreateBuffer(context->device, &ubDesc);

        WGPUBindGroupEntry uEntry = {0};
        uEntry.binding = 0;
        uEntry.buffer = material->meshUniformBuffer;
        uEntry.offset = 0;
        uEntry.size = MESH_UNIFORM_CAPACITY;
        WGPUBindGroupDescriptor uBgDesc = {0};
        uBgDesc.layout = context->mesh_uniforms_layout;
        uBgDesc.entryCount = 1;
        uBgDesc.entries = &uEntry;
        material->meshUniformBindGroup = wgpuDeviceCreateBindGroup(context->device, &uBgDesc);
    }
    
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

    // Initialize all available textures to the default 1x1 white pixel
    material->textureCount = 0;
    // todo: memory leak, we need to free this malloc stuff
    // todo: would it make more sense to just have a few separate structs where the size is fully known up front
    // todo: then we never have to malloc, and can put everything in a big static array
    int max_textures = TEXTURE_LAYOUT_DESCRIPTOR.entryCount-1;
    material->textureObjects = malloc(sizeof(WGPUTexture) * max_textures);
    material->textureViews = malloc(sizeof(WGPUTextureView) * max_textures);
    for (int i=0; i<max_textures; i++) {
        material->textureObjects[i] = context->defaultTextureArray;
        material->textureViews[i]   = context->defaultTextureView;
    }
    
    {
        int totalEntries = max_textures + 1; // entry 0 is the sampler
        WGPUBindGroupEntry *entries = calloc(totalEntries, sizeof(WGPUBindGroupEntry));
        // Sampler at binding=0
        entries[0].binding = 0;
        entries[0].sampler = material->textureSampler;
        for (int i=0; i<max_textures; i++) {
            entries[i+1].binding = i + 1;
            entries[i+1].textureView = material->textureViews[i];
        }
        if (material->textureBindGroup) {
            wgpuBindGroupRelease(material->textureBindGroup);
        }
        WGPUBindGroupDescriptor bgDesc = {0};
        bgDesc.layout     = context->texture_layout;
        bgDesc.entryCount = totalEntries;
        bgDesc.entries    = entries;
        material->textureBindGroup = wgpuDeviceCreateBindGroup(context->device, &bgDesc);

        free(entries);
    }
    
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
    size_t vertexDataSize = VERTEX_LAYOUT[0].arrayStride * vc;
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
    size_t instanceDataSize = VERTEX_LAYOUT[1].arrayStride * iic;
    WGPUBufferDescriptor instBufDesc = {0};
    instBufDesc.size = instanceDataSize;
    instBufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
    WGPUBuffer instanceBuffer = wgpuDeviceCreateBuffer(context->device, &instBufDesc);
    assert(instanceBuffer);
    wgpuQueueWriteBuffer(context->queue, instanceBuffer, 0, ii, instanceDataSize);
    mesh->instanceBuffer = instanceBuffer;
    mesh->instances = ii;
    mesh->instance_count = iic;
    
    mesh->material_id = material_id;
    printf("[webgpu.c] Created instanced mesh %d with %d vertices, %d indices, and %d instances for pipeline %d\n",
           mesh_id, vc, ic, iic, material_id);
    return mesh_id;
}

int createGPUTexture(WebGPUContext *context, int mesh_id, void *data, int w, int h) {
    GpuMesh* mesh = &context->meshes[mesh_id];
    GpuMaterial* material = &context->materials[mesh->material_id];
    int max_textures = TEXTURE_LAYOUT_DESCRIPTOR.entryCount-1;
    if (material->textureCount >= max_textures) {
        fprintf(stderr, "No more texture slots in mesh!");
        return -1;
    }
    int slot = material->textureCount; // e.g. 0 => binding=1, etc.

    // Define destination layer
    WGPUImageCopyTexture dst = {
        .texture = context->defaultTextureArray, // todo; put this texture array in the pipeline instead of the global context
        .mipLevel = 0,
        .origin = {0, 0, 0}, // <-- Change `z` to overwrite a specific layer
        .aspect = WGPUTextureAspect_All,
    };

    // Define source layout
    WGPUTextureDataLayout srcLayout = {
        .offset = 0,
        .bytesPerRow = w * 4,   // w * 4 bytes per pixel
        .rowsPerImage = h,      // Full image height
    };

    // Define texture size (just 1 layer)
    WGPUExtent3D copySize = {
        .width = w,
        .height = h,
        .depthOrArrayLayers = 1, // Only updating one layer!
    };

    // Perform the update
    wgpuQueueWriteTexture(context->queue, &dst, data, w * h * 4, &srcLayout, &copySize);

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
    int aligned_offset = (material->global_uniform_offset + (alignment - 1)) & ~(alignment - 1);
    // Check if the new offset exceeds buffer capacity
    if (aligned_offset + data_size > GLOBAL_UNIFORM_CAPACITY) {
        // todo: print warning on screen or in log that this failed
        return -1;
    }
    // Copy the data into the aligned buffer
    memcpy(material->global_uniform_data + aligned_offset, data, data_size);
    // Update the current offset
    material->global_uniform_offset = aligned_offset + data_size;
    // todo: print on screen that uniform changed
    return aligned_offset;
}

void setGPUUniformValue(WebGPUContext *context, int material_id, int offset, const void* data, int dataSize) {
    GpuMaterial *material = &context->materials[material_id];
    if (offset < 0 || offset + dataSize > material->global_uniform_offset) {
        // todo: print warning on screen or in log that this failed
        return;
    }
    memcpy(material->global_uniform_data + offset, data, dataSize);
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
            wgpuQueueWriteBuffer(context->queue, gpu_material->globalUniformBuffer, 0, gpu_material->global_uniform_data, GLOBAL_UNIFORM_CAPACITY);
            // Bind uniform bind group (group 0).
            // todo: potentially we could do per-mesh uniform values like we do for textures
            wgpuRenderPassEncoderSetBindGroup(context->currentPass, 0, gpu_material->globalUniformBindGroup, 0, NULL); // group 0 for global uniforms
            wgpuRenderPassEncoderSetBindGroup(context->currentPass, 2, context->materials[material_id].textureBindGroup, 0, NULL); // group 2 for textures
            // Set the render pipeline.
            wgpuRenderPassEncoderSetPipeline(context->currentPass, gpu_material->pipeline);

            // Iterate over all meshes that use this material. // todo: more efficient if material has array of meshes instead
            for (int i = 0; i < MAX_MESHES; i++) {
                GpuMesh *mesh = &context->meshes[i];
                if (mesh->used && mesh->material_id == material_id) {
                    uint32_t mesh_uniform_offset = (uint32_t) (i * MESH_UNIFORM_CAPACITY);
                    // todo: mesh settings: UPDATE_INSTANCES, UPDATE_MESH_UNIFORMS
                    // If the mesh requires uniform data updates, update the mesh uniform buffer
                    if (1) {
                        size_t instanceDataSize = VERTEX_LAYOUT[1].arrayStride * mesh->instance_count;
                        wgpuQueueWriteBuffer(context->queue, gpu_material->meshUniformBuffer, mesh_uniform_offset, mesh->instances, MESH_UNIFORM_CAPACITY);
                    }
                    // If the mesh requires instance data updates, update the instance buffer
                    // todo: make this a mesh value, then we don't need to do this for static meshes
                    if (1) {
                        size_t instanceDataSize = VERTEX_LAYOUT[1].arrayStride * mesh->instance_count;
                        // write RAM instances to GPU instances
                        wgpuQueueWriteBuffer(context->queue,mesh->instanceBuffer,0,mesh->instances,instanceDataSize);
                    }
                    // todo: divide meshes by new 'material', reusing the same mesh uniforms for different meshes with the same settings and texture and shader id etc.
                    // Set the dynamic offset to point to the uniform values for this mesh
                    // *info* max size of 64kb -> with 1000 meshes we can have at most 64 bytes of uniform data per mesh (!)
                    wgpuRenderPassEncoderSetBindGroup(context->currentPass, 1, gpu_material->meshUniformBindGroup, 1, &mesh_uniform_offset); // group 1 for per-mesh uniforms
                    // todo: long term: use one big buffer for all meshes' uniform data and use offset per mesh
                    // todo: use one texture array for all pipeline textures instead, and use per-mesh uniform for index
                    // todo: that way we can still differentiate per mesh/instance and never switch bindgroup
                    // Instanced mesh: bind its vertex + instance buffer (slots 0 and 1) and draw with instance_count
                    // todo: could it be possible to set all vertex buffers here and deal with it in the shader (?)
                    wgpuRenderPassEncoderSetVertexBuffer(context->currentPass, 0, mesh->vertexBuffer, 0, WGPU_WHOLE_SIZE);
                    wgpuRenderPassEncoderSetVertexBuffer(context->currentPass, 1, mesh->instanceBuffer, 0, WGPU_WHOLE_SIZE);
                    wgpuRenderPassEncoderSetIndexBuffer(context->currentPass, mesh->indexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
                    wgpuRenderPassEncoderDrawIndexed(context->currentPass, mesh->indexCount,mesh->instance_count,0,0,0);
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
