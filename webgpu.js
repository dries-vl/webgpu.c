"use strict";

// todo: use JS types @ annotations to get types etc. in this file
// todo: make webgpu.c as concise as possible -> this also very concise
// todo: is there a way to process/compile JS like TS is, to slightly more efficient
// todo: compress file

// ----- Global constants & layout definitions -----
// #region CONST
const MAX_TEXTURES = 4;
const MAX_PIPELINES = 2;
const MAX_MATERIALS = 64;
const MAX_MESHES = 128;
const GLOBAL_UNIFORM_CAPACITY = 1024;
const MATERIAL_UNIFORM_CAPACITY = 256;
const MATERIALS_UNIFORM_BUFFER_TOTAL_SIZE = MATERIAL_UNIFORM_CAPACITY * MAX_MATERIALS;

// Vertex buffer layout (first buffer)
const VERTEX_BUFFER_LAYOUT = {
  arrayStride: 48,
  stepMode: "vertex",
  attributes: [
    { shaderLocation: 0, offset: 0,  format: "uint32x4" },
    { shaderLocation: 1, offset: 16, format: "float32x3" },
    { shaderLocation: 2, offset: 28, format: "unorm8x4" },
    { shaderLocation: 3, offset: 32, format: "unorm8x4" },
    { shaderLocation: 4, offset: 36, format: "unorm16x2" },
    { shaderLocation: 5, offset: 40, format: "unorm8x4" },
    { shaderLocation: 6, offset: 44, format: "uint8x4" }
  ]
};

// Instance buffer layout (second buffer)
const INSTANCE_BUFFER_LAYOUT = {
  arrayStride: 96,
  stepMode: "instance",
  attributes: [
    { shaderLocation: 7,  offset: 0,  format: "float32x4" },
    { shaderLocation: 8,  offset: 16, format: "float32x4" },
    { shaderLocation: 9,  offset: 32, format: "float32x4" },
    { shaderLocation: 10, offset: 48, format: "float32x4" },
    { shaderLocation: 11, offset: 64, format: "uint32x3" },
    { shaderLocation: 12, offset: 76, format: "unorm16x4" },
    { shaderLocation: 13, offset: 84, format: "uint32" },
    { shaderLocation: 14, offset: 88, format: "float32" },
    { shaderLocation: 15, offset: 92, format: "unorm16x2" }
  ]
};
//#endregion

// ----- Global state for GPU contexts -----
// We keep GPUContext objects in a global Map and return an opaque integer id.
let gpuContextCounter = 1;
const gpuContexts = new Map();

// ----- Helper functions to read WASM memory -----
// It is assumed that a global "wasmMemory" (an instance of WebAssembly.Memory) exists.
function readString(ptr) {
  const mem = new Uint8Array(wasmMemory.buffer);
  let str = "";
  for (let i = ptr; mem[i] !== 0; i++) {
    str += String.fromCharCode(mem[i]);
  }
  return str;
}
function readData(ptr, length) {
  return new Uint8Array(wasmMemory.buffer, ptr, length);
}

// ----- Exposed Functions -----

// createGPUContext(int width, int height)
// Creates a canvas, requests the adapter/device, configures WebGPU and creates default resources.
async function createGPUContext(width, height) {
  // Create (and attach) a canvas element
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  document.body.appendChild(canvas);

  if (!navigator.gpu) {
    throw new Error("WebGPU not supported on this browser.");
  }
  const adapter = await navigator.gpu.requestAdapter();
  if (!adapter) {
    throw new Error("Failed to get GPU adapter.");
  }
  const device = await adapter.requestDevice();
  const queue = device.queue;

  // Get the canvas WebGPU context and configure it.
  const context = canvas.getContext("webgpu");
  // Use the browser’s preferred canvas format (or fallback)
  const presentationFormat = navigator.gpu.getPreferredCanvasFormat
    ? navigator.gpu.getPreferredCanvasFormat()
    : "bgra8unorm";
  context.configure({
    device: device,
    format: presentationFormat,
    alphaMode: "opaque",
    size: [width, height]
  });

  // Create bind group layouts for global uniforms and mesh uniforms.
  const globalUniformLayout = device.createBindGroupLayout({
    entries: [{
      binding: 0,
      visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
      buffer: { type: "uniform", hasDynamicOffset: false, minBindingSize: GLOBAL_UNIFORM_CAPACITY }
    }]
  });
  const meshUniformsLayout = device.createBindGroupLayout({
    entries: [{
      binding: 0,
      visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
      buffer: { type: "uniform", hasDynamicOffset: true, minBindingSize: MATERIAL_UNIFORM_CAPACITY }
    }]
  });

  // Create texture bind group layout.
  // Binding 0: sampler, bindings 1..MAX_TEXTURES: textures.
  const textureLayoutEntries = [{
    binding: 0,
    visibility: GPUShaderStage.FRAGMENT,
    sampler: { type: "filtering" }
  }];
  for (let i = 0; i < MAX_TEXTURES; i++) {
    textureLayoutEntries.push({
      binding: i + 1,
      visibility: GPUShaderStage.FRAGMENT,
      texture: { sampleType: "float", viewDimension: "2d", multisampled: false }
    });
  }
  const textureLayout = device.createBindGroupLayout({
    entries: textureLayoutEntries
  });

  // Create a default 1×1 texture (white pixel with RGBA value [127,127,127,127])
  const defaultTexture = device.createTexture({
    size: [1, 1, 1],
    format: "rgba8unorm",
    usage: GPUTextureUsage.COPY_DST | GPUTextureUsage.TEXTURE_BINDING
  });
  const whitePixel = new Uint8Array([127, 127, 127, 127]);
  device.queue.writeTexture(
    { texture: defaultTexture },
    whitePixel,
    { bytesPerRow: 4 },
    [1, 1, 1]
  );
  const defaultTextureView = defaultTexture.createView();

  // Create a depth texture for the render pass.
  const depthTexture = device.createTexture({
    size: [width, height, 1],
    format: "depth24plus",
    usage: GPUTextureUsage.RENDER_ATTACHMENT
  });
  const depthTextureView = depthTexture.createView();

  // Build our GPUContext object (mimicking the C WebGPUContext structure)
  const gpuCtx = {
    canvas, context, adapter,
    /** @type {GPUDevice} */ device,
    queue,
    presentationFormat,
    globalUniformLayout,
    meshUniformsLayout,
    textureLayout,
    defaultTexture, defaultTextureView,
    depthTexture, depthTextureView,
    pipelines: new Array(MAX_PIPELINES).fill(null), // array of pipeline objects
    materials: new Array(MAX_MATERIALS).fill(null),   // array of material objects
    meshes: new Array(MAX_MESHES).fill(null)            // array of mesh objects
  };

  // Save the context and return its opaque pointer (an integer id)
  const id = gpuContextCounter++;
  gpuContexts.set(id, gpuCtx);
  console.log("Created GPUContext with id", id);
  return id;
}

// createGPUPipeline(GPUContext context, const char *shader)
// Loads WGSL shader code (provided as a null-terminated string in WASM memory),
// creates the shader module, pipeline layout (with three bind groups) and render pipeline,
// then allocates uniform buffers and bind groups.
function createGPUPipeline(ctxId, shaderPtr) {
  const gpuCtx = gpuContexts.get(ctxId);
  if (!gpuCtx) return -1;
  const device = gpuCtx.device;
  // Read WGSL shader source from WASM memory.
  const shaderCode = `@vertex
  fn vs_main(
      @location(0) data: vec4<u32>,
      @location(1) position: vec3<f32>,
      @location(7) dummy: vec4<f32>
  ) -> @builtin(position) vec4<f32> {
      return vec4<f32>(position, 1.0);
  }
  @fragment
  fn fs_main() -> @location(0) vec4<f32> {
      return vec4<f32>(1.0, 0.0, 0.0, 1.0);
  }
  `;
  console.log(shaderCode);
  const shaderModule = device.createShaderModule({ code: shaderCode });
  const pipelineLayout = device.createPipelineLayout({
    bindGroupLayouts: [gpuCtx.globalUniformLayout, gpuCtx.meshUniformsLayout, gpuCtx.textureLayout]
  });

  let pipeline;
  try {
    pipeline = device.createRenderPipeline({
      layout: pipelineLayout,
      vertex: {
        module: shaderModule,
        entryPoint: "vs_main",
        buffers: [VERTEX_BUFFER_LAYOUT, INSTANCE_BUFFER_LAYOUT]
      },
      fragment: {
        module: shaderModule,
        entryPoint: "fs_main",
        targets: [{
          format: gpuCtx.presentationFormat,
          writeMask: GPUColorWrite.ALL
        }]
      },
      primitive: {
        topology: "triangle-list",
        cullMode: "back",
        frontFace: "cw"
      },
      depthStencil: {
        format: "depth24plus",
        depthWriteEnabled: true,
        depthCompare: "less"
      },
      multisample: { count: 1 }
    });
  } catch (e) {
    console.error("Failed to create pipeline:", e);
    return -1;
  }

  // Create uniform buffers and bind groups.
  const globalUniformBuffer = device.createBuffer({
    size: GLOBAL_UNIFORM_CAPACITY,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
  });
  const globalUniformBindGroup = device.createBindGroup({
    layout: gpuCtx.globalUniformLayout,
    entries: [{
      binding: 0,
      resource: { buffer: globalUniformBuffer, offset: 0, size: GLOBAL_UNIFORM_CAPACITY }
    }]
  });
  const materialUniformBuffer = device.createBuffer({
    size: MATERIALS_UNIFORM_BUFFER_TOTAL_SIZE,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
  });
  const materialUniformBindGroup = device.createBindGroup({
    layout: gpuCtx.meshUniformsLayout,
    entries: [{
      binding: 0,
      resource: { buffer: materialUniformBuffer, offset: 0, size: MATERIAL_UNIFORM_CAPACITY }
    }]
  });

  // Find an unused pipeline slot.
  let pipelineId = -1;
  for (let i = 0; i < MAX_PIPELINES; i++) {
    if (!gpuCtx.pipelines[i]) {
      pipelineId = i;
      break;
    }
  }
  if (pipelineId < 0) return -1;

  gpuCtx.pipelines[pipelineId] = {
    used: true,
    pipeline,
    globalUniformBuffer,
    globalUniformBindGroup,
    global_uniform_data: new Uint8Array(GLOBAL_UNIFORM_CAPACITY),
    global_uniform_offset: 0,
    materialUniformBuffer,
    materialUniformBindGroup,
    material_ids: new Array(MAX_MATERIALS).fill(-1)
  };

  console.log("Created pipeline", pipelineId, "from shader");
  return pipelineId;
}

// createGPUMesh(GPUContext context, int pipeline_id, void *v, int vc, void *i, int ic, void *ii, int iic)
// Allocates a new material (and mesh) for the given pipeline, creates vertex, index, and instance buffers
// by reading WASM memory at pointers v, i, and ii. (Note: although the given signature’s second parameter
// is named “material_id”, we treat it as the pipeline id as in the original C code.)
function createGPUMesh(ctxId, pipelineId, vPtr, vc, iPtr, ic, iiPtr, iic) {
  const gpuCtx = gpuContexts.get(ctxId);
  if (!gpuCtx) return -1;
  if (pipelineId < 0 || pipelineId >= MAX_PIPELINES || !gpuCtx.pipelines[pipelineId]) {
    console.error("Invalid pipeline ID", pipelineId);
    return -1;
  }
  const device = gpuCtx.device;
  const pipelineObj = gpuCtx.pipelines[pipelineId];

  // Allocate a new material slot.
  let materialId = -1;
  for (let j = 0; j < MAX_MATERIALS; j++) {
    if (!gpuCtx.materials[j]) {
      materialId = j;
      break;
    }
  }
  if (materialId < 0) {
    console.error("No more material slots!");
    return -1;
  }
  // Initialize material.
  const material = {
    used: true,
    pipeline_id: pipelineId,
    mesh_ids: new Array(MAX_MESHES).fill(-1),
    texture_bindgroup: null,
    texture_views: new Array(MAX_TEXTURES).fill(gpuCtx.defaultTextureView),
    texture_sampler: null,
    textures: new Array(MAX_TEXTURES).fill(gpuCtx.defaultTexture),
    texture_count: 0,
    uniform_data: new Uint8Array(MATERIAL_UNIFORM_CAPACITY),
    uniform_offset: 0
  };
  gpuCtx.materials[materialId] = material;
  // Register this material in the pipeline.
  for (let j = 0; j < MAX_MATERIALS; j++) {
    if (pipelineObj.material_ids[j] === -1) {
      pipelineObj.material_ids[j] = materialId;
      break;
    }
  }

  // Allocate a new mesh slot.
  let meshId = -1;
  for (let k = 0; k < MAX_MESHES; k++) {
    if (!gpuCtx.meshes[k]) {
      meshId = k;
      break;
    }
  }
  if (meshId < 0) {
    console.error("No more mesh slots!");
    return -1;
  }

  // Create vertex buffer. (Each vertex is 48 bytes.)
  const vertexDataSize = vc * VERTEX_BUFFER_LAYOUT.arrayStride;
  const vertexData = readData(vPtr, vertexDataSize);
  const vertexBuffer = device.createBuffer({
    size: vertexDataSize,
    usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST
  });
  device.queue.writeBuffer(vertexBuffer, 0, vertexData);

  // Create index buffer. (Assuming 4 bytes per index.)
  const indexDataSize = ic * 4;
  const indexData = readData(iPtr, indexDataSize);
  const indexBuffer = device.createBuffer({
    size: indexDataSize,
    usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST
  });
  device.queue.writeBuffer(indexBuffer, 0, indexData);

  // Create instance buffer. (Each instance is 96 bytes.)
  const instanceDataSize = iic * INSTANCE_BUFFER_LAYOUT.arrayStride;
  const instanceData = readData(iiPtr, instanceDataSize);
  const instanceBuffer = device.createBuffer({
    size: instanceDataSize,
    usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST
  });
  device.queue.writeBuffer(instanceBuffer, 0, instanceData);

  // Create a sampler for this material.
  const textureSampler = device.createSampler({
    minFilter: "linear",
    magFilter: "linear",
    mipmapFilter: "linear",
    addressModeU: "clamp-to-edge",
    addressModeV: "clamp-to-edge",
    addressModeW: "clamp-to-edge"
  });
  material.texture_sampler = textureSampler;

  // Build a texture bind group with the default texture views.
  const entries = [];
  entries.push({ binding: 0, resource: textureSampler });
  for (let i = 0; i < MAX_TEXTURES; i++) {
    entries.push({ binding: i + 1, resource: gpuCtx.defaultTextureView });
  }
  material.texture_bindgroup = device.createBindGroup({
    layout: gpuCtx.textureLayout,
    entries
  });

  // Create the mesh object.
  const mesh = {
    used: true,
    material_id: materialId,
    vertexBuffer, vertexCount: vc,
    indexBuffer, indexCount: ic,
    instanceBuffer, instances: instanceData,
    instance_count: iic
  };
  gpuCtx.meshes[meshId] = mesh;
  // Register mesh in material.
  for (let k = 0; k < MAX_MESHES; k++) {
    if (material.mesh_ids[k] === -1) {
      material.mesh_ids[k] = meshId;
      break;
    }
  }

  console.log(`Created mesh ${meshId} with ${vc} vertices, ${ic} indices, ${iic} instances for pipeline ${pipelineId}`);
  return meshId;
}

// createGPUTexture(GPUContext context, int mesh_id, void *data, int w, int h)
// Creates a texture from the pixel data (RGBA) and uploads it; then updates the material’s texture bind group.
function createGPUTexture(ctxId, meshId, dataPtr, w, h) {
  const gpuCtx = gpuContexts.get(ctxId);
  if (!gpuCtx) return -1;
  const device = gpuCtx.device;
  const mesh = gpuCtx.meshes[meshId];
  if (!mesh) {
    console.error("Invalid mesh id", meshId);
    return -1;
  }
  const material = gpuCtx.materials[mesh.material_id];
  if (!material) {
    console.error("Material not found for mesh", meshId);
    return -1;
  }
  if (material.texture_count >= MAX_TEXTURES) {
    console.error("No more texture slots in mesh!");
    return -1;
  }
  const slot = material.texture_count;

  // Create texture of size w×h.
  const texture = device.createTexture({
    size: [w, h, 1],
    format: "rgba8unorm",
    usage: GPUTextureUsage.COPY_DST | GPUTextureUsage.TEXTURE_BINDING
  });
  const dataSize = 4 * w * h;
  const pixelData = readData(dataPtr, dataSize);
  device.queue.writeTexture(
    { texture },
    pixelData,
    { bytesPerRow: 4 * w },
    [w, h, 1]
  );
  const textureView = texture.createView();

  material.textures[slot] = texture;
  material.texture_views[slot] = textureView;
  material.texture_count++;

  // Rebuild the texture bind group.
  const entries = [];
  entries.push({ binding: 0, resource: material.texture_sampler });
  for (let i = 0; i < MAX_TEXTURES; i++) {
    entries.push({
      binding: i + 1,
      resource: (i < material.texture_count) ? material.texture_views[i] : gpuCtx.defaultTextureView
    });
  }
  material.texture_bindgroup = device.createBindGroup({
    layout: gpuCtx.textureLayout,
    entries
  });

  console.log(`Added texture to material ${mesh.material_id} at slot ${slot}`);
  return slot;
}

// addGPUGlobalUniform(GPUContext context, int pipeline_id, const void* data, int data_size)
// Adds data into the pipeline’s global uniform buffer (stored in RAM) and returns the offset.
function addGPUGlobalUniform(ctxId, pipelineId, dataPtr, data_size) {
  const gpuCtx = gpuContexts.get(ctxId);
  if (!gpuCtx) return -1;
  if (pipelineId < 0 || pipelineId >= MAX_PIPELINES || !gpuCtx.pipelines[pipelineId])
    return -1;
  const pipelineObj = gpuCtx.pipelines[pipelineId];
  const alignment = (data_size <= 4) ? 4 : (data_size <= 8) ? 8 : 16;
  let aligned_offset = (pipelineObj.global_uniform_offset + (alignment - 1)) & ~(alignment - 1);
  if (aligned_offset + data_size > GLOBAL_UNIFORM_CAPACITY) {
    console.warn("Global uniform buffer overflow");
    return -1;
  }
  const data = readData(dataPtr, data_size);
  pipelineObj.global_uniform_data.set(data, aligned_offset);
  pipelineObj.global_uniform_offset = aligned_offset + data_size;
  return aligned_offset;
}

// setGPUGlobalUniformValue(GPUContext context, int pipeline_id, int offset, const void* data, int dataSize)
// Updates a portion of the pipeline’s global uniform buffer.
function setGPUGlobalUniformValue(ctxId, pipelineId, offset, dataSize, dataPtr) {
  const gpuCtx = gpuContexts.get(ctxId);
  if (!gpuCtx) return;
  if (pipelineId < 0 || pipelineId >= MAX_PIPELINES || !gpuCtx.pipelines[pipelineId])
    return;
  const pipelineObj = gpuCtx.pipelines[pipelineId];
  if (offset < 0 || offset + dataSize > pipelineObj.global_uniform_offset)
    return;
  const data = readData(dataPtr, dataSize);
  pipelineObj.global_uniform_data.set(data, offset);
}

// addGPUMaterialUniform(GPUContext context, int material_id, const void* data, int data_size)
// Works like addGPUGlobalUniform but for a material’s uniform data.
function addGPUMaterialUniform(ctxId, materialId, dataPtr, data_size) {
  const gpuCtx = gpuContexts.get(ctxId);
  if (!gpuCtx) return -1;
  if (materialId < 0 || materialId >= MAX_MATERIALS || !gpuCtx.materials[materialId])
    return -1;
  const material = gpuCtx.materials[materialId];
  const alignment = (data_size <= 4) ? 4 : (data_size <= 8) ? 8 : 16;
  let aligned_offset = (material.uniform_offset + (alignment - 1)) & ~(alignment - 1);
  if (aligned_offset + data_size > MATERIAL_UNIFORM_CAPACITY) {
    console.warn("Material uniform buffer overflow");
    return -1;
  }
  const data = readData(dataPtr, data_size);
  material.uniform_data.set(data, aligned_offset);
  material.uniform_offset = aligned_offset + data_size;
  return aligned_offset;
}

// setGPUMaterialUniformValue(GPUContext context, int material_id, int offset, const void* data, int dataSize)
// Updates material uniform data.
function setGPUMaterialUniformValue(ctxId, materialId, offset, dataSize, dataPtr) {
  const gpuCtx = gpuContexts.get(ctxId);
  if (!gpuCtx) return;
  if (materialId < 0 || materialId >= MAX_MATERIALS || !gpuCtx.materials[materialId])
    return;
  const material = gpuCtx.materials[materialId];
  if (offset < 0 || offset + dataSize > material.uniform_offset)
    return;
  const data = readData(dataPtr, dataSize);
  material.uniform_data.set(data, offset);
}

// setGPUInstanceBuffer(GPUContext context, int mesh_id, void* ii, int iic)
// Updates the instance data for the given mesh.
function setGPUInstanceBuffer(ctxId, meshId, iiPtr, iic) {
  const gpuCtx = gpuContexts.get(ctxId);
  if (!gpuCtx) return;
  if (meshId < 0 || meshId >= MAX_MESHES || !gpuCtx.meshes[meshId])
    return;
  const mesh = gpuCtx.meshes[meshId];
  const instanceDataSize = iic * INSTANCE_BUFFER_LAYOUT.arrayStride;
  const instanceData = readData(iiPtr, instanceDataSize);
  mesh.instances = instanceData;
  mesh.instance_count = iic;
  gpuCtx.queue.writeBuffer(mesh.instanceBuffer, 0, instanceData);
}

// drawGPUFrame(GPUContext context)
// Renders one frame. It obtains the current swap-chain texture, builds a command encoder,
// loops over pipelines/materials/meshes (updating uniform buffers and instance buffers) and issues draw calls.
function drawGPUFrame(ctxId) {
  const gpuCtx = gpuContexts.get(ctxId);
  if (!gpuCtx) return -1.0;
  const device = gpuCtx.device;
  const queue = gpuCtx.queue;
  const context = gpuCtx.context;

  const currentTexture = context.getCurrentTexture();
  const currentView = currentTexture.createView();
  const commandEncoder = device.createCommandEncoder();
  const renderPassDesc = {
    colorAttachments: [{
      view: currentView,
      clearValue: { r: 0.1, g: 0.2, b: 0.3, a: 1.0 },
      loadOp: "clear",
      storeOp: "store"
    }],
    depthStencilAttachment: {
      view: gpuCtx.depthTextureView,
      depthClearValue: 1.0,
      depthLoadOp: "clear",
      depthStoreOp: "store"
    }
  };

  const passEncoder = commandEncoder.beginRenderPass(renderPassDesc);

  // Iterate over pipelines.
  for (let p = 0; p < MAX_PIPELINES; p++) {
    const pipelineObj = gpuCtx.pipelines[p];
    if (pipelineObj && pipelineObj.used) {
      passEncoder.setPipeline(pipelineObj.pipeline);
      // Update global uniform buffer on GPU.
      queue.writeBuffer(pipelineObj.globalUniformBuffer, 0, pipelineObj.global_uniform_data);
      passEncoder.setBindGroup(0, pipelineObj.globalUniformBindGroup);

      // Loop over the materials registered to this pipeline.
      for (let m = 0; m < MAX_MATERIALS; m++) {
        const matId = pipelineObj.material_ids[m];
        if (matId === -1) break;
        const material = gpuCtx.materials[matId];
        if (!material || !material.used) continue;
        const material_uniform_offset = matId * MATERIAL_UNIFORM_CAPACITY;
        queue.writeBuffer(pipelineObj.materialUniformBuffer, material_uniform_offset, material.uniform_data);
        passEncoder.setBindGroup(1, pipelineObj.materialUniformBindGroup, [material_uniform_offset]);
        passEncoder.setBindGroup(2, material.texture_bindgroup);

        // Loop over meshes in this material.
        for (let mi = 0; mi < MAX_MESHES; mi++) {
          const meshId = material.mesh_ids[mi];
          if (meshId === -1) break;
          const mesh = gpuCtx.meshes[meshId];
          if (!mesh || !mesh.used) continue;
          // Update instance buffer (if dynamic).
          queue.writeBuffer(mesh.instanceBuffer, 0, mesh.instances);
          passEncoder.setVertexBuffer(0, mesh.vertexBuffer);
          passEncoder.setVertexBuffer(1, mesh.instanceBuffer);
          passEncoder.setIndexBuffer(mesh.indexBuffer, "uint32");
          passEncoder.drawIndexed(mesh.indexCount, mesh.instance_count, 0, 0, 0);
        }
      }
    }
  }

  passEncoder.end();
  const commandBuffer = commandEncoder.finish();
  queue.submit([commandBuffer]);

  // Presentation is handled automatically in WebGPU.
  return -1.0; // (GPU wait time is not measured here.)
}

// ----- Expose the functions to the WASM module -----
// They are attached to the global scope so that the WASM module (or its loader) can import them.
window.createGPUContext = createGPUContext;
window.createGPUPipeline = createGPUPipeline;
window.createGPUMesh = createGPUMesh;
window.createGPUTexture = createGPUTexture;
window.addGPUGlobalUniform = addGPUGlobalUniform;
window.setGPUGlobalUniformValue = setGPUGlobalUniformValue;
window.addGPUMaterialUniform = addGPUMaterialUniform;
window.setGPUMaterialUniformValue = setGPUMaterialUniformValue;
window.setGPUInstanceBuffer = setGPUInstanceBuffer;
window.drawGPUFrame = drawGPUFrame;
