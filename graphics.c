/*  demo.c – two‑compute‑pass triangle (TCC / wgpu‑native)  */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include "wgpu.h"

#define WIDTH 800
#define HEIGHT 600
#define CHECK(x) if(!(x)){MessageBoxA(0,#x,":(",0);ExitProcess(1);}
static WGPUBuffer mkbuf(WGPUDevice d,uint64_t s,WGPUBufferUsageFlags u){
  WGPUBufferDescriptor desc={.size=s,.usage=u};return wgpuDeviceCreateBuffer(d,&desc);}

/* ---------- Win32 window ---------- */
static LRESULT CALLBACK wndproc(HWND h,UINT m,WPARAM w,LPARAM l){
  if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcA(h,m,w,l);}

void on_adapter(WGPURequestAdapterStatus s, WGPUAdapter a, const char* msg, void* p) {*(WGPUAdapter*)p = a;}
void on_device(WGPURequestDeviceStatus s, WGPUDevice d, const char* msg, void* p) {*(WGPUDevice*)p = d;}
void on_work_done(WGPUQueueWorkDoneStatus status, void* userdata) {bool* done = userdata; *done = 1;}

int WINAPI WinMain(HINSTANCE hi,HINSTANCE prev,LPSTR lp,int n){
  WNDCLASSA wc={.lpfnWndProc=wndproc,.hInstance=hi,.lpszClassName="wgpu"};
  RegisterClassA(&wc);
  HWND hwnd=CreateWindowA("wgpu","triangle",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
      CW_USEDEFAULT,CW_USEDEFAULT,WIDTH,HEIGHT,0,0,hi,0);
  RECT rc;GetClientRect(hwnd,&rc);

  /* ---------- wgpu instance / surface ---------- */
  WGPUInstance ins=wgpuCreateInstance(&(WGPUInstanceDescriptor){0});
  WGPUSurfaceDescriptorFromWindowsHWND sd_hwnd={
    .chain.sType=WGPUSType_SurfaceDescriptorFromWindowsHWND,
    .hinstance=hi,.hwnd=hwnd};
  WGPUSurface surface=wgpuInstanceCreateSurface(ins,
      &(WGPUSurfaceDescriptor){.nextInChain=&sd_hwnd.chain});
  /* adapter + device */
  WGPUAdapter adapter=NULL;WGPUDevice dev=NULL;
  wgpuInstanceRequestAdapter(ins,&(WGPURequestAdapterOptions){.compatibleSurface=surface},
    on_adapter,&adapter);
  while(!adapter) wgpuInstanceProcessEvents(ins);
  wgpuAdapterRequestDevice(adapter,0,
    on_device,&dev);
  while(!dev) wgpuInstanceProcessEvents(ins);
  WGPUQueue q=wgpuDeviceGetQueue(dev);

  /* swap‑chain */
  WGPUTextureFormat fmt=WGPUTextureFormat_BGRA8UnormSrgb;
  WGPUSurfaceConfiguration cfg={.device=dev,.format=fmt,.usage=WGPUTextureUsage_RenderAttachment,
      .width=rc.right,.height=rc.bottom,.presentMode=WGPUPresentMode_Fifo};
  wgpuSurfaceConfigure(surface,&cfg);

  /* ---------- host data ---------- */
  float template_v[6]={ 0.0f,0.5f,  -0.5f,-0.5f,  0.5f,-0.5f };
  struct {float x,y;} inst={0.0f,0.0f}; /* offset */

  /* ---------- GPU buffers ---------- */
  WGPUBuffer template_buf=mkbuf(dev,sizeof template_v,
    WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst);
  wgpuQueueWriteBuffer(q,template_buf,0,template_v,sizeof template_v);

  WGPUBuffer inst_buf=mkbuf(dev,sizeof inst,
    WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst);
  wgpuQueueWriteBuffer(q,inst_buf,0,&inst,sizeof inst);

  const uint64_t MAX_V=3*256, MAX_I=3*256;
  WGPUBuffer vis_buf = mkbuf(dev,256*4,WGPUBufferUsage_Storage);
  WGPUBuffer ctr_buf = mkbuf(dev,20,WGPUBufferUsage_Storage|WGPUBufferUsage_Indirect|WGPUBufferUsage_CopyDst);
  WGPUBuffer stream_v = mkbuf(dev,MAX_V*8,WGPUBufferUsage_Storage|WGPUBufferUsage_Vertex);
  WGPUBuffer stream_i = mkbuf(dev,MAX_I*4,WGPUBufferUsage_Storage|WGPUBufferUsage_Index);

  /* ---------- WGSL shader ---------- */
  FILE*f=fopen("shaders.wgsl","rb");CHECK(f);
  fseek(f,0,SEEK_END);long sz=ftell(f);rewind(f);char*src=malloc(sz+1);fread(src,1,sz,f);src[sz]=0;fclose(f);
  WGPUShaderModuleWGSLDescriptor wdesc={.chain.sType=WGPUSType_ShaderModuleWGSLDescriptor,.code=src};
  WGPUShaderModule sm=wgpuDeviceCreateShaderModule(dev,&(WGPUShaderModuleDescriptor){.nextInChain=&wdesc.chain});
  free(src);

  /* ---------- bind group layout & group (single layout for all passes) ---------- */
  WGPUBindGroupLayoutEntry l[6] = {
      { .nextInChain = NULL, .binding = 0, .visibility = WGPUShaderStage_Compute, .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }, .sampler = {0}, .texture = {0}, .storageTexture = {0} },
      { .nextInChain = NULL, .binding = 1, .visibility = WGPUShaderStage_Compute, .buffer = { .type = WGPUBufferBindingType_Storage },         .sampler = {0}, .texture = {0}, .storageTexture = {0} },
      { .nextInChain = NULL, .binding = 2, .visibility = WGPUShaderStage_Compute, .buffer = { .type = WGPUBufferBindingType_Storage },         .sampler = {0}, .texture = {0}, .storageTexture = {0} },
      { .nextInChain = NULL, .binding = 3, .visibility = WGPUShaderStage_Compute, .buffer = { .type = WGPUBufferBindingType_ReadOnlyStorage }, .sampler = {0}, .texture = {0}, .storageTexture = {0} },
      { .nextInChain = NULL, .binding = 4, .visibility = WGPUShaderStage_Compute, .buffer = { .type = WGPUBufferBindingType_Storage },         .sampler = {0}, .texture = {0}, .storageTexture = {0} },
      { .nextInChain = NULL, .binding = 5, .visibility = WGPUShaderStage_Compute, .buffer = { .type = WGPUBufferBindingType_Storage },         .sampler = {0}, .texture = {0}, .storageTexture = {0} },
  };

  WGPUBindGroupLayout bgl=wgpuDeviceCreateBindGroupLayout(dev,&(WGPUBindGroupLayoutDescriptor){.entries=l,.entryCount=6});

  WGPUBindGroupEntry be[6] = {
      { .nextInChain = NULL, .binding = 0, .buffer = inst_buf,    .offset = 0, .size = sizeof inst,         .sampler = NULL, .textureView = NULL },
      { .nextInChain = NULL, .binding = 1, .buffer = vis_buf,     .offset = 0, .size = 256 * 4,             .sampler = NULL, .textureView = NULL },
      { .nextInChain = NULL, .binding = 2, .buffer = ctr_buf,     .offset = 0, .size = 12,                  .sampler = NULL, .textureView = NULL },
      { .nextInChain = NULL, .binding = 3, .buffer = template_buf,.offset = 0, .size = sizeof template_v,  .sampler = NULL, .textureView = NULL },
      { .nextInChain = NULL, .binding = 4, .buffer = stream_v,    .offset = 0, .size = MAX_V * 8,           .sampler = NULL, .textureView = NULL },
      { .nextInChain = NULL, .binding = 5, .buffer = stream_i,    .offset = 0, .size = MAX_I * 4,           .sampler = NULL, .textureView = NULL },
  };

  WGPUBindGroup bg=wgpuDeviceCreateBindGroup(dev,&(WGPUBindGroupDescriptor){.layout=bgl,.entries=be,.entryCount=6});

  /* ---------- compute pipelines ---------- */
  WGPUPipelineLayout pl=wgpuDeviceCreatePipelineLayout(dev,&(WGPUPipelineLayoutDescriptor){.bindGroupLayouts=&bgl,.bindGroupLayoutCount=1});
  WGPUComputePipeline cpA=wgpuDeviceCreateComputePipeline(dev,&(WGPUComputePipelineDescriptor){.layout=pl,.compute={.module=sm,.entryPoint="cs_instance"}});
  WGPUComputePipeline cpB=wgpuDeviceCreateComputePipeline(dev,&(WGPUComputePipelineDescriptor){.layout=pl,.compute={.module=sm,.entryPoint="cs_meshlet"}});

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

  /* ---------- main loop ---------- */
  MSG msg; bool quit=false; while(!quit){
    while(PeekMessage(&msg,NULL,0,0,PM_REMOVE)){ if(msg.message==WM_QUIT) quit=true; TranslateMessage(&msg);DispatchMessage(&msg);}
    /* reset counters */
    uint32_t zero[3]={0,0,0}; wgpuQueueWriteBuffer(q,ctr_buf,0,zero,12);

    /* command encoder */
    WGPUCommandEncoder enc=wgpuDeviceCreateCommandEncoder(dev,NULL);
    
    /* --- compute passes --- */
    WGPUComputePassEncoder cpe = wgpuCommandEncoderBeginComputePass(enc, NULL);
    wgpuComputePassEncoderSetPipeline(cpe, cpA);
    wgpuComputePassEncoderSetBindGroup(cpe, 0, bg, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(cpe, 1, 1, 1);
    wgpuComputePassEncoderSetPipeline(cpe, cpB);
    wgpuComputePassEncoderDispatchWorkgroups(cpe, 1, 1, 1);
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
    wgpuRenderPassEncoderSetVertexBuffer(rpe, 0, stream_v, 0, MAX_V * 8);
    wgpuRenderPassEncoderSetIndexBuffer(rpe, stream_i, WGPUIndexFormat_Uint32, 0, MAX_I * 4);
    wgpuRenderPassEncoderDrawIndexedIndirect(rpe, ctr_buf, 0);
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