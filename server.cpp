#include <algorithm>
#include <list>
#include <sstream>
#include <string>

#include "utils.h"

struct Client {
  bool online;
  std::string id;
  std::string ip;
  int port;
  int socket;
  bool message_sent;

 public:
  Client() {
    online = false;
    message_sent = false;
  }
};

struct Topic {
  std::string name;
  std::list<Client *> subscribers;
};

/// @brief Retrieve the port number from a socket file descriptor.
/// @param fd The socket file descriptor.
/// @return The port number that the peer socket is connected to.
int getPortFromFd(int fd) {
  struct sockaddr_in addr;
  socklen_t addr_size = sizeof(struct sockaddr_in);
  int res = getpeername(fd, (struct sockaddr *)&addr, &addr_size);
  return ntohs(addr.sin_port);
}

/// @brief Splits a string into a vector of substrings based on a delimiter.
/// @param str The string to split.
/// @param delim The delimiter character.
/// @return A vector of substrings.
std::vector<std::string> split(const std::string &str, char delim) {
  std::vector<std::string> tokens;
  std::stringstream ss(str);
  std::string token;
  while (std::getline(ss, token, delim)) {
    tokens.push_back(token);
  }
  return tokens;
}

/// @brief Checks whether a given topic matches a wildcard topic.
/// @param topic A string representing the topic to be checked.
/// @param wildcard A string representing the wildcard topic against which the comparison is made.
/// @return A boolean indicating whether the topic matches the wildcard. Returns 'true' if it matches, and 'false'
/// otherwise.
bool matchesWildcard(const std::string &topic, const std::string &wildcard) {
  std::vector<std::string> topic_parts = split(topic, '/');
  std::vector<std::string> wildcard_parts = split(wildcard, '/');

  auto it_topic = topic_parts.begin();
  auto it_wildcard = wildcard_parts.begin();

  while (it_topic != topic_parts.end() && it_wildcard != wildcard_parts.end()) {
    if (*it_wildcard == "+") {
      ++it_topic;
      ++it_wildcard;
    } else if (*it_wildcard == "*") {
      if (++it_wildcard == wildcard_parts.end()) {
        return true;
      }
      while (it_topic != topic_parts.end() && *it_topic != *it_wildcard) {
        ++it_topic;
      }
    } else if (*it_topic != *it_wildcard) {
      return false;
    } else {
      ++it_topic;
      ++it_wildcard;
    }
  }

  return it_topic == topic_parts.end() && it_wildcard == wildcard_parts.end();
}

int main(int argc, char **argv) {
  // Check number of parameters.
  DIE(argc != 2, "[SERVER] One argument must be provided (<PORT_DORIT>)!");

  // Disable buffering.
  setvbuf(stdout, NULL, _IONBF, BUFSIZ);

  // Create TCP socket.
  int socket_desc_tcp = socket(AF_INET, SOCK_STREAM, 0);
  DIE(socket_desc_tcp == -1, "[SERVER] Unable to create TCP socket!");

  // Set port and IP that we'll be listening for.
  struct sockaddr_in tcp_server_addr;
  tcp_server_addr.sin_family = AF_INET;
  tcp_server_addr.sin_port = htons(atoi(argv[1]));
  tcp_server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  // Bind to the set port and IP.
  DIE(bind(socket_desc_tcp, (struct sockaddr *)&tcp_server_addr, sizeof(tcp_server_addr)),
      "[SERVER] Couldn't bind to the port!");

  // Set listening on TCP socket.
  DIE(listen(socket_desc_tcp, SOMAXCONN), "[SERVER] Unable to listen on TCP socket!");

  // Create UDP socket.
  int socket_desc_udp = socket(AF_INET, SOCK_DGRAM, 0);
  DIE(socket_desc_udp == -1, "[SERVER] Unable to create UDP socket!");

  // Set port and IP that we'll be listening for UDP datagrams.
  struct sockaddr_in udp_server_addr;
  udp_server_addr.sin_family = AF_INET;
  udp_server_addr.sin_port = htons(atoi(argv[1]));
  udp_server_addr.sin_addr.s_addr = INADDR_ANY;

  // Bind to the set port and IP for UDP datagrams.
  DIE(bind(socket_desc_udp, (struct sockaddr *)&udp_server_addr, sizeof(udp_server_addr)),
      "[SERVER] Couldn't bind to the UDP port!");

  // Set polling for multiplexing clients input and stdin input.
  int nfds = 0;
  struct pollfd pfds[MAX_PFDS];
  pfds[nfds].fd = STDIN_FILENO;
  pfds[nfds].events = POLLIN;
  nfds++;

  pfds[nfds].fd = socket_desc_tcp;
  pfds[nfds].events = POLLIN;
  nfds++;

  pfds[nfds].fd = socket_desc_udp;
  pfds[nfds].events = POLLIN;
  nfds++;

  // Initialise data for server loop.
  bool stop = false;
  std::string input;
  Message msg;
  std::list<Client> clients;
  std::list<Topic> topics;
  std::list<Topic> wildcard_topics;

  // Wait for stdin, UDP or TCP clients.
  while (!stop) {
    // Wait for readiness notification.
    poll(pfds, nfds, -1);

    // Read user data from standard input.
    if ((pfds[0].revents & POLLIN) != 0) {
      // Read whole line.
      getline(std::cin, input);

      // Exit command.
      if (input.find("exit") != std::string::npos) {
        // Close all TCP sockets.
        for (int i = 3; i < nfds; i++) {
          // Notify clients of server closure.
          msg.type = MessageType::SERVER_EXIT;
          DIE(send_message(pfds[i].fd, &msg) == -1, "[SERVER] Error sending close notification to client!");

          close(pfds[i].fd);
          pfds[i].fd = -1;
          pfds[i].events = 0;
        }

        nfds = 0;
        stop = true;
        continue;
      }
      continue;
    }

    // Handle new TCP connection.
    if ((pfds[1].revents & POLLIN) != 0) {
      struct sockaddr_in client_addr;
      socklen_t client_len = sizeof(client_addr);
      int new_socket = accept(socket_desc_tcp, (struct sockaddr *)&client_addr, &client_len);
      DIE(new_socket == -1, "[SERVER] Unable to accept connection TCP socket!");

      // Disable Nagle algorithm.
      char flag = 1;
      DIE(setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, (char *)&(flag), sizeof(int)),
          "[SERVER] Unable to disable Nagle algorithm!");

      // Add the new socket to the pollfds array.
      pfds[nfds].fd = new_socket;
      pfds[nfds].events = POLLIN;
      nfds++;

      // Store client's IP and port.
      Client new_client;
      new_client.ip = inet_ntoa(client_addr.sin_addr);
      new_client.port = ntohs(client_addr.sin_port);
      new_client.socket = new_socket;
      clients.push_back(new_client);
      continue;
    }

    // Read UDP client data.
    if ((pfds[2].revents & POLLIN) != 0) {
      struct sockaddr_in client_addr;
      socklen_t client_len = sizeof(client_addr);
      int bytes_received =
          recvfrom(socket_desc_udp, msg.content, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &client_len);
      DIE(bytes_received <= 0, "[SERVER] Error receiving data from UDP client!");

      // Extract the topic.
      char topic_name_buffer[51];
      memcpy(topic_name_buffer, msg.content, 50);
      topic_name_buffer[50] = 0;

      msg.content[bytes_received] = 0;

      // Convert char array to string.
      std::string topic_name = std::string(topic_name_buffer);

      auto topic = std::find_if(topics.begin(), topics.end(),
                                [topic_name](const auto &topic) { return topic.name == topic_name; });

      msg.type = MessageType::TOPIC_MESSAGE;

      // Send messages to clients subscribed to the specific topic.
      if (topic != topics.end()) {
        for (auto &client : topic->subscribers) {
          if (client->online == true) {
            DIE(send_message(client->socket, &msg) == -1, "[SERVER] Error sending topic message to client");
            client->message_sent = true;
          }
        }
      }

      // Send messages to clients subscribed to topics with wildcard.
      for (auto &wildcard_topic : wildcard_topics) {
        if (matchesWildcard(topic_name, wildcard_topic.name)) {
          for (auto &client : wildcard_topic.subscribers) {
            // Only send if the message was not already sent.
            if (client->online && !client->message_sent) {
              DIE(send_message(client->socket, &msg) == -1, "[SERVER] Error sending topic message to client");
              client->message_sent = true;
            }
          }
        }
      }

      // Reset message_sent for all clients.
      for (auto &client : clients) {
        client.message_sent = false;
      }

      continue;
    }
    // Check all TCP clients for data.
    for (int i = 3; i < nfds; i++) {
      int client_socket = pfds[i].fd;
      int client_port = getPortFromFd(client_socket);

      auto client = std::find_if(clients.begin(), clients.end(), [client_socket, client_port](const auto &client) {
        return client.socket == client_socket && client.port == client_port;
      });

      if ((pfds[i].revents & POLLIN) != 0) {
        // Receive message from client.
        DIE(receive_message(client_socket, &msg) == -1, "[SERVER] Error receiving message from client!");

        // Check message type.
        if (msg.type == MessageType::ID) {
          std::string id(msg.content);

          auto already_client =
              std::find_if(clients.begin(), clients.end(), [id](const auto &client) { return client.id == id; });

          // If the client is new.
          if (already_client == clients.end()) {
            client->id = id;
            client->online = true;
            std::cout << "New client " << id << " connected from " << client->ip << ":" << client->port << ".\n";
          } else {
            // If the client was offline, make it online.
            if (already_client->online == false) {
              already_client->online = true;
              already_client->socket = client->socket;
              already_client->port = client->port;
              already_client->id = id;
              std::cout << "New client " << id << " connected from " << client->ip << ":" << client->port << ".\n";
              clients.erase(client);
            } else {
              // Otherwise, don't allow connection and send disconnect message.
              std::cout << "Client " << id << " already connected.\n";

              msg.type = MessageType::ALREADY_CONNECTED;
              DIE(send_message(client_socket, &msg) == -1,
                  "[SERVER] Error sending already connected message to client");

              clients.erase(client);
              close(pfds[i].fd);
              for (int j = i; j < nfds - 1; j++) {
                pfds[j] = pfds[j + 1];
              }
              nfds--;
              i--;
            }
          }
          continue;
        }

        if (msg.type == MessageType::SUBSCRIBER_EXIT) {
          // Set client's status to offline.
          client->online = false;

          close(pfds[i].fd);
          for (int j = i; j < nfds - 1; j++) {
            pfds[j] = pfds[j + 1];
          }
          nfds--;
          i--;

          std::cout << "Client " << client->id << " disconnected.\n";
          continue;
        }

        if (msg.type == MessageType::SUBSCRIBE) {
          std::string topic_name(msg.content);

          // Check if the topic is a wildcard topic.
          bool is_wildcard_topic =
              (topic_name.find("*") != std::string::npos || topic_name.find("+") != std::string::npos);

          // Find the topic.
          auto topic = topics.end();
          if (is_wildcard_topic)
            topic = std::find_if(wildcard_topics.begin(), wildcard_topics.end(),
                                 [topic_name](const auto &topic) { return topic.name == topic_name; });
          else
            topic = std::find_if(topics.begin(), topics.end(),
                                 [topic_name](const auto &topic) { return topic.name == topic_name; });

          // If the topic does not exist, create it.
          if (topic == topics.end() || topic == wildcard_topics.end()) {
            Topic new_topic;
            new_topic.name = topic_name;
            new_topic.subscribers.push_back(&*client);

            if (is_wildcard_topic)
              wildcard_topics.push_back(new_topic);
            else
              topics.push_back(new_topic);
          }
          // If the topic exists, add the client to it.
          else {
            topic->subscribers.push_back(&*client);
          }

          continue;
        }

        if (msg.type == MessageType::UNSUBSCRIBE) {
          std::string topic_name(msg.content);

          // Check if the topic is a wildcard topic.
          bool is_wildcard_topic =
              (topic_name.find("*") != std::string::npos || topic_name.find("+") != std::string::npos);

          // Find the topic.
          auto topic = topics.end();
          if (is_wildcard_topic)
            topic = std::find_if(wildcard_topics.begin(), wildcard_topics.end(),
                                 [topic_name](const auto &topic) { return topic.name == topic_name; });
          else
            topic = std::find_if(topics.begin(), topics.end(),
                                 [topic_name](const auto &topic) { return topic.name == topic_name; });
          if (topic != topics.end() && topic != wildcard_topics.end()) {
            topic->subscribers.remove_if([client_socket, client_port](const auto &client) {
              return client->socket == client_socket && client_port == client_port;
            });
          }
        }
      }
    }
  }

  // Close the TCP socket.
  close(socket_desc_tcp);

  // Close the UDP socket.
  close(socket_desc_udp);

  return 0;
}