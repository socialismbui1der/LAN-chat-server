#include"user.h"
#include<sys/epoll.h>
#include<fcntl.h>
#include<string.h>
#include<stdio.h>
#include<stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include<errno.h>
#include<iostream>
#include <unistd.h>
#include<unordered_map>
#include"easylogging.h"

#define ERROR_CHECK(ret,val,msg){ \
    if(ret==val){ \
        perror(msg); \
        exit(1); \
    }\
}\

extern std::unordered_map<std::string,user*> users;

user::user(int fd){
    this->m_sockfd=fd;
    stop=0;
}

user::~user() {
    close(m_sockfd);
    close(epoll_fd);
}

void user::addfd(int fd){
    epoll_event event;//创建临时的epoll_event对象
	event.data.fd = fd;//将fd加装到event上
	event.events = EPOLLIN | EPOLLET;// 监听可读事件 + 边缘触发模式
	epoll_ctl( epoll_fd, EPOLL_CTL_ADD, fd, &event );// 将 fd 添加到 epoll 实例
	setnonblock( fd );
}

void user::setnonblock(int fd){
    int old_option = fcntl( fd, F_GETFL );
	int new_option = old_option | O_NONBLOCK;
	fcntl( fd, F_SETFL, new_option ); 
}

request_msg user::recvmsg(int& flag){
    request_msg req;
    int byte;
    
    // 读取消息头
    int total_read = 0;
    while (total_read < sizeof(req.header)) {
        byte = recv(m_sockfd, ((char*)&req.header) + total_read, sizeof(req.header) - total_read, 0);
        if (byte == -1) {
            if (errno == EINTR) continue;      // 信号中断，继续读
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // 非阻塞 socket，没数据了
            perror("recv error");//当客户端发送关闭信号时，如果socket里还有数据未读取，客户端就会转而发送一个RST重置信号，导致异常关闭
            close(m_sockfd);
            flag=0;
            return req;
        } else if (byte == 0) {
            std::cout << "客户端关闭连接" << std::endl;
            close(m_sockfd);
            flag=0;
            return req;
        }
        total_read += byte;
    }

    if (byte == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            flag=2;
            return req; // 非阻塞模式下，当前无数据可读，返回
        } else {
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, m_sockfd, nullptr);
            close(m_sockfd);
            flag=0;
            return req;
        }
    }
    if (byte == 0) {
        std::cout << "客户端关闭连接" << std::endl;
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, m_sockfd, nullptr);
        close(m_sockfd);
        flag=0;
        return req;
    }

    // 解析消息头
    req.header.msg_type = ntohl(req.header.msg_type);
    req.header.msg_len = ntohl(req.header.msg_len);
    int req_type = req.header.msg_type;

    // 读取消息体，循环读取以保证完整性
    int total_received = 0;
    while (total_received < req.header.msg_len) {
        byte = recv(m_sockfd, req.msg + total_received, req.header.msg_len - total_received, 0);

        if (byte == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                flag=2;
                return req; // 没有更多数据可读
            } else {
                perror("recv fail");
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, m_sockfd, nullptr);
                close(m_sockfd);
                flag=0;
                return req;
            }
        }
        if (byte == 0) {
            std::cout << "客户端关闭连接" << std::endl;
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, m_sockfd, nullptr);
            close(m_sockfd);
            flag=0;
            return req;
        }
        total_received += byte;
    }

    req.msg[req.header.msg_len] = '\0'; // 确保字符串以 '\0' 结尾
    flag=1;
    return req;
}

void user::sendmsg(int m_sockfd,const char* msg){
    response_msg res;
    res.header.msg_type=msg[0]-'0';
    int msg_len=strlen(msg);
    res.header.msg_len=msg_len;
    strncpy(res.msg,msg,res.header.msg_len);
    res.header.msg_len = htonl(res.header.msg_len);
    res.header.msg_type = htonl(res.header.msg_type);
    ssize_t len=sizeof(res.header)+msg_len;
    int totalsent=0;
    while(totalsent<len){
        int bytes_sent=send(m_sockfd,reinterpret_cast<char *>(&res),len,0);
        if (bytes_sent == -1) {
            std::cout<<"send fail"<<std::endl;
            return ;
        }
        totalsent+=bytes_sent;
    }
    return;
}

void user::run(){
    epoll_fd = epoll_create1( 0 );
    ERROR_CHECK(epoll_fd,-1,"reuse socket error");

    addfd(user_server_pipe[1]);
    addfd(m_sockfd);

    epoll_event events[30];//最多同时响应100个
    //LOG(DEBUG)<<"子线程开始监听"<<'\n';
    while(!stop){
        int number=epoll_wait(epoll_fd,events,30,-1);//-1表示不设置超时
        if(number<0&&errno != EINTR ){
            break;
        }
        for(int i=0;i<number;i++){
            if(events[i].data.fd==user_server_pipe[1]){//主线程发来信息——有其他用户发来信息了,要发送给客户端
                //LOG(DEBUG)<<"收到广播信息"<<'\n';
                char message[512];
                ssize_t len=read(user_server_pipe[1],message,sizeof(message));
                if(len > 0) {
                    message[len] = '\0';
                    LOG(DEBUG) << "主线程(其他用户)发来信息:" << message;
                }
                sendmsg(m_sockfd,message);
            }
            else if(events[i].events & EPOLLIN){
                int flag=0;
                request_msg req=recvmsg(flag);
                if(flag==0){
                    stop=1;
                    break;
                }
                else if(flag==2){
                    continue;
                }
                else{//成功接收到客户发来的信息——发送给主线程，主线程发送给其他所有子线程(发送格式为：type#username-msg)，在广播中直接转发，到了各子线程发送到客户端的时候(sendmsg中)再解析
                    if(req.header.msg_type==mes_type::new_user){
                        username=req.msg;//用户刚登录进来时，发送的第一条消息就是自己的用户名
                        //LOG(DEBUG)<<"用户来临："<<username<<'\n';
                    }
                    std::string message=std::to_string(req.header.msg_type)+"#"+username+"-"+req.msg;
                    LOG(DEBUG)<<"客户发来消息："<<req.msg<<'\n';
                    write(user_server_pipe[1],message.c_str(),message.length()+1);
                }
            }
            else
                continue;
        }
    }
    std::string message=std::to_string(mes_type::user_quit)+"#"+username+"-quit";
    write(user_server_pipe[1],message.c_str(),message.length());//告诉主线程我退出了，让主线程也关闭管道并且移除监听——主线程
    close(user_server_pipe[1]);
}

