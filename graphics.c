#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "wgpu.h"
#include <stdint.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;typedef int8_t i8;typedef int16_t i16;typedef int32_t i32;typedef int64_t i64;typedef float f32;typedef double f64;

enum {WIDTH = 800, HEIGHT = 600, DISCRETE_GPU = 0, HDR = 1, MAX_VERTICES=3*256, MAX_INSTANCES=3*256};
const char *BACKENDS[] = {"NA","NULL","WebGPU","D3D11","D3D12","Metal","Vulkan","OpenGL","GLES"};

WGPUBuffer mkbuf(WGPUDevice d,i64 s,WGPUBufferUsageFlags u){WGPUBufferDescriptor desc={.size=s,.usage=u};return wgpuDeviceCreateBuffer(d,&desc);}
void on_adapter(WGPURequestAdapterStatus s, WGPUAdapter a, const char* msg, void* p) {*(WGPUAdapter*)p = a;}
void on_device(WGPURequestDeviceStatus s, WGPUDevice d, const char* msg, void* p) {*(WGPUDevice*)p = d;}
void on_work_done(WGPUQueueWorkDoneStatus status, void* userdata) {bool* done = userdata; *done = 1;}
void my_error_cb(WGPUErrorType type, const char* message, void* user_data) {fprintf(stderr, "WebGPU Error [%d]: %s\n", type, message);}

static LRESULT CALLBACK wndproc(HWND h,UINT m,WPARAM w,LPARAM l) // *WINDOWS CALLBACK*
  { if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcA(h,m,w,l); }

i32 WINAPI WinMain(HINSTANCE hi,HINSTANCE prev,LPSTR lp,i32 n){
  WNDCLASSA wc={.lpfnWndProc=wndproc,.hInstance=hi,.lpszClassName="wgpu"};RegisterClassA(&wc); // *WINDOWS WINDOW*
  HWND hwnd=CreateWindowA("wgpu","webgpu.c",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,WIDTH,HEIGHT,0,0,hi,0);RECT rc;GetClientRect(hwnd,&rc);
  WGPUInstance ins=wgpuCreateInstance(&(WGPUInstanceDescriptor){.nextInChain=(const WGPUChainedStruct*)&(WGPUInstanceExtras){ // *INSTANCE*
    .chain.sType = WGPUSType_InstanceExtras, .backends = WGPUInstanceBackend_GL, .flags = WGPUInstanceFlag_DiscardHalLabels, // *BACKEND*
    .dx12ShaderCompiler = WGPUDx12Compiler_Undefined, .gles3MinorVersion = WGPUGles3MinorVersion_Automatic, .dxilPath = NULL, .dxcPath = NULL } });
  WGPUSurfaceDescriptorFromWindowsHWND sd_hwnd={.chain.sType=WGPUSType_SurfaceDescriptorFromWindowsHWND,.hinstance=hi,.hwnd=hwnd}; // *SURFACE*
  WGPUSurface surface=wgpuInstanceCreateSurface(ins,&(WGPUSurfaceDescriptor){.nextInChain=&sd_hwnd.chain});
  WGPUAdapter adapter=NULL;WGPUDevice dev=NULL; // *ADAPTER AND DEVICE*
  WGPUAdapter adapters[16]; WGPUInstanceEnumerateAdapterOptions opts = { .backends = WGPUInstanceBackend_All };
  i32 count = wgpuInstanceEnumerateAdapters(ins, &opts, adapters);
  for (i32 i = 0; i < count; i++) { WGPUAdapterInfo info = {0}; wgpuAdapterGetInfo(adapters[i], &info); adapter = adapters[i]; 
      if (info.adapterType == (DISCRETE_GPU ? WGPUAdapterType_DiscreteGPU : WGPUAdapterType_IntegratedGPU)) 
        { printf("Selected GPU: %s, backend: %s\n", info.device, BACKENDS[info.backendType]); break; } }
  WGPUDeviceDescriptor desc = { .deviceLostCallback = my_error_cb, .requiredFeatures = (WGPUFeatureName[]){0}, .requiredFeatureCount = 0 };
  wgpuAdapterRequestDevice(adapter, &desc, on_device, &dev); while(!dev) { wgpuInstanceProcessEvents(ins); }
  WGPUTextureFormat fmt= HDR ? WGPUTextureFormat_RGBA16Float : WGPUTextureFormat_RGBA8UnormSrgb; // *SURFACE FORMAT* only GL and DX12 support HDR on windows
  WGPUSurfaceConfiguration cfg={.device=dev,.format=fmt,.usage=WGPUTextureUsage_RenderAttachment,.width=rc.right,.height=rc.bottom,.presentMode=WGPUPresentMode_Fifo};
  wgpuSurfaceConfigure(surface,&cfg); // ! GL supports rgba16, DX12 supports rgba16 and rgb10a2
  WGPUQueue q=wgpuDeviceGetQueue(dev); // *QUEUE*

  /* ---------- mesh data ---------- */
  f32 vertices[6]={ 0.0f,0.5f,  -0.5f,-0.5f,  0.5f,-0.5f };
  struct {f32 x,y;} instances[]={{-0.5f,-0.5f}, {0.5f,-0.5f}, {-0.5, 0.5}, {0.5, 0.5}};

  /* ---------- GPU buffers ---------- */
  WGPUBuffer VERTICES=mkbuf(dev,sizeof vertices,WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst);
  wgpuQueueWriteBuffer(q,VERTICES,0,vertices,sizeof vertices);

  WGPUBuffer INSTANCES=mkbuf(dev,sizeof instances,WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst);
  wgpuQueueWriteBuffer(q,INSTANCES,0,&instances,sizeof instances);

  WGPUBuffer VISIBLE = mkbuf(dev,256*sizeof(i32),WGPUBufferUsage_Storage);
  WGPUBuffer COUNTERS = mkbuf(dev,5*sizeof(i32),WGPUBufferUsage_Storage|WGPUBufferUsage_Indirect|WGPUBufferUsage_CopyDst);
  WGPUBuffer VARYINGS = mkbuf(dev,MAX_VERTICES*sizeof(f32)*2,WGPUBufferUsage_Storage|WGPUBufferUsage_Vertex);
  WGPUBuffer INDICES = mkbuf(dev,MAX_INSTANCES*sizeof(i16)*2,WGPUBufferUsage_Storage|WGPUBufferUsage_Index);
  WGPUBuffer DISPATCH = mkbuf(dev,3*sizeof(u32),WGPUBufferUsage_Storage|WGPUBufferUsage_Indirect);

  /* ---------- load shader ---------- */
  FILE*f=fopen("shaders.wgsl","rb"); if(!(f)){MessageBoxA(0,"f",":(",0);ExitProcess(1);};
  fseek(f,0,SEEK_END);i64 sz=ftell(f);rewind(f);char*src=malloc(sz+1);fread(src,1,sz,f);src[sz]=0;fclose(f);
  WGPUShaderModuleWGSLDescriptor wdesc={.chain.sType=WGPUSType_ShaderModuleWGSLDescriptor,.code=src};
  WGPUShaderModule sm=wgpuDeviceCreateShaderModule(dev,&(WGPUShaderModuleDescriptor){.nextInChain=&wdesc.chain});
  free(src);

  /* ---------- bind group layout & group (single layout for all passes) ---------- */
  WGPUBindGroupEntry be[7] = {
      { NULL, 0, INSTANCES, 0, sizeof instances, NULL,NULL },
      { NULL, 1, VISIBLE, 0, 256 * 4, NULL,NULL },
      { NULL, 2, COUNTERS, 0, sizeof(i32) * 5, NULL,NULL },
      { NULL, 3, VERTICES, 0, sizeof vertices, NULL,NULL },
      { NULL, 4, VARYINGS, 0, MAX_VERTICES * 8, NULL,NULL },
      { NULL, 5, INDICES, 0, MAX_INSTANCES * 4, NULL,NULL },
      { NULL, 6, DISPATCH, 0, 3 * sizeof(u32), NULL, NULL }
  };
  #define C_STORAGE(x) {NULL,x,4,{.type=2},{0},{0},{0}}
  #define C_READONLY(x) {NULL,x,4,{.type=3},{0},{0},{0}}
  WGPUBindGroupLayoutEntry l[7] = {C_READONLY(0),C_STORAGE(1),C_STORAGE(2),C_READONLY(3),C_STORAGE(4),C_STORAGE(5),C_STORAGE(6)};
  WGPUBindGroupLayout bgl=wgpuDeviceCreateBindGroupLayout(dev,&(WGPUBindGroupLayoutDescriptor){.entries=l,.entryCount=7});
  WGPUBindGroup bg=wgpuDeviceCreateBindGroup(dev,&(WGPUBindGroupDescriptor){.layout=bgl,.entries=be,.entryCount=7});

  /* ---------- compute pipelines ---------- */
  WGPUPipelineLayout pl=wgpuDeviceCreatePipelineLayout(dev,&(WGPUPipelineLayoutDescriptor){.bindGroupLayouts=&bgl,.bindGroupLayoutCount=1});
  WGPUComputePipeline cp1=wgpuDeviceCreateComputePipeline(dev,&(WGPUComputePipelineDescriptor){.layout=pl,.compute={.module=sm,.entryPoint="cs_instance"}});
  WGPUComputePipeline cp2 =wgpuDeviceCreateComputePipeline(dev,&(WGPUComputePipelineDescriptor){.layout = pl,.compute = {.module = sm, .entryPoint = "cs_prepare"}});
  WGPUComputePipeline cp3=wgpuDeviceCreateComputePipeline(dev,&(WGPUComputePipelineDescriptor){.layout=pl,.compute={.module=sm,.entryPoint="cs_meshlet"}});

  /* ---------- render pipeline ---------- */
  WGPUVertexAttribute vat={.format=WGPUVertexFormat_Float32x2,.offset=0,.shaderLocation=0};
  WGPUVertexBufferLayout vbl={.arrayStride=8,.attributeCount=1,.attributes=&vat};
  WGPURenderPipeline rp=wgpuDeviceCreateRenderPipeline(dev,&(WGPURenderPipelineDescriptor){
     .layout=wgpuDeviceCreatePipelineLayout(dev,&(WGPUPipelineLayoutDescriptor){0}),
     .vertex={.module=sm,.entryPoint="vs_main",.buffers=&vbl,.bufferCount=1},
     .primitive={.topology=WGPUPrimitiveTopology_TriangleList},
     .multisample = { .count = 1, .mask = 0xFFFFFFFF, .alphaToCoverageEnabled = false },
     .fragment=&(WGPUFragmentState){.module=sm,.entryPoint="fs_main",.targetCount=1,.targets=&(WGPUColorTargetState){.format=fmt,.writeMask=WGPUColorWriteMask_All}}
  });

  MSG msg; bool quit=false; while(!quit){ // *WINDOWS EVENT LOOP*
    while(PeekMessage(&msg,NULL,0,0,PM_REMOVE)){ if(msg.message==WM_QUIT) quit=true; TranslateMessage(&msg);DispatchMessage(&msg);}
    /* reset counters */
    uint32_t zero[5]={0,0,0,0,0}; wgpuQueueWriteBuffer(q,COUNTERS,0,zero,20);

    /* command encoder */
    WGPUCommandEncoder enc=wgpuDeviceCreateCommandEncoder(dev,NULL);
    
    /* --- compute passes --- */
    WGPUComputePassEncoder cpe = wgpuCommandEncoderBeginComputePass(enc, NULL);
    wgpuComputePassEncoderSetPipeline(cpe, cp1);
    wgpuComputePassEncoderSetBindGroup(cpe, 0, bg, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(cpe, 1, 1, 1);
    wgpuComputePassEncoderSetPipeline(cpe, cp2);
    wgpuComputePassEncoderDispatchWorkgroups(cpe, 1, 1, 1);
    wgpuComputePassEncoderSetPipeline(cpe, cp3);
    wgpuComputePassEncoderDispatchWorkgroupsIndirect(cpe, DISPATCH, 0);
    wgpuComputePassEncoderEnd(cpe);
    wgpuComputePassEncoderRelease(cpe);

    /* --- render pass --- */
    WGPUSurfaceTexture st; wgpuSurfaceGetCurrentTexture(surface, &st);
    WGPUTextureView tv = wgpuTextureCreateView(st.texture, NULL);
    WGPURenderPassColorAttachment ca = {
        .view       = tv,
        .loadOp     = WGPULoadOp_Clear,
        .storeOp    = WGPUStoreOp_Store,
        .clearValue = {0, 0, 0, 1}
    };
    WGPURenderPassDescriptor rpd = {
        .colorAttachmentCount = 1,
        .colorAttachments     = &ca
    };
    WGPURenderPassEncoder rpe = wgpuCommandEncoderBeginRenderPass(enc, &rpd);
    wgpuRenderPassEncoderSetPipeline(rpe, rp);
    wgpuRenderPassEncoderSetVertexBuffer(rpe, 0, VARYINGS, 0, MAX_VERTICES * 8);
    wgpuRenderPassEncoderSetIndexBuffer(rpe, INDICES, WGPUIndexFormat_Uint32, 0, MAX_INSTANCES * 4);
    wgpuRenderPassEncoderDrawIndexedIndirect(rpe, COUNTERS, 0);
    wgpuRenderPassEncoderEnd(rpe);
    wgpuRenderPassEncoderRelease(rpe);

    /* --- submit and cleanup --- */
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, NULL);
    wgpuQueueSubmit(q, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuSurfacePresent(surface);
    wgpuTextureViewRelease(tv);

  }
  return 0;
}