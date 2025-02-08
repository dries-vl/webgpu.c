#include "webgpu.c"

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>

extern void  wgpuInit(HINSTANCE hInstance, HWND hwnd, int width, int height);
extern int   wgpuCreatePipeline(const char *shaderPath);
extern int   wgpuCreateMesh(int pipelineID, struct Mesh mesh);
extern int   wgpuAddUniform(int pipelineID, const void* data, int dataSize);
extern void  wgpuSetUniformValue(int pipelineID, int uniformOffset, const void* data, int dataSize);
extern int   wgpuAddTexture(int pipelineID, const char* texturePath);
extern void  wgpuStartFrame();
extern void  wgpuDrawPipeline(int pipelineID);
extern void  wgpuEndFrame();

static bool g_Running = true;
float fov = 3.14f / 4.0f; // 45 degrees
float farClip = 2000.0f;
float nearClip = 1.0f;
float AR = 800.0f / 600.0f; // aspect ratio

struct Vector3 {
    float x, y, z;
};
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


float camera[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, -100.0f,
        0.0f, 0.0f, 0.0f, 1
};
float cameraspeed[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

void cameraMovement(float *camera, float *speed, float ms) {
    float cameraRotation[9] = {
        camera[0], camera[1], camera[2],
        camera[4], camera[5], camera[6],
        camera[8], camera[9], camera[10]
    };
    float transSpeed[3] = {speed[0], speed[1], speed[2]};
    printf("transSpeed: %f %f %f\n", transSpeed[0], transSpeed[1], transSpeed[2]);
    multiply(cameraRotation, 3, 3, transSpeed, 3, 1, transSpeed); // in world coords
    printf("transSpeedAbs: %f %f %f\n", transSpeed[0], transSpeed[1], transSpeed[2]);
    yaw(speed[3] * ms, camera);
    struct Vector3 movit = {transSpeed[0], transSpeed[1], transSpeed[2]};
    move(movit, camera);
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
    struct Instance singleInstance = {.position = {0,0,0}};
    *instances_in = singleInstance;
    return (struct Mesh) {
        .vertices=vertices_in, .indices=indices_in, .instances = instances_in,
        .vertexCount=header.vertexCount, .indexCount=header.indexCount, .instanceCount = 1};
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

                // Print active keys
                if (isPressed) {
                    printf("%d\n", virtualKey);
                    printf("%d\n", flags);
                    char msg[32];
                    snprintf(msg, sizeof(msg), "Key Pressed: %c\n", virtualKey);
                    printf(msg);
                    if (virtualKey == 'Z') {
                        cameraspeed[2] = -0.1f;
                    }
                    if (virtualKey == 'S') {
                        cameraspeed[2] = 0.1f;
                    }
                    if (virtualKey == 'Q') {
                        cameraspeed[0] = -0.1f;
                    }
                    if (virtualKey == 'D') {
                        cameraspeed[0] = 0.1f;
                    }
                    if (virtualKey == 'E') {
                        cameraspeed[3] = -0.01f;
                    }
                    if (virtualKey == 'A') {
                        cameraspeed[3] = 0.01f;
                    }
                }
                else if (!isPressed) {
                    if (virtualKey == 'Z') {
                        cameraspeed[2] = 0.0f;
                        printf("Z released\n");
                    }
                    if (virtualKey == 'S') {
                        cameraspeed[2] = 0.0f;
                    }
                    if (virtualKey == 'Q') {
                        cameraspeed[0] = 0.0f;
                    }
                    if (virtualKey == 'D') {
                        cameraspeed[0] = 0.0f;
                    }
                    if (virtualKey == 'E') {
                        cameraspeed[3] = 0.0f;
                    }
                    if (virtualKey == 'A') {
                        cameraspeed[3] = 0.0f;
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
            }
            free(lpb); // Free allocated memory
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

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;
    
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
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600, NULL, NULL, hInstance, NULL
    );
    assert(hwnd);

    load_raw_input_functions();
    InitializeRawInput();
    
    wgpuInit(hInstance, hwnd, 800, 600);
    
    // Create a pipeline.
    // The shader (shader.wgsl) must declare a uniform block (group0) and texture struct (group1).
    // todo: use precompiled shader for faster loading
    // todo: use glsl instead of wgsl for C-style syntax
    int pipelineA = wgpuCreatePipeline("data/shaders/shader.wgsl");

    // todo: material as abstraction for pipelines etc.
    // todo: instanced drawing
    // struct Material {

    // };
    
    // Create a mesh.
    // todo: use indices
    // todo: use obj file mesh -> first convert to C array on disk for faster loading
    // Note: vertices now include UV coordinates.
    struct Mesh triVerts = read_mesh_binary("data/models/meshes/teapot.bin");
    int meshA = wgpuCreateMesh(pipelineA, triVerts);
    
    // Add uniforms. For example, add a brightness value (a float).
    float brightness = 1.0f;
    int brightnessOffset = wgpuAddUniform(pipelineA, &brightness, sizeof(float));

    // Optionally, add a time uniform.
    float timeVal = 0.0f;
    int timeOffset = wgpuAddUniform(pipelineA, &timeVal, sizeof(float));
    
    // Add a camera transform (a 4x4 matrix).  
    int cameraOffset = wgpuAddUniform(pipelineA, camera, sizeof(camera));
    
    // Add a projection matrix (a 4x4 matrix).  
    float view[16] = {
    1.0 / (tan(fov / 2.0) * AR), 0.0f,  0.0f,                               0.0f,
    0.0f,  1.0 / tan(fov / 2.0),          0.0f,                               0.0f,
    0.0f,  0.0f, -(farClip + nearClip) / (farClip - nearClip), -(2 * farClip * nearClip) / (farClip - nearClip),
    0.0f,  0.0f, -1.0f,                               0.0f
    };
    int viewOffset = wgpuAddUniform(pipelineA, view, sizeof(view));

    // --- Add a texture to the pipeline ---
    int texSlot1 = wgpuAddTexture(pipelineA, "data/textures/texture_1.png");
    int texSlot2 = wgpuAddTexture(pipelineA, "data/textures/texture_2.png");
    
    // Variables to keep track of performance
    LARGE_INTEGER query_perf_result;
	QueryPerformanceFrequency(&query_perf_result);
	long ticks_per_second = query_perf_result.QuadPart;
    LARGE_INTEGER current_tick_count;
    QueryPerformanceCounter(&current_tick_count);
    long current_cycle_count = read_cycle_count();
    float ms_last_60_frames[60] = {0}; int ms_index = 0; int avg_count = 60; float count = 0.0;
    float ms_last_frame = 1.0f;
    // Main loop
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
        cameraMovement(camera, cameraspeed, ms_last_frame);
        float inv[16];
        inverseViewMatrix(camera, inv);
        wgpuSetUniformValue(pipelineA, timeOffset, &timeVal, sizeof(float));
        wgpuSetUniformValue(pipelineA, cameraOffset, &inv, sizeof(camera));
        
        // Actual frame rendering
        // *info* without vsync/fifo the cpu can keep pushing new frames without waiting, until the queue is full and backpressure
        // *info* forces the cpu to wait before pushing another frame, bringing the cpu speed down to the gpu speed
        // *info* we can force the cpu to wait regardless by using the fence in wgpuEndFrame()
        wgpuStartFrame();
        wgpuDrawPipeline(pipelineA);
        wgpuEndFrame();
        // todo: create a function that we can use as oneliner measuring timing here
        // todo: -> game loop time -> cpu to gpu commands time -> gpu time -> total time (and see if those three add up to total or not)

        // Measure performance
        // todo: isolate out the exact time we spend inside the loop iteration itself to see the actual CPU time
        // todo: use this cpu time to measure how much time we lose waiting on the GPU (?) ~eg. know if the GPU is the bottleneck or not
        // todo: isolate out the exact time from StartFrame to EndFrame to know how much CPU time gets spent on setting up gpu commands; draw calls etc.
        {
            LARGE_INTEGER new_tick_count;
            QueryPerformanceCounter(&new_tick_count);
            long ticks_elapsed = new_tick_count.QuadPart - current_tick_count.QuadPart;
            current_tick_count = new_tick_count;

            long new_cycle_count = read_cycle_count();
            long cycles_elapsed = new_cycle_count - current_cycle_count;
            int cycles_last_frame = (int) cycles_elapsed / 1000000; // million cycles per frame
            current_cycle_count = new_cycle_count;

            ms_last_frame = ((float) (1000*ticks_elapsed) / (float) ticks_per_second);
            int fps = ticks_per_second / ticks_elapsed; // calculate how many times we could do this amount of ticks (=1frame) in one second
            // todo: render in bitmap font to screen instead of printf IO
            char perf_output_string[256];
            printf("%4.2fms/f,  %df/s,  %dmc/f\n", ms_last_frame, fps, cycles_last_frame);
            count += ms_last_frame - ms_last_60_frames[ms_index];
            ms_last_60_frames[ms_index] = ms_last_frame;
            ms_index = (ms_index + 1) % avg_count;
            if (ms_index % avg_count == 0) {
                printf("Average frame timing last %d frames: %4.2fms \n", avg_count, count / (float) avg_count);
            }
        }
    }
    
    return 0;
}