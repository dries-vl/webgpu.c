#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

#include "wgpu.h"
#include "game_data.h"

#pragma region PREDEFINED DATA
#define MAX_TEXTURES 4
static const WGPUBindGroupLayoutDescriptor PER_MATERIAL_BINDGROUP_LAYOUT_DESC = {
    .entryCount = 1 + MAX_TEXTURES, // one for the sampler + the textures
    .entries = (WGPUBindGroupLayoutEntry[]){
        // Sampler
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        },
        // Textures
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
    WGPUBindGroup      pipeline_bindgroup;
    WGPUBuffer         global_uniform_buffer;
    unsigned char      global_uniform_data[GLOBAL_UNIFORM_CAPACITY]; // global uniform data in RAM
    int                global_uniform_offset;
    WGPUBuffer         material_uniform_buffer;
} Pipeline;

typedef struct {
    bool               used;
    int                pipeline_id;
    int                mesh_ids[MAX_MESHES];
    WGPUBindGroup      material_bindgroup;
    // textures
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
    bool       animated;
    int        material_id;
    // buffers
    WGPUBuffer vertexBuffer;
    int        vertexCount;
    WGPUBuffer indexBuffer;
    int        indexCount;
    WGPUBuffer instanceBuffer;
    void      *instances; // instances in RAM (verts and indices are not kept in RAM) // todo: Instance instead of void
    int        instance_count;
    // animation
    WGPUBindGroup mesh_bindgroup;
    WGPUBuffer bone_buffer;
    float      *bones;
    int frame_count;
    float current_bones[BONE_FRAME_SIZE];
    float current_frame;
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
    // current frame objects (global for simplicity)
    WGPUSurfaceTexture    currentSurfaceTexture;
    WGPUTextureView       currentView;
    WGPUCommandEncoder    currentEncoder;
    WGPURenderPassEncoder currentPass;
    // default texture to assign to every empty texture slot for new meshes (1x1 pixel)
    WGPUTexture      defaultTexture;
    WGPUTextureView  defaultTextureView;
    // default bones for meshes that are not animated (identity matrix)
    WGPUBuffer defaultBoneBuffer;
    WGPUBindGroup defaultBoneBindGroup;
    float default_bones[MAX_BONES][16];
    // depth texture
    WGPUDepthStencilState depthStencilState;
    WGPURenderPassDepthStencilAttachment depthAttachment;
    // shadow texture
    WGPURenderPipeline shadow_pipeline;
    WGPUBuffer shadow_uniform_buffer;
    WGPUBindGroup shadow_bindgroup;
    WGPUTexture shadow_texture;
    WGPUTextureView shadow_texture_view;
    WGPUSampler shadow_sampler;
    // layouts for all the bindgroups
    WGPUBindGroupLayout per_pipeline_layout; // *should have only one pipeline for all main rendering*
    WGPUBindGroupLayout per_material_layout;
    WGPUBindGroupLayout per_mesh_layout;
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
    // todo: this might not be possible in Web, so use another way to get the dedicated gpu there
    context.adapter = selectDiscreteGPU(context.instance); // code to force select dedicated gpu
    assert(context.adapter);
    wgpuAdapterRequestDevice(context.adapter, NULL, handle_request_device, &context);
    assert(context.device);
    context.queue = wgpuDeviceGetQueue(context.device);
    assert(context.queue);

    // Create the per-pipeline bind group layout (ex. the global uniforms, the dynamic offset buffer for all materials' uniforms, the shadow texture)
    {
        WGPUBindGroupLayoutEntry entries[4] = {
            // Per-pipeline global uniforms
            {
                .binding = 0,
                .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                .buffer.type = WGPUBufferBindingType_Uniform,
                .buffer.minBindingSize = GLOBAL_UNIFORM_CAPACITY,
                .buffer.hasDynamicOffset = 0,
            },
            // Per-material uniforms (with offset to differentiate materials)
            {
                .binding = 1,
                .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                .buffer.type = WGPUBufferBindingType_Uniform,
                .buffer.minBindingSize = MATERIAL_UNIFORM_CAPACITY,
                .buffer.hasDynamicOffset = 1,
            },
            // Shadow map texture view.
            {
                .binding = 2,
                .visibility = WGPUShaderStage_Fragment,
                .texture = {
                    .sampleType = WGPUTextureSampleType_Depth, // Depth texture
                    .viewDimension = WGPUTextureViewDimension_2D,
                    .multisampled = false,
                },
            },
            // Comparison sampler for shadow
            {
                .binding = 3,
                .visibility = WGPUShaderStage_Fragment,
                .sampler = {
                    .type = WGPUSamplerBindingType_Comparison, // Comparison sampler for shadow mapping
                },
            },
        };
        WGPUBindGroupLayoutDescriptor bglDesc = {0};
        bglDesc.entryCount = 4;
        bglDesc.entries = entries;
        context.per_pipeline_layout = wgpuDeviceCreateBindGroupLayout(context.device, &bglDesc);
    }

    // Create the per-material bind group layout (ex. the textures)
    {
        context.per_material_layout = wgpuDeviceCreateBindGroupLayout(context.device, &PER_MATERIAL_BINDGROUP_LAYOUT_DESC);
    }

    // Create per-mesh bindgroup layout (ex. the bones)
    {
        // Define a bind group layout for bones (similar to your global uniform layout)
        WGPUBindGroupLayoutEntry boneEntry = {0};
        boneEntry.binding = 0;
        boneEntry.visibility = WGPUShaderStage_Vertex;
        boneEntry.buffer.type = WGPUBufferBindingType_Uniform;
        boneEntry.buffer.minBindingSize = sizeof(context.default_bones);
        WGPUBindGroupLayoutDescriptor boneBGLDesc = {0};
        boneBGLDesc.entryCount = 1;
        boneBGLDesc.entries = &boneEntry;
        context.per_mesh_layout = wgpuDeviceCreateBindGroupLayout(context.device, &boneBGLDesc);
    }

    // Create default dummy bone data
    {
        // identity matrices
        for (int i = 0; i < MAX_BONES; i++) {
            for (int j = 0; j < 16; j++) {
                context.default_bones[i][j] = (j % 5 == 0) ? 1.0f : 0.0f;
            }
        }

        {
            WGPUBufferDescriptor bufDesc = {0};
            bufDesc.size = sizeof(context.default_bones);
            bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            context.defaultBoneBuffer = wgpuDeviceCreateBuffer(context.device, &bufDesc);

            // Upload the identity matrix to the buffer.
            wgpuQueueWriteBuffer(context.queue, context.defaultBoneBuffer, 0, context.default_bones, sizeof(context.default_bones));
        }

        // Create the default bind group with our minimal bone buffer.
        WGPUBindGroupEntry bgEntry = {0};
        bgEntry.binding = 0;
        bgEntry.buffer = context.defaultBoneBuffer;
        bgEntry.offset = 0;
        bgEntry.size = sizeof(context.default_bones);

        WGPUBindGroupDescriptor bgDesc = {0};
        bgDesc.layout = context.per_mesh_layout;
        bgDesc.entryCount = 1;
        bgDesc.entries = &bgEntry;
        context.defaultBoneBindGroup = wgpuDeviceCreateBindGroup(context.device, &bgDesc);
    }

    // Create default value for empty texture slots (1x1 texture)
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

    // Create the depth texture attachment
    {
        WGPUTextureDescriptor depthTextureDesc = {
            .usage = WGPUTextureUsage_RenderAttachment,
            .dimension = WGPUTextureDimension_2D,
            .size = { .width = width, .height = height, .depthOrArrayLayers = 1 },
            .format = WGPUTextureFormat_Depth24PlusStencil8, // Or Depth32Float if supported
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
            .depthCompare = WGPUCompareFunction_LessEqual, // Pass fragments with lesser depth values
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

    // Create global shadow texture + sampler
    {
        WGPUTextureDescriptor shadowTextureDesc = {
            .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
            .dimension = WGPUTextureDimension_2D,
            .format = WGPUTextureFormat_Depth32Float,
            .size = (WGPUExtent3D){ .width = 1024, .height = 1024, .depthOrArrayLayers = 1 },
            .mipLevelCount = 1,
            .sampleCount = 1,
        };
        context.shadow_texture = wgpuDeviceCreateTexture(context.device, &shadowTextureDesc);
        context.shadow_texture_view = wgpuTextureCreateView(context.shadow_texture, NULL);
        
        // Create a comparison sampler for shadow sampling.
        WGPUSamplerDescriptor shadowSamplerDesc = {
            .addressModeU = WGPUAddressMode_ClampToEdge,
            .addressModeV = WGPUAddressMode_ClampToEdge,
            .addressModeW = WGPUAddressMode_ClampToEdge,
            .maxAnisotropy = 1,
            .magFilter = WGPUFilterMode_Nearest,
            .minFilter = WGPUFilterMode_Nearest,
            .mipmapFilter = WGPUMipmapFilterMode_Nearest,
            .compare = WGPUCompareFunction_LessEqual,
        };
        context.shadow_sampler = wgpuDeviceCreateSampler(context.device, &shadowSamplerDesc);
    }

    create_shadow_pipeline(&context);

    WGPUSurfaceCapabilities caps = {0};
    wgpuSurfaceGetCapabilities(context.surface, context.adapter, &caps);
    WGPUTextureFormat chosenFormat = WGPUTextureFormat_RGBA8UnormSrgb;
    // *Rgba8UnormSrgb seems slightly slower than Rgba8Unorm for some reason*
    // if (caps.formatCount > 0) { // selects Rgba8UnormSrgb it seems
    //     chosenFormat = caps.formats[0];
    // }

    context.config = (WGPUSurfaceConfiguration){
        .device = context.device,
        .format = chosenFormat,
        .width = width,
        .height = height,
        .usage = WGPUTextureUsage_RenderAttachment,
        .alphaMode = WGPUCompositeAlphaMode_Auto,
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
    #define LAYOUT_COUNT 3
    WGPUBindGroupLayout bgls[LAYOUT_COUNT] = { 
        context->per_pipeline_layout, 
        context->per_material_layout, 
        context->per_mesh_layout
    };
    WGPUPipelineLayoutDescriptor layoutDesc = {0};
    layoutDesc.bindGroupLayoutCount = LAYOUT_COUNT;
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
        pipeline->global_uniform_buffer = wgpuDeviceCreateBuffer(context->device, &ubDesc);

        WGPUBufferDescriptor ubDesc2 = {0};
        ubDesc2.size = MATERIALS_UNIFORM_BUFFER_TOTAL_SIZE;
        ubDesc2.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        pipeline->material_uniform_buffer = wgpuDeviceCreateBuffer(context->device, &ubDesc2);

        #define ENTRY_COUNT 4
        WGPUBindGroupEntry entries[ENTRY_COUNT] = {
            {
                .binding = 0,
                .buffer = pipeline->global_uniform_buffer,
                .offset = 0,
                .size = GLOBAL_UNIFORM_CAPACITY,
            },
            {
                .binding = 1,
                .buffer = pipeline->material_uniform_buffer,
                .offset = 0,
                .size = MATERIAL_UNIFORM_CAPACITY,
            },
            {
                .binding = 2,
                .textureView = context->shadow_texture_view,
            },
            {
                .binding = 3,
                .sampler = context->shadow_sampler,
            }
        };

        WGPUBindGroupDescriptor uBgDesc = {0};
        uBgDesc.layout = context->per_pipeline_layout;
        uBgDesc.entryCount = ENTRY_COUNT;
        uBgDesc.entries = entries;
        pipeline->pipeline_bindgroup = wgpuDeviceCreateBindGroup(context->device, &uBgDesc);
    }
    
    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);
    
    printf("[webgpu.c] Created pipeline %d from shader: %s\n", pipeline_id, shader);
    return pipeline_id;
}

// Computes a light view–projection matrix for a 100x100 scene.
// The scene is assumed to span from -50 to 50 in X and Z (centered at the origin).
// We use an orthographic projection with left=-50, right=50, bottom=-50, top=50,
// and set near and far to tightly cover the scene (e.g. near=50, far=150).
// The light is assumed to come from the direction (0.5, -1, 0.5) (normalized)
// and is placed at distance 100 along the opposite direction.
// The resulting matrix is output in column‑major order in the array out[16].
void computeLightViewProj(float out[16]) {
    // --- Orthographic projection parameters ---
    float left = -50.0f, right = 50.0f;
    float bottom = -50.0f, top = 50.0f;
    // Choose near and far so that the scene depth is tightly covered in light-space.
    float nearVal = 1.0f, farVal = 150.0f;
    // Standard orthographic projection matrix (row-major, OpenGL style):
    // [ 2/(r-l)      0            0          -(r+l)/(r-l) ]
    // [    0      2/(t-b)         0          -(t+b)/(t-b) ]
    // [    0         0       -2/(f-n)       -(f+n)/(f-n) ]
    // [    0         0            0               1       ]
    float P[16] = {
         2.0f/(right-left), 0, 0, 0,
         0, 2.0f/(top-bottom), 0, 0,
         0, 0, -2.0f/(farVal-nearVal), 0,
         -(right+left)/(right-left), -(top+bottom)/(top-bottom), -(farVal+nearVal)/(farVal-nearVal), 1
    };

    // --- Light (view) parameters ---
    // Choose a light direction (e.g., (0.5, -1, 0.5)) and normalize it.
    float lx = 0.5f, ly = -1.0f, lz = 0.5f;
    float len = sqrt(lx*lx + ly*ly + lz*lz);
    lx /= len; ly /= len; lz /= len;
    // Position the light far away along the opposite direction:
    float distance = 100.0f;
    // eye = sceneCenter - (lightDirection * distance)
    float eye[3] = { -lx * distance, -ly * distance, -lz * distance };
    float center[3] = { 0.0f, 0.0f, 0.0f };
    float up[3] = { 0.0f, 1.0f, 0.0f };

    // --- Compute the view matrix using a look-at function (row-major) ---
    // f = normalize(center - eye)
    float fwd[3] = { center[0]-eye[0], center[1]-eye[1], center[2]-eye[2] };
    float fwdLen = sqrt(fwd[0]*fwd[0] + fwd[1]*fwd[1] + fwd[2]*fwd[2]);
    fwd[0] /= fwdLen; fwd[1] /= fwdLen; fwd[2] /= fwdLen;

    // s = normalize(cross(fwd, up))
    float s[3] = {
        fwd[1]*up[2] - fwd[2]*up[1],
        fwd[2]*up[0] - fwd[0]*up[2],
        fwd[0]*up[1] - fwd[1]*up[0]
    };
    float sLen = sqrt(s[0]*s[0] + s[1]*s[1] + s[2]*s[2]);
    s[0] /= sLen; s[1] /= sLen; s[2] /= sLen;

    // u = cross(s, fwd)
    float u[3] = {
        s[1]*fwd[2] - s[2]*fwd[1],
        s[2]*fwd[0] - s[0]*fwd[2],
        s[0]*fwd[1] - s[1]*fwd[0]
    };

    // Build the view matrix V (row-major):
    // [ s.x   s.y   s.z    -dot(s,eye) ]
    // [ u.x   u.y   u.z    -dot(u,eye) ]
    // [ -f.x -f.y  -f.z    dot(f,eye)  ]
    // [  0     0     0         1       ]
    float V[16] = {
        s[0], s[1], s[2], 0,
        u[0], u[1], u[2], 0,
       -fwd[0], -fwd[1], -fwd[2], 0,
        0, 0, 0, 1
    };
    // Compute translation components.
    V[12] = - (s[0]*eye[0] + s[1]*eye[1] + s[2]*eye[2]);
    V[13] = - (u[0]*eye[0] + u[1]*eye[1] + u[2]*eye[2]);
    V[14] =   ( fwd[0]*eye[0] + fwd[1]*eye[1] + fwd[2]*eye[2]);

    // --- Multiply projection and view matrices: M = P * V ---
    float M[16] = {0};
    for (int i = 0; i < 4; i++) {
       for (int j = 0; j < 4; j++) {
           for (int k = 0; k < 4; k++) {
               M[i*4+j] += P[i*4+k] * V[k*4+j];
           }
       }
    }

    // --- Convert from row-major (M) to column-major order for WGSL ---
    for (int i = 0; i < 4; i++) {
       for (int j = 0; j < 4; j++) {
          out[i*4+j] = M[j*4+i];
       }
    }
}
void create_shadow_pipeline(void *context_ptr) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    // 1. Create a bind group layout for the shadow uniform.
    //    This layout has a single entry at binding 0 for the lightViewProj matrix.
    WGPUBindGroupLayoutEntry shadowBGL_Entry = {0};
    shadowBGL_Entry.binding = 0;
    shadowBGL_Entry.visibility = WGPUShaderStage_Vertex;
    shadowBGL_Entry.buffer.type = WGPUBufferBindingType_Uniform;
    shadowBGL_Entry.buffer.minBindingSize = 64; // 4x4 f32 (16 * 4 bytes)
    shadowBGL_Entry.buffer.hasDynamicOffset = false;

    WGPUBindGroupLayoutDescriptor shadowBGLDesc = {0};
    shadowBGLDesc.entryCount = 1;
    shadowBGLDesc.entries = &shadowBGL_Entry;
    WGPUBindGroupLayout shadowBindGroupLayout = wgpuDeviceCreateBindGroupLayout(context->device, &shadowBGLDesc);

    // 2. Create a pipeline layout for the shadow pipeline
    WGPUPipelineLayoutDescriptor shadowPLDesc = {0};
    shadowPLDesc.bindGroupLayoutCount = 1;
    shadowPLDesc.bindGroupLayouts = &shadowBindGroupLayout;
    WGPUPipelineLayout shadowPipelineLayout = wgpuDeviceCreatePipelineLayout(context->device, &shadowPLDesc);

    // 3. Load the shadow shader module.
    WGPUShaderModule shadowShaderModule = loadWGSL(context->device, "data/shaders/shadow.wgsl");
    assert(shadowShaderModule);

    // 5. Set up a depth stencil state that matches your shadow texture format.
    //    Here we assume your shadow texture was created with WGPUTextureFormat_Depth32Float.
    // Define a default stencil face state with valid settings.
    WGPUStencilFaceState defaultStencilState = {0};
    defaultStencilState.compare = WGPUCompareFunction_Always;
    defaultStencilState.failOp = WGPUStencilOperation_Keep;
    defaultStencilState.depthFailOp = WGPUStencilOperation_Keep;
    defaultStencilState.passOp = WGPUStencilOperation_Keep;
    WGPUDepthStencilState shadowDepthStencil = {0};
    shadowDepthStencil.format = WGPUTextureFormat_Depth32Float;
    shadowDepthStencil.depthWriteEnabled = true;
    shadowDepthStencil.depthCompare = WGPUCompareFunction_LessEqual;
    shadowDepthStencil.stencilFront = defaultStencilState;
    shadowDepthStencil.stencilBack = defaultStencilState;

    // 6. Create the render pipeline descriptor for the shadow pipeline.
    WGPURenderPipelineDescriptor shadowRPDesc = {0};
    shadowRPDesc.layout = shadowPipelineLayout;

    // Vertex stage.
    shadowRPDesc.vertex.module = shadowShaderModule;
    shadowRPDesc.vertex.entryPoint = "vs_main";
    shadowRPDesc.vertex.bufferCount = 2;
    shadowRPDesc.vertex.buffers = VERTEX_LAYOUT;

    // For a depth-only pass, the fragment stage can be omitted.
    // (If required by your implementation, you could supply a minimal fragment stage that writes nothing.)
    shadowRPDesc.fragment = NULL;

    // Set primitive state (you can adjust as needed).
    WGPUPrimitiveState primState = {0};
    primState.topology = WGPUPrimitiveTopology_TriangleList;
    primState.cullMode = WGPUCullMode_Back;
    primState.frontFace = WGPUFrontFace_CCW;
    shadowRPDesc.primitive = primState;

    // Multisample state.
    WGPUMultisampleState msState = {0};
    msState.count = 1;
    msState.mask = 0xFFFFFFFF;
    shadowRPDesc.multisample = msState;

    // Attach the depth stencil state.
    shadowRPDesc.depthStencil = &shadowDepthStencil;

    // 7. Create the shadow render pipeline.
    WGPURenderPipeline shadowPipeline = wgpuDeviceCreateRenderPipeline(context->device, &shadowRPDesc);
    assert(shadowPipeline);

    // Store the shadowPipeline in your context for later use in the shadow pass.
    context->shadow_pipeline = shadowPipeline;

    WGPUBufferDescriptor shadowUniformBufferDesc = {0};
    shadowUniformBufferDesc.size = 64; // size of a 4x4 f32 matrix in bytes
    shadowUniformBufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    context->shadow_uniform_buffer = wgpuDeviceCreateBuffer(context->device, &shadowUniformBufferDesc);

    // --- Create the shadow bind group ---
    WGPUBindGroupEntry shadowBindGroupEntry = {0};
    shadowBindGroupEntry.binding = 0;
    shadowBindGroupEntry.buffer = context->shadow_uniform_buffer;
    shadowBindGroupEntry.offset = 0;
    shadowBindGroupEntry.size = 64;

    WGPUBindGroupDescriptor shadowBindGroupDesc = {0};
    shadowBindGroupDesc.layout = shadowBindGroupLayout; // created earlier
    shadowBindGroupDesc.entryCount = 1;
    shadowBindGroupDesc.entries = &shadowBindGroupEntry;
    context->shadow_bindgroup = wgpuDeviceCreateBindGroup(context->device, &shadowBindGroupDesc);

    // Assume lightViewProjData is a 64-byte array (or pointer) holding your light view-projection matrix.
    // todo: we could modify the lighting each frame if we wanted to
    float lightViewProjData[16] = {
        -0.01414,  0.01154,  0.00408,  0.0,
        0.0,      0.01152, -0.00816,  0.0,
        0.01414,  0.01152,  0.00408,  0.0,
        0.0,      0.006,   -0.001,   1.0 
    };
    // computeLightViewProj(lightViewProjData);
    printf("Light View-Projection Matrix (column-major):\n");
    for (int i = 0; i < 4; i++) {
        printf("[ ");
        for (int j = 0; j < 4; j++) {
            printf("%f ", lightViewProjData[i*4+j]);
        }
        printf("]\n");
    }
    // Upload the lightViewProj matrix to the shadow uniform buffer.
    wgpuQueueWriteBuffer(context->queue, context->shadow_uniform_buffer, 0, lightViewProjData, 64);

    // Optionally, release the shader module and pipeline layout if no longer needed.
    wgpuShaderModuleRelease(shadowShaderModule);
    wgpuPipelineLayoutRelease(shadowPipelineLayout);
    wgpuBindGroupLayoutRelease(shadowBindGroupLayout);
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
    Material *material = &context->materials[material_id]; // todo: separate and don't create a new one every time
    int mesh_id = -1;
    for (int i = 0; i < MAX_MESHES; i++) {
        if (!context->meshes[i].used) {
            mesh_id = i;
            context->meshes[mesh_id] = (Mesh) {0};
            // set the first available mesh index in the list
            // todo: on deleting a mesh, loop over the list and fill up the gap with the furthest remaining mesh
            // todo: so that there is no gap of -1 that blocks everything behind it from being rendered
            for (int j = 0; j < MAX_MESHES; j++) {
                if (material->mesh_ids[j] == -1) {
                    material->mesh_ids[j] = mesh_id;
                    break;
                }
            }
            context->meshes[mesh_id].used = true;
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

    // Create the texture setup
    // todo: in material instead (but no function for that yet)
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
        if (material->material_bindgroup) {
            wgpuBindGroupRelease(material->material_bindgroup);
        }
        WGPUBindGroupDescriptor bgDesc = {0};
        bgDesc.layout     = context->per_material_layout;
        bgDesc.entryCount = totalEntries;
        bgDesc.entries    = entries;
        material->material_bindgroup = wgpuDeviceCreateBindGroup(context->device, &bgDesc);

        free(entries);
    }

    // Set default bone uniform bindgroup & buffer
    {
        mesh->mesh_bindgroup = context->defaultBoneBindGroup;
        mesh->bone_buffer = context->defaultBoneBuffer;
        mesh->bones = (float *)context->default_bones;
        mesh->frame_count = 1;
        memcpy(mesh->current_bones, context->default_bones, sizeof(mesh->current_bones));
        mesh->current_frame = 0.;
    }
    
    mesh->material_id = mesh_id;
    material->pipeline_id = pipeline_id;
    printf("[webgpu.c] Created instanced mesh %d with %d vertices, %d indices, and %d instances for pipeline %d\n",
           mesh_id, vc, ic, iic, pipeline_id);
    return mesh_id;
}

void setGPUMeshBoneData(void *context_ptr, int mesh_id, float *bf[MAX_BONES][16], int bc, int fc) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    Mesh* mesh = &context->meshes[mesh_id];
    // Set bone uniform bindgroup & buffer
    {
        {
            WGPUBufferDescriptor bufDesc = {0};
            bufDesc.size = sizeof(context->default_bones);
            bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            mesh->bone_buffer = wgpuDeviceCreateBuffer(context->device, &bufDesc);
        }

        // Create the default bind group with our minimal bone buffer.
        WGPUBindGroupEntry bgEntry = {0};
        bgEntry.binding = 0;
        bgEntry.buffer = mesh->bone_buffer;
        bgEntry.offset = 0;
        bgEntry.size = sizeof(context->default_bones);

        WGPUBindGroupDescriptor bgDesc = {0};
        bgDesc.layout = context->per_mesh_layout;
        bgDesc.entryCount = 1;
        bgDesc.entries = &bgEntry;
        mesh->mesh_bindgroup = wgpuDeviceCreateBindGroup(context->device, &bgDesc);
    }

    mesh->bones = (float *)bf;
    mesh->frame_count = fc;
    memcpy(mesh->current_bones, bf, sizeof(mesh->current_bones));
    mesh->current_frame = 0.;
    mesh->animated = 1;
    // Upload the identity matrix to the buffer.
    wgpuQueueWriteBuffer(context->queue, mesh->bone_buffer, 0, bf, sizeof(context->default_bones));
}

int createGPUTexture(void *context_ptr, int mesh_id, void *data, int w, int h) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    Mesh* mesh = &context->meshes[mesh_id];
    Material* material = &context->materials[mesh->material_id];
    Pipeline* pipeline = &context->pipelines[material->pipeline_id];
    int max_textures = PER_MATERIAL_BINDGROUP_LAYOUT_DESC.entryCount-1;
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
    if (material->material_bindgroup) {
        wgpuBindGroupRelease(material->material_bindgroup);
    }
    WGPUBindGroupDescriptor bgDesc = {0};
    bgDesc.layout     = context->per_material_layout;
    bgDesc.entryCount = totalEntries;
    bgDesc.entries    = e;
    material->material_bindgroup = wgpuDeviceCreateBindGroup(context->device, &bgDesc);
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

static void updateMeshAnimationFrame(WebGPUContext *context, int mesh_id) {
    Mesh *mesh = &context->meshes[mesh_id];

    // Advance the current frame (using your time step, here 0.1f)
    mesh->current_frame += 0.2f;
    if (mesh->current_frame >= (float)mesh->frame_count)
        mesh->current_frame -= (float)mesh->frame_count;

    // Determine the current frame and the next frame indices
    int frame = (int)floor(mesh->current_frame);
    float alpha = mesh->current_frame - frame;
    int nextFrame = frame + 1;
    if (nextFrame >= mesh->frame_count)
        nextFrame = 0;

    // For each bone, interpolate between the current and next frame
    for (int b = 0; b < MAX_BONES; b++)
    {
        int baseCurrent = (frame * MAX_BONES + b) * 16;
        int baseNext    = (nextFrame * MAX_BONES + b) * 16;
        for (int i = 0; i < 16; i++)
        {
            float currentVal = mesh->bones[baseCurrent + i];
            float nextVal    = mesh->bones[baseNext + i];
            mesh->current_bones[b * 16 + i] = currentVal * (1.0f - alpha) + nextVal * alpha;
        }
    }
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
        .format = WGPUTextureFormat_RGBA8UnormSrgb,
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

    // Animate a specific bone—for example, rotate bone 0 around the Y axis.
    static float angle = 1.; // Your time value (e.g. from a timer)
    angle += 0.01;
    float cosA = cos(angle);
    float sinA = sin(angle);
    context->default_bones[0][0] = cosA;
    context->default_bones[0][2] = sinA;
    context->default_bones[0][8] = -sinA;
    context->default_bones[0][10] = cosA;

    // SHADOWS
    {
        // 1. Create a command encoder for the shadow pass.
        WGPUCommandEncoderDescriptor shadowEncDesc = {0};
        WGPUCommandEncoder shadowEncoder = wgpuDeviceCreateCommandEncoder(context->device, &shadowEncDesc);

        // 2. Set up a render pass descriptor for the shadow pass.
        //    This render pass uses no color attachment (depth-only).
        WGPURenderPassDepthStencilAttachment shadowDepthAttachment = {0};
        shadowDepthAttachment.view = context->shadow_texture_view;
        shadowDepthAttachment.depthLoadOp = WGPULoadOp_Clear;
        shadowDepthAttachment.depthStoreOp = WGPUStoreOp_Store;
        shadowDepthAttachment.depthClearValue = 1.0f;
        // (Stencil settings can be added if you use them.)
        WGPURenderPassDescriptor shadowPassDesc = {0};
        shadowPassDesc.colorAttachmentCount = 0; // no color attachments
        shadowPassDesc.depthStencilAttachment = &shadowDepthAttachment;

        // 3. Begin the shadow render pass.
        WGPURenderPassEncoder shadowPass = wgpuCommandEncoderBeginRenderPass(shadowEncoder, &shadowPassDesc);

        // 4. Bind the shadow pipeline.
        wgpuRenderPassEncoderSetPipeline(shadowPass, context->shadow_pipeline);
        wgpuRenderPassEncoderSetBindGroup(shadowPass, 0, context->shadow_bindgroup, 0, NULL);

        // 5. For each mesh that casts shadows, set its vertex/index/instance buffers and draw.
        //    (This is similar to your main render loop; you may loop through your scene meshes.)
        for (int pipeline_id = 0; pipeline_id < MAX_PIPELINES; pipeline_id++) {
            if (context->pipelines[pipeline_id].used) {
                Pipeline *pipeline = &context->pipelines[pipeline_id];
                // You might need to set a shadow-specific bind group (for the lightViewProj matrix)
                // e.g., wgpuRenderPassEncoderSetBindGroup(shadowPass, 0, shadowGlobalBindGroup, 0, NULL);
                // Then for each mesh in your pipeline:
                for (int j = 0; j < MAX_MATERIALS && pipeline->material_ids[j] > -1; j++) {
                    int material_id = pipeline->material_ids[j];
                    Material *material = &context->materials[material_id];
                    for (int k = 0; k < MAX_MESHES && material->mesh_ids[k] > -1; k++) {
                        int mesh_id = material->mesh_ids[k];
                        Mesh *mesh = &context->meshes[mesh_id];
                        // Bind the mesh's vertex and instance buffers.
                        wgpuRenderPassEncoderSetVertexBuffer(shadowPass, 0, mesh->vertexBuffer, 0, WGPU_WHOLE_SIZE);
                        wgpuRenderPassEncoderSetVertexBuffer(shadowPass, 1, mesh->instanceBuffer, 0, WGPU_WHOLE_SIZE);
                        // Bind the index buffer.
                        wgpuRenderPassEncoderSetIndexBuffer(shadowPass, mesh->indexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
                        // Draw the mesh.
                        wgpuRenderPassEncoderDrawIndexed(shadowPass, mesh->indexCount, mesh->instance_count, 0, 0, 0);
                    }
                }
            }
        }

        // 6. End the shadow render pass and finish encoding.
        wgpuRenderPassEncoderEnd(shadowPass);
        wgpuRenderPassEncoderRelease(shadowPass);
        WGPUCommandBufferDescriptor shadowCmdDesc = {0};
        WGPUCommandBuffer shadowCmdBuf = wgpuCommandEncoderFinish(shadowEncoder, &shadowCmdDesc);
        wgpuCommandEncoderRelease(shadowEncoder);
        // Submit the shadow command buffer.
        wgpuQueueSubmit(context->queue, 1, &shadowCmdBuf);
        wgpuCommandBufferRelease(shadowCmdBuf);

        // ----- Now proceed with your main render pass -----
        // (The main pass will sample context->shadow_texture_view using the PCF code in your fragment shader.)
    }

    // Loop through all pipelines and draw each one
    for (int pipeline_id = 0; pipeline_id < MAX_PIPELINES; pipeline_id++) {
        if (context->pipelines[pipeline_id].used) {

            Pipeline *pipeline = &context->pipelines[pipeline_id];
            // Set the render pipeline // todo: does it matter where we call this?
            // todo: is it possible to create the command/renderpass encoders, set the pipeline once at the beginning, and then keep reusing it?
            wgpuRenderPassEncoderSetPipeline(context->currentPass, pipeline->pipeline);
            // Write CPU–side uniform data to GPU
            // todo: condition to only do when updated data
            wgpuQueueWriteBuffer(context->queue, pipeline->global_uniform_buffer, 0, pipeline->global_uniform_data, GLOBAL_UNIFORM_CAPACITY);

            for (int j = 0; j < MAX_MATERIALS && pipeline->material_ids[j] > -1; j++) {

                int material_id = pipeline->material_ids[j];
                if (!context->materials[material_id].used) {printf("[FATAL WARNING] An unused material was left in the pipeline's list of material ids\n"); continue;}
                Material *material = &context->materials[material_id];

                unsigned int material_uniform_offset = material_id * MATERIAL_UNIFORM_CAPACITY;
                // If the material requires uniform data updates, update the material uniform buffer
                // todo: only write when material setting UPDATE_MATERIAL_UNIFORMS is true
                // todo: we could even set this to true when we do an actual update, and otherwise never do this
                // todo: we can also do that for the global uniforms
                if (1) {
                    // todo: we can batch this write buffer call into one single call for the pipeline instead
                    wgpuQueueWriteBuffer(context->queue, pipeline->material_uniform_buffer, material_uniform_offset, material->uniform_data, MATERIAL_UNIFORM_CAPACITY);
                }
                // Set the dynamic offset to point to the uniform values for this material
                // *info* max size of 64kb -> with 1000 meshes we can have at most 64 bytes of uniform data per mesh (!)
                // Bind per-pipeline bind group (group 0) (global uniforms, material uniforms, shadows), pass offset for the material uniforms
                wgpuRenderPassEncoderSetBindGroup(context->currentPass, 0, pipeline->pipeline_bindgroup, 1, &material_uniform_offset);
                // Bind per-material bind group (group 1) // todo: we could fit all the textures into the pipeline, up to 1000 bindings in one bindgroup -> avoid this call for every material
                wgpuRenderPassEncoderSetBindGroup(context->currentPass, 1, material->material_bindgroup, 0, NULL);

                for (int k = 0; k < MAX_MESHES && material->mesh_ids[k] > -1; k++) {
                    
                    int mesh_id = material->mesh_ids[k];
                    if (!context->meshes[mesh_id].used) {printf("[FATAL WARNING] An unused mesh (%d) was left in the material's (%d) list of mesh ids\n", mesh_id, material_id); continue;}
                    Mesh *mesh = &context->meshes[mesh_id];
                    
                    // todo: based on mesh setting PLAYING_ANIMATION or something
                    if (mesh->animated) {
                        updateMeshAnimationFrame(context, mesh_id);
                        wgpuQueueWriteBuffer(context->queue,mesh->bone_buffer,0,mesh->current_bones, sizeof(context->default_bones));
                    }
                    // todo: make this based on mesh setting USE_BONES (but needs to be reset if previous?)
                    if (1) {
                        wgpuRenderPassEncoderSetBindGroup(context->currentPass, 2, mesh->mesh_bindgroup, 0, NULL);
                    }
                    // todo: make this based on mesh setting UPDATE_MESH_INSTANCES, then we don't need to do this for static meshes
                    // If the mesh requires instance data updates, update the instance buffer (this is really expensive!)
                    if (1) {
                        unsigned long long instanceDataSize = VERTEX_LAYOUT[1].arrayStride * mesh->instance_count;
                        // write RAM instances to GPU instances
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
