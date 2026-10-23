/*
  消息格式(body): 
  START 123456 1 2 3
  中间使用空格隔开，第一个单词代表状态，后面的是参数
  发送时使用to_string拼接，接收时使用istringstream解析
  名字不能有空格
*/ 

// 定义消息边界（长度前缀包头）
#include <iostream> 
#include <cstdint>
#include <string> 

// 引入流数据解析读入数据
#include <sstream>

enum class MsgType : uint16_t {
  Join = 1, // C->S join in
  Welcome = 2, // S->C give out ID
  Ready = 3, // C->S ready for game
  Start = 4, // S->C start the game 
  Input = 5, // C->S behaviour
  Event = 6, // S->C game event
  Snapshot = 7, // S->C circumstance snapshot
  Ping = 8,  
  Pong = 9, 
  PlayerJoin = 10, // S->C broadcast someone come in
  PlayerLeave = 11, // S->C broadcast someone has gone
  Error = 12
};

#pragma pack(push, 1) 
struct PacketHeader {
  uint32_t length; // body length
  uint16_t type; // MsgType
};
#pragma pack(pop)

// 服务端接口
#include <winsock2.h> 
#include <ws2tcpip.h>
#include <chrono> 
#include <cmath> 
#include <thread>
#include <mutex>
#include <vector>
#include <array>
#include <memory>
#include <atomic> // 引入原子级别变量
#include <random> // 引入随机数将所有参与玩家的seed设为一致
#include <unordered_map>

// keyboard工具类
#include <conio.h>
#include <windows.h> // 如果需要Sleep
using namespace std;
using namespace std::chrono;

// 定义宏变量
static int constexpr WIDTH = 10; 
static int constexpr HIGHT = 20;

// 定义全局变量
uint8_t g_garbageDice;
int g_tick = 0; // 只为gameLoop计数，不用atomic

// 标准基础分
static int baseScore[] = {0, 100, 300, 500, 800}; 

// 全局静态引擎
static mt19937& get_engine() {
  static mt19937 engine(random_device{}());
  return engine;
}

// 定义结构体
// 定义颜色结构体
struct Color {
	uint8_t r, g, b;
};

// 定义类类型
class DropTimer {
public:
  DropTimer(int intervalMs) : m_interval(intervalMs) {
    m_lastTime = steady_clock::now();
  }

  // 检查是否到达下落时间，如果到达则重置计时器并返回true
  bool isTimeToDrop() {
    auto now = steady_clock::now();
    auto elapsed = duration_cast<milliseconds>(now - m_lastTime);
    if (elapsed.count() >= m_interval) {
      m_lastTime = now;
      return true;
    }
    return false;
  }

  // 更新下落间隔（等级变化时使用）
  void setInterval(int intervalMs) {
    m_interval = intervalMs;
  }

private:
  // 定义私有变量m_interval为int类型，初始化m_lastTime为当前时间点
  int m_interval;
  steady_clock::time_point m_lastTime;
};

// 发送一个包（函数声明）
bool send_packet(SOCKET s, MsgType type, const string& body);

// 接收缓冲区，处理粘包
class PacketReader {
public:
  void append(const char* data, int len);
  bool next(string& out_body, MsgType& out_type);
private:
  string buffer_;
};

// 客户端结构体
struct Client {
  int id;
  SOCKET sock;
  string name;
  bool ready = false;
  bool alive = false; // 设置玩家在线状态
  bool softDrop = false; // 设置软降逻辑
  bool gameOverBroadcasted = false; // 设置标记来记录玩家的结束游戏是否被广播过
  PacketReader reader;
  mutex send_mutex;

  // 每个客户端储存自己的信息（详情见全局变量）
  uint8_t dice = -1, nextDice = 0;
  uint8_t rotateNum = 0;
  uint8_t nowCeil = 0, nowLeft = 0;
  uint8_t nowShadeCeil = 0;
  uint32_t level = 1;
  uint32_t score = 0, lines = 0;
  bool quit = false;
  DropTimer timer{1000}; // 每个玩家构造一个下落计时器
  array<array<char, WIDTH + 5>, 2 * HIGHT + 5> buffer = {}, temporaryBuffer = {};
  int temporaryBlock[4][4] = {
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
  };

  // 设置Ping/Pong检查字段
  steady_clock::time_point last_pong_time;
  bool kicked = false;

  // 设置垃圾行缓冲条（只有垃圾行为0的时候才能将垃圾行发出去）
  uint8_t garbageBuffer = 0;
  
  void send(MsgType type, const string& body) {
    lock_guard<mutex> lk(send_mutex);
    send_packet(sock, type, body);
  }
};

// 定义枚举类型
// 设置颜色代码
enum TetrisColor {
  COLOR_EMPTY = 0, // 用0表示空格
  COLOR_CYAN, // 1: I
  COLOR_YELLOW, // 2: O
  COLOR_PURPLE, // 3: T
  COLOR_GREEN, // 4: S
  COLOR_RED, // 5: Z
  COLOR_BLUE, // 6: J
  COLOR_ORANGE // 7: L
};

// 按键动作枚举
enum KeyAction {
  KEY_NONE,
  KEY_UP,
  KEY_DOWN_START,
  KEY_DOWN_END,
  KEY_SPACE,
  KEY_LEFT,
  KEY_RIGHT,
  KEY_QUIT
};

// 定义固定元素构成
// 定义rgb三色代码
const Color COLORS_CHART[] = {
  {255, 255, 255},
  {0, 159, 218},
  {254, 203, 0},
  {149, 45, 152},
  {105, 190, 40},
  {237, 41, 57},
  {0, 101, 189},
  {255, 121, 0}
};

// 设置方块代码
const int SHAPES[7][4][4] = {
  // I
  { 
    {0, 0, 0, 0},
    {1, 1, 1, 1},
    {0, 0, 0, 0},
    {0, 0, 0, 0}
  },
  // O
  {
    {2, 2, 0, 0}, 
    {2, 2, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0}
  },
  // T
  { 
    {0, 3, 0, 0},
    {3, 3, 3, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0}
  },
  // S
  {
    {0, 4, 4, 0},
    {4, 4, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0}
  },
  // Z
  { 
    {5, 5, 0, 0},
    {0, 5, 5, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0}
  },
  // J
  {
    {6, 0, 0, 0},
    {6, 6, 6, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0}
  },
  // L
  { 
    {0, 0, 7, 0},
    {7, 7, 7, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0}
  }
};

// 设置翻转形状后方块的代码（按上顺时针旋转）
// I 1 两种形状
const int rotateI[2][4][4] = {
  {
    {0, 0, 0, 0},
    {1, 1, 1, 1},
    {0, 0, 0, 0},
    {0, 0, 0, 0}
  },
  { 
    {0, 1, 0, 0},
    {0, 1, 0, 0},
    {0, 1, 0, 0},
    {0, 1, 0, 0}
  }
};

// O 2 一种形状
const int rotateO[1][4][4] = {
  {
    {2, 2, 0, 0},
    {2, 2, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0}
  }
};

// T 3 四种形状 
const int rotateT[4][4][4] = {
  {
    {0, 3, 0, 0},
    {3, 3, 3, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0}
  },
  {
    {0, 3, 0, 0},
    {0, 3, 3, 0},
    {0, 3, 0, 0},
    {0, 0, 0, 0}
  },
  {
    {0, 0, 0, 0},
    {3, 3, 3, 0},
    {0, 3, 0, 0},
    {0, 0, 0, 0}
  },
  {
    {0, 3, 0, 0},
    {3, 3, 0, 0},
    {0, 3, 0, 0},
    {0, 0, 0, 0}
  }
};

// S 4 两种形状
const int rotateS[2][4][4] = {
  {
    {0, 4, 4, 0},
    {4, 4, 0, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0}
  },
  {
    {0, 4, 0, 0},
    {0, 4, 4, 0},
    {0, 0, 4, 0},
    {0, 0, 0, 0}
  }
};

// Z 5 两种形状
const int rotateZ[2][4][4] = {
  { 
    {5, 5, 0, 0},
    {0, 5, 5, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0}
  },
  { 
    {0, 0, 5, 0},
    {0, 5, 5, 0},
    {0, 5, 0, 0},
    {0, 0, 0, 0}
  }
};

// J 6 四种形状
const int rotateJ[4][4][4] = { 
  {
    {6, 0, 0, 0},
    {6, 6, 6, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0}
  },
  {
    {0, 6, 6, 0},
    {0, 6, 0, 0},
    {0, 6, 0, 0},
    {0, 0, 0, 0}
  },
  {
    {0, 0, 0, 0},
    {6, 6, 6, 0},
    {0, 0, 6, 0},
    {0, 0, 0, 0}
  },
  {
    {0, 6, 0, 0},
    {0, 6, 0, 0},
    {6, 6, 0, 0},
    {0, 0, 0, 0}
  }
};

// L 7 四种形状
const int rotateL[4][4][4] = {
  { 
    {0, 0, 7, 0},
    {7, 7, 7, 0},
    {0, 0, 0, 0},
    {0, 0, 0, 0}
  },
  { 
    {0, 7, 0, 0},
    {0, 7, 0, 0},
    {0, 7, 7, 0},
    {0, 0, 0, 0}
  },
  { 
    {0, 0, 0, 0},
    {7, 7, 7, 0},
    {7, 0, 0, 0},
    {0, 0, 0, 0}
  },
  { 
    {7, 7, 0, 0},
    {0, 7, 0, 0},
    {0, 7, 0, 0},
    {0, 0, 0, 0}
  }
};

/* 功能函数 */
// 获取 [min, max] 闭区间的随机整数
int get_random_int(int min, int max) {
  static mutex mtx;
  lock_guard<mutex> lk(mtx);
  static mt19937 engine(random_device{}());
  uniform_int_distribution<int> dist(min, max);
  return dist(engine);
}

// 线性计算下落间隔
int getFallInterval(const Client& c) {
  double base = 0.8 - (c.level - 1) * 0.007;
  if (base < 0.1) base = 0.1;
  double seconds = pow(base, c.level - 1);
  return max(50, (int)(seconds * 1000));
}

// 检查是否更新等级
void updateLevel(Client& c) {
  while (c.level * 10 <= c.lines) c.level++;
  c.timer.setInterval(getFallInterval(c));
}

// 设置软降间隔
int getSoftDropInterval(const Client& c) {
  return max(50, getFallInterval(c) / 20);
}

// 初始化游戏下降帧时间
void startGameForClient(Client& c) {
  c.timer.setInterval(getFallInterval(c));
  c.timer = DropTimer(getFallInterval(c));
}

void initBlocks(Client& c) {
  c.rotateNum = 0;
  c.dice = c.nextDice;
  c.nextDice = get_random_int(0, 6);
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) c.temporaryBlock[i][j] = SHAPES[c.dice][i][j];
  }
  c.nowCeil = HIGHT - 2;
  c.nowLeft = WIDTH / 2;
  return;
}

void initClientParameters(Client& c) {
  // 清空棋盘
  c.buffer = {};
  c.temporaryBuffer = {};

  // 重置游戏状态
  c.dice = get_random_int(0, 6);
  c.nextDice = get_random_int(0, 6);
  c.rotateNum = 0;
  c.level = 1;
  c.score = 0;
  c.lines = 0;
  c.garbageBuffer = 0;
  c.kicked = false;
  c.softDrop = false;
  c.gameOverBroadcasted = false;

  // 生成第一个方块
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) c.temporaryBlock[i][j] = SHAPES[c.dice][i][j];
  }

  // 方块初始位置
  c.nowCeil = HIGHT - 2;
  c.nowLeft = WIDTH / 2;
  
  // 初始化阴影位置
  c.nowShadeCeil = c.nowCeil;

  // 初始化计时器
  c.timer = DropTimer(getFallInterval(c));

  // 初始化心跳时间
  c.last_pong_time = steady_clock::now();
}

// 检测虚线方块是否到底
bool isShadeBlockTouchBottom(Client& c) {
  bool ok = false;
  for (int j = 0; j < 4; j++) {
    for (int i = 3; i >= 0; i--) {
      if (c.temporaryBlock[i][j] != COLOR_EMPTY && i + c.nowShadeCeil >= 2 * HIGHT) {
        ok = true;
        break;
      }
    }
  }
  return ok;
}

bool isShadeBlockTouchBlockBelow(Client& c) {
  bool ok = false;
  for (int j = 0; j < 4; j++) {
    int nowFloor = -1;
    for (int i = 0; i < 4; i++) {
      if (c.temporaryBlock[i][j] != COLOR_EMPTY) nowFloor = i;
    }
    if (nowFloor != -1 && c.buffer[c.nowShadeCeil + nowFloor][j + c.nowLeft] != COLOR_EMPTY) ok = true;
  }
  return ok;
}

void renewShadeBlocks(Client& c) {
  c.nowShadeCeil = c.nowCeil;
  int maxCeil = 2 * HIGHT - 1;
  while (c.nowShadeCeil < maxCeil) {
    c.nowShadeCeil++;
    if (isShadeBlockTouchBottom(c) || isShadeBlockTouchBlockBelow(c)) {
      c.nowShadeCeil--;
      break;
    }
  }
  return;
}

void pushBlocks(Client& c) {
  c.temporaryBuffer = c.buffer; 
  renewShadeBlocks(c);
  for (int i = c.nowCeil; i < c.nowCeil + 4; i++) {
    for (int j = c.nowLeft; j < c.nowLeft + 4; j++) {
      if (c.buffer[i][j] == COLOR_EMPTY) c.temporaryBuffer[i][j] = c.temporaryBlock[i - c.nowCeil][j - c.nowLeft];
      else c.temporaryBuffer[i][j] = c.buffer[i][j];
    }
  }
  return;
}

// 清除行（判断行是否满、改变buffer数组、清除行）
int isRowFull(Client& c, int row) {
  int cnt = 0;
  for (int j = 1; j <= WIDTH; j++) {
    if (c.buffer[row][j] != COLOR_EMPTY) cnt++;
  }
  return cnt;
}

void deleteRowInBuffer(Client& c, int row) {
  for (; row > HIGHT; row--) {
    for (int j = 1; j <= WIDTH; j++) {
      c.buffer[row][j] = c.buffer[row - 1][j];
    }
  }
  for (int j = 1; j <= WIDTH; j++) c.buffer[row][j] = COLOR_EMPTY;
  return;
}

void clearRow(Client& c) {
  int cleared = 0;
  int row = 2 * HIGHT - 1;
  while (row >= HIGHT) {
    if (isRowFull(c, row) == WIDTH) { 
      deleteRowInBuffer(c, row);
      cleared++;
    } else if (isRowFull(c, row) == 0) break;
    else row--;
  }
  c.score += baseScore[cleared] * c.level;
  c.lines += cleared;
  return;
}

// 碰撞检测
bool isTouchBottom(Client& c) {
  bool ok = false;
  for (int j = 0; j < 4; j++) {
    for (int i = 3; i >= 0; i--) {
      if (c.temporaryBlock[i][j] != COLOR_EMPTY && i + c.nowCeil >= 2 * HIGHT) {
        ok = true;
        break;
      }
    }
  }
  return ok;
}

bool isTouchLeft(Client& c) {
  bool ok = false;
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      if (c.temporaryBlock[i][j] != COLOR_EMPTY && c.buffer[i + c.nowCeil][j + c.nowLeft - 1] != COLOR_EMPTY) ok = true;
      if (ok) break;
    }
  }
  return ok;
}

bool isTouchRight(Client& c) {
  bool ok = false;
  for (int i = 0; i < 4; i++) {
    for (int j = 3; j >= 0; j--) {
      if (c.temporaryBlock[i][j] != COLOR_EMPTY && c.buffer[i + c.nowCeil][j + c.nowLeft + 1] != COLOR_EMPTY) ok = true;
      if (ok) break;
    }
  }
  return ok;
}

bool isTouchBlockBelow(Client& c) {
  bool ok = false;
  for (int j = 0; j < 4; j++) {
    int nowFloor = -1;
    for (int i = 0; i < 4; i++) {
      if (c.temporaryBlock[i][j] != COLOR_EMPTY) nowFloor = i;
    }
    if (nowFloor != -1 && c.buffer[c.nowCeil + nowFloor][j + c.nowLeft] != COLOR_EMPTY) ok = true;
  }
  return ok;
}

// 控制方块逻辑 
void tryMoveLeft(Client& c) {
  int nowLeft = 4;
  for (int i = 0; i < 4; i++) {
    for (int j = 3; j >= 0; j--) {
      if (c.temporaryBlock[i][j] != COLOR_EMPTY) nowLeft = min(j, nowLeft);
    }
  }
  if (c.nowLeft + nowLeft >= 2 && !isTouchLeft(c)) c.nowLeft--;
  return;
}

void tryMoveRight(Client& c) {
  int nowRight = 0; 
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      if (c.temporaryBlock[i][j] != COLOR_EMPTY) nowRight = max(j, nowRight);
    }
  }
  if (c.nowLeft + nowRight <= 9 && !isTouchRight(c)) c.nowLeft++;
  return;
}

// 检查方块踢墙
void checkIKickWall(Client& c) {
  if (c.rotateNum == 1) return;
  if (c.nowLeft == 8) c.nowLeft--;
  else if (c.nowLeft == 9) c.nowLeft -= 2;
  else if (c.nowLeft == 0) c.nowLeft++;
  return;
}

void checkTKickWall(Client& c) {
  if (c.rotateNum != 3 && c.nowLeft == 9) c.nowLeft--;
  else if (c.rotateNum != 1 && c.nowLeft == 0) c.nowLeft++;
  return;
}

void checkSOrZKickWall(Client& c) {
  if (c.rotateNum == 0 && c.nowLeft == 0) c.nowLeft++;
  return;
}

void checkJOrLKickWall(Client& c) {
  if ((c.rotateNum == 0 || c.rotateNum == 2) && c.nowLeft == 0) c.nowLeft++;
  else if ((c.rotateNum == 0 || c.rotateNum == 2) && c.nowLeft == 9) c.nowLeft--;
  return;
}

void tryRotate(Client& c) {
  c.rotateNum++;
  int rotateLen = 0;
  switch (c.dice) {
    case 0: {
      rotateLen = 2;
      c.rotateNum %= rotateLen;
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          c.temporaryBlock[i][j] = rotateI[c.rotateNum][i][j]; 
        }
      }
      checkIKickWall(c);
      break;
    }
    case 1: {
      rotateLen = 1;
      c.rotateNum %= rotateLen;
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          c.temporaryBlock[i][j] = rotateO[c.rotateNum][i][j]; 
        }
      }
      break;
    } 
    case 2: {
      rotateLen = 4;
      c.rotateNum %= rotateLen;
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          c.temporaryBlock[i][j] = rotateT[c.rotateNum][i][j]; 
        }
      }
      checkTKickWall(c);
      break;
    } 
    case 3: {
      rotateLen = 2;
      c.rotateNum %= rotateLen;
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          c.temporaryBlock[i][j] = rotateS[c.rotateNum][i][j]; 
        }
      }
      checkSOrZKickWall(c);
      break;
    }
    case 4: {
      rotateLen = 2;
      c.rotateNum %= rotateLen;
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          c.temporaryBlock[i][j] = rotateZ[c.rotateNum][i][j]; 
        }
      }
      checkSOrZKickWall(c);
      break;
    }
    case 5: {
      rotateLen = 4;
      c.rotateNum %= rotateLen;
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          c.temporaryBlock[i][j] = rotateJ[c.rotateNum][i][j]; 
        }
      }
      checkJOrLKickWall(c);
      break;
    }
    case 6: {
      rotateLen = 4;
      c.rotateNum %= rotateLen;
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          c.temporaryBlock[i][j] = rotateL[c.rotateNum][i][j]; 
        }
      }
      checkJOrLKickWall(c);
      break;
    }
    default: break;
  }
  return;
}

void hardDrop(Client& c) {
  int dropDistance = 0;
  while (true) {
    c.nowCeil++;
    if (isTouchBottom(c) || isTouchBlockBelow(c)) {
      c.nowCeil--;
      break;
    }
    dropDistance++;
  }
  c.score += dropDistance * 2; // 硬降每格加两分
  return;
}

// 判断是否结束
bool isGameOver(Client& c) {
  bool ok = false;
  for (int j = 1; j <= WIDTH; j++) {
    if (c.buffer[HIGHT - 1][j] != COLOR_EMPTY) {
      ok = true;
      break;
    }
  }
  return ok;
}


/* ===== 包传递 ===== */
// 全局客户端表
mutex g_clients_mutex;
unordered_map<int, shared_ptr<Client>> g_clients;
atomic<int> g_next_id{1};

/* ===== 包处理 ===== */
bool send_all(SOCKET s, const char* data, int len) {
  int sent = 0;
  while (sent < len) {
    int n = send(s, data + sent, len - sent, 0);
    if (n <= 0) return false;
    sent += n;
  }
  return true;
}

bool send_packet(SOCKET s, MsgType type, const string& body) {
  PacketHeader h;
  h.length = htonl((uint32_t)body.size()); // 32位转网络字节序
  h.type = htons((uint16_t)type); // 16位转网络字节序
  if (!send_all(s, (const char*)&h, sizeof(h))) return false;
  if (!body.empty() && !send_all(s, body.data(), (int)body.size())) return false;
  return true;
}

void PacketReader::append(const char* data, int len) {
  buffer_.append(data, len);
}

bool PacketReader::next(string& out_body, MsgType& out_type) {
  if (buffer_.size() < sizeof(PacketHeader)) return false;

  PacketHeader h;
  memcpy(&h, buffer_.data(), sizeof(h));
  
  // 字节序转换
  uint32_t len = ntohl(h.length);
  uint16_t type = ntohs(h.type);

  if (buffer_.size() < sizeof(h) + len) return false;

  out_type = (MsgType)type;
  out_body.assign(buffer_.data() + sizeof(h), len);

  buffer_.erase(0, sizeof(h) + len);
  return true;
}

// 广播给所有人
void broadcast(MsgType type, const string& body, int except_id = -1) {
  lock_guard<mutex> lk(g_clients_mutex);
  for (auto& [id, client] : g_clients) {
    if (id == except_id) continue;
    client->send(type, body);
  }
}

// 将每个玩家的buffer转换成string
string bufferToString(const Client& c) {
  string buffer;
  for (int i = 0; i < 2 * HIGHT; ++i) {
    for (int j = 0; j < WIDTH + 2; ++j) buffer += char('0' + c.buffer[i][j]);
    if (i != 2 * HIGHT - 1) buffer += '|';
  }
  return buffer;
}

// 设置游戏参数snapshot发送body的格式
string make_snapshot_body(Client& c) {
  // 发送信息之前再调用一次设置阴影方块位置的函数
  renewShadeBlocks(c);

  string body = "SNAPSHOT ";
  body += to_string(g_tick) + " ";
  body += to_string(c.id) + " ";
  body += (c.alive ? "1 " : "0 ");
  body += to_string(c.dice) + " ";
  body += to_string(c.nextDice) + " ";
  body += to_string(c.rotateNum) + " ";
  body += to_string(c.nowCeil) + " ";
  body += to_string(c.nowLeft) + " ";
  body += to_string(c.nowShadeCeil) + " ";
  body += to_string(c.nowLeft) + " ";
  body += to_string(c.level) + " ";
  body += to_string(c.score) + " ";
  body += to_string(c.lines) + " ";
  body += bufferToString(c);
  return body;
}

// 服务器主动广播游戏渲染(SNAPSHOT)
void send_snapshots() {
  /*
    此处定义消息的格式: 
    消息头为SNAPSHOT
    消息内容定义顺序为：
    id -> alive -> dice -> nextDice -> rotateNum -> nowCeil -> nowLeft -> nowShadeCeil -> nowShadeLeft -> level -> score -> lines ->buffer 
    其中buffer使用|来分隔每行数据
  */ 

  // 注意锁里面只能定义收集每个玩家的信息而不能广播，因为广播也会加锁，这样会产生死锁
  // 在外部定义数组收集
  vector<string> messages;
  
  {
    lock_guard<mutex> lk(g_clients_mutex);
    for (auto& [id, c] : g_clients) {
      if (!c->ready && !c->alive) continue;
      messages.push_back(make_snapshot_body(*c));
    }
  }

  // 锁外广播
  for (auto& msg : messages) broadcast(MsgType::Snapshot, msg);

  return;
}

// 处理按键反馈
void applyInput(Client& c, KeyAction key) {
  switch (key) {
  case KEY_LEFT:
    tryMoveLeft(c);
    break;
  
  case KEY_RIGHT:
    tryMoveRight(c);  
    break;

  case KEY_UP:
    tryRotate(c);
    break;

  case KEY_DOWN_START:
    // 软降：临时加速
    c.softDrop = true;
    c.timer.setInterval(getSoftDropInterval(c));
    // 软降加分由tickDrop处理
    break;
   
  case KEY_DOWN_END:
    c.softDrop = false;
    c.timer.setInterval(getFallInterval(c));
    break;

  case KEY_SPACE: {
    // 硬降：直接落底
    hardDrop(c);
    pushBlocks(c);
    c.buffer = c.temporaryBuffer;
    clearRow(c);
    updateLevel(c);
    initBlocks(c);

    // 重设时间间隔
    c.timer = DropTimer(getFallInterval(c));

    // 如果落地直接到顶部则游戏结束
    if (isGameOver(c)) c.alive = false;
    break;
  }

  case KEY_QUIT:
    c.alive = false;
    c.gameOverBroadcasted = true; // 标记为已广播过而不再触发GAMEOVER
    break;

  default: 
    break;
  }
}
 
// 每个客户端的处理线程
void client_thread(shared_ptr<Client> client) {
  char buf[4096];
  while (true) {
    int n = recv(client->sock, buf, sizeof(buf), 0);
    if (n <= 0) break;

    client->reader.append(buf, n);

    string body;
    MsgType type;
    while (client->reader.next(body, type)) {
      // 该处处理消息
      switch (type) {
      case MsgType::Join: {
        // body里是玩家名字，解析body，并判断消息头是不是与type相对应
        istringstream iss(body);
        string cmd; 
        iss >> cmd;

        if (cmd != "JOIN") {
          // 格式错误，忽略信息
          break;
        }

        string name;
        iss >> name; // 读出名字

        if (name.empty()) {
          client->send(MsgType::Error, "ERROR 1002 名字不能为空");
          break;
        }

        client->name = name;
        // 定义发送信息：欢迎发送给单个玩家，广播玩家名称给所有玩家
        string sendToOnePlayer = "WELCOME " + to_string(client->id);
        string sendToAllPlayer = "PLAYERJOIN " + to_string(client->id) + " " + client->name;
        
        client->send(MsgType::Welcome, sendToOnePlayer);
        broadcast(MsgType::PlayerJoin, sendToAllPlayer);
        break;
      }
      case MsgType::Ready: {
        istringstream iss(body);
        string cmd;
        iss >> cmd;

        if (cmd != "READY") break;

        // 只初始化该玩家自己
        {
          lock_guard<mutex> lk(g_clients_mutex);
          initClientParameters(*client);
          client->alive = true;
          client->ready = true;
        }

        // 只给这个玩家发START
        string sendToOnePlayer = "START " + to_string(client->id);
        client->send(MsgType::Start, sendToOnePlayer);

        break;
      }
      case MsgType::Input: {
        // body是操作，传输键盘键入数据到blocksFallDown进行判断
        istringstream iss(body);
        string cmd;
        iss >> cmd;

        if (cmd != "INPUT") break;

        // 定义键盘类型变量key
        KeyAction key = KEY_NONE;

        string KEY;
        iss >> KEY;

        // 判断KEY的值能否合法赋给key
        if (KEY == "UP") key = KEY_UP;
        else if (KEY == "DOWN_START") key = KEY_DOWN_START;
        else if (KEY == "DOWN_END") key = KEY_DOWN_END;
        else if (KEY == "SPACE") key = KEY_SPACE;
        else if (KEY == "LEFT") key = KEY_LEFT;
        else if (KEY == "RIGHT") key = KEY_RIGHT;
        else if (KEY == "QUIT") key = KEY_QUIT;
        
        // 检测key是否为QUIT
        if (key == KEY_QUIT) {
          lock_guard<mutex> lk(g_clients_mutex);
          client->alive = false;
          break;
        }

        {
          lock_guard<mutex> lk(g_clients_mutex);
          applyInput(*client, key);
        }

        // 交给游戏逻辑处理
        break;
      }
      // 暂时不处理EVENT来优化显示
      // case MsgType::Event: { 
      // }
      case MsgType::Pong: {
        istringstream iss(body);
        string cmd;

        uint64_t t;
        iss >> cmd >> t; // 此处为了格式干净，虽然读入t没有使用，但是还是读入了
        
        if (cmd != "PONG") break;

        client->last_pong_time = steady_clock::now();
        
        break;
      }

      default: break;
      }
    }
  }

  // 断开
  { 
    lock_guard<mutex> lk(g_clients_mutex);
    g_clients.erase(client->id);
  }

  // 定义玩家离开的广播格式
  string sendToAllPlayer = "PLAYERLEAVE " + to_string(client->id) + " " + client->name;

  broadcast(MsgType::PlayerLeave, sendToAllPlayer);
  closesocket(client->sock);
}

// 开启服务器
void startServer() {
  // windows上使用网络功能需要开启网络权限
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
  
  // 1.创建socket套接字
  SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_socket == INVALID_SOCKET) return;

  // 2.给socket绑定端口号
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_port = htons(8080);
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  
  int yes = 1;
  setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));

  if (bind(listen_socket, (sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
    cout << "bind failed: " << WSAGetLastError() << endl;
    return;
  }

  // 3.给socket开启监听属性
  if (listen(listen_socket, 10) == SOCKET_ERROR) {
    cout << "listen failed: " << WSAGetLastError() << endl;
    return;
  }

  // 4.等待客户端连接
  cout << "server listen on 0.0.0.0:8080" << endl;

  while (true) {
    SOCKET client_sock = accept(listen_socket, nullptr, nullptr);
    if (client_sock == INVALID_SOCKET) continue;

    auto client = make_shared<Client>();
    client->id = g_next_id++;
    client->sock = client_sock;

    // 获取当前时间作为零点，防止还没开始就被踢了
    client->last_pong_time = steady_clock::now();

    // 设置安全锁以保证连接不中断
    {
      lock_guard<mutex> lk(g_clients_mutex);
      g_clients[client->id] = client;
    }

    cout << "client " << client->id << " connected" << endl;

    // 每个客户开一个线程
    thread(client_thread, client).detach();
  }

  closesocket(listen_socket);
  WSACleanup();
}

// 单独判断每个玩家的下落时间是否到达
void tickDrop(Client& c) {
  c.nowCeil++;
  if (isTouchBottom(c) || isTouchBlockBelow(c)) {
    c.nowCeil--;
    pushBlocks(c);
    c.buffer = c.temporaryBuffer; // 将方块固化进buffer
    clearRow(c);
 
    // 检查是否消行并可以更新等级
    updateLevel(c);
 
    initBlocks(c);

    // 重设时间间隔
    c.timer = DropTimer(getFallInterval(c));
  
    // 此处也需要判断游戏是否已经结束
    if (isGameOver(c)) c.alive = false;

    // 更新计时器（单个玩家的等级可能变化）
    c.timer.setInterval(getFallInterval(c));

    return;
  }
  // 如果正在软降，加一分
  // 将向下按键的判定改为上升沿和下降沿检测
  if (c.softDrop) c.score++;
  
  // 不能在此处使用drawBlocks()，服务器不负责渲染
}

/* ===== 定义线程 ===== */
// 启用心跳线程
void heartbeat_thread() {
  while (true) {
    this_thread::sleep_for(seconds(5)); // 每5秒检测一次

    auto now = steady_clock::now();
    vector<shared_ptr<Client>> to_kick;
    
    // 先收集再发送Ping
    vector<shared_ptr<Client>> to_ping;

    {
      lock_guard<mutex> lk(g_clients_mutex);
      
      for (auto& [id, c] : g_clients) {
        if (c->kicked) continue;

        // 1.将没被踢掉的对象先储存起来
        to_ping.push_back(c);

        // 2.检查是否超时
        auto elasped = duration_cast<seconds>(  
          now - c->last_pong_time);
        if (elasped.count() > 15) { // 15秒没回Pong
          c->kicked = true;
          c->alive = false;
          to_kick.push_back(c);
        }
      }
    }

    // 3.锁外发送Ping，避免线程阻塞
    for (auto& c : to_ping) {
      uint64_t t = duration_cast<milliseconds>(
          now.time_since_epoch()).count();
      c->send(MsgType::Ping, "PING " + to_string(t));
    }

    // 4.锁外踢人
    for (auto& c : to_kick) {
      cout << "kick client " << c->id << " (timeout)" << endl;
      shutdown(c->sock, SD_BOTH);
    }
  }
}

// 游戏主循环
void gameLoop() {
  auto next = steady_clock::now();
  const auto tick = milliseconds(16);

  while (true) {
    // 用于记录玩家游戏是否结束
    vector<int> gameOverIds;
    
    next += tick;
    this_thread::sleep_until(next);
    
    // 加锁遍历所有玩家 
    {
      lock_guard<mutex> lk(g_clients_mutex);

      for (auto& [id, c] : g_clients) {
        // 每个玩家用自己的计时器
        if (c->alive && c->timer.isTimeToDrop()) tickDrop(*c);
        
        // 查看玩家是否游戏结束，如果是则在锁外广播
        if (!c->alive && !c->gameOverBroadcasted) {
          c->gameOverBroadcasted = true;
          gameOverIds.push_back(c->id);
        }
      }
    }

    // 锁外广播玩家游戏情况
    for (int id : gameOverIds) broadcast(MsgType::Event, "EVENT " + to_string(id) + " GAMEOVER");

    g_tick++;

    // 每6帧发一次快照，约10Hz
    if (g_tick % 6 == 0) send_snapshots();
  }

  return;
}

int main() {
  // 启用线程
  thread(gameLoop).detach();
  thread(heartbeat_thread).detach();
  startServer();
  return 0;
}
