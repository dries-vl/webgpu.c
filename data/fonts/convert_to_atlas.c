/*
    Modified version of the font-to-BMP atlas generator for high resolution monospaced glyphs,
    with a proportional horizontal compression to bring glyphs even closer together.
    
    For every .ttf file in the current folder and its subfolders, this program:
      - Loads the font via stb_truetype.
      - Renders the ASCII codepoints into a texture atlas using a desired text height of 64 pixels.
      - Uses a cell width that is a fixed fraction (CELL_WIDTH_FACTOR) of the computed advance width,
        so that the glyphs are rendered closer together.
      - Writes out the resulting atlas as a PNG file into the "atlas" folder.
      
    Compile with (for example):
         tcc -run font2png_atlas_monospaced.c

    Make sure "stb_truetype.h" and "stb_image_write.h" are in the same folder.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <direct.h>
#include <windows.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// --- settings ---
#define DESIRED_TEXT_HEIGHT 64.0f  // Render glyphs at ~64 pixels tall.
#define PADDING 0                  // No extra padding for a tight monospaced atlas.
// Instead of a fixed subtraction, use a scaling factor for the cell width.
#define CELL_WIDTH_FACTOR 0.75f

// Forward declarations:
void process_font_file(const char *ttf_path);
void process_directory(const char *dir);

int main(void)
{
    // Create the output directory "atlas". (If it exists already, that's fine.)
    if (_mkdir("atlas") != 0) {
        DWORD attr = GetFileAttributes("atlas");
        if(attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            fprintf(stderr, "Error: Could not create or access atlas directory.\n");
            return 1;
        }
    }

    // Recursively process the current directory.
    process_directory(".");
    return 0;
}

// Recursively search for .ttf files in directory 'dir'
void process_directory(const char *dir)
{
    char search_path[MAX_PATH];
    snprintf(search_path, MAX_PATH, "%s\\*", dir);
    
    WIN32_FIND_DATA findData;
    HANDLE hFind = FindFirstFile(search_path, &findData);
    if (hFind == INVALID_HANDLE_VALUE)
        return;
    
    do {
        if (strcmp(findData.cFileName, ".") == 0 ||
            strcmp(findData.cFileName, "..") == 0)
            continue;
            
        char path[MAX_PATH];
        snprintf(path, MAX_PATH, "%s\\%s", dir, findData.cFileName);
        
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            process_directory(path);
        } else {
            // Check for .ttf extension (case-insensitive)
            char *ext = strrchr(findData.cFileName, '.');
            if (ext && _stricmp(ext, ".ttf") == 0) {
                process_font_file(path);
            }
        }
    } while (FindNextFile(hFind, &findData));
    
    FindClose(hFind);
}

// Process one .ttf file: load it, render a monospaced atlas, and write out a PNG.
void process_font_file(const char *ttf_path)
{
    // --- Load the entire TTF file into memory ---
    FILE *fontFile = fopen(ttf_path, "rb");
    if (!fontFile) {
        fprintf(stderr, "Error: Could not open font file %s\n", ttf_path);
        return;
    }
    fseek(fontFile, 0, SEEK_END);
    long fontSize = ftell(fontFile);
    fseek(fontFile, 0, SEEK_SET);
    unsigned char *ttfBuffer = malloc(fontSize);
    if (!ttfBuffer) {
        fprintf(stderr, "Error: Could not allocate memory for font file %s\n", ttf_path);
        fclose(fontFile);
        return;
    }
    fread(ttfBuffer, 1, fontSize, fontFile);
    fclose(fontFile);

    // --- Initialize the font ---
    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, ttfBuffer, 0)) {
        fprintf(stderr, "Error: Could not initialize font from %s\n", ttf_path);
        free(ttfBuffer);
        return;
    }

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    // Compute the scale so that (ascent - descent) maps to DESIRED_TEXT_HEIGHT pixels.
    float scale = DESIRED_TEXT_HEIGHT / (float)(ascent - descent);

    // --- Scan the ASCII range to compute a global bounding box and the maximum advance ---
    int global_min_x0 =  10000;
    int global_min_y0 =  10000;
    int global_max_x1 = -10000;
    int global_max_y1 = -10000;
    float max_advance = 0.0f;
    for (int i = 0; i < 128; i++) {
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&font, i, scale, scale, &x0, &y0, &x1, &y1);
        if (x0 < global_min_x0) global_min_x0 = x0;
        if (y0 < global_min_y0) global_min_y0 = y0;
        if (x1 > global_max_x1) global_max_x1 = x1;
        if (y1 > global_max_y1) global_max_y1 = y1;
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font, i, &advance, &lsb);
        float adv_px = advance * scale;
        if (adv_px > max_advance)
            max_advance = adv_px;
    }
    
    // --- Determine a fixed cell size ---
    // For a monospaced atlas, we use a compressed version of the advance width.
    int adv_w = (int)ceil(max_advance);
    int cell_w = (int)ceil(adv_w * CELL_WIDTH_FACTOR);
    // Ensure that cell_w is not less than the widest glyph's drawn width.
    int min_cell_w = (global_max_x1 - global_min_x0);
    if (cell_w < min_cell_w)
        cell_w = min_cell_w;
    int cell_h = (int)ceil(DESIRED_TEXT_HEIGHT) + 2 * PADDING;

    // --- Choose a fixed pen position inside each cell ---
    // Place every glyph so that its origin (0,0) is drawn at the same position.
    int fixed_pen_x = -global_min_x0 + PADDING;
    int fixed_baseline_y = -global_min_y0 + PADDING;

    // --- Set up an atlas grid (16 columns, 8 rows for 128 glyphs) ---
    int columns = 16;
    int rows = (128 + columns - 1) / columns;
    int atlas_w = columns * cell_w;
    int atlas_h = rows * cell_h;
    
    // Allocate a grayscale image buffer for the atlas.
    unsigned char *img = calloc(atlas_w * atlas_h, 1);
    if (!img) {
        fprintf(stderr, "Error: Could not allocate memory for atlas image for %s\n", ttf_path);
        free(ttfBuffer);
        return;
    }

    // --- Render each ASCII glyph into its proper cell ---
    for (int i = 0; i < 128; i++) {
        int col = i % columns;
        int row = i / columns;
        int cell_x = col * cell_w;
        int cell_y = row * cell_h;

        int w, h, xoff, yoff;
        unsigned char *glyph_bitmap = stbtt_GetCodepointBitmap(&font, scale, scale, i, &w, &h, &xoff, &yoff);
        if (glyph_bitmap) {
            // Position the glyph so that its origin appears at (fixed_pen_x, fixed_baseline_y) in the cell.
            int dest_x = cell_x + fixed_pen_x + xoff;
            int dest_y = cell_y + fixed_baseline_y + yoff;
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    int dx = dest_x + x;
                    int dy = dest_y + y;
                    if (dx >= cell_x && dx < cell_x + cell_w &&
                        dy >= cell_y && dy < cell_y + cell_h &&
                        dx >= 0 && dx < atlas_w &&
                        dy >= 0 && dy < atlas_h)
                    {
                        img[dy * atlas_w + dx] = glyph_bitmap[y * w + x];
                    }
                }
            }
            stbtt_FreeBitmap(glyph_bitmap, NULL);
        }
    }

    // --- Create the output PNG file path ---
    const char *base = strrchr(ttf_path, '\\');
    if (!base)
        base = ttf_path;
    else
        base++; // skip the backslash
    char base_no_ext[MAX_PATH];
    strncpy(base_no_ext, base, MAX_PATH);
    char *dot = strrchr(base_no_ext, '.');
    if (dot)
        *dot = '\0';  // remove extension

    char out_path[MAX_PATH];
    snprintf(out_path, MAX_PATH, "atlas\\%s.png", base_no_ext);

    // --- Convert the grayscale atlas to 24-bit RGB for PNG output ---
    unsigned char *png_data = malloc(atlas_w * atlas_h * 3);
    if (!png_data) {
        fprintf(stderr, "Error: Could not allocate memory for PNG data for %s\n", ttf_path);
        free(img);
        free(ttfBuffer);
        return;
    }
    for (int y = 0; y < atlas_h; y++) {
        for (int x = 0; x < atlas_w; x++) {
            unsigned char pixel = img[y * atlas_w + x];
            int idx = (y * atlas_w + x) * 3;
            png_data[idx + 0] = pixel;  // Red
            png_data[idx + 1] = pixel;  // Green
            png_data[idx + 2] = pixel;  // Blue
        }
    }

    // --- Write the image buffer as a PNG file ---
    if (!stbi_write_png(out_path, atlas_w, atlas_h, 3, png_data, atlas_w * 3)) {
        fprintf(stderr, "Error: Could not write PNG file %s\n", out_path);
    } else {
        printf("Processed font: %s -> %s\n", ttf_path, out_path);
    }

    free(png_data);
    free(img);
    free(ttfBuffer);
}
