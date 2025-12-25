#include "common.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <sys/wait.h>
#include <time.h>

static char player_login[MAX_PLAYER_NAME] = "";
static int current_game_id = -1;
static int my_board[BOARD_SIZE][BOARD_SIZE];
static int opponent_shots[BOARD_SIZE][BOARD_SIZE];
static bool in_game = false;
static bool game_started = false;
static bool my_turn = false;

bool ask_yes_no(const char *question, bool default_value) {
  char buf[32];

  int c;
  while ((c = getchar()) != '\n' && c != EOF)
    ;

  while (1) {
    printf("%s [%c/%c]: ", question, default_value ? 'Y' : 'y',
           default_value ? 'n' : 'N');

    if (!fgets(buf, sizeof(buf), stdin)) {
      // EOF → используем дефолт
      return default_value;
    }

    // Убираем \n
    buf[strcspn(buf, "\n")] = '\0';

    // Пустой ввод → дефолт
    if (buf[0] == '\0') {
      return default_value;
    }

    // Берём первый значащий символ
    char c = tolower((unsigned char)buf[0]);

    if (c == 'y')
      return true;
    if (c == 'n')
      return false;

    printf("Please enter 'y' or 'n'.\n");
  }
}

int send_message(void *socket, Message *msg) {
  int size = zmq_send(socket, msg, sizeof(Message), 0);
  return size == sizeof(Message);
}

int receive_message(void *socket, Message *msg) {
  int size = zmq_recv(socket, msg, sizeof(Message), 0);
  return size == sizeof(Message);
}

int receive_message_nonblock(void *socket, Message *msg) {
  int size = zmq_recv(socket, msg, sizeof(Message), ZMQ_DONTWAIT);
  return size == sizeof(Message);
}

bool register_player(void *socket, const char *login) {
  Message msg = {0};
  msg.type = MSG_REGISTER;
  strncpy(msg.sender, login, MAX_PLAYER_NAME - 1);
  strncpy(msg.recipient, "SERVER", MAX_PLAYER_NAME - 1);

  send_message(socket, &msg);

  Message response = {0};
  if (receive_message(socket, &response)) {
    if (response.type == MSG_ACK) {
      printf("Successfully registered as %s\n", login);
      strncpy(player_login, login, MAX_PLAYER_NAME - 1);
      return true;
    } else {
      printf("Registration failed: %s\n", response.data);
      return false;
    }
  }
  return false;
}

bool create_game(void *socket, const char *game_name) {
  Message msg = {0};
  msg.type = MSG_CREATE_GAME;
  strncpy(msg.sender, player_login, MAX_PLAYER_NAME - 1);
  strncpy(msg.recipient, "SERVER", MAX_PLAYER_NAME - 1);
  strncpy(msg.game_name, game_name, MAX_GAME_NAME - 1);

  send_message(socket, &msg);

  Message response = {0};
  if (receive_message(socket, &response)) {
    if (response.type == MSG_ACK) {
      current_game_id = response.game_id;
      in_game = true;
      printf("Game '%s' created successfully (ID: %d)\n", game_name,
             current_game_id);
      return true;
    } else {
      printf("Failed to create game: %s\n", response.data);
      return false;
    }
  }
  return false;
}

bool join_game(void *socket, const char *game_name) {
  Message msg = {0};
  msg.type = MSG_JOIN_GAME;
  strncpy(msg.sender, player_login, MAX_PLAYER_NAME - 1);
  strncpy(msg.recipient, "SERVER", MAX_PLAYER_NAME - 1);
  strncpy(msg.game_name, game_name, MAX_GAME_NAME - 1);

  send_message(socket, &msg);

  Message response = {0};
  if (receive_message(socket, &response)) {
    if (response.type == MSG_ACK) {
      current_game_id = response.game_id;
      in_game = true;
      printf("Joined game '%s' successfully (ID: %d)\n", game_name,
             current_game_id);
      printf("%s\n", response.data);
      return true;
    } else {
      printf("Failed to join game: %s\n", response.data);
      return false;
    }
  }
  return false;
}

bool invite_player(void *socket, const char *player_name) {
  Message msg = {0};
  msg.type = MSG_INVITE_PLAYER;
  strncpy(msg.sender, player_login, MAX_PLAYER_NAME - 1);
  strncpy(msg.recipient, player_name, MAX_PLAYER_NAME - 1);

  send_message(socket, &msg);

  Message response = {0};
  if (receive_message(socket, &response)) {
    if (response.type == MSG_ACK) {
      printf("Invitation sent to %s\n", player_name);
      return true;
    } else {
      printf("Failed to invite player: %s\n", response.data);
      return false;
    }
  }
  return false;
}

bool place_ship_on_board(void *socket, int x, int y, int size, int horizontal) {
  Message msg = {0};
  msg.type = MSG_PLACE_SHIP;
  strncpy(msg.sender, player_login, MAX_PLAYER_NAME - 1);
  strncpy(msg.recipient, "SERVER", MAX_PLAYER_NAME - 1);
  msg.x = x;
  msg.y = y;
  snprintf(msg.data, MAX_MESSAGE_SIZE, "%d,%d,%d,%d", x, y, size, horizontal);

  send_message(socket, &msg);

  Message response = {0};
  if (receive_message(socket, &response)) {
    if (response.type == MSG_ACK) {
      if (place_ship(my_board, x, y, size, horizontal)) {
        printf("Ship placed at (%d,%d) size %d %s\n", x, y, size,
               horizontal == 1 ? "horizontal" : "vertical");
        return true;
      }
    } else {
      printf("Failed to place ship: %s\n", response.data);
      return false;
    }
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

  Message response = {0};
  if (receive_message(socket, &response)) {
    if (response.type == MSG_SHOT_RESULT) {
      opponent_shots[x][y] = (response.shot_result == SHOT_MISS) ? 2 : 1;
      printf("Shot at (%d,%d): %s\n", x, y, response.data);
      return response.shot_result;
    } else if (response.type == MSG_ERROR) {
      printf("Error: %s\n", response.data);
      return SHOT_INVALID;
    }
  }
  return SHOT_INVALID;
}

void list_games(void *socket) {
  Message msg = {0};
  msg.type = MSG_LIST_GAMES;
  strncpy(msg.sender, player_login, MAX_PLAYER_NAME - 1);
  strncpy(msg.recipient, "SERVER", MAX_PLAYER_NAME - 1);

  send_message(socket, &msg);

  Message response = {0};
  if (receive_message(socket, &response)) {
    if (response.type == MSG_LIST_GAMES) {
      printf("%s\n", response.data);
    }
  }
}

void place_ships_manually(void *socket) {
  printf("You need to place ships. Format: x y size horizontal(1/0)\n");
  printf("Example: 0 0 4 1 (places 4-cell ship at (0,0) horizontally)\n");
  printf("Ships: 1x4, 2x3, 3x2, 4x1\n");

  int ships[] = {4, 3, 3, 2, 2, 2, 1, 1, 1, 1};
  int placed = 0;

  init_board(my_board);

  while (placed < MAX_SHIPS) {
    printf("\nYour board:\n");
    print_board(my_board, true);
    printf("\nPlace ship %d/%d (size %d): ", placed + 1, MAX_SHIPS,
           ships[placed]);

    int x, y, size, h;
    if (scanf("%d %d %d %d", &x, &y, &size, &h) == 4) {
      if (size != ships[placed]) {
        printf("Wrong ship size! Expected %d\n", ships[placed]);
        continue;
      }

      if (place_ship_on_board(socket, x, y, size, h)) {
        placed++;
      }
    } else {
      printf("Invalid input. Try again.\n");
      while (getchar() != '\n')
        ; // Очистка буфера
    }
  }

  printf("\nAll ships placed!\n");
  printf("Your final board:\n");
  print_board(my_board, true);
}

void auto_place_ships(void *socket) {
  printf("Auto placing ships: 1x4, 2x3, 3x2, 4x1\n");
  int ships[] = {4, 3, 3, 2, 2, 2, 1, 1, 1, 1};
  int placed = 0;

  init_board(my_board);

  srand(time(NULL));

  while (placed < MAX_SHIPS) {
    int size = ships[placed];

    int x = rand() % BOARD_SIZE;
    int y = rand() % BOARD_SIZE;
    int h = rand() % 2;

    if (place_ship_on_board(socket, x, y, size, h)) {
      placed++;
    }
  }

  printf("All ships placed automatically!\n");
}

void select_ships_placement_mode(void *socket) {
  printf("\n=== Placing Ships ===\n");
  bool autoPlacement =
      ask_yes_no("Whould you like to try auto ship placement?", true);
  if (autoPlacement) {
    auto_place_ships(socket);
  } else {
    place_ships_manually(socket);
  }
}

void request_game_state(void *socket) {
  Message req = {0};
  req.type = MSG_GAME_STATE;
  strncpy(req.sender, player_login, MAX_PLAYER_NAME - 1);
  send_message(socket, &req);
}

void handle_server_response(void *socket, Message *msg) {
  if (msg->type == MSG_SHOT_RESULT) {
    if (strcmp(msg->recipient, player_login) == 0) {
      printf("Opponent shot at (%d,%d): %s\n", msg->x, msg->y, msg->data);
      if (msg->shot_result == SHOT_MISS) {
        my_turn = true;
      }
    }
  } else if (msg->type == MSG_GAME_STATE) {
    printf("[GAME STATE] %s\n", msg->data);

    my_turn = (strcmp(msg->data, "Your turn!") == 0);

  } else if (msg->type == MSG_GAME_OVER) {
    printf("\n=== GAME OVER ===\n");
    printf("%s\n", msg->data);
    game_started = false;
    in_game = false;
    return;
  } else if (msg->type == MSG_INVITE_PLAYER) {
    printf("\n=== INVITATION ===\n");
    printf("%s\n", msg->data);
    printf("Do you want to join game '%s'? (y/n): ", msg->game_name);
    char answer;
    scanf(" %c", &answer);
    if (answer == 'y' || answer == 'Y') {
      join_game(socket, msg->game_name);
    }
  }
}

void process_incoming(void *socket) {
  Message msg;
  while (receive_message_nonblock(socket, &msg)) {
    handle_server_response(socket, &msg);
  }
}

void game_loop(void *socket) {
  printf("\n=== Game Started ===\n");
  game_started = true;

  // printf("pr_rgs\n");
  request_game_state(socket);
  // printf("pst_rgs\n");

  while (game_started) {
    printf("\n");
    print_boards_side_by_side(my_board, opponent_shots);
    // printf("Your board (ships visible):\n");
    // print_board(my_board, true);
    // printf("\nOpponent board (shots only):\n");
    // print_board(opponent_shots, false);
    Message msg = {0};

    if (my_turn) {
      printf("\nYour turn! Enter coordinates (x y): ");
      int x, y;
      if (scanf("%d %d", &x, &y) == 2) {
        ShotResult result = make_shot_to_opponent(socket, x, y);
        if (result == SHOT_MISS) {
          my_turn = false;
        } else if (result == SHOT_INVALID) {
          printf("Invalid shot. Try again.\n");
        }
      } else {
        printf("Invalid input.\n");
        while (getchar() != '\n')
          ;
      }
    } else {
      // printf("\nWaiting for opponent's turn...\n");

      printf("%s\n", msg.data);
      sleep(1);
      request_game_state(socket);
    }

    // Проверка сообщений от сервера
    if (receive_message_nonblock(socket, &msg)) {
      printf("%s\n", msg.data);
      handle_server_response(socket, &msg);
    }
  }
}

void show_menu() {
  printf("\n=== Sea Battle Menu ===\n");
  printf("1. Create game\n");
  printf("2. Join game\n");
  printf("3. Invite player\n");
  printf("4. List games\n");
  printf("5. Start game (if in game)\n");
  printf("6. Exit\n");
  printf("Choice: ");
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("Usage: %s <login>\n", argv[0]);
    return 1;
  }

  const char *login = argv[1];

  void *context = zmq_ctx_new();
  void *socket = zmq_socket(context, ZMQ_DEALER);
  zmq_setsockopt(socket, ZMQ_IDENTITY, player_login, strlen(player_login));

  char address[100];
  snprintf(address, sizeof(address), "tcp://localhost:%s", SERVER_PORT);

  if (zmq_connect(socket, address) != 0) {
    fprintf(stderr, "Error connecting to server: %s\n", zmq_strerror(errno));
    return 1;
  }

  printf("Connecting to server...\n");

  // Регистрация
  if (!register_player(socket, login)) {
    zmq_close(socket);
    zmq_ctx_destroy(context);
    return 1;
  }

  // Инициализация досок
  init_board(my_board);
  init_board(opponent_shots);

  // Главный цикл
  while (1) {
    show_menu();
    int choice;
    if (scanf("%d", &choice) != 1) {
      printf("Invalid input.\n");
      while (getchar() != '\n')
        ;
      continue;
    }

    switch (choice) {
    case 1: {
      char game_name[MAX_GAME_NAME];
      printf("Enter game name: ");
      scanf("%s", game_name);
      create_game(socket, game_name);
      break;
    }
    case 2: {
      char game_name[MAX_GAME_NAME];
      printf("Enter game name: ");
      scanf("%s", game_name);
      if (join_game(socket, game_name)) {
        select_ships_placement_mode(socket);
        game_loop(socket);
      }
      break;
    }
    case 3: {
      if (!in_game) {
        printf("You are not in a game.\n");
        break;
      }
      char player_name[MAX_PLAYER_NAME];
      printf("Enter player login to invite: ");
      scanf("%s", player_name);
      invite_player(socket, player_name);
      break;
    }
    case 4: {
      list_games(socket);
      break;
    }
    case 5: {
      if (in_game && !game_started) {
        select_ships_placement_mode(socket);
        game_loop(socket);
      } else if (!in_game) {
        printf("You are not in a game.\n");
      } else {
        printf("Game already started.\n");
      }
      break;
    }
    case 6: {
      printf("Exiting...\n");
      zmq_close(socket);
      zmq_ctx_destroy(context);
      return 0;
    }
    default:
      printf("Invalid choice.\n");
      break;
    }

    // Проверка входящих сообщений (приглашения и т.д.)
    Message msg = {0};
    if (receive_message_nonblock(socket, &msg)) {
      if (msg.type == MSG_INVITE_PLAYER) {
        printf("\n=== INVITATION ===\n");
        printf("%s\n", msg.data);
        printf("Do you want to join game '%s'? (y/n): ", msg.game_name);
        char answer;
        scanf(" %c", &answer);
        if (answer == 'y' || answer == 'Y') {
          if (join_game(socket, msg.game_name)) {
            select_ships_placement_mode(socket);
            game_loop(socket);
          }
        }
      } else if (msg.type == MSG_ACK && in_game && !game_started) {
        if (strstr(msg.data, "joined") != NULL ||
            strstr(msg.data, "Start placing") != NULL) {
          printf("%s\n", msg.data);
          select_ships_placement_mode(socket);
          game_loop(socket);
        }
      }
    }
  }

  zmq_close(socket);
  zmq_ctx_destroy(context);
  return 0;
}
