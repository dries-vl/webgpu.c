#include "present.c"

#include <windows.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* PLATFORM LAYER API */
// todo: pass these by struct to present.c
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
#pragma endregion

#pragma region SETUP_TIME_PERIOD
typedef UINT (WINAPI *timeBeginPeriod_t)(UINT);
typedef UINT (WINAPI *timeEndPeriod_t)(UINT);
typedef UINT (WINAPI *timeGetDevCaps_t)(void *ptc, UINT cbtc);
timeBeginPeriod_t pTimeBeginPeriod = NULL;
timeEndPeriod_t   pTimeEndPeriod   = NULL;
timeGetDevCaps_t  pTimeGetDevCaps  = NULL;
typedef struct {
    UINT wPeriodMin;
    UINT wPeriodMax;
} TIMECAPS;
void load_winmm_functions() {
    HMODULE hWinmm = LoadLibraryA("winmm.dll");
    if (!hWinmm) {
        MessageBoxA(0, "Failed to load winmm.dll", "Error", MB_OK);
        ExitProcess(1);
    }

    pTimeBeginPeriod = (timeBeginPeriod_t)GetProcAddress(hWinmm, "timeBeginPeriod");
    pTimeEndPeriod   = (timeEndPeriod_t)GetProcAddress(hWinmm, "timeEndPeriod");
    pTimeGetDevCaps   = (timeGetDevCaps_t)GetProcAddress(hWinmm, "timeGetDevCaps");

    if (!pTimeBeginPeriod || !pTimeEndPeriod || !pTimeGetDevCaps) {
        MessageBoxA(0, "Failed to get winmm.dll functions", "Error", MB_OK);
        ExitProcess(1);
    }
}
#pragma endregion

#pragma region CYCLES
#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(__rdtsc)
inline unsigned long long read_cycle_count() {
    return __rdtsc();
}
#else
unsigned long long read_cycle_count() { // inline fails with gcc, but works with tcc (?)
    unsigned int lo, hi;
    __asm__ __volatile__ (
        "rdtsc"
        : "=a"(lo), "=d"(hi)
    );
    return ((unsigned long long)hi << 32) | lo;
}
#endif
#pragma endregion

#pragma region RAW INPUT SETUP
typedef BOOL (WINAPI *RegisterRawInputDevices_t)(PCRAWINPUTDEVICE, UINT, UINT);
typedef UINT (WINAPI *GetRawInputData_t)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
RegisterRawInputDevices_t pRegisterRawInputDevices = NULL;
GetRawInputData_t pGetRawInputData = NULL;
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
            load_winmm_functions(); // load windows dll to be able to call timeBeginPeriod for ms-accuracy sleep
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
                        gameState.player.velocity.y = 0.01f;
                    }
                    if (virtualKey == VK_TAB) {
                        ShowCursor(SHOW_CURSOR ^= 1);
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
                if (!SHOW_CURSOR) {
                    // Process mouse input
                    LONG dx = raw->data.mouse.lLastX;
                    LONG dy = raw->data.mouse.lLastY;
                    USHORT buttonFlags = raw->data.mouse.usButtonFlags;
                    // Handle mouse movement and button clicks
                    absolute_yaw(dx * 0.002f, view);
                    absolute_pitch(dy * 0.002f, view);
                }
            }
            free(lpb); // Free allocated memory
            break;
        }
        case WM_MOUSEMOVE: {
            if (!SHOW_CURSOR) {
                // Reset cursor to center every frame
                RECT rect;
                GetClientRect(hWnd, &rect);
                POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
                ClientToScreen(hWnd, &center);
                SetCursorPos(center.x, center.y);
            }
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

#pragma region FULLSCREEN
static int findClosestMode(DEVMODE *outMode)
{
    long long bestDist = 9223372036854775807;
    int foundAny = 0;
    int bestIndex = -1;

    printf("=== Enumerating all resolutions to find closest to %dx%d ===\n", WINDOW_WIDTH, WINDOW_HEIGHT);

    for (int i = 0; ; i++)
    {
        DEVMODE temp;
        ZeroMemory(&temp, sizeof(temp));
        temp.dmSize = sizeof(temp);

        if (!EnumDisplaySettingsEx(NULL, i, &temp, 0)) {
            // No more modes
            break;
        }

        if (temp.dmPelsWidth >= WINDOW_WIDTH
             && temp.dmPelsHeight >= WINDOW_HEIGHT
             && temp.dmBitsPerPel == outMode->dmBitsPerPel
             && temp.dmDisplayFrequency == outMode->dmDisplayFrequency) {
            long long dx = (long long)temp.dmPelsWidth  - WINDOW_WIDTH;
            long long dy = (long long)temp.dmPelsHeight - WINDOW_HEIGHT;
            long long dist = dx * dx + dy * dy; // squared distance

            // Log each candidate (optional)
            // printf("Mode %d: %lux%lu @ %lu Hz => dist=%lld\n",
            //        i, (unsigned long)temp.dmPelsWidth,
            //        (unsigned long)temp.dmPelsHeight,
            //        (unsigned long)temp.dmDisplayFrequency,
            //        dist);

            if (dist < bestDist) {
                bestDist = dist;
                *outMode = temp;
                foundAny = 1;
                bestIndex = i;
            }
        }
    }

    if (foundAny) {
        printf("Found closest mode at index %d: %lux%lu @ %lu Hz\n",
               bestIndex,
               (unsigned long)outMode->dmPelsWidth,
               (unsigned long)outMode->dmPelsHeight,
               (unsigned long)outMode->dmDisplayFrequency);
        return 1;
    } else {
        printf("No matching modes found at all!\n");
        return 0;
    }
}

void fullscreen_window(HWND hwnd)
{
    // 1) Save current display mode for fallback.
    DEVMODE currentMode = {0};
    currentMode.dmSize = sizeof(currentMode);
    if (!EnumDisplaySettingsEx(NULL, ENUM_CURRENT_SETTINGS, &currentMode, 0)) {
        MessageBoxA(hwnd, "Failed to get current display settings!", "Error", MB_OK);
        return;
    }
    printf("Current Display: %dx%d @ %d Hz\n",
           currentMode.dmPelsWidth, currentMode.dmPelsHeight, currentMode.dmDisplayFrequency);

    int targetWidth, targetHeight;

    if (FORCE_RESOLUTION) {
        // force resolution of screen to our preferred (will flicker)
        DEVMODE chosenMode = {0};
        chosenMode.dmSize = sizeof(chosenMode);
        chosenMode.dmBitsPerPel = currentMode.dmBitsPerPel;
        chosenMode.dmDisplayFrequency = currentMode.dmDisplayFrequency;
        chosenMode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
        if (!findClosestMode(&chosenMode))
            chosenMode = currentMode;

        targetWidth  = chosenMode.dmPelsWidth;
        targetHeight = chosenMode.dmPelsHeight;

        LONG res = ChangeDisplaySettingsEx(NULL, &chosenMode, NULL, CDS_FULLSCREEN, NULL);
        if (res != DISP_CHANGE_SUCCESSFUL) {
            printf("Failed to switch to requested display mode\n");
            ChangeDisplaySettingsEx(NULL, &currentMode, NULL, CDS_FULLSCREEN, NULL);
            return;
        }
    } else {
        // fit the window based on DPI (no resolution change, no flicker)
        targetWidth = GetSystemMetrics(SM_CXSCREEN);
        targetHeight = GetSystemMetrics(SM_CYSCREEN);

        printf("Calculated logical resolution: %dx%d\n", targetWidth, targetHeight);
    }

    WINDOW_WIDTH  = targetWidth;
    WINDOW_HEIGHT = targetHeight;
    
    if (FORCE_ASPECT_RATIO) {
        // Suppose we want to scale up 500x500 to fill as much vertical space as possible.
        float scaleX = (float)WINDOW_WIDTH  / (float)VIEWPORT_WIDTH;  // e.g. 1920 / 500 = 3.84
        float scaleY = (float)WINDOW_HEIGHT / (float)VIEWPORT_HEIGHT;  // e.g. 1080 / 500 = 2.16
        float scale  = (scaleX < scaleY) ? scaleX : scaleY; // pick smaller => 2.16

        // final scaled size
        VIEWPORT_WIDTH = VIEWPORT_WIDTH * scale; // 500 * 2.16 = 1080
        VIEWPORT_HEIGHT = VIEWPORT_HEIGHT * scale; // same => 1080

        // center it
        OFFSET_X = (WINDOW_WIDTH  - VIEWPORT_WIDTH) * 0.5f;  // (1920 - 1080)/2 = 420
        OFFSET_Y = (WINDOW_HEIGHT - VIEWPORT_HEIGHT) * 0.5f;  // (1080 - 1080)/2 = 0
    } else {
        VIEWPORT_WIDTH = WINDOW_WIDTH;
        VIEWPORT_HEIGHT = WINDOW_HEIGHT;
    }

    ASPECT_RATIO = (float) VIEWPORT_WIDTH / (float) VIEWPORT_HEIGHT;

    printf("Viewport: %dx%d, offset: %dx%d, aspect ratio: %4.2f\n",
           VIEWPORT_WIDTH, VIEWPORT_HEIGHT, OFFSET_X, OFFSET_Y, ASPECT_RATIO);

    // Set window style and position.
    SetWindowLongA(hwnd, GWL_STYLE, (WINDOWED ? WS_CAPTION | WS_SYSMENU : WS_POPUP) | WS_VISIBLE);
    SetWindowPos(hwnd, HWND_TOP, 0, 0,
                 (FORCE_RESOLUTION ? currentMode.dmPelsWidth : targetWidth),
                 (FORCE_RESOLUTION ? currentMode.dmPelsHeight : targetHeight),
                 SWP_SHOWWINDOW);
}
#pragma endregion

double current_time_ms() {
    static LARGE_INTEGER query_perf_result;
    static long long ticks_per_second = 0;
    if (!ticks_per_second) { QueryPerformanceFrequency(&query_perf_result); ticks_per_second = query_perf_result.QuadPart; }
    LARGE_INTEGER new_tick_count;
    QueryPerformanceCounter(&new_tick_count);
    double s = ((double) (new_tick_count.QuadPart) / (double) ticks_per_second);
    return s * 1000.0;
}

void sleep_ms(double ms) {
    static int setup = 0;
    if (!setup) pTimeBeginPeriod(1);
    unsigned long wait_time = (unsigned long) max(ms - 2.0, 1.0); // windows Sleep() sleeps about between 0 and 1.5ms longer than input, so to be sure sleep 2ms less than input
    Sleep((DWORD) wait_time);
}

void poll_inputs() {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            g_Running = false;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    #pragma region WINDOWS-SPECIFIC SETUP
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
        0, wc.lpszClassName, "WebGPU renderer!",
        WS_VISIBLE | WS_POPUP,  // WS_POPUP makes it borderless
        CW_USEDEFAULT, CW_USEDEFAULT,
        WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL, hInstance, NULL
    );
    assert(hwnd);

    if (FULLSCREEN) {
        if (FORCE_RESOLUTION) {
            // *info* set aware of DPI to force using actual screen resolution size
            HMODULE shcore = LoadLibrary("Shcore.dll"); // import explicitly because tcc doesn't know these headers
            if (shcore) {
                typedef HRESULT (WINAPI *SetProcessDpiAwareness_t)(int);
                SetProcessDpiAwareness_t SetProcessDpiAwareness = (SetProcessDpiAwareness_t) GetProcAddress(shcore, "SetProcessDpiAwareness");
                if (SetProcessDpiAwareness) {
                    #define PROCESS_PER_MONITOR_DPI_AWARE 2
                    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
                }
                FreeLibrary(shcore);
            }
        }
        fullscreen_window(hwnd);
    }
    RECT screen;
    GetClientRect(GetDesktopWindow(), &screen);
    int x = (screen.right - WINDOW_WIDTH) / 2;
    int y = (screen.bottom - WINDOW_HEIGHT) / 2;
    SetWindowPos(hwnd, NULL, x, y, WINDOW_WIDTH, WINDOW_HEIGHT, SWP_NOZORDER | SWP_SHOWWINDOW); // put window in middle of screen
    ShowCursor(FALSE); // hide the cursor
    #pragma endregion

    void *context = createGPUContext(hInstance, hwnd, WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_WIDTH, WINDOW_HEIGHT);

    struct Platform p = {
        .current_time_ms = current_time_ms,
        .map_file = map_file,
        .unmap_file = unmap_file,
        .sleep_ms = sleep_ms,
        .poll_inputs = poll_inputs
    };

    /* MAIN LOOP */
    // todo: put present.c in a dll so we can reload it here -> also present.h file
    // todo: put game.c in a dll so we can reload it here -> also a game.h file
    // todo: put all the binary outputs from /data in a single /bin
    // todo: put all the /bin and /shaders and everything needed to run together
        // -> then we can fetch it all at once in browser version
        // -> then we can take all of that and put it into one big executable later
    // todo: put all the stb headers used by scripts in /data in a single /lib folder
    // todo: put all the /data bat files higher together (maybe in root with run.bat)
    // todo: create /src folder
    while (g_Running) tick(&p, context);
    
    return 0;
}