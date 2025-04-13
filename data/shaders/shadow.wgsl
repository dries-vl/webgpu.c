// todo: duplicated from main shader
struct GlobalUniforms {
    shadows: u32,
    time: f32,
    brightness: f32,
    camera_world_space: vec4<f32>,
    view: mat4x4<f32>,  // View matrix
    projection: mat4x4<f32>,    // Projection matrix
    light_view_proj: mat4x4<f32>,
};
struct MaterialUniforms {
    shader: u32,
    reflective: f32,
    padding_1: u32,
    padding_2: u32,
};

@group(0) @binding(0)
var<uniform> g_uniforms: GlobalUniforms;
@group(0) @binding(1)
var animation_sampler: sampler;
@group(0) @binding(2)
var animation_texture: texture_2d<f32>;
@group(0) @binding(3)
var<uniform> material_uniform_array: array<MaterialUniforms, 256>; // hardcoded: 65536 / 256 (size of MaterialUniforms)

// Vertex input includes the vertex position and the per-instance transform.
struct VertexInput {
    @location(1) position: vec3<f32>,
    @location(2) normal: vec4<f32>,
    @location(5) bone_weights: vec4<f32>, // weights (assumed normalized)
    @location(6) bone_indices: vec4<u32>,  // indices into bone_uniforms.bones
    @location(7) i_pos_0: vec4<f32>,
    @location(8) i_pos_1: vec4<f32>,
    @location(9) i_pos_2: vec4<f32>,
    @location(10) i_pos_3: vec4<f32>,
};

// Vertex output just needs to pass the clip-space position.
struct VertexOutput {
  @builtin(position) Position: vec4f,
  @location(0) shadowPos: vec3f,
  @location(1) fragPos: vec3f,
  @location(2) fragNorm: vec3f,
};

@vertex
fn vs_main(input: VertexInput) -> @builtin(position) vec4f {
    var output: VertexOutput;
    let i_transform = mat4x4<f32>(input.i_pos_0, input.i_pos_1,input.i_pos_2,input.i_pos_3);
    let vertex_position = vec4<f32>(input.position, 1.0);
    // let skin_matrix = 
    //     m_uniforms.bones[input.bone_indices[0]] * input.bone_weights[0] +
    //     m_uniforms.bones[input.bone_indices[1]] * input.bone_weights[1] +
    //     m_uniforms.bones[input.bone_indices[2]] * input.bone_weights[2] +
    //     m_uniforms.bones[input.bone_indices[3]] * input.bone_weights[3];

    return g_uniforms.light_view_proj * i_transform/* * skin_matrix*/ * vertex_position;
}