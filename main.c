#include "webgpu.c"

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>

extern void  wgpuInit(HINSTANCE hInstance, HWND hwnd, int width, int height);
extern int   wgpuCreatePipeline(const char *shaderPath);
extern int   wgpuCreateMesh(int pipelineID, const Vertex *vertices, int vertexCount);
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

    float temp[row1 * col2]; // Stack-allocated temporary array

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

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
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
    
    wgpuInit(hInstance, hwnd, 800, 600);
    
    // Create a pipeline.
    // The shader (shader.wgsl) must declare a uniform block (group0) and texture struct (group1).
    // todo: use precompiled shader for faster loading
    // todo: use glsl instead of wgsl for C-style syntax
    int pipelineA = wgpuCreatePipeline("data/shaders/shader.wgsl");

    // struct Material {

    // };
    
    // Create a mesh.
    // todo: use indices
    // todo: use obj file mesh -> first convert to C array on disk for faster loading
    // Note: vertices now include UV coordinates.
    Vertex triVerts[] = {
        { { -0.5f,  0.8f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.5f, -0.5f } },
        { { -1.3f, -0.8f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { -0.5f, 1.0f } },
        { {  0.3f, -0.8f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.5f, 1.0f } },
    };
    int meshA = wgpuCreateMesh(pipelineA, triVerts, 3);
    Vertex triVerts2[] = {
        {{ -0.5f,  0.8f, 0.5f }, { 1.0f, 0.0f, 0.0f }, { 0.5f, -0.5f }},
        {{ -1.3f, -0.8f, 0.5f }, { 0.0f, 1.0f, 0.0f }, { -0.5f, 1.0f }},
        {{  0.3f, -0.8f, 0.5f }, { 0.0f, 0.0f, 1.0f }, { 1.5f, 1.0f }},
    };
    int meshB = wgpuCreateMesh(pipelineA, triVerts2, 3);
    
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
         0.0f, 0.0f, 1.0f, -10.0f,
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
        yaw(0.001f * ms_last_frame, camera);
        float inv[16];
        inverseViewMatrix(camera, inv);
        wgpuSetUniformValue(pipelineA, timeOffset, &timeVal, sizeof(float));
        wgpuSetUniformValue(pipelineA, cameraOffset, &inv, sizeof(camera));
        
        // Actual frame rendering
        wgpuStartFrame();
        wgpuDrawPipeline(pipelineA);
        wgpuEndFrame();

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

            ms_last_frame = (float) (((float)(1000*ticks_elapsed)) / (float)ticks_per_second);
            int fps = ticks_per_second / ticks_elapsed; // calculate how many times we could do this amount of ticks (=1frame) in one second
            // todo: render in bitmap font to screen instead of printf IO
            printf("%.2fms/f,  %df/s,  %dmc/f\n", ms_last_frame, fps, cycles_last_frame);
        }
    }
    
    DestroyWindow(hwnd);
    return 0;
}