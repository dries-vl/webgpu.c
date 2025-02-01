#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include "webgpu.c"  // Includes our updated webgpu implementation

extern void  wgpuInit(HINSTANCE hInstance, HWND hwnd, int width, int height);
extern void  wgpuShutdown();
extern int   wgpuCreatePipeline(const char *shaderPath);
extern int   wgpuCreateMesh(int pipelineID, const Vertex *vertices, int vertexCount);
extern int   wgpuAddUniform(int pipelineID, const void* data);
extern void  wgpuSetUniformValue(int pipelineID, int uniformOffset, const void* data, int dataSize);
extern int   wgpuAddTexture(int pipelineID, const char* texturePath);
extern void  wgpuStartFrame();
extern void  wgpuDrawPipeline(int pipelineID);
extern void  wgpuEndFrame();

static bool g_Running = true;

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
        0, wc.lpszClassName, "Generic Uniform Demo",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600, NULL, NULL, hInstance, NULL
    );
    assert(hwnd);
    
    wgpuInit(hInstance, hwnd, 800, 600);
    
    // Create a pipeline.
    // Your shader (shader.wgsl) should declare a uniform block that matches
    // the layout of the generic uniform buffer (e.g. an array of floats of size UNIFORM_BUFFER_CAPACITY).
    int pipelineA = wgpuCreatePipeline("shader.wgsl");
    
    // Create a mesh.
    Vertex triVerts[] = {
        {{ -0.5f,  0.8f, 0.0f }, { 1.0f, 0.0f, 0.0f }},
        {{ -1.3f, -0.8f, 0.0f }, { 0.0f, 1.0f, 0.0f }},
        {{  0.3f, -0.8f, 0.0f }, { 0.0f, 0.0f, 1.0f }},
    };
    int meshA = wgpuCreateMesh(pipelineA, triVerts, 3);
    
    // Add uniforms. For example, add a brightness value (a float).
    float brightness = 1.0f;
    int brightnessOffset = wgpuAddUniform(pipelineA, &brightness);

    // Optionally, add a time uniform.
    float timeVal = 0.0f;
    int timeOffset = wgpuAddUniform(pipelineA, &timeVal);
    
    // Add a camera transform (a 4x4 matrix).  
    float camera[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    int cameraOffset = wgpuAddUniform(pipelineA, camera);

    // Main loop.
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
        
        // For example, update the time uniform.
        timeVal += 0.016f; // pretend 16ms per frame
        wgpuSetUniformValue(pipelineA, timeOffset, &timeVal, sizeof(float));
        
        wgpuStartFrame();
        wgpuDrawPipeline(pipelineA);
        wgpuEndFrame();
    }
    
    wgpuShutdown();
    DestroyWindow(hwnd);
    return 0;
}
