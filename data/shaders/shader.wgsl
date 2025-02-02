struct Uniforms {
    brightness : f32,
    time : f32,
    camera : mat4x4<f32>
};
@group(0) @binding(0)
var<uniform> uniforms : Uniforms;

// Vertex output.
struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) color: vec3<f32>,
    @location(1) uv: vec2<f32>,
};

@vertex
fn vs_main(
    @location(0) position: vec3<f32>,
    @location(1) color: vec3<f32>,
    @location(2) uv: vec2<f32>
) -> VertexOutput {
    var out: VertexOutput;
    out.pos = vec4<f32>(position, 1.0);
    out.color = color;
    out.uv = uv;
    return out;
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
