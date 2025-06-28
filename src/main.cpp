#include<unordered_map>
#include <sys/socket.h>
#include <netinet/in.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include <arpa/inet.h>
#include<string>
#include <csignal>
#include<sys/epoll.h>
#include<fcntl.h>
#include"server.h"
#include"easylogging.h"
class user;

std::unordered_map<int,user*> users;//sockfd,username
int sig_pipefd[2];//信号管道

INITIALIZE_EASYLOGGINGPP
int main(){
    server s;
    s.run() ;
}
