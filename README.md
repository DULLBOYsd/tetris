# tetris

## something you may be interested in

- a repository for algorithm research of tetris.

- There's only a Cplus version for tetris game now but will be renewed in near future.

- This is a repository contains codes coded mostly by hand.

- This is a repository for Cplus practice, so all coded by using Cplus.

# introduction to different versions of tetris game

1. version1: a very basic version of tetris game, use pure Cplus to create a game frame, with most of the ability includes displaying current score/level/line and next shape. 
  1.1. version1.1: add a .txt file to record the highest score in history, and can be displayed.

2. version2: a trained version that can play this game by itself. this model uses Genetic Algorithm(GA) to achieve it. With this Cplus code, you can train your own tetris ai.
  2.2. version2.2: change some parameters(especially the SHAPE and rotateShape of these cubes) and make it more similar to a real game. And based on that, I retrain the model and let it just good for autoplay.
>something you should notice
>-  how to train a tetris ai?\
>You can use the file tetris_train.cpp, this is just a simulation data version of tetris_gene.cpp, without any render, only evolution of data displayed. It takes few minutes, but, to some extend, not so visualized.  
>Or, it's a good idea to use tetris_gene.cpp to train your model, then you can see how the process going on. You need to set variation "g_render" and "g_train" for true, but g_render takes time. Speed is lower than train mode.
>-  file you need download\
>If you only want to see how it works rather than experience what the process is, then you can only download file "tetris_gene.cpp", "log.txt" and "gene_progress.txt". Log file provide a record of statistics once when you carry on a train. And Cplus file will find the best trained data from gene_progess.txt.
>- how does it works?\
>I set 4 variations for each of 7 shapes, including linesCleared(double), aggregateHeight(double), holes(double) and bumpiness(double). These are called **FEARURES** in a GA content. And our orientation is to find the best parameter for these 4 * 7 variations. we don't need to find them by our own. Actually, by experiment, I find that 28 variations and double-precision floating-point type are enough for gaining an excellent result. And POP, GENS, ELITE and MUT_RATE can be changed if **OVERFITTING** occurs

3. version3: this is a demo version of tetris ai, auto-play this game by itself. I use **TETR.IO** for example. But seemingly now it performs just so-so in the game. And I'd like to add ability of auto-playing in multi-person game(just add the ability to recognize the gray(trash) cubes, that problem can be solved). 
>something you should notice
>within this version, I use visual recognization method to get pixels and their rgb amounts. BUT this is based on my own laptop(with an addtional screen), so you **CAN NOT** use this code directly. You have to know resolution and positions of pixels of your computer, **JUST CHANGE THESE** can solve everything.
- exist problems:
  1. resolution is different within different computers, and frame is displayed differently(become smaller on computers with higher resolution, vice versa).
  2. can not show a choice interface
  3. can not pause when game goes on
  4. the evolution velocity is not quick enough, with still a vast time taken for training from zero to seccess. 
  5. model needs to be trained, and this is closely related to the performance of such ai.
  
4. version4: this is a version of local server&client connection, by using socket in Cplus and ipv4 to connect. message header is defined in type below:
```cpp
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
```
- experience gotten from this project:
  1. how and where to use a lock, without causing deadlock.
  2. how to define a message header, and how to deal with message from & to server & client.
  3. how to use thread to simutaneously deal with several tasks.
  4. responsibility of server and client, what should they separately do. e.g. server should not carry out the code to render surface, which should be done by client.
