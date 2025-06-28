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
#include<unordered_map>
#include"server.h"
#include"user.h"
#include"threadpool.h"
#include"easylogging.h"

#define ERROR_CHECK(ret,val,msg){ \
    if(ret==val){ \
        perror(msg); \
        exit(1); \
    }\
}\

extern int sig_pipefd[2];//信号管道
extern std::unordered_map<int,user*> users;

void signalhandler(int signum, siginfo_t *info, void* context) {
    LOG(DEBUG)<<"接收到信号"<<'\n';
    if(signum==SIGINT){
        int save_errno = errno;//errno 是一个全局变量，用于存储最近的错误代码，先保存 errno 的值，因为其他函数可能会修改它
        int msg = signum;
        send( sig_pipefd[1], ( char * )&msg, sizeof( msg ), 0 );//非阻塞发送
        errno = save_errno;//恢复原始的 errno 值，确保信号处理函数不会影响到程序中其他地方的错误代码。
    }
}

server::~server(){
    delete threadpool;
    close(epoll_fd);
    close(listen_fd);
    for(auto &iter:users){
        delete iter.second;
    }
}

server::server(){
    epoll_fd = epoll_create1( 0 );
    ERROR_CHECK(epoll_fd,-1,"reuse socket error");
    stop=0;
    user_id=0;
    threadpool=new ThreadPool(10);
    initnet();
    initsig();
}

void server::initnet(){
    int reuse, ret;
	//int listen_fd2,ret2;
	struct sockaddr_in servaddr;
	//struct sockaddr_in servaddr2;
	reuse=1;
	listen_fd = socket( AF_INET, SOCK_STREAM, 0);
    ERROR_CHECK(listen_fd,-1,"create socket error");
	
	if ( setsockopt( listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) ) == -1 )
	{
		perror( "reuse socket error" );
		exit(0);
	}

	memset( &servaddr, 0, sizeof( servaddr) );
	servaddr.sin_family = AF_INET;
	//servaddr2.sin_family=AF_INET;
	servaddr.sin_addr.s_addr = inet_addr("172.18.81.202");
	//servaddr2.sin_addr.s_addr = inet_addr("172.18.81.202");
	servaddr.sin_port = htons( 8001 );//这是普通通信用的端口

	ret = bind( listen_fd, ( struct sockaddr* )&servaddr, sizeof( servaddr ));
	ERROR_CHECK(ret,-1,"bind socket error")

	ret = listen( listen_fd, 10 );
    ERROR_CHECK(ret,-1,"listen socket error");
}

void server::setnonblock(int fd){
    int old_option = fcntl( fd, F_GETFL );
	int new_option = old_option | O_NONBLOCK;
	fcntl( fd, F_SETFL, new_option ); 
}

void server::initsig(){
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;  // 允许 siginfo_t 传参
    sa.sa_sigaction = signalhandler;
    sigemptyset(&sa.sa_mask);

    // 设置信号处理函数
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        return ;
    }
}

void server::addfd(int fd){
    epoll_event event;//创建临时的epoll_event对象
	event.data.fd = fd;//将fd加装到event上
	event.events = EPOLLIN | EPOLLET;// 监听可读事件 + 边缘触发模式
	epoll_ctl( epoll_fd, EPOLL_CTL_ADD, fd, &event );// 将 fd 添加到 epoll 实例
	setnonblock( fd );
}


void server::run(){
    //添加监听网络
    addfd(listen_fd);

    //添加监听信号
    int ret = socketpair( PF_UNIX, SOCK_STREAM, 0, sig_pipefd );
    ERROR_CHECK(ret,-1,"socketpair error");
    setnonblock(sig_pipefd[1]);
    addfd(sig_pipefd[0]);
    
    //开始轮询
    epoll_event events[30];//最多同时响应100个
    //LOG(DEBUG)<<"开始监听"<<'\n';
    while(!stop){
        int number=epoll_wait(epoll_fd,events,30,-1);//-1表示不设置超时
        if(number<0&&errno != EINTR ){
            break;
        }
        for(int i=0;i<number;i++){
            if(events[i].data.fd==listen_fd){
                //LOG(DEBUG)<<"有用户连接"<<'\n';
                struct sockaddr_in client_add;
                socklen_t client_add_len = sizeof(client_add);
                int m_sockfd=accept(listen_fd,(struct sockaddr*)&client_add, &client_add_len);

                user* user_=new user(m_sockfd);
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, user_->user_server_pipe) == -1) {
                    perror("socketpair");
                    exit(1);
                }
                users[++user_id]=user_;
                user_->user_id=user_id;
                addfd(user_->user_server_pipe[0]);
                threadpool->addTask([=](){
                    user_->run() ;
                });
            }
            else if((events[i].data.fd==sig_pipefd[0] ) && ( events[i].events & EPOLLIN )) {
                //LOG(DEBUG)<<"信号通道发来信号"<<'\n';
                int signal;
				int ret = recv( sig_pipefd[0], ( char * )&signal, sizeof( signal ), 0 );
				if ( ret < 0 )
				{
					continue;
				}
                else{
                    if(signal==SIGINT){
                        //遍历发送给所有用户关闭信号(有时间再做)
                        //LOG(DEBUG)<<"要退出了"<<'\n';
                        stop=1;
                    }
                }
            }
            else{//查看是否是子线程发来需要广播的信息
                //LOG(DEBUG)<<"有需要广播的信息"<<'\n';
                int user_id=-1;
                char message[512];
                size_t len=0;
                for(auto &iter:users){
                    if(events[i].data.fd==iter.second->user_server_pipe[0]){//收到客户端的需要广播的信息（ 格式：type#username-msg）
                        len=read(events[i].data.fd,message,512);
                        message[len]='\0';
                        LOG(DEBUG)<<"接收到客户端需要广播的信息:"<<message<<'\n';
                        user_id=iter.second->user_id;
                    }
                }
                if(user_id!=-1){//确保是子线程发来的信息，而不是奇怪的信息
                    bool quit=0;
                    if(message[0]-'0'==mes_type::user_quit){
                        quit=1;
                    }
                    for(auto iter = users.begin(); iter != users.end(); ) {
                        if(iter->second->user_id != user_id) {
                            write(iter->second->user_server_pipe[0], message, strlen(message));
                            ++iter;
                        } else {
                            if(quit) {
                                LOG(DEBUG)<<"用户"<<user_id<<"退出了"<<'\n';
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, iter->second->user_server_pipe[0], nullptr);
                                close(iter->second->user_server_pipe[0]);
                                delete iter->second;
                                iter = users.erase(iter);
                            } else {
                                ++iter;
                            }
                        }
                    }
                }
            }
        }
    }
}