struct Uniforms {
    brightness: f32,
    time: f32,
    camera: mat4x4<f32>,  // Projection matrix (IS matrix for projection)
    view: mat4x4<f32>,    // View matrix
};

@group(0) @binding(0)
var<uniform> uniforms: Uniforms;

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
    
    // Determine a color based on the triangle corner.
    let corner_id = vertex_index % 3u;
    var color: vec3<f32>;
    if (corner_id == 0u) {
        color = vec3<f32>(1.0, 0.0, 0.0);
    } else if (corner_id == 1u) {
        color = vec3<f32>(0.0, 1.0, 0.0);
    } else {
        color = vec3<f32>(0.0, 0.0, 1.0);
    }
    
    // Reconstruct the instance transform matrix from instance attributes.
    let inst_matrix = mat4x4<f32>(
        input.t0,
        input.t1,
        input.t2,
        input.t3
    );
    
    // Transform the vertex position:
    // - Scale the vertex position by 100.0,
    // - Apply the instance transform,
    // - Then apply the camera (projection) and view matrices.
    let world_pos = inst_matrix * vec4<f32>(input.position * 100.0, 1.0);
    let proj_pos = uniforms.camera * world_pos;
    output.pos = uniforms.view * proj_pos;
    
    output.color = color;
    output.uv = input.uv;
    return output;
}

@group(1) @binding(0)
var textureSampler: sampler;
@group(1) @binding(1)
var texture0: texture_2d<f32>;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    // Sample the texture using the interpolated UV coordinates.
    let tex_color = textureSample(texture0, textureSampler, input.uv);
    var color = tex_color.rgb;
    // Darken the color if the per-vertex color is dark.
    if (input.color.x < 0.1 || input.color.y < 0.1 || input.color.z < 0.1) {
        color = color / 2.0;
    }
    return vec4<f32>(color, 1.0);
}
