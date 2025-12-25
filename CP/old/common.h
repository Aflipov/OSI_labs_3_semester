#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// #include <time.h>
#include <unistd.h>
#include <zmq.h>

#define MAX_PLAYERS 2
#define BOARD_SIZE 10
#define MAX_SHIPS 10
#define MAX_GAMES 100
#define MAX_PLAYER_NAME 50
#define MAX_GAME_NAME 50
#define MAX_MESSAGE_SIZE 1024
#define SERVER_PORT "5555"

// Типы сообщений
typedef enum {
  MSG_REGISTER = 1,
  MSG_CREATE_GAME,
  MSG_JOIN_GAME,
  MSG_INVITE_PLAYER,
  MSG_GAME_STATE,
  MSG_TURN_ORDER,
  MSG_PLACE_SHIP,
  MSG_MAKE_SHOT,
  MSG_SHOT_RESULT,
  MSG_GAME_OVER,
  MSG_ERROR,
  MSG_ACK,
  MSG_LIST_GAMES,
  MSG_LIST_PLAYERS
} MessageType;

// Статус игры
typedef enum {
  GAME_WAITING = 0,
  GAME_PLACING_SHIPS,
  GAME_PLAYING,
  GAME_FINISHED
} GameStatus;

// Результат выстрела
typedef enum { SHOT_MISS = 0, SHOT_HIT, SHOT_SUNK, SHOT_INVALID } ShotResult;

// Структура сообщения
typedef struct {
  MessageType type;
  char sender[MAX_PLAYER_NAME];
  char recipient[MAX_PLAYER_NAME];
  char game_name[MAX_GAME_NAME];
  char data[MAX_MESSAGE_SIZE];
  int x, y;
  ShotResult shot_result;
  int game_id;
} Message;

// Структура игрока
typedef struct {
  char login[MAX_PLAYER_NAME];
  void *socket;
  int game_id;
  bool in_game;
  bool ready;
} Player;

// Структура игры
typedef struct {
  int id;
  char name[MAX_GAME_NAME];
  char players[MAX_PLAYERS][MAX_PLAYER_NAME];
  int player_count;
  GameStatus status;
  int current_turn; // Индекс игрока, чей ход
  int boards[MAX_PLAYERS][BOARD_SIZE]
            [BOARD_SIZE]; // 0 - пусто, 1 - корабль, 2 - промах, 3 - попадание
  int shots[MAX_PLAYERS][BOARD_SIZE][BOARD_SIZE]; // История выстрелов
  int ships_remaining[MAX_PLAYERS]; // Количество оставшихся кораблей
} Game;

// Функции для работы с сообщениями
void send_message(void *socket, Message *msg);
int receive_message(void *socket, Message *msg);
int receive_message_nonblock(void *socket, Message *msg);
void print_message(Message *msg);

// Функции для работы с игрой
void init_board(int board[BOARD_SIZE][BOARD_SIZE]);
bool place_ship(int board[BOARD_SIZE][BOARD_SIZE], int x, int y, int size,
                int horizontal);
bool is_valid_placement(int board[BOARD_SIZE][BOARD_SIZE], int x, int y,
                        int size, int horizontal);
ShotResult make_shot(int board[BOARD_SIZE][BOARD_SIZE],
                     int shots[BOARD_SIZE][BOARD_SIZE], int x, int y);
bool check_game_over(int board[BOARD_SIZE][BOARD_SIZE]);
void print_board(int board[BOARD_SIZE][BOARD_SIZE], bool show_ships);
void print_boards_side_by_side(int my_board[BOARD_SIZE][BOARD_SIZE],
                               int enemy_board[BOARD_SIZE][BOARD_SIZE]);

#endif // COMMON_H
