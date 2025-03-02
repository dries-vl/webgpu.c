struct GlobalUniforms {
    brightness: f32,
    time: f32,
    camera: mat4x4<f32>,  // Projection matrix (IS matrix for projection)
    view: mat4x4<f32>,    // View matrix
};
struct MeshUniforms {
    index: u32,
};

@group(0) @binding(0)
var<uniform> global_uniforms: GlobalUniforms;
@group(1) @binding(0)
var<uniform> mesh_uniforms: MeshUniforms;
@group(2) @binding(0)
var texture_sampler: sampler;
@group(2) @binding(1)
var texture: texture_2d_array<f32>;

struct VertexInput {
    // Vertex buffer attributes (stepMode: Vertex)
    @location(1) position: vec3<f32>,
    @location(2) normal: vec4<f32>,
    @location(3) tangent: vec4<f32>,
    @location(4) uv: vec2<f32>,
    // Instance buffer attributes (stepMode: Instance)
    @location(7) t0: vec4<f32>, // transform row 0
    @location(8) t1: vec4<f32>, // transform row 1
    @location(9) t2: vec4<f32>, // transform row 2
    @location(10) t3: vec4<f32>,// transform row 3
};

struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) color: vec3<f32>,
    @location(1) uv: vec2<f32>,
};

@vertex
fn vs_main(input: VertexInput, @builtin(vertex_index) vertex_index: u32) -> VertexOutput {
    var output: VertexOutput;
    
    // barymetric coordinates
    let corner_id = vertex_index % 3u;
    var color: vec3<f32>;
    if (corner_id == 0u) {
        color = vec3<f32>(1.0, 0.0, 0.0);
    } else if (corner_id == 1u) {
        color = vec3<f32>(0.0, 1.0, 0.0);
    } else {
        color = vec3<f32>(0.0, 0.0, 1.0);
    }
    
    // reconstruct the instance transform matrix
    let instance_transform = mat4x4<f32>(
        input.t0,
        input.t1,
        input.t2,
        input.t3
    );
    let vertex_position = vec4<f32>(input.position * 100.0, 1.0);

    // *important* order of multiplication matters here (!)
    output.pos = instance_transform * vertex_position * global_uniforms.camera * global_uniforms.view;
    
    output.color = color;
    output.uv = input.uv;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    // Sample the texture using the interpolated UV coordinates.
    let tex_color = textureSample(texture, texture_sampler, input.uv, 0);
    var color = tex_color.rgb;
    // Darken the color if the per-vertex color is dark.
    if (input.color.x < 0.1 || input.color.y < 0.1 || input.color.z < 0.1) {
        color = color / 2.0;
    }
    return vec4<f32>(color, 1.0);
}
