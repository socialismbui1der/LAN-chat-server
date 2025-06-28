#pragma once
//server监听信号管道，listen_fd若用户发来连接请求，则将listen传递构造user，并创建与子进程的通信管道（添加监听1），然后用lambda包装user::run()放入线程池
class ThreadPool;
class server{
public:
    server();
    ~server();
    void run();
private:
    void initnet();
    void initsig();
    void setnonblock(int);
    void addfd(int);
private: 
    int user_id;//给user编号
    int epoll_fd;
    int listen_fd;
    bool stop;
    ThreadPool* threadpool;
};