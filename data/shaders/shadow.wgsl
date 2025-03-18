struct GlobalUniforms {
    brightness: f32,
    time: f32,
    view: mat4x4<f32>,  // View matrix
    projection: mat4x4<f32>,    // Projection matrix
    lightViewProj: mat4x4<f32>
};

@group(0) @binding(0)
var<uniform> g_uniforms: GlobalUniforms;

// Minimal vertex input: only the position is required.
struct VertexInput {
    @location(1) position: vec3<f32>,
    @location(7) i_pos_0: vec4<f32>,  // instance transform row 0
    @location(8) i_pos_1: vec4<f32>,  // instance transform row 1
    @location(9) i_pos_2: vec4<f32>,  // instance transform row 2
    @location(10) i_pos_3: vec4<f32>, // instance transform row 3
};

@vertex
fn vs_main(input: VertexInput) -> @builtin(position) vec4<f32> {
    let i_transform = mat4x4<f32>(input.i_pos_0, input.i_pos_1,input.i_pos_2,input.i_pos_3);

    // Transform the vertex position by the light view-projection matrix.
    return g_uniforms.lightViewProj * i_transform * vec4<f32>(input.position, 1.0);
}
