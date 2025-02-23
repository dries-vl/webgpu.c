#include "game.c"
#include "webgpu.c"

#include <windows.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>

// todo: add DX12 which allows for more lightweight setup on windows + VRS for high resolution screens
// todo: add functions to remove meshes from the scene, and automatically remove materials/pipelines that have no meshes anymore (?)
extern void  wgpuInit(HINSTANCE hInstance, HWND hwnd, int width, int height);
extern int   wgpuCreateMaterial(struct Material *material);
extern int   wgpuCreateMesh(int materialID, struct Mesh *mesh);
extern int   wgpuAddTexture(int mesh_id, const char* texturePath);
extern float wgpuDrawFrame(void);

static bool g_Running = true;

void set_instances(struct Mesh *mesh, struct Instance *instances, int instanceCount) {
    free(mesh->instances);
    mesh->instances = instances;
    mesh->instanceCount = instanceCount;
}

typedef struct {
    unsigned int vertexCount;
    unsigned int indexCount;
    unsigned int vertexArrayOffset;
    unsigned int indexArrayOffset;
} MeshHeader;
struct Mesh read_mesh_binary(const char *binFilename) {
    FILE *file = fopen(binFilename, "rb");
    if (!file) {
        fprintf(stderr, "Failed to open %s for reading\n", binFilename);
        exit(1);
    }
    MeshHeader header;
    fread(&header, sizeof(MeshHeader), 1, file);
    struct Vertex *vertices_in = malloc(header.vertexCount * sizeof(struct Vertex));
    if (!vertices_in) {
        fprintf(stderr, "Memory allocation failed for vertices_in\n");
        exit(1);
    }
    unsigned int *indices_in = malloc(header.indexCount * sizeof(unsigned int));
    if (!indices_in) {
        fprintf(stderr, "Memory allocation failed for indices_in\n");
        exit(1);
    }
    fseek(file, header.vertexArrayOffset, SEEK_SET);
    fread(vertices_in, sizeof(struct Vertex), header.vertexCount, file);
    fseek(file, header.indexArrayOffset, SEEK_SET);
    fread(indices_in, sizeof(unsigned int), header.indexCount, file);
    fclose(file);
    size_t fileSize = header.indexArrayOffset + header.indexCount * sizeof(unsigned int);
    double fileSizeKB = fileSize / 1024.0;
    printf("Size of struct Vertex: %zu bytes\n", sizeof(struct Vertex));
    printf("Unique vertex count: %u\n", header.vertexCount);
    printf("Index count: %u\n", header.indexCount);
    printf("Expected binary file size: %.2f KB\n", fileSizeKB);
    struct Instance *instances_in = malloc(sizeof(struct Instance) * 1);
    struct Instance singleInstance = {0};
    *instances_in = singleInstance;
    return (struct Mesh) {
        .vertices=vertices_in, .indices=indices_in, .instances = instances_in,
        .vertexCount=header.vertexCount, .indexCount=header.indexCount, .instanceCount = 1};
}



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





/* RAW INPUT SETUP */
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
/* RAW INPUT SETUP */





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
    // set_fullscreen(hwnd, WINDOW_WIDTH, WINDOW_HEIGHT, 60); // need to specify refresh rate of monitor (?)
    RECT screen;
    GetClientRect(GetDesktopWindow(), &screen);
    int x = (screen.right - WINDOW_WIDTH) / 2;
    int y = (screen.bottom - WINDOW_HEIGHT) / 2;
    SetWindowPos(hwnd, NULL, x, y, WINDOW_WIDTH, WINDOW_HEIGHT, SWP_NOZORDER | SWP_SHOWWINDOW); // put window in middle of screen
    ShowCursor(FALSE); // hide the cursor
    /* WINDOWS-ONLY SPECIFIC SETTINGS */
    
    wgpuInit(hInstance, hwnd, WINDOW_WIDTH, WINDOW_HEIGHT);

    // todo: lighting
    // todo: cubemap sky
    // todo: LOD: how to most efficiently swap out the mesh with a lower/higher res one? swap instances with other material (?)
    // todo: character mesh
    // todo: animate the character mesh (skeleton?)
    
    // todo: use precompiled shader for faster loading
    // todo: use glsl instead of wgsl for C-style syntax
    
    // Add a projection matrix (a 4x4 matrix).  
    float view[16] = {
    1.0 / (tan(fov / 2.0) * aspect_ratio), 0.0f,  0.0f,                               0.0f,
    0.0f,  1.0 / tan(fov / 2.0),          0.0f,                               0.0f,
    0.0f,  0.0f, -(farClip + nearClip) / (farClip - nearClip), -(2 * farClip * nearClip) / (farClip - nearClip),
    0.0f,  0.0f, -1.0f,                               0.0f
    };
    
    // load teapot mesh
    struct Mesh teapot_mesh = read_mesh_binary("data/models/bin/teapot.bin");
    struct Instance instance1 = {0.0f, 0.0f, 0.0f};
    struct Instance instance2 = {0.0f, 80.0f, 0.0f};
    struct Instance instances[2] = {instance1, instance2};
    set_instances(&teapot_mesh, instances, 2);
    
    // load cube mesh
    struct Mesh cube_mesh = read_mesh_binary("data/models/bin/cube.bin");
    struct Instance cube = {0.0f, 0.0f, 0.0f};
    set_instances(&cube_mesh, &cube, 1);


    // CALLS TO GPU BACKEND
    // todo: create a function that does this automatically based on game state object
    int basic_material_id = wgpuCreateMaterial(&basic_material);
    int hud_material_id = wgpuCreateMaterial(&hud_material);

    int ground_mesh_id = wgpuCreateMesh(basic_material_id, &ground_mesh);
    int teapot_mesh_id = wgpuCreateMesh(basic_material_id, &teapot_mesh);
    int cube_mesh_id = wgpuCreateMesh(basic_material_id, &cube_mesh);
    int quad_mesh_id = wgpuCreateMesh(hud_material_id, &quad_mesh);

    int texSlot2 = wgpuAddTexture(cube_mesh_id, "data/textures/bin/font_atlas.bin");
    int font_atlas_texture_slot = wgpuAddTexture(quad_mesh_id, "data/textures/bin/font_atlas.bin");

    int aspect_ratio_uniform = wgpuAddUniform(&hud_material, &aspect_ratio, sizeof(float));
    int brightnessOffset = wgpuAddUniform(&basic_material, &brightness, sizeof(float));
    int timeOffset = wgpuAddUniform(&basic_material, &timeVal, sizeof(float));
    int cameraOffset = wgpuAddUniform(&basic_material, camera, sizeof(camera));
    int viewOffset = wgpuAddUniform(&basic_material, view, sizeof(view));



    
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
        wgpuSetUniformValue(&basic_material, timeOffset, &timeVal, sizeof(float));
        wgpuSetUniformValue(&basic_material, cameraOffset, &inv, sizeof(camera));

        // Actual frame rendering
        // *info* without vsync/fifo the cpu can keep pushing new frames without waiting, until the queue is full and backpressure
        // *info* forces the cpu to wait before pushing another frame, bringing the cpu speed down to the gpu speed
        // *info* we can force the cpu to wait regardless by using the fence in wgpuEndFrame()
        debug_info.ms_waited_on_gpu = wgpuDrawFrame();
        
        // todo: create a central place for things that need to happen to initialize every frame iteration correctly
        // Set the printed chars to 0 to reset the text in the HUD
        screen_chars_index = 0;
        current_screen_char = 0;
        quad_mesh.instanceCount = screen_chars_index;
        memset(screen_chars, 0, sizeof(screen_chars));

        draw_debug_info();
        if (fabs(cameraSpeed.x) > 1.0f || fabs(cameraSpeed.y) > 1.0f || fabs(cameraSpeed.z) > 1.0f) {
            printf("Camera speed: %4.2f %4.2f %4.2f\n", cameraSpeed.x, cameraSpeed.y, cameraSpeed.z);
        }
    }
    
    return 0;
}