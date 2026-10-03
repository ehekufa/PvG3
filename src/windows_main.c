/* Standalone Windows desktop host for the shared C game and LVGL UI.
 * Win32/GDI supplies a resizable 16:9 window, mouse input and local saves. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>

#include "game.h"
#include "lvgl_ui.h"
#include "online_net.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define APP_CLASS L"PvG3DesktopWindow"
#define PATH_CAP 32768

static HWND window_handle;
static int running = 1, focused = 1, ui_ready;
static int mouse_active, mouse_via_lvgl, legacy_down_phase = -1;
static int legacy_down_x, legacy_down_y;
static int key_left, key_right, key_jump, key_trigger;
static uint32_t game_pixels[GAME_W * GAME_H];
static uint32_t dib_pixels[GAME_W * GAME_H];
static BITMAPINFO dib_info;
static wchar_t executable_dir[PATH_CAP];
static wchar_t save_dir[PATH_CAP];
static wchar_t campaign_file[PATH_CAP];
static wchar_t garden_file[PATH_CAP];
static wchar_t music_file[PATH_CAP];

static int legacy_renderer_phase(int phase) {
    return phase == GAME_MENU || phase == GAME_INTRO || phase == GAME_PLAY ||
           phase == GAME_LEVEL_CLEAR || phase == GAME_WIN || phase == GAME_LOSE ||
           phase == GAME_GARDEN || phase == GAME_BOOK || phase == GAME_SELECT;
}

static int join_path(wchar_t *out, size_t cap, const wchar_t *dir,
                     const wchar_t *name) {
    if (!out || !cap) return 0;
    out[0] = 0;
    int n = swprintf(out, cap, L"%ls\\%ls", dir, name);
    if (n <= 0 || (size_t)n >= cap) {
        out[0] = 0;
        return 0;
    }
    return 1;
}

static int copy_wide(wchar_t *out, size_t cap, const wchar_t *text) {
    if (!out || !cap || !text) return 0;
    size_t n = wcslen(text);
    if (n >= cap) { out[0] = 0; return 0; }
    memcpy(out, text, (n + 1) * sizeof *out);
    return 1;
}

static int append_wide(wchar_t *out, size_t cap, const wchar_t *text) {
    if (!out || !cap || !text) return 0;
    size_t used = wcslen(out), n = wcslen(text);
    if (used >= cap || n >= cap - used) return 0;
    memcpy(out + used, text, (n + 1) * sizeof *out);
    return 1;
}

static void initialize_paths(void) {
    DWORD n = GetModuleFileNameW(NULL, executable_dir, PATH_CAP);
    if (!n || n >= PATH_CAP) executable_dir[0] = 0;
    else {
        wchar_t *slash = wcsrchr(executable_dir, L'\\');
        wchar_t *forward = wcsrchr(executable_dir, L'/');
        if (!slash || (forward && forward > slash)) slash = forward;
        if (slash) *slash = 0;
        else executable_dir[0] = 0;
    }

    DWORD env_len = GetEnvironmentVariableW(L"LOCALAPPDATA", save_dir, PATH_CAP);
    if (!env_len || env_len >= PATH_CAP) {
        if (executable_dir[0]) copy_wide(save_dir, PATH_CAP, executable_dir);
        else {
            DWORD cwd_len = GetCurrentDirectoryW(PATH_CAP, save_dir);
            if (!cwd_len || cwd_len >= PATH_CAP) save_dir[0] = 0;
        }
    }
    if (!save_dir[0]) copy_wide(save_dir, PATH_CAP, L".");
    size_t dir_len = wcslen(save_dir);
    if (dir_len > 0 && save_dir[dir_len - 1] != L'\\' &&
        save_dir[dir_len - 1] != L'/' &&
        !append_wide(save_dir, PATH_CAP, L"\\"))
        save_dir[0] = 0;
    if (save_dir[0] && append_wide(save_dir, PATH_CAP, L"PvG3"))
        CreateDirectoryW(save_dir, NULL);
    else
        save_dir[0] = 0;
    if (save_dir[0]) {
        join_path(campaign_file, PATH_CAP, save_dir, L"pvg3-campaign.v1");
        join_path(garden_file, PATH_CAP, save_dir, L"pvg3-garden.v1");
    }
    if (executable_dir[0]) {
        int n = swprintf(music_file, PATH_CAP,
                         L"%ls\\assets\\music\\kirill-pond-loop.wav",
                         executable_dir);
        if (n <= 0 || n >= PATH_CAP) music_file[0] = 0;
    }
}

static void campaign_load(void) {
    FILE *file = _wfopen(campaign_file, L"rb");
    if (!file) return;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return; }
    long length = ftell(file);
    if (length < 0 || length > 65536 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return;
    }
    uint8_t *bytes = (uint8_t *)malloc((size_t)length ? (size_t)length : 1);
    if (!bytes) { fclose(file); return; }
    size_t read_count = fread(bytes, 1, (size_t)length, file);
    int extra = fgetc(file);
    fclose(file);
    if (read_count == (size_t)length && extra == EOF && length > 0)
        game_save_import(bytes, read_count);
    free(bytes);
}

static void garden_load(void) {
    FILE *file = _wfopen(garden_file, L"rb");
    if (!file) return;
    uint8_t cells[GAME_GARDEN_CELLS];
    size_t count = fread(cells, 1, sizeof cells, file);
    int extra = fgetc(file);
    fclose(file);
    if (count == sizeof cells && extra == EOF) game_garden_import(cells);
}

static void atomic_write(const wchar_t *path, const void *bytes, size_t size) {
    wchar_t temporary[PATH_CAP];
    int n = swprintf(temporary, PATH_CAP, L"%ls.tmp", path);
    if (n <= 0 || n >= PATH_CAP) return;
    FILE *file = _wfopen(temporary, L"wb");
    if (!file) return;
    size_t written = fwrite(bytes, 1, size, file);
    int closed = fclose(file);
    if (written != size || closed != 0 ||
        !MoveFileExW(temporary, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        DeleteFileW(temporary);
}

static void campaign_save(void) {
    size_t size = game_save_size();
    if (!size || size > 65536) return;
    uint8_t *bytes = (uint8_t *)malloc(size);
    if (!bytes) return;
    if (game_save_export(bytes, size)) atomic_write(campaign_file, bytes, size);
    free(bytes);
}

static void garden_save(void) {
    uint8_t cells[GAME_GARDEN_CELLS];
    game_garden_export(cells);
    atomic_write(garden_file, cells, sizeof cells);
}

static void save_all(void) {
    campaign_save();
    garden_save();
}

static void set_music(int play) {
    if (!play || !music_file[0]) {
        PlaySoundW(NULL, NULL, 0);
        return;
    }
    PlaySoundW(music_file, NULL,
               SND_ASYNC | SND_FILENAME | SND_LOOP | SND_NODEFAULT);
}

static void game_viewport(int client_w, int client_h, int *left, int *top,
                          int *width, int *height) {
    if (client_w < 1) client_w = GAME_W;
    if (client_h < 1) client_h = GAME_H;
    if ((int64_t)client_w * GAME_H > (int64_t)client_h * GAME_W) {
        *height = client_h;
        *width = (int)((int64_t)client_h * GAME_W / GAME_H);
    } else {
        *width = client_w;
        *height = (int)((int64_t)client_w * GAME_H / GAME_W);
    }
    if (*width < 1) *width = 1;
    if (*height < 1) *height = 1;
    *left = (client_w - *width) / 2;
    *top = (client_h - *height) / 2;
}

static int screen_to_game(int sx, int sy, int clamp_to_canvas, int *x, int *y) {
    RECT client;
    if (!window_handle || !GetClientRect(window_handle, &client)) return 0;
    int left, top, width, height;
    game_viewport(client.right - client.left, client.bottom - client.top,
                  &left, &top, &width, &height);
    int right = left + width, bottom = top + height;
    if (!clamp_to_canvas && (sx < left || sy < top || sx >= right || sy >= bottom))
        return 0;
    if (sx < left) sx = left;
    if (sy < top) sy = top;
    if (sx >= right) sx = right - 1;
    if (sy >= bottom) sy = bottom - 1;
    *x = (sx - left) * GAME_W / width;
    *y = (sy - top) * GAME_H / height;
    if (*x < 0) *x = 0;
    if (*x >= GAME_W) *x = GAME_W - 1;
    if (*y < 0) *y = 0;
    if (*y >= GAME_H) *y = GAME_H - 1;
    return 1;
}

static void mouse_press(int sx, int sy) {
    int x, y;
    if (!screen_to_game(sx, sy, 0, &x, &y)) return;
    SetCapture(window_handle);
    mouse_active = 1;
    mouse_via_lvgl = 0;
    legacy_down_phase = -1;
    int phase = game_phase();
    int was_garden = phase == GAME_GARDEN;
    if (ui_ready && !legacy_renderer_phase(phase))
        mouse_via_lvgl = lvgl_ui_pointer(x, y, 1);
    if (!mouse_via_lvgl) {
        legacy_down_phase = phase;
        legacy_down_x = x;
        legacy_down_y = y;
        game_input_press(x, y);
    }
    if (was_garden) garden_save();
    campaign_save();
}

static void mouse_move(int sx, int sy) {
    if (!mouse_active) return;
    int x, y;
    if (!screen_to_game(sx, sy, 1, &x, &y)) return;
    if (ui_ready && mouse_via_lvgl) lvgl_ui_move(x, y);
}

static void mouse_release(int sx, int sy) {
    if (!mouse_active) return;
    int x = 0, y = 0;
    if (!screen_to_game(sx, sy, 1, &x, &y)) {
        mouse_active = mouse_via_lvgl = 0;
        legacy_down_phase = -1;
        ReleaseCapture();
        return;
    }
    int was_garden = game_phase() == GAME_GARDEN;
    int handled = ui_ready && mouse_via_lvgl && lvgl_ui_pointer(x, y, 0);
    if (!handled && legacy_down_phase == game_phase() &&
        game_legacy_plant_drag(legacy_down_phase,
                               legacy_down_x, legacy_down_y, x, y))
        game_input_press(x, y);
    if (!handled) game_input_release(x, y);
    mouse_active = mouse_via_lvgl = 0;
    legacy_down_phase = -1;
    ReleaseCapture();
    if (was_garden) garden_save();
    campaign_save();
}

static void mouse_cancel(void) {
    if (!mouse_active) return;
    int handled = ui_ready && mouse_via_lvgl && lvgl_ui_cancel();
    if (!handled) game_input_release(0, 0);
    mouse_active = mouse_via_lvgl = 0;
    legacy_down_phase = -1;
    if (GetCapture() == window_handle) ReleaseCapture();
}

static void update_custom_keys(void) {
    int horizontal = key_left == key_right ? 0 : key_left ? -1 : 1;
    game_custom_control(horizontal, key_jump, key_trigger);
}

static void convert_pixels(void) {
    for (size_t i = 0; i < (size_t)GAME_W * GAME_H; ++i) {
        uint32_t rgba = game_pixels[i];
        /* Game pixels are RGBA in memory; a BI_RGB DIB is BGRA/BGRX. */
        dib_pixels[i] = ((rgba & 0x000000ffu) << 16) |
                        (rgba & 0x0000ff00u) |
                        ((rgba & 0x00ff0000u) >> 16);
    }
}

static void render_frame(float dt) {
    int phase = game_phase();
    int lvgl_screen = ui_ready && !legacy_renderer_phase(phase);
    game_set_lvgl_ui(lvgl_screen);
    int fullscreen = lvgl_screen && lvgl_ui_fullscreen(phase);
    if (fullscreen) game_tick(dt, NULL);
    else game_tick(dt, game_pixels);
    if (fullscreen && !lvgl_ui_fullscreen(game_phase())) game_tick(0, game_pixels);
    if (ui_ready && !legacy_renderer_phase(game_phase()))
        lvgl_ui_frame(dt, game_pixels);
    convert_pixels();
    InvalidateRect(window_handle, NULL, FALSE);
    UpdateWindow(window_handle);
}

static void paint_frame(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);
    FillRect(dc, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
    if (dib_info.bmiHeader.biSize) {
        int left, top, width, height;
        game_viewport(client.right - client.left, client.bottom - client.top,
                      &left, &top, &width, &height);
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, left, top, width, height, 0, 0, GAME_W, GAME_H,
                      dib_pixels, &dib_info, DIB_RGB_COLORS, SRCCOPY);
    }
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message,
                                    WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_PAINT:
        paint_frame(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_SETFOCUS:
        focused = 1;
        set_music(1);
        return 0;
    case WM_KILLFOCUS:
        focused = 0;
        mouse_cancel();
        save_all();
        set_music(0);
        return 0;
    case WM_LBUTTONDOWN:
        mouse_press(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
    case WM_MOUSEMOVE:
        mouse_move(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
    case WM_LBUTTONUP:
        mouse_release(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
    case WM_CAPTURECHANGED:
        if (mouse_active) mouse_cancel();
        return 0;
    case WM_KEYDOWN:
        if (!(lparam & (1L << 30))) {
            if (wparam == VK_LEFT || wparam == 'A') key_left = 1;
            if (wparam == VK_RIGHT || wparam == 'D') key_right = 1;
            if (wparam == VK_SPACE || wparam == VK_UP || wparam == 'W') key_jump = 1;
            if (wparam == 'E' || wparam == VK_SHIFT) key_trigger = 1;
            update_custom_keys();
        }
        return 0;
    case WM_KEYUP:
        if (wparam == VK_LEFT || wparam == 'A') key_left = 0;
        if (wparam == VK_RIGHT || wparam == 'D') key_right = 0;
        if (wparam == VK_SPACE || wparam == VK_UP || wparam == 'W') key_jump = 0;
        if (wparam == 'E' || wparam == VK_SHIFT) key_trigger = 0;
        update_custom_keys();
        return 0;
    case WM_CLOSE:
        save_all();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        running = 0;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous,
                   LPSTR command_line, int show_command) {
    (void)previous;
    (void)command_line;
    SetProcessDPIAware();
    initialize_paths();

    game_init();
    campaign_load();
    garden_load();
    ui_ready = lvgl_ui_init();
    game_set_lvgl_ui(ui_ready && !legacy_renderer_phase(game_phase()));

    memset(&dib_info, 0, sizeof dib_info);
    dib_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dib_info.bmiHeader.biWidth = GAME_W;
    dib_info.bmiHeader.biHeight = -GAME_H; /* top-down */
    dib_info.bmiHeader.biPlanes = 1;
    dib_info.bmiHeader.biBitCount = 32;
    dib_info.bmiHeader.biCompression = BI_RGB;

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = APP_CLASS;
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"Не удалось создать окно PvG3.", L"PvG3",
                    MB_OK | MB_ICONERROR);
        goto cleanup;
    }

    RECT rect = {0, 0, GAME_W, GAME_H};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    window_handle = CreateWindowExW(0, APP_CLASS,
        L"Растения против гусей 3", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
        rect.bottom - rect.top, NULL, NULL, instance, NULL);
    if (!window_handle) {
        MessageBoxW(NULL, L"Не удалось открыть окно PvG3.", L"PvG3",
                    MB_OK | MB_ICONERROR);
        goto cleanup;
    }
    ShowWindow(window_handle, show_command);
    UpdateWindow(window_handle);
    focused = 1;
    set_music(1);

    LARGE_INTEGER frequency, last, last_save, now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&last);
    last_save = last;
    MSG msg;
    while (running) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = 0; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;
        QueryPerformanceCounter(&now);
        if (focused && !IsIconic(window_handle)) {
            float dt = (float)(now.QuadPart - last.QuadPart) /
                       (float)frequency.QuadPart;
            if (dt < 0) dt = 0;
            if (dt > 0.05f) dt = 0.05f;
            last = now;
            render_frame(dt);
            if (now.QuadPart - last_save.QuadPart >= frequency.QuadPart) {
                if (game_phase() == GAME_PLAY) campaign_save();
                last_save = now;
            }
            MsgWaitForMultipleObjects(0, NULL, FALSE, 12, QS_ALLINPUT);
        } else {
            last = now;
            MsgWaitForMultipleObjects(0, NULL, FALSE, 50, QS_ALLINPUT);
        }
    }

cleanup:
    save_all();
    on_net_shutdown();
    set_music(0);
    if (ui_ready) lvgl_ui_shutdown();
    return 0;
}
