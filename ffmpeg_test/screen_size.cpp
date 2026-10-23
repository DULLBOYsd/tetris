#include <windows.h>
#include <iostream>

int main() {
  SetProcessDPIAware(); // 设置为物理感知

  int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
  int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
  int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

  std::cout << "Virtual desktop origin: (" << vx << "," << vy << ")\n";
  std::cout << "Virtual desktop size:   " << vw << " x " << vh << "\n";

  int sw = GetSystemMetrics(SM_CXSCREEN);
  int sh = GetSystemMetrics(SM_CYSCREEN);
  std::cout << "Primary screen size:    " << sw << " x " << sh << "\n";

  return 0;
}
