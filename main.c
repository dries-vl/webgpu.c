#include "webgpu.c"  // Includes our updated webgpu implementation

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

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
    int pipelineA = wgpuCreatePipeline("data/shaders/shader.wgsl");

    // struct Material {

    // };
    
    // Create a mesh.
    // Note: vertices now include UV coordinates.
    Vertex triVerts[] = {
        { { -0.5f,  0.8f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.5f, -0.5f } },
        { { -1.3f, -0.8f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { -0.5f, 1.0f } },
        { {  0.3f, -0.8f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.5f, 1.0f } },
    };
    int meshA = wgpuCreateMesh(pipelineA, triVerts, 3);

    Vertex quadVerts[] = {
    { { 0.0f,  0.8f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.f, 1.f } },
    { { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.f, 0.f } },
    { {  0.8f, 0.8f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.f, 1.f } },
    { {  0.8f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.f, 0.f } },
    };
    int meshB = wgpuCreateMesh(pipelineA, quadVerts, 4);
    
    // Add uniforms. For example, add a brightness value (a float).
    float brightness = 1.0f;
    int brightnessOffset = wgpuAddUniform(pipelineA, &brightness, sizeof(float));

    // Optionally, add a time uniform.
    float timeVal = 0.0f;
    int timeOffset = wgpuAddUniform(pipelineA, &timeVal, sizeof(float));
    
    // Add a camera transform (a 4x4 matrix).  
    float camera[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    int cameraOffset = wgpuAddUniform(pipelineA, camera, sizeof(camera));

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
        wgpuSetUniformValue(pipelineA, timeOffset, &timeVal, sizeof(float));
        
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

            int ms_last_frame = (int) ((1000*ticks_elapsed) / ticks_per_second);
            int fps = ticks_per_second / ticks_elapsed; // calculate how many times we could do this amount of ticks (=1frame) in one second
            // todo: render in bitmap font to screen instead of printf IO
            char perf_output_string[256];
            wsprintf(perf_output_string, "%dms/f,  %df/s,  %dmc/f\n", ms_last_frame, fps, cycles_last_frame);
            printf(perf_output_string);
        }
    }
    
    DestroyWindow(hwnd);
    return 0;
}
