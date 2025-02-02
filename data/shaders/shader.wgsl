struct Uniforms {
    brightness : f32,
    time : f32,
    camera : mat4x4<f32>,  // 16 floats (64 bytes) IS matrix for projection not position of
    view : mat4x4<f32>,  // 16 floats (64 bytes)
};
@group(0) @binding(0)
var<uniform> uniforms : Uniforms;


struct VertexInput {
    @location(0) position : vec3<f32>,
    @location(1) color : vec3<f32>
};


// Vertex output.
struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) color: vec3<f32>,
    @location(1) uv: vec2<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.pos = vec4<f32>(input.position, 1.0) * uniforms.camera;
    output.pos = output.pos * uniforms.view;
    output.pos.z = 0.5f;
    output.color.y = output.pos.z;
    return output;
}

// Group 1: Textures
@group(1) @binding(0)
var textureSampler: sampler;
@group(1) @binding(1)
var texture0: texture_2d<f32>;
@group(1) @binding(2)
var texture1: texture_2d<f32>;

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    // Sample the texture using the uv coordinates.
    let texColor1 = textureSample(texture0, textureSampler, in.uv);
    let texColor2 = textureSample(texture1, textureSampler, in.uv);
    // Multiply texture color by vertex color.
    return vec4<f32>((texColor1.rgb + texColor2.rgb) * in.color, 1.0);
}
