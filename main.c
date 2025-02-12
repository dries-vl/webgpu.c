#include "webgpu.c"

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>

extern void  wgpuInit(HINSTANCE hInstance, HWND hwnd, int width, int height);
extern int   wgpuCreateMaterial(struct Material *material);
extern int   wgpuCreateMesh(int materialID, struct Mesh *mesh);
extern int   wgpuAddUniform(int materialID, const void* data, int dataSize);
extern void  wgpuSetUniformValue(int materialID, int uniformOffset, const void* data, int dataSize);
extern int   wgpuAddTexture(int materialID, const char* texturePath);
extern void  wgpuStartFrame();
extern void  wgpuDrawMaterial(int materialID);
extern float  wgpuEndFrame();

static bool g_Running = true;
float fov = 3.14f / 4.0f; // 45 degrees
float farClip = 2000.0f;
float nearClip = 1.0f;
#define WINDOW_WIDTH 1920 // todo: fps degrades massively when at higher resolution, even with barely any fragment shader logic
#define WINDOW_HEIGHT 1080
float AR = (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT; // Aspect ratio
float cameraRotation[2] = {0.0f, 0.0f}; // yaw, pitch
struct ButtonState {
    int left;
    int right;
    int forward;
    int backward;
};
struct ButtonState buttonState = {0, 0, 0, 0};

struct Vector3 {
    float x, y, z;
};
#define CHAR_WIDTH_SCREEN (48 * 2) // todo: avoid difference with same const in shader code...
#define CHAR_HEIGHT_SCREEN (24 * 2)
#define MAX_CHAR_ON_SCREEN (48 * 24 * 2)
struct char_instance screen_chars[MAX_CHAR_ON_SCREEN] = {0};
struct Mesh quad_mesh = {0};
int screen_chars_index = 0;
int current_screen_char = 0;
void print_on_screen(char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        if (i >= CHAR_WIDTH_SCREEN) break;
        if (str[i] == '\n') {
            current_screen_char = ((current_screen_char / CHAR_WIDTH_SCREEN) + 1) * CHAR_WIDTH_SCREEN;
            continue;
        }
        screen_chars[screen_chars_index] = (struct char_instance) {.i_pos={current_screen_char}, .i_char=(int) str[i]};
        screen_chars_index++;
        current_screen_char++;
        quad_mesh.instanceCount = screen_chars_index;
    }
}
void multiply(const float *a, int row1, int col1, const float *b, int row2, int col2, float *d) {
    assert(col1 == row2); // Ensure valid matrix dimensions

    float *temp = (float *) malloc(row1 * col2 * 4); // Stack-allocated temporary array

    for (int i = 0; i < row1; i++) {
        for (int j = 0; j < col2; j++) {
            float sum = 0.0f;
            for (int k = 0; k < col1; k++) {
                sum += a[i * col1 + k] * b[k * col2 + j];
            }
            temp[i * col2 + j] = sum;
        }
    }

    // Copy back to d
    for (int i = 0; i < row1 * col2; i++) {
        d[i] = temp[i];
    }
    free(temp);
}
void inverseViewMatrix(const float m[16], float inv[16]) {
    // Extract the 3x3 rotation matrix
    float rot[9] = {
        m[0], m[1], m[2],
        m[4], m[5], m[6],
        m[8], m[9], m[10]
    };

    // Transpose the rotation matrix (which is its inverse if it's orthonormal)
    float rotT[9] = {
        rot[0], rot[3], rot[6],
        rot[1], rot[4], rot[7],
        rot[2], rot[5], rot[8]
    };

    // Extract and transform the translation vector
    float translation[3] = {-m[3], -m[7], -m[11]};
    float newTranslation[3];
    multiply(rotT, 3, 3, translation, 3, 1, newTranslation);

    // Construct the inverse matrix
    inv[0] = rotT[0]; inv[1] = rotT[1]; inv[2] = rotT[2]; inv[3] = newTranslation[0];
    inv[4] = rotT[3]; inv[5] = rotT[4]; inv[6] = rotT[5]; inv[7] = newTranslation[1];
    inv[8] = rotT[6]; inv[9] = rotT[7]; inv[10] = rotT[8]; inv[11] = newTranslation[2];
    inv[12] = 0.0f; inv[13] = 0.0f; inv[14] = 0.0f; inv[15] = 1.0f;
}

void move(struct Vector3 move, float *matrix) {
    matrix[3] += move.x;
    matrix[7] += move.y;
    matrix[11] += move.z;
}

void yaw(float angle, float *matrix) {
    // Create the yaw rotation matrix.
    float yawRot[16] = {
         cos(angle), 0.0f, sin(angle), 0.0f,
         0.0f,       1.0f, 0.0f,       0.0f,
        -sin(angle), 0.0f, cos(angle), 0.0f,
         0.0f,       0.0f, 0.0f,       1.0f
    };

    // Create a temporary matrix to hold the new rotation.
    float newRot[16] = {0};

    // Multiply the yaw rotation by the current rotation part.
    // Since the rotation is stored in the upper 3x3 block, we multiply the full matrices
    // but only update the corresponding 3x3 part.
    multiply(yawRot, 4, 4, matrix, 4, 4, newRot);

    // Update only the rotation portion (indices 0,1,2; 4,5,6; 8,9,10) of the original matrix.
    matrix[0] = newRot[0];
    matrix[1] = newRot[1];
    matrix[2] = newRot[2];

    matrix[4] = newRot[4];
    matrix[5] = newRot[5];
    matrix[6] = newRot[6];

    matrix[8] = newRot[8];
    matrix[9] = newRot[9];
    matrix[10] = newRot[10];

    // Leave indices 3, 7, and 11 (translation) unchanged.
}
void pitch(float angle, float *matrix) { // for rotating around itself
    float rotMatrix[16] = {
         1.0f, 0.0f, 0.0f, 0.0f,
         0.0f, cos(angle), -sin(angle), 0.0f,
         0.0f, sin(angle), cos(angle), 0.0f,
         0.0f, 0.0f, 0.0f, 1
    };

    // Create a temporary matrix to hold the new rotation.
    float newRot[16] = {0};

    // Multiply the yaw rotation by the current rotation part.
    // Since the rotation is stored in the upper 3x3 block, we multiply the full matrices
    // but only update the corresponding 3x3 part.
    multiply(rotMatrix, 4, 4, matrix, 4, 4, newRot);

    // Update only the rotation portion (indices 0,1,2; 4,5,6; 8,9,10) of the original matrix.
    matrix[0] = newRot[0];
    matrix[1] = newRot[1];
    matrix[2] = newRot[2];

    matrix[4] = newRot[4];
    matrix[5] = newRot[5];
    matrix[6] = newRot[6];

    matrix[8] = newRot[8];
    matrix[9] = newRot[9];
    matrix[10] = newRot[10];

    // Leave indices 3, 7, and 11 (translation) unchanged.
}

void absolute_yaw(float angle, float *matrix){
    float yawRot[16] = {
        cos(angle + cameraRotation[0]), 0.0f, sin(angle + cameraRotation[0]), 0.0f,
        0.0f,       1.0f, 0.0f,       0.0f,
       -sin(angle + cameraRotation[0]), 0.0f, cos(angle + cameraRotation[0]), 0.0f,
        0.0f,       0.0f, 0.0f,       1.0f
    };
    float pitchRot[16] = {
         1.0f, 0.0f, 0.0f, 0.0f,
         0.0f, cos(cameraRotation[1]), -sin(cameraRotation[1]), 0.0f,
         0.0f, sin(cameraRotation[1]), cos(cameraRotation[1]), 0.0f,
         0.0f, 0.0f, 0.0f, 1
    };
    float rotMatrix[16] = {0};
    multiply(yawRot, 4, 4, pitchRot, 4, 4, rotMatrix);
    matrix[0] = rotMatrix[0];
    matrix[1] = rotMatrix[1];
    matrix[2] = rotMatrix[2];
    matrix[4] = rotMatrix[4];
    matrix[5] = rotMatrix[5];
    matrix[6] = rotMatrix[6];
    matrix[8] = rotMatrix[8];
    matrix[9] = rotMatrix[9];
    matrix[10] = rotMatrix[10];
    cameraRotation[0] += angle;
}
void absolute_pitch(float angle, float *matrix){
    float pitchRot[16] = {
         1.0f, 0.0f, 0.0f, 0.0f,
         0.0f, cos(angle + cameraRotation[1]), -sin(angle + cameraRotation[1]), 0.0f,
         0.0f, sin(angle + cameraRotation[1]), cos(angle + cameraRotation[1]), 0.0f,
         0.0f, 0.0f, 0.0f, 1
    };
    float yawRot[16] = {
        cos(cameraRotation[0]), 0.0f, sin(cameraRotation[0]), 0.0f,
        0.0f,       1.0f, 0.0f,       0.0f,
       -sin(cameraRotation[0]), 0.0f, cos(cameraRotation[0]), 0.0f,
        0.0f,       0.0f, 0.0f,       1.0f
    };
    float rotMatrix[16] = {0};
    multiply(yawRot, 4, 4, pitchRot, 4, 4, rotMatrix);
    matrix[0] = rotMatrix[0];
    matrix[1] = rotMatrix[1];
    matrix[2] = rotMatrix[2];
    matrix[4] = rotMatrix[4];
    matrix[5] = rotMatrix[5];
    matrix[6] = rotMatrix[6];
    matrix[8] = rotMatrix[8];
    matrix[9] = rotMatrix[9];
    matrix[10] = rotMatrix[10];
    cameraRotation[1] += angle;
}

float camera[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, -300.0f,
        0.0f, 0.0f, 0.0f, 1
};
struct Speed {
    float x;
    float y;
    float z;
    float yaw;
    float pitch;
    float roll;
};
struct Speed cameraSpeed = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}; // only y-speed used
float movementSpeed = 0.5f;
void cameraMovement(float *camera, float speed, float ms) {
    float yawRot[16] = {
        cos(cameraRotation[0]), 0.0f, sin(cameraRotation[0]),
        0.0f,       1.0f, 0.0f,
       -sin(cameraRotation[0]), 0.0f, cos(cameraRotation[0])
    };
    float xSpeed = speed * ms * (buttonState.right - buttonState.left);
    float ySpeed = cameraSpeed.y * ms;
    float zSpeed = speed * ms * -(buttonState.forward - buttonState.backward);
    float transSpeed[3] = {xSpeed, ySpeed, zSpeed};
    multiply(yawRot, 3, 3, transSpeed, 3, 1, transSpeed); // in world coords
    struct Vector3 movit = {transSpeed[0], transSpeed[1], transSpeed[2]};
    move(movit, camera);
}
void applyGravity(struct Speed *speed, float *pos, float ms) { // gravity as velocity instead of acceleration
    float gravity = 9.81f * 0.001f;
    float gravitySpeed = gravity * ms;
    if (pos[1] > 0.0f){
        speed->y -= gravitySpeed;
        // if (pos[1] < 0.0f) {pos[1] = 0.0f;}; // this doesn't work
    }
    else { // some sort of hit the ground / collision detection
        speed->y = 0.0f;
    }
}

typedef struct {
    uint32_t vertexCount;
    uint32_t indexCount;
    uint32_t vertexArrayOffset;
    uint32_t indexArrayOffset;
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
    uint32_t *indices_in = malloc(header.indexCount * sizeof(uint32_t));
    if (!indices_in) {
        fprintf(stderr, "Memory allocation failed for indices_in\n");
        exit(1);
    }
    fseek(file, header.vertexArrayOffset, SEEK_SET);
    fread(vertices_in, sizeof(struct Vertex), header.vertexCount, file);
    fseek(file, header.indexArrayOffset, SEEK_SET);
    fread(indices_in, sizeof(uint32_t), header.indexCount, file);
    fclose(file);
    size_t fileSize = header.indexArrayOffset + header.indexCount * sizeof(uint32_t);
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

void set_instances(struct Mesh *mesh, struct Instance *instances, int instanceCount) {
    free(mesh->instances);
    mesh->instances = instances;
    mesh->instanceCount = instanceCount;
}

// todo: move this somewhere deep...
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
    float aspect_ratio = ((float) WINDOW_WIDTH / (float) WINDOW_HEIGHT);

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

    int width = WINDOW_WIDTH;
    int height = WINDOW_HEIGHT;
    int x = (screen.right - width) / 2;
    int y = (screen.bottom - height) / 2;

    SetWindowPos(hwnd, NULL, x, y, width, height, SWP_NOZORDER | SWP_SHOWWINDOW);
    ShowCursor(FALSE);

    print_time_since_startup("Setup window");

    load_raw_input_functions();
    InitializeRawInput();
    print_time_since_startup("Load raw input dll");
    
    wgpuInit(hInstance, hwnd, WINDOW_WIDTH, WINDOW_HEIGHT);
    print_time_since_startup("Init wgpu");

    // todo: create a 'floor' quad mesh to use as orientation and for shadows etc.
    // todo: lighting
    // todo: cubemap sky
    // todo: LOD: how to most efficiently swap out the mesh with a lower/higher res one? swap instances with other material (?)
    // todo: character mesh
    // todo: animate the character mesh (skeleton?)
    
    // todo: use precompiled shader for faster loading
    // todo: use glsl instead of wgsl for C-style syntax
    struct Material basic_material = (struct Material) {
        .vertex_layout=STANDARD_LAYOUT, .shader="data/shaders/shader.wgsl", .texture_layout=STANDARD_FOUR_TEXTURES,
        .use_alpha=0, .use_textures=1, .use_uniforms=1, .update_instances=0};
    int basic_material_id = wgpuCreateMaterial(&basic_material);

    print_time_since_startup("Create basic material");

    // create ground mesh
    struct Vertex ground_verts[4] = {0};
    // todo: use meters as basic measurement for everything
    ground_verts[0] = (struct Vertex) {.position={-10000.0, -10.0, 10000.0}, .color={.5,.5,.5}, .normal={0}, .uv={-1.,1.}};
    ground_verts[1] = (struct Vertex) {.position={10000.0, -10.0, 10000.0}, .color={.5,.5,.5}, .normal={0}, .uv={1.,1.}};
    ground_verts[2] = (struct Vertex) {.position={-10000.0, -10.0, -10000.0}, .color={.5,.5,.5}, .normal={0}, .uv={-1.,-1.}};
    ground_verts[3] = (struct Vertex) {.position={10000.0, -10.0, -10000.0}, .color={.5,.5,.5}, .normal={0}, .uv={1.,-1.}};
    uint32_t ground_indices[6] = {0,1,2, 1,2,3};
    struct Mesh ground_mesh = {0};
    ground_mesh.indexCount = 6;
    ground_mesh.indices = ground_indices;
    ground_mesh.vertexCount = 4;
    ground_mesh.vertices = ground_verts;
    ground_mesh.instanceCount = 1;
    ground_mesh.instances = (struct Instance[1]) {(struct Instance) {.position={0., 0., 0.}}};
    // todo: manipulate bindgroups for textures to have one material for meshes with a different texture
    int ground_mesh_id = wgpuCreateMesh(basic_material_id, &ground_mesh);
    int texSlot2 = wgpuAddTexture(ground_mesh_id, "data/textures/bin/texture_2.bin");
    
    // load teapot mesh
    struct Mesh teapot_mesh = read_mesh_binary("data/models/bin/teapot.bin");
    struct Instance instance1 = {0.0f, 0.0f, 0.0f};
    struct Instance instance2 = {0.0f, 80.0f, 0.0f};
    struct Instance instances[2] = {instance1, instance2};
    set_instances(&teapot_mesh, instances, 2);
    int teapot_mesh_id = wgpuCreateMesh(basic_material_id, &teapot_mesh);
    print_time_since_startup("Load teapot binary mesh");
    
    // load cube mesh
    struct Mesh cube_mesh = read_mesh_binary("data/models/bin/ground.bin");
    struct Instance cube = {0.0f, 0.0f, 0.0f};
    set_instances(&cube_mesh, &cube, 1);
    int cube_mesh_id = wgpuCreateMesh(basic_material_id, &cube_mesh);

    // create hud mesh
    struct vert2 quad_vertices[4] = {
        quad_vertices[0] = (struct vert2) {.position={0.0, 1.0}, .uv={0.0, 1.0}},
        quad_vertices[1] = (struct vert2) {.position={1.0, 1.0}, .uv={1.0, 1.0}},
        quad_vertices[2] = (struct vert2) {.position={0.0, 0.0}, .uv={0.0, 0.0}},
        quad_vertices[3] = (struct vert2) {.position={1.0, 0.0}, .uv={1.0, 0.0}}
    };
    uint32_t quad_indices[6] = {0,1,2, 1,2,3};
    quad_mesh.indexCount = 6;
    quad_mesh.indices = quad_indices;
    quad_mesh.vertexCount = 4;
    quad_mesh.vertices = quad_vertices;
    quad_mesh.instanceCount = MAX_CHAR_ON_SCREEN;
    quad_mesh.instances = screen_chars;
    struct Material hud_material = (struct Material) {
        .vertex_layout=HUD_LAYOUT, .shader="data/shaders/hud.wgsl", .texture_layout=STANDARD_FOUR_TEXTURES,
        .use_alpha=1, .use_textures=1, .use_uniforms=1, .update_instances=1
    };
    int hud_material_id = wgpuCreateMaterial(&hud_material);
    int quad_mesh_id = wgpuCreateMesh(hud_material_id, &quad_mesh);
    int font_atlas_texture_slot = wgpuAddTexture(quad_mesh_id, "data/textures/bin/font_atlas.bin");
    print_time_since_startup("Create HUD material");

    // add uniforms
    int aspect_ratio_uniform = wgpuAddUniform(hud_material_id, &aspect_ratio, sizeof(float));
    float brightness = 1.0f;
    int brightnessOffset = wgpuAddUniform(basic_material_id, &brightness, sizeof(float));
    float timeVal = 0.0f;
    int timeOffset = wgpuAddUniform(basic_material_id, &timeVal, sizeof(float));
    int cameraOffset = wgpuAddUniform(basic_material_id, camera, sizeof(camera));
    
    // Add a projection matrix (a 4x4 matrix).  
    float view[16] = {
    1.0 / (tan(fov / 2.0) * AR), 0.0f,  0.0f,                               0.0f,
    0.0f,  1.0 / tan(fov / 2.0),          0.0f,                               0.0f,
    0.0f,  0.0f, -(farClip + nearClip) / (farClip - nearClip), -(2 * farClip * nearClip) / (farClip - nearClip),
    0.0f,  0.0f, -1.0f,                               0.0f
    };
    int viewOffset = wgpuAddUniform(basic_material_id, view, sizeof(view));
    
    print_time_since_startup("Add textures and uniforms");
    
    // Main loop
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
        camera[7] = cameraLocation[1];
        float inv[16];
        inverseViewMatrix(camera, inv);
        wgpuSetUniformValue(basic_material_id, timeOffset, &timeVal, sizeof(float));
        wgpuSetUniformValue(basic_material_id, cameraOffset, &inv, sizeof(camera));

        // Actual frame rendering
        // *info* without vsync/fifo the cpu can keep pushing new frames without waiting, until the queue is full and backpressure
        // *info* forces the cpu to wait before pushing another frame, bringing the cpu speed down to the gpu speed
        // *info* we can force the cpu to wait regardless by using the fence in wgpuEndFrame()
        wgpuStartFrame();
        wgpuDrawMaterial(basic_material_id);
        wgpuDrawMaterial(hud_material_id);
        debug_info.ms_waited_on_gpu = wgpuEndFrame();
        
        // todo: create a central place for things that need to happen to initialize every frame iteration correctly
        // Set the printed chars to 0 to reset the text in the HUD
        screen_chars_index = 0;
        current_screen_char = 0;
        quad_mesh.instanceCount = screen_chars_index;
        memset(screen_chars, 0, sizeof(screen_chars));

        draw_debug_info();
    }
    
    return 0;
}