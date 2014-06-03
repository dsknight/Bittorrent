#include "btdata.h"
#include "util.h"


// 正确的关闭客户端
void client_shutdown(int sig)
{
    // 设置全局停止变量以停止连接到其他peer, 以及允许其他peer的连接. Set global stop variable so that we stop trying to connect to peers and
    // 这控制了其他peer连接的套接字和连接到其他peer的线程.
    exit(0);
}
