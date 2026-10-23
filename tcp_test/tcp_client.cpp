#include <iostream>
#include <winsock2.h>
#include <string>
using namespace std;

int main() {
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);

  // 1.创建socket套接字
  SOCKET client_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (client_socket == INVALID_SOCKET) {
    cout << "create socket failed!" << endl;
    return -1;
  }

  // 2.连接服务器
  struct sockaddr_in target;
  target.sin_family = AF_INET;
  target.sin_port = htons(8080);
  target.sin_addr.s_addr = inet_addr("127.0.0.1");

  if (connect(client_socket, (struct sockaddr*)&target, sizeof(target)) == -1) {
    cout << "connect server failed!" << endl;
    closesocket(client_socket);
    return -1;
  }

  // 3.开始通讯（示例：回旋服务器）send recv
  while (1) {
    char buffer[1024] = { 0 };
    cout << "please enter: ";
    cin >> buffer;
    send(client_socket, buffer, strlen(buffer), 0);

    char rbuffer[1024] = { 0 };
    int ret = recv(client_socket, rbuffer, 1024, 0);
    if (ret <= 0) break;
    cout << rbuffer << endl;
  }

  // 4.关闭连接
  closesocket(client_socket);

  return 0;
}
