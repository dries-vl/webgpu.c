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
    @location(1) color : vec3<f32>,
    @location(2) uv : vec2<f32>,
    @location(3) instanceOffset : vec3<f32>, // NEW: per-instance offset.
};

// Vertex output.
struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) color: vec3<f32>,
    @location(1) uv: vec2<f32>,
};

@vertex
fn vs_main(input: VertexInput, @builtin(vertex_index) vertex_index: u32) -> VertexOutput {

    let corner_id = vertex_index % 3u;  // Identifies the triangle corner (0,1,2)
    var color: vec3<f32>;
    if (corner_id == 0u) {
        color = vec3<f32>(1.0, 0.0, 0.0);  // Red for first vertex
    } else if (corner_id == 1u) {
        color = vec3<f32>(0.0, 1.0, 0.0);  // Green for second vertex
    } else {
        color = vec3<f32>(0.0, 0.0, 1.0);  // Blue for third vertex
    }

    var output: VertexOutput;
    output.pos = vec4<f32>(input.position + input.instanceOffset, 1.0) * uniforms.camera;
    output.pos = output.pos * uniforms.view;
    output.pos.z = 0.5f; // todo: this might just be the cause of the incorrect ordering of faces / culling issues
    output.color = color;
    output.uv = input.uv;
    return output;
}

@group(1) @binding(0)
var textureSampler: sampler;
@group(1) @binding(1)
var texture0: texture_2d<f32>;

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    // Sample the texture using the uv coordinates.
    var texColor1 = textureSample(texture0, textureSampler, in.uv + vec2(0.5, 0.5));
    var color = texColor1.rgb;
    if (in.color.x < 0.1 || in.color.y < 0.1 || in.color.z < 0.1) {
        color = vec3(0.0);
    }
    return vec4<f32>(color.rgb, 1.0);
}
