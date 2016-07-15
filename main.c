#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define MAXEVENTS 64

static int make_socket_non_blocking(int sfd)
{
  int flags, s;
  flags = fcntl(sfd, F_GETFL, 0);
  if (-1 == flags) {
    perror("F_GETFL");
    return -1;
  }

  flags |= O_NONBLOCK;
  s = fcntl(sfd, F_SETFL, flags);
  if (-1 == s) {
    perror("F_SETFL");
    return -1;
  }

  return 0;
}

int main(int argc, char *argv[])
{
  int sfd = socket(AF_INET, SOCK_STREAM, 0);
  if (-1 == sfd) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in s_addr;
  memset(&s_addr, 0, sizeof(struct sockaddr_in));
  s_addr.sin_family = AF_INET;
  s_addr.sin_port = htons(8000);
  s_addr.sin_addr.s_addr = inet_addr("0.0.0.0");
  if (-1 == bind(sfd, (struct sockaddr *)&s_addr, sizeof(struct sockaddr))) {
    perror("bind");
    exit(EXIT_FAILURE);
  }
 
  if (-1 == make_socket_non_blocking(sfd)) {
    perror("non blocking");
    exit(EXIT_FAILURE);
  }

  if (-1 == listen(sfd, SOMAXCONN)) {
    perror("listen");
    exit(EXIT_FAILURE);
  }
  printf("Start listening.\n");
 
  int efd = epoll_create(MAXEVENTS);
  if (-1 == efd) {
    perror("epoll_create");
    exit(EXIT_FAILURE);
  }

  struct epoll_event event;
  event.data.fd = sfd;
  event.events = EPOLLIN;
  if (-1 == epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event)) {
    perror("epoll_ctl: add");
    exit(EXIT_FAILURE);
  }
  
  struct epoll_event events[MAXEVENTS];
  for (;;) {
    int nfds = epoll_wait(efd, events, MAXEVENTS, -1);
    if (-1 == nfds) {
      perror("epoll_wait");
      exit(EXIT_FAILURE);
    }

    for (int i=0; i<nfds; i++) {      
      if ((events[i].events & EPOLLERR) ||
          (events[i].events & EPOLLHUP) ||
         !(events[i].events & EPOLLIN)) {
        fprintf(stderr, "epoll error:%d.\n", events[i].data.fd);
        close(events[i].data.fd);
        continue;
      }

      if (sfd == events[i].data.fd) {
        struct sockaddr_in in_addr;
	socklen_t in_len = sizeof(struct sockaddr_in);
        int infd = accept(sfd, (struct sockaddr *)&in_addr, &in_len);
        if (-1 == infd ) {
          perror("accept");
          exit(EXIT_FAILURE);
        }
        
        if (-1 == make_socket_non_blocking(infd)) {
          perror("non blocking");
          exit(EXIT_FAILURE);
        }

        struct epoll_event event;
        event.data.fd = infd;
        event.events = EPOLLIN | EPOLLET; 
        if (-1 == epoll_ctl(efd, EPOLL_CTL_ADD, infd, &event)) {
          perror("epoll_ctl: add");
          exit(EXIT_FAILURE);
        }
        printf("client conncect:%d\n", infd);
        continue;
      }

      if (events[i].events & EPOLLIN) {
        char buf[BUFSIZ] = {0};
        ssize_t n = 0, nread = 0;
        while((nread = read(events[i].data.fd, buf+n, BUFSIZ-n-1)) > 0)
          n += nread;

        if (-1 == nread && errno != EAGAIN)
          perror("read");

        printf("Message [%s] come from %d\n", buf, events[i].data.fd);
      }
    }
  }

  printf("End\n");
  close(sfd);
  return 0;
}
