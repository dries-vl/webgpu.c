// A uniform buffer is declared in group(0) binding(0)
struct Uniforms {
  value: f32,
};

@group(0) @binding(0)
var<uniform> uUniform : Uniforms;

struct VertexOutput {
  @builtin(position) position : vec4<f32>,
  @location(0) color : vec3<f32>,
};

@vertex
fn vs_main(@location(0) a_position: vec3<f32>, @location(1) a_color: vec3<f32>) -> VertexOutput {
  var output: VertexOutput;
  output.position = vec4<f32>(a_position, 1.0);
  output.color = a_color;
  return output;
}

@fragment
fn fs_main(@location(0) color: vec3<f32>) -> @location(0) vec4<f32> {
  // Here the uniform value is used to modulate the incoming color.
  return vec4<f32>(color * uUniform.value, 1.0);
}
