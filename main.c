#include "game.c"

#include <windows.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>

// todo: add DX12 which allows for more lightweight setup on windows + VRS for high resolution screens
// todo: add functions to remove meshes from the scene, and automatically remove pipelines/pipelines that have no meshes anymore (?)
/* GRAPHICS LAYER API */
// todo : platform provides these functions to presentation layer via a struct (then they don't need to be compiled together)
typedef void* GPUContext;
extern GPUContext createGPUContext(void *hInstance, void *hwnd, int width, int height);
extern int   createGPUPipeline(GPUContext context, const char *shader);
extern int   createGPUMesh(GPUContext context, int material_id, void *v, int vc, void *i, int ic, void *ii, int iic);
extern int   createGPUTexture(GPUContext context, int mesh_id, void *data, int w, int h);
int          addGPUGlobalUniform(GPUContext context, int pipeline_id, const void* data, int data_size);
void         setGPUGlobalUniformValue(GPUContext context, int pipeline_id, int offset, const void* data, int dataSize);
int          addGPUMaterialUniform(GPUContext context, int material_id, const void* data, int data_size);
void         setGPUMaterialUniformValue(GPUContext context, int material_id, int offset, const void* data, int dataSize);
extern void  setGPUInstanceBuffer(GPUContext context, int mesh_id, void* ii, int iic);
extern float drawGPUFrame(GPUContext context);
/* PLATFORM LAYER API */
// todo: pass these by struct to game.c
// memory map file / load file
// networking functions
// audio
// window
// event loop callback
// ...

// todo: add presentation layer that orchestrates the graphics layer (eg. webgpu.c)
// todo: ... automatically based on the game state + game state deltas that it gets from the domain layer
// todo: ... it also contains the UI

// todo: |platform|--> |presentation| --> |domain|
// todo:                              --> |graphics api|
// todo:                              --> |platform api|
// todo: platform initializes presentation layer, and provides it a struct with functions for platform-dependent things
// todo: presentation initializes domain, and provides it with eg. save file content
// BROWSER: rust bindgen main --> provide file/network/... functions
// ...                        --> C presentation wasm --> rust wgpu graphics
// ...                                             --> C domain wasm

static bool g_Running = true;


#pragma region FILE MAPPING
struct MappedMemory {
    void *data;     // Base pointer to mapped file data
    void *mapping;  // Opaque handle for the mapping (ex. Windows HANDLE)
};
struct MappedMemory map_file(const char *filename) {
    struct MappedMemory mm = {0};
    HANDLE hFile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed to open file: %s\n", filename);
        return mm;
    }
    HANDLE hMapping = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    CloseHandle(hFile); // File handle can be closed once mapping is created.
    if (!hMapping) {
        fprintf(stderr, "Failed to create mapping for: %s\n", filename);
        return mm;
    }
    void *base = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
    if (!base) {
        fprintf(stderr, "Failed to map view for: %s\n", filename);
        CloseHandle(hMapping);
        return mm;
    }
    mm.data = base;
    mm.mapping = (void*)hMapping;
    return mm;
}
void unmap_file(struct MappedMemory *mm) {
    if (mm->data) {
        UnmapViewOfFile(mm->data);
    }
    if (mm->mapping) {
        CloseHandle((HANDLE)mm->mapping);
    }
    mm->data = NULL;
    mm->mapping = NULL;
}
/* FILE MAPPING */
/* MEMORY MAPPING MESH */
typedef struct {
    unsigned int vertexCount;
    unsigned int indexCount;
    unsigned int vertexArrayOffset;
    unsigned int indexArrayOffset;
} MeshHeader;
struct MappedMemory load_mesh(const char *filename, void** v, int *vc, void** i, int *ic) {
    struct MappedMemory mm = map_file(filename);
    if (!mm.data) {
        fprintf(stderr, "Failed to load mesh: %s\n", filename);
        exit(1);
    }
    
    MeshHeader *header = (MeshHeader*)mm.data;
    // Set pointers into the mapped memory using the header's offsets
    *vc = header->vertexCount;
    *v = (unsigned char*)mm.data + header->vertexArrayOffset;
    *ic  = header->indexCount;
    *i  = (unsigned int*)((unsigned char*)mm.data + header->indexArrayOffset);
    
    return mm;
}
/* MEMORY MAPPING MESH */
/* MEMORY MAPPING TEXTURE */
typedef struct {
    int width;
    int height;
} ImageHeader;  
struct MappedMemory load_texture(const char *filename, int *out_width, int *out_height) {
    struct MappedMemory mm = map_file(filename);

    ImageHeader *header = (ImageHeader*)mm.data;
    *out_width  = header->width;
    *out_height = header->height;
    return mm;
}
/* MEMORY MAPPING TEXTURE */
#pragma endregion



/* CYCLE COUNT */
#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(__rdtsc)
inline unsigned long long read_cycle_count() {
    return __rdtsc();
}
#else
inline unsigned long long read_cycle_count() {
    unsigned int lo, hi;
    __asm__ __volatile__ (
        "rdtsc"
        : "=a"(lo), "=d"(hi)
    );
    return ((unsigned long long)hi << 32) | lo;
}
#endif
/* CYCLE COUNT */





#pragma region RAW INPUT SETUP
typedef BOOL (WINAPI *RegisterRawInputDevices_t)(PCRAWINPUTDEVICE, UINT, UINT);
typedef UINT (WINAPI *GetRawInputData_t)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);

RegisterRawInputDevices_t pRegisterRawInputDevices = NULL;
GetRawInputData_t pGetRawInputData = NULL;

// Load required functions dynamically
void load_raw_input_functions() {
    HMODULE hUser32 = LoadLibrary("user32.dll");
    if (!hUser32) {
        printf("Failed to load user32.dll\n");
        exit(1);
    }

    pRegisterRawInputDevices = (RegisterRawInputDevices_t)GetProcAddress(hUser32, "RegisterRawInputDevices");
    pGetRawInputData = (GetRawInputData_t)GetProcAddress(hUser32, "GetRawInputData");

    if (!pRegisterRawInputDevices || !pGetRawInputData) {
        printf("Failed to get function addresses from user32.dll\n");
        exit(1);
    }
}
#pragma endregion




#pragma region INPUT EVENTS
RAWINPUTDEVICE rid[2];
HRESULT InitializeRawInput()
{
    HRESULT hr = S_OK;
    // Keyboard
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x06; // Keyboard
    rid[0].dwFlags = 0;
    rid[0].hwndTarget = NULL;

    // Mouse
    rid[1].usUsagePage = 0x01;
    rid[1].usUsage = 0x02; // Mouse
    rid[1].dwFlags = 0;
    rid[1].hwndTarget = NULL;

    if (!pRegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE)))
    {
        MessageBox(NULL, "Failed to register raw input devices!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return E_FAIL;
    }
    return 0;
}
#define MAX_KEYS 256
bool keyStates[MAX_KEYS] = { false };  // Track pressed keys
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
        case WM_CREATE:
        {
            load_raw_input_functions(); // load windows dll to use raw input
            InitializeRawInput(); // setup for listening to windows raw input
        } break;
        case WM_INPUT:
        {
            UINT dwSize = 0;
            pGetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
            LPBYTE lpb = (LPBYTE)malloc(dwSize);
            if (lpb == NULL) break;
            if (pGetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) != dwSize)
                OutputDebugString(TEXT("GetRawInputData does not return correct size !\n"));
            RAWINPUT* raw = (RAWINPUT*)lpb;
            if (raw->header.dwType == RIM_TYPEKEYBOARD)
            {
                // Process keyboard input
                USHORT scanCode = raw->data.keyboard.MakeCode;
                USHORT flags = raw->data.keyboard.Flags;
                bool isPressed = !(flags & RI_KEY_BREAK);
                // Convert scan code to virtual key
                UINT virtualKey = raw->data.keyboard.VKey;
                // Handle key press/release
                // Translate scan code to virtual key if needed
                if (virtualKey == 0) virtualKey = MapVirtualKey(scanCode, 1);
                if (virtualKey < MAX_KEYS) {
                    keyStates[virtualKey] = isPressed;
                }
                if (virtualKey == VK_ESCAPE) {
                    g_Running = 0;
                    return 0;
                }

                // Print active keys; side up forward yaw pitch roll
                if (isPressed) {
                    char msg[32];
                    if (virtualKey == 'Z' || virtualKey == VK_UP) {
                        buttonState.forward = 1;
                    }
                    if (virtualKey == 'S' || virtualKey == VK_DOWN) {
                        buttonState.backward = 1;
                    }
                    if (virtualKey == 'Q' || virtualKey == VK_LEFT) {
                        buttonState.left = 1;
                    }
                    if (virtualKey == 'D' || virtualKey == VK_RIGHT) {
                        buttonState.right = 1;
                    }
                    if (virtualKey == ' ' || virtualKey == VK_SPACE) {
                        cameraSpeed.y = 2.5f;
                    }
                }
                else if (!isPressed) {
                    if (virtualKey == 'Z' || virtualKey == VK_UP) {
                        buttonState.forward = 0;
                    }
                    if (virtualKey == 'S' || virtualKey == VK_DOWN) {
                        buttonState.backward = 0;
                    }
                    if (virtualKey == 'Q' || virtualKey == VK_LEFT) {
                        buttonState.left = 0;
                    }
                    if (virtualKey == 'D' || virtualKey == VK_RIGHT) {
                        buttonState.right = 0;
                    }
                }
            }
            else if (raw->header.dwType == RIM_TYPEMOUSE)
            {
                // Process mouse input
                LONG dx = raw->data.mouse.lLastX;
                LONG dy = raw->data.mouse.lLastY;
                USHORT buttonFlags = raw->data.mouse.usButtonFlags;
                // Handle mouse movement and button clicks
                absolute_yaw(-dx * 0.002f, camera);
                absolute_pitch(-dy * 0.002f, camera);
            }
            free(lpb); // Free allocated memory
            break;
        }
        case WM_MOUSEMOVE: {
            // Reset cursor to center every frame
            RECT rect;
            GetClientRect(hWnd, &rect);
            POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
            ClientToScreen(hWnd, &center);
            SetCursorPos(center.x, center.y);
            break;
        }
        case WM_CLOSE:
            g_Running = false;
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}
#pragma endregion

void set_fullscreen(HWND hwnd, int width, int height, int refreshRate) {
    DEVMODE devMode = {0};
    devMode.dmSize = sizeof(devMode);
    devMode.dmPelsWidth = width;
    devMode.dmPelsHeight = height;
    devMode.dmBitsPerPel = 32;  // 32-bit color
    devMode.dmDisplayFrequency = refreshRate;
    devMode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;

    // Switch to fullscreen mode
    if (ChangeDisplaySettingsEx(NULL, &devMode, NULL, CDS_FULLSCREEN, NULL) != DISP_CHANGE_SUCCESSFUL) {
        MessageBox(hwnd, "Failed to switch to fullscreen!", "Error", MB_OK);
        return;
    }

    SetWindowLong(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, width, height, SWP_FRAMECHANGED);
}

struct debug_info {
    LARGE_INTEGER query_perf_result;
	long ticks_per_second;
    LARGE_INTEGER current_tick_count;
    long current_cycle_count;
    float ms_last_60_frames[60]; 
    int ms_index; 
    int avg_count; 
    float count;
    float ms_last_frame;
    float ms_waited_on_gpu;
};
static struct debug_info debug_info = {0};
void init_debug_info() {
	QueryPerformanceFrequency(&debug_info.query_perf_result);
	debug_info.ticks_per_second = debug_info.query_perf_result.QuadPart;
    QueryPerformanceCounter(&debug_info.current_tick_count);
    debug_info.current_cycle_count = read_cycle_count();
    memset(debug_info.ms_last_60_frames, 0, sizeof(debug_info.ms_last_60_frames));
    debug_info.ms_index = 0;
    debug_info.avg_count = 60; 
    debug_info.count = 0.0;
    debug_info.ms_last_frame = 0.0f;
    debug_info.ms_waited_on_gpu = 0.0f;
}
void draw_debug_info() {
    // todo: way to not have any of the debug HUD and fps and timing code at all when not in debug mode
    // todo: create a function that we can use as oneliner measuring timing here
    // todo: -> game loop time -> cpu to gpu commands time -> gpu time -> total time (and see if those three add up to total or not)
    // todo: isolate out the exact time we spend inside the loop iteration itself to see the actual CPU time
    // todo: use this cpu time to measure how much time we lose waiting on the GPU (?) ~eg. know if the GPU is the bottleneck or not
    // todo: isolate out the exact time from StartFrame to EndFrame to know how much CPU time gets spent on setting up gpu commands; draw calls etc.
    LARGE_INTEGER new_tick_count;
    QueryPerformanceCounter(&new_tick_count);
    long ticks_elapsed = new_tick_count.QuadPart - debug_info.current_tick_count.QuadPart;
    debug_info.current_tick_count = new_tick_count;

    long new_cycle_count = read_cycle_count();
    long cycles_elapsed = new_cycle_count - debug_info.current_cycle_count;
    int cycles_last_frame = (int) cycles_elapsed / 1000000; // million cycles per frame
    debug_info.current_cycle_count = new_cycle_count;

    debug_info.ms_last_frame = ((float) (1000*ticks_elapsed) / (float) debug_info.ticks_per_second);
    int fps = debug_info.ticks_per_second / ticks_elapsed; // calculate how many times we could do this amount of ticks (=1frame) in one second
    // todo: render in bitmap font to screen instead of printf IO
    char perf_output_string[256];
    snprintf(perf_output_string, sizeof(perf_output_string), "%4.2fms/f,  %df/s,  %4.2fgpu-ms/f\n", debug_info.ms_last_frame, fps, debug_info.ms_waited_on_gpu);
    print_on_screen(perf_output_string);
    debug_info.count += debug_info.ms_last_frame - debug_info.ms_last_60_frames[debug_info.ms_index];
    debug_info.ms_last_60_frames[debug_info.ms_index] = debug_info.ms_last_frame;
    debug_info.ms_index = (debug_info.ms_index + 1) % debug_info.avg_count;
    char perf_avg_string[256];
    snprintf(perf_avg_string, sizeof(perf_avg_string), "Average frame timing last %d frames: %4.2fms\n", debug_info.avg_count, debug_info.count / (float) debug_info.avg_count);
    print_on_screen(perf_avg_string);
}
static long ticks_per_second;
static LARGE_INTEGER tick_count_at_startup;
void print_time_since_startup(char *str) {
    LARGE_INTEGER new_tick_count;
    QueryPerformanceCounter(&new_tick_count);
    long ticks_elapsed = new_tick_count.QuadPart - tick_count_at_startup.QuadPart;
    tick_count_at_startup = new_tick_count;
    float ms_since_startup = ((float) (1000*ticks_elapsed) / (float) ticks_per_second);
    printf(str);
    printf("\nStartup time: -----------------------------------------------------------------------------------------> %4.2fms\n", ms_since_startup);
}

typedef HRESULT (WINAPI *SetProcessDpiAwareness_t)(int);
#define PROCESS_PER_MONITOR_DPI_AWARE 2
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    #pragma region WINDOWS-SPECIFIC SETUP
    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    // ----- TO CHECK STARTUP SPEED -----
    LARGE_INTEGER query_perf_result;
	QueryPerformanceFrequency(&query_perf_result);
	ticks_per_second = query_perf_result.QuadPart;
    QueryPerformanceCounter(&tick_count_at_startup);
    // ----------------------------------------

    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "WgpuWindowClass";
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindowEx(
        0, wc.lpszClassName, "Generic Uniform & Texture Demo",
        WS_POPUP | WS_VISIBLE,  // WS_POPUP makes it borderless
        CW_USEDEFAULT, CW_USEDEFAULT,
        WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL, hInstance, NULL
    );
    assert(hwnd);

    // *info* to be aware of DPI to avoid specified resolutions with lower granularity than actual screen resolution
    HMODULE shcore = LoadLibrary("Shcore.dll"); // import explicitly because tcc doesn't know these headers
    if (shcore) {
        SetProcessDpiAwareness_t SetProcessDpiAwareness = 
            (SetProcessDpiAwareness_t) GetProcAddress(shcore, "SetProcessDpiAwareness");

        if (SetProcessDpiAwareness) {
            SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
        }

        FreeLibrary(shcore);
    }
    // *info* this sets the window to exclusive fullscreen bypassing the window manager
    set_fullscreen(hwnd, WINDOW_WIDTH, WINDOW_HEIGHT, 60); // need to specify refresh rate of monitor (?)
    RECT screen;
    GetClientRect(GetDesktopWindow(), &screen);
    int x = (screen.right - WINDOW_WIDTH) / 2;
    int y = (screen.bottom - WINDOW_HEIGHT) / 2;
    SetWindowPos(hwnd, NULL, x, y, WINDOW_WIDTH, WINDOW_HEIGHT, SWP_NOZORDER | SWP_SHOWWINDOW); // put window in middle of screen
    ShowCursor(FALSE); // hide the cursor
    /* WINDOWS-ONLY SPECIFIC SETTINGS */
    #pragma endregion
    
    GPUContext context = createGPUContext(hInstance, hwnd, WINDOW_WIDTH, WINDOW_HEIGHT);

    // todo: lighting
    // todo: cubemap sky
    // todo: LOD: how to most efficiently swap out the mesh with a lower/higher res one? -> put instance in instance buffer of the LOD mesh instead
    // todo: character mesh
    // todo: animate the character mesh (skeleton?)
    
    // todo: use precompiled shader for faster loading
    // todo: use glsl instead of wgsl for C-style syntax
    
    // CREATE MATERIALS
    // todo: create a function that does this automatically based on game state object
    
    int basic_material_id = createGPUPipeline(context, "data/shaders/shader.wgsl");
    int hud_material_id = createGPUPipeline(context, "data/shaders/hud.wgsl");
    // CREATE MATERIALS

    // LOAD MESHES FROM DISK
    int vc, ic; void *v, *i;
    
    struct MappedMemory teapot_mm = load_mesh("data/models/bin/teapot.bin", &v, &vc, &i, &ic);
    float identity_matrix[16] = {
        1, 0, 0, 0,
        0, -1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    struct Instance ii[2] = {0};
    for (int i = 0; i < 16; i++) {
        ii[0].transform[i] = identity_matrix[i];
        ii[1].transform[i] = identity_matrix[i];
    }
    int teapot_mesh_id = createGPUMesh(context, basic_material_id, v, vc, i, ic, &ii, 2);
    unmap_file(&teapot_mm);

    struct MappedMemory cube_mm = load_mesh("data/models/bin/cube.bin", &v, &vc, &i, &ic);
    struct Instance cube = {0};
    for (int i = 0; i < 16; i++) {
        cube.transform[i] = identity_matrix[i];
    }
    int cube_mesh_id = createGPUMesh(context, basic_material_id, v, vc, i, ic, &ii, 1);
    unmap_file(&cube_mm);
    // LOAD MESHES FROM DISK

    // PREDEFINED MESHES
    int ground_mesh_id = createGPUMesh(context, basic_material_id, &ground_verts, 4, &ground_indices, 6, &ground_instance, 1);
    int quad_mesh_id = createGPUMesh(context, hud_material_id, &quad_vertices, 4, &quad_indices, 6, &char_instances, MAX_CHAR_ON_SCREEN);
    // PREDEFINED MESHES

    // TEXTURE
    int w, h = 0;
    struct MappedMemory font_texture_mm = load_texture("data/textures/bin/font_atlas_small.bin", &w, &h);
    int cube_texture_id = createGPUTexture(context, cube_mesh_id, font_texture_mm.data, w, h);
    int quad_texture_id = createGPUTexture(context, quad_mesh_id, font_texture_mm.data, w, h);
    unmap_file(&font_texture_mm);
    struct MappedMemory crabby_mm = load_texture("data/textures/bin/texture_2.bin", &w, &h);
    int ground_texture_id = createGPUTexture(context, ground_mesh_id, crabby_mm.data, w, h);
    unmap_file(&crabby_mm);
    // TEXTURE



    float view[16] = {
        1.0 / (tan(fov / 2.0) * ASPECT_RATIO), 0.0f,  0.0f,                               0.0f,
        0.0f,  1.0 / tan(fov / 2.0),          0.0f,                               0.0f,
        0.0f,  0.0f, -(farClip + nearClip) / (farClip - nearClip), -(2 * farClip * nearClip) / (farClip - nearClip),
        0.0f,  0.0f, -1.0f,                               0.0f
    };
    int brightnessOffset = addGPUGlobalUniform(context, basic_material_id, &brightness, sizeof(float));
    int timeOffset = addGPUGlobalUniform(context, basic_material_id, &timeVal, sizeof(float));
    int cameraOffset = addGPUGlobalUniform(context, basic_material_id, camera, sizeof(camera));
    int viewOffset = addGPUGlobalUniform(context, basic_material_id, view, sizeof(view));

    /* MAIN LOOP */
    init_debug_info();
    while (g_Running) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_Running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!g_Running) break;

        // Update uniforms
        timeVal += 0.016f; // pretend 16ms per frame
        //yaw(0.001f * ms_last_frame, camera);
        cameraMovement(camera, movementSpeed, debug_info.ms_last_frame);
        float cameraLocation[3] = {camera[3], camera[7], camera[11]};
        applyGravity(&cameraSpeed, cameraLocation, debug_info.ms_last_frame);
        //collisionDetectionCamera(cubeCollisionBox);
        // struct Vector3 separation = detectCollision(cameraCollisionBox, cubeCollisionBox);
        //printf("Collision detected: %4.2f\n", separation.x);
        //camera[7] = cameraLocation[1];
        float inv[16];
        inverseViewMatrix(camera, inv);
        setGPUGlobalUniformValue(context, basic_material_id, timeOffset, &timeVal, sizeof(float));
        setGPUGlobalUniformValue(context, basic_material_id, cameraOffset, &inv, sizeof(camera));

        // update the instances of the text
        setGPUInstanceBuffer(context, quad_mesh_id, &char_instances, screen_chars_index);

        // Actual frame rendering
        // *info* without vsync/fifo the cpu can keep pushing new frames without waiting, until the queue is full and backpressure
        // *info* forces the cpu to wait before pushing another frame, bringing the cpu speed down to the gpu speed
        // *info* we can force the cpu to wait regardless by using the fence in wgpuEndFrame()
        debug_info.ms_waited_on_gpu = drawGPUFrame(context);
        
        // todo: create a central place for things that need to happen to initialize every frame iteration correctly
        // Set the printed chars to 0 to reset the text in the HUD
        {
            screen_chars_index = 0;
            current_screen_char = 0; // todo: replace with function that resets instead of spaghetti
            memset(char_instances, 0, sizeof(char_instances));
            setGPUInstanceBuffer(context, quad_mesh_id, &char_instances, screen_chars_index);
        }

        draw_debug_info();
        // if (fabs(cameraSpeed.x) > 1.0f || fabs(cameraSpeed.y) > 1.0f || fabs(cameraSpeed.z) > 1.0f) {
        //     printf("Camera speed: %4.2f %4.2f %4.2f\n", cameraSpeed.x, cameraSpeed.y, cameraSpeed.z);
        // }
    }
    
    return 0;
}