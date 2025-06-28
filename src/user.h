#pragma once
#include<string>
#include"common.h"
//用户这边也有run进行轮询
    //若是用户发来信息，接收，然后通过user_server_pipe发送到主线程，主线程再把这条信息通过用户管道发送到所有线程，然后各自线程调用sengmsg发送给负责的客户端
class user{
public:
    user(int);
    ~user();
    request_msg  recvmsg(int&);//这个要变成int,如果用户关闭连接返回0，然后退出死循环，线程执行完毕,正常返回1，但是有一种特殊情况返回2，run里面continue就ok
    void sendmsg(int,const char* msg);
    void setnonblock(int);
    void addfd(int);
    void run();
    int user_server_pipe[2];//在主线程中构建通道，主线程用0，子线程用1
    int user_id;
private:
    //用来和主线程通信，当用户发来信息，就把
    std::string username;
    int m_sockfd;
    int epoll_fd;
    bool stop;
};