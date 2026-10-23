/*
  1.创建socket套接字
  2.给这个socket绑定一个端口号
  3.给这个socket开启监听属性
  4.等待客户端连接
  5.开始通讯
  6.关闭连接
*/

#include <iostream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
using namespace std;

int main() {
  // windows上使用网络功能需要开启网络权限
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
  
  // 1.创建socket套接字
  // SOCKET socket(:!g++ % -o %< -lws2_32
    // int at, // 协议地址簇 IPV4/IPV6 AF_INET/AF_INET6
    // int type, // 类型 流式协议 帧式协议 SOCK_STREAM/SOCK_DGRAM
    // int protocol // 保护协议 0
  // );
  SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_socket == INVALID_SOCKET) {
    cout << "create listen socket failed! errcode: " << GetLastError() << endl;
    return -1;
  }
  
  // 2.给这个socket绑定一个端口号
  // struct sockaddr_in {
    // ADDRESS_FAMILY sin_family; // 协议地址簇
    // USHORT sin_port; // 端口号
    // IN_ADDR sin_addr; // IP地址
    // CHAR sin_zero[8]; // 保留字节
  // };
  struct sockaddr_in local = { 0 };
  local.sin_family = AF_INET;
  local.sin_port = htons(8080); // 大小端的转换问题
  // local.sin_addr.s_addr = htonl(INADDR_ANY);
  local.sin_addr.s_addr = inet_addr("0.0.0.0"); // 字符串IP地址转换成整数IP
  
  // int bind(
    // SOCKET s,
    // const struct sockaddr *name,
    // int namelen
  // );
  if (::bind(listen_socket, (struct sockaddr*)&local, sizeof(local)) == -1) {
    cout << "bind socket failed! errcode: " << GetLastError();
    return -1;
  }

  // 3.给这个socket开启监听属性
  // int listen(
    // SOCKET s,
    // int backlog
  // );
  if (listen(listen_socket, 10)) {
    cout << "start listen failed! errcode: " << GetLastError() << endl;
    return -1;
  }

  // 4.等待客户端连
  // 返回的客户端socket才是跟客户端可以通讯的一个socket
  // 阻塞函数，等到有客户端连接进来就断开，然后返回，否则就阻塞
  // SOCKET accept(
    // SOCKET s, // 监听socket
    // struct sockaddr* addr, // 客户端的IP地址和端口号
    // int* addrlen // 结构的大小
  // );

  while (1) {
    SOCKET client_socket = accept(listen_socket, NULL, NULL);
    if (client_socket == INVALID_SOCKET) continue;
  
    while (1) {
      // 5.开始通讯(B/S) 
      char buffer[1024] = { 0 };
      // int recv(
        // SOCKET s, // 客户端socket
        // char* buf, // 接受的数据存到哪里
        // int len, // 接受的长度
        // int flags // 0
      // );
      int ret = recv(client_socket, buffer, 1024, 0);
      if (ret <= 0) break;
  
      cout << buffer << endl;
  
      send(client_socket, buffer, strlen(buffer), 0);
    }
    // 6.关闭连接
    closesocket(client_socket);
  }

}
