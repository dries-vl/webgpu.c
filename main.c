#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

#include "webgpu.c"

extern void  wgpuInit(HINSTANCE hInstance, HWND hwnd, int width, int height);
extern void  wgpuShutdown();

extern int   wgpuCreatePipeline(const char *shaderPath);
extern int   wgpuCreateMesh(int pipelineID, const Vertex *vertices, int vertexCount);

extern void  wgpuSetUniform(int pipelineID, float someValue);
extern void  wgpuStartFrame();
extern void  wgpuDrawPipeline(int pipelineID);
extern void  wgpuEndFrame();


// Win32 boilerplate
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

    // Register window class
    WNDCLASSEX wc = {0};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "WgpuWindowClass";
    RegisterClassEx(&wc);

    // Create window
    HWND hwnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        "Multi-Mesh Demo",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 
        800, 600,
        NULL,
        NULL,
        hInstance,
        NULL
    );
    assert(hwnd);

    // Init WebGPU
    wgpuInit(hInstance, hwnd, 800, 600);

    // 1) Create multiple pipelines
    int pipelineGreen  = wgpuCreatePipeline("shader.wgsl");
    int pipelineOrange = wgpuCreatePipeline("shader2.wgsl");

    // 2) Create some meshes with different vertex data & pipeline assignments
    Vertex triVertsA[] = {
        {{  -0.5f,  0.8f }, { 0.0f, 0.0f, 0.0f }},
        {{ -1.3f, -0.8f }, { 1.0f, 1.0f, 1.0f }},
        {{  0.3f, -0.8f }, { 1.0f, 1.0f, 1.0f }},
    };
    Vertex triVertsB[] = {
        {{  0.0f,  0.8f }, { 1.0f, 0.0f, 0.0f }},
        {{ -0.8f, -0.8f }, { 0.0f, 1.0f, 0.0f }},
        {{  0.8f, -0.8f }, { 0.0f, 0.0f, 1.0f }},
    };

    // Create two meshes, each assigned to a different pipeline
    int meshA = wgpuCreateMesh(pipelineGreen,  triVertsA, 3);
    int meshB = wgpuCreateMesh(pipelineOrange, triVertsB, 3);

    // 3) Example of setting a uniform on the pipelines
    //    (In a real engine, you'd have per-mesh or per-material uniform buffers)
    wgpuSetUniform(pipelineGreen,  1.0f);  // maybe some time-based param
    wgpuSetUniform(pipelineOrange, 2.0f);

    // Main loop
    while (g_Running) {
        // Windows message pump
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

		// todo: what about the command buffer (?)
		// todo: what about the gpu malloc/free regardless of pipeline (?)

        // Begin frame
        wgpuStartFrame();

        // Draw calls for each pipeline
        wgpuDrawPipeline(pipelineGreen);
        wgpuDrawPipeline(pipelineOrange);

        // End frame
        wgpuEndFrame();
    }

    // Cleanup
    wgpuShutdown();
    DestroyWindow(hwnd);

    return 0;
}
