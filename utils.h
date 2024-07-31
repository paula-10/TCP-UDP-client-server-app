#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>

#define BUFFER_SIZE 1552
#define MAX_PFDS 32

#define DIE(assertion, call_description)                 \
  do {                                                   \
    if (assertion) {                                     \
      fprintf(stderr, "(%s, %d): ", __FILE__, __LINE__); \
      perror(call_description);                          \
      exit(EXIT_FAILURE);                                \
    }                                                    \
  } while (0)

enum class MessageType : uint8_t {
  ID,
  SUBSCRIBE,
  UNSUBSCRIBE,
  TOPIC_MESSAGE,
  ALREADY_CONNECTED,
  SUBSCRIBER_EXIT,
  SERVER_EXIT,
};

struct Message {
  MessageType type;
  char content[BUFFER_SIZE];
};

/// @brief Sends a message of the size of a `message` structure to a socket.
/// @param fd The file descriptor of the socket where the message is sent.
/// @param msg Pointer to the `message` structure that contains the message to be sent.
/// @return The total number of bytes sent, or -1 in case of error.
int send_message(int fd, const Message *msg) {
  const char *buffer = (const char *)msg;

  int total_bytes_sent = 0;
  do {
    int bytes_sent = send(fd, msg + total_bytes_sent, sizeof(Message), 0);
    if (bytes_sent <= 0) return -1;

    total_bytes_sent += bytes_sent;
  } while (total_bytes_sent != sizeof(Message));

  return total_bytes_sent;
}

/// @brief Receives a message of the size of a `message` structure from a socket.
/// @param fd The file descriptor of the socket from where the message is received.
/// @param msg Pointer to the `message` structure that will store the received message.
/// @return The total number of bytes received, or -1 in case of error.
int receive_message(int fd, Message *msg) {
  char *buffer = (char *)msg;

  int total_bytes_received = 0;
  do {
    int bytes_received = recv(fd, buffer + total_bytes_received, sizeof(Message), 0);
    if (bytes_received <= 0) return -1;

    total_bytes_received += bytes_received;
  } while (total_bytes_received != sizeof(Message));

  return total_bytes_received;
}