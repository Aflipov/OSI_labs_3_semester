// sea_battle_server.c
#include "common.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zmq.h>

#define MAX_PLAYERS 2
#define MAX_GAMES 100
#define BOARD_SIZE 10
#define MAX_SHIPS 10
#define MAX_PLAYER_NAME 50
#define MAX_GAME_NAME 50
#define MAX_MESSAGE_SIZE 1024
#define SERVER_PORT "5555"

// ===================== GLOBALS =====================
Player players[100];
int player_count = 0;

Game games[MAX_GAMES];
int game_count = 0;

// ===================== PLAYER UTILS =====================
Player *find_player(const char *login) {
  for (int i = 0; i < player_count; i++)
    if (strcmp(players[i].login, login) == 0)
      return &players[i];
  return NULL;
}

Game *find_game_by_id(int id) {
  for (int i = 0; i < game_count; i++)
    if (games[i].id == id)
      return &games[i];
  return NULL;
}

// ===================== SERVER LOGIC =====================
void send_ack(void *socket, const char *recipient, const char *text,
              int game_id) {
  Message msg = {0};
  msg.type = MSG_ACK;
  if (recipient)
    strncpy(msg.recipient, recipient, MAX_PLAYER_NAME - 1);
  strncpy(msg.data, text, MAX_MESSAGE_SIZE - 1);
  msg.game_id = game_id;
  zmq_send(socket, &msg, sizeof(msg), 0);
}

void send_error(void *socket, const char *recipient, const char *text) {
  Message msg = {0};
  msg.type = MSG_ERROR;
  if (recipient)
    strncpy(msg.recipient, recipient, MAX_PLAYER_NAME - 1);
  strncpy(msg.data, text, MAX_MESSAGE_SIZE - 1);
  zmq_send(socket, &msg, sizeof(msg), 0);
}

void handle_register(void *socket, Message *msg) {
  if (find_player(msg->sender) != NULL) {
    send_error(socket, msg->sender, "Player already registered");
    return;
  }
  Player p = {0};
  strncpy(p.login, msg->sender, MAX_PLAYER_NAME - 1);
  p.in_game = false;
  players[player_count++] = p;
  send_ack(socket, msg->sender, "Registered successfully", -1);
}

void handle_create_game(void *socket, Message *msg) {
  Game g = {0};
  g.id = game_count + 1;
  strncpy(g.name, msg->game_name, MAX_GAME_NAME - 1);
  strncpy(g.players[0], msg->sender, MAX_PLAYER_NAME - 1);
  g.player_count = 1;
  g.status = GAME_WAITING;
  g.current_turn = 0;
  init_board(g.boards[0]);
  games[game_count++] = g;
  Player *p = find_player(msg->sender);
  if (p) {
    p->in_game = true;
    p->game_id = g.id;
  }
  send_ack(socket, msg->sender, "Game created", g.id);
}

void handle_join_game(void *socket, Message *msg) {
  for (int i = 0; i < game_count; i++) {
    Game *g = &games[i];
    if (strcmp(g->name, msg->game_name) == 0 && g->player_count < MAX_PLAYERS) {
      strncpy(g->players[g->player_count], msg->sender, MAX_PLAYER_NAME - 1);
      init_board(g->boards[g->player_count]);
      g->player_count++;
      g->status =
          (g->player_count == MAX_PLAYERS) ? GAME_PLACING_SHIPS : GAME_WAITING;
      Player *p = find_player(msg->sender);
      if (p) {
        p->in_game = true;
        p->game_id = g->id;
      }
      send_ack(socket, msg->sender, "Joined game", g->id);
      // Notify first player
      send_ack(socket, g->players[0], "Player joined", g->id);
      return;
    }
  }
  send_error(socket, msg->sender, "Game not found or full");
}

void handle_place_ship(void *socket, Message *msg) {
  Player *p = find_player(msg->sender);
  if (!p) {
    send_error(socket, msg->sender, "Player not found");
    return;
  }
  Game *g = find_game_by_id(p->game_id);
  if (!g) {
    send_error(socket, msg->sender, "Game not found");
    return;
  }
  int x, y, size, h;
  if (sscanf(msg->data, "%d,%d,%d,%d", &x, &y, &size, &h) != 4) {
    send_error(socket, msg->sender, "Invalid data");
    return;
  }
  if (!place_ship(g->boards[p == find_player(g->players[0]) ? 0 : 1], x, y,
                  size, h)) {
    send_error(socket, msg->sender, "Invalid placement");
    return;
  }
  send_ack(socket, msg->sender, "Ship placed", g->id);
}

void handle_make_shot(void *socket, Message *msg) {
  Player *p = find_player(msg->sender);
  if (!p) {
    send_error(socket, msg->sender, "Player not found");
    return;
  }
  Game *g = find_game_by_id(p->game_id);
  if (!g) {
    send_error(socket, msg->sender, "Game not found");
    return;
  }
  int player_idx = (strcmp(p->login, g->players[0]) == 0) ? 0 : 1;
  int opp_idx = 1 - player_idx;
  ShotResult res = make_shot(g->boards[opp_idx], msg->x, msg->y);
  Message rmsg = {0};
  rmsg.type = MSG_SHOT_RESULT;
  rmsg.x = msg->x;
  rmsg.y = msg->y;
  rmsg.shot_result = res;
  strncpy(rmsg.recipient, g->players[opp_idx], MAX_PLAYER_NAME - 1);
  strncpy(rmsg.data, (res == SHOT_HIT) ? "HIT" : "MISS", MAX_MESSAGE_SIZE - 1);
  zmq_send(socket, &rmsg, sizeof(rmsg), 0);
  send_ack(socket, msg->sender, (res == SHOT_HIT) ? "HIT" : "MISS", g->id);
  if (check_game_over(g->boards[opp_idx])) {
    Message over = {0};
    over.type = MSG_GAME_OVER;
    strncpy(over.data, "You lost!", MAX_MESSAGE_SIZE - 1);
    strncpy(over.recipient, g->players[opp_idx], MAX_PLAYER_NAME - 1);
    zmq_send(socket, &over, sizeof(over), 0);
    strncpy(over.data, "You won!", MAX_MESSAGE_SIZE - 1);
    strncpy(over.recipient, g->players[player_idx], MAX_PLAYER_NAME - 1);
    zmq_send(socket, &over, sizeof(over), 0);
    g->status = GAME_FINISHED;
  }
}

// ===================== MAIN =====================
int main() {
  void *context = zmq_ctx_new();
  void *socket = zmq_socket(context, ZMQ_ROUTER);
  char addr[64];
  snprintf(addr, sizeof(addr), "tcp://*:%s", SERVER_PORT);
  if (zmq_bind(socket, addr) != 0) {
    perror("bind");
    return 1;
  }
  printf("Server started on %s\n", addr);

  while (1) {
    zmq_msg_t identity, message;
    zmq_msg_init(&identity);
    zmq_msg_init(&message);
    zmq_msg_recv(&identity, socket, 0);
    zmq_msg_recv(&message, socket, 0);

    char *id = (char *)zmq_msg_data(&identity);
    Message *msg = (Message *)zmq_msg_data(&message);

    switch (msg->type) {
    case MSG_REGISTER:
      handle_register(socket, msg);
      break;
    case MSG_CREATE_GAME:
      handle_create_game(socket, msg);
      break;
    case MSG_JOIN_GAME:
      handle_join_game(socket, msg);
      break;
    case MSG_PLACE_SHIP:
      handle_place_ship(socket, msg);
      break;
    case MSG_MAKE_SHOT:
      handle_make_shot(socket, msg);
      break;
    default:
      send_error(socket, msg->sender, "Unknown message");
      break;
    }

    zmq_msg_close(&identity);
    zmq_msg_close(&message);
  }

  zmq_close(socket);
  zmq_ctx_destroy(context);
  return 0;
}
