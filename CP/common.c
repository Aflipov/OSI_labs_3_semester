#include "common.h"
#include <stdbool.h>
#include <stdio.h>

// Отправка сообщения
int send_message(void *socket, Message *msg) {
  int size = zmq_send(socket, msg, sizeof(Message), 0);
  return size == sizeof(Message);
}

// Получение сообщения (блокирующее)
int receive_message(void *socket, Message *msg) {
  int size = zmq_recv(socket, msg, sizeof(Message), 0);
  return size == sizeof(Message);
}

// Получение сообщения (неблокирующее)
int receive_message_nonblock(void *socket, Message *msg) {
  int size = zmq_recv(socket, msg, sizeof(Message), ZMQ_DONTWAIT);
  return size == sizeof(Message);
}

// Инициализация доски
void init_board(int board[BOARD_SIZE][BOARD_SIZE]) {
  for (int i = 0; i < BOARD_SIZE; i++) {
    for (int j = 0; j < BOARD_SIZE; j++) {
      board[i][j] = 0;
    }
  }
}

// Проверка валидности размещения корабля
bool is_valid_placement(int board[BOARD_SIZE][BOARD_SIZE], int x, int y,
                        int size, int horizontal) {
  if (x < 0 || y < 0)
    return false;

  if (horizontal == 1) {
    if (x + size > BOARD_SIZE)
      return false;

    for (int row = y - 1; row <= y + 1; row++) {
      for (int col = x - 1; col <= x + size; col++) {
        if (row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE) {
          if (board[row][col] == 1) {
            // printf("h conflict at %d %d\n", row, col);
            return false;
          }
        }
      }
    }
  } else {
    if (y + size > BOARD_SIZE)
      return false;

    for (int row = y - 1; row <= y + size; row++) {
      for (int col = x - 1; col <= x + 1; col++) {
        if (row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE) {
          if (board[row][col] == 1) {
            // printf("nh conflict at %d %d", row, col);
            return false;
          }
        }
      }
    }
  }

  return true;
}

// Размещение корабля
bool place_ship(int board[BOARD_SIZE][BOARD_SIZE], int x, int y, int size,
                int horizontal) {
  if (!is_valid_placement(board, x, y, size, horizontal)) {
    return false;
  }

  if (horizontal == 1) {
    for (int i = 0; i < size; i++) {
      board[y][x + i] = 1;
    }
  } else {
    for (int i = 0; i < size; i++) {
      board[y + i][x] = 1;
    }
  }

  return true;
}

// Выстрел
ShotResult make_shot(int board[BOARD_SIZE][BOARD_SIZE],
                     int shots[BOARD_SIZE][BOARD_SIZE], int x, int y) {
  if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) {
    return SHOT_INVALID;
  }

  if (shots[x][y] != 0) {
    return SHOT_INVALID;
  }

  if (board[x][y] == 1) {
    shots[x][y] = 1;
    board[x][y] = 3; // Помечаем как попадание

    // Проверяем, потоплен ли корабль
    bool sunk = true;
    // Проверяем все клетки вокруг
    for (int dx = -1; dx <= 1; dx++) {
      for (int dy = -1; dy <= 1; dy++) {
        int nx = x + dx;
        int ny = y + dy;
        if (nx >= 0 && nx < BOARD_SIZE && ny >= 0 && ny < BOARD_SIZE) {
          if (board[nx][ny] == 1) {
            sunk = false;
            break;
          }
        }
      }
      if (!sunk)
        break;
    }

    // Более точная проверка: ищем все клетки корабля
    if (sunk) {
      // Проверяем горизонтальное направление
      for (int i = x - 1; i >= 0 && board[i][y] != 0 && board[i][y] != 2; i--) {
        if (board[i][y] == 1) {
          sunk = false;
          break;
        }
      }
      for (int i = x + 1;
           i < BOARD_SIZE && board[i][y] != 0 && board[i][y] != 2; i++) {
        if (board[i][y] == 1) {
          sunk = false;
          break;
        }
      }
      // Проверяем вертикальное направление
      for (int j = y - 1; j >= 0 && board[x][j] != 0 && board[x][j] != 2; j--) {
        if (board[x][j] == 1) {
          sunk = false;
          break;
        }
      }
      for (int j = y + 1;
           j < BOARD_SIZE && board[x][j] != 0 && board[x][j] != 2; j++) {
        if (board[x][j] == 1) {
          sunk = false;
          break;
        }
      }
    }

    return sunk ? SHOT_SUNK : SHOT_HIT;
  } else {
    shots[x][y] = 2;
    board[x][y] = 2; // Помечаем как промах
    return SHOT_MISS;
  }
}

// Проверка окончания игры
bool check_game_over(int board[BOARD_SIZE][BOARD_SIZE]) {
  for (int i = 0; i < BOARD_SIZE; i++) {
    for (int j = 0; j < BOARD_SIZE; j++) {
      if (board[i][j] == 1) {
        return false; // Есть непотопленные корабли
      }
    }
  }
  return true; // Все корабли потоплены
}

// Вывод доски
void print_board(int board[BOARD_SIZE][BOARD_SIZE], bool show_ships) {
  printf("   ");
  for (int j = 0; j < BOARD_SIZE; j++) {
    printf("%2d ", j);
  }
  printf("\n");

  for (int i = 0; i < BOARD_SIZE; i++) {
    printf("%2d ", i);
    for (int j = 0; j < BOARD_SIZE; j++) {
      if (board[i][j] == 0) {
        printf(" . ");
      } else if (board[i][j] == 1) {
        if (show_ships) {
          printf(" S ");
        } else {
          printf(" . ");
        }
      } else if (board[i][j] == 2) {
        printf(" O ");
      } else if (board[i][j] == 3) {
        printf(" X ");
      }
    }
    printf("\n");
  }
}

void print_boards_side_by_side(int my_board[BOARD_SIZE][BOARD_SIZE],
                               int enemy_board[BOARD_SIZE][BOARD_SIZE]) {
  // Заголовки
  printf("      YOUR BOARD                     OPPONENT BOARD\n");

  printf("   ");
  for (int j = 0; j < BOARD_SIZE; j++) {
    printf("%2d ", j);
  }

  printf(" |   ");

  for (int j = 0; j < BOARD_SIZE; j++) {
    printf("%2d ", j);
  }
  printf("\n");

  // Строки досок
  for (int i = 0; i < BOARD_SIZE; i++) {
    // Левая доска (твоя)
    printf("%2d ", i);
    for (int j = 0; j < BOARD_SIZE; j++) {
      if (my_board[i][j] == 0) {
        printf(" . ");
      } else if (my_board[i][j] == 1) {
        printf(" S ");
      } else if (my_board[i][j] == 2) {
        printf(" O ");
      } else if (my_board[i][j] == 3) {
        printf(" X ");
      }
    }

    // Разделитель
    printf(" | ");

    // Правая доска (противник)
    printf("%2d ", i);
    for (int j = 0; j < BOARD_SIZE; j++) {
      if (enemy_board[i][j] == 0 || enemy_board[i][j] == 1) {
        printf(" . "); // корабли противника скрыты
      } else if (enemy_board[i][j] == 2) {
        printf(" O ");
      } else if (enemy_board[i][j] == 3) {
        printf(" X ");
      }
    }

    printf("\n");
  }
}
