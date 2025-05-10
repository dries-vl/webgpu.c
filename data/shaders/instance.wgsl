@group(0) @binding(0) var<storage,read> inst : array<instance>;
@group(0) @binding(1) var<storage,read_write> vis : array<u32>;
@group(0) @binding(2) var<storage,read_write> ctr : gpu_counters;
@compute @workgroup_size(256)
fn cs_instance(@builtin(global_invocation_id) id: vec3<u32>) {
  let i=id.x; if(i>=inst_len) {return;}
  let ins=inst[i];                             /* load instance */
  if(!in_frustum(ins.sphere)) {return;}        /* quick reject */
  let lod=choose_lod(ins.sphere);              /* distance test */
  for(var m=u32(0); m<lod_mlet_cnt(lod); m++){
    let idx=atomicAdd(&ctr.mlet_cnt,1u);
    vis[idx]=pack(ins.base_mlet+lod_offset(lod)+m, i); /* 32 b */
  }
}