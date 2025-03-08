struct GlobalUniforms {
    brightness: f32,
    time: f32,
    camera: mat4x4<f32>,  // Projection matrix (IS matrix for projection)
    view: mat4x4<f32>,    // View matrix
};
struct MaterialUniforms {
    shader: u32,
};

@group(0) @binding(0)
var<uniform> g_uniforms: GlobalUniforms;
@group(1) @binding(0)
var<uniform> m_uniforms: MaterialUniforms;
@group(2) @binding(0)
var texture_sampler: sampler;
@group(2) @binding(1)
var tex_0: texture_2d<f32>;

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
    @location(11) i_data: vec3<u32>,
    @location(15) i_atlas_uv: vec2<f32>,
};

struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) color: vec3<f32>,
    @location(1) uv: vec2<f32>,
};

@vertex
fn vs_main(input: VertexInput, @builtin(vertex_index) vertex_index: u32) -> VertexOutput {
    var output: VertexOutput;
    let i_transform = mat4x4<f32>(input.t0, input.t1,input.t2,input.t3);
    let vertex_position = vec4<f32>(input.position, 1.0);
    if (m_uniforms.shader == 1u) {
        // BASE SHADER
        let i = vertex_index % 3u;
        output.color = vec3<f32>(select(0.0, 1.0, i == 0u), select(0.0, 1.0, i == 1u), select(0.0, 1.0, i == 2u)); // barycentric coords
        output.pos = i_transform * vertex_position * g_uniforms.camera * g_uniforms.view; // *important* order of multiplication matters here (!)
        output.uv = input.uv * max(1.0f, f32(input.i_data.x)); // texture scaling
    } else if (m_uniforms.shader == 0u) {
        // HUD SHADER
        // let i = vertex_index % 3u;
        // output.color = vec3<f32>(select(0.0, 1.0, i == 0u), select(0.0, 1.0, i == 1u), select(0.0, 1.0, i == 2u)); // barycentric coords
        let vertex_position = vec4<f32>(input.position.xy + vec2(0.5, -0.5), 0.0, 1.0); // correct for screen position
        output.pos = i_transform * vertex_position; // no tranformation to camera/view space
        let char_scale = vec2<f32>(1.0 / 16.0, 1.0 / 8.0); // each glyph occupies (1/16, 1/8) of the texture
        output.uv = input.i_atlas_uv + (input.uv * char_scale);
    }
    
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    // uv wrapping
    let uv = input.uv;
    let tex_color = textureSample(tex_0, texture_sampler, input.uv);
    var color = tex_color.rgb;
    color += 0.5 * (1.0 - min(min(step(0.02, input.color.x), step(0.02, input.color.y)), step(0.02, input.color.z))); // barys
    return vec4<f32>(color, tex_color.a);
}
