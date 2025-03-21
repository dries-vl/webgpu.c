#include <stdint.h>
#include <string.h>
#include <limits.h>

// Helper macro to convert a float UV (assumed to be in the range [-1,1] or [0,1])
// to an unsigned short (mapping -1 => 0 and 1 => 65535).
#define FLOAT_TO_U16(x) ((uint16_t) x * 65535.0f)

//---------------------------------------------------------------------
// Predefined Mesh Data
//---------------------------------------------------------------------
// Ground instance (for the entire ground mesh)
// Here we use an identity transform (no translation, rotation or scale change)
static struct Instance ground_instance = {
    .transform = {
         100, 0, 0, 0,
         0, 0, 100, 0,
         0, 100, 0, 0,
         0, 0, 0, 1
    },
    .data = {20, 0, 0},
    .norms = {0},
    .animation = 0,
    .animation_phase = 0.0f,
    .atlas_uv = {0}
};

struct Instance pine = {
    .transform = {
        5.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 5.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 5.0f, 0.0f,
        0.0f, 0.0f, 10.0f, 1.0f
    },
    .data = {0},
    .norms = {0},
    .animation = 0,
    .animation_phase = 0.0f,
    .atlas_uv = {0}
};
struct Instance pines[10];

// HUD quad (2D UI element)
static struct Vertex quad_vertices[4] = {
    {
        .data = {0},
        .position = {-0.5f, 0.5f, 0.0f},
        .normal = {0, 0, 255, 0},
        .tangent = {0},
        .uv = {FLOAT_TO_U16(0.0f), FLOAT_TO_U16(0.0f)},
        .bone_weights = {UCHAR_MAX, 0, 0, 0},
        .bone_indices = {0}
    },
    {
        .data = {0},
        .position = {0.5f, 0.5f, 0.0f},
        .normal = {0, 0, 255, 0},
        .tangent = {0},
        .uv = {FLOAT_TO_U16(1.0f), FLOAT_TO_U16(0.0f)},
        .bone_weights = {UCHAR_MAX, 0, 0, 0},
        .bone_indices = {0}
    },
    {
        .data = {0},
        .position = {-0.5f, -0.5f, 0.0f},
        .normal = {0, 0, 255, 0},
        .tangent = {0},
        .uv = {FLOAT_TO_U16(0.0f), FLOAT_TO_U16(1.0f)},
        .bone_weights = {UCHAR_MAX, 0, 0, 0},
        .bone_indices = {0}
    },
    {
        .data = {0},
        .position = {0.5f, -0.5f, 0.0f},
        .normal = {0, 0, 255, 0},
        .tangent = {0},
        .uv = {FLOAT_TO_U16(1.0f), FLOAT_TO_U16(1.0f)},
        .bone_weights = {UCHAR_MAX, 0, 0, 0},
        .bone_indices = {0}
    }
};
static uint32_t quad_indices[6] = {0, 2, 1, 2, 3, 1};

#define CHAR_COLUMNS 48 // columns of text that fit across half of the screen
#define CHAR_ROWS 24 // rows that fit across half of the screen
#define MAX_CHAR_ON_SCREEN (CHAR_COLUMNS * CHAR_ROWS)  // 2304 max characters
// screen coordinates goes from -1 to 1, so we need to account for this in some places
#define CHAR_HALF_COLUMNS (CHAR_COLUMNS/2)
#define CHAR_HALF_ROWS (CHAR_ROWS/2)
#define CHAR_WIDTH  (1.0 / CHAR_HALF_COLUMNS)  // width of one character
#define CHAR_HEIGHT (1.0 / CHAR_HALF_ROWS)  // height of one character

static struct Instance char_instances[MAX_CHAR_ON_SCREEN] = {0};
int screen_chars_index = 0;
int current_screen_char = 0;
void print_on_screen(const char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        // Prevent buffer overflow
        if (current_screen_char >= MAX_CHAR_ON_SCREEN)
            // todo: also break based on current_screen_char to account for skipped chars
            break;
        
        // Handle newline: jump to the beginning of the next row
        if (str[i] == '\n') {
            current_screen_char = ((current_screen_char / CHAR_COLUMNS) + 1) * CHAR_COLUMNS;
            continue;
        }
        
        // x and y on the 48 x 24 grid of possible characters on screen
        int col = current_screen_char % CHAR_COLUMNS;
        int row = current_screen_char / CHAR_COLUMNS;
        
        // convert to an actual position from 0 to 1
        float x = (col / (((float) CHAR_HALF_COLUMNS * 1.1)) / ASPECT_RATIO); // times 1.1 for slight kerning fix
        float y = row / (float) CHAR_HALF_ROWS;
        x -= 0.95; // x starts at -1 in topleft corner
        y = 0.95 - y; // y starts at 1 in topleft corner

        // Build a transform matrix that scales the quad and translates it
        // (Column-major order: first 4 floats = first column, etc.)
        float transform[16] = {
            // Column 0: scale X
            CHAR_WIDTH/ASPECT_RATIO, 0.0f, 0.0f, 0.0f,
            // Column 1: scale Y
            0.0f, CHAR_HEIGHT, 0.0f, 0.0f,
            // Column 2: Z remains unchanged
            0.0f, 0.0f, 1.0f, 0.0f,
            // Column 3: translation (x, y, 0) with homogeneous coordinate 1
            x,    y,    0.0f, 1.0f
        };
        
        // Create a new Instance for this character
        struct Instance *inst = &char_instances[screen_chars_index];
        for (int j = 0; j < 16; j++) {
            inst->transform[j] = transform[j];
        }
        
        // Map the ASCII code to an atlas cell in a 16x8 grid.
        // For example, 'A' (ASCII 65) will go to cell (65 % 16, 65 / 16)
        unsigned char ascii = str[i];
        unsigned char atlas_col = ascii % 16;
        unsigned char atlas_row = ascii / 16;
        inst->atlas_uv[0] = ((uint16_t) atlas_col) * 4096 + (4096 / 8); // todo: why do we have to add an eight (?)
        inst->atlas_uv[1] = ((uint16_t) atlas_row) * 8192;
        
        // (Other fields like data, norms, animation, etc. remain zero for now)

        screen_chars_index++;
        current_screen_char++;
    }
}
