struct Uniforms {
    brightness : f32,
    time : f32,
    camera : mat4x4<f32>,  // 16 floats (64 bytes)
    view : mat4x4<f32>,  // 16 floats (64 bytes)
};

@group(0) @binding(0)
var<uniform> uniforms : Uniforms;

struct VertexInput {
    @location(0) position : vec3<f32>,
    @location(1) color : vec3<f32>
};

struct VertexOutput {
    @builtin(position) position : vec4<f32>,
    @location(0) color : vec3<f32>
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = vec4<f32>(input.position, 1.0) * uniforms.camera;
    output.position = output.position * uniforms.view;
    output.position.z = 0.5f;
    output.color.y = output.position.z;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let finalColor = input.color;
    return vec4<f32>(finalColor, 1.0);
}
