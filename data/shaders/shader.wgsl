struct GlobalUniforms {
    time: f32,
    brightness: f32,
    shadows: u32,
    camera_world_space: vec4<f32>,
    view: mat4x4<f32>,  // View matrix
    projection: mat4x4<f32>,    // Projection matrix
    light_view_proj: mat4x4<f32>,
};
struct MaterialUniforms {
    shader: u32,
    reflective: f32,
    animated: u32,
    padding_2: u32,
    padding_3: mat4x4<f32>, // 64 bytes
    padding_3: mat4x4<f32>, // 64 bytes
    padding_3: mat4x4<f32>, // 64 bytes
    padding_4: vec4<f32>,
    padding_4: vec4<f32>,
    padding_4: vec4<f32>,
};

@group(0) @binding(0) var<uniform> global_uniforms: GlobalUniforms;
@group(0) @binding(1) var animation_sampler: sampler;
@group(0) @binding(2) var animation_texture: texture_2d<f32>;
@group(0) @binding(3) var texture_sampler: sampler;
@group(0) @binding(4) var textures: texture_2d_array<f32>;
@group(0) @binding(5) var<uniform> material_uniform_array: array<MaterialUniforms, 256>; // hardcoded: 65536 / 256 (size of MaterialUniforms)
@group(0) @binding(6) var shadow_map: texture_depth_2d;
@group(0) @binding(7) var shadow_sampler: sampler_comparison;
@group(0) @binding(8) var cubemap: texture_cube<f32>;
@group(0) @binding(9) var cubemap_sampler: sampler;

struct VertexInput {
    // Vertex
    @location(1) position: vec3<f32>,
    @location(2) normal: vec4<f32>,
    @location(3) tangent: vec4<f32>,
    @location(4) uv: vec2<f32>,
    @location(5) bone_weights: vec4<f32>, // weights (assumed normalized)
    @location(6) bone_indices: vec4<u32>,  // indices into bone_uniforms.bones
    // Instance
    @location(7) i_pos_0: vec4<f32>,  // instance transform row 0
    @location(8) i_pos_1: vec4<f32>,  // instance transform row 1
    @location(9) i_pos_2: vec4<f32>,  // instance transform row 2
    @location(10) i_pos_3: vec4<f32>, // instance transform row 3
    @location(11) i_data: vec3<u32>,
    @location(12) i_norms: vec4<f32>,
    @location(13) i_animation: u32,
    @location(14) i_frame: f32,
    @location(15) i_atlas_uv: vec2<f32>,
};

const BASE_SHADER: u32 = 0;
const HUD_SHADER: u32 = 1;
const SHADOW_MESH_SHADER: u32 = 2;
const REFLECTION_SHADER: u32 = 3;
const ENV_CUBE_SHADER: u32 = 4;

const animation_size: u32 = 8192; // nr of pixels per animation (is also the width of the texture -> 1 height per animation)
const frame_size: u32 = 64 * 4; // pixels (1 pixel is one vec4 in the bone, 64 bones in a frame/skeleton)
const bone_size: u32 = 4;

@vertex
fn vs_main(input: VertexInput, @builtin(vertex_index) vertex_index: u32) -> VertexOutput {
    var output: VertexOutput;
    var i_transform = mat4x4<f32>(input.i_pos_0, input.i_pos_1,input.i_pos_2,input.i_pos_3);
    var vertex_position = vec4<f32>(input.position, 1.0);
    let shader = input.i_data[1]; // todo: put shader id in material instead
    let material = material_uniform_array[input.i_data[2]];
    var skin_matrix = mat4x4<f32>(    
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0, 
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    );
    if (input.i_atlas_uv.x == 0.0) {
        if (material.animated == 1) {
            let frame = u32(input.i_frame); // [32 x [64 x [p1,p2,p3,p4] ] ] == 8192 pixels
            let frame_start = frame * frame_size;
            let bone_0 = frame_start + (input.bone_indices[0] * bone_size);
            let bone_1 = frame_start + (input.bone_indices[1] * bone_size);
            let bone_2 = frame_start + (input.bone_indices[2] * bone_size);
            let bone_3 = frame_start + (input.bone_indices[3] * bone_size);
            skin_matrix = mat4x4<f32>(textureLoad(animation_texture, vec2<u32>(bone_0, input.i_animation), 0),
                                      textureLoad(animation_texture, vec2<u32>(bone_0 + 1, input.i_animation), 0),
                                      textureLoad(animation_texture, vec2<u32>(bone_0 + 2, input.i_animation), 0),
                                      textureLoad(animation_texture, vec2<u32>(bone_0 + 3, input.i_animation), 0))
                        * input.bone_weights[0]
                        + mat4x4<f32>(textureLoad(animation_texture, vec2<u32>(bone_1, input.i_animation), 0),
                                      textureLoad(animation_texture, vec2<u32>(bone_1 + 1, input.i_animation), 0),
                                      textureLoad(animation_texture, vec2<u32>(bone_1 + 2, input.i_animation), 0),
                                      textureLoad(animation_texture, vec2<u32>(bone_1 + 3, input.i_animation), 0))
                        * input.bone_weights[1]
                        + mat4x4<f32>(textureLoad(animation_texture, vec2<u32>(bone_2, input.i_animation), 0),
                                      textureLoad(animation_texture, vec2<u32>(bone_2 + 1, input.i_animation), 0),
                                      textureLoad(animation_texture, vec2<u32>(bone_2 + 2, input.i_animation), 0),
                                      textureLoad(animation_texture, vec2<u32>(bone_2 + 3, input.i_animation), 0))
                        * input.bone_weights[2]
                        + mat4x4<f32>(textureLoad(animation_texture, vec2<u32>(bone_3, input.i_animation), 0),
                                      textureLoad(animation_texture, vec2<u32>(bone_3 + 1, input.i_animation), 0),
                                      textureLoad(animation_texture, vec2<u32>(bone_3 + 2, input.i_animation), 0),
                                      textureLoad(animation_texture, vec2<u32>(bone_3 + 3, input.i_animation), 0))
                        * input.bone_weights[3];
        }

        var world_space = i_transform * skin_matrix * vertex_position;

        // projected shadow mesh
        if (material.shader == SHADOW_MESH_SHADER) {
            let above = 0.01;
            let distance = -(world_space.y - above) / vec3(0.5, -0.8, 0.5).y;
            let projected_pos = world_space.xyz + (distance * vec3(0.5, -0.8, 0.5));
            output.shadow_depth = max(1. - (1.5 * length(projected_pos - input.i_pos_3.xyz)), 0.);
            world_space = vec4(projected_pos, 1.0);
        }
        
        var view_space = global_uniforms.view * world_space;
        var clip_space = global_uniforms.projection * view_space;

        output.pos = clip_space;
        output.world_space = world_space;
        output.center_pos = input.i_pos_3;
        
        // REFLECTIONS
        // todo: mirrored mesh: fade with depth underwater, distort the mesh/texture itself, transparent water to see (?)
        // todo: pass simult. with shadowmap pass -> planar reflection
        // todo: passes simult. with shadowmap pass -> low res (64-128px) cubemaps for distorted real-time environment reflections
        // todo: shadow mesh projection -> could use dithering, OR, for normal shadow, draw transparent things last, as supposed
        // todo: emscripten (!)

        // DIRECTIONAL LIGHT
        var world_space_normal = normalize(((i_transform * skin_matrix) * vec4<f32>(input.normal.xyz, 0.0)).xyz);
        let diff = max(dot(world_space_normal, -vec3(0.5, -0.8, 0.5)), 0.0);

        output.world_normal = world_space_normal;
        output.light = diff * 2.;
        
        // SHADOW
        if (global_uniforms.shadows == 1) {
            let light_space_pos = global_uniforms.light_view_proj * world_space;
            // Convert XY (-1, 1) to (0, 1), Y is flipped because texture coords are Y-down, Z is already in (0, 1) space
            output.shadow_pos = vec3(light_space_pos.xy * vec2(0.5, -0.5) + vec2(0.5), light_space_pos.z) / light_space_pos.w;
            output.light_space_pos = light_space_pos;
        }
        // UV
        let uv_scale = 1.0 / (1. - input.i_norms[0]);
        output.uv = input.i_atlas_uv + input.uv * uv_scale; // texture scaling
    } else if (input.i_atlas_uv.x != 0.0) {
        // HUD SHADER
        // let i = vertex_index % 3u;
        // output.color = vec3<f32>(select(0.0, 1.0, i == 0u), select(0.0, 1.0, i == 1u), select(0.0, 1.0, i == 2u)); // barycentric coords
        let vertex_position = vec4<f32>(input.position.xy + vec2(0.5, -0.5), 0.0, 1.0); // correct for screen position
        output.pos = i_transform * vertex_position; // no tranformation to camera/view space
        let char_scale = vec2<f32>(1.0 / 16.0, 1.0 / 8.0); // each glyph occupies (1/16, 1/8) of the texture
        output.uv = input.i_atlas_uv + (input.uv * char_scale);
    }
    
    output.i_data = input.i_data;
    return output;
}

struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) light_space_pos: vec4<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) light: f32,
    @location(3) shadow_pos: vec3<f32>,
    @location(4) world_normal: vec3<f32>, // World-space normal for debugging
    @location(5) world_space: vec4<f32>, // World-space normal for debugging
    @location(6) center_pos: vec4<f32>, // World-space normal for debugging
    @location(7) shadow_depth: f32,
    @location(8) i_data: vec3<u32>
};

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let shader = input.i_data[1]; // todo: put shader id in material instead
    let material = material_uniform_array[input.i_data[2]];
    // ENVIRONMENT CUBE
    if (shader == ENV_CUBE_SHADER) {
        return textureSample(cubemap, cubemap_sampler, normalize(input.world_space.xyz));
    }
    if (shader == HUD_SHADER) {
        let color = textureSample(textures, texture_sampler, input.uv, input.i_data[0]);
        if (color.a < 0.49) {
            discard;
        }
        return color;
    }

    let depth = (input.pos.z / input.pos.w);
    let texture_id = input.i_data[0];
    var tex_color = textureSample(textures, texture_sampler, input.uv, texture_id);
    
    var color = tex_color.rgb;
    var alpha = tex_color.a;
    if (tex_color.a < 0.49) {
        discard;
    }

    // REFLECTION CUBEMAP
    if (shader == REFLECTION_SHADER) {
        let P = input.world_space.xyz;
        let probeCenter = vec3<f32>(0.0, 0.0, 0.0);
        let viewDir = normalize(global_uniforms.camera_world_space.xyz - P);
        let r = reflect(-viewDir, input.world_normal);

        // Choose a probe radius that fully encloses your reflective objects.
        let probeRadius: f32 = 100.0;
        let t = intersectSphere(P, r, probeCenter, probeRadius);
        // If t is negative, the ray missed the sphere; in a well-set scene this shouldn't happen.
        let sampleDir = normalize((P + r * t) - probeCenter);

        color = ((1.0 - material.reflective) * color) + (material.reflective * textureSample(cubemap, cubemap_sampler, sampleDir).rgb);

        if (t < 0.) {
            color = vec3(1.,0.,1.);
        }
    }

    // DECAL
    // let decal_uv = vec3(input.shadow_pos.xy * 5. - vec2(2.5,1.4), input.shadow_pos.z);
    // let decal_color = textureSample(tex_1, texture_sampler, decal_uv.xy).rgb;
    // let facing_decal = dot(input.world_normal, -vec3(0.5, -0.8, 0.5)) > 0.0;
    // let hit_by_decal = decal_uv.x >= 0. && decal_uv.x <= 1. && decal_uv.y >= 0. && decal_uv.y <= 1. && decal_uv.z >= 0. && decal_uv.z <= 1.;
    // color = select(tex_color.rgb, decal_color, hit_by_decal && facing_decal);
    
    // SHADOWS
    var shadow = 1.;
    if (global_uniforms.shadows == 1) {
        shadow = calculate_shadow(input);
    }

    if (shader == BASE_SHADER || shader == REFLECTION_SHADER) {
        // OBSCURE DEPTH
        let depth_limit = 20.0;
        let depth_cutoff = pow(depth/depth_limit, 4.0);
        color = color - (color * depth_cutoff);
        alpha = alpha - (alpha * depth_cutoff);
        // LIGHT
        let ambient_light = 0.6;
        let ambient_light_color = vec3(.33, .33, 1.) * ambient_light;
        let dir_light_color = vec3(1.,1.,.5) * input.light * shadow;
        color = color * (ambient_light_color + dir_light_color);
        // color = color / (color + vec3(1.0/2.2)); // gamma (needed for 16bit output only)
        // color = pow(color, vec3(1.0 / 2.2)); // gamma (needed for 16bit output only)
    }

    // VOLUMETRIC LIGHT
    // let vol_light = raymarch_volumetric_light(input);
    // let volumetric_intensity = 0.25;
    // color += (volumetric_intensity * vol_light * vec3(0.8, 0.4, 0.2));

    // SHADOW MESH
    if (shader == SHADOW_MESH_SHADER) {
        // color = vec3(input.shadow_depth);
        color = vec3(0.02,0.02,0.05);
        // alpha = input.shadow_depth;
        if (input.shadow_depth < bayer_dither(vec2<i32>(input.pos.xy) / 8)) { discard; }
    }
   
    // FRESNEL
    // The exponent (5.0) controls the sharpness of the rim.
    // if (material_uniforms.shader == 2) {
    //     let N = input.world_normal;
    //     let vector = global_uniforms.camera_world_space.xyz - input.world_space.xyz;
    //     let len = length(vector) * 0.05;
    //     let V = normalize(vector);
    //     let dotNV = max(dot(N, V), 0.);
    //     let fresnel = pow(1.0 - dotNV, 5.0);
    //     color += vec3(fresnel,0.,0.);
    // }

    return vec4<f32>(color, alpha);
    // todo: normals are not smoothed between triangles in some meshes, causing jank lighting
    // todo: is it possible that the char mesh's normals don't make sense (?)
    // return vec4<f32>(input.world_normal, alpha); //*draw normals*
    // return vec4<f32>(color * color_based_on_shadow_uv(input.shadow_pos), alpha); //*draw shadowmap extent*
    // return vec4<f32>(vec3(smoothstep(0.51, 0.52, 1. - input.light_space_pos.z)), alpha); //*draw shadowmap*
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
    let bias = 0.003;
    let texel_size = vec2<f32>(1. / 1024.0, 1. / 1024.0); // assuming 1024x1024 shadow map resolution
    var shadow_sum: f32 = 0.0;
    let samples: i32 = 0;
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

// Computes the intersection of a ray (origin, normalized direction)
// with a sphere centered at 'center' and with radius 'radius'.
// Returns the distance along the ray (t) to the sphere's surface, or -1.0 if no hit.
// todo: this did not fix seam, find another way to fix the cubemap seam, ideally also cheaper than this
fn intersectSphere(origin: vec3<f32>, dir: vec3<f32>, center: vec3<f32>, radius: f32) -> f32 {
    let oc = origin - center;
    // Since dir is normalized, A = 1.
    let b = dot(oc, dir);
    let c = dot(oc, oc) - radius * radius;
    let discriminant = b * b - c;
    var t: f32;
    if (discriminant < 0.0) {
        t = -1.0;
    } else {
        t = -b + sqrt(discriminant);
    }
    return t;
}

fn bayer_dither(pos: vec2<i32>) -> f32 {
    var m: array<f32, 16> = array<f32, 16>(
        0.0,    0.5,    0.125,  0.625,
        0.75,   0.25,   0.875,  0.375,
        0.1875, 0.6875, 0.0625, 0.5625,
        0.9375, 0.4375, 0.8125, 0.3125
    );
    let i = (pos.y & 3) * 4 + (pos.x & 3);
    return m[i];
}

// Inline Volumetric Lighting Pass
// -------------------------------------------
// Inline Volumetric Lighting with Shadow Sampling
fn raymarch_volumetric_light(input: VertexOutput) -> f32 {
    var volLight = 0.0;
    let numSteps: u32 = 10u;      // Adjust sample count for quality/performance.
    let rayOrigin = global_uniforms.camera_world_space.xyz;
    let rayDir = normalize(input.world_space.xyz - rayOrigin);
    let tMax = length(input.world_space.xyz - rayOrigin);

    // Loop along the ray.
    for (var i: u32 = numSteps; i > 0; i = i - 1u) {
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
        let shadowVal = textureSampleCompare(shadow_map, shadow_sampler, shadowCoord.xy, shadowCoord.z + 0.0);
        
        // Accumulate: only add scattering from lit parts of the volume.
        volLight += shadowVal / f32(numSteps);
    }
    return volLight;
}