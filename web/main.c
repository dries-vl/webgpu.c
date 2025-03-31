#include <webgpu/webgpu.h>
#include <emscripten/html5.h>
#include <stdio.h>

#include "../webgpu.c"
#include "../present.c"

static WGPUInstance instance;
static WGPUSurface surface;
static WGPUDevice device;
static WGPUSwapChain swapchain;
static WGPURenderPipeline pipeline;

// Forward declarations
static void create_pipeline(void);
static void create_swapchain(void);

static EM_BOOL frame(double time, void *userData) {

    WGPUTexture swapTexture = wgpuSwapChainGetCurrentTexture(swapchain);

    WGPUTextureViewDescriptor viewDesc = {
        .label           = "Swapchain2DView",
        .format          = WGPUTextureFormat_BGRA8Unorm,
        .dimension       = WGPUTextureViewDimension_2D,
        .baseMipLevel    = 0,
        .mipLevelCount   = 1,
        .baseArrayLayer  = 0,
        .arrayLayerCount = 1,
        .aspect          = WGPUTextureAspect_All
    };

    // Create the 2D view explicitly
    WGPUTextureView swapView = wgpuTextureCreateView(swapTexture, &viewDesc);

    WGPURenderPassColorAttachment colorAtt = {
        .view          = swapView,
        .resolveTarget = NULL,
        .loadOp        = WGPULoadOp_Clear,
        .storeOp       = WGPUStoreOp_Store,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,  // Ensure depthSlice is undefined for 2D textures
        .clearValue    = (WGPUColor){0.1, 0.2, 0.3, 1.0}
    };

    WGPURenderPassDescriptor passDesc = {
        .colorAttachmentCount = 1,
        .colorAttachments     = &colorAtt,
        .depthStencilAttachment = NULL
    };    

    // Begin encoding commands.
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, NULL);
    WGPURenderPassEncoder passEnc = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(passEnc, pipeline);
    wgpuRenderPassEncoderDraw(passEnc, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(passEnc);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuQueueSubmit(wgpuDeviceGetQueue(device), 1, &cmd);
    wgpuTextureViewRelease(swapView);
    return EM_TRUE;
}

static void create_pipeline(void) {
    // Inline WGSL shaders. Note the struct definition does not end with a semicolon.
    const char* vsCode =
        "struct VertexOutput {\n"
        "  @builtin(position) pos : vec4<f32>\n"
        "}\n"
        "@vertex\n"
        "fn main(@builtin(vertex_index) vertexIndex : u32) -> VertexOutput {\n"
        "  var pos = array<vec2<f32>, 3>(\n"
        "      vec2<f32>( 0.0,  0.5),\n"
        "      vec2<f32>(-0.5, -0.5),\n"
        "      vec2<f32>( 0.5, -0.5));\n"
        "  var output: VertexOutput;\n"
        "  output.pos = vec4<f32>(pos[vertexIndex], 0.0, 1.0);\n"
        "  return output;\n"
        "}\n";

    const char* fsCode =
        "@fragment\n"
        "fn main() -> @location(0) vec4<f32> {\n"
        "  return vec4<f32>(1.0, 0.0, 0.0, 1.0);\n"
        "}\n";

    // Create the vertex shader module.
    WGPUShaderModuleWGSLDescriptor vsDesc = {0};
    vsDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    vsDesc.code = vsCode;
    WGPUShaderModuleDescriptor vsModDesc = {0};
    vsModDesc.label = "Vertex Shader";
    vsModDesc.nextInChain = (const WGPUChainedStruct *)&vsDesc;
    WGPUShaderModule vsModule = wgpuDeviceCreateShaderModule(device, &vsModDesc);

    // Create the fragment shader module.
    WGPUShaderModuleWGSLDescriptor fsDesc = {0};
    fsDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    fsDesc.code = fsCode;
    WGPUShaderModuleDescriptor fsModDesc = {0};
    fsModDesc.label = "Fragment Shader";
    fsModDesc.nextInChain = (const WGPUChainedStruct *)&fsDesc;
    WGPUShaderModule fsModule = wgpuDeviceCreateShaderModule(device, &fsModDesc);

    // Create a pipeline layout (no bind groups).
    WGPUPipelineLayoutDescriptor layoutDesc = {0};
    layoutDesc.bindGroupLayoutCount = 0;
    layoutDesc.bindGroupLayouts = NULL;
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);

    // Set up the render pipeline descriptor.
    WGPURenderPipelineDescriptor pipeDesc = {0};
    pipeDesc.layout = layout;

    // Vertex stage.
    WGPUVertexState vtxState = {0};
    vtxState.module = vsModule;
    vtxState.entryPoint = "main";
    vtxState.bufferCount = 0;
    vtxState.buffers = NULL;
    pipeDesc.vertex = vtxState;

    // Fragment stage.
    WGPUFragmentState fragState = {0};
    fragState.module = fsModule;
    fragState.entryPoint = "main";
    WGPUColorTargetState colorTarget = {0};
    colorTarget.format = WGPUTextureFormat_BGRA8Unorm;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;
    pipeDesc.fragment = &fragState;

    // Primitive and multisample state.
    pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipeDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pipeDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipeDesc.primitive.cullMode = WGPUCullMode_None;
    pipeDesc.multisample.count = 1;
    pipeDesc.multisample.mask = ~0u;
    pipeDesc.multisample.alphaToCoverageEnabled = false;

    pipeline = wgpuDeviceCreateRenderPipeline(device, &pipeDesc);
}

static void create_swapchain(void) {
    WGPUSwapChainDescriptor swapDesc = {0};
    swapDesc.usage = WGPUTextureUsage_RenderAttachment;
    swapDesc.format = WGPUTextureFormat_BGRA8Unorm;
    swapDesc.width = 800;
    swapDesc.height = 600;
    swapDesc.presentMode = WGPUPresentMode_Fifo;
    swapchain = wgpuDeviceCreateSwapChain(device, surface, &swapDesc);
}

static void on_device(WGPURequestDeviceStatus status, WGPUDevice dev, const char *message, void *userData) {
    if (status != WGPURequestDeviceStatus_Success) {
        printf("Device request failed: %s\n", message);
        return;
    }
    device = dev;
    create_pipeline();
    create_swapchain();
    emscripten_request_animation_frame_loop(frame, NULL);
}

static void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, const char *message, void *userData) {
    if (status != WGPURequestAdapterStatus_Success) {
        printf("Adapter request failed: %s\n", message);
        return;
    }
    wgpuAdapterRequestDevice(adapter, NULL, on_device, NULL);
}

static void init_webgpu(void) {
    instance = wgpuCreateInstance(NULL);
    WGPUSurfaceDescriptorFromCanvasHTMLSelector canvasDesc = {0};
    canvasDesc.chain.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;
    canvasDesc.selector = "#canvas";
    WGPUSurfaceDescriptor surfDesc = {0};
    surfDesc.nextInChain = (const WGPUChainedStruct *)&canvasDesc;
    surface = wgpuInstanceCreateSurface(instance, &surfDesc);
    WGPURequestAdapterOptions adapterOpts = {0};
    adapterOpts.compatibleSurface = surface;
    wgpuInstanceRequestAdapter(instance, &adapterOpts, on_adapter, NULL);
}

int main(void) {
    init_webgpu();
    return 0;
}