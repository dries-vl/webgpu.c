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
float fov = 4.0f;
float farClip = 2000.0f;
float nearClip = 1.0f;

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
    // Rotation around Y-axis
    float rotMatrix[16] = {
         cos(angle), 0.0f, sin(angle), 0.0f,
         0.0f, 1.0f, 0.0f, 0.0f,
        -sin(angle), 0.0f, cos(angle), 0.0f,
         0.0f, 0.0f, 0.0f, 1.0f
    };

    // Save the original translation values
    float tx = matrix[3];
    float ty = matrix[7];
    float tz = matrix[11];

    // Remove the translation values
    matrix[3]  = 0.0f;  
    matrix[7]  = 0.0f;
    matrix[11] = 0.0f;

    // Apply the rotation
    float tempMatrix[16] = {0};
    multiply(matrix, 4, 4, rotMatrix, 4, 4, tempMatrix);

    // Restore translation values
    tempMatrix[3]  = tx;
    tempMatrix[7]  = ty;
    tempMatrix[11] = tz;

    // Copy back the result
    for (int i = 0; i < 16; i++) {
        matrix[i] = tempMatrix[i];
    }
}

void pitch(float angle, float *matrix) { // for rotating around itself
    float rotMatrix[16] = {
         1.0f, 0.0f, 0.0f, 0.0f,
         0.0f, cos(angle), -sin(angle), 0.0f,
         0.0f, sin(angle), cos(angle), 0.0f,
         0.0f, 0.0f, 0.0f, 1
    };
    float translate[16] = {
         1.0f, 0.0f, 0.0f, -matrix[3],
         0.0f, 1.0f, 0.0f, -matrix[7],
         0.0f, 0.0f, 1.0f, -matrix[11],
         0.0f, 0.0f, 0.0f, 1
    };
    float translateback[16] = {
         1.0f, 0.0f, 0.0f, matrix[3],
         0.0f, 1.0f, 0.0f, matrix[7],
         0.0f, 0.0f, 1.0f, matrix[11],
         0.0f, 0.0f, 0.0f, 1
    };
    multiply(matrix, 4, 4, translate, 4, 4, matrix);
    multiply(matrix, 4, 4, rotMatrix, 4, 4, matrix);
    multiply(matrix, 4, 4, translateback, 4, 4, matrix);
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
                    char msg[32];
                    snprintf(msg, sizeof(msg), "Key Pressed: %c\n", virtualKey);
                    printf(msg);
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
    float camera[16] = {
         1.0f, 0.0f, 0.0f, 0.0f,
         0.0f, 1.0f, 0.0f, 0.0f,
         0.0f, 0.0f, 1.0f, -500.0f,
         0.0f, 0.0f, 0.0f, 1
    };
    int cameraOffset = wgpuAddUniform(pipelineA, camera, sizeof(camera));
    
    // Add a projection matrix (a 4x4 matrix).  
    float view[16] = {
         1.0/(tan(fov/2.0)*1.6), 0.0f, 0.0f, 0.0f,
         0.0f, 1.0/tan(fov/2.0), 0.0f, 0.0f,
         0.0f, 0.0f, (nearClip+farClip)/(nearClip-farClip), (2*farClip*nearClip)/(nearClip-farClip),
         0.0f, 0.0f, -1, 1
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
        timeVal += 0.016f;
        pitch(0.001f, camera);
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

            float ms_last_frame = ((float) (1000*ticks_elapsed) / (float) ticks_per_second);
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