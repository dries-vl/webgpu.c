struct GlobalUniforms {
    brightness: f32,
    time: f32,
    view: mat4x4<f32>,  // View matrix
    projection: mat4x4<f32>,    // Projection matrix
};
struct MaterialUniforms {
    shader: u32,
};
struct BoneUniforms {
    bones: array<mat4x4<f32>, 64>, // 64 bones
};

@group(0) @binding(0) // *group 0 for global* (pipeline)
var<uniform> g_uniforms: GlobalUniforms;
@group(0) @binding(1)
var<uniform> m_uniforms: MaterialUniforms;
@group(0) @binding(2)
var shadow_map: texture_depth_2d;
@group(0) @binding(3)
var shadow_sampler: sampler_comparison;
@group(1) @binding(0) // *group 1 for per-material*
var texture_sampler: sampler;
@group(1) @binding(1)
var tex_0: texture_2d<f32>;
@group(2) @binding(0) // *group 2 for per-mesh*
var<uniform> b_uniforms: BoneUniforms;

const lightViewProj = mat4x4<f32>(
    vec4<f32>( -0.01414,  0.01154,  0.00408,  0.0 ),
    vec4<f32>(  0.0,      0.01152, -0.00816,  0.0 ),
    vec4<f32>(  0.01414,  0.01152,  0.00408,  0.0 ),
    vec4<f32>(  0.0,      0.006,   -0.001,   1.0 )
);

struct VertexInput {
    @location(1) position: vec3<f32>,
    @location(2) normal: vec4<f32>,
    @location(3) tangent: vec4<f32>,
    @location(4) uv: vec2<f32>,
    @location(5) bone_weights: vec4<f32>, // weights (assumed normalized)
    @location(6) bone_indices: vec4<u32>,  // indices into bone_uniforms.bones
    @location(7) i_pos_0: vec4<f32>,  // instance transform row 0
    @location(8) i_pos_1: vec4<f32>,  // instance transform row 1
    @location(9) i_pos_2: vec4<f32>,  // instance transform row 2
    @location(10) i_pos_3: vec4<f32>, // instance transform row 3
    @location(11) i_data: vec3<u32>,
    @location(15) i_atlas_uv: vec2<f32>,
};

struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) color: vec3<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) l: f32,
    @location(3) world_pos: vec4<f32>,
};

@vertex
fn vs_main(input: VertexInput, @builtin(vertex_index) vertex_index: u32) -> VertexOutput {
    var output: VertexOutput;
    let i_transform = mat4x4<f32>(input.i_pos_0, input.i_pos_1,input.i_pos_2,input.i_pos_3);
    let vertex_position = vec4<f32>(input.position, 1.0);
    if (m_uniforms.shader == 1u) {
        // BASE SHADER
        // Skin the vertex by blending bone transforms
        let skin_matrix = 
            b_uniforms.bones[input.bone_indices[0]] * input.bone_weights[0] +
            b_uniforms.bones[input.bone_indices[1]] * input.bone_weights[1] +
            b_uniforms.bones[input.bone_indices[2]] * input.bone_weights[2] +
            b_uniforms.bones[input.bone_indices[3]] * input.bone_weights[3];

        let world_space = i_transform * skin_matrix * vertex_position;
        output.pos = g_uniforms.projection * g_uniforms.view * world_space;
        output.world_pos = world_space;

        let transformedNormal = i_transform * skin_matrix * vec4<f32>(input.normal.xyz, 0.0);
        let worldNormal = normalize(transformedNormal.xyz);
        let diff = max(dot(worldNormal, -vec3(0.5, -0.8, 0.5)), 0.0);
        output.l = pow(diff, 3.0) * 5.;

        output.uv = input.i_atlas_uv + input.uv * max(1.0f, f32(input.i_data.x)); // texture scaling
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
    let tex_color = textureSample(tex_0, texture_sampler, input.uv);
    let depth = 1. / (input.pos.z / input.pos.w);
    let ambient_light = 0.75;
    let light_color = vec3(1.,1.,.8);
    var color = tex_color.rgb * (ambient_light + light_color * input.l) * depth;
    return vec4<f32>(color, tex_color.a);

}


/*
// Only apply shadow mapping for the BASE shader.
    var shadow_factor: f32 = 1.0;
    if (m_uniforms.shader == 1u) {
        // Transform world position into light space.
        let shadow_coord = lightViewProj * input.world_pos;
        let shadow_ndc = shadow_coord.xyz / shadow_coord.w;
        // Convert from NDC [-1,1] to UV space [0,1]
        let shadow_uv = shadow_ndc.xy * 0.5 + vec2<f32>(0.5, 0.5);
        let shadow_depth = shadow_ndc.z * 0.5 + 0.5;
    
        // PCF settings.
        let bias = 0.005;
        let texel_size = vec2<f32>(1.0 / 1024.0, 1.0 / 1024.0); // assuming 1024x1024 shadow map resolution
        var shadow_sum: f32 = 0.0;
        // 3x3 kernel sampling
        for (var x: i32 = -1; x <= 1; x = x + 1) {
            for (var y: i32 = -1; y <= 1; y = y + 1) {
                let offset = vec2<f32>(f32(x), f32(y)) * texel_size;
                shadow_sum += textureSampleCompare(shadow_map, shadow_sampler, shadow_uv + offset, shadow_depth - bias);
            }
        }
        shadow_factor = shadow_sum / 9.0;
    }
    
    // Lighting: ambient plus diffuse modulated by shadow factor.
    let ambient_light = 0.5;
    let light_color = vec3<f32>(1.0, 1.0, 0.8);
    let lighting = ambient_light + light_color * input.l * shadow_factor;
    
    let color = tex_color.rgb * lighting;
    // return vec4<f32>(color, tex_color.a);
    return vec4<f32>(vec3(shadow_factor), 1.0);
*/