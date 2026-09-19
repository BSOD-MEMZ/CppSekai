// capwin.c -> build/capwin.exe
//
// 把 SDL_app 窗口**在屏幕上的客户区**截一张图存成 BMP（Pillow 能直接读）。
// 用途：拖动窗口时，主循环到底有没有真的在刷新屏幕 —— 游戏自己的 --screenshot 抓的是 GL
// 缓冲区（那是"我们画了什么"），这个抓的是"屏幕上显示了什么"，两者是不同的问题。
//
//   zig cc -x c -std=c11 -O2 -s .workbuddy/tools/capwin.c -lgdi32 -luser32 -o build/capwin.exe
//   capwin.exe out.bmp
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: capwin.exe <out.bmp>\n");
        return 2;
    }
    HWND hwnd = FindWindowW(L"SDL_app", NULL);
    if (hwnd == NULL) {
        fprintf(stderr, "no SDL_app window\n");
        return 1;
    }
    RECT wr, cr;
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);
    POINT origin = {0, 0};
    ClientToScreen(hwnd, &origin); // 客户区左上角在屏幕坐标系里的位置
    const int w = cr.right - cr.left;
    const int h = cr.bottom - cr.top;

    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // 负数 = 自上而下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL || bits == NULL) {
        fprintf(stderr, "CreateDIBSection failed\n");
        return 1;
    }
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, origin.x, origin.y, SRCCOPY);
    SelectObject(mem, old);

    const int rowBytes = w * 4;
    const int imageBytes = rowBytes * h;
    const int fileBytes = 14 + 40 + imageBytes;
    FILE* f = fopen(argv[1], "wb");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    unsigned char header[14 + 40];
    ZeroMemory(header, sizeof(header));
    header[0] = 'B';
    header[1] = 'M';
    memcpy(header + 2, &fileBytes, 4);
    const int dataOff = 14 + 40;
    memcpy(header + 10, &dataOff, 4);
    const int dibSize = 40;
    memcpy(header + 14, &dibSize, 4);
    memcpy(header + 18, &w, 4);
    int negH = -h;
    memcpy(header + 22, &negH, 4);
    const short planes = 1, bpp = 32;
    memcpy(header + 26, &planes, 2);
    memcpy(header + 28, &bpp, 2);
    memcpy(header + 34, &imageBytes, 4);
    fwrite(header, 1, sizeof(header), f);
    fwrite(bits, 1, (size_t)imageBytes, f);
    fclose(f);

    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    printf("captured %dx%d at screen %ld,%ld (window %ld,%ld-%ld,%ld)\n", w, h, origin.x, origin.y,
        wr.left, wr.top, wr.right, wr.bottom);
    fflush(stdout);
    return 0;
}
