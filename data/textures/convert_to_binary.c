/*
    main.c

    This program recursively scans the current folder and all subfolders for PNG files.
    For each PNG, it loads the image as RGBA (forcing 4 channels using stb_image),
    then writes a binary file with a header (storing width and height) followed by the raw
    RGBA pixel data. The output files are saved in a folder called "bin" (created if needed),
    and any existing files are overwritten.

    This example uses both stb_image and stb_image_write (the latter is included as per request,
    even though it isn’t used in this code).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <direct.h>  // for _mkdir

// Include stb_image and stb_image_write implementations.
#define STBI_NO_SIMD  // Disable SSE/AVX intrinsics (doesn't work with TCC)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// Define the header to be written at the start of each .bin file.
typedef struct {
    int width;
    int height;
} ImageHeader;

/*
    process_image
    -------------
    Loads a PNG image from 'full_path' (forcing conversion to RGBA),
    then writes a binary file in the "bin" folder. The binary file begins
    with an ImageHeader (width and height), followed by the raw pixel data.
*/
void process_image(const char *full_path, const char *file_name) {
    int width, height, channels;
    // Force loading as 4 channels (RGBA). If the PNG lacks an alpha channel, 255 is used.
    unsigned char *data = stbi_load(full_path, &width, &height, &channels, 4);
    if (!data) {
        printf("Failed to load image: %s\n", full_path);
        return;
    }
    
    // Build the output file name.
    // Strip the extension from the original file name and replace it with .bin.
    char base_name[256];
    strncpy(base_name, file_name, sizeof(base_name)-1);
    base_name[sizeof(base_name)-1] = '\0';
    char *dot = strrchr(base_name, '.');
    if (dot) {
        *dot = '\0'; // Remove extension.
    }
    
    char out_filename[512];
    snprintf(out_filename, sizeof(out_filename), "bin\\%s.bin", base_name);
    
    FILE *fp = fopen(out_filename, "wb");
    if (!fp) {
        printf("Failed to open output file: %s\n", out_filename);
        stbi_image_free(data);
        return;
    }
    
    // Write the header (width and height).
    ImageHeader header;
    header.width = width;
    header.height = height;
    if (fwrite(&header, sizeof(ImageHeader), 1, fp) != 1) {
        printf("Failed to write header to file: %s\n", out_filename);
        fclose(fp);
        stbi_image_free(data);
        return;
    }
    
    // Write the raw RGBA data.
    size_t data_size = (size_t) width * height * 4;
    if (fwrite(data, sizeof(unsigned char), data_size, fp) != data_size) {
        printf("Failed to write image data to file: %s\n", out_filename);
    }
    
    fclose(fp);
    stbi_image_free(data);
    
    printf("Processed: %s -> %s (Width: %d, Height: %d)\n", full_path, out_filename, width, height);
}

/*
    scan_directory
    --------------
    Recursively scans the directory specified by 'directory'. For every file with a
    ".png" extension (case-insensitive), it calls process_image().
    
    This function uses the Windows API (FindFirstFile/FindNextFile) for directory traversal.
*/
void scan_directory(const char *directory) {
    char search_path[512];
    snprintf(search_path, sizeof(search_path), "%s\\*", directory);
    
    WIN32_FIND_DATA find_data;
    HANDLE hFind = FindFirstFile(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }
    
    do {
        // Skip the special directories "." and ".."
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0)
            continue;
        
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s\\%s", directory, find_data.cFileName);
        
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Recursively scan subdirectories.
            scan_directory(full_path);
        } else {
            // Check if the file extension is ".png" (case-insensitive).
            const char *ext = strrchr(find_data.cFileName, '.');
            if (ext && (_stricmp(ext, ".png") == 0)) {
                process_image(full_path, find_data.cFileName);
            }
        }
    } while (FindNextFile(hFind, &find_data));
    
    FindClose(hFind);
}

int main(void) {
    // Create the "bin" folder if it does not already exist.
    if (_mkdir("bin") != 0) {
        // You can check errno here; if the folder already exists, it's acceptable.
    }
    
    // Begin scanning from the current directory.
    scan_directory(".");
    
    return 0;
}
