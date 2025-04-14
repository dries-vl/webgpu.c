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
static const WGPUTextureFormat depth_stencil_format = WGPUTextureFormat_Depth32Float;
static const WGPUVertexBufferLayout VERTEX_LAYOUT[2] = {
    {   // Vertex layout
        .arrayStride = sizeof(struct Vertex),
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
        .arrayStride = sizeof(struct Instance),
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
            { .format = WGPUVertexFormat_Unorm16x2, .offset = 92,  .shaderLocation = 15 },  // atlas_uv[2] (4 bytes)
}
    }
};
#pragma endregion

#pragma region STRUCT DEFINITIONS
typedef struct {
    bool               used;
    int                pipeline_id;
    int                mesh_ids[MAX_MESHES];
} Material;

typedef struct {
    bool       used;
    enum MeshFlags  flags;
    int        material_id;
    // todo: do we even need this struct and the material struct at all (?)
    int vertex_count; uint32_t first_vertex;
    int index_count; uint32_t first_index;
    int instance_count; uint32_t first_instance;
    void *instances; // instances in RAM (verts and indices are not kept in RAM) // todo: Instance instead of void
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
    WGPURenderPipeline    main_pipeline;
    Material              materials[MAX_MATERIALS];
    Mesh                  meshes[MAX_MESHES];
    // current frame objects (global for simplicity)     // todo: make a bunch of these static to avoid global bloat
    WGPUSurfaceTexture    currentSurfaceTexture;
    WGPUTextureView       swapchain_view;
    // draw indirect buffers
    WGPUBuffer indirect_draw_buffer; int indirect_count;
    WGPUBuffer indirect_count_buffer; // todo: for later, when we do gpu-culling
    // scene buffers
    WGPUBuffer vertices; uint64_t vertex_count;
    WGPUBuffer indices; uint64_t index_count;
    WGPUBuffer instances; uint64_t instance_count;
    WGPUTexture animations; WGPUTextureView animations_view; WGPUSampler animations_sampler; uint64_t animation_count;
    WGPUTexture texture_array; WGPUTextureView texture_array_view; WGPUSampler texture_array_sampler; uint64_t texture_count;
    // optional postprocessing with intermediate texture
    WGPURenderPipeline    post_processing_pipeline;
    WGPUTexture           post_processing_texture;
    WGPUTextureView       post_processing_texture_view;
    WGPUSampler           post_processing_sampler;
    WGPUBindGroup         post_processing_bindgroup;
    // shadow texture // todo: cascading shadow maps
    WGPURenderPipeline shadow_pipeline;
    WGPUTexture        shadow_texture;
    WGPUTextureView    shadow_texture_view;
    WGPUSampler        shadow_sampler;
    WGPUBindGroup      shadow_bindgroup;
    // depth texture
    WGPUDepthStencilState depthStencilState;
    WGPURenderPassDepthStencilAttachment depthAttachment;
    // msaa texture
    WGPUTexture     msaa_texture;
    WGPUTextureView msaa_texture_view;
    // reflection cubemap
    WGPUTexture        cubemap_texture;
    WGPUTextureView    cubemap_texture_view;
    WGPUSampler        cubemap_sampler;
    // global bindgroup
    WGPUBindGroupLayout global_layout;
    WGPUBindGroup       global_bindgroup;
    WGPUBuffer          global_uniform_buffer;
    WGPUBuffer          material_uniform_buffer;
} WebGPUContext;
#pragma endregion

static void writeDataToTexture(void *context_ptr, WGPUTexture *tex, void *data, int w, int h, uint64_t offset, int layer) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    WGPUImageCopyTexture ict = {0};
    ict.texture = *tex;
    ict.origin.y = (offset / 4) / w;
    ict.origin.z = layer;
    WGPUTextureDataLayout tdl = {0};
    tdl.bytesPerRow  = 4 * w; // 4 bytes per pixel (RGBA)
    tdl.rowsPerImage = h;
    WGPUExtent3D ext = { .width = (uint32_t)w, .height = (uint32_t)h, .depthOrArrayLayers = 1 };
    wgpuQueueWriteTexture(context->queue, &ict, data, (size_t)(4 * w * h), &tdl, &ext);
}

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
        enum { entry_count = 10 };
        WGPUBindGroupLayoutEntry layout_entries[entry_count] = {
            // Global uniforms
            {
                .binding = 0,
                .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                .buffer.type = WGPUBufferBindingType_Uniform,
                .buffer.minBindingSize = GLOBAL_UNIFORM_CAPACITY,
            },
            // todo: add all the other stuff here as well: shadowmap, env cube, material uniforms buffer
            // Animations Sampler
            {
                .binding = 1,
                .visibility = WGPUShaderStage_Vertex,
                .sampler = { .type = WGPUSamplerBindingType_NonFiltering }
            },
            // Animations Texture
            {
                .binding = 2, 
                .visibility = WGPUShaderStage_Vertex, 
                .texture = {.sampleType = WGPUTextureSampleType_UnfilterableFloat, .viewDimension = WGPUTextureViewDimension_2D, .multisampled = false}
            },
            // Textures Sampler
            {
                .binding = 3,
                .visibility = WGPUShaderStage_Fragment,
                .sampler = { .type = WGPUSamplerBindingType_Filtering }
            },
            // Textures // todo: second texture array for fonts (?)
            {
                .binding = 4, 
                .visibility = WGPUShaderStage_Fragment, 
                .texture = {.sampleType = WGPUTextureSampleType_Float, .viewDimension = WGPUTextureViewDimension_2DArray, .multisampled = false}
            },
            // Per-material uniforms (array index to differentiate materials)
            {
                .binding = 5,
                .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                .buffer.type = WGPUBufferBindingType_Uniform,
                .buffer.minBindingSize = UNIFORM_BUFFER_MAX_SIZE
            },
            // Shadow map texture entry
            {
                .binding = 6,
                .visibility = WGPUShaderStage_Fragment,
                .texture = {
                    .sampleType = WGPUTextureSampleType_Depth, // Depth texture
                    .viewDimension = WGPUTextureViewDimension_2D,
                    .multisampled = false,
                },
            },
            // Comparison sampler for shadow
            {
                .binding = 7,
                .visibility = WGPUShaderStage_Fragment,
                .sampler = {
                    .type = WGPUSamplerBindingType_Comparison, // Comparison sampler for shadow mapping
                },
            },
            // Cubemap texture entry
            {
                .binding = 8,
                .visibility = WGPUShaderStage_Fragment,
                .texture = {
                    .sampleType = WGPUTextureSampleType_Float,
                    .viewDimension = WGPUTextureViewDimension_Cube,
                    .multisampled = false,
                },
            },
            // Sampler for cubemap
            {
                .binding = 9,
                .visibility = WGPUShaderStage_Fragment,
                .sampler = {
                    .type = WGPUSamplerBindingType_Filtering,
                },
            }
        };
        WGPUBindGroupLayoutDescriptor bglDesc = {0};
        bglDesc.entryCount = entry_count;
        bglDesc.entries = layout_entries;
        context->global_layout = wgpuDeviceCreateBindGroupLayout(context->device, &bglDesc);

        // Create Global uniform buffer
        WGPUBufferDescriptor ubDesc = {0};
        ubDesc.size = GLOBAL_UNIFORM_CAPACITY;
        ubDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        context->global_uniform_buffer = wgpuDeviceCreateBuffer(context->device, &ubDesc);

        // Create material uniforms buffer
        WGPUBufferDescriptor ubDesc2 = {0};
        ubDesc2.size = UNIFORM_BUFFER_MAX_SIZE;
        ubDesc2.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        context->material_uniform_buffer = wgpuDeviceCreateBuffer(context->device, &ubDesc2);

        // Create animations texture
        #define ANIMATION_LIMIT 200
        WGPUTextureDescriptor animTexDesc = {.size={.depthOrArrayLayers=1, .width=ANIMATION_SIZE / 4, .height=ANIMATION_LIMIT}, .dimension=WGPUTextureDimension_2D,
        .format=WGPUTextureFormat_RGBA8Unorm, .usage=WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst, .mipLevelCount = 1, .sampleCount = 1, .label = "Animation Texture"};
        WGPUTextureViewDescriptor animViewDesc = {.format = animTexDesc.format, .dimension = WGPUTextureViewDimension_2D, .mipLevelCount = 1, .arrayLayerCount = 1, 
        .label = "Animation Texture View"};
        WGPUSamplerDescriptor animSamplerDesc = {.label = "Animation Sampler", .minFilter = WGPUFilterMode_Nearest, .magFilter = WGPUFilterMode_Nearest, .mipmapFilter = WGPUMipmapFilterMode_Nearest,
        .maxAnisotropy = 1, .addressModeU = WGPUAddressMode_ClampToEdge, .addressModeV = WGPUAddressMode_ClampToEdge, .addressModeW = WGPUAddressMode_ClampToEdge};
        context->animations = wgpuDeviceCreateTexture(context->device, &animTexDesc);
        context->animations_view = wgpuTextureCreateView(context->animations, &animViewDesc);
        context->animations_sampler = wgpuDeviceCreateSampler(context->device, &animSamplerDesc);

        // Create texture array
        #define TEXTURE_LIMIT 256
        WGPUTextureDescriptor texDesc = {.size={.depthOrArrayLayers=TEXTURE_LIMIT, .width=TEXTURE_SIZE, .height=TEXTURE_SIZE}, .dimension=WGPUTextureDimension_2D,
        .format=WGPUTextureFormat_RGBA8Unorm, .usage=WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst, .mipLevelCount = 1, .sampleCount = 1, .label = "Textures array"};
        WGPUTextureViewDescriptor viewDesc = {.format = animTexDesc.format, .dimension = WGPUTextureViewDimension_2DArray, .mipLevelCount = 1, .arrayLayerCount = TEXTURE_LIMIT, 
        .label = "Textures array View"};
        WGPUSamplerDescriptor samplerDesc = {.label = "Textures array Sampler", .minFilter = WGPUFilterMode_Linear, .magFilter = WGPUFilterMode_Linear, .mipmapFilter = WGPUMipmapFilterMode_Linear,
        .maxAnisotropy = 1, .addressModeU = WGPUAddressMode_Repeat, .addressModeV = WGPUAddressMode_Repeat, .addressModeW = WGPUAddressMode_Repeat};
        context->texture_array = wgpuDeviceCreateTexture(context->device, &texDesc);
        context->texture_array_view = wgpuTextureCreateView(context->texture_array, &viewDesc);
        context->texture_array_sampler = wgpuDeviceCreateSampler(context->device, &samplerDesc);

        // Create shadow texture + sampler
        {
            WGPUTextureDescriptor shadowTextureDesc = {
                .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
                .label = "shadow texture",
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
        }

        // Create env cube texture + sampler
        {
            WGPUTextureDescriptor texDesc = {
                .usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding,
                .label = "env cube texture",
                .dimension = WGPUTextureDimension_2D,
                .size = (WGPUExtent3D){ .width = ENV_TEXTURE_SIZE, .height = ENV_TEXTURE_SIZE, .depthOrArrayLayers = 6 },
                .mipLevelCount = 1,
                .sampleCount = 1,
                .format = WGPUTextureFormat_RGBA8Unorm,
            };
            context->cubemap_texture = wgpuDeviceCreateTexture(context->device, &texDesc);
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

        WGPUBindGroupEntry entries[entry_count] = {
            {
                .binding = 0,
                .buffer = context->global_uniform_buffer,
                .offset = 0,
                .size = GLOBAL_UNIFORM_CAPACITY,
            },
            {
                .binding = 1,
                .sampler = context->animations_sampler,
            },
            {
                .binding = 2,
                .textureView = context->animations_view,
            },
            {
                .binding = 3,
                .sampler = context->texture_array_sampler,
            },
            {
                .binding = 4,
                .textureView = context->texture_array_view,
            },
            {
                .binding = 5,
                .buffer = context->material_uniform_buffer,
                .offset = 0,
                .size = UNIFORM_BUFFER_MAX_SIZE,
            },
            {
                .binding = 6,
                .textureView = context->shadow_texture_view,
            },
            {
                .binding = 7,
                .sampler = context->shadow_sampler,
            },
            {
                .binding = 8,
                .textureView = context->cubemap_texture_view,
            },
            {
                .binding = 9,
                .sampler = context->cubemap_sampler
            }
        };
        WGPUBindGroupDescriptor uBgDesc = {0};
        uBgDesc.layout = context->global_layout;
        uBgDesc.entryCount = entry_count;
        uBgDesc.entries = entries;
        context->global_bindgroup = wgpuDeviceCreateBindGroup(context->device, &uBgDesc);
    }

    // Create the depth texture attachment
    {
        WGPUTextureDescriptor depthTextureDesc = {
            .usage = WGPUTextureUsage_RenderAttachment,
            .label = "DEPTH TEXTURE",
            .dimension = WGPUTextureDimension_2D,
            .size = { .width = context->viewport_width, .height = context->viewport_height, .depthOrArrayLayers = 1 },
            .format = depth_stencil_format,
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
        msaaDesc.label = "msaa texture";
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

    // Create the scene buffers // todo: redo this for every new scene we enter (could in open-world setting also do one per chunk)
    {
        // max single buffer size is 268mb, let's keep it to 50mb, which is about a million max vertices // todo: create with actual needed size, which is prob. much smaller
        #define VERTEX_LIMIT 1000000
        #define INDEX_LIMIT (VERTEX_LIMIT * 2)
        #define INSTANCE_LIMIT (VERTEX_LIMIT / 2)
        // Create vertex buffer 
        WGPUBufferDescriptor vertexBufDesc = {.size = VERTEX_LAYOUT[0].arrayStride * VERTEX_LIMIT, .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex};
        context->vertices = wgpuDeviceCreateBuffer(context->device, &vertexBufDesc);
        assert(context->vertices);
        // Create index buffer
        WGPUBufferDescriptor indexBufDesc = {.size = sizeof(uint32_t) * INDEX_LIMIT, .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index};
        context->indices = wgpuDeviceCreateBuffer(context->device, &indexBufDesc);
        assert(context->indices);
        // Create instance buffer
        WGPUBufferDescriptor instBufDesc = {.size = VERTEX_LAYOUT[1].arrayStride * INSTANCE_LIMIT, .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex};
        context->instances = wgpuDeviceCreateBuffer(context->device, &instBufDesc);
        assert(context->instances);
    }

    // Create shadow pipeline
    {
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

static void my_error_cb(WGPUErrorType type, const char* message, void* user_data) {
    fprintf(stderr, "WebGPU Error [%d]: %s\n", type, message);
}

static void handle_request_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message, void* userdata) {
    WebGPUContext *context = (WebGPUContext *)userdata;
    if (status == WGPURequestAdapterStatus_Success) {
        context->adapter = adapter;
        assert(context->adapter);
        WGPUDeviceDescriptor desc = {0}; desc.deviceLostCallback = my_error_cb;
        desc.requiredFeatures = (WGPUFeatureName[]){ WGPUNativeFeature_MultiDrawIndirect };
        desc.requiredFeatureCount = 1;
        wgpuAdapterRequestDevice(context->adapter, &desc, handle_request_device, context);
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
                if (!selectedAdapter) {
                    printf("Selected GPU: %s, type: %s, backend: %s\n", info.device, type, backend);
                    selectedAdapter = adapters[i];
                } else printf("Available GPU: %s, type: %s, backend: %s\n", info.device, type, backend);
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
        WGPUDeviceDescriptor desc = {0}; desc.deviceLostCallback = my_error_cb;
        desc.requiredFeatures = (WGPUFeatureName[]){ WGPUNativeFeature_MultiDrawIndirect };
        desc.requiredFeatureCount = 1;
        wgpuAdapterRequestDevice(context->adapter, &desc, handle_request_device, context);
        return;
    }
    #endif
    WGPURequestAdapterOptions adapter_opts = {0};
    adapter_opts.compatibleSurface = context->surface;
    adapter_opts.powerPreference = WGPUPowerPreference_HighPerformance;
    wgpuInstanceRequestAdapter(context->instance, &adapter_opts, handle_request_adapter, context);
}

#ifdef __EMSCRIPTEN__
void *createGPUContext(void (*callback)(), int width, int height, int viewport_width, int viewport_height)
    setup_callback = callback;
#else
void *createGPUContext(void *hInstance, void *hwnd, int width, int height, int viewport_width, int viewport_height)
#endif
{
    static WebGPUContext context = {0};

    #ifdef __EMSCRIPTEN__
    context.instance = wgpuCreateInstance(NULL);
    #else
    WGPUInstanceDescriptor instDesc = {0};
    WGPUInstanceExtras extras = {0};
    extras.chain.sType = WGPUSType_InstanceExtras;
    extras.backends   = WGPUInstanceBackend_Primary;
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

int create_main_pipeline(void *context_ptr, const char *shader) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    if (!context->initialized) {
        fprintf(stderr, "[webgpu.c] wgpuCreatePipeline called before init!\n");
        return -1;
    }
    WGPUShaderModule shaderModule = loadWGSL(context->device, shader);
    if (!shaderModule) {
        fprintf(stderr, "[webgpu.c] Failed to load shader: %s\n", shader);
        return -1;
    }

    #define LAYOUT_COUNT 1
    WGPUBindGroupLayout bgls[LAYOUT_COUNT] = {
        context->global_layout,
    };
    WGPUPipelineLayoutDescriptor layoutDesc = {0};
    layoutDesc.bindGroupLayoutCount = LAYOUT_COUNT;
    layoutDesc.bindGroupLayouts = bgls;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(context->device, &layoutDesc);
    assert(pipelineLayout);
    
    WGPURenderPipelineDescriptor rpDesc = {0};
    rpDesc.label = "main render pipeline";
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
    context->main_pipeline = gpu_pipeline;
   
    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);
    
    printf("[webgpu.c] Created main pipeline");
    return 0;
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
    ppTexDesc.label = "postprocessing texture";
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
    enum { entry_count = 4 };
    WGPUBindGroupLayoutEntry layout_entries[entry_count] = {
        // todo: these entries are duplicated from the global layout
        // Global uniforms
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
            .buffer.type = WGPUBufferBindingType_Uniform,
            .buffer.minBindingSize = GLOBAL_UNIFORM_CAPACITY,
        },
        // Animations Sampler
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Vertex,
            .sampler = { .type = WGPUSamplerBindingType_NonFiltering }
        },
        // Animations Texture
        {
            .binding = 2, 
            .visibility = WGPUShaderStage_Vertex, 
            .texture = {.sampleType = WGPUTextureSampleType_UnfilterableFloat, .viewDimension = WGPUTextureViewDimension_2D, .multisampled = false}
        },
        // Per-material uniforms (array index to differentiate materials)
        {
            .binding = 3,
            .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
            .buffer.type = WGPUBufferBindingType_Uniform,
            .buffer.minBindingSize = UNIFORM_BUFFER_MAX_SIZE
        },
    };
    WGPUBindGroupLayoutDescriptor bglDesc = {0};
    bglDesc.entryCount = entry_count;
    bglDesc.entries = layout_entries;
    WGPUBindGroupLayout bindgroup_layout = wgpuDeviceCreateBindGroupLayout(context->device, &bglDesc);
    
    WGPUBindGroupEntry entries[entry_count] = {
        {
            .binding = 0,
            .buffer = context->global_uniform_buffer,
            .offset = 0,
            .size = GLOBAL_UNIFORM_CAPACITY,
        },
        {
            .binding = 1,
            .sampler = context->animations_sampler,
        },
        {
            .binding = 2,
            .textureView = context->animations_view,
        },
        {
            .binding = 3,
            .buffer = context->material_uniform_buffer,
            .offset = 0,
            .size = UNIFORM_BUFFER_MAX_SIZE,
        },
    };
    WGPUBindGroupDescriptor uBgDesc = {0};
    uBgDesc.layout = bindgroup_layout;
    uBgDesc.entryCount = entry_count;
    uBgDesc.entries = entries;
    context->shadow_bindgroup = wgpuDeviceCreateBindGroup(context->device, &uBgDesc);

    // 2. Create a pipeline layout for the shadow pipeline
    WGPUPipelineLayoutDescriptor shadowPLDesc = {0};
    shadowPLDesc.bindGroupLayoutCount = 1;
    shadowPLDesc.bindGroupLayouts = &bindgroup_layout;
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
    int material_id = -1;
    for (int i = 0; i < MAX_MATERIALS; i++) {
        if (!context->materials[i].used) {
            material_id = i;
            context->materials[i] = (Material) {0}; // init material
            // set all mesh indices to -1, which means not used
            for (int j = 0; j < MAX_MESHES; j++) context->materials[i].mesh_ids[j] = -1;
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

    // Write into vertex buffer (same as in wgpuCreateMesh)
    wgpuQueueWriteBuffer(context->queue, context->vertices, context->vertex_count * sizeof(struct Vertex), v, vc * sizeof(struct Vertex));
    mesh->first_vertex = context->vertex_count;
    context->vertex_count += vc;
    mesh->vertex_count = vc;
    
    // Write into index buffer
    wgpuQueueWriteBuffer(context->queue, context->indices, context->index_count * sizeof(uint32_t), i, ic * sizeof(uint32_t));
    mesh->first_index = context->index_count;
    context->index_count += ic;
    mesh->index_count = ic;
    
    // Write into instance buffer
    wgpuQueueWriteBuffer(context->queue, context->instances, context->instance_count * sizeof(struct Instance), ii, iic * sizeof(struct Instance));
    mesh->first_instance = context->instance_count;
    context->instance_count += iic;
    mesh->instances = ii;
    mesh->instance_count = iic;

    // Create the texture setup
    // todo: in material instead (but no function for that yet)
    {
        // // Create a sampler
        // WGPUSamplerDescriptor samplerDesc = {0};
        // samplerDesc.minFilter = WGPUFilterMode_Linear;
        // samplerDesc.magFilter = WGPUFilterMode_Nearest;
        // samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
        // samplerDesc.lodMinClamp = 0;
        // samplerDesc.lodMaxClamp = 0;
        // samplerDesc.maxAnisotropy = 1;
        // samplerDesc.addressModeU = WGPUAddressMode_Repeat;
        // samplerDesc.addressModeV = WGPUAddressMode_Repeat;
        // samplerDesc.addressModeW = WGPUAddressMode_Repeat;
        // material->texture_sampler = wgpuDeviceCreateSampler(context->device, &samplerDesc);
        // assert(material->texture_sampler != NULL);

        // // Initialize all available textures to the default 1x1 white pixel
        // material->texture_count = 0; // 0 textures have been actually set yet with a non-default value
        // for (int i=0; i<MAX_TEXTURES; i++) {
        //     material->textures[i] = context->defaultTexture;
        //     material->texture_views[i]   = context->defaultTextureView;
        // }
        // int totalEntries = MAX_TEXTURES + 1; // entry 0 is the sampler
        // WGPUBindGroupEntry *entries = calloc(totalEntries, sizeof(WGPUBindGroupEntry));
        // // Sampler at binding=0
        // entries[0].binding = 0;
        // entries[0].sampler = material->texture_sampler;
        // for (int i=0; i<MAX_TEXTURES; i++) {
        //     entries[i+1].binding = i + 1;
        //     entries[i+1].textureView = material->texture_views[i];
        // }
        // if (pipeline->material_bindgroup) {
        //     wgpuBindGroupRelease(pipeline->material_bindgroup);
        // }
        // WGPUBindGroupDescriptor bgDesc = {0};
        // bgDesc.layout     = context->per_material_layout;
        // bgDesc.entryCount = totalEntries;
        // bgDesc.entries    = entries;
        // pipeline->material_bindgroup = wgpuDeviceCreateBindGroup(context->device, &bgDesc);

        // free(entries);
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
    mesh->flags = mesh->flags | MESH_ANIMATED; // todo: this should be an instance thing (?)
    writeDataToTexture(context, &context->animations, bf, ANIMATION_SIZE / 4, 1, context->animation_count * ANIMATION_SIZE, 0);
    context->animation_count += 1;
}

int createGPUTexture(void *context_ptr, int mesh_id, void *data, int w, int h) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    Mesh* mesh = &context->meshes[mesh_id];
    Material* material = &context->materials[mesh->material_id];
    if (context->texture_count >= TEXTURE_LIMIT) {
        fprintf(stderr, "No more texture slots in mesh!"); // todo: allow re-assigning a new texture to a slot that was occupied
        return -1;
    }
    int slot = context->texture_count; // e.g. 0 => binding=1, etc.
    context->texture_count += 1;
    
    // Upload the pixel data.
    writeDataToTexture(context, &context->texture_array, data, w, h, 0, slot);

    printf("Added texture to material %d at slot %d (binding=%d)\n", mesh->material_id, slot, slot+1);

    return slot;
}

int set_env_cube(void *context_ptr, void *data[6], int face_size) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
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
}

void setGPUInstanceBuffer(void *context_ptr, int mesh_id, void* ii, int iic) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    // freeing the previous buffer is the responsibility of the caller
    Mesh *mesh = &context->meshes[mesh_id];
    mesh->instances = ii;
    mesh->instance_count = iic;
}

static void fenceCallback(WGPUQueueWorkDoneStatus status, WGPU_NULLABLE void *userdata) {
    bool *done = (bool*)userdata;
    *done = true;
}
double block_on_gpu_queue(void *context_ptr, struct Platform *p) {
    #ifdef __EMSCRIPTEN__ 
    return 0.0; 
    #else
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    volatile bool workDone = false;
    double time_before_ns = p->current_time_ms();

    // Request notification when the GPU work is done.
    wgpuQueueOnSubmittedWorkDone(context->queue, fenceCallback, (void*)&workDone);

    // Busy-wait until the flag is set.
    while (!workDone) {
        wgpuDevicePoll(context->device, true, NULL); // blocks internally with 'true' set, to avoid wasting cpu resources
    }
    return p->current_time_ms() - time_before_ns;
    #endif
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

struct DrawIndexedIndirect { // 20 bytes -> 32 bytes for alignment
  uint32_t index_count; // 4 bytes
  uint32_t instanceCount; // 4 bytes
  uint32_t firstIndex; // 4 bytes
  uint32_t  baseVertex; // 4 bytes
  uint32_t firstInstance; // 4 bytes
};

#define MAX_DRAW_CALLS 1024
void createDrawIndirectBuffers(void *context_ptr) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;

    struct DrawIndexedIndirect drawCommands[MAX_DRAW_CALLS];
    uint32_t drawCount = 0;
    
    for (int k = 0; k < MAX_MESHES; k++) {
        assert(drawCount <= MAX_DRAW_CALLS);
        Mesh *mesh = &context->meshes[k];
        if (mesh->used) {
            struct DrawIndexedIndirect cmd = {0};
            cmd.index_count    = mesh->index_count;
            cmd.instanceCount = mesh->instance_count;
            cmd.firstIndex    = mesh->first_index; 
            cmd.baseVertex    = mesh->first_vertex;
            cmd.firstInstance = mesh->first_instance;
            drawCommands[drawCount++] = cmd;
        }
    }
    
    // create the two buffers
    WGPUBufferDescriptor indirect_draw_buffer_desc = {0};
    indirect_draw_buffer_desc.size = drawCount * sizeof(struct DrawIndexedIndirect);
    indirect_draw_buffer_desc.usage = WGPUBufferUsage_Indirect | WGPUBufferUsage_CopyDst;
    context->indirect_draw_buffer = wgpuDeviceCreateBuffer(context->device, &indirect_draw_buffer_desc);
    context->indirect_count = drawCount;

    // WGPUBufferDescriptor indirect_count_buffer_desc = {0};
    // indirect_count_buffer_desc.size = sizeof(uint32_t);
    // indirect_count_buffer_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    // context->indirect_count_buffer = wgpuDeviceCreateBuffer(context->device, &indirect_count_buffer_desc);

    wgpuQueueWriteBuffer(context->queue, context->indirect_draw_buffer, 0, drawCommands, drawCount * sizeof(struct DrawIndexedIndirect));
    // wgpuQueueWriteBuffer(context->queue, context->indirect_count_buffer, 0, &drawCount, sizeof(uint32_t));
}

struct draw_result drawGPUFrame(void *context_ptr, struct Platform *p, int offset_x, int offset_y, int viewport_width, int viewport_height, int save_to_disk, char *filename,
struct GlobalUniforms *global_uniforms, struct MaterialUniforms material_uniforms[MAX_MATERIALS]) {
    WebGPUContext *context = (WebGPUContext *)context_ptr;
    struct draw_result result = {0};
    double mut_ms = p->current_time_ms();

    // acquire the surface texture and view
    wgpuSurfaceGetCurrentTexture(context->surface, &context->currentSurfaceTexture);
    // if not available, return early and notify that the surface is not yet available to be rendered to
    if (context->currentSurfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success) {
        result.cpu_ms = p->current_time_ms() - mut_ms;
        result.surface_not_available = 1;
        return result;
    }
    WGPUTextureViewDescriptor d = {.format = screen_color_format, .dimension = WGPUTextureViewDimension_2D, .baseMipLevel = 0, .mipLevelCount = 1, .baseArrayLayer = 0, .arrayLayerCount = 1, .nextInChain = NULL};
    context->swapchain_view = wgpuTextureCreateView(context->currentSurfaceTexture.texture, &d);

    result.get_surface_ms = p->current_time_ms() - mut_ms; mut_ms = p->current_time_ms();

    double start_ms = p->current_time_ms();

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
    
    // update all the gpu data
    // Write CPU–side uniform data to GPU
    // todo: condition to only do when updated data
    wgpuQueueWriteBuffer(context->queue, context->global_uniform_buffer, 0, global_uniforms, sizeof(struct GlobalUniforms));
    for (int material_id = 0; material_id < MAX_MATERIALS; material_id++) {
        Material *material = &context->materials[material_id];
        // If the material requires uniform data updates, update the material uniform buffer
        // todo: only write when material setting UPDATE_MATERIAL_UNIFORMS is true
        // todo: we could even set this to true when we do an actual update, and otherwise never do this
        // todo: we can also do that for the global uniforms
        if (1) {
            uint64_t offset = material_id * sizeof(struct MaterialUniforms);
            // todo: we could batch this write buffer call into one single call for the pipeline instead
            wgpuQueueWriteBuffer(context->queue, context->material_uniform_buffer, offset, &material_uniforms[material_id], sizeof(struct MaterialUniforms));
        }
    }
    for (int mesh_id = 0; mesh_id < MAX_MESHES; mesh_id++) {
        Mesh *mesh = &context->meshes[mesh_id];
        // todo: make this based on mesh setting UPDATE_MESH_INSTANCES, then we don't need to do this for static meshes
        // If the mesh requires instance data updates, update the instance buffer (this is really expensive!)
        if (1 && mesh->used) {
            unsigned long long instanceDataSize = VERTEX_LAYOUT[1].arrayStride * mesh->instance_count;
            // write RAM instances to GPU instances
            wgpuQueueWriteBuffer(context->queue,context->instances,mesh->first_instance*sizeof(struct Instance),mesh->instances, instanceDataSize);
        }
    }
    result.write_buffer_ms = p->current_time_ms() - mut_ms; mut_ms = p->current_time_ms();

    static int pass_desc_created = 0;
    static WGPURenderPassDescriptor passDesc = {0};
    static WGPURenderPassColorAttachment colorAtt = {0};
    if (!pass_desc_created) {
        pass_desc_created = 1;
        colorAtt.loadOp = WGPULoadOp_Clear;
        colorAtt.storeOp = WGPUStoreOp_Store;
        colorAtt.clearValue = (WGPUColor){0., 0., 0., 1.0};
        colorAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAtt;
        passDesc.depthStencilAttachment = &context->depthAttachment;
    }
    // every frame reset the colorattachment view to the new surface texture
    // every frame create a new renderpass encoder because the colorattachment changed
    if (POST_PROCESSING_ENABLED && MSAA_ENABLED) { colorAtt.view = context->msaa_texture_view; colorAtt.resolveTarget = context->post_processing_texture_view;}
    else if (POST_PROCESSING_ENABLED) { colorAtt.view = context->post_processing_texture_view;}
    else if (MSAA_ENABLED) { colorAtt.view = context->msaa_texture_view; colorAtt.resolveTarget = context->swapchain_view;}
    else { colorAtt.view = context->swapchain_view;}
    result.setup_ms = p->current_time_ms() - mut_ms; mut_ms = p->current_time_ms();

    WGPUCommandEncoderDescriptor encDesc = {0};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(context->device, &encDesc);

    // SHADOW PASS
    // Reuse the global pipeline uniform data in the shader uniforms // todo: is it possible to reuse the same gpu-buffer and write only once?
    // wgpuQueueWriteBuffer(context->queue, context->shadow_uniform_buffer, 0, context->pipelines[0].global_uniform_data, GLOBAL_UNIFORM_CAPACITY);
    if (SHADOWS_ENABLED) {
        static WGPURenderPassDescriptor shadowPassDesc = {0};
        static WGPURenderPassDepthStencilAttachment shadowDepthAttachment = {0};
        static WGPURenderBundle shadow_bundle = NULL;
        if (!shadow_bundle) {
            shadowDepthAttachment.view = context->shadow_texture_view;
            shadowDepthAttachment.depthLoadOp = WGPULoadOp_Clear;
            shadowDepthAttachment.depthStoreOp = WGPUStoreOp_Store;
            shadowDepthAttachment.depthClearValue = 1.0f;
            // This render pass uses no color attachment (depth-only).
            shadowPassDesc.colorAttachmentCount = 0; // no color attachments
            shadowPassDesc.depthStencilAttachment = &shadowDepthAttachment;   

            WGPURenderBundleEncoderDescriptor bundle_desc = {
                .label = "shadow-bundle",
                .colorFormatCount = 0,
                .colorFormats = (WGPUTextureFormat[]){},
                .depthStencilFormat = depth_stencil_format,
                .sampleCount = 1,
                .depthReadOnly = 0,
                .stencilReadOnly = 1,
            }; 
            WGPURenderBundleEncoder shadow_bundle_encoder = wgpuDeviceCreateRenderBundleEncoder(context->device, &bundle_desc);

            // 4. Bind the shadow pipeline.
            wgpuRenderBundleEncoderSetPipeline(shadow_bundle_encoder, context->shadow_pipeline);
            wgpuRenderBundleEncoderSetBindGroup(shadow_bundle_encoder, 0, context->shadow_bindgroup, 0, NULL);

            // Set the scene's vertex/index/instance buffers
            wgpuRenderBundleEncoderSetVertexBuffer(shadow_bundle_encoder, 0, context->vertices, 0, VERTEX_LIMIT * sizeof(struct Vertex));
            wgpuRenderBundleEncoderSetVertexBuffer(shadow_bundle_encoder, 1, context->instances, 0, INSTANCE_LIMIT * sizeof(struct Instance));
            wgpuRenderBundleEncoderSetIndexBuffer(shadow_bundle_encoder, context->indices, WGPUIndexFormat_Uint32, 0, INDEX_LIMIT * sizeof(uint32_t));
            // 5. For each mesh that casts shadows, draw
            for (int mesh_id = 0; mesh_id < MAX_MESHES; mesh_id++) {
                Mesh *mesh = &context->meshes[mesh_id];
                if (mesh->flags & MESH_CAST_SHADOWS && mesh->used) {
                    wgpuRenderBundleEncoderDrawIndexed(shadow_bundle_encoder, mesh->index_count, mesh->instance_count, mesh->first_index, mesh->first_vertex, mesh->first_instance);
                }
            }
            WGPURenderBundleDescriptor desc = {0}; desc.label = "shadow bundle";
            shadow_bundle = wgpuRenderBundleEncoderFinish(shadow_bundle_encoder, &desc);
        }

        WGPURenderPassEncoder shadowPass = wgpuCommandEncoderBeginRenderPass(encoder, &shadowPassDesc);

        wgpuRenderPassEncoderExecuteBundles(shadowPass, 1, &shadow_bundle);

        // 6. End the shadow render pass
        wgpuRenderPassEncoderEnd(shadowPass);
        wgpuRenderPassEncoderRelease(shadowPass);
    }
    result.shadowmap_ms = p->current_time_ms() - mut_ms; mut_ms = p->current_time_ms();

    // Bundle
    #define USE_BUNDLE 0
    static WGPURenderBundle main_bundle = NULL;
    if (USE_BUNDLE && !main_bundle) {
        WGPURenderBundleEncoderDescriptor bundle_desc = {
            .label = "main-scene-bundle",
            .colorFormatCount = 1,
            .colorFormats = (WGPUTextureFormat[]){ screen_color_format },
            .depthStencilFormat = depth_stencil_format,
            .sampleCount = MSAA_ENABLED ? 4 : 1,
            .depthReadOnly = 0,
            .stencilReadOnly = 1,
        }; 
        WGPURenderBundleEncoder main_bundle_encoder = wgpuDeviceCreateRenderBundleEncoder(context->device, &bundle_desc);

        wgpuRenderBundleEncoderSetVertexBuffer(main_bundle_encoder, 0, context->vertices, 0, VERTEX_LIMIT * sizeof(struct Vertex));
        wgpuRenderBundleEncoderSetVertexBuffer(main_bundle_encoder, 1, context->instances, 0, INSTANCE_LIMIT * sizeof(struct Instance));
        wgpuRenderBundleEncoderSetIndexBuffer(main_bundle_encoder, context->indices, WGPUIndexFormat_Uint32, 0, INDEX_LIMIT * sizeof(uint32_t));
        // todo: is it possible to set the pipeline once at the beginning, and then avoid this call every frame?
        wgpuRenderBundleEncoderSetPipeline(main_bundle_encoder, context->main_pipeline);
        wgpuRenderBundleEncoderSetBindGroup(main_bundle_encoder, 0, context->global_bindgroup, 0, NULL);
        for (int mesh_id = 0; mesh_id < MAX_MESHES; mesh_id++) {
            Mesh *mesh = &context->meshes[mesh_id];
            if (mesh->used)
                wgpuRenderBundleEncoderDrawIndexed(main_bundle_encoder, mesh->index_count,mesh->instance_count,mesh->first_index, mesh->first_vertex, mesh->first_instance);
        }
        WGPURenderBundleDescriptor desc = {0}; desc.label = "main bundle";
        main_bundle = wgpuRenderBundleEncoderFinish(main_bundle_encoder, &desc);
    }

    WGPURenderPassEncoder main_pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    
    // set scissor to render only to the extent of the viewport
    wgpuRenderPassEncoderSetViewport(main_pass, offset_x, offset_y, viewport_width, viewport_height, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(main_pass, (uint32_t)offset_x, (uint32_t)offset_y, (uint32_t)viewport_width, (uint32_t)viewport_height);

    static int buffers = 0;
    if (!buffers) {
        createDrawIndirectBuffers(context);
        buffers = 1;
    }
    if (USE_BUNDLE) {
        wgpuRenderPassEncoderExecuteBundles(main_pass, 1, &main_bundle);
    } else {
        wgpuRenderPassEncoderSetVertexBuffer(main_pass, 0, context->vertices, 0, VERTEX_LIMIT * sizeof(struct Vertex));
        wgpuRenderPassEncoderSetVertexBuffer(main_pass, 1, context->instances, 0, INSTANCE_LIMIT * sizeof(struct Instance));
        wgpuRenderPassEncoderSetIndexBuffer(main_pass, context->indices, WGPUIndexFormat_Uint32, 0, INDEX_LIMIT * sizeof(uint32_t));
        wgpuRenderPassEncoderSetPipeline(main_pass, context->main_pipeline);
        wgpuRenderPassEncoderSetBindGroup(main_pass, 0, context->global_bindgroup, 0, NULL);
        // todo: we can avoid the above 5 calls by putting draw indirect calls in a renderbundle, but then we cannot do multi anymore, so many calls
        wgpuRenderPassEncoderMultiDrawIndexedIndirect(main_pass, context->indirect_draw_buffer, 0, context->indirect_count);
    }

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
    double start_submit_ms = p->current_time_ms();
    WGPUCommandBufferDescriptor cmdDesc = {0};
    WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(context->queue, 1, &cmdBuf);
    wgpuCommandEncoderRelease(encoder);
    wgpuCommandBufferRelease(cmdBuf);
    result.submit_ms = p->current_time_ms() - start_submit_ms; mut_ms = p->current_time_ms();

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
