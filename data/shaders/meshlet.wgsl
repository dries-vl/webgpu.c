@group(0) @binding(0) var<storage,read>   vis : array<u32>;
@group(0) @binding(1) var<storage,read>   vtx_in : array<packed_vertex>;
@group(0) @binding(2) var<storage,read>   idx_in : array<u32>;
@group(0) @binding(3) var<storage,read>   inst   : array<instance>;
@group(0) @binding(4) var<storage,read_write> stream_vtx : array<vertex>;
@group(0) @binding(5) var<storage,read_write> stream_idx : array<u32>;
@group(0) @binding(6) var<storage,read_write> ctr : gpu_counters;

@compute @workgroup_size(64)
fn cs_meshlet(@builtin(workgroup_id) gid: vec3<u32>,
              @builtin(local_invocation_id) lid: vec3<u32>) {
  let p = gid.x; if(p>=ctr.mlet_cnt){return;}
  let packed=vis[p];     /* 16 b meshlet id | 16 b inst id */
  let inst_id = packed & 0xFFFFu;
  let mlet_id = packed >> 16u;
  let base_v  = mlet_vertex_base(mlet_id);
  let v_cnt   = mlet_vertex_cnt(mlet_id);
  let prim_cnt= mlet_tri_cnt(mlet_id);

  /* === stage 1 : load vertices to shared === */
  var<workgroup> lpos : array<vec3<f32>,64>;
  if(lid.x < v_cnt){
    let pv = vtx_in[base_v+lid.x];
    lpos[lid.x] = mul4x3(inst[inst_id].mat, pv.pos);   /* skin optional */
  }
  workgroupBarrier();

  /* === stage 2 : write triangles === */
  if(lid.x < prim_cnt){
    let tri = idx_in[mlet_index_base(mlet_id)+lid.x];
    let out_base = atomicAdd(&ctr.idx_cnt,3u);
    let v_base  = atomicAdd(&ctr.vtx_cnt,3u);
    stream_vtx[v_base+0]=make_vert(lpos[tri.x], pv.uv0);
    stream_vtx[v_base+1]=make_vert(lpos[tri.y], pv.uv1);
    stream_vtx[v_base+2]=make_vert(lpos[tri.z], pv.uv2);
    stream_idx[out_base+0]=v_base+0u;
    stream_idx[out_base+1]=v_base+1u;
    stream_idx[out_base+2]=v_base+2u;
  }
}