// Vertex shader: generate positions for a full‑screen quad.
@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> @builtin(position) vec4<f32> {
  var positions = array<vec2<f32>, 6>(
    vec2<f32>(-1.0, -1.0),
    vec2<f32>( 1.0, -1.0),
    vec2<f32>(-1.0,  1.0),
    vec2<f32>(-1.0,  1.0),
    vec2<f32>( 1.0, -1.0),
    vec2<f32>( 1.0,  1.0)
  );
  return vec4<f32>(positions[vertexIndex], 0.0, 1.0);
}

// Fragment shader: sample from the previous frame's texture.
@group(0) @binding(0) var postTexture: texture_2d<f32>;
@group(0) @binding(1) var postSampler: sampler;

@fragment
fn fs_main(@builtin(position) fragCoord: vec4<f32>) -> @location(0) vec4<f32> {
  // Here we assume a fixed screen size; in practice you may pass these as uniforms.
  let uv = fragCoord.xy / vec2<f32>(3456.0, 1944.0);
  return textureSample(postTexture, postSampler, uv);
}
