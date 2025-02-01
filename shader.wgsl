// Vertex Stage
struct VertexOutput {
  @builtin(position) position : vec4<f32>,
  @location(0) color : vec3<f32>,
};

@vertex
fn vs_main(@location(0) a_position: vec2<f32>, @location(1) a_color: vec3<f32>) -> VertexOutput {
  var output: VertexOutput;
  // Convert the 2D position into a 4D homogeneous coordinate.
  output.position = vec4<f32>(a_position, 0.0, 1.0);
  // Pass the incoming color through (could be ignored in the fragment shader).
  output.color = a_color;
  return output;
}

// Fragment Stage
@fragment
fn fs_main(@location(0) color: vec3<f32>) -> @location(0) vec4<f32> {
  // Instead of using the passed-in color, output a constant green color.
  let outColor: vec3<f32> = vec3<f32>(0.0, 1.0, 0.0);
  return vec4<f32>(color, 1.0);
}
