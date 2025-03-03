// struct GlobalUniforms {
//     brightness: f32,
//     time: f32,
//     camera: mat4x4<f32>,  // Projection matrix (IS matrix for projection)
//     view: mat4x4<f32>,    // View matrix
// };
// struct MeshUniforms {
//     index: u32,
// };

// @group(0) @binding(0)
// var<uniform> global_uniforms: GlobalUniforms;
// @group(1) @binding(0)
// var<uniform> mesh_uniforms: MeshUniforms;
@group(2) @binding(0)
var tex_sampler: sampler;
@group(2) @binding(1)
var font_atlas: texture_2d<f32>;

struct VertexInput {
    // From Vertex buffer:
    @location(1) in_pos: vec3<f32>,
    @location(4) in_uv: vec2<f32>,
    // From Instance buffer:
    @location(7) t0: vec4<f32>,
    @location(8) t1: vec4<f32>,
    @location(9) t2: vec4<f32>,
    @location(10) t3: vec4<f32>,
    @location(15) inst_atlas_uv: vec2<f32>,
};

struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    // Reconstruct the 4x4 transform matrix from the instance attributes.
    let inst_matrix = mat4x4<f32>(
        input.t0,
        input.t1,
        input.t2,
        input.t3
    );
    
    // For HUD text, we assume the quad vertex only needs the x and y.
    let vertex_position = vec4<f32>(input.in_pos.xy, 0.0, 1.0);
    output.pos = inst_matrix * vertex_position;
        
    // Compute the final UV into the font atlas
    // Here the font atlas is assumed to have 16 columns and 8 rows,
    // so each glyph occupies (1/16, 1/8) of the texture.
    let char_scale = vec2<f32>(1.0 / 16.0, 1.0 / 8.0);
    output.uv = input.inst_atlas_uv + (input.in_uv * char_scale);
    
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    // Sample the font atlas at the computed coordinate.
    let color = textureSample(font_atlas, tex_sampler, input.uv);
    // Return the color with an alpha computed as the average of the rgb channels.
    return vec4<f32>(color.rgb, (color.r + color.g + color.b) / 3.0);
}
