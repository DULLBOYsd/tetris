#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <conio.h>

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <array>
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>
#include <unordered_map>
#include <algorithm>
#include <cstdint>
#include <random>
#include <cstring>

using namespace std;
using namespace std::chrono;

// ============ 协议 ============
enum class MsgType : uint16_t {
  Join = 1,
  Welcome = 2,
  Ready = 3,
  Start = 4,
  Input = 5,
  Event = 6,
  Snapshot = 7,
  Ping = 8,
  Pong = 9,
  PlayerJoin = 10,
  PlayerLeave = 11,
  Error = 12
};

#pragma pack(push, 1)
struct PacketHeader {
  uint32_t length;
  uint16_t type;
};
#pragma pack(pop)

SOCKET g_sock = INVALID_SOCKET;
mutex g_send_mutex;

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
  h.length = htonl((uint32_t)body.size());
  h.type = htons((uint16_t)type);
  if (!send_all(s, (const char*)&h, sizeof(h))) return false;
  if (!body.empty() && !send_all(s, body.data(), (int)body.size())) return false;
  return true;
}

class PacketReader {
public:
  void append(const char* data, int len) { buffer_.append(data, len); }

  bool next(string& out_body, MsgType& out_type) {
    if (buffer_.size() < sizeof(PacketHeader)) return false;

    PacketHeader h;
    memcpy(&h, buffer_.data(), sizeof(h));
    uint32_t len = ntohl(h.length);
    uint16_t type = ntohs(h.type);

    if (buffer_.size() < sizeof(h) + len) return false;

    out_type = (MsgType)type;
    out_body.assign(buffer_.data() + sizeof(h), len);
    buffer_.erase(0, sizeof(h) + len);
    return true;
  }

private:
  string buffer_;
};

// ============ 常量 ============
static int constexpr WIDTH = 10;
static int constexpr HIGHT = 20;

// ============ 玩家显示数据 ============
struct PlayerView {
  int id = 0;
  string name;
  bool alive = true;
  int dice = 0;
  int nextDice = 0;
  int rotateNum = 0;
  int nowCeil = 0;
  int nowLeft = 0;
  int nowShadeCeil = 0;
  int nowShadeLeft = 0;
  int level = 1;
  int score = 0;
  int lines = 0;
  bool isSelf = false;

  // 储存发送过来的棋盘
  array<array<char, WIDTH + 5>, 2 * HIGHT + 5> buffer = {};
};

mutex g_players_mutex;
unordered_map<int, PlayerView> g_players;
int g_myId = -1;
atomic<bool> g_running{true};

// ============ 方块参数（用于绘制当前方块） ============
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

// 定义固定元素构成
// 定义结构体
struct Color {  
  uint8_t r, g, b;
};

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

const int SHAPES[7][4][4] = {
  // I
  {
    {0,0,0,0},
    {1,1,1,1},
    {0,0,0,0},
    {0,0,0,0}
  },
  // O
  {
    {2,2,0,0},
    {2,2,0,0},
    {0,0,0,0},
    {0,0,0,0}
  },
  // T
  {
    {0,3,0,0},
    {3,3,3,0},
    {0,0,0,0},
    {0,0,0,0}
  },
  // S
  {
    {0,4,4,0},
    {4,4,0,0},
    {0,0,0,0},
    {0,0,0,0}
  },
  // Z
  {
    {5,5,0,0},
    {0,5,5,0},
    {0,0,0,0},
    {0,0,0,0}
  },
  // J
  {
    {6,0,0,0},
    {6,6,6,0},
    {0,0,0,0},
    {0,0,0,0}
  },
  // L
  {
    {0,0,7,0},
    {7,7,7,0},
    {0,0,0,0},
    {0,0,0,0}
  }
};

const int rotateI[2][4][4] = {
  { {0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0} },
  { {0,1,0,0},{0,1,0,0},{0,1,0,0},{0,1,0,0} }
};
const int rotateO[1][4][4] = {
  { {2,2,0,0},{2,2,0,0},{0,0,0,0},{0,0,0,0} }
};
const int rotateT[4][4][4] = {
  { {0,3,0,0},{3,3,3,0},{0,0,0,0},{0,0,0,0} },
  { {0,3,0,0},{0,3,3,0},{0,3,0,0},{0,0,0,0} },
  { {0,0,0,0},{3,3,3,0},{0,3,0,0},{0,0,0,0} },
  { {0,3,0,0},{3,3,0,0},{0,3,0,0},{0,0,0,0} }
};
const int rotateS[2][4][4] = {
  { {0,4,4,0},{4,4,0,0},{0,0,0,0},{0,0,0,0} },
  { {0,4,0,0},{0,4,4,0},{0,0,4,0},{0,0,0,0} }
};
const int rotateZ[2][4][4] = {
  { {5,5,0,0},{0,5,5,0},{0,0,0,0},{0,0,0,0} },
  { {0,0,5,0},{0,5,5,0},{0,5,0,0},{0,0,0,0} }
};
const int rotateJ[4][4][4] = {
  { {6,0,0,0},{6,6,6,0},{0,0,0,0},{0,0,0,0} },
  { {0,6,6,0},{0,6,0,0},{0,6,0,0},{0,0,0,0} },
  { {0,0,0,0},{6,6,6,0},{0,0,6,0},{0,0,0,0} },
  { {0,6,0,0},{0,6,0,0},{6,6,0,0},{0,0,0,0} }
};
const int rotateL[4][4][4] = {
  { {0,0,7,0},{7,7,7,0},{0,0,0,0},{0,0,0,0} },
  { {0,7,0,0},{0,7,0,0},{0,7,7,0},{0,0,0,0} },
  { {0,0,0,0},{7,7,7,0},{7,0,0,0},{0,0,0,0} },
  { {7,7,0,0},{0,7,0,0},{0,7,0,0},{0,0,0,0} }
};

// 结束画面
const bool THEEND[8][41] = {
  {true,  true,  true,  true,  true,  true, false,  true,  true, false, false,  true,  true, false,  true,  true,  true,  true,  true,  true, false,  true,  true,  true,  true,  true,  true, false,  true,  true,  true, false,  true,  true, false,  true,  true,  true, false, false, false},
  {true,  true,  true,  true,  true,  true, false,  true,  true, false, false,  true,  true, false,  true,  true,  true,  true,  true,  true, false,  true,  true,  true,  true,  true,  true, false,  true,  true,  true, false,  true,  true, false,  true,  true,  true,  true, false, false},
  {false, false, true,  true, false, false, false,  true,  true, false, false,  true,  true, false,  true,  true, false, false, false, false, false,  true,  true, false, false, false, false, false,  true,  true,  true, false,  true,  true, false,  true,  true, false,  true,  true, false},
  {false, false, true,  true, false, false, false,  true,  true,  true,  true,  true,  true, false,  true,  true,  true,  true,  true,  true, false,  true,  true,  true,  true,  true,  true, false,  true,  true,  true, false,  true,  true, false,  true,  true, false, false,  true,  true},
  {false, false, true,  true, false, false, false,  true,  true,  true,  true,  true,  true, false,  true,  true,  true,  true,  true,  true, false,  true,  true,  true,  true,  true,  true, false,  true,  true, false,  true,  true,  true, false,  true,  true, false, false,  true,  true},
  {false, false, true,  true, false, false, false,  true,  true, false, false,  true,  true, false,  true,  true, false, false, false, false, false,  true,  true, false, false, false, false, false,  true,  true, false,  true,  true,  true, false,  true,  true, false,  true,  true, false},
  {false, false, true,  true, false, false, false,  true,  true, false, false,  true,  true, false,  true,  true,  true,  true,  true,  true, false,  true,  true,  true,  true,  true,  true, false,  true,  true, false,  true,  true,  true, false,  true,  true,  true,  true, false, false},
  {false, false, true,  true, false, false, false,  true,  true, false, false,  true,  true, false,  true,  true,  true,  true,  true,  true, false,  true,  true,  true,  true,  true,  true, false,  true,  true, false,  true,  true,  true, false,  true,  true,  true, false, false, false}
};

// 全局静态引擎
static mt19937& get_engine() {
  static mt19937 engine(random_device{}());
  return engine;
}

// 设置随机数
int get_random_int(int min, int max) {
  static mutex mtx;
  lock_guard<mutex> lk(mtx);
  static mt19937 engine(random_device{}());
  uniform_int_distribution<int> dist(min, max);
  return dist(engine);
}

// 直接重定位光标到左上角
void clearScreen() {
  cout << "\033[H";
  return;
}

// 渲染界面
// 输出颜色
void output(const array<array<char, WIDTH + 5>, 2 * HIGHT + 5>& tmp, int i, int j, string& line) {
  if (tmp[i][j] == COLOR_EMPTY) {
    line += ' ';
  } else if (tmp[i][j] <= 7) {
    const Color& c = COLORS_CHART[tmp[i][j]];
    line += "\033[38;2;";
    line += to_string((int)c.r); line += ';';
    line += to_string((int)c.g); line += ';';
    line += to_string((int)c.b);
    line += "m█\033[0m";
  } else {
    const Color& c = COLORS_CHART[tmp[i][j] / 10];
    line += "\033[38;2;";
    line += to_string((int)c.r); line += ';';
    line += to_string((int)c.g); line += ';';
    line += to_string((int)c.b);
    line += "m░\033[0m";
  }
  return;
}

string rowToString(const PlayerView& p, int row) {
  string oneRow;
  for (int j = 0; j < 4; j++) {
    if (SHAPES[p.nextDice][row][j] != COLOR_EMPTY) { 
      const Color& c = COLORS_CHART[SHAPES[p.nextDice][row][j]];
      oneRow += "\033[38;2;";
      oneRow += to_string((int)c.r); oneRow += ';';
      oneRow += to_string((int)c.g); oneRow += ';';
      oneRow += to_string((int)c.b);
      oneRow += "m█\033[0m";
    } else oneRow += " ";
  }
  return oneRow;
}

// 获取block
void getBlock(const PlayerView& p, int (&block)[4][4]) {
  switch (p.dice) {
    case 0: {
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          block[i][j] = rotateI[p.rotateNum][i][j]; 
        }
      }
      break;
    }
    case 1: {
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          block[i][j] = rotateO[p.rotateNum][i][j]; 
        }
      }
      break;
    } 
    case 2: {
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          block[i][j] = rotateT[p.rotateNum][i][j]; 
        }
      }
      break;
    } 
    case 3: {
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          block[i][j] = rotateS[p.rotateNum][i][j]; 
        }
      }
      break;
    }
    case 4: {
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          block[i][j] = rotateZ[p.rotateNum][i][j]; 
        }
      }
      break;
    }
    case 5: {
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          block[i][j] = rotateJ[p.rotateNum][i][j]; 
        }
      }
      break;
    }
    case 6: {
      for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
          block[i][j] = rotateL[p.rotateNum][i][j]; 
        }
      }
      break;
    }
    default: memset(block, 0, 16 * sizeof(int)); break;
  }
}

// 绘制实体方块当前显示位置
void pushNowBlock(const PlayerView& p, const int block[4][4], array<array<char, WIDTH + 5>, 2 * HIGHT + 5>& tmp) {
  for (int i = p.nowCeil; i < p.nowCeil + 4; ++i) {
    for (int j = p.nowLeft; j < p.nowLeft + 4; ++j) {
      if (block[i - p.nowCeil][j - p.nowLeft] != COLOR_EMPTY) tmp[i][j] = block[i - p.nowCeil][j - p.nowLeft];
		}
	}
	return; 
}

// 绘制阴影方块当前显示位置
void pushNowShadeBlock(PlayerView& p, const int block[4][4], array<array<char, WIDTH + 5>, 2 * HIGHT + 5>& tmp) {	
  for (int i = p.nowShadeCeil; i < p.nowShadeCeil + 4; ++i) {
    for (int j = p.nowShadeLeft; j < p.nowShadeLeft + 4; ++j) {
      if (block[i - p.nowShadeCeil][j - p.nowShadeLeft] != COLOR_EMPTY) tmp[i][j] = block[i - p.nowShadeCeil][j - p.nowShadeLeft] * 10;
		}
	}
	return; 
}

// 获取其他玩家的棋盘
void getOtherBuffers(const PlayerView& otherPlayer, string (&otherPlayers)[2 * HIGHT + 1]) {
  array<array<char, WIDTH + 5>, 2 * HIGHT + 5> tmp = otherPlayer.buffer;

  // 获取自己的方块形状
  int block[4][4];
  getBlock(otherPlayer, block);
  pushNowBlock(otherPlayer, block, tmp);

  // 上方空白区
  for (int i = 0; i < HIGHT; i++) {
    for (int j = 0; j < WIDTH + 2; j++) output(tmp, i, j, otherPlayers[i]);
  }
  // 游戏区
  for (int i = HIGHT; i < 2 * HIGHT; i++) {
    otherPlayers[i] += '|';
    for (int j = 1; j <= WIDTH; j++) output(tmp, i, j, otherPlayers[i]);
    otherPlayers[i] += '|';
  }
  otherPlayers[2 * HIGHT] += '|';
  for (int i = 1; i <= WIDTH; i++) otherPlayers[2 * HIGHT] += '_';
  otherPlayers[2 * HIGHT] += '|';
  
  return;
}

// 绘制界面
void drawPlayer(PlayerView& p) {
  // 改用string记录buffer内容，依次输出
  static string frame;
	string otherPlayers[2 * HIGHT + 1] = {};

  frame.clear();
  frame.reserve(HIGHT * WIDTH * 24); // 预留空间，避免反复扩容
  
  // 侧边栏内容：显示引导和游戏参数（按可视行索引 0 ~ 2 * HIGHT - 1）
  string sidebar[2 * HIGHT] = {};
  sidebar[0] = "   总得分: " + to_string(p.score),   otherPlayers[0] = "         ";
  sidebar[1] = "   当前等级: " + to_string(p.level), otherPlayers[1] = "        "; 
  sidebar[2] = "   消去行数: " + to_string(p.lines), otherPlayers[2] = "        ";
                                                     otherPlayers[3] = "                 ";
  sidebar[4] = "  [← →]  移动",                      otherPlayers[4] = "      "; 
  sidebar[5] = "  [↑]    旋转",                      otherPlayers[5] = "      ";
  sidebar[6] = "  [↓]    软降",                      otherPlayers[6] = "      ";
  sidebar[7] = "  [空格] 硬降",                      otherPlayers[7] = "      ";
  sidebar[8] = "  [Q]    退出",                      otherPlayers[8] = "      ";
                                                     otherPlayers[9] = "                 ";
  
  // 侧边栏内容：显示下一个出现的方块
  sidebar[10] = " 下一个方块",                      otherPlayers[10] = "           ";
  sidebar[11] = "  |======|",                       otherPlayers[11] = "       ";
  sidebar[12] = "  |      |",                       otherPlayers[12] = "       "; 
  sidebar[13] = "  | " + rowToString(p, 0) + " |",  otherPlayers[13] = "       ";
  sidebar[14] = "  | " + rowToString(p, 1) + " |",  otherPlayers[14] = "       ";
  sidebar[15] = "  | " + rowToString(p, 2) + " |",  otherPlayers[15] = "       ";
  sidebar[16] = "  |======|",                       otherPlayers[16] = "       ";

  // 将剩下的部分用18个空格填充
  for (int row = 17; row < 2 * HIGHT + 1; ++row)       otherPlayers[row] = "                 ";
  
	// 获取其他玩家的棋盘
	{
    lock_guard<mutex> lk(g_players_mutex);
    for (const auto& [id, otherPlayer] : g_players) { 
      if (id != g_myId) getOtherBuffers(otherPlayer, otherPlayers);
    }
  }

  // 获取当前正在下落的方块
  int myBlock[4][4];
  getBlock(p, myBlock);

  // 使用tmp而不是p.buffer
  array<array<char, WIDTH + 5>, 2 * HIGHT + 5> tmp = p.buffer;

	// 将阴影方块和正在下落的方块push到buffer里面
	pushNowShadeBlock(p, myBlock, tmp);
	pushNowBlock(p, myBlock, tmp);

  // 上方空白区
  for (int i = 0; i < HIGHT; i++) {
    for (int j = 0; j < WIDTH + 2; j++) output(tmp, i, j, frame);
    frame += sidebar[i] + otherPlayers[i];
    frame += '\n';
  }
  // 游戏区
  for (int i = HIGHT; i < 2 * HIGHT; i++) {
    frame += '|';
    for (int j = 1; j <= WIDTH; j++) output(tmp, i, j, frame);
    frame += '|' + otherPlayers[i];
    frame += '\n';
  }
  frame += '|';
  for (int i = 1; i <= WIDTH; i++) frame += '_';
  frame += '|' + otherPlayers[2 * HIGHT];
  frame += '\n';
  
  // 下方空白区（此处要谨慎，因为这是在vim平台上面测试得到换行为WIDTH（10格）刚好显示不出来下次的内容)
  for (int i = 0; i < WIDTH; i++) frame += '\n';

  cout << frame; // 一次性输出整帧
  
}

/* ===== 绘制结束画面 ===== */
void outputOneBlock(int dice) {
  cout << "\033[38;2;" << (int)COLORS_CHART[dice].r << ";" << 
  (int)COLORS_CHART[dice].g << ";" << (int)COLORS_CHART[dice].b << "m"
    << '#'
    << "\033[0m";
  return;
}

void drawEnd(const PlayerView& p) {
  cout << "\033[2J";
  cout << "\033[H";
  int row = 8, col = 41, dice;
  for (int i = 0; i < row; i++) {
    for (int j = 0; j < col; j++) {
      dice = get_random_int(1, 7);
      if (!THEEND[i][j]) {
        cout << ' ';
        continue;
      }
      outputOneBlock(dice);
    }
    cout << endl;
  }
  cout << "总得分: " << p.score << endl;
  cout << "最终等级: " << p.level << endl;
  cout << "消去行数: " << p.lines << endl;
  
  return;
}

// 读入信息
void render() {
	PlayerView copy;
  bool found = false;
  {
    lock_guard<mutex> lk(g_players_mutex);
    auto it = g_players.find(g_myId);
    if (it == g_players.end()) return;
    copy = it->second;
  } 
  
  // 游戏结束，结束进程
  if (!copy.alive) {
    clearScreen();
    drawEnd(copy);
    
		this_thread::sleep_for(seconds(5));
    
    // 设置进程结束标志
    g_running = false;
    return;
  }

  clearScreen();
  drawPlayer(copy);
  return;
}

// ============ 处理消息 ============
void handle_welcome(const string& body) {
  istringstream iss(body);
  string cmd;
  int id;
  iss >> cmd >> id;
  if (cmd != "WELCOME") return;

  // welcome这个地方是第一次收到反馈给玩家的id信息，需要在此处记录自己的编号
  g_myId = id;
}

void handle_player_join(const string& body) {
  istringstream iss(body);
  string cmd;
  int id;
  string name;
  iss >> cmd >> id >> name;
  if (cmd != "PLAYERJOIN") return;

  // 更改玩家信息也需要加锁
  { 
    lock_guard<mutex> lk(g_players_mutex);
    PlayerView& p = g_players[id];
    p.id = id;
    p.name = name;
    p.isSelf = (id == g_myId);
  }
}

void handle_player_leave(const string& body) {
  istringstream iss(body);
  string cmd;
  int id;
  string name;
  iss >> cmd >> id >> name;
  if (cmd != "PLAYERLEAVE") return;
  
  // 从玩家名单中抹除这个玩家的id
  {
    lock_guard<mutex> lk(g_players_mutex);
    g_players.erase(id);
  }
}

void handle_start(const string& body) {
  // body: START <id1> <id2> ...（此处记录了所有参与者的id）
  istringstream iss(body);
  string cmd;
  iss >> cmd;
  if (cmd != "START") return;

  int id;
  while (iss >> id) {
    lock_guard<mutex> lk(g_players_mutex);
    if (g_players.find(id) == g_players.end()) {
      g_players[id].id = id;
    }
  }
}

void handle_snapshot(const string& body) {
  istringstream iss(body);
  string cmd;
  int tick, playerId, alive, dice, nextDice, rot, nowCeil, nowLeft;
  int nowShadeCeil, nowShadeLeft, level, score, lines;
  string boardStr;

  iss >> cmd >> tick >> playerId >> alive >> dice >> nextDice >> rot
      >> nowCeil >> nowLeft >> nowShadeCeil >> nowShadeLeft >> level
      >> score >> lines >> boardStr;

  if (cmd != "SNAPSHOT") return;

  lock_guard<mutex> lk(g_players_mutex);
  PlayerView& p = g_players[playerId];
  p.id = playerId;
  p.alive = (alive == 1);
  p.dice = dice;
  p.nextDice = nextDice;
  p.rotateNum = rot;
  p.nowCeil = nowCeil;
  p.nowLeft = nowLeft;
  p.nowShadeCeil = nowShadeCeil;
  p.nowShadeLeft = nowShadeLeft;
  p.level = level;
  p.score = score;
  p.lines = lines;
  p.isSelf = (playerId == g_myId);

  // 解析棋盘：每行用 '|' 分隔，每行 15 个字符（WIDTH+5）
  // 服务器发的是 buffer[i][j]，i 从 0..2*HIGHT-1，j 从 0..WIDTH+4
  // 我们只关心 i = HIGHT..2*HIGHT-1，j = 1..WIDTH
  vector<string> rows;
  string cur;
  for (char ch : boardStr) {
    if (ch == '|') {
      rows.push_back(cur); 
      cur.clear(); 
    }
    else cur += ch;
  }
  if (!cur.empty()) rows.push_back(cur);

  // rows 有 2*HIGHT 行
  for (int i = HIGHT; i < 2 * HIGHT; i++) {
    if (i >= rows.size()) break;
    const string& r = rows[i];
    for (int j = 1; j < WIDTH + 1; j++) {
      if (j >= r.size()) { 
        p.buffer[i][j] = COLOR_EMPTY;
        continue; 
      }
      p.buffer[i][j] = r[j] - '0';
    }
  }
}

void handle_event(const string& body) {
  istringstream iss(body);
  string cmd;
  int playerId;
  string eventType;
  iss >> cmd >> playerId >> eventType;
  if (cmd != "EVENT") return;

  if (eventType == "GAMEOVER") {
    lock_guard<mutex> lk(g_players_mutex);
    if (g_players.count(playerId)) {
      g_players[playerId].alive = false;
    }
  }
}

// ============ 接收线程 ============
void recv_thread() {
  PacketReader reader;
  char buf[4096];

  while (g_running) {
    int n = recv(g_sock, buf, sizeof(buf), 0);
    if (n <= 0) break;

    reader.append(buf, n);

    string body;
    MsgType type;
    while (reader.next(body, type)) {
      switch (type) {
      case MsgType::Welcome: handle_welcome(body); break;
      case MsgType::PlayerJoin: handle_player_join(body); break;
      case MsgType::PlayerLeave: handle_player_leave(body); break;
      case MsgType::Start: handle_start(body); break;
      case MsgType::Snapshot: handle_snapshot(body); break;
      case MsgType::Event: handle_event(body); break;
      case MsgType::Ping: {
        // 收到 Ping，回 Pong，原样带回时间戳
        istringstream iss(body);
        string cmd;
        uint64_t t = 0;
        iss >> cmd >> t;
        // 发送信息依然要上锁
        {
          lock_guard<mutex> lk(g_send_mutex);
          send_packet(g_sock, MsgType::Pong, "PONG " + to_string(t));
        }
        break;
      }
      case MsgType::Error: {
        cout << "服务器错误: " << body << "        " << endl;
        break;
      }
      default: break;
      }
    }
  }

  g_running = false;
}

// ============ 发送消息 ============
void send_line(MsgType type, const string& body) {
  lock_guard<mutex> lk(g_send_mutex);
  send_packet(g_sock, type, body);
}

// ============ 键盘输入线程 ============
void input_thread() {
  HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
  bool downHeld = false;

  while (g_running) {
    // 使用 ReadConsoleInput 检测按下和松开
    DWORD count = 0;
    GetNumberOfConsoleInputEvents(hIn, &count);

    if (count > 0) {
      INPUT_RECORD rec;
      DWORD read = 0;
      ReadConsoleInput(hIn, &rec, 1, &read);

      if (rec.EventType == KEY_EVENT) {
        bool down = rec.Event.KeyEvent.bKeyDown;
        WORD vk = rec.Event.KeyEvent.wVirtualKeyCode;

        if (down) {
          switch (vk) {
          case VK_LEFT:  send_line(MsgType::Input, "INPUT LEFT"); break;
          case VK_RIGHT: send_line(MsgType::Input, "INPUT RIGHT"); break;
          case VK_UP:    send_line(MsgType::Input, "INPUT UP"); break;
          case VK_SPACE: send_line(MsgType::Input, "INPUT SPACE"); break;
          case 'Q':      send_line(MsgType::Input, "INPUT QUIT"); break;
          case VK_DOWN:
            if (!downHeld) {
              downHeld = true;
              send_line(MsgType::Input, "INPUT DOWN_START");
            }
            break;
          }
        } else {
          // 松开
          if (vk == VK_DOWN) {
            downHeld = false;
            send_line(MsgType::Input, "INPUT DOWN_END");
          }
        }
      }
    }

    this_thread::sleep_for(milliseconds(10));
  }
}

// ============ 渲染线程 ============
void render_thread() {
  while (g_running) {
    render();
    this_thread::sleep_for(milliseconds(100));
  }
}

// ============ 主函数 ============
int main() {
  // 隐藏光标
  cout << "\033[?25l";
  
  // Winsock 初始化
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);

  // 输入服务器 IP
  string ip;
  string name;
  cout << "请输入服务器 IP: ";
  cin >> ip;
  cout << "请输入你的名字: ";
  cin >> name;

  // 创建socket
  g_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (g_sock == INVALID_SOCKET) {
    cout << "创建 socket 失败" << endl;
    return -1;
  }

  sockaddr_in target{};
  target.sin_family = AF_INET;
  target.sin_port = htons(8080);
  // 字符串IP转整数
  target.sin_addr.s_addr = inet_addr(ip.c_str());

  if (connect(g_sock, (sockaddr*)&target, sizeof(target)) == SOCKET_ERROR) {
    cout << "连接服务器失败" << endl;
    closesocket(g_sock);
    return -1;
  }

  cout << "连接成功！" << endl;

  // 进入游戏界面
  clearScreen();

  // 启动接收线程
  thread(recv_thread).detach();

  // 发送 JOIN
  this_thread::sleep_for(milliseconds(100));
  send_line(MsgType::Join, "JOIN " + name);

  // 等待服务器分配 ID
  this_thread::sleep_for(milliseconds(200));

  // 发送 READY
  send_line(MsgType::Ready, "READY");

  // 启动输入线程和渲染线程
  thread(input_thread).detach();
  thread(render_thread).detach();

  // 主线程等待
  while (g_running) {
    this_thread::sleep_for(milliseconds(500));
  }

  closesocket(g_sock);
  WSACleanup();

  // 恢复光标
  cout << "\033[?25h";
  return 0;
}
