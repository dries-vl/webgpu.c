#ifndef __EMSCRIPTEN__
#define __EMSCRIPTEN__
#endif
#include <emscripten.h>
#include <emscripten/html5.h>

#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "../present.c"

double current_time_ms_web(void) {
    // emscripten_get_now returns the time in milliseconds since the page loaded.
    return emscripten_get_now();
}

void web_blocking_sleep(double ms) {
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000.0);
    ts.tv_nsec = (long)((ms - ts.tv_sec * 1000.0) * 1000000.0);
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
        ; // repeat if interrupted
}

#pragma region FILE_MAPPING
static struct MappedMemory web_map_file(const char *filename) {
    struct MappedMemory mm = {0};
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open file: %s\n", filename);
        return mm;
    }
    struct stat st;
    if (stat(filename, &st) != 0) {
        fclose(f);
        return mm;
    }
    size_t filesize = st.st_size;
    mm.data = malloc(filesize);
    if (!mm.data) {
        fprintf(stderr, "Failed to allocate memory for file: %s\n", filename);
        fclose(f);
        return mm;
    }
    size_t read = fread(mm.data, 1, filesize, f);
    fclose(f);
    if (read != filesize) {
        fprintf(stderr, "Failed to read entire file: %s\n", filename);
        free(mm.data);
        mm.data = NULL;
        return mm;
    }
    mm.mapping = NULL;  // No mapping handle needed on the web.
    return mm;
}

// Unmap file: simply free the allocated buffer.
static void web_unmap_file(struct MappedMemory *mm) {
    if (mm->data) {
        free(mm->data);
        mm->data = NULL;
    }
    mm->mapping = NULL;
}
#pragma endregion

// Global flag to signal when to stop the main loop.
static bool g_Running = true;

// Global pointer for your GPU context (set up by your createGPUContext).
// In Windows this came from CreateWindow and related APIs. For Emscripten, you
// might create your GPU context using the canvas element.
void *g_Context = NULL;
struct Platform g_Platform = {0};

#pragma region INPUTS
EM_BOOL key_callback(int eventType, const EmscriptenKeyboardEvent *e, void *userData) {
    if (eventType == EMSCRIPTEN_EVENT_KEYDOWN || eventType == EMSCRIPTEN_EVENT_KEYUP) {
        bool isPressed = (eventType == EMSCRIPTEN_EVENT_KEYDOWN);
        const char *key = e->key; // e.g. "w", "ArrowUp", "Escape"

        if (strcmp(key, "Escape") == 0) g_Running = false;

        if (strcmp(key, "w") == 0 || strcmp(key, "ArrowUp") == 0) buttonState.forward = isPressed;
        if (strcmp(key, "s") == 0 || strcmp(key, "ArrowDown") == 0) buttonState.backward = isPressed;
        if (strcmp(key, "a") == 0 || strcmp(key, "ArrowLeft") == 0) buttonState.left = isPressed;
        if (strcmp(key, "d") == 0 || strcmp(key, "ArrowRight") == 0) buttonState.right = isPressed;
        if (strcmp(key, " ") == 0 && isPressed) gameState.player.velocity.y = 0.01f;
        if (strcmp(key, "Tab") == 0 && isPressed) SHOW_CURSOR ^= 1;

        // prevent default browser behavior like scrolling
        return 1;
    }
    return 0;
}
EM_BOOL mouse_move_callback(int eventType, const EmscriptenMouseEvent *e, void *userData) {
    if (!SHOW_CURSOR) {
        float dx = e->movementX * 0.002f;
        float dy = e->movementY * 0.002f;
        absolute_yaw(dx, view);
        absolute_pitch(dy, view);
    }
    return 1;
}
EM_BOOL mouse_button_callback(int eventType, const EmscriptenMouseEvent *e, void *userData) {
    // Use e->button (0 = left, 1 = middle, 2 = right)
    // You can track pressed state or trigger immediate actions
    return 1;
}
// In web version we can't control the inputs like in native, we can only provide the browser with callbacks
void poll_inputs_web() {
    return;
}
#pragma endregion

// Wrapper function called repeatedly by the browser's event loop.
void main_called_by_browser(void) {
    tick(&g_Platform, g_Context);
    if (!g_Running) {
        emscripten_cancel_main_loop();
    }
}
// Function we pass to the webgpu setup to start when the setup is done (only for emscripten, not in native)
void start_function(void) {

    if (!g_Context) {
        fprintf(stderr, "Failed to create GPU context!\n");
    }
    
    // Set up the Platform struct with our web implementations.
    g_Platform = (struct Platform) {
         .current_time_ms = current_time_ms_web,
         .map_file       = web_map_file,
         .unmap_file     = web_unmap_file,
         .sleep_ms       = web_blocking_sleep,
         .poll_inputs    = poll_inputs_web
    };
   
    // Now set up the main loop. This loop will repeatedly call main_called_by_browser()
    // which in turn calls your tick() and draw_debug_info() functions (and therefore print_on_screen).
    // todo: figure out, this might or might not run in a separate loop
    emscripten_set_main_loop(main_called_by_browser, 0, 1);
}
int main(void) {
    // Set the canvas element size if needed.
    emscripten_set_canvas_element_size("#canvas", 800, 600);
    WINDOW_WIDTH = 800;
    WINDOW_HEIGHT = 600;
    VIEWPORT_WIDTH = 800;
    VIEWPORT_HEIGHT = 600;
 
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, 0, 1, key_callback);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, 0, 1, key_callback);
    emscripten_set_mousemove_callback("#canvas", 0, 1, mouse_move_callback);
    emscripten_set_mousedown_callback("#canvas", 0, 1, mouse_button_callback);
    emscripten_set_mouseup_callback("#canvas", 0, 1, mouse_button_callback);
 
    // setup the weggpu context
    g_Context = createGPUContext(start_function, 800, 600, 800, 600);
    return 0;
}