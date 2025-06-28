#pragma once

namespace
{
    enum mes_type{
        new_user,
        user_quit,
        common_msg
    };
};

struct msg_def{
    int msg_len;
    int msg_type;
};

struct request_msg
{
    msg_def header;
    char msg[1024];
};

struct response_msg
{
    msg_def header;
    char msg[1024];
};

