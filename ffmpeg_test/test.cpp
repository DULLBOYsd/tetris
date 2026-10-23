#include <windows.h>
#include <iostream>
#include <string> 
#include <vector>
#include <cstdint>
using namespace std;

const int CAPTURE_WIDTH = 3760;
const int CAPTURE_HEIGHT = 800;
const int BYTES_PER_PIXEL = 4;
const int FRAME_SIZE = CAPTURE_WIDTH * CAPTURE_HEIGHT * BYTES_PER_PIXEL;

HANDLE g_hChildStd_OUT_Rd = NULL;
HANDLE g_hChildStd_OUT_Wr = NULL;
PROCESS_INFORMATION g_pi = { 0 };

bool StartFFmpegCapture() {
  SECURITY_ATTRIBUTES sa = { sizeof(sa) };
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = NULL;

  if (!CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &sa, 0)) {
    cerr << "CreatePipe failed: " << GetLastError() << endl;
    return false;
  }

  SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0);

  wstring cmd;

  cmd += L"\"D:\\ffmpeg\\ffmpeg-master-latest-win64-gpl-shared\\bin\\ffmpeg.exe\" ";
  cmd += L"-f gdigrab -framerate 30 -i desktop ";
  cmd += L"-vf crop=";
  cmd += to_wstring(CAPTURE_WIDTH);
  cmd += L":";
  cmd += to_wstring(CAPTURE_HEIGHT);
  cmd += L":2560:0 ";
  cmd += L"-f rawvideo -pix_fmt bgra -";
  
  STARTUPINFOW si = { sizeof(si) };
  si.hStdOutput = g_hChildStd_OUT_Wr;
  si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;

  ZeroMemory(&g_pi, sizeof(g_pi));

  if (!CreateProcessW(NULL, &cmd[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &g_pi)) {
    cerr << "CreateProcess failed: " << GetLastError() << endl;
    CloseHandle(g_hChildStd_OUT_Rd);
    CloseHandle(g_hChildStd_OUT_Wr);
    g_hChildStd_OUT_Rd = NULL;
    g_hChildStd_OUT_Wr = NULL;
    return false;
  }

  CloseHandle(g_hChildStd_OUT_Wr);
  g_hChildStd_OUT_Wr = NULL;

  cout << "FFmpeg started, PID: " << g_pi.dwProcessId << endl;
  return true;
}

bool ReadFrameFromPipe(vector<uint8_t>& frameBuffer) {
  frameBuffer.resize(FRAME_SIZE);
  DWORD totalRead = 0;

  while (totalRead < (DWORD)FRAME_SIZE) {
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(g_hChildStd_OUT_Rd, frameBuffer.data() + totalRead, FRAME_SIZE - totalRead, &bytesRead, NULL);

    if (!ok) {
      cerr << "ReadFile failed: " << GetLastError() << endl;
      return false;
    }

    if (bytesRead == 0) retrun false;
    totalRead += bytesRead;
  }
  return true;
}

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

COLORREF GetPixelFromBuffer(const vector<uint8_t>& buffer, int x, int y) {
  int index = (y * CAPTURE_WIDTH + x) * BYTES_PER_PIXEL;


