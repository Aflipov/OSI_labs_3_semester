#include "common.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

Player players[100];
Game games[MAX_GAMES];
int player_count = 0;
int game_count = 0;
int next_game_id = 1;

int server_receive(void *socket, char *identity, Message *msg) {
  if (zmq_recv(socket, identity, 256, 0) <= 0)
    return 0;

  zmq_recv(socket, NULL, 0, 0); // empty frame

  int size = zmq_recv(socket, msg, sizeof(Message), 0);
  return size == sizeof(Message);
}

void server_send(void *socket, const char *identity, Message *msg) {
  zmq_send(socket, identity, strlen(identity), ZMQ_SNDMORE);
  zmq_send(socket, "", 0, ZMQ_SNDMORE);
  zmq_send(socket, msg, sizeof(Message), 0);
}

Player *find_player(const char *login) {
  for (int i = 0; i < player_count; i++) {
    if (strcmp(players[i].login, login) == 0) {
      return &players[i];
    }
  }
  return NULL;
}

Game *find_game_by_name(const char *name) {
  for (int i = 0; i < game_count; i++) {
    if (strcmp(games[i].name, name) == 0) {
      return &games[i];
    }
  }
  return NULL;
}

Game *find_game_by_id(int id) {
  for (int i = 0; i < game_count; i++) {
    if (games[i].id == id) {
      return &games[i];
    }
  }
  return NULL;
}

Game *create_game(const char *name, const char *creator) {
  if (game_count >= MAX_GAMES) {
    return NULL;
  }

  Game *game = &games[game_count];
  game->id = next_game_id++;
  strncpy(game->name, name, MAX_GAME_NAME - 1);
  game->name[MAX_GAME_NAME - 1] = '\0';
  strncpy(game->players[0], creator, MAX_PLAYER_NAME - 1);
  game->players[0][MAX_PLAYER_NAME - 1] = '\0';
  game->player_count = 1;
  game->status = GAME_WAITING;
  game->current_turn = rand() % 2;

  for (int p = 0; p < MAX_PLAYERS; p++) {
    init_board(game->boards[p]);
    for (int i = 0; i < BOARD_SIZE; i++) {
      for (int j = 0; j < BOARD_SIZE; j++) {
        game->shots[p][i][j] = 0;
      }
    }
    game->ships_remaining[p] = 0;
  }

  game_count++;
  return game;
}

bool add_player_to_game(Game *game, const char *player_name) {
  if (game->player_count >= MAX_PLAYERS) {
    return false;
  }

  strncpy(game->players[game->player_count], player_name, MAX_PLAYER_NAME - 1);
  game->players[game->player_count][MAX_PLAYER_NAME - 1] = '\0';
  game->player_count++;

  if (game->player_count == MAX_PLAYERS) {
    game->status = GAME_PLACING_SHIPS;
  }

  return true;
}

void handle_register(void *socket, char *identity, Message *msg) {
  if (find_player(msg->sender)) {
    Message err = {.type = MSG_ERROR};
    strcpy(err.data, "Already registered");
    server_send(socket, identity, &err);
    return;
  }

  Player *p = &players[player_count++];
  strcpy(p->login, msg->sender);
  strcpy(p->identity, identity);
  p->in_game = false;
  p->ready = false;
  p->game_id = -1;

  Message ok = {.type = MSG_ACK};
  strcpy(ok.data, "Registered");
  server_send(socket, identity, &ok);
}

void handle_create_game(void *socket, char *identity, Message *msg) {
  Player *player = find_player(msg->sender);
  if (player == NULL) {
    Message response = {0};
    response.type = MSG_ERROR;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "Player not registered", MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);
    return;
  }

  if (player->in_game) {
    Message response = {0};
    response.type = MSG_ERROR;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "Already in a game", MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);
    return;
  }

  if (find_game_by_name(msg->game_name) != NULL) {
    Message response = {0};
    response.type = MSG_ERROR;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "Game name already exists", MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);
    return;
  }

  Game *game = create_game(msg->game_name, msg->sender);
  if (game == NULL) {
    Message response = {0};
    response.type = MSG_ERROR;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "Failed to create game", MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);
    return;
  }

  player->game_id = game->id;
  player->in_game = true;

  Message response = {0};
  response.type = MSG_ACK;
  response.game_id = game->id;
  strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
  strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
  strncpy(response.game_name, msg->game_name, MAX_GAME_NAME - 1);
  strncpy(response.data, "Game created successfully", MAX_MESSAGE_SIZE - 1);
  server_send(socket, identity, &response);

  printf("Game '%s' created by %s (ID: %d)\n", game->name, player->login,
         game->id);
}

void handle_join_game(void *socket, char *identity, Message *msg) {
  Player *player = find_player(msg->sender);
  if (player == NULL) {
    Message response = {0};
    response.type = MSG_ERROR;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "Player not registered", MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);
    return;
  }

  if (player->in_game) {
    Message response = {0};
    response.type = MSG_ERROR;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "Already in a game", MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);
    return;
  }

  Game *game = find_game_by_name(msg->game_name);
  if (game == NULL) {
    Message response = {0};
    response.type = MSG_ERROR;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "Game not found", MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);
    return;
  }

  if (game->player_count >= MAX_PLAYERS) {
    Message response = {0};
    response.type = MSG_ERROR;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "Game is full", MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);
    return;
  }

  if (!add_player_to_game(game, msg->sender)) {
    Message response = {0};
    response.type = MSG_ERROR;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "Failed to join game", MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);
    return;
  }

  player->game_id = game->id;
  player->in_game = true;

  // Уведомление обоих игроков
  for (int i = 0; i < game->player_count; i++) {
    Player *p = find_player(game->players[i]);
    if (p != NULL) {
      Message response = {0};
      response.type = MSG_ACK;
      response.game_id = game->id;
      strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
      strncpy(response.recipient, game->players[i], MAX_PLAYER_NAME - 1);
      strncpy(response.game_name, game->name, MAX_GAME_NAME - 1);
      if (i == game->player_count - 1) {
        strncpy(response.data, "Joined game successfully. Start placing ships!",
                MAX_MESSAGE_SIZE - 1);
      } else {
        snprintf(response.data, MAX_MESSAGE_SIZE,
                 "Player %s joined the game. Start placing ships!",
                 msg->sender);
      }
      server_send(socket, p->identity, &response);
    }
  }

  printf("Player %s joined game '%s' (ID: %d)\n", player->login, game->name,
         game->id);
}

void handle_invite_player(void *socket, char *identity, Message *msg) {
  Player *inviter = find_player(msg->sender);
  if (inviter == NULL || !inviter->in_game) {
    Message response = {0};
    response.type = MSG_ERROR;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "You are not in a game", MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);
    return;
  }

  Game *game = find_game_by_id(inviter->game_id);
  if (game == NULL) {
    Message response = {0};
    response.type = MSG_ERROR;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "Game not found", MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);
    return;
  }

  Player *invitee = find_player(msg->recipient);
  if (invitee == NULL) {
    Message response = {0};
    response.type = MSG_ERROR;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "Player not found", MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);
    return;
  }

  if (invitee->in_game) {
    Message response = {0};
    response.type = MSG_ERROR;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "Player is already in a game", MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);
    return;
  }

  // Отправка приглашения
  Message response = {0};
  response.type = MSG_INVITE_PLAYER;
  strncpy(response.sender, msg->sender, MAX_PLAYER_NAME - 1);
  strncpy(response.recipient, msg->recipient, MAX_PLAYER_NAME - 1);
  strncpy(response.game_name, game->name, MAX_GAME_NAME - 1);
  snprintf(response.data, MAX_MESSAGE_SIZE,
           "You are invited to game '%s' by %s", game->name, msg->sender);
  server_send(socket, identity, &response);

  response.type = MSG_ACK;
  strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
  strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
  strncpy(response.data, "Invitation sent", MAX_MESSAGE_SIZE - 1);
  server_send(socket, identity, &response);

  printf("Player %s invited %s to game '%s'\n", msg->sender, msg->recipient,
         game->name);
}

void handle_place_ship(void *socket, char *identity, Message *msg) {
  Player *player = find_player(msg->sender);
  if (player == NULL || !player->in_game) {
    return;
  }

  Game *game = find_game_by_id(player->game_id);
  if (game == NULL || game->status != GAME_PLACING_SHIPS) {
    Message response = {0};
    response.type = MSG_ERROR;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "Cannot place ship now", MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);
    return;
  }

  // Определяем индекс игрока
  int player_idx = -1;
  for (int i = 0; i < game->player_count; i++) {
    if (strcmp(game->players[i], msg->sender) == 0) {
      player_idx = i;
      break;
    }
  }

  if (player_idx == -1) {
    return;
  }

  // Парсим данные о корабле из msg->data (формат: "x,y,size,horizontal")
  int x = -1, y = -1;
  int size = 0;
  int horizontal = 1;

  if (sscanf(msg->data, "%d,%d,%d,%d", &x, &y, &size, &horizontal) < 3) {
    // Используем значения из структуры
    size = 1; // По умолчаниюs
  }

  if (place_ship(game->boards[player_idx], x, y, size, horizontal)) {
    game->ships_remaining[player_idx]++;
    printf("Player %s has placed %d ships\n", player->login,
           game->ships_remaining[player_idx]);

    if (game->ships_remaining[player_idx] == MAX_SHIPS) {
      player->ready = true;
      printf("Player %s is ready\n", player->login);
    }

    Message response = {0};
    response.type = MSG_ACK;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "Ship placed successfully", MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);

  } else {
    Message response = {0};
    response.type = MSG_ERROR;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "Invalid ship placement", MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);
  }
}

void handle_game_state(void *socket, char *identity, Message *msg) {
  printf("handling game state req\n");

  Player *player = find_player(msg->sender);
  if (player == NULL || !player->in_game) {
    return;
  }

  Game *game = find_game_by_id(player->game_id);
  if (game == NULL) {
    return;
  }
  // Проверяем, готовы ли оба игрока
  bool both_ready = true;
  for (int i = 0; i < game->player_count; i++) {
    Player *p = find_player(game->players[i]);
    if (p != NULL && !p->ready) {
      both_ready = false;
      break;
    }
  }

  Message response = {0};

  if (both_ready && game->player_count == MAX_PLAYERS) {
    game->status = GAME_PLAYING;
    game->current_turn = 0;

    // for (int i = 0; i < MAX_PLAYERS; i++) {
    //   Player *p = find_player(game->players[i]);
    //   if (p != NULL) {
    //     response.type = MSG_GAME_STATE;
    //     strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    //     strncpy(response.recipient, game->players[i], MAX_PLAYER_NAME - 1);
    //     if (i == game->current_turn) {
    //       strncpy(response.data, "Your turn!", MAX_MESSAGE_SIZE - 1);
    //       printf("%s's turn\n", game->players[i]);
    //     } else {
    //       strncpy(response.data, "Opponent's turn", MAX_MESSAGE_SIZE - 1);
    //     }
    //     send_message(p->socket, &response);
    //   }
    // }

    response.type = MSG_GAME_STATE;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, player->login, MAX_PLAYER_NAME - 1);
    if (strcmp(player->login, game->players[game->current_turn]) == 0) {
      strncpy(response.data, "Your turn!", MAX_MESSAGE_SIZE - 1);
      printf("%s's turn\n", game->players[game->current_turn]);
    } else {
      strncpy(response.data, "Opponent's turn", MAX_MESSAGE_SIZE - 1);
    }
    server_send(socket, identity, &response);
  }

  else {
    response.type = MSG_GAME_STATE;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, player->login, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "Opponent is getting ready...",
            MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);
  }
}

void handle_make_shot(void *socket, char *identity, Message *msg) {
  Player *player = find_player(msg->sender);
  if (player == NULL || !player->in_game) {
    return;
  }

  Game *game = find_game_by_id(player->game_id);
  if (game == NULL || game->status != GAME_PLAYING) {
    Message response = {0};
    response.type = MSG_ERROR;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "Cannot make shot now", MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);
    return;
  }

  int player_idx = -1;
  for (int i = 0; i < game->player_count; i++) {
    if (strcmp(game->players[i], msg->sender) == 0) {
      player_idx = i;
      break;
    }
  }

  if (player_idx == -1 || player_idx != game->current_turn) {
    Message response = {0};
    response.type = MSG_ERROR;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "Not your turn", MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);
    return;
  }

  int opponent_idx = 1 - player_idx;
  int x = msg->x;
  int y = msg->y;

  if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) {
    Message response = {0};
    response.type = MSG_ERROR;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "Invalid coordinates", MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);
    return;
  }

  if (game->shots[player_idx][x][y] != 0) {
    Message response = {0};
    response.type = MSG_ERROR;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
    strncpy(response.data, "Already shot here", MAX_MESSAGE_SIZE - 1);
    server_send(socket, identity, &response);
    return;
  }

  ShotResult result =
      make_shot(game->boards[opponent_idx], game->shots[player_idx], x, y);

  Message response = {0};
  response.type = MSG_SHOT_RESULT;
  response.x = x;
  response.y = y;
  response.shot_result = result;
  strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
  strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);

  if (result == SHOT_MISS) {
    strncpy(response.data, "Miss!", MAX_MESSAGE_SIZE - 1);
    game->current_turn = opponent_idx;
  } else if (result == SHOT_HIT) {
    strncpy(response.data, "Hit!", MAX_MESSAGE_SIZE - 1);
  } else if (result == SHOT_SUNK) {
    strncpy(response.data, "Ship sunk!", MAX_MESSAGE_SIZE - 1);
    game->ships_remaining[opponent_idx]--;
  }

  server_send(socket, identity, &response);

  Player *opponent = find_player(game->players[opponent_idx]);
  if (opponent != NULL) {
    Message response = {0};
    response.type = MSG_SHOT_RESULT;
    response.x = x;
    response.y = y;
    response.shot_result = result;
    strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
    strncpy(response.recipient, game->players[opponent_idx],
            MAX_PLAYER_NAME - 1);

    if (result == SHOT_MISS) {
      strncpy(response.data, "Opponent missed", MAX_MESSAGE_SIZE - 1);
    } else {
      snprintf(response.data, MAX_MESSAGE_SIZE, "Opponent hit at (%d,%d)", x,
               y);
    }

    server_send(socket, opponent->identity, &response);
  }

  if (check_game_over(game->boards[opponent_idx])) {
    game->status = GAME_FINISHED;

    for (int i = 0; i < game->player_count; i++) {
      Player *p = find_player(game->players[i]);
      if (p != NULL) {
        Message game_over = {0};
        game_over.type = MSG_GAME_OVER;
        strncpy(game_over.sender, "SERVER", MAX_PLAYER_NAME - 1);
        strncpy(game_over.recipient, game->players[i], MAX_PLAYER_NAME - 1);

        if (i == player_idx) {
          strncpy(game_over.data, "You won!", MAX_MESSAGE_SIZE - 1);
        } else {
          strncpy(game_over.data, "You lost!", MAX_MESSAGE_SIZE - 1);
        }

        server_send(socket, p->identity, &response);

        p->in_game = false;
        p->game_id = -1;
        p->ready = false;
      }
    }

    printf("Game '%s' finished. Winner: %s\n", game->name,
           game->players[player_idx]);
  }
}

// Обработка списка игр
void handle_list_games(void *socket, char *identity, Message *msg) {
  Player *player = find_player(msg->sender);
  if (player == NULL) {
    return;
  }

  char list[MAX_MESSAGE_SIZE] = "Available games:\n";
  int count = 0;

  for (int i = 0; i < game_count; i++) {
    if (games[i].status == GAME_WAITING ||
        games[i].status == GAME_PLACING_SHIPS) {
      char game_info[200];
      snprintf(game_info, sizeof(game_info), "%d. %s (%d/%d players)\n",
               games[i].id, games[i].name, games[i].player_count, MAX_PLAYERS);
      if (strlen(list) + strlen(game_info) < MAX_MESSAGE_SIZE) {
        strcat(list, game_info);
        count++;
      }
    }
  }

  if (count == 0) {
    strncpy(list, "No available games", MAX_MESSAGE_SIZE - 1);
  }

  Message response = {0};
  response.type = MSG_LIST_GAMES;
  strncpy(response.sender, "SERVER", MAX_PLAYER_NAME - 1);
  strncpy(response.recipient, msg->sender, MAX_PLAYER_NAME - 1);
  strncpy(response.data, list, MAX_MESSAGE_SIZE - 1);
  server_send(socket, identity, &response);
}

int main() {
  void *context = zmq_ctx_new();
  void *socket = zmq_socket(context, ZMQ_ROUTER);
  // void *socket = zmq_socket(context, ZMQ_ROUTER);

  char address[100];
  snprintf(address, sizeof(address), "tcp://*:%s", SERVER_PORT);

  if (zmq_bind(socket, address) != 0) {
    fprintf(stderr, "Error binding socket: %s\n", zmq_strerror(errno));
    return 1;
  }

  printf("Sea Battle server started on port %s\n", SERVER_PORT);
  printf("Waiting for clients...\n");

  while (1) {
    char identity[256] = {0};
    Message msg = {0};

    if (!server_receive(socket, identity, &msg))
      continue;

    Player *p = find_player(msg.sender);

    // если уже зарегистрирован — обновим identity (reconnect)
    if (p) {
      strcpy(p->identity, identity);
    }

    switch (msg.type) {
    case MSG_REGISTER:
      handle_register(socket, identity, &msg);
      break;
    case MSG_CREATE_GAME:
      handle_create_game(socket, identity, &msg);
      break;
    case MSG_JOIN_GAME:
      handle_join_game(socket, identity, &msg);
      break;
    case MSG_INVITE_PLAYER:
      handle_invite_player(socket, identity, &msg);
      break;
    case MSG_PLACE_SHIP:
      handle_place_ship(socket, identity, &msg);
      break;
    case MSG_GAME_STATE:
      handle_game_state(socket, identity, &msg);
      break;
    case MSG_MAKE_SHOT:
      handle_make_shot(socket, identity, &msg);
      break;
    case MSG_LIST_GAMES:
      handle_list_games(socket, identity, &msg);
      break;
    default:
      printf("Unknown message type: %d\n", msg.type);
      break;
    }
  }

  zmq_close(socket);
  zmq_ctx_destroy(context);
  return 0;
}
