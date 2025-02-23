// ===== Global Variables and Backend State =====

// (These constants match your C definitions.)
const MAX_MATERIALS = 16;
const MAX_MESHES = 128;
const UNIFORM_BUFFER_CAPACITY = 1024; // bytes

const STANDARD_MAX_TEXTURES = 4;
const TEXTURE_LAYOUT_STANDARD = { layoutIndex: 0, maxTextures: STANDARD_MAX_TEXTURES };

// Enum for vertex layouts.
const VertexLayout = {
  STANDARD_LAYOUT: 0,
  HUD_LAYOUT: 1,
};

// Vertex buffer layout descriptors (for JS WebGPU)
// For STANDARD_LAYOUT: stride = 36 bytes for vertices, 12 bytes for instance data
const VERTEX_LAYOUTS = [
  // STANDARD_LAYOUT
  [
    {
      arrayStride: 36,
      stepMode: 'vertex',
      attributes: [
        { shaderLocation: 0, offset: 0, format: 'float32x3' },
        { shaderLocation: 1, offset: 12, format: 'float32x3' },
        { shaderLocation: 2, offset: 24, format: 'float32x2' },
        // (The 4 bytes for color are omitted here; adjust as needed.)
      ],
    },
    {
      arrayStride: 12,
      stepMode: 'instance',
      attributes: [
        { shaderLocation: 3, offset: 0, format: 'float32x3' },
      ],
    },
  ],
  // HUD_LAYOUT (example layout – adjust if needed)
  [
    {
      arrayStride: 16,
      stepMode: 'vertex',
      attributes: [
        { shaderLocation: 0, offset: 0, format: 'float32x2' },
        { shaderLocation: 1, offset: 8, format: 'float32x2' },
      ],
    },
    {
      arrayStride: 8,
      stepMode: 'instance',
      attributes: [
        { shaderLocation: 2, offset: 0, format: 'sint32' },
        { shaderLocation: 3, offset: 4, format: 'sint32' },
      ],
    },
  ],
];

// Global backend state (mirrors your C global arrays)
const wgpuBackend = {
  canvas: null,
  device: null,
  context: null,
  swapChainFormat: null,
  uniformBindGroupLayout: null,
  textureBindGroupLayouts: [],
  defaultTexture: null,
  defaultTextureView: null,
  // Arrays for created materials and meshes
  materials: new Array(MAX_MATERIALS).fill(null).map(() => ({ used: false })),
  meshes: new Array(MAX_MESHES).fill(null).map(() => ({ used: false })),
};

// A cache for preloaded assets (shader code, textures)
// (It is assumed that all assets are preloaded synchronously before these externs are called.)
const shaderCache = {};
const textureCache = {};

// The WebAssembly memory (must be set by your embedder)
let wasmMemory = null;
function setWasmMemory(memory) {
  wasmMemory = memory;
}

// ===== Helper Functions to Read Wasm Memory =====

// Read a null-terminated C string from wasmMemory starting at address ptr.
function getCString(ptr) {
  const memU8 = new Uint8Array(wasmMemory.buffer);
  let str = "";
  while (memU8[ptr] !== 0) {
    str += String.fromCharCode(memU8[ptr]);
    ptr++;
  }
  return str;
}

// Read a 32-bit little-endian integer.
function getInt32(ptr) {
  const view = new DataView(wasmMemory.buffer);
  return view.getInt32(ptr, true);
}
function getUint32(ptr) {
  const view = new DataView(wasmMemory.buffer);
  return view.getUint32(ptr, true);
}

// ===== Functions to Convert Wasm Structs to JS Objects =====

// Read a TextureLayout struct (2 ints) from wasm memory.
function readTextureLayout(ptr) {
  if (!ptr) return null;
  return {
    layoutIndex: getInt32(ptr),
    maxTextures: getInt32(ptr + 4)
  };
}

function allocateInWasm(instance, size) {
    // Try to use malloc if available.
    if (instance.exports.malloc) {
      return instance.exports.malloc(size);
    }
    // Otherwise, fall back on __heap_base.
    if (instance.exports.__heap_base) {
      // Initialize our own pointer if needed.
      if (typeof window.__wasm_heap_alloc === 'undefined') {
        // __heap_base might be exported as a number.
        window.__wasm_heap_alloc = instance.exports.__heap_base.value || instance.exports.__heap_base;
      }
      let ptr = window.__wasm_heap_alloc;
      // Advance our heap pointer.
      window.__wasm_heap_alloc += size;
      return ptr;
    }
    throw new Error("Cannot allocate memory in wasm: no malloc or __heap_base exported");
  }
  
// Global variables for struct offsets
const MATERIAL_OFFSETS = {};
const MESH_OFFSETS = {};

// Fetch struct offsets from Wasm memory
function loadStructOffsets(instance) {
    const totalInts = 12 + 8; // 20 integers, 20*4 = 80 bytes.
    const offsetsPtr = allocateInWasm(instance, totalInts * 4); // 80 bytes
    instance.exports.get_material_struct_offsets(offsetsPtr);
  
  const mem = new DataView(wasmMemory.buffer);
  MATERIAL_OFFSETS.used = mem.getInt32(offsetsPtr, true);
  MATERIAL_OFFSETS.hash = mem.getInt32(offsetsPtr + 4, true);
  MATERIAL_OFFSETS.index = mem.getInt32(offsetsPtr + 8, true);
  MATERIAL_OFFSETS.use_alpha = mem.getInt32(offsetsPtr + 12, true);
  MATERIAL_OFFSETS.use_textures = mem.getInt32(offsetsPtr + 16, true);
  MATERIAL_OFFSETS.use_uniforms = mem.getInt32(offsetsPtr + 20, true);
  MATERIAL_OFFSETS.update_instances = mem.getInt32(offsetsPtr + 24, true);
  MATERIAL_OFFSETS.vertex_layout = mem.getInt32(offsetsPtr + 28, true);
  MATERIAL_OFFSETS.texture_layout = mem.getInt32(offsetsPtr + 32, true);
  MATERIAL_OFFSETS.shader = mem.getInt32(offsetsPtr + 36, true);
  MATERIAL_OFFSETS.uniformData = mem.getInt32(offsetsPtr + 40, true);
  MATERIAL_OFFSETS.uniformCurrentOffset = mem.getInt32(offsetsPtr + 44, true);

  instance.exports.get_mesh_struct_offsets(offsetsPtr);
  MESH_OFFSETS.material = mem.getInt32(offsetsPtr, true);
  MESH_OFFSETS.indices = mem.getInt32(offsetsPtr + 4, true);
  MESH_OFFSETS.vertices = mem.getInt32(offsetsPtr + 8, true);
  MESH_OFFSETS.instances = mem.getInt32(offsetsPtr + 12, true);
  MESH_OFFSETS.texture_ids = mem.getInt32(offsetsPtr + 16, true);
  MESH_OFFSETS.indexCount = mem.getInt32(offsetsPtr + 20, true);
  MESH_OFFSETS.vertexCount = mem.getInt32(offsetsPtr + 24, true);
  MESH_OFFSETS.instanceCount = mem.getInt32(offsetsPtr + 28, true);

  // Free memory
//   instance.exports.free(offsetsPtr);

  console.log("✅ Material struct offsets loaded:", MATERIAL_OFFSETS);
  console.log("✅ Mesh struct offsets loaded:", MESH_OFFSETS);
}
// Read a Material struct from wasm memory at pointer 'ptr'.
// Returns a JS object that mirrors the C struct and stores the pointer to uniformData.
function readMaterial(ptr) {
  if (!wasmMemory) throw new Error("wasmMemory not set");

  const material = {};
  material.used = getInt32(ptr + MATERIAL_OFFSETS.used);
  material.hash = getInt32(ptr + MATERIAL_OFFSETS.hash);
  material.index = getInt32(ptr + MATERIAL_OFFSETS.index);
  material.use_alpha = getInt32(ptr + MATERIAL_OFFSETS.use_alpha);
  material.use_textures = getInt32(ptr + MATERIAL_OFFSETS.use_textures);
  material.use_uniforms = getInt32(ptr + MATERIAL_OFFSETS.use_uniforms);
  material.update_instances = getInt32(ptr + MATERIAL_OFFSETS.update_instances);
  material.vertex_layout = getInt32(ptr + MATERIAL_OFFSETS.vertex_layout);
  material.texture_layout = getUint32(ptr + MATERIAL_OFFSETS.texture_layout);
  material.shader = getCString(getUint32(ptr + MATERIAL_OFFSETS.shader));
  
  const uniformDataPtr = ptr + MATERIAL_OFFSETS.uniformData;
  material.uniformData = new Uint8Array(wasmMemory.buffer, uniformDataPtr, UNIFORM_BUFFER_CAPACITY).slice();
  material._uniformDataPtr = uniformDataPtr;

  material.uniformCurrentOffset = getInt32(ptr + MATERIAL_OFFSETS.uniformCurrentOffset);
  material._ptr = ptr;

  return material;
}

// Read a Mesh struct from wasm memory at pointer 'ptr'.
// Returns a JS object with copies of the arrays for indices, vertices, and instances.
function readMesh(ptr, layout) {
    if (!wasmMemory) throw new Error("wasmMemory not set");
  
    const mesh = {};
    const memBuffer = wasmMemory.buffer;
  
    const indicesPtr = getUint32(ptr + MESH_OFFSETS.indices);
    const verticesPtr = getUint32(ptr + MESH_OFFSETS.vertices);
    const instancesPtr = getUint32(ptr + MESH_OFFSETS.instances);
  
    mesh.indexCount = getUint32(ptr + MESH_OFFSETS.indexCount);
    mesh.vertexCount = getUint32(ptr + MESH_OFFSETS.vertexCount);
    mesh.instanceCount = getUint32(ptr + MESH_OFFSETS.instanceCount);

    // Copy memory
    mesh.indices = new Uint32Array(memBuffer, indicesPtr, mesh.indexCount).slice();
    const vertex_stride = VERTEX_LAYOUTS[layout][0].arrayStride;
    mesh.vertices = new Uint8Array(memBuffer, verticesPtr, mesh.vertexCount * vertex_stride).slice();
    const instance_stride = VERTEX_LAYOUTS[layout][1].arrayStride;
    mesh.instances = new Uint8Array(memBuffer, instancesPtr, mesh.instanceCount * instance_stride).slice();
  
    return mesh;
  }
  
// Update the uniformData field for all materials from the current wasm memory.
// (Call this at the start of each frame so that any changes in wasm are reflected.)
function updateUniformDataFromWasm() {
    for (let i = 0; i < MAX_MATERIALS; i++) {
      const m = wgpuBackend.materials[i];
      if (m && m.used && m.material && m.material._uniformDataPtr && m.uniformBuffer instanceof GPUBuffer) {
        m.material.uniformData = new Uint8Array(wasmMemory.buffer, m.material._uniformDataPtr, UNIFORM_BUFFER_CAPACITY).slice();
      }
    }
}
  
// ===== Underlying WebGPU API (Synchronous Versions) =====

// Note: Because the C/wasm code expects synchronous calls, we assume that
// all assets (shaders, textures) have been preloaded into shaderCache/textureCache.

// Initialize WebGPU. Creates (or finds) a canvas and configures the WebGPU context.
function wgpuInit(width, height) {
  // Get or create the canvas.
  let canvas = document.getElementById('webgpu-canvas');
  if (!canvas) {
    canvas = document.createElement('canvas');
    canvas.id = 'webgpu-canvas';
    document.body.appendChild(canvas);
  }
  canvas.width = width;
  canvas.height = height;
  wgpuBackend.canvas = canvas;

  if (!navigator.gpu) {
    console.error("WebGPU is not supported.");
    return;
  }

  // Request adapter and device synchronously (blocking the first time).
  // (Note: while adapter/device requests are asynchronous in general,
  // here you must preload before wasm runs, or block your initialization.)
  // For simplicity, we assume this is done before any extern calls.
  // (In practice, you may want to await these before instantiating wasm.)
  return navigator.gpu.requestAdapter().then(adapter => {
    if (!adapter) { console.error("No GPU adapter found."); return; }
    return adapter.requestDevice().then(device => {
      wgpuBackend.device = device;
      wgpuBackend.context = canvas.getContext('webgpu');
      wgpuBackend.swapChainFormat = navigator.gpu.getPreferredCanvasFormat
        ? navigator.gpu.getPreferredCanvasFormat()
        : 'bgra8unorm';
      wgpuBackend.context.configure({
        device: device,
        format: wgpuBackend.swapChainFormat,
        alphaMode: 'opaque'
      });
      // Create global uniform bind group layout.
      wgpuBackend.uniformBindGroupLayout = device.createBindGroupLayout({
        entries: [{
          binding: 0,
          visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
          buffer: { type: 'uniform', minBindingSize: UNIFORM_BUFFER_CAPACITY }
        }]
      });
      // Create texture bind group layout for standard textures.
      const textureBindGroupLayout = device.createBindGroupLayout({
        entries: [
          { binding: 0, visibility: GPUShaderStage.FRAGMENT, sampler: { type: 'filtering' } },
          ...Array.from({ length: STANDARD_MAX_TEXTURES }, (_, i) => ({
            binding: i + 1,
            visibility: GPUShaderStage.FRAGMENT,
            texture: { sampleType: 'float', viewDimension: '2d', multisampled: false }
          }))
        ]
      });
      wgpuBackend.textureBindGroupLayouts[TEXTURE_LAYOUT_STANDARD.layoutIndex] = textureBindGroupLayout;
      // Create a default 1×1 texture.
      const defaultTexture = device.createTexture({
        size: [1, 1, 1],
        format: 'rgba8unorm',
        usage: GPUTextureUsage.COPY_DST | GPUTextureUsage.TEXTURE_BINDING
      });
      const whitePixel = new Uint8Array([127, 127, 127, 127]);
      device.queue.writeTexture(
        { texture: defaultTexture },
        whitePixel,
        { bytesPerRow: 4 },
        { width: 1, height: 1, depthOrArrayLayers: 1 }
      );
      wgpuBackend.defaultTexture = defaultTexture;
      wgpuBackend.defaultTextureView = defaultTexture.createView();

      console.log("[wgpu] Init done.");
    });
  });
}
// Create a material from a wasm Material pointer.
function wgpuCreateMaterialGlue(materialPtr) {
    if (!wgpuBackend.device) {
        console.error("❌ WebGPU device is null! Called too early.");
        console.trace();  // Print the stack trace to find out who is calling this function
        return -1;
        }
  // Convert the wasm Material struct into a JS object.
  const material = readMaterial(materialPtr);
  let materialID = -1;
  for (let i = 0; i < MAX_MATERIALS; i++) {
    if (!wgpuBackend.materials[i].used) {
      materialID = i;
      // Initialize uniformData was already done in readMaterial.
      wgpuBackend.materials[i] = { used: true, material: material };
      break;
    }
  }
  if (materialID < 0) {
    console.error("No more material slots!");
    return -1;
  }

  // Retrieve shader code from the cache
  const shaderCode = shaderCache[material.shader];
  if (!shaderCode) {
    console.error("Shader not preloaded:", material.shader);
    wgpuBackend.materials[materialID].used = false;
    return -1;
  }
  const device = wgpuBackend.device;
  const shaderModule = device.createShaderModule({ code: shaderCode });

  // Create pipeline layout with two bind group layouts.
    // Retrieve texture layout from the material, or use the standard one.
    let textureLayout = material.texture_layout || TEXTURE_LAYOUT_STANDARD;

    // If the layout index isn’t valid, fall back and update the material.
    if (!wgpuBackend.textureBindGroupLayouts[textureLayout.layoutIndex]) {
    console.warn("Invalid texture bind group layout for material, falling back to standard layout.");
    textureLayout = TEXTURE_LAYOUT_STANDARD;
    material.texture_layout = TEXTURE_LAYOUT_STANDARD; // Update the material so later uses are valid.
    }

    const pipelineLayout = device.createPipelineLayout({
    bindGroupLayouts: [
        wgpuBackend.uniformBindGroupLayout,
        wgpuBackend.textureBindGroupLayouts[textureLayout.layoutIndex]
    ]
    });

  // Get vertex buffer layouts based on vertex_layout.
  const vertexBuffers = VERTEX_LAYOUTS[material.vertex_layout];
  const pipeline = device.createRenderPipeline({
    layout: pipelineLayout,
    vertex: {
      module: shaderModule,
      entryPoint: "vs_main",
      buffers: vertexBuffers,
    },
    fragment: {
      module: shaderModule,
      entryPoint: "fs_main",
      targets: [{
        format: wgpuBackend.swapChainFormat,
        blend: material.use_alpha ? {
          color: {
            srcFactor: 'src-alpha',
            dstFactor: 'one-minus-src-alpha',
            operation: 'add'
          },
          alpha: {
            srcFactor: 'one',
            dstFactor: 'zero',
            operation: 'add'
          }
        } : undefined,
        writeMask: GPUColorWrite.ALL,
      }],
    },
    primitive: {
      topology: 'triangle-list',
      cullMode: 'back',
      frontFace: 'cw',
    },
    depthStencil: {
      format: 'depth24plus',
      depthWriteEnabled: true,
      depthCompare: 'less',
    },
    multisample: { count: 1 }
  });

  // Create uniform buffer.
  const uniformBuffer = device.createBuffer({
    size: UNIFORM_BUFFER_CAPACITY,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
  });
  // Create uniform bind group.
  const uniformBindGroup = device.createBindGroup({
    layout: wgpuBackend.uniformBindGroupLayout,
    entries: [{
      binding: 0,
      resource: { buffer: uniformBuffer, offset: 0, size: UNIFORM_BUFFER_CAPACITY }
    }]
  });
  // Create a sampler.
  const textureSampler = device.createSampler({
    minFilter: 'linear',
    magFilter: 'linear',
    mipmapFilter: 'linear',
    addressModeU: 'clamp-to-edge',
    addressModeV: 'clamp-to-edge',
    addressModeW: 'clamp-to-edge',
  });

  // Save material state.
  wgpuBackend.materials[materialID] = {
    used: true,
    material: material,
    pipeline: pipeline,
    uniformBuffer: uniformBuffer,
    uniformBindGroup: uniformBindGroup,
    textureSampler: textureSampler,
  };

  console.log("[wgpu] Created material", materialID, "shader:", material.shader);
  return materialID;
}
// Create a mesh from a wasm Mesh pointer. materialID must be valid.
function wgpuCreateMeshGlue(materialID, meshPtr) {
  if (materialID < 0 || materialID >= MAX_MATERIALS || !wgpuBackend.materials[materialID].used) {
    console.error("Invalid material ID", materialID);
    return -1;
  }
  let meshID = -1;
  for (let i = 0; i < MAX_MESHES; i++) {
    if (!wgpuBackend.meshes[i].used) {
      meshID = i;
      wgpuBackend.meshes[i] = { used: true, materialID: materialID };
      break;
    }
  }
  if (meshID < 0) {
    console.error("No more mesh slots!");
    return -1;
  }
  // Read the mesh struct from wasm memory.
  const material = wgpuBackend.materials[materialID].material;
  const mesh = readMesh(meshPtr, material.vertex_layout);
  // Overwrite the material pointer.
  mesh.material = material;

  const device = wgpuBackend.device;
  // Create vertex buffer.
  const stride_vertex = VERTEX_LAYOUTS[material.vertex_layout][0].arrayStride;
  const vertexDataSize = mesh.vertexCount * stride_vertex;
  const vertexBuffer = device.createBuffer({
    size: vertexDataSize,
    usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
  });
  device.queue.writeBuffer(vertexBuffer, 0, mesh.vertices);

  // Create index buffer.
  const indexDataSize = mesh.indexCount * 4;
  const indexBuffer = device.createBuffer({
    size: indexDataSize,
    usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST,
  });
  device.queue.writeBuffer(indexBuffer, 0, mesh.indices);

  // Create instance buffer.
  const stride_instance = VERTEX_LAYOUTS[material.vertex_layout][1].arrayStride;
  const instanceDataSize = mesh.instanceCount * stride_instance;
  const instanceBuffer = device.createBuffer({
    size: instanceDataSize,
    usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
  });
  device.queue.writeBuffer(instanceBuffer, 0, mesh.instances);

  // Allocate texture arrays and create initial texture bind group.
  const maxTextures = material.texture_layout ? material.texture_layout.maxTextures : STANDARD_MAX_TEXTURES;
  const textureObjects = new Array(maxTextures).fill(wgpuBackend.defaultTexture);
  const textureViews = new Array(maxTextures).fill(wgpuBackend.defaultTextureView);
  const entries = [
    { binding: 0, resource: wgpuBackend.materials[materialID].textureSampler },
  ];
  for (let i = 0; i < maxTextures; i++) {
    entries.push({ binding: i + 1, resource: textureViews[i] });
  }
  const textureBindGroup = device.createBindGroup({
    layout: wgpuBackend.textureBindGroupLayouts[
      material.texture_layout ? material.texture_layout.layoutIndex : TEXTURE_LAYOUT_STANDARD.layoutIndex
    ],
    entries: entries,
  });

  // Save mesh state.
  wgpuBackend.meshes[meshID] = {
    used: true,
    mesh: mesh,
    materialID: materialID,
    vertexBuffer: vertexBuffer,
    indexBuffer: indexBuffer,
    instanceBuffer: instanceBuffer,
    textureBindGroup: textureBindGroup,
    textureObjects: textureObjects,
    textureViews: textureViews,
    textureCount: 0,
    vertexCount: mesh.vertexCount,
    indexCount: mesh.indexCount,
    instanceCount: mesh.instanceCount,
    meshPtr: meshPtr
  };

  console.log("[wgpu] Created mesh", meshID, "with", mesh.vertexCount, "vertices and", mesh.indexCount, "indices.");
  return meshID;
}
// Add a texture to a mesh. texturePathPtr is a pointer to a C string in wasm memory.
function wgpuAddTextureGlue(meshID, texturePathPtr) {
    const meshData = wgpuBackend.meshes[meshID];
    if (!meshData || !meshData.used) {
      console.error("Invalid mesh ID", meshID);
      return -1;
    }
    const materialData = wgpuBackend.materials[meshData.materialID];
    const maxTextures = materialData.material.texture_layout
      ? materialData.material.texture_layout.maxTextures
      : STANDARD_MAX_TEXTURES;
    if (meshData.textureCount >= maxTextures) {
      console.error("No more texture slots in mesh!");
      return -1;
    }
    const slot = meshData.textureCount;
    const texturePath = getCString(texturePathPtr);
    const textureAsset = textureCache[texturePath];
    if (!textureAsset) {
      console.error("Texture not preloaded:", texturePath);
      return -1;
    }
    
    const device = wgpuBackend.device;
    const { pixelData, width, height } = textureAsset;
    
    // Create a GPU texture using the raw pixel data.
    const texture = device.createTexture({
      size: [width, height, 1],
      format: 'rgba8unorm',
      usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
    });
    
    // Upload the pixel data to the texture.
    device.queue.writeTexture(
      { texture: texture },
      pixelData,
      { bytesPerRow: width * 4 }, // 4 bytes per pixel (RGBA)
      { width: width, height: height, depthOrArrayLayers: 1 }
    );
    
    const view = texture.createView();
    meshData.textureObjects[slot] = texture;
    meshData.textureViews[slot] = view;
    meshData.textureCount++;
    
    // Rebuild the texture bind group as needed.
    const entries = [
      { binding: 0, resource: materialData.textureSampler },
    ];
    for (let i = 0; i < maxTextures; i++) {
      entries.push({
        binding: i + 1,
        resource: i < meshData.textureCount ? meshData.textureViews[i] : wgpuBackend.defaultTextureView,
      });
    }
    meshData.textureBindGroup = device.createBindGroup({
      layout: wgpuBackend.textureBindGroupLayouts[
        materialData.material.texture_layout ? materialData.material.texture_layout.layoutIndex : TEXTURE_LAYOUT_STANDARD.layoutIndex
      ],
      entries: entries,
    });
    
    console.log("[wgpu] Added texture", texturePath, "to mesh", meshID, "at slot", slot);
    return slot;
  }
  
// Draw a frame. This synchronous version updates uniforms from wasm memory,
// encodes commands to draw all materials/meshes, submits the command buffer,
// and returns a float (here, simply 0.0 because GPU wait time is not measured synchronously).
function wgpuDrawFrame() {
    const device = wgpuBackend.device;
    const context = wgpuBackend.context;
    updateUniformDataFromWasm();
    const commandEncoder = device.createCommandEncoder();
    const currentTexture = wgpuBackend.context.getCurrentTexture();
    const textureView = currentTexture.createView();
    // Create depth texture
    const depthTexture = device.createTexture({
      size: [wgpuBackend.canvas.width, wgpuBackend.canvas.height, 1],
      format: 'depth24plus',
      usage: GPUTextureUsage.RENDER_ATTACHMENT,
    });
    const depthView = depthTexture.createView();
    const renderPassDesc = {
      colorAttachments: [{
        view: textureView,
        clearValue: { r: 0.1, g: 0.2, b: 0.3, a: 1.0 },
        loadOp: 'clear',
        storeOp: 'store'
      }],
      depthStencilAttachment: {
        view: depthView,
        depthClearValue: 1.0,
        depthLoadOp: 'clear',
        depthStoreOp: 'store'
      }
    };
    const passEncoder = commandEncoder.beginRenderPass(renderPassDesc);
    // For each material...
    for (let materialID = 0; materialID < MAX_MATERIALS; materialID++) {
      const matData = wgpuBackend.materials[materialID];
      if (matData && matData.used && matData.uniformBuffer instanceof GPUBuffer) {
        device.queue.writeBuffer(matData.uniformBuffer, 0, matData.material.uniformData);
        passEncoder.setBindGroup(0, matData.uniformBindGroup);
        passEncoder.setPipeline(matData.pipeline);
        // Draw meshes using this material.
        for (let i = 0; i < MAX_MESHES; i++) {
          const meshData = wgpuBackend.meshes[i];
          if (meshData && meshData.used && meshData.materialID === materialID) {
            passEncoder.setBindGroup(1, meshData.textureBindGroup);        
            passEncoder.setVertexBuffer(0, meshData.vertexBuffer);
            passEncoder.setVertexBuffer(1, meshData.instanceBuffer);
            passEncoder.setIndexBuffer(meshData.indexBuffer, 'uint32');
            passEncoder.drawIndexed(meshData.indexCount, meshData.mesh.instanceCount, 0, 0, 0);
          }
        }
      }
    }
    passEncoder.end();
    const commandBuffer = commandEncoder.finish();
    device.queue.submit([commandBuffer]);
    return 0.0;
}
  
// --- Asset Preloading Functions ---
function preloadShaderSync(url) {
    const xhr = new XMLHttpRequest();
    xhr.open('GET', url, false); // synchronous
    xhr.send(null);
    if (xhr.status === 200 || xhr.status === 0) {
      shaderCache[url] = xhr.responseText;
    } else {
      throw new Error("Failed to load shader " + url);
    }
}

async function preloadBinaryTexture(url) {
    // Fetch the binary file as an ArrayBuffer.
    const response = await fetch(url);
    if (!response.ok) {
      throw new Error(`Failed to load texture ${url}`);
    }
    const buffer = await response.arrayBuffer();
    
    // Read header (assuming 4-byte little-endian ints for width and height).
    const view = new DataView(buffer);
    const width = view.getInt32(0, true);   // Offset 0
    const height = view.getInt32(4, true);    // Offset 4
    
    // The pixel data starts after the header (8 bytes).
    const pixelData = new Uint8Array(buffer, 8);
    
    // Store in cache for later use.
    textureCache[url] = { pixelData, width, height };
    console.log(`Binary texture loaded: ${url} (${width}x${height})`);
    return textureCache[url];
  }
  
  
(async function initApp() {
try {
    console.log("Preloading assets...");
    // Preload shader (synchronously) and texture (asynchronously)
    preloadShaderSync('data/shaders/shader.wgsl');
    preloadShaderSync('data/shaders/hud.wgsl');
    console.log("Shaders loaded.");
    await preloadBinaryTexture('data/textures/bin/font_atlas.bin');
    await preloadBinaryTexture('data/textures/bin/texture_1.bin');
    await preloadBinaryTexture('data/textures/bin/texture_2.bin');
    console.log("Textures loaded.");

    console.log("Initializing WebGPU...");
    // Assign functions directly to avoid recursion
    window.wgpuInit = wgpuInit;
    window.wgpuCreateMaterial = wgpuCreateMaterialGlue;
    window.wgpuCreateMesh = wgpuCreateMeshGlue;
    window.wgpuAddTexture = wgpuAddTextureGlue;
    window.wgpuDrawFrame = wgpuDrawFrame;
    
    // Wait for WebGPU to be fully initialized
    await wgpuInit(1200, 800);
    console.log("WebGPU initialized.");
    
    // Set WebAssembly memory
    const memory = new WebAssembly.Memory({ initial: 256, maximum: 512 });
    const importObject = {
    env: {
        memory: memory,
        wgpuInit: window.wgpuInit,
        wgpuCreateMaterial: window.wgpuCreateMaterial,
        wgpuCreateMesh: window.wgpuCreateMesh,
        wgpuAddTexture: window.wgpuAddTexture,
        wgpuDrawFrame: window.wgpuDrawFrame,
        setWasmMemory: setWasmMemory
    }
    };

    console.log("Fetching wasm module...");
    const wasmResponse = await fetch('main.wasm');
    const wasmBytes = await wasmResponse.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(wasmBytes, importObject);

    
    // Make wasm memory available to our helper functions.
    setWasmMemory(instance.exports.memory || memory);
    console.log("Wasm module instantiated, calling main...");
    
    loadStructOffsets(instance);
    // Now call the wasm main function.
    instance.exports.main();
    
} catch (err) {
    console.error("Initialization error:", err);
}
})();