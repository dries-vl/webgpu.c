#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

#ifdef __EMSCRIPTEN__
#include "web/webgpu.h"
#include <emscripten/html5.h>
void (*setup_callback)();
#else
#include "wgpu.h"
#endif
#include "platform.h"
#include "graphics.h"

#pragma region PREDEFINED DATA
static const WGPUTextureFormat screen_color_format = WGPUTextureFormat_RGBA8Unorm;
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
            { .format = WGPUVertexFormat_Snorm8x4,   .offset = 28, .shaderLocation = 2 }, // normal[4]  (4 bytes)
            { .format = WGPUVertexFormat_Snorm8x4,   .offset = 32, .shaderLocation = 3 }, // tangent[4] (4 bytes)
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
    enum MeshFlags  flags;
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
    // setup
    int width; int height; int viewport_width; int viewport_height;
    // data
    Pipeline              pipelines[MAX_PIPELINES];
    Material              materials[MAX_MATERIALS];
    Mesh                  meshes[MAX_MESHES];
    // current frame objects (global for simplicity)     // todo: make a bunch of these static to avoid global bloat
    WGPUSurfaceTexture    currentSurfaceTexture;
    WGPUTextureView       swapchain_view;
    // optional postprocessing with intermediate texture
    WGPUTexture           post_processing_texture;
    WGPUTextureView       post_processing_texture_view;
    WGPURenderPipeline    post_processing_pipeline;
    WGPUBindGroup         post_processing_bindgroup;
    WGPUSampler           post_processing_sampler;
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
    // msaa texture
    WGPUTexture     msaa_texture;
    WGPUTextureView msaa_texture_view;
    // shadow texture // todo: cascading shadow maps + dyn. updating
    WGPURenderPipeline shadow_pipeline;
    WGPUTexture        shadow_texture;
    WGPUTextureView    shadow_texture_view;
    WGPUSampler        shadow_sampler;
    // reflection cubemap // todo: multiple cubemaps for multiple reflection sources + dyn. updating
    WGPUTexture        cubemap_texture;
    WGPUTextureView    cubemap_texture_view;
    WGPUSampler        cubemap_sampler;
    // global bindgroup
    WGPUBindGroupLayout global_layout;
    WGPUBindGroup       global_bindgroup;
    WGPUBuffer          global_uniform_buffer;
    unsigned char       global_uniform_data[GLOBAL_UNIFORM_CAPACITY]; // global uniform data in RAM
    int                 global_uniform_offset;
    // layouts for all the bindgroups
    WGPUBindGroupLayout main_pipeline_layout;
    WGPUBindGroupLayout per_material_layout;
    WGPUBindGroupLayout per_mesh_layout;
} WebGPUContext;
#pragma endregion

// todo: separate context from device setup; and then allow the context to be freed/recreated while keeping the device stuff
static void setup_context(WebGPUContext *context) {
    assert(context->adapter);
    assert(context->device);
    context->queue = wgpuDeviceGetQueue(context->device);
    assert(context->queue);

    WGPUSurfaceCapabilities caps = {0};
    wgpuSurfaceGetCapabilities(context->surface, context->adapter, &caps);
    WGPUTextureFormat chosenFormat = screen_color_format;

    if (!POST_PROCESSING_ENABLED) {context->viewport_width=context->width;context->viewport_height=context->height;}
    context->config = (WGPUSurfaceConfiguration){
        .device = context->device,
        .format = chosenFormat,
        .width = context->width,
        .height = context->height,
        .usage = WGPUTextureUsage_RenderAttachment,
        .alphaMode = WGPUCompositeAlphaMode_Auto,
        .presentMode = WGPUPresentMode_Fifo // *info* use fifo for vsync
    };
    wgpuSurfaceConfigure(context->surface, &context->config);

    // Create the global bindgroup layout + create the bindgroup
    {
        WGPUBindGroupLayoutEntry entries[1] = {
            // Per-pipeline global uniforms
            {
                .binding = 0,
                .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                .buffer.type = WGPUBufferBindingType_Uniform,
                .buffer.minBindingSize = GLOBAL_UNIFORM_CAPACITY,
                .buffer.hasDynamicOffset = 0,
            }
        };
        WGPUBindGroupLayoutDescriptor bglDesc = {0};
        bglDesc.entryCount = 1;
        bglDesc.entries = entries;
        context->global_layout = wgpuDeviceCreateBindGroupLayout(context->device, &bglDesc);
        
        {
            WGPUBufferDescriptor ubDesc = {0};
            ubDesc.size = GLOBAL_UNIFORM_CAPACITY;
            ubDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            context->global_uniform_buffer = wgpuDeviceCreateBuffer(context->device, &ubDesc);
            
            enum { entry_count = 1 };
            WGPUBindGroupEntry entries[entry_count] = {
                {
                    .binding = 0,
                    .buffer = context->global_uniform_buffer,
                    .offset = 0,
                    .size = GLOBAL_UNIFORM_CAPACITY,
                }
            };
            WGPUBindGroupDescriptor uBgDesc = {0};
            uBgDesc.layout = context->global_layout;
            uBgDesc.entryCount = entry_count;
            uBgDesc.entries = entries;
            context->global_bindgroup = wgpuDeviceCreateBindGroup(context->device, &uBgDesc);
        }
    }

    // Create the per-pipeline bind group layout (ex. the dynamic offset buffer for all materials' uniforms, the shadow texture)
    {
        #define PIPELINE_BINDGROUP_ENTRIES 5
        WGPUBindGroupLayoutEntry entries[PIPELINE_BINDGROUP_ENTRIES] = {
            // Per-material uniforms (with offset to differentiate materials)
            {
                .binding = 0,
                .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                .buffer.type = WGPUBufferBindingType_Uniform,
                .buffer.minBindingSize = MATERIAL_UNIFORM_CAPACITY,
                .buffer.hasDynamicOffset = 1,
            },
            // Shadow map texture entry
            {
                .binding = 1,
                .visibility = WGPUShaderStage_Fragment,
                .texture = {
                    .sampleType = WGPUTextureSampleType_Depth, // Depth texture
                    .viewDimension = WGPUTextureViewDimension_2D,
                    .multisampled = false,
                },
            },
            // Comparison sampler for shadow
            {
                .binding = 2,
                .visibility = WGPUShaderStage_Fragment,
                .sampler = {
                    .type = WGPUSamplerBindingType_Comparison, // Comparison sampler for shadow mapping
                },
            },
            // Cubemap texture entry
            {
                .binding = 3,
                .visibility = WGPUShaderStage_Fragment,
                .texture = {
                    .sampleType = WGPUTextureSampleType_Float,
                    .viewDimension = WGPUTextureViewDimension_Cube,
                    .multisampled = false,
                },
            },
            // Sampler for cubemap
            {
                .binding = 4,
                .visibility = WGPUShaderStage_Fragment,
                .sampler = {
                    .type = WGPUSamplerBindingType_Filtering,
                },
            },
        };
        WGPUBindGroupLayoutDescriptor bglDesc = {0};
        bglDesc.entryCount = PIPELINE_BINDGROUP_ENTRIES;
        bglDesc.entries = entries;
        context->main_pipeline_layout = wgpuDeviceCreateBindGroupLayout(context->device, &bglDesc);
    }

    // Create the per-material bind group layout (ex. the textures)
    {
        context->per_material_layout = wgpuDeviceCreateBindGroupLayout(context->device, &PER_MATERIAL_BINDGROUP_LAYOUT_DESC);
    }

    // Create per-mesh bindgroup layout (ex. the bones)
    {
        // Define a bind group layout for bones (similar to your global uniform layout)
        WGPUBindGroupLayoutEntry boneEntry = {0};
        boneEntry.binding = 0;
        boneEntry.visibility = WGPUShaderStage_Vertex;
        boneEntry.buffer.type = WGPUBufferBindingType_Uniform;
        boneEntry.buffer.minBindingSize = sizeof(context->default_bones);
        WGPUBindGroupLayoutDescriptor boneBGLDesc = {0};
        boneBGLDesc.entryCount = 1;
        boneBGLDesc.entries = &boneEntry;
        context->per_mesh_layout = wgpuDeviceCreateBindGroupLayout(context->device, &boneBGLDesc);
    }

    // Create default dummy bone data
    {
        // identity matrices
        for (int i = 0; i < MAX_BONES; i++) {
            for (int j = 0; j < 16; j++) {
                context->default_bones[i][j] = (j % 5 == 0) ? 1.0f : 0.0f;
            }
        }

        {
            WGPUBufferDescriptor bufDesc = {0};
            bufDesc.size = sizeof(context->default_bones);
            bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            context->defaultBoneBuffer = wgpuDeviceCreateBuffer(context->device, &bufDesc);

            // Upload the identity matrix to the buffer.
            wgpuQueueWriteBuffer(context->queue, context->defaultBoneBuffer, 0, context->default_bones, sizeof(context->default_bones));
        }

        // Create the default bind group with our minimal bone buffer.
        WGPUBindGroupEntry bgEntry = {0};
        bgEntry.binding = 0;
        bgEntry.buffer = context->defaultBoneBuffer;
        bgEntry.offset = 0;
        bgEntry.size = sizeof(context->default_bones);

        WGPUBindGroupDescriptor bgDesc = {0};
        bgDesc.layout = context->per_mesh_layout;
        bgDesc.entryCount = 1;
        bgDesc.entries = &bgEntry;
        context->defaultBoneBindGroup = wgpuDeviceCreateBindGroup(context->device, &bgDesc);
    }

    // Create default value for empty texture slots (1x1 texture)
    {
        unsigned char whitePixel[4] = {127,255,127,255};
        WGPUTextureDescriptor td = {0};
        td.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
        td.dimension = WGPUTextureDimension_2D;
        td.format = WGPUTextureFormat_RGBA8Unorm;
        td.size.width  = 1;
        td.size.height = 1;
        td.size.depthOrArrayLayers = 1;
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        context->defaultTexture = wgpuDeviceCreateTexture(context->device, &td);

        // Copy data
        WGPUImageCopyTexture ict = {0};
        ict.texture = context->defaultTexture;
        WGPUTextureDataLayout tdl = {0};
        tdl.bytesPerRow    = 4;
        tdl.rowsPerImage   = 1;
        WGPUExtent3D extent = {1,1,1};
        wgpuQueueWriteTexture(context->queue, &ict, whitePixel, 4, &tdl, &extent);

        // Create a view
        context->defaultTextureView = wgpuTextureCreateView(context->defaultTexture, NULL);
    }

    // Create the depth texture attachment
    {
        WGPUTextureDescriptor depthTextureDesc = {
            .usage = WGPUTextureUsage_RenderAttachment,
            .label = "DEPTH TEXTURE",
            .dimension = WGPUTextureDimension_2D,
            .size = { .width = context->viewport_width, .height = context->viewport_height, .depthOrArrayLayers = 1 },
            .format = WGPUTextureFormat_Depth32Float, // Or Depth32Float if supported
            .mipLevelCount = 1,
            .sampleCount = MSAA_ENABLED ? 4 : 1,
            .nextInChain = NULL,
        };
        WGPUTexture depthTexture = wgpuDeviceCreateTexture(context->device, &depthTextureDesc);
        // 2. Create a texture view for the depth texture
        WGPUTextureViewDescriptor depthViewDesc = {
            .label = "DEPTH TEXTURE VIEW",
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
        context->depthStencilState = (WGPUDepthStencilState){
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
        context->depthAttachment = (WGPURenderPassDepthStencilAttachment){
            .view = depthTextureView,
            .depthLoadOp = WGPULoadOp_Clear,   // Clear depth at start of pass
            .depthStoreOp = WGPUStoreOp_Discard,  // Optionally store depth results
            .depthClearValue = 1.0f,            // Clear value (far plane)
            // Set stencil values if using stencil; otherwise, leave them out.
        };
    }

    // Create a multisample texture for MSAA
    if (MSAA_ENABLED) {
        WGPUTextureDescriptor msaaDesc = {0};
        msaaDesc.usage = WGPUTextureUsage_RenderAttachment;
        msaaDesc.dimension = WGPUTextureDimension_2D;
        msaaDesc.format = context->config.format;
        msaaDesc.size.width  = context->viewport_width;
        msaaDesc.size.height = context->viewport_height;
        msaaDesc.size.depthOrArrayLayers = 1;
        msaaDesc.mipLevelCount = 1;
        msaaDesc.sampleCount   = 4; // Should match ms.count
        context->msaa_texture = wgpuDeviceCreateTexture(context->device, &msaaDesc);
        context->msaa_texture_view = wgpuTextureCreateView(context->msaa_texture, NULL);
    }

    // Create post processing pipeline and bindgroup
    if (POST_PROCESSING_ENABLED) {
        create_postprocessing_pipeline(context, context->viewport_width, context->viewport_height);
    }

    // Create global shadow pipeline + texture + sampler
    {
        WGPUTextureDescriptor shadowTextureDesc = {
            .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
            .dimension = WGPUTextureDimension_2D,
            .format = WGPUTextureFormat_Depth32Float,
            .size = (WGPUExtent3D){ .width = 1024, .height = 1024, .depthOrArrayLayers = 1 },
            .mipLevelCount = 1,
            .sampleCount = 1,
        };
        context->shadow_texture = wgpuDeviceCreateTexture(context->device, &shadowTextureDesc);
        context->shadow_texture_view = wgpuTextureCreateView(context->shadow_texture, NULL);
        
        // Create a comparison sampler for shadow sampling.
        WGPUSamplerDescriptor shadowSamplerDesc = {
            .addressModeU = WGPUAddressMode_ClampToEdge, // todo: we need a way to avoid reading beyond the shadow texture
            .addressModeV = WGPUAddressMode_ClampToEdge,
            .addressModeW = WGPUAddressMode_ClampToEdge,
            .maxAnisotropy = 1,
            .magFilter = WGPUFilterMode_Linear,
            .minFilter = WGPUFilterMode_Linear,
            .mipmapFilter = WGPUMipmapFilterMode_Nearest,
            .compare = WGPUCompareFunction_LessEqual,
        };
        context->shadow_sampler = wgpuDeviceCreateSampler(context->device, &shadowSamplerDesc);

        create_shadow_pipeline(context);
    }

    context->initialized = true;
    printf("[webgpu.c] wgpuInit done.\n");
    WGPUSupportedLimits limits = {0}; wgpuDeviceGetLimits(context->device, &limits);
    printf("[webgpu.c] max uniform buffer size: %d\n", limits.limits.maxUniformBufferBindingSize);
    printf("[webgpu.c] max buffer size: %d\n", limits.limits.maxBufferSize);
    printf("[webgpu.c] max nr of textures in array: %d\n", limits.limits.maxTextureArrayLayers);
    printf("[webgpu.c] max texture 2D dimension: %d\n", limits.limits.maxTextureDimension2D);
    #ifdef __EMSCRIPTEN__
    setup_callback();
    #endif
}

static void handle_request_device(WGPURequestDeviceStatus status, WGPUDevice device, const char* message, void* userdata) {
    WebGPUContext *context = (WebGPUContext *)userdata;
    if (status == WGPURequestDeviceStatus_Success) {
        context->device = device;
        assert(context->device);
        setup_context(context);
    } else fprintf(stderr, "[webgpu.c] RequestDevice failed: %s\n", message);
}

static void handle_request_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message, void* userdata) {
    WebGPUContext *context = (WebGPUContext *)userdata;
    if (status == WGPURequestAdapterStatus_Success) {
        context->adapter = adapter;
        assert(context->adapter);
        wgpuAdapterRequestDevice(context->adapter, NULL, handle_request_device, context);
    } else fprintf(stderr, "[webgpu.c] RequestAdapter failed: %s\n", message);
}

static void setup_gpu_device(WebGPUContext *context) {
    #ifndef __EMSCRIPTEN__
    if (FORCE_GPU_CHOICE) {
        WGPUAdapter adapters[16];
        WGPUInstanceEnumerateAdapterOptions opts = {.backends = WGPUInstanceBackend_All};
        int adapterCount = wgpuInstanceEnumerateAdapters(context->instance, &opts, adapters);
        printf("Adapters found: %d\n", adapterCount);

        WGPUAdapter selectedAdapter = NULL;
        for (size_t i = 0; i < adapterCount; i++) {
            WGPUAdapterInfo info = {0};
            wgpuAdapterGetInfo(adapters[i], &info);
            if (info.adapterType == DISCRETE_GPU ? WGPUAdapterType_DiscreteGPU : WGPUAdapterType_IntegratedGPU) {
                char *type = info.adapterType == 0 ? "Discrete"
                    : info.adapterType == 1 ? "Integrated"
                    : info.adapterType == 2 ? "CPU"
                    : info.adapterType == 3 ? "Unknown"
                    : "Undefined";
                char *backend = info.backendType == 2 ? "WebGPU"
                    : info.backendType == 3 ? "D3D11"
                    : info.backendType == 4 ? "D3D12"
                    : info.backendType == 5 ? "Metal"
                    : info.backendType == 6 ? "Vulkan"
                    : info.backendType == 7 ? "OpenGL"
                    : info.backendType == 8 ? "OpenGLES"
                    : "Undefined";
                printf("Selected GPU: %s, type: %s, backend: %s\n", info.device, type, backend);
                selectedAdapter = adapters[i];
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

        context->adapter = selectedAdapter;
        wgpuAdapterRequestDevice(context->adapter, NULL, handle_request_device, context);
        return;
    }
    #endif
    WGPURequestAdapterOptions adapter_opts = {0};
    adapter_opts.compatibleSurface = context->surface;
    adapter_opts.powerPreference = WGPUPowerPreference_HighPerformance;
    wgpuInstanceRequestAdapter(context->instance, &adapter_opts, handle_request_adapter, context);
}

#ifdef __EMSCRIPTEN__
void *createGPUContext(void (*callback)(), int width, int height, int viewport_width, int viewport_height) {
    setup_callback = callback;
#else
void *createGPUContext(void *hInstance, void *hwnd, int width, int height, int viewport_width, int viewport_height) {
#endif
    static WebGPUContext context = {0};

    #ifdef __EMSCRIPTEN__
    context.instance = wgpuCreateInstance(NULL);
    #else
    WGPUInstanceDescriptor instDesc = {0};
    WGPUInstanceExtras extras = {0};
    extras.chain.sType = WGPUSType_InstanceExtras;
    extras.backends   = WGPUInstanceBackend_GL;
    extras.flags      = WGPUInstanceFlag_DiscardHalLabels;
    extras.dx12ShaderCompiler = WGPUDx12Compiler_Undefined;
    extras.gles3MinorVersion  = WGPUGles3MinorVersion_Automatic;
    extras.dxilPath = NULL;
    extras.dxcPath  = NULL;
    instDesc.nextInChain = (const WGPUChainedStruct*)&extras;
    context.instance = wgpuCreateInstance(&instDesc);
    assert(context.instance);
    #endif

    WGPUSurfaceDescriptor surface_desc = {0};
    #ifdef __EMSCRIPTEN__
    // Use a canvas selector to identify the canvas element in the DOM
    WGPUSurfaceDescriptorFromCanvasHTMLSelector canvasDesc = {0};
    canvasDesc.chain.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;
    canvasDesc.selector = "#canvas";  // Use your canvas's CSS selector
    surface_desc.nextInChain = (WGPUChainedStruct*)&canvasDesc;
    #else
    /* WINDOWS SPECIFIC */
    WGPUSurfaceDescriptorFromWindowsHWND chained_desc = {0};
    chained_desc.chain.sType = WGPUSType_SurfaceDescriptorFromWindowsHWND;
    chained_desc.hwnd = hwnd;
    chained_desc.hinstance = hInstance;
    surface_desc.nextInChain = (const WGPUChainedStruct*)&chained_desc;
    /* WINDOWS SPECIFIC */
    #endif

    context.surface = wgpuInstanceCreateSurface(context.instance, &surface_desc);
    assert(context.surface);
    context.width = width;
    context.height = height;
    context.viewport_width = viewport_width;
    context.viewport_height = viewport_height;

    setup_gpu_device(&context);
    
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

// todo: only one pipeline, init in context creation, replace with function 'create_main_pipeline' (?)
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
    #define LAYOUT_COUNT 4
    WGPUBindGroupLayout bgls[LAYOUT_COUNT] = {
        context->global_layout,
        context->main_pipeline_layout, 
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
    // --- enable alpha blending ---
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
    ms.count = MSAA_ENABLED ? 4 : 1; // *MSAA anti aliasing* ~set it to 1 to avoid, and don't set the target to msaa texture in draw_frame
    ms.mask = 0xFFFFFFFF;
    rpDesc.multisample = ms;
    // add depth texture
    rpDesc.depthStencil = &context->depthStencilState;

    // todo: this has exception when running with windows compiler...
    WGPURenderPipeline gpu_pipeline = wgpuDeviceCreateRenderPipeline(context->device, &rpDesc);
    pipeline->pipeline = gpu_pipeline;
    
    // Create pipeline bind group
    {
        WGPUBufferDescriptor ubDesc2 = {0};
        ubDesc2.size = MATERIALS_UNIFORM_BUFFER_TOTAL_SIZE;
        ubDesc2.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        pipeline->material_uniform_buffer = wgpuDeviceCreateBuffer(context->device, &ubDesc2);

        #define ENTRY_COUNT 5
        WGPUBindGroupEntry entries[ENTRY_COUNT] = {
            {
                .binding = 0,
                .buffer = pipeline->material_uniform_buffer,
                .offset = 0,
                .size = MATERIAL_UNIFORM_CAPACITY,
            },
            {
                .binding = 1,
                .textureView = context->shadow_texture_view,
            },
            {
                .binding = 2,
                .sampler = context->shadow_sampler,
            },
            {
                .binding = 3,
                .textureView = context->cubemap_texture_view,
            },
            {
                .binding = 4,
                .sampler = context->cubemap_sampler
            }
        };

        WGPUBindGroupDescriptor uBgDesc = {0};
        uBgDesc.layout = context->main_pipeline_layout;
        uBgDesc.entryCount = ENTRY_COUNT;
        uBgDesc.entries = entries;
        pipeline->pipeline_bindgroup = wgpuDeviceCreateBindGroup(context->device, &uBgDesc);
    }
    
    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);
    
    printf("[webgpu.c] Created pipeline %d from shader: %s\n", pipeline_id, shader);
    return pipeline_id;
}
void create_postprocessing_pipeline(void *context_ptr, int viewport_width, int viewport_height) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    // Create the bind group layout for the blit pass.
    WGPUBindGroupLayoutEntry blitBglEntries[2] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = { .sampleType = WGPUTextureSampleType_UnfilterableFloat, 
                        .viewDimension = WGPUTextureViewDimension_2D, 
                        .multisampled = false }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_NonFiltering }
        }
    };
    WGPUBindGroupLayoutDescriptor blitBglDesc = {0};
    blitBglDesc.entryCount = 2;
    blitBglDesc.entries = blitBglEntries;
    WGPUBindGroupLayout blitBindGroupLayout = wgpuDeviceCreateBindGroupLayout(context->device, &blitBglDesc);

    // Create a pipeline layout using the blit bind group layout.
    WGPUPipelineLayoutDescriptor blitPlDesc = {0};
    blitPlDesc.bindGroupLayoutCount = 1;
    blitPlDesc.bindGroupLayouts = &blitBindGroupLayout;
    WGPUPipelineLayout blitPipelineLayout = wgpuDeviceCreatePipelineLayout(context->device, &blitPlDesc);

    // Load the WGSL shader module for the blit pass.
    WGPUShaderModule blitShaderModule = loadWGSL(context->device, "data/shaders/postprocess.wgsl");

    // Create the render pipeline descriptor.
    WGPURenderPipelineDescriptor blitPipelineDesc = {0};
    blitPipelineDesc.layout = blitPipelineLayout;

    // Vertex stage.
    blitPipelineDesc.vertex.module = blitShaderModule;
    blitPipelineDesc.vertex.entryPoint = "vs_main";
    // No vertex buffers needed when using vertex_index only.
    blitPipelineDesc.vertex.bufferCount = 0;

    // Fragment stage.
    WGPUFragmentState blitFragState = {0};
    blitFragState.module = blitShaderModule;
    blitFragState.entryPoint = "fs_main";
    blitFragState.targetCount = 1;
    WGPUColorTargetState blitColorTarget = {0};
    blitColorTarget.format = context->config.format;
    blitColorTarget.writeMask = WGPUColorWriteMask_All;
    blitFragState.targets = &blitColorTarget;
    blitPipelineDesc.fragment = &blitFragState;

    // Primitive state.
    WGPUPrimitiveState primState = {0};
    primState.topology = WGPUPrimitiveTopology_TriangleList;
    primState.cullMode = WGPUCullMode_Back;
    primState.frontFace = WGPUFrontFace_CCW;
    blitPipelineDesc.primitive = primState;

    // Multisample state.
    WGPUMultisampleState msState = {0};
    msState.count = 1;
    msState.mask = 0xFFFFFFFF;
    blitPipelineDesc.multisample = msState;

    // Create the render pipeline.
    context->post_processing_pipeline = wgpuDeviceCreateRenderPipeline(context->device, &blitPipelineDesc);

    // Create the bind group for the blit pass.
    // Create a sampler
    WGPUSamplerDescriptor samplerDesc = {0};
    samplerDesc.minFilter = WGPUMipmapFilterMode_Nearest;
    samplerDesc.magFilter = WGPUMipmapFilterMode_Nearest;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    samplerDesc.lodMinClamp = 0;
    samplerDesc.lodMaxClamp = 0;
    samplerDesc.maxAnisotropy = 1;
    samplerDesc.addressModeU = WGPUAddressMode_Repeat;
    samplerDesc.addressModeV = WGPUAddressMode_Repeat;
    samplerDesc.addressModeW = WGPUAddressMode_Repeat;
    context->post_processing_sampler = wgpuDeviceCreateSampler(context->device, &samplerDesc);
    // Create an offscreen texture to render to (with copy capability).
    WGPUTextureDescriptor ppTexDesc = {0};
    ppTexDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding;
    ppTexDesc.dimension = WGPUTextureDimension_2D;
    ppTexDesc.format = context->config.format;
    ppTexDesc.size.width  = viewport_width;
    ppTexDesc.size.height = viewport_height;
    ppTexDesc.size.depthOrArrayLayers = 1;
    ppTexDesc.mipLevelCount = 1;
    ppTexDesc.sampleCount = 1;
    context->post_processing_texture = wgpuDeviceCreateTexture(context->device, &ppTexDesc);
    context->post_processing_texture_view = wgpuTextureCreateView(context->post_processing_texture, NULL);
    WGPUBindGroupEntry blitBgEntries[2] = {
        {
            .binding = 0,
            .textureView = context->post_processing_texture_view
        },
        {
            .binding = 1,
            .sampler = context->post_processing_sampler  // create this sampler during setup
        }
    };
    WGPUBindGroupDescriptor blitBgDesc = {0};
    blitBgDesc.layout = blitBindGroupLayout;
    blitBgDesc.entryCount = 2;
    blitBgDesc.entries = blitBgEntries;
    context->post_processing_bindgroup = wgpuDeviceCreateBindGroup(context->device, &blitBgDesc);

    // Cleanup temporary objects not stored in the context.
    wgpuShaderModuleRelease(blitShaderModule);
    wgpuPipelineLayoutRelease(blitPipelineLayout);
    wgpuBindGroupLayoutRelease(blitBindGroupLayout);
    printf("[webgpu.c] Created postprocessing pipeline \n");
}
void create_shadow_pipeline(void *context_ptr) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;

    WGPUBindGroupLayout layouts[2] = {
        context->global_layout, context->per_mesh_layout
    };
    assert(context->global_layout);
    assert(context->main_pipeline_layout);

    // 2. Create a pipeline layout for the shadow pipeline
    WGPUPipelineLayoutDescriptor shadowPLDesc = {0};
    shadowPLDesc.bindGroupLayoutCount = 2;
    shadowPLDesc.bindGroupLayouts = layouts;
    WGPUPipelineLayout shadowPipelineLayout = wgpuDeviceCreatePipelineLayout(context->device, &shadowPLDesc);
    assert(shadowPipelineLayout);

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
    shadowRPDesc.fragment = NULL;

    // Set primitive state (you can adjust as needed).
    WGPUPrimitiveState primState = {0};
    primState.topology = WGPUPrimitiveTopology_TriangleList;
    primState.cullMode = WGPUCullMode_Front; // todo: why does cull front seem to behave like cull back for shadows (?) the light view proj maybe (?)
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
    context->shadow_pipeline = wgpuDeviceCreateRenderPipeline(context->device, &shadowRPDesc);
    assert(context->shadow_pipeline);

    // Optionally, release the shader module and pipeline layout if no longer needed.
    wgpuShaderModuleRelease(shadowShaderModule);
    wgpuPipelineLayoutRelease(shadowPipelineLayout);
    printf("[webgpu.c] Created shadow pipeline \n");
}

int createGPUMesh(void *context_ptr, int pipeline_id, enum MeshFlags flags, void *v, int vc, void *i, int ic, void *ii, int iic) {
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
    // todo: animations should become per-instance instead of per-mesh
    {
        mesh->mesh_bindgroup = context->defaultBoneBindGroup;
        mesh->bone_buffer = context->defaultBoneBuffer;
        mesh->bones = (float *)context->default_bones;
        mesh->frame_count = 1;
        memcpy(mesh->current_bones, context->default_bones, sizeof(mesh->current_bones));
        mesh->current_frame = 0.;
    }
    
    mesh->flags = flags;

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
    mesh->flags = mesh->flags | MESH_ANIMATED;
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

int load_cube_map(void *context_ptr, void *data[6], int face_size) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    WGPUTextureDescriptor texDesc = {
        .usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){ .width = face_size, .height = face_size, .depthOrArrayLayers = 6 },
        .mipLevelCount = 1,
        .sampleCount = 1,
        .format = WGPUTextureFormat_RGBA8Unorm,
    };
    context->cubemap_texture = wgpuDeviceCreateTexture(context->device, &texDesc);
    for (int face = 0; face < 6; face++) {
        WGPUImageCopyTexture copyTex = {
            .texture = context->cubemap_texture,
            .mipLevel = 0,
            .origin = (WGPUOrigin3D){ .x = 0, .y = 0, .z = face },
        };
        WGPUTextureDataLayout tdl = {0};
        tdl.bytesPerRow  = 4 * face_size; // 4 bytes per pixel (RGBA)
        tdl.rowsPerImage = face_size;
        WGPUExtent3D ext = { .width = (uint32_t)face_size, .height = (uint32_t)face_size, .depthOrArrayLayers = 1 };
        wgpuQueueWriteTexture(context->queue, &copyTex, data[face], (size_t)(4 * face_size * face_size), &tdl, &ext);
    }
    WGPUTextureViewDescriptor viewDesc = {
        .dimension = WGPUTextureViewDimension_Cube,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = 6,
    };
    context->cubemap_texture_view = wgpuTextureCreateView(context->cubemap_texture, &viewDesc);
    WGPUSamplerDescriptor samplerDesc = {
        .label = "CubeSampler",
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUFilterMode_Linear,
        .lodMinClamp = 0.0f,
        .lodMaxClamp = 1000.0f, // a high value to allow all mip levels
        .maxAnisotropy = 1,
    };
    context->cubemap_sampler = wgpuDeviceCreateSampler(context->device, &samplerDesc);
    
}

#pragma region UNIFORMS
int addGPUGlobalUniform(void *context_ptr, int pipeline_id, const void* data, int data_size) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    // Inline alignment determination using ternary operators
    int alignment = (data_size <= 4) ? 4 :
                    (data_size <= 8) ? 8 :
                    16; // Default for vec3, vec4, mat4x4, or larger
    // Align the offset to the correct boundary (based on WGSL rules)
    int aligned_offset = (context->global_uniform_offset + (alignment - 1)) & ~(alignment - 1);
    // Check if the new offset exceeds buffer capacity
    if (aligned_offset + data_size > GLOBAL_UNIFORM_CAPACITY) {
        // todo: print warning on screen or in log that this failed
        return -1;
    }
    // Copy the data into the aligned buffer
    memcpy(context->global_uniform_data + aligned_offset, data, data_size);
    // Update the current offset
    context->global_uniform_offset = aligned_offset + data_size;
    // todo: print on screen that uniform changed
    return aligned_offset;
}
void setGPUGlobalUniformValue(void *context_ptr, int pipeline_id, int offset, const void* data, int dataSize) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    if (offset < 0 || offset + dataSize > context->global_uniform_offset) {
        // todo: print warning on screen or in log that this failed
        return;
    }
    memcpy(context->global_uniform_data + offset, data, dataSize);
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

static void fenceCallback(WGPUQueueWorkDoneStatus status, WGPU_NULLABLE void *userdata) {
    bool *done = (bool*)userdata;
    *done = true;
}
double block_on_gpu_queue(void *context_ptr, struct Platform *p) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    volatile bool workDone = false;
    double time_before_ns = p->current_time_ms();

    // Request notification when the GPU work is done.
    wgpuQueueOnSubmittedWorkDone(context->queue, fenceCallback, (void*)&workDone);

    // Busy-wait until the flag is set.
    while (!workDone) {
        #ifdef __EMSCRIPTEN__
        p->sleep_ms(1.0);
        #else
        wgpuDevicePoll(context->device, true, NULL); // blocks internally with 'true' set, to avoid wasting cpu resources
        #endif
    }
    return p->current_time_ms() - time_before_ns;
}
#ifndef __EMSCRIPTEN__
static void bufferMapCallback(WGPUBufferMapAsyncStatus status, void *userdata) {
    bool *mappingDone = (bool *)userdata;
    if (status == WGPUBufferMapAsyncStatus_Success) {
        printf("Buffer mapped successfully.\n");
    } else {
        fprintf(stderr, "Buffer mapping failed with status: %d\n", status);
    }
    *mappingDone = true;
}
#endif

struct draw_result drawGPUFrame(void *context_ptr, struct Platform *p, int offset_x, int offset_y, int viewport_width, int viewport_height, int save_to_disk, char *filename) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    struct draw_result result = {0};
    double start_ms = p->current_time_ms();
    double mut_ms = p->current_time_ms();
    // acquire the surface texture
    wgpuSurfaceGetCurrentTexture(context->surface, &context->currentSurfaceTexture);
    // if not available, return early and notify that the surface is not yet available to be rendered to
    if (context->currentSurfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success) {
        result.cpu_ms = p->current_time_ms() - start_ms;
        result.surface_not_available = 1;
        return result;
    }
    result.get_surface_ms = p->current_time_ms() - mut_ms; mut_ms = p->current_time_ms();
    WGPUTextureViewDescriptor d = {
        .format = screen_color_format,
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = 1,
        .nextInChain = NULL,
    };
    context->swapchain_view = wgpuTextureCreateView(context->currentSurfaceTexture.texture, &d);
    WGPUCommandEncoderDescriptor encDesc = {0};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(context->device, &encDesc);
    WGPURenderPassColorAttachment colorAtt = {0};
    if (POST_PROCESSING_ENABLED && MSAA_ENABLED) { colorAtt.view = context->msaa_texture_view; colorAtt.resolveTarget = context->post_processing_texture_view;}
    else if (POST_PROCESSING_ENABLED) { colorAtt.view = context->post_processing_texture_view;}
    else if (MSAA_ENABLED) { colorAtt.view = context->msaa_texture_view; colorAtt.resolveTarget = context->swapchain_view;}
    else { colorAtt.view = context->swapchain_view;}
    colorAtt.loadOp = WGPULoadOp_Clear;
    colorAtt.storeOp = WGPUStoreOp_Store;
    colorAtt.clearValue = (WGPUColor){0., 0., 0., 1.0};
    colorAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    WGPURenderPassDescriptor passDesc = {0};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAtt;
    passDesc.depthStencilAttachment = &context->depthAttachment;
    result.setup_ms = p->current_time_ms() - mut_ms; mut_ms = p->current_time_ms();

    #pragma region SAVE TO DISK
    #ifndef __EMSCRIPTEN__
    WGPUBuffer stagingBuffer;
    WGPUImageCopyTexture src;
    WGPUImageCopyBuffer dst;
    WGPUExtent3D extent;
    uint32_t bytes_per_pixel = 4;
    uint32_t unpadded_bytes_per_row = viewport_width * bytes_per_pixel;
    uint32_t padded_bytes_per_row = (unpadded_bytes_per_row + 255) & ~255;
    size_t buffer_size = padded_bytes_per_row * viewport_height;
    if (save_to_disk) {
        // Create staging buffer
        WGPUBufferDescriptor stagingBufferDesc = {0};
        stagingBufferDesc.size = buffer_size;
        stagingBufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        stagingBuffer = wgpuDeviceCreateBuffer(context->device, &stagingBufferDesc);

        // Set up the copy: define the source (the rendered texture) and destination (the staging buffer).
        src = (WGPUImageCopyTexture) {
            .texture = context->post_processing_texture,
            .mipLevel = 0,
            .origin = {0, 0, 0},
            .aspect = WGPUTextureAspect_All
        };

        dst = (WGPUImageCopyBuffer) {0};
        dst.buffer = stagingBuffer;
        dst.layout = (WGPUTextureDataLayout) {
            .offset = 0,
            .bytesPerRow = padded_bytes_per_row,
            .rowsPerImage = viewport_height,
        };

        extent = (WGPUExtent3D) {
            .width = viewport_width,
            .height = viewport_height,
            .depthOrArrayLayers = 1
        };
    }
    #endif
    #pragma endregion
    
    // Write CPU–side uniform data to GPU
    // todo: condition to only do when updated data
    wgpuQueueWriteBuffer(context->queue, context->global_uniform_buffer, 0, context->global_uniform_data, GLOBAL_UNIFORM_CAPACITY);
    result.global_uniforms_ms = p->current_time_ms() - mut_ms; mut_ms = p->current_time_ms();

    // SHADOW PASS
    // Reuse the global pipeline uniform data in the shader uniforms // todo: is it possible to reuse the same gpu-buffer and write only once?
    // wgpuQueueWriteBuffer(context->queue, context->shadow_uniform_buffer, 0, context->pipelines[0].global_uniform_data, GLOBAL_UNIFORM_CAPACITY);
    if (SHADOWS_ENABLED) {
        // Set up a render pass descriptor for the shadow pass.
        // This render pass uses no color attachment (depth-only).
        WGPURenderPassDepthStencilAttachment shadowDepthAttachment = {0};
        shadowDepthAttachment.view = context->shadow_texture_view;
        shadowDepthAttachment.depthLoadOp = WGPULoadOp_Clear;
        shadowDepthAttachment.depthStoreOp = WGPUStoreOp_Store;
        shadowDepthAttachment.depthClearValue = 1.0f;
        WGPURenderPassDescriptor shadowPassDesc = {0};
        shadowPassDesc.colorAttachmentCount = 0; // no color attachments
        shadowPassDesc.depthStencilAttachment = &shadowDepthAttachment;   

        // Shadow render pass.
        WGPURenderPassEncoder shadowPass = wgpuCommandEncoderBeginRenderPass(encoder, &shadowPassDesc);

        // 4. Bind the shadow pipeline.
        wgpuRenderPassEncoderSetPipeline(shadowPass, context->shadow_pipeline);
        wgpuRenderPassEncoderSetBindGroup(shadowPass, 0, context->global_bindgroup, 0, NULL);

        // 5. For each mesh that casts shadows, draw
        for (int pipeline_id = 0; pipeline_id < MAX_PIPELINES; pipeline_id++) {
            if (context->pipelines[pipeline_id].used) {
                Pipeline *pipeline = &context->pipelines[pipeline_id];
                for (int j = 0; j < MAX_MATERIALS && pipeline->material_ids[j] > -1; j++) {
                    int material_id = pipeline->material_ids[j];
                    Material *material = &context->materials[material_id];
                    for (int k = 0; k < MAX_MESHES && material->mesh_ids[k] > -1; k++) {
                        int mesh_id = material->mesh_ids[k];
                        Mesh *mesh = &context->meshes[mesh_id];
                        if (mesh->flags & MESH_CAST_SHADOWS) {
                            // todo: make this based on mesh setting USE_BONES (but needs to be reset if previous?)
                            if (1) {
                                wgpuRenderPassEncoderSetBindGroup(shadowPass, 1, mesh->mesh_bindgroup, 0, NULL);
                            }
                            // todo: make this based on mesh setting UPDATE_MESH_INSTANCES, then we don't need to do this for static meshes
                            // todo: this write buffer is duplicated in both render passes
                            // If the mesh requires instance data updates, update the instance buffer (this is really expensive!)
                            if (1) {
                                unsigned long long instanceDataSize = VERTEX_LAYOUT[1].arrayStride * mesh->instance_count;
                                // write RAM instances to GPU instances
                                wgpuQueueWriteBuffer(context->queue,mesh->instanceBuffer,0,mesh->instances, instanceDataSize);
                            }
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
        }

        // 6. End the shadow render pass
        wgpuRenderPassEncoderEnd(shadowPass);
        wgpuRenderPassEncoderRelease(shadowPass);
    }
    result.shadowmap_ms = p->current_time_ms() - mut_ms; mut_ms = p->current_time_ms();

    // Main rendering pass
    WGPURenderPassEncoder main_pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    
    // set scissor to render only to the extent of the viewport
    wgpuRenderPassEncoderSetViewport(
        main_pass,
        offset_x,   // x
        offset_y,   // y
        viewport_width,   // width
        viewport_height,   // height
        0.0f,      // minDepth
        1.0f       // maxDepth
    );
    wgpuRenderPassEncoderSetScissorRect(
        main_pass,
        (uint32_t)offset_x, (uint32_t)offset_y,
        (uint32_t)viewport_width, (uint32_t)viewport_height
    );


    for (int pipeline_id = 0; pipeline_id < MAX_PIPELINES; pipeline_id++) {
        if (context->pipelines[pipeline_id].used) {

            Pipeline *pipeline = &context->pipelines[pipeline_id];
            // Set the render pipeline // todo: does it matter where we call this?
            // todo: is it possible to create the command/renderpass encoders, set the pipeline once at the beginning, and then keep reusing it?
            wgpuRenderPassEncoderSetPipeline(main_pass, pipeline->pipeline);
            wgpuRenderPassEncoderSetBindGroup(main_pass, 0, context->global_bindgroup, 0, NULL);

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
                wgpuRenderPassEncoderSetBindGroup(main_pass, 1, pipeline->pipeline_bindgroup, 1, &material_uniform_offset);
                // Bind per-material bind group (group 1) // todo: we could fit all the textures into the pipeline, up to 1000 bindings in one bindgroup -> avoid this call for every material
                wgpuRenderPassEncoderSetBindGroup(main_pass, 2, material->material_bindgroup, 0, NULL);

                for (int k = 0; k < MAX_MESHES && material->mesh_ids[k] > -1; k++) {
                    
                    int mesh_id = material->mesh_ids[k];
                    if (!context->meshes[mesh_id].used) {printf("[FATAL WARNING] An unused mesh (%d) was left in the material's (%d) list of mesh ids\n", mesh_id, material_id); continue;}
                    Mesh *mesh = &context->meshes[mesh_id];
                    
                    // todo: based on mesh setting PLAYING_ANIMATION or something
                    if (mesh->flags & MESH_ANIMATED) {
                        updateMeshAnimationFrame(context, mesh_id);
                        wgpuQueueWriteBuffer(context->queue,mesh->bone_buffer,0,mesh->current_bones, sizeof(context->default_bones));
                    }
                    // todo: make this based on mesh setting USE_BONES (but needs to be reset if previous?)
                    if (1) {
                        wgpuRenderPassEncoderSetBindGroup(main_pass, 3, mesh->mesh_bindgroup, 0, NULL);
                    }
                    // todo: make this based on mesh setting UPDATE_MESH_INSTANCES, then we don't need to do this for static meshes
                    // If the mesh requires instance data updates, update the instance buffer (this is really expensive!)
                    if (1) {
                        unsigned long long instanceDataSize = VERTEX_LAYOUT[1].arrayStride * mesh->instance_count;
                        // write RAM instances to GPU instances
                        wgpuQueueWriteBuffer(context->queue,mesh->instanceBuffer,0,mesh->instances, instanceDataSize);
                    }

                    // Instanced mesh: bind its vertex + instance buffer (slots 0 and 1) and draw with instance_count
                    wgpuRenderPassEncoderSetVertexBuffer(main_pass, 0, mesh->vertexBuffer, 0, WGPU_WHOLE_SIZE);
                    wgpuRenderPassEncoderSetVertexBuffer(main_pass, 1, mesh->instanceBuffer, 0, WGPU_WHOLE_SIZE);
                    wgpuRenderPassEncoderSetIndexBuffer(main_pass, mesh->indexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
                    // todo: considering this allows a base vertex, first index and first instance,
                    // todo: is it then not possible to put all vertex, index and instance buffers together, 
                    // todo: and loop just over this call instead of calling setVertexBuffer in a loop (?)
                    wgpuRenderPassEncoderDrawIndexed(main_pass, mesh->indexCount,mesh->instance_count,0,0,0);
                    // todo: wgpuRenderPassEncoderDrawIndexedIndirect
                }
            }
        }
    }

    // End the render pass.
    wgpuRenderPassEncoderEnd(main_pass);
    wgpuRenderPassEncoderRelease(main_pass);
    
    result.main_pass_ms = p->current_time_ms() - mut_ms; mut_ms = p->current_time_ms();

    // save to disk
    #ifndef __EMSCRIPTEN__
    if (save_to_disk) {
        // Add the copy command to the command encoder to get the data later for saving to disk
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);

        // Map the staging buffer to CPU memory.
        void *mappedData = NULL;
        int mappingCompleted = 0;
        wgpuBufferMapAsync(stagingBuffer, WGPUMapMode_Read, 0, buffer_size, bufferMapCallback, &mappingCompleted);

        // Poll until the mapping is done.
        while (!mappingCompleted) {
            wgpuDevicePoll(context->device, false, NULL);
        }
        // You will need to wait or poll until mapping is complete.
        // For simplicity, assume mapping is synchronous in this example.
        mappedData = wgpuBufferGetMappedRange(stagingBuffer, 0, buffer_size);

        // Create a contiguous image buffer by copying each row to remove the padded bytes.
        printf("row: %d, height: %d\n", unpadded_bytes_per_row, viewport_height);
        unsigned char *image = malloc(unpadded_bytes_per_row * viewport_height);
        for (int y = 0; y < viewport_height; y++) {
            memcpy(image + y * unpadded_bytes_per_row,
                (unsigned char*)mappedData + y * padded_bytes_per_row,
                unpadded_bytes_per_row);
        }

        // Write out the image using stb_image_write
        // todo: instead use platform to do a save binary file
        // if (stbi_write_png(filename, viewport_width, viewport_height, 4, image, viewport_width * bytes_per_pixel)) {
        //     printf("Screenshot saved successfully.\n");
        // } else {
        //     printf("Failed to save screenshot.\n");
        // }

        // Clean up
        free(image);
        wgpuBufferUnmap(stagingBuffer);
        wgpuBufferRelease(stagingBuffer);
    }
    #endif

    // Final blit
    if (POST_PROCESSING_ENABLED) {
        // Set up a render pass targeting the swap chain.
        WGPURenderPassColorAttachment finalColorAtt = {0};
        finalColorAtt.view = context->swapchain_view;
        finalColorAtt.loadOp = WGPULoadOp_Clear;
        finalColorAtt.storeOp = WGPUStoreOp_Store;
        finalColorAtt.clearValue = (WGPUColor){1.0, 0.0, 1.0, 1.0};

        WGPURenderPassDescriptor finalPassDesc = {0};
        finalPassDesc.colorAttachmentCount = 1;
        finalPassDesc.colorAttachments = &finalColorAtt;

        // Begin the final render pass.
        WGPURenderPassEncoder finalPass = wgpuCommandEncoderBeginRenderPass(encoder, &finalPassDesc);

        // Bind your simple post-processing pipeline.
        // This pipeline should use a vertex shader that outputs full-screen positions
        // and a fragment shader that samples from context->post_processing_texture.
        wgpuRenderPassEncoderSetPipeline(finalPass, context->post_processing_pipeline);
        wgpuRenderPassEncoderSetBindGroup(finalPass, 0, context->post_processing_bindgroup, 0, NULL);

        // Draw a full-screen triangle (3 vertices) or quad.
        wgpuRenderPassEncoderDraw(finalPass, 3, 1, 0, 0);

        wgpuRenderPassEncoderEnd(finalPass);
        wgpuRenderPassEncoderRelease(finalPass);
    }

    // Finish command encoding and submit.
    WGPUCommandBufferDescriptor cmdDesc = {0};
    WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuCommandEncoderRelease(encoder);
    wgpuQueueSubmit(context->queue, 1, &cmdBuf);
    wgpuCommandBufferRelease(cmdBuf);
    result.submit_ms = p->current_time_ms() - mut_ms; mut_ms = p->current_time_ms();

    // time the draw calls and frame setup on cpu (full time from start to finish)
    double current_time = p->current_time_ms();
    result.cpu_ms = current_time - start_ms;
    mut_ms = current_time;

    // Present the surface.
    #ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(context->surface);
    #endif
    wgpuTextureViewRelease(context->swapchain_view);
    context->swapchain_view = NULL;
    wgpuTextureRelease(context->currentSurfaceTexture.texture);
    context->currentSurfaceTexture.texture = NULL;
    
    // time spent waiting to present to surface
    result.present_wait_ms = p->current_time_ms() - mut_ms;

    return result;
}
