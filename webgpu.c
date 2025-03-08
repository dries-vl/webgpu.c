#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "wgpu.h"
#include "game_data.h"

#pragma region PREDEFINED DATA
#define MAX_TEXTURES 4
static const WGPUBindGroupLayoutDescriptor TEXTURE_LAYOUT_DESCRIPTOR = {
    .entryCount = 1 + MAX_TEXTURES, // one extra for the sampler
    .entries = (WGPUBindGroupLayoutEntry[]){
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        },
        #define STANDARD_TEXTURE {.sampleType = WGPUTextureSampleType_Float, .viewDimension = WGPUTextureViewDimension_2D, .multisampled = false}
        {.binding = 1, .visibility = WGPUShaderStage_Fragment, .texture = STANDARD_TEXTURE},
        {.binding = 2, .visibility = WGPUShaderStage_Fragment, .texture = STANDARD_TEXTURE},
        {.binding = 3, .visibility = WGPUShaderStage_Fragment, .texture = STANDARD_TEXTURE},
        {.binding = 4, .visibility = WGPUShaderStage_Fragment, .texture = STANDARD_TEXTURE},
    }
};
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
    bool               used;
    int                material_ids[MAX_MATERIALS];
    // pipeline
    WGPURenderPipeline pipeline;
    // global uniforms
    WGPUBindGroup      globalUniformBindGroup;
    WGPUBuffer         globalUniformBuffer;
    unsigned char      global_uniform_data[GLOBAL_UNIFORM_CAPACITY]; // global uniform data in RAM
    int                global_uniform_offset;
    // buffer for material uniforms
    WGPUBindGroup      materialUniformBindGroup;
    WGPUBuffer         materialUniformBuffer;
} Pipeline;

typedef struct {
    bool               used;
    int                pipeline_id;
    int                mesh_ids[MAX_MESHES];
    // textures
    WGPUBindGroup      texture_bindgroup;
    WGPUTextureView    texture_views[MAX_TEXTURES];
    WGPUSampler        texture_sampler;
    WGPUTexture        textures[MAX_TEXTURES];
    int                texture_count; // nr of textures currently set
    // uniforms
    unsigned char      uniform_data[MATERIAL_UNIFORM_CAPACITY]; // uniform data in RAM // todo: init
    int                uniform_offset;
} Material;

typedef struct {
    bool       used;
    int        material_id;
    // buffers
    WGPUBuffer vertexBuffer;
    int        vertexCount;
    WGPUBuffer indexBuffer;
    int        indexCount;
    WGPUBuffer instanceBuffer;
    void      *instances; // instances in RAM (verts and indices are not kept in RAM) // todo: Instance instead of void
    int        instance_count;
} Mesh;

typedef struct {
    bool                     initialized;
    WGPUInstance             instance;
    WGPUSurface              surface;
    WGPUAdapter              adapter;
    WGPUDevice               device;
    WGPUQueue                queue;
    WGPUSurfaceConfiguration config;
    // data
    Pipeline              pipelines[MAX_PIPELINES];
    Material              materials[MAX_MATERIALS];
    Mesh                  meshes[MAX_MESHES];
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
// Code to force select dedicated gpu if possible
WGPUAdapter selectDiscreteGPU(WGPUInstance instance) {
    // First call to get the number of available adapters.
    WGPUInstanceEnumerateAdapterOptions opts = {.backends = WGPUInstanceBackend_All};
    size_t adapterCount = wgpuInstanceEnumerateAdapters(instance, &opts, NULL);
    if (adapterCount == 0) {
        fprintf(stderr, "No adapters found!\n");
        exit(EXIT_FAILURE);
    }

    // Allocate an array to hold the adapter handles.
    WGPUAdapter* adapters = malloc(sizeof(WGPUAdapter) * adapterCount);
    if (!adapters) {
        fprintf(stderr, "Failed to allocate memory for adapters.\n");
        exit(EXIT_FAILURE);
    }

    // Second call: fill the array with adapter handles.
    adapterCount = wgpuInstanceEnumerateAdapters(instance, &opts, adapters);

    WGPUAdapter selectedAdapter = NULL;
    for (size_t i = 0; i < adapterCount; i++) {
        WGPUAdapterInfo info;
        memset(&info, 0, sizeof(info));
        wgpuAdapterGetInfo(adapters[i], &info);
        printf("Adapter %zu: %s, Type: %d\n", i, info.device, info.adapterType);

        // Use the provided enum: select the adapter if it is a discrete GPU.
        if (info.adapterType == WGPUAdapterType_DiscreteGPU) {
            selectedAdapter = adapters[i];
            printf("Selected discrete GPU: %s\n", info.device);
            break;
        }
    }

    if (!selectedAdapter) {
        // Fallback: use the first adapter if no discrete GPU is found.
        selectedAdapter = adapters[0];
        WGPUAdapterInfo info;
        memset(&info, 0, sizeof(info));
        wgpuAdapterGetInfo(selectedAdapter, &info);
        printf("No discrete GPU found; falling back to adapter: %s\n", info.device);
    }

    free(adapters);
    return selectedAdapter;
}
void *createGPUContext(void *hInstance, void *hwnd, int width, int height) {
    static WebGPUContext context = {0}; // initialize all fields to zero

    // Instance creation (your instance extras, etc.)
    WGPUInstanceExtras extras = {0};
    extras.chain.sType = WGPUSType_InstanceExtras;
    extras.backends   = WGPUInstanceBackend_GL;
    extras.flags      = WGPUInstanceFlag_DiscardHalLabels;
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
    adapter_opts.powerPreference = WGPUPowerPreference_HighPerformance;
    //wgpuInstanceRequestAdapter(context.instance, &adapter_opts, handle_request_adapter, &context);
    context.adapter = selectDiscreteGPU(context.instance); // code to force select dedicated gpu
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
        entry.buffer.minBindingSize = MATERIAL_UNIFORM_CAPACITY;
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
            .depthStoreOp = WGPUStoreOp_Discard,  // Optionally store depth results
            .depthClearValue = 1.0f,            // Clear value (far plane)
            // Set stencil values if using stencil; otherwise, leave them out.
        };
    }

    WGPUSurfaceCapabilities caps = {0};
    wgpuSurfaceGetCapabilities(context.surface, context.adapter, &caps);
    WGPUTextureFormat chosenFormat = WGPUTextureFormat_RGBA8Unorm;
    // *it used to be Rgba8UnormSrgb, which was slightly slower somehow*
    // if (caps.formatCount > 0) { // selects Rgba8UnormSrgb it seems
    //     chosenFormat = caps.formats[0];
    // }

    context.config = (WGPUSurfaceConfiguration){
        .device = context.device,
        .format = chosenFormat,
        .width = width,
        .height = height,
        .usage = WGPUTextureUsage_RenderAttachment,
        .alphaMode = WGPUCompositeAlphaMode_Opaque,
        .presentMode = WGPUPresentMode_Immediate // *info* use fifo for vsync
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
// todo: only one pipeline, init in context creation, remove this function (?)
int createGPUPipeline(void *context_ptr, const char *shader) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    if (!context->initialized) {
        fprintf(stderr, "[webgpu.c] wgpuCreatePipeline called before init!\n");
        return -1;
    }
    int pipeline_id = -1;
    for (int i = 0; i < MAX_PIPELINES; i++) {
        if (!context->pipelines[i].used) {
            pipeline_id = i;
            context->pipelines[i] = (Pipeline) {0};
            // set all material indices to -1, which means not used
            for (int j = 0; j < MAX_MATERIALS; j++) context->pipelines[i].material_ids[j] = -1;
            context->pipelines[i].used = true;
            break;
        }
    }
    
    if (pipeline_id < 0) {
        fprintf(stderr, "[webgpu.c] No more pipeline slots!\n");
        return -1;
    }
    
    WGPUShaderModule shaderModule = loadWGSL(context->device, shader);
    if (!shaderModule) {
        fprintf(stderr, "[webgpu.c] Failed to load shader: %s\n", shader);
        context->pipelines[pipeline_id].used = false;
        return -1;
    }

    Pipeline *pipeline = &context->pipelines[pipeline_id];

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
     {
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
    prim.cullMode = WGPUCullMode_Back;
    prim.frontFace = WGPUFrontFace_CCW;
    rpDesc.primitive = prim;
    WGPUMultisampleState ms = {0};
    ms.count = 1;
    ms.mask = 0xFFFFFFFF;
    rpDesc.multisample = ms;
    // add depth texture
    rpDesc.depthStencil = &context->depthStencilState;

    // todo: this has exception when running with windows compiler...
    WGPURenderPipeline gpu_pipeline = wgpuDeviceCreateRenderPipeline(context->device, &rpDesc);
    pipeline->pipeline = gpu_pipeline;
    
    // Create global uniform bind group
    {
        WGPUBufferDescriptor ubDesc = {0};
        ubDesc.size = GLOBAL_UNIFORM_CAPACITY;
        ubDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        pipeline->globalUniformBuffer = wgpuDeviceCreateBuffer(context->device, &ubDesc);

        WGPUBindGroupEntry uEntry = {0};
        uEntry.binding = 0;
        uEntry.buffer = pipeline->globalUniformBuffer;
        uEntry.offset = 0;
        uEntry.size = GLOBAL_UNIFORM_CAPACITY;
        WGPUBindGroupDescriptor uBgDesc = {0};
        uBgDesc.layout = context->global_uniform_layout;
        uBgDesc.entryCount = 1;
        uBgDesc.entries = &uEntry;
        pipeline->globalUniformBindGroup = wgpuDeviceCreateBindGroup(context->device, &uBgDesc);
    }

    // Create mesh uniforms bind group
    {
        WGPUBufferDescriptor ubDesc = {0};
        ubDesc.size = MATERIALS_UNIFORM_BUFFER_TOTAL_SIZE;
        ubDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        pipeline->materialUniformBuffer = wgpuDeviceCreateBuffer(context->device, &ubDesc);

        WGPUBindGroupEntry uEntry = {0};
        uEntry.binding = 0;
        uEntry.buffer = pipeline->materialUniformBuffer;
        uEntry.offset = 0;
        uEntry.size = MATERIAL_UNIFORM_CAPACITY;
        WGPUBindGroupDescriptor uBgDesc = {0};
        uBgDesc.layout = context->mesh_uniforms_layout;
        uBgDesc.entryCount = 1;
        uBgDesc.entries = &uEntry;
        pipeline->materialUniformBindGroup = wgpuDeviceCreateBindGroup(context->device, &uBgDesc);
    }
    
    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);
    
    printf("[webgpu.c] Created pipeline %d from shader: %s\n", pipeline_id, shader);
    return pipeline_id;
}

int createGPUMesh(void *context_ptr, int pipeline_id, void *v, int vc, void *i, int ic, void *ii, int iic) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    if (!context->initialized) {
        fprintf(stderr, "[webgpu.c] wgpuCreateInstancedMesh called before init!\n");
        return -1;
    }
    if (pipeline_id < 0 || pipeline_id >= MAX_PIPELINES || !context->pipelines[pipeline_id].used) {
        fprintf(stderr, "[webgpu.c] Invalid pipeline ID %d!\n", pipeline_id);
        return -1;
    }
    Pipeline *pipeline = &context->pipelines[pipeline_id];
    int material_id = -1;
    for (int i = 0; i < MAX_MATERIALS; i++) {
        if (!context->materials[i].used) {
            material_id = i;
            context->materials[i] = (Material) {0}; // init material
            // set all mesh indices to -1, which means not used
            for (int j = 0; j < MAX_MESHES; j++) context->materials[i].mesh_ids[j] = -1;
            // set the first available material index in the list
            // todo: on deleting a material, loop over the list and fill up the gap with the furthest remaining material
            // todo: so that there is no gap of -1 that blocks everything behind it from being rendered
            for (int j = 0; j < MAX_MATERIALS; j++) {
                if (pipeline->material_ids[j] == -1) {
                    pipeline->material_ids[j] = material_id;
                    break;
                }
            }
            context->materials[i].used = true;
            break;
        }
    }
    if (material_id < 0) {
        fprintf(stderr, "[webgpu.c] No more material slots!\n");
        return -1;
    }
    Material *material = &context->materials[material_id]; // todo: separate
    int mesh_id = -1;
    for (int i = 0; i < MAX_MESHES; i++) {
        if (!context->meshes[i].used) {
            mesh_id = i;
            context->meshes[i] = (Mesh) {0};
            // set the first available mesh index in the list
            // todo: on deleting a mesh, loop over the list and fill up the gap with the furthest remaining mesh
            // todo: so that there is no gap of -1 that blocks everything behind it from being rendered
            for (int i = 0; i < MAX_MESHES; i++) {
                if (material->mesh_ids[i] == -1) {
                    material->mesh_ids[i] = mesh_id;
                    break;
                }
            }
            context->meshes[i].used = true;
            break;
        }
    }
    if (mesh_id < 0) {
        fprintf(stderr, "[webgpu.c] No more mesh slots!\n");
        return -1;
    }
    Mesh *mesh = &context->meshes[mesh_id];

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

    // Create the texture setup // todo: separate mesh config (and call it pipeline instead)
    {
        // Create a sampler
        WGPUSamplerDescriptor samplerDesc = {0};
        samplerDesc.minFilter = WGPUFilterMode_Linear;
        samplerDesc.magFilter = WGPUFilterMode_Nearest;
        samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
        samplerDesc.lodMinClamp = 0;
        samplerDesc.lodMaxClamp = 0;
        samplerDesc.maxAnisotropy = 1;
        samplerDesc.addressModeU = WGPUAddressMode_Repeat;
        samplerDesc.addressModeV = WGPUAddressMode_Repeat;
        samplerDesc.addressModeW = WGPUAddressMode_Repeat;
        material->texture_sampler = wgpuDeviceCreateSampler(context->device, &samplerDesc);
        assert(material->texture_sampler != NULL);

        // Initialize all available textures to the default 1x1 white pixel
        material->texture_count = 0; // 0 textures have been actually set yet with a non-default value
        for (int i=0; i<MAX_TEXTURES; i++) {
            material->textures[i] = context->defaultTexture;
            material->texture_views[i]   = context->defaultTextureView;
        }
        int totalEntries = MAX_TEXTURES + 1; // entry 0 is the sampler
        WGPUBindGroupEntry *entries = calloc(totalEntries, sizeof(WGPUBindGroupEntry));
        // Sampler at binding=0
        entries[0].binding = 0;
        entries[0].sampler = material->texture_sampler;
        for (int i=0; i<MAX_TEXTURES; i++) {
            entries[i+1].binding = i + 1;
            entries[i+1].textureView = material->texture_views[i];
        }
        if (material->texture_bindgroup) {
            wgpuBindGroupRelease(material->texture_bindgroup);
        }
        WGPUBindGroupDescriptor bgDesc = {0};
        bgDesc.layout     = context->texture_layout;
        bgDesc.entryCount = totalEntries;
        bgDesc.entries    = entries;
        material->texture_bindgroup = wgpuDeviceCreateBindGroup(context->device, &bgDesc);

        free(entries);
    }
    
    mesh->material_id = mesh_id;
    material->pipeline_id = pipeline_id;
    printf("[webgpu.c] Created instanced mesh %d with %d vertices, %d indices, and %d instances for pipeline %d\n",
           mesh_id, vc, ic, iic, pipeline_id);
    return mesh_id;
}

int createGPUTexture(void *context_ptr, int mesh_id, void *data, int w, int h) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    Mesh* mesh = &context->meshes[mesh_id];
    Material* material = &context->materials[mesh->material_id];
    Pipeline* pipeline = &context->pipelines[material->pipeline_id];
    int max_textures = TEXTURE_LAYOUT_DESCRIPTOR.entryCount-1;
    if (material->texture_count >= max_textures) {
        fprintf(stderr, "No more texture slots in mesh!"); // todo: allow re-assigning a new texture to a slot that was occupied
        return -1;
    }
    int slot = material->texture_count; // e.g. 0 => binding=1, etc.

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

    material->textures[slot] = tex;
    material->texture_views[slot]   = view;
    material->texture_count++;

    // Now rebuild the bind group // todo: AVOID THIS BY CREATING (?)
    int totalEntries = 1 + max_textures; // + 1 is because we have at index 0 the sampler binding
    WGPUBindGroupEntry* e = calloc(totalEntries, sizeof(WGPUBindGroupEntry));
    e[0].binding = 0;
    e[0].sampler = material->texture_sampler;
    for (int i=0; i<max_textures; i++) {
        e[i+1].binding = i+1;
        e[i+1].textureView = (i < material->texture_count) 
                             ? material->texture_views[i]
                             : context->defaultTextureView;
    }
    if (material->texture_bindgroup) {
        wgpuBindGroupRelease(material->texture_bindgroup);
    }
    WGPUBindGroupDescriptor bgDesc = {0};
    bgDesc.layout     = context->texture_layout;
    bgDesc.entryCount = totalEntries;
    bgDesc.entries    = e;
    material->texture_bindgroup = wgpuDeviceCreateBindGroup(context->device, &bgDesc);
    free(e);

    printf("Added texture to material %d at slot %d (binding=%d)\n", mesh->material_id, slot, slot+1);

    return slot;
}

#pragma region UNIFORMS
int addGPUGlobalUniform(void *context_ptr, int pipeline_id, const void* data, int data_size) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    Pipeline *pipeline = &context->pipelines[pipeline_id];
    // Inline alignment determination using ternary operators
    int alignment = (data_size <= 4) ? 4 :
                    (data_size <= 8) ? 8 :
                    16; // Default for vec3, vec4, mat4x4, or larger
    // Align the offset to the correct boundary (based on WGSL rules)
    int aligned_offset = (pipeline->global_uniform_offset + (alignment - 1)) & ~(alignment - 1);
    // Check if the new offset exceeds buffer capacity
    if (aligned_offset + data_size > GLOBAL_UNIFORM_CAPACITY) {
        // todo: print warning on screen or in log that this failed
        return -1;
    }
    // Copy the data into the aligned buffer
    memcpy(pipeline->global_uniform_data + aligned_offset, data, data_size);
    // Update the current offset
    pipeline->global_uniform_offset = aligned_offset + data_size;
    // todo: print on screen that uniform changed
    return aligned_offset;
}

void setGPUGlobalUniformValue(void *context_ptr, int pipeline_id, int offset, const void* data, int dataSize) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    Pipeline *pipeline = &context->pipelines[pipeline_id];
    if (offset < 0 || offset + dataSize > pipeline->global_uniform_offset) {
        // todo: print warning on screen or in log that this failed
        return;
    }
    memcpy(pipeline->global_uniform_data + offset, data, dataSize);
}
int addGPUMaterialUniform(void *context_ptr, int material_id, const void* data, int data_size) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    Material *material = &context->materials[material_id];
    // Inline alignment determination using ternary operators
    int alignment = (data_size <= 4) ? 4 :
                    (data_size <= 8) ? 8 :
                    16; // Default for vec3, vec4, mat4x4, or larger
    // Align the offset to the correct boundary (based on WGSL rules)
    int aligned_offset = (material->uniform_offset + (alignment - 1)) & ~(alignment - 1);
    // Check if the new offset exceeds buffer capacity
    if (aligned_offset + data_size > MATERIAL_UNIFORM_CAPACITY) {
        // todo: print warning on screen or in log that this failed
        return -1;
    }
    // Copy the data into the aligned buffer
    memcpy(material->uniform_data + aligned_offset, data, data_size);
    // Update the current offset
    material->uniform_offset = aligned_offset + data_size;
    return aligned_offset;
}

void setGPUMaterialUniformValue(void *context_ptr, int material_id, int offset, const void* data, int dataSize) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    Material *material = &context->materials[material_id];
    if (offset < 0 || offset + dataSize > material->uniform_offset) {
        // todo: print warning on screen or in log that this failed
        return;
    }
    memcpy(material->uniform_data + offset, data, dataSize);
}
#pragma endregion

void setGPUInstanceBuffer(void *context_ptr, int mesh_id, void* ii, int iic) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    // freeing the previous buffer is the responsibility of the caller
    Mesh *mesh = &context->meshes[mesh_id];
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
    long long time_before_ns = clock();
    while (!workDone) {
        wgpuDevicePoll(context->device, false, NULL);
    }
    long long time_after_ns = clock();
    float ms_waited_on_gpu = (float) (time_after_ns - time_before_ns);
    return ms_waited_on_gpu;
}
float drawGPUFrame(void *context_ptr, int offset_x, int offset_y, int viewport_width, int viewport_height) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    // Start the frame.
    wgpuSurfaceGetCurrentTexture(context->surface, &context->currentSurfaceTexture);
    if (context->currentSurfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success) return 0.0f;
    WGPUTextureViewDescriptor d = {
        .format = WGPUTextureFormat_RGBA8Unorm,
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = 1,
        .nextInChain = NULL,
    };
    context->currentView = wgpuTextureCreateView(context->currentSurfaceTexture.texture, &d);
    WGPUCommandEncoderDescriptor encDesc = {0};
    context->currentEncoder = wgpuDeviceCreateCommandEncoder(context->device, &encDesc);
    WGPURenderPassColorAttachment colorAtt = {0};
    colorAtt.view = context->currentView;
    colorAtt.loadOp = WGPULoadOp_Clear;
    colorAtt.storeOp = WGPUStoreOp_Store;
    colorAtt.clearValue = (WGPUColor){0., 0., 0., 1.0};
    WGPURenderPassDescriptor passDesc = {0};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAtt;
    passDesc.depthStencilAttachment = &context->depthAttachment,
    context->currentPass = wgpuCommandEncoderBeginRenderPass(context->currentEncoder, &passDesc);

    wgpuRenderPassEncoderSetViewport(
        context->currentPass,
        offset_x,   // x
        offset_y,   // y
        viewport_width,   // width
        viewport_height,   // height
        0.0f,      // minDepth
        1.0f       // maxDepth
    );
    wgpuRenderPassEncoderSetScissorRect(
        context->currentPass,
        (uint32_t)offset_x, (uint32_t)offset_y,
        (uint32_t)viewport_width, (uint32_t)viewport_height
    );

    // Loop through all pipelines and draw each one
    for (int pipeline_id = 0; pipeline_id < MAX_PIPELINES; pipeline_id++) {
        if (context->pipelines[pipeline_id].used) {

            Pipeline *pipeline = &context->pipelines[pipeline_id];
            // Set the render pipeline // todo: does it matter where we call this?
            // todo: is it possible to create the command/renderpass encoders, set the pipeline once at the beginning, and then keep reusing it?

            wgpuRenderPassEncoderSetPipeline(context->currentPass, pipeline->pipeline);
            // Write CPU–side uniform data to GPU
            // todo: condition to only do when updated data
            wgpuQueueWriteBuffer(context->queue, pipeline->globalUniformBuffer, 0, pipeline->global_uniform_data, GLOBAL_UNIFORM_CAPACITY);
            // Bind uniform bind group (group 0).
            wgpuRenderPassEncoderSetBindGroup(context->currentPass, 0, pipeline->globalUniformBindGroup, 0, NULL); // group 0 for global uniforms

            for (int j = 0; j < MAX_MATERIALS && pipeline->material_ids[j] > -1; j++) {

                int material_id = pipeline->material_ids[j];
                if (!context->materials[material_id].used) printf("[FATAL WARNING] An unused material was left in the pipeline's list of material ids");
                Material *material = &context->materials[material_id];

                unsigned int material_uniform_offset = material_id * MATERIAL_UNIFORM_CAPACITY;
                // If the material requires uniform data updates, update the material uniform buffer
                // todo: only write when material setting UPDATE_MATERIAL_UNIFORMS is true
                // todo: we could even set this to true when we do an actual update, and otherwise never do this
                // todo: we can also do that for the global uniforms
                if (1) {
                    // todo: we can batch this write buffer call into one single call for the pipeline instead
                    wgpuQueueWriteBuffer(context->queue, pipeline->materialUniformBuffer, material_uniform_offset, material->uniform_data, MATERIAL_UNIFORM_CAPACITY);
                }
                // Set the dynamic offset to point to the uniform values for this material
                // *info* max size of 64kb -> with 1000 meshes we can have at most 64 bytes of uniform data per mesh (!)
                wgpuRenderPassEncoderSetBindGroup(context->currentPass, 1, pipeline->materialUniformBindGroup, 1, &material_uniform_offset); // group 1 for per-mesh uniforms
                wgpuRenderPassEncoderSetBindGroup(context->currentPass, 2, material->texture_bindgroup, 0, NULL); // group 2 for textures

                for (int k = 0; k < MAX_MESHES && material->mesh_ids[k] > -1; k++) {
                    int mesh_id = material->mesh_ids[k];
                    if (!context->meshes[mesh_id].used) printf("[FATAL WARNING] An unused mesh was left in the material's list of mesh ids");;
                    Mesh *mesh = &context->meshes[mesh_id];

                    // todo: make this based on mesh setting UPDATE_MESH_INSTANCES, then we don't need to do this for static meshes
                    // If the mesh requires instance data updates, update the instance buffer (this is really expensive!)
                    if (1) {
                        unsigned long long instanceDataSize = VERTEX_LAYOUT[1].arrayStride * mesh->instance_count;
                        // write RAM instances to GPU instances
                        // todo: we could 
                        wgpuQueueWriteBuffer(context->queue,mesh->instanceBuffer,0,mesh->instances, instanceDataSize);
                    }

                    // Instanced mesh: bind its vertex + instance buffer (slots 0 and 1) and draw with instance_count
                    wgpuRenderPassEncoderSetVertexBuffer(context->currentPass, 0, mesh->vertexBuffer, 0, WGPU_WHOLE_SIZE);
                    wgpuRenderPassEncoderSetVertexBuffer(context->currentPass, 1, mesh->instanceBuffer, 0, WGPU_WHOLE_SIZE);
                    wgpuRenderPassEncoderSetIndexBuffer(context->currentPass, mesh->indexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
                    // todo: considering this allows a base vertex, first index and first instance,
                    // todo: is it then not possible to put all vertex, index and instance buffers together, 
                    // todo: and loop just over this call instead of calling setVertexBuffer in a loop (?)
                    wgpuRenderPassEncoderDrawIndexed(context->currentPass, mesh->indexCount,mesh->instance_count,0,0,0);
                    // todo: wgpuRenderPassEncoderDrawIndexedIndirect
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

    // Wait on the fence to measure GPU work time
    // float ms_waited_on_gpu = fenceAndWait(context);

    // Release command buffer and present the surface.
    wgpuCommandBufferRelease(cmdBuf);
    wgpuSurfacePresent(context->surface);
    wgpuTextureViewRelease(context->currentView);
    context->currentView = NULL;
    wgpuTextureRelease(context->currentSurfaceTexture.texture);
    context->currentSurfaceTexture.texture = NULL;

    return -1.0;
}
