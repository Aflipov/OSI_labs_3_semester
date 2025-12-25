// sea_battle_client.c
#include "common.h"
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <zmq.h>

// ===================== CONFIG =====================
#define MAX_PLAYERS 2
#define BOARD_SIZE 10
#define MAX_SHIPS 10
#define MAX_PLAYER_NAME 50
#define MAX_GAME_NAME 50
#define MAX_MESSAGE_SIZE 1024
#define SERVER_PORT "5555"

// ===================== GLOBALS =====================
static char player_login[MAX_PLAYER_NAME] = "";
static int current_game_id = -1;
static int my_board[BOARD_SIZE][BOARD_SIZE];
static int opponent_shots[BOARD_SIZE][BOARD_SIZE];
static bool in_game = false;
static bool game_started = false;
static bool my_turn = false;

// ===================== UTILS =====================
bool ask_yes_no(const char *question, bool default_value) {
  char buf[32];
  int c;
  while ((c = getchar()) != '\n' && c != EOF)
    ;

  while (1) {
    printf("%s [%c/%c]: ", question, default_value ? 'Y' : 'y',
           default_value ? 'n' : 'N');
    if (!fgets(buf, sizeof(buf), stdin))
      return default_value;
    buf[strcspn(buf, "\n")] = '\0';
    if (buf[0] == '\0')
      return default_value;
    char ch = tolower((unsigned char)buf[0]);
    if (ch == 'y')
      return true;
    if (ch == 'n')
      return false;
    printf("Please enter 'y' or 'n'.\n");
  }
}

// ===================== MESSAGE =====================
int send_message(void *socket, Message *msg) {
  return zmq_send(socket, msg, sizeof(Message), 0);
}

int receive_message(void *socket, Message *msg) {
  int size = zmq_recv(socket, msg, sizeof(Message), 0);
  return size == sizeof(Message);
}

int receive_message_nonblock(void *socket, Message *msg) {
  int size = zmq_recv(socket, msg, sizeof(Message), ZMQ_DONTWAIT);
  return size == sizeof(Message);
}

// ===================== CLIENT LOGIC =====================
bool register_player(void *socket, const char *login) {
  Message msg = {0};
  msg.type = MSG_REGISTER;
  strncpy(msg.sender, login, MAX_PLAYER_NAME - 1);
  strncpy(msg.recipient, "SERVER", MAX_PLAYER_NAME - 1);
  send_message(socket, &msg);

  Message resp;
  if (receive_message(socket, &resp) && resp.type == MSG_ACK) {
    printf("Registered as %s\n", login);
    strncpy(player_login, login, MAX_PLAYER_NAME - 1);
    return true;
  }
  printf("Registration failed\n");
  return false;
}

bool create_game(void *socket, const char *game_name) {
  Message msg = {0};
  msg.type = MSG_CREATE_GAME;
  strncpy(msg.sender, player_login, MAX_PLAYER_NAME - 1);
  strncpy(msg.recipient, "SERVER", MAX_PLAYER_NAME - 1);
  strncpy(msg.game_name, game_name, MAX_GAME_NAME - 1);
  send_message(socket, &msg);
  printf("Create game request sent\n");
  in_game = true;
  return true;
}

bool join_game(void *socket, const char *game_name) {
  Message msg = {0};
  msg.type = MSG_JOIN_GAME;
  strncpy(msg.sender, player_login, MAX_PLAYER_NAME - 1);
  strncpy(msg.recipient, "SERVER", MAX_PLAYER_NAME - 1);
  strncpy(msg.game_name, game_name, MAX_GAME_NAME - 1);
  send_message(socket, &msg);
  in_game = true;
  printf("Join game request sent\n");
  return true;
}

bool place_ship_on_board(void *socket, int x, int y, int size, int horizontal) {
  Message msg = {0};
  msg.type = MSG_PLACE_SHIP;
  strncpy(msg.sender, player_login, MAX_PLAYER_NAME - 1);
  strncpy(msg.recipient, "SERVER", MAX_PLAYER_NAME - 1);
  snprintf(msg.data, sizeof(msg.data), "%d,%d,%d,%d", x, y, size, horizontal);
  send_message(socket, &msg);
  if (place_ship(my_board, x, y, size, horizontal)) {
    printf("Ship placed at (%d,%d) size %d %s\n", x, y, size,
           horizontal ? "H" : "V");
    return true;
  }
  return false;
}

ShotResult make_shot_to_opponent(void *socket, int x, int y) {
  Message msg = {0};
  msg.type = MSG_MAKE_SHOT;
  strncpy(msg.sender, player_login, MAX_PLAYER_NAME - 1);
  strncpy(msg.recipient, "SERVER", MAX_PLAYER_NAME - 1);
  msg.x = x;
  msg.y = y;
  send_message(socket, &msg);
  opponent_shots[y][x] = 1;
  return SHOT_HIT; // Ответ сервер пришлёт отдельно
}

// ===================== INCOMING PROCESS =====================
void handle_server_response(void *socket, Message *msg) {
  switch (msg->type) {
  case MSG_GAME_STATE:
    printf("[GAME STATE] %s\n", msg->data);
    my_turn = (strcmp(msg->data, "Your turn!") == 0);
    break;
  case MSG_SHOT_RESULT:
    printf("Shot result at (%d,%d): %s\n", msg->x, msg->y, msg->data);
    if (msg->shot_result == SHOT_MISS)
      my_turn = true;
    break;
  case MSG_INVITE_PLAYER:
    printf("Invite: %s\nJoin? (y/n): ", msg->data);
    char ans;
    scanf(" %c", &ans);
    if (ans == 'y' || ans == 'Y')
      join_game(socket, msg->game_name);
    break;
  case MSG_GAME_OVER:
    printf("GAME OVER: %s\n", msg->data);
    game_started = false;
    in_game = false;
    break;
  default:
    break;
  }
}

void process_incoming(void *socket) {
  Message msg;
  while (receive_message_nonblock(socket, &msg)) {
    handle_server_response(socket, &msg);
  }
}

// ===================== SHIP PLACEMENT =====================
void place_ships_manually(void *socket) {
  int ships[] = {4, 3, 3, 2, 2, 2, 1, 1, 1, 1};
  init_board(my_board);
  int placed = 0;
  while (placed < MAX_SHIPS) {
    print_board(my_board, true);
    int x, y, h;
    printf("Place ship %d/%d (size %d, h=1/0): ", placed + 1, MAX_SHIPS,
           ships[placed]);
    if (scanf("%d %d %d", &x, &y, &h) == 3) {
      if (place_ship_on_board(socket, x, y, ships[placed], h))
        placed++;
    } else
      while (getchar() != '\n')
        ;
  }
}

void auto_place_ships(void *socket) {
  int ships[] = {4, 3, 3, 2, 2, 2, 1, 1, 1, 1};
  init_board(my_board);
  srand(time(NULL));
  int placed = 0;
  while (placed < MAX_SHIPS) {
    int size = ships[placed];
    int x = rand() % BOARD_SIZE, y = rand() % BOARD_SIZE, h = rand() % 2;
    if (place_ship_on_board(socket, x, y, size, h))
      placed++;
  }
}

void select_ships_placement_mode(void *socket) {
  if (ask_yes_no("Auto place ships?", true))
    auto_place_ships(socket);
  else
    place_ships_manually(socket);
}

// ===================== GAME LOOP =====================
void game_loop(void *socket) {
  game_started = true;
  while (game_started) {
    process_incoming(socket);
    print_boards_side_by_side(my_board, opponent_shots);
    if (my_turn) {
      int x, y;
      printf("Your turn (x y): ");
      if (scanf("%d %d", &x, &y) == 2) {
        make_shot_to_opponent(socket, x, y);
        my_turn = false;
      } else
        while (getchar() != '\n')
          ;
    } else {
      sleep(1);
      Message req = {0};
      req.type = MSG_GAME_STATE;
      strncpy(req.sender, player_login, MAX_PLAYER_NAME - 1);
      send_message(socket, &req);
    }
  }
}

// ===================== MENU =====================
void show_menu() {
  printf("\n1. Create game\n2. Join game\n3. List games\n4. Start game\n5. "
         "Exit\nChoice: ");
}

// ===================== MAIN =====================
int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("Usage: %s <login>\n", argv[0]);
    return 1;
  }
  const char *login = argv[1];
  strncpy(player_login, login, MAX_PLAYER_NAME - 1);

  void *context = zmq_ctx_new();
  void *socket = zmq_socket(context, ZMQ_DEALER);
  zmq_setsockopt(socket, ZMQ_IDENTITY, login, strlen(login));
  char address[100];
  snprintf(address, sizeof(address), "tcp://localhost:%s", SERVER_PORT);
  if (zmq_connect(socket, address) != 0) {
    fprintf(stderr, "Connect error: %s\n", zmq_strerror(errno));
    return 1;
  }

  if (!register_player(socket, login)) {
    zmq_close(socket);
    zmq_ctx_destroy(context);
    return 1;
  }

  init_board(my_board);
  init_board(opponent_shots);

  while (1) {
    process_incoming(socket);
    show_menu();
    int choice;
    if (scanf("%d", &choice) != 1) {
      while (getchar() != '\n')
        ;
      continue;
    }
    switch (choice) {
    case 1: {
      char name[MAX_GAME_NAME];
      printf("Enter game name: ");
      scanf("%s", name);
      create_game(socket, name);
      break;
    }
    case 2: {
      char name[MAX_GAME_NAME];
      printf("Enter game name: ");
      scanf("%s", name);
      if (join_game(socket, name)) {
        select_ships_placement_mode(socket);
        game_loop(socket);
      }
      break;
    }
    case 3: {
      printf("Listing not implemented yet\n");
      break;
    }
    case 4: {
      if (in_game && !game_started) {
        select_ships_placement_mode(socket);
        game_loop(socket);
      } else
        printf("Cannot start game\n");
      break;
    }
    case 5: {
      printf("Exiting...\n");
      zmq_close(socket);
      zmq_ctx_destroy(context);
      return 0;
    }
    default:
      printf("Invalid choice\n");
      break;
    }
  }

  zmq_close(socket);
  zmq_ctx_destroy(context);
  return 0;
}
