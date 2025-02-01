#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include "webgpu.c"  // Includes our modified webgpu functions and definitions

extern void  wgpuInit(HINSTANCE hInstance, HWND hwnd, int width, int height);
extern void  wgpuShutdown();
extern int   wgpuCreatePipeline(const char *shaderPath);
extern int   wgpuCreateMesh(int pipelineID, const Vertex *vertices, int vertexCount);
extern int   wgpuAddUniform(int pipelineID, float initialValue);
extern void  wgpuSetUniformValue(int uniformID, float newValue);
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
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    WNDCLASSEX wc = {0};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "WgpuWindowClass";
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        "3D rpg.c",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600,
        NULL,
        NULL,
        hInstance,
        NULL
    );
    assert(hwnd);

    wgpuInit(hInstance, hwnd, 800, 600);

    // Create two pipelines (shaders must support the uniform buffer binding).
    int pipelineA = wgpuCreatePipeline("shader.wgsl");
    int pipelineB = wgpuCreatePipeline("shader2.wgsl");

    // Create meshes.
    Vertex triVertsA[] = {
        {{ -0.5f,  0.8f, 0.0f }, { 0.0f, 0.0f, 0.0f }},
        {{ -1.3f, -0.8f, 0.0f }, { 1.0f, 1.0f, 1.0f }},
        {{  0.3f, -0.8f, 0.0f }, { 1.0f, 1.0f, 1.0f }},
    };
    Vertex triVertsB[] = {
        {{  0.0f,  0.8f, 0.0f }, { 1.0f, 0.0f, 0.0f }},
        {{ -0.8f, -0.8f, 0.0f }, { 0.0f, 1.0f, 0.0f }},
        {{  0.8f, -0.8f, 0.5f }, { 0.0f, 0.0f, 1.0f }},
    };

    int meshA = wgpuCreateMesh(pipelineA, triVertsA, 3);
    int meshB = wgpuCreateMesh(pipelineB, triVertsB, 3);

    // Add one uniform per pipeline.
    int uniformA = wgpuAddUniform(pipelineA, 1.0f);
    float uniformBValue = 1.0; 
    int uniformB = wgpuAddUniform(pipelineB, uniformBValue);

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
        if (!g_Running)
            break;

        // Optionally update uniform values here, e.g.,
        uniformBValue -= 0.01;
        wgpuSetUniformValue(uniformB, uniformBValue);

        wgpuStartFrame();
        wgpuDrawPipeline(pipelineA);
        wgpuDrawPipeline(pipelineB);
        wgpuEndFrame();
    }

    wgpuShutdown();
    DestroyWindow(hwnd);
    return 0;
}
