#include <arpa/inet.h>
#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

constexpr uint16_t PORT = 8080;
constexpr uint16_t MAX_EVENT = 1000;

struct Client {
  int fd;
  std::string buffer;
};

std::vector<Client> clients;

void set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void remove_client(int dead_fd) {
  for (auto it = clients.begin(); it != clients.end();) {

    if (it->fd == dead_fd) {
      clients.erase(it);
      break;
    } else {
      ++it;
    }
  }
}

void broadcast(std::string msg , int fd){
  for(auto &client : clients){
    if(client.fd != fd){
      send(client.fd,msg.c_str(),msg.size(),0);
    }
  }
}

int main() {

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  set_nonblocking(server_fd);

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  bind(server_fd, (sockaddr *)&address, sizeof address);
  listen(server_fd, SOMAXCONN);

  std::cout << "Server started on port 8080" << std::endl;

  int epoll_fd = epoll_create1(0);
  epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = epoll_fd;

  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);

  epoll_event events[MAX_EVENT];

  while (true) {
    int n = epoll_wait(epoll_fd, events, MAX_EVENT, -1);

    for (int i = 0; i < n; i++) {
      int fd = events[i].data.fd;

      if (fd == server_fd) {
        int client_fd = accept(fd, nullptr, nullptr);
        set_nonblocking(client_fd);

        epoll_event client_ev{};
        ev.events = EPOLLIN;
        ev.data.fd = client_fd;

        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev);

        clients.push_back({client_fd,""});
        
      } else {
        char buffer[1024];
        int bytes = recv(fd, buffer, sizeof buffer, 0);

        
        if(bytes > 0){
          for(auto &client : clients){
            if(client.fd == fd){
              client.buffer.append(buffer,bytes);

              size_t pos;
              while((pos = client.buffer.find('\n')) != std::string::npos){
                std::string msg = client.buffer.substr(0,pos+1);
                client.buffer.erase(0,pos+1);
                std::cout << msg << std::endl;
                broadcast(msg,fd);
              }
              break;
            }else if (bytes == 0) {
              std::cout << "Client disconnected ... removing from queue "
                    << std::endl;
              remove_client(fd);
              return 1;
        }else {
          if(errno != EAGAIN && errno != EWOULDBLOCK){
            std::cout << "Client error:"<<fd<<std::endl;
            remove_client(fd);
          }
        }
        }
          
        }
      }
    }
  }
 close(server_fd);
  return 0;
}
