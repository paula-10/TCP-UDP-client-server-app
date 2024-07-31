#include <cmath>
#include <iomanip>

#include "utils.h"

/**
 * @brief Parses the data received in a buffer and returns it in a formatted string.
 * The data is expected to be in a specific format: the topic name, followed by a type identifier and the data itself.
 * The type identifier can be 0 (INT), 1 (SHORT_REAL), 2 (FLOAT), or 3 (STRING).
 * @param buffer A pointer to the buffer containing the data to be parsed.
 * @return A formatted string representing the parsed data.
 */
std::string parseData(const char *buffer) {
  char topic[51];
  memcpy(topic, buffer, 50);
  topic[50] = 0;

  uint32_t type_identifier = buffer[50];
  const char *data = buffer + 51;

  if (type_identifier > 3) return "";

  std::ostringstream oss;
  switch (type_identifier) {
    case 0: {
      int sign = data[0];
      long value = ntohl(*(uint32_t *)(data + 1));
      if (sign == 1) value *= -1;
      oss << topic << " - INT - " << value;
      break;
    }
    case 1: {
      float value = ntohs(*(uint16_t *)(data));
      float nr = value / 100;
      oss << topic << " - SHORT_REAL - " << std::fixed << std::setprecision(2) << nr;
      break;
    }
    case 2: {
      int sign = data[0];
      uint32_t value = ntohl(*(uint32_t *)(data + 1));
      int power = data[5];

      uint32_t integral = value / pow(10, power);
      uint32_t fractional = value % (uint32_t)pow(10, power);

      char buffer[50];
      sprintf(buffer, "%s%d.%0*d", sign ? "-" : "", integral, power, fractional);
      std::string floatStr(buffer);

      oss << topic << " - FLOAT - " << floatStr;
      break;
    }
    case 3: {
      oss << topic << " - STRING - " << data;
      break;
    }
    default:
      break;
  }

  return oss.str();
}

int main(int argc, char **argv) {
  // Check number of parameters.
  DIE(argc != 4, "[SUBSCRIBER] Three arguments must be provided (<ID_CLIENT> <IP_SERVER> <PORT_SERVER>)");

  // Disable buffering.
  setvbuf(stdout, NULL, _IONBF, BUFSIZ);

  // Create socket, we use SOCK_STREAM for TCP
  int socket_desc = socket(AF_INET, SOCK_STREAM, 0);
  DIE(socket_desc == -1, "[SUBSCRIBER] Unable to create socket!");

  // Set port and IP the same as server-side.
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(atoi(argv[3]));
  server_addr.sin_addr.s_addr = inet_addr(argv[2]);

  // Send connection request to server.
  DIE(connect(socket_desc, (struct sockaddr *)&server_addr, sizeof(server_addr)), "[SUBSCRIBER] Unable to connect!");

  // Set polling for multiplexing server input and stdin input.
  int nfds = 0;
  struct pollfd pfds[2];
  pfds[nfds].fd = STDIN_FILENO;
  pfds[nfds].events = POLLIN;
  nfds++;

  pfds[nfds].fd = socket_desc;
  pfds[nfds].events = POLLIN;
  nfds++;

  // Initialise data for subscriber loop.
  bool stop = false;
  std::string input;
  Message msg;

  // Send id to server.
  strcpy(msg.content, argv[1]);
  msg.type = MessageType::ID;
  DIE(send_message(socket_desc, &msg) == -1, "[SUBSCRIBER] Error sending id to server!");

  // Wait for stdin or server.
  while (!stop) {
    // Wait for readiness notification.
    poll(pfds, nfds, -1);

    // Read user data from standard input.
    if ((pfds[0].revents & POLLIN) != 0) {
      // Read whole line.
      getline(std::cin, input);

      // Unsubscribe command.
      if (input.find("unsubscribe") != std::string::npos) {
        msg.type = MessageType::UNSUBSCRIBE;

        // Extract topic name.
        std::string topic_name = input.substr(std::string("unsubscribe").length() + 1);
        strcpy(msg.content, topic_name.c_str());

        send_message(socket_desc, &msg);
        std::cout << "Unsubscribed from topic " << topic_name << '\n';
        continue;
      }

      // Subscribe command.
      if (input.find("subscribe") != std::string::npos) {
        msg.type = MessageType::SUBSCRIBE;

        // Extract topic name.
        std::string topic_name = input.substr(std::string("subscribe").length() + 1);
        strcpy(msg.content, topic_name.c_str());

        send_message(socket_desc, &msg);
        std::cout << "Subscribed to topic " << topic_name << '\n';
        continue;
      }

      // Exit command.
      if (input.find("exit") != std::string::npos) {
        // Send exit message to server.
        msg.type = MessageType::SUBSCRIBER_EXIT;
        send_message(socket_desc, &msg);
        stop = true;
        continue;
      }
      continue;
    }

    // Read server data.
    if ((pfds[1].revents & POLLIN) != 0) {
      DIE(receive_message(socket_desc, &msg) == -1, "[SUBSCRIBER] Error receiving message from server!");

      // Check message type.
      if (msg.type == MessageType::SERVER_EXIT || msg.type == MessageType::ALREADY_CONNECTED) {
        stop = true;
        continue;
      }

      if (msg.type == MessageType::TOPIC_MESSAGE) {
        std::string data = parseData(msg.content);
        std::cout << data << '\n';
        continue;
      }

      continue;
    }
  }

  // Close the socket.
  close(socket_desc);

  return 0;
}