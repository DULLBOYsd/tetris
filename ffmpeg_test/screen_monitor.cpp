#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
using namespace std;

// 屏幕捕获区域尺寸（电脑分辨率：2560×1600，但是还要加上副屏，而副屏的物理大小与主屏一样，注意偏移（原大小的1.5倍是主屏，但是物理显示上被拓展了））
const int CAPTURE_WIDTH  = 3760;
const int CAPTURE_HEIGHT = 800;
// 像素格式为 BGRA，每个像素 4 个字节
const int BYTES_PER_PIXEL = 4;
// 一帧的字节大小
const int FRAME_SIZE = CAPTURE_WIDTH * CAPTURE_HEIGHT * BYTES_PER_PIXEL;

// FFmpeg 进程句柄
HANDLE g_hChildStd_OUT_Rd = NULL;
HANDLE g_hChildStd_OUT_Wr = NULL;
PROCESS_INFORMATION g_pi = { 0 };

/* ===== FFmpeg部分 ===== */ 
// 启动 FFmpeg 进程，并重定向其标准输出到管道
bool StartFFmpegCapture() {
    SECURITY_ATTRIBUTES sa = { sizeof(sa) };
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    // 创建管道
    if (!CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &sa, 0)) {
        std::cerr << "CreatePipe failed: " << GetLastError() << std::endl;
        return false;
    }
    // 读取端不被子进程继承
    SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0);

    // 用 std::wstring 逐步拼命令，避免指针 + wstring 混淆
    std::wstring cmd;

    // 注意：这里用双引号把 ffmpeg 路径括起来，防止路径含空格
    cmd += L"\"D:\\ffmpeg\\ffmpeg-master-latest-win64-gpl-shared\\bin\\ffmpeg.exe\" ";
    cmd += L"-f gdigrab -framerate 30 -i desktop ";
    // crop 参数不要加引号，Windows 会原样传给 ffmpeg
    cmd += L"-vf crop=";
    cmd += std::to_wstring(CAPTURE_WIDTH);
    cmd += L":";
    cmd += std::to_wstring(CAPTURE_HEIGHT);
    cmd += L":2560:0 ";
    cmd += L"-f rawvideo -pix_fmt bgra -";

    STARTUPINFOW si = { sizeof(si) };
    si.hStdOutput = g_hChildStd_OUT_Wr;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    ZeroMemory(&g_pi, sizeof(g_pi));

    if (!CreateProcessW(
            NULL,
            &cmd[0],           // 命令行（必须可写）
            NULL, NULL,
            TRUE,              // 句柄继承
            CREATE_NO_WINDOW,
            NULL, NULL,
            &si, &g_pi)) {
        std::cerr << "CreateProcess failed: " << GetLastError() << std::endl;
        CloseHandle(g_hChildStd_OUT_Rd);
        CloseHandle(g_hChildStd_OUT_Wr);
        g_hChildStd_OUT_Rd = NULL;
        g_hChildStd_OUT_Wr = NULL;
        return false;
    }

    // 父进程不需要写端
    CloseHandle(g_hChildStd_OUT_Wr);
    g_hChildStd_OUT_Wr = NULL;

    std::cout << "FFmpeg started, PID: " << g_pi.dwProcessId << std::endl;
    return true;
}

// 从管道中读取一帧原始像素数据
bool ReadFrameFromPipe(std::vector<uint8_t>& frameBuffer) {
    frameBuffer.resize(FRAME_SIZE);
    DWORD totalRead = 0;

    while (totalRead < (DWORD)FRAME_SIZE) {
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(
            g_hChildStd_OUT_Rd,
            frameBuffer.data() + totalRead,
            FRAME_SIZE - totalRead,
            &bytesRead,
            NULL);

        if (!ok) {
            std::cerr << "ReadFile failed: " << GetLastError() << std::endl;
            return false;
        }
        if (bytesRead == 0) {
            // 管道关闭，FFmpeg 可能已退出
            return false;
        }
        totalRead += bytesRead;
    }
    return true;
}

// 停止录屏进程
void StopFFmpegCapture() {
    if (g_pi.hProcess) {
        TerminateProcess(g_pi.hProcess, 0);
        WaitForSingleObject(g_pi.hProcess, 1000);
        CloseHandle(g_pi.hProcess);
        CloseHandle(g_pi.hThread);
        g_pi.hProcess = NULL;
        g_pi.hThread = NULL;
    }
    if (g_hChildStd_OUT_Rd) {
        CloseHandle(g_hChildStd_OUT_Rd);
        g_hChildStd_OUT_Rd = NULL;
    }
    if (g_hChildStd_OUT_Wr) {
        CloseHandle(g_hChildStd_OUT_Wr);
        g_hChildStd_OUT_Wr = NULL;
    }
}

// 从 buffer 里取指定坐标的像素颜色
COLORREF GetPixelFromBuffer(const std::vector<uint8_t>& buffer, int x, int y) {
    int index = (y * CAPTURE_WIDTH + x) * BYTES_PER_PIXEL;
    uint8_t b = buffer[index];
    uint8_t g = buffer[index + 1];
    uint8_t r = buffer[index + 2];
    return RGB(r, g, b);
}

/* ===== 模拟按键输入 ===== */
// 按下并松开一个键（虚拟键码）
void SendKey(WORD vk) {
	INPUT in[2] = {};
	in[0].type = INPUT_KEYBOARD;
  in[0].ki.wVk = vk;
  in[1].type = INPUT_KEYBOARD;
  in[1].ki.wVk = vk;
  in[1].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(2, in, sizeof(INPUT));
}

// 按下并松开一个键（扫描码，兼容游戏）
void SendKeyScan(WORD vk) {
  WORD vsc = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
  INPUT in[2] = {};
  in[0].type = INPUT_KEYBOARD;
  in[0].ki.wScan = vsc;
  in[0].ki.dwFlags = KEYEVENTF_SCANCODE;
  in[1].type = INPUT_KEYBOARD;
  in[1].ki.wScan = vsc;
  in[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
  SendInput(2, in, sizeof(INPUT));
}

// 组合键: ctrl + 指定键
void SendCtrl(WORD vk) {
  INPUT in[4] = {};
  in[0].type = INPUT_KEYBOARD;
  in[0].ki.wVk = VK_CONTROL;
  in[1].type = INPUT_KEYBOARD;
  in[1].ki.wVk = vk;
  in[2].type = INPUT_KEYBOARD;
  in[2].ki.wVk = vk;
  in[2].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(4, in, sizeof(INPUT));
}

// 输入Unicode字符
void SendUnicodeChar(wchar_t ch) {
  INPUT in[2] = {};
  in[0].type = INPUT_KEYBOARD;
  in[0].ki.wScan = ch;
  in[0].ki.dwFlags = KEYEVENTF_UNICODE;
  in[1].type = INPUT_KEYBOARD;
  in[1].ki.wScan = ch;
  in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
  SendInput(2, in, sizeof(INPUT));
}

// 发送Unicode字符串
void SendUnicodeString(const wstring& s) {
  for (wchar_t c : s) {
    SendUnicodeChar(c);
    Sleep(10);
  }
}

int main() {
    SetProcessDPIAware(); // 避免 DPI 缩放导致坐标问题

    if (!StartFFmpegCapture()) {
        return 1;
    }

    std::vector<uint8_t> frameBuffer;
    int frameCount = 0;

    while (true) {
        if (!ReadFrameFromPipe(frameBuffer)) {
            std::cerr << "Read frame failed, ffmpeg may have exited." << std::endl;
            break;
        }

        // 检测 (100, 100) 是否为红色
        COLORREF color = GetPixelFromBuffer(frameBuffer, 3560, 700);
        int r = GetRValue(color);
        int g = GetGValue(color);
        int b = GetBValue(color);

        if (r > 200 && g < 50 && b < 50) {
            std::cout << "Red pixel detected! frame: " << frameCount << std::endl;
            // 这里可以调用 SendInput
            SendKey('A');
        }

        frameCount++;
        if (frameCount % 30 == 0) {
            std::cout << "Processed " << frameCount << " frames..." << std::endl;
        }
    }

    StopFFmpegCapture();
    return 0;
}
