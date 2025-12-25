#include "common.h"

// ==================== MESSAGE FUNCTIONS ====================

void print_message(Message *msg) {
  printf("Message: type=%d sender=%s recipient=%s game=%s data=%s x=%d y=%d "
         "result=%d id=%d\n",
         msg->type, msg->sender, msg->recipient, msg->game_name, msg->data,
         msg->x, msg->y, msg->shot_result, msg->game_id);
}

// ==================== BOARD FUNCTIONS ====================

void init_board(int board[BOARD_SIZE][BOARD_SIZE]) {
  for (int y = 0; y < BOARD_SIZE; y++)
    for (int x = 0; x < BOARD_SIZE; x++)
      board[y][x] = 0;
}

bool is_valid_placement(int board[BOARD_SIZE][BOARD_SIZE], int x, int y,
                        int size, int horizontal) {
  if (horizontal) {
    if (x + size > BOARD_SIZE)
      return false;
    for (int i = 0; i < size; i++)
      if (board[y][x + i] != 0)
        return false;
  } else {
    if (y + size > BOARD_SIZE)
      return false;
    for (int i = 0; i < size; i++)
      if (board[y + i][x] != 0)
        return false;
  }
  return true;
}

bool place_ship(int board[BOARD_SIZE][BOARD_SIZE], int x, int y, int size,
                int horizontal) {
  if (!is_valid_placement(board, x, y, size, horizontal))
    return false;
  if (horizontal)
    for (int i = 0; i < size; i++)
      board[y][x + i] = 1;
  else
    for (int i = 0; i < size; i++)
      board[y + i][x] = 1;
  return true;
}

ShotResult make_shot(int board[BOARD_SIZE][BOARD_SIZE], int x, int y) {
  if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE)
    return SHOT_INVALID;
  if (board[y][x] == 1) {
    board[y][x] = 3;
    return SHOT_HIT;
  }
  if (board[y][x] == 0) {
    board[y][x] = 2;
    return SHOT_MISS;
  }
  return SHOT_INVALID;
}

bool check_game_over(int board[BOARD_SIZE][BOARD_SIZE]) {
  for (int y = 0; y < BOARD_SIZE; y++)
    for (int x = 0; x < BOARD_SIZE; x++)
      if (board[y][x] == 1)
        return false;
  return true;
}

void print_board(int board[BOARD_SIZE][BOARD_SIZE], bool show_ships) {
  printf("  ");
  for (int x = 0; x < BOARD_SIZE; x++)
    printf("%d ", x);
  printf("\n");
  for (int y = 0; y < BOARD_SIZE; y++) {
    printf("%d ", y);
    for (int x = 0; x < BOARD_SIZE; x++) {
      char c = '.';
      if (board[y][x] == 1 && show_ships)
        c = 'S';
      else if (board[y][x] == 2)
        c = '*';
      else if (board[y][x] == 3)
        c = 'X';
      printf("%c ", c);
    }
    printf("\n");
  }
}

void print_boards_side_by_side(int my_board[BOARD_SIZE][BOARD_SIZE],
                               int enemy_board[BOARD_SIZE][BOARD_SIZE]) {
  printf("Your board:              Opponent board:\n");
  printf("  ");
  for (int x = 0; x < BOARD_SIZE; x++)
    printf("%d ", x);
  printf("       ");
  for (int x = 0; x < BOARD_SIZE; x++)
    printf("%d ", x);
  printf("\n");

  for (int y = 0; y < BOARD_SIZE; y++) {
    printf("%d ", y);
    for (int x = 0; x < BOARD_SIZE; x++) {
      char c = '.';
      if (my_board[y][x] == 1)
        c = 'S';
      else if (my_board[y][x] == 2)
        c = '*';
      else if (my_board[y][x] == 3)
        c = 'X';
      printf("%c ", c);
    }
    printf("     %d ", y);
    for (int x = 0; x < BOARD_SIZE; x++) {
      char c = '.';
      if (enemy_board[y][x] == 1)
        c = '.'; // hidden
      else if (enemy_board[y][x] == 2)
        c = '*';
      else if (enemy_board[y][x] == 3)
        c = 'X';
      printf("%c ", c);
    }
    printf("\n");
  }
}
