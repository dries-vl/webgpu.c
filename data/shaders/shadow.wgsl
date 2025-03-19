// Global uniform block containing the light view-projection matrix,
// among other parameters (brightness, time, etc.).
struct GlobalUniforms {
    brightness: f32,
    time: f32,
    view: mat4x4<f32>,         // unused in shadow pass
    projection: mat4x4<f32>,   // unused in shadow pass
    light_view_proj: mat4x4<f32>,
};
struct MeshUniforms {
    bones: array<mat4x4<f32>, 64>, // 64 bones
};

@group(0) @binding(0)
var<uniform> g_uniforms: GlobalUniforms;
@group(1) @binding(0) // *group 2 for per-mesh* (bones)
var<uniform> m_uniforms: MeshUniforms;

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
    let skin_matrix = 
        m_uniforms.bones[input.bone_indices[0]] * input.bone_weights[0] +
        m_uniforms.bones[input.bone_indices[1]] * input.bone_weights[1] +
        m_uniforms.bones[input.bone_indices[2]] * input.bone_weights[2] +
        m_uniforms.bones[input.bone_indices[3]] * input.bone_weights[3];

    return g_uniforms.light_view_proj * i_transform * skin_matrix * vertex_position;
}