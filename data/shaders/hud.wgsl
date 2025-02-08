struct VertexInput {
    @location(0) position : vec2<f32>,
    @location(1) uv : vec2<f32>,
    @location(2) i_pos : i32,
    @location(3) i_char : i32
};

// Vertex output.
struct VertexOutput {
    @builtin(position) pos: vec4<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) char_uv: vec2<f32>
};

const char_scale = vec2(1.0 / 16.0, 1.0 / 8.0);
const columns = 16;  // glyphs per row in atlas
const rows = 8;      // total rows in atlas
const text_columns = 32; // nr of characters that fit across screen-width
const text_rows = 16; // nr of characters that fit across screen-height

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    // calculate the position of this character
    var pos_col = input.i_pos % text_columns;
    var pos_row = input.i_pos / text_columns;
    var pos = vec2(f32(pos_col) / f32(text_columns-1), f32(pos_row) / f32(text_rows-1));
    // calculate the uv at which this character starts in the font atlas
    var col: i32 = input.i_char % columns;
    var row: i32 = input.i_char / columns;
    var char_uv = vec2(f32(col) * char_scale.x, f32(row) * char_scale.y);
    // calculate the correct starting top-left position
    var start_position = vec2((input.position.x / f32(text_columns)) - 1.0, -(input.position.y / f32(text_rows)) + 1.0);
    // create the output we send to the fragment function
    var output: VertexOutput;
    output.pos = vec4(start_position + pos, 0.0, 1.0);
    output.uv = input.uv;
    output.char_uv = char_uv;
    return output;
}

// Group 1: Textures
@group(1) @binding(0)
var tex_sampler: sampler;
@group(1) @binding(1)
var font_atlas: texture_2d<f32>;

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    var font_uv = in.char_uv + in.uv * char_scale;
    var color = textureSample(font_atlas, tex_sampler, font_uv);
    return vec4<f32>(color);
}
