struct GlobalUniforms {
    brightness: f32,
    time: f32,
    view: mat4x4<f32>,  // View matrix
    projection: mat4x4<f32>,    // Projection matrix
    shadows: u32,
    camera_world_space: vec4<f32>,
    light_view_proj: mat4x4<f32>,
};
struct MaterialUniforms {
    shader: u32,
};
struct MeshUniforms {
    bones: array<mat4x4<f32>, 64>, // 64 bones
};

@group(0) @binding(0) // *group 0 for global* (global uniforms)
var<uniform> global_uniforms: GlobalUniforms;
@group(1) @binding(0) // *group 1 for pipeline* (material uniforms, shadows)
var<uniform> material_uniforms: MaterialUniforms;
@group(1) @binding(1)
var shadow_map: texture_depth_2d;
@group(1) @binding(2)
var shadow_sampler: sampler_comparison;
@group(2) @binding(0) // *group 2 for per-material* (textures)
var texture_sampler: sampler;
@group(2) @binding(1)
var tex_0: texture_2d<f32>;
@group(2) @binding(2)
var tex_1: texture_2d<f32>;
@group(3) @binding(0) // *group 3 for per-mesh* (bones)
var<uniform> mesh_uniforms: MeshUniforms;

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

@vertex
fn vs_main(input: VertexInput, @builtin(vertex_index) vertex_index: u32) -> VertexOutput {
    var output: VertexOutput;
    let i_transform = mat4x4<f32>(input.i_pos_0, input.i_pos_1,input.i_pos_2,input.i_pos_3);
    let vertex_position = vec4<f32>(input.position, 1.0);
    if (material_uniforms.shader >= 1u) {
        // BASE SHADER
        let skin_matrix = 
            mesh_uniforms.bones[input.bone_indices[0]] * input.bone_weights[0] +
            mesh_uniforms.bones[input.bone_indices[1]] * input.bone_weights[1] +
            mesh_uniforms.bones[input.bone_indices[2]] * input.bone_weights[2] +
            mesh_uniforms.bones[input.bone_indices[3]] * input.bone_weights[3];

        let world_space = i_transform * skin_matrix * vertex_position;
        let view_space = global_uniforms.view * world_space;
        output.pos = global_uniforms.projection * view_space;
        output.world_space = world_space;
        output.center_pos = input.i_pos_3;

        if (material_uniforms.shader == 2) {
            output.pos.z += 0.001;
        }

        // DIRECTIONAL LIGHT
        let world_space_normal = normalize(((i_transform * skin_matrix) * vec4<f32>(input.normal.xyz, 0.0)).xyz);
        let diff = max(dot(world_space_normal, -vec3(0.5, -0.8, 0.5)), 0.0);
        output.world_normal = world_space_normal;
        output.light = diff;

        // SHADOW
        if (global_uniforms.shadows == 1) {
            let light_space_pos = global_uniforms.light_view_proj * world_space;
            // Convert XY (-1, 1) to (0, 1), Y is flipped because texture coords are Y-down, Z is already in (0, 1) space
            output.shadow_pos = vec3(light_space_pos.xy * vec2(0.5, -0.5) + vec2(0.5), light_space_pos.z) / light_space_pos.w;
            output.color = light_space_pos;
        }
        // UV
        output.uv = input.i_atlas_uv + input.uv * max(1.0f, f32(input.i_data.x)); // texture scaling
    } else if (material_uniforms.shader == 0u) {
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

struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) color: vec4<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) light: f32,
    @location(3) shadow_pos: vec3<f32>,
    @location(4) world_normal: vec3<f32>, // World-space normal for debugging
    @location(5) world_space: vec4<f32>, // World-space normal for debugging
    @location(6) center_pos: vec4<f32>, // World-space normal for debugging
};

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let depth_limit = 20.0;
    let depth = (input.pos.z / input.pos.w);
    var tex_color = textureSample(tex_0, texture_sampler, input.uv);
    var color = tex_color.rgb;
    var alpha = tex_color.a;
    if (tex_color.a < 0.49) {
        discard;
    }
    // let decal_uv = vec3(input.shadow_pos.xy * 5. - vec2(2.5,1.4), input.shadow_pos.z);
    // let decal_color = textureSample(tex_1, texture_sampler, decal_uv.xy).rgb;
    // let facing_decal = dot(input.world_normal, -vec3(0.5, -0.8, 0.5)) > 0.0;
    // let hit_by_decal = decal_uv.x >= 0. && decal_uv.x <= 1. && decal_uv.y >= 0. && decal_uv.y <= 1. && decal_uv.z >= 0. && decal_uv.z <= 1.;
    // color = select(tex_color.rgb, decal_color, hit_by_decal && facing_decal);
    var shadow = 1.;
    if (global_uniforms.shadows == 1 && material_uniforms.shader >= 1) {
        shadow = calculate_shadow(input);
    }

    if (material_uniforms.shader == 1) {
        // apply light
        let ambient_light = 0.2;
        let ambient_light_color = vec3(.33, .33, 1.) * ambient_light;
        let dir_light_color = vec3(1.,1.,.5) * input.light * shadow;
        color = color * min(ambient_light_color + dir_light_color, vec3(1.));
        // obscure depth
        color = color - (color * (depth/depth_limit));
    } else if (material_uniforms.shader == 2) {
        color = vec3(0.);
        alpha = 1.;
    }
    
    // Compute the Fresnel factor using a power term.
    // The exponent (5.0) controls the sharpness of the rim.
    // let N = input.world_normal;
    // let vector = global_uniforms.camera_world_space.xyz - input.world_space.xyz;
    // let len = length(vector) * 0.05;
    // let V = normalize(vector);
    // let dotNV = max(dot(N, V), 0.);
    // let fresnel = pow(1.0 - dotNV, 5.0);
    // color += vec3(fresnel,0.,0.);

        // Inline Volumetric Lighting Pass
    // -------------------------------------------
    // Inline Volumetric Lighting with Shadow Sampling
    var volLight = 0.0;
    let numSteps: u32 = 32u;      // Adjust sample count for quality/performance.
    let stepSize: f32 = 0.5;      // Distance between samples.
    let rayOrigin = global_uniforms.camera_world_space.xyz;
    let rayDir = normalize(input.world_space.xyz - rayOrigin);
    let tMax = length(input.world_space.xyz - rayOrigin);
    let decayFactor: f32 = 0.2;   // Adjust for medium density.

    // Loop along the ray.
    for (var i: u32 = 0u; i < numSteps; i = i + 1u) {
        let t = (f32(i) / f32(numSteps)) * tMax;
        let samplePos = rayOrigin + rayDir * t;
        
        // Transform samplePos into light space.
        let lightSpacePos = global_uniforms.light_view_proj * vec4<f32>(samplePos, 1.0);
        let lightSpacePosNDC = lightSpacePos / lightSpacePos.w; // Now in NDC (-1, 1)
        
        // Convert NDC to texture coordinates.
        let shadowCoord = vec3<f32>(
            lightSpacePosNDC.xy * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5, 0.5),
            lightSpacePosNDC.z
        );
        
        // Sample the shadow map; result is 1.0 if lit, 0.0 if in shadow (with PCF it might be fractional).
        let shadowVal = textureSampleCompare(shadow_map, shadow_sampler, shadowCoord.xy, shadowCoord.z - 0.002);
        
        // Calculate the medium density at this sample. You can combine this with shadowVal.
        let density = exp(-t * decayFactor);
        
        // Accumulate: only add scattering from lit parts of the volume.
        volLight += shadowVal * density * stepSize;
    }
    
    // Scale the accumulated light by an intensity factor.
    let volumetricIntensity = 0.1;
    // -------------------------------------------
    color += (volumetricIntensity * volLight * vec3(0.8, 0.4, 0.2));
    color = vec3(min(color.x, 1.), min(color.y, 1.), min(color.z, 1.));

    return vec4<f32>(color, alpha);
    // todo: normals are not smoothed between triangles in some meshes, causing jank lighting
    // todo: is it possible that the char mesh's normals don't make sense (?)
    // return vec4<f32>(input.world_normal, alpha); //*draw normals*
    // return vec4<f32>(color * color_based_on_shadow_uv(input.shadow_pos), alpha); //*draw shadowmap extent*
    // return vec4<f32>(vec3(smoothstep(0.51, 0.52, 1. - input.color.z)), alpha); //*draw shadowmap*
    // return vec4<f32>(vec3(shadow), alpha); //*draw only shadows*
    // return vec4<f32>(vec3(1./depth), alpha); //*draw depth*
}

// function to show the extent of the shadow map
fn color_based_on_shadow_uv(shadow_uv: vec3<f32>) -> f32 {
    let near_zero  = (shadow_uv >= vec3<f32>(0.0)) & (shadow_uv <= vec3<f32>(0.01));
    let near_one   = (shadow_uv >= vec3<f32>(0.99)) & (shadow_uv <= vec3<f32>(1.0));
    let not_outside = shadow_uv.x > 1. || shadow_uv.x < 0. || shadow_uv.y > 1. || shadow_uv.y < 0;
    if (any(near_zero | near_one) & !not_outside) {
        return 0.;
    }
    return 1.;
}

fn calculate_shadow(input: VertexOutput) -> f32 {
    // 3x3 kernel sampling
    // PCF settings.
    let bias = 0.001;
    let texel_size = vec2<f32>(1. / 1024.0, 1. / 1024.0); // assuming 1024x1024 shadow map resolution
    var shadow_sum: f32 = 0.0;
    let samples: i32 = 1;
    for (var x: i32 = -samples; x <= samples; x = x + 1) {
        for (var y: i32 = -samples; y <= samples; y = y + 1) {
            let offset = vec2<f32>(f32(x), f32(y)) * texel_size;
            shadow_sum += textureSampleCompare(shadow_map, shadow_sampler, input.shadow_pos.xy + offset, input.shadow_pos.z - bias);
        }
    }
    var shadow_factor = shadow_sum / pow(f32(samples * 2 + 1), 2.);

    // distance to camera
    let diff = global_uniforms.camera_world_space - input.world_space;
    let distance: f32 = length(diff) * 0.05;

    shadow_factor = clamp(shadow_factor + distance, 0., 1.);
    return smoothstep(0.75, 1.0, shadow_factor);
}