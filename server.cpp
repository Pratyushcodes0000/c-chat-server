#include <arpa/inet.h>
#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <errno.h>

constexpr uint16_t PORT = 8080;
constexpr uint16_t MAX_EVENT = 1000;

struct Client {
  int fd;
  std::string buffer;
};

std::vector<Client> clients;

void set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    perror("fcntl(F_GETFL)");
    return;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("fcntl(F_SETFL)");
  }
}

void remove_client(int dead_fd) {
  for (auto it = clients.begin(); it != clients.end();) {
    if (it->fd == dead_fd) {
      std::cout << "[INFO] Removing client fd=" << dead_fd << std::endl;
      close(it->fd);
      clients.erase(it);
      break;
    } else {
      ++it;
    }
  }
}

void broadcast(const std::string& msg, int fd) {
  for (auto &client : clients) {
    if (client.fd != fd) {
      int s = send(client.fd, msg.c_str(), msg.size(), 0);
      if (s == -1) {
        perror("send");
      }
    }
  }
}

int main() {

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    perror("socket");
    return 1;
  }

  set_nonblocking(server_fd);

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  if (bind(server_fd, (sockaddr *)&address, sizeof address) == -1) {
    perror("bind");
    return 1;
  }

  if (listen(server_fd, SOMAXCONN) == -1) {
    perror("listen");
    return 1;
  }

  std::cout << "[INFO] Server started on port 8080\n";

  int epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    perror("epoll_create1");
    return 1;
  }

  epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = server_fd;

  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
    perror("epoll_ctl ADD server_fd");
    return 1;
  }

  epoll_event events[MAX_EVENT];

  while (true) {
    int n = epoll_wait(epoll_fd, events, MAX_EVENT, -1);

    if (n == -1) {
      perror("epoll_wait");
      continue;
    }

    std::cout << "[DEBUG] epoll_wait returned " << n << " events\n";

    for (int i = 0; i < n; i++) {
      int fd = events[i].data.fd;
      std::cout << "[DEBUG] Event on fd=" << fd << std::endl;

      if (fd == server_fd) {
        int client_fd = accept(fd, nullptr, nullptr);

        if (client_fd == -1) {
          perror("accept");
          continue;
        }

        set_nonblocking(client_fd);

        epoll_event client_ev{};
        client_ev.events = EPOLLIN;
        client_ev.data.fd = client_fd;

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) == -1) {
          perror("epoll_ctl ADD client_fd");
          close(client_fd);
          continue;
        }

        clients.push_back({client_fd, ""});
        std::cout << "[INFO] New client connected fd=" << client_fd << std::endl;

      } else {
        char buffer[1024];
        int bytes = recv(fd, buffer, sizeof buffer, 0);

        if (bytes > 0) {
          std::cout << "[DEBUG] recv " << bytes << " bytes from fd=" << fd << std::endl;

          for (auto &client : clients) {
            if (client.fd == fd) {
              client.buffer.append(buffer, bytes);

              size_t pos;
              while ((pos = client.buffer.find('\n')) != std::string::npos) {
                std::string msg = client.buffer.substr(0, pos + 1);
                client.buffer.erase(0, pos + 1);

                std::cout << "[MSG] " << msg;
                broadcast(msg, fd);
              }
              break;
            }
          }

        } else if (bytes == 0) {
          std::cout << "[INFO] Client disconnected fd=" << fd << std::endl;
          epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
          remove_client(fd);

        } else {
          if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("recv");
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
            remove_client(fd);
          }
        }
      }
    }
  }

  close(server_fd);
  return 0;
}
