struct Instance { offset: vec2<f32> };
struct Counters { vertex_count: atomic<u32>, index_count: atomic<u32>, instance_count: atomic<u32>, };

@group(0) @binding(0) var<storage,read> INSTANCES: array<Instance>; // IN
@group(0) @binding(1) var<storage,read_write> VISIBLE: array<u32>; // OUT
@group(0) @binding(2) var<storage,read_write> COUNTERS: Counters; // OUT

@group(0) @binding(3) var<storage,read> VERTICES: array<vec2<f32>>; // IN  ~hardcoded 3 vertices data in here
@group(0) @binding(4) var<storage,read_write> VARYINGS: array<vec2<f32>>; // OUT  ~vertex buffer written by meshlet shader
@group(0) @binding(5) var<storage,read_write> INDICES: array<u32>; // OUT  ~index buffer written by meshlet shader

// ---------- compute A : instance → visible ----------
@compute @workgroup_size(64) // todo: we have 64 here, but not really using the same memory at all per instance; still use 64?
fn cs_instance(@builtin(global_invocation_id) id: vec3<u32>) {
    if (id.x >= arrayLength(&INSTANCES)) { return; }
    let slot = atomicAdd(&COUNTERS.instance_count, 1u); // todo: atomic makes no sense without culling/lod, but we'll add it later
    VISIBLE[slot] = id.x;
}

// ---------- compute B : visible instance → 1 triangle ----------
var<workgroup> vertices: array<vec2<f32>, 64>;
var<workgroup> instance: Instance;

@compute @workgroup_size(64)
fn cs_meshlet(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_id) lid: vec3<u32>) {
    if (wg.x >= atomicLoad(&COUNTERS.instance_count)) { return; } // todo: we shouldn't do this, instead only dispatch the amount of meshlets that the instance compute pass writes
    if (lid.x > 3u) { return; } // todo: replace this with comparing the invocation id with the nr of verts in the meshlet

    // *note* we assume about 4-8kb per workgroup of LDS memory -> load in first thing, then only use that going forward
    vertices[lid.x] = VERTICES[lid.x]; // todo: offset into the buffer to get this meshlet's data
    if (lid.x == 0u) {
        let inst_id = VISIBLE[wg.x]; // todo: avoid doing per-vertex?
        instance = INSTANCES[inst_id];
    }
    workgroupBarrier(); // wait for all vertex data to be loaded into LDS

    // todo: offset based on the meshlet id somehow? avoid needing an atomic here?
    VARYINGS[lid.x] = vertices[lid.x] + instance.offset; // write the vertex data (just the NDC of the vertices now) + the offset of the instance

    // one thread writes indices
    if (lid.x == 0u) {
        let base = atomicLoad(&COUNTERS.vertex_count) - 3u;
        let i_base = atomicAdd(&COUNTERS.index_count, 3u);
        INDICES[i_base+0u] = base + 0u;
        INDICES[i_base+1u] = base + 1u;
        INDICES[i_base+2u] = base + 2u;
    }
}

// ---------- raster shaders ----------
struct VSIn  { @location(0) pos : vec2<f32> };
struct VSOut { @builtin(position) pos : vec4<f32> };

@vertex
fn vs_main(i:VSIn) -> VSOut { return VSOut(vec4<f32>(i.pos,0.,1.)); }

@fragment
fn fs_main() -> @location(0) vec4<f32> { return vec4<f32>(0.8,0.4,0.1,1); }