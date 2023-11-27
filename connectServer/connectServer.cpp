//c/c++
#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include<vector>
//linux
#include<sys/types.h>
#include<sys/stat.h>
#include<unistd.h>
#include<signal.h>
#include<errno.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<sys/poll.h>
//user define
#include"connectServer.h"
#include"../mq_util/mq_util.h"
#include"../timer/timer.h"
#include"../common_def/comdef.h"
#include"../logger/logger.h"
#include"../message/message.h"
#include"../ini_file/ini_file.h"
using namespace WSMQ;
using std::vector;
//全局对象
Logger conSrvLogger;
int iTestSend=1;
int iTestRecv=1;

ConnectServer::ConnectServer()
{
    m_iEpollFd=-1;
    m_iListenFd=-1;
    m_bStop=false;
    m_pClientConnectManager=NULL;
    m_pQueueToLogicLayer=NULL;
    m_pQueueLogicToConnect=NULL;
    m_iSendCount=0;
    m_iRecvCount=0;
}

ConnectServer::~ConnectServer()
{
    if(m_pClientConnectManager)
    {
        delete m_pClientConnectManager;
        m_pClientConnectManager=NULL;
    }
    if(m_pQueueToLogicLayer)
    {
        delete m_pQueueToLogicLayer;
        m_pQueueToLogicLayer=NULL;
    }
    if(m_pQueueLogicToConnect)
    {
        delete m_pQueueLogicToConnect;
        m_pQueueLogicToConnect=NULL;
    }
}

ConnectServer* ConnectServer::GetInstance()
{
    if(m_pConnectServer==NULL)
    {
        m_pConnectServer=new ConnectServer();
    }
    return m_pConnectServer;
}

void ConnectServer::Destroy()
{
    if(m_pConnectServer)
    {
        delete m_pConnectServer;
        m_pConnectServer=NULL;
    }
}

//初始化信号处理函数
int ConnectServer::InitSigHandler()
{
    struct sigaction act;//信号安装函数
    memset(&act,0,sizeof(act));
    act.sa_handler=SigTermHandler; //新的信号处理函数         static void SigTermHandler(int iSig){ m_bStop = true; }
    //检查或修改与指定信号相关联的处理动作
    sigaction(SIGINT,&act,NULL);//signum参数指出要捕获的信号类型，act参数指定新的信号处理方式，oldact参数输出先前信号的处理方式（如果不为NULL的话）
    sigaction(SIGTERM,&act,NULL);
    sigaction(SIGQUIT,&act,NULL);

    sigset_t set;//进程信号集合
    sigemptyset(&set);//将set的信号集先清空
    //试图访问未分配给自己的内存
    sigaddset(&set, SIGSEGV);//把后面的信号加入到set中，该位为1，阻塞
    //非法地址
    sigaddset(&set, SIGBUS);
    //调用abort函数生成的信号
    sigaddset(&set, SIGABRT);
    // 执行了非法指令
    sigaddset(&set, SIGILL);
    //在发生致命的算术运算错误时发出
    sigaddset(&set, SIGFPE);
    sigprocmask(SIG_UNBLOCK,&set,NULL);//将参数newset所指向的信号集中的信号从当前的信号掩码中移除
    return SUCCESS;

}

int ConnectServer::InitConf(const char *ipPath)
{
    // if(!FuncTool::IsFileExist(ipPath))
    // {
    //     return -1;
    // }
    //CIniFile objIniFile(ipPath);
    // objIniFile.GetInt("MQ_CONF", "MaxCliPackSize", 0, &MAX_CLINT_PACKAGE_LENGTH);
    // objIniFile.GetInt("MQ_CONF", "MaxSrvPackSize", 0, &MAX_SERVER_PACKAGE_LENGTH);
    // objIniFile.GetInt("MQ_CONF", "MaxNameLength", 0, &MAX_NAME_LENGTH);
    // objIniFile.GetString("MQ_CONF", "ServerIP", "",SERVER_DEFAULT_IP_ADDR , sizeof(SERVER_DEFAULT_IP_ADDR));
    // objIniFile.GetInt("MQ_CONF", "ServerPort", 0, (int *)&SERVER_DEFAULT_PORT);
    // objIniFile.GetInt("MQ_CONF", "EpollCount", 0, &CLIENT_EPOLL_COUNT);
    // objIniFile.GetInt("MQ_CONF", "SockSendBufSize", 32, &SOCK_SEND_BUFF_SIZE);
    // objIniFile.GetInt("MQ_CONF", "SockRecvBufSize", 0, &SOCK_RECV_BUFF_SIZE);
    // objIniFile.GetInt("MQ_CONF", "ShmConLogicKey", 0, &CONNECT_TO_LOGIC_KEY);
    // objIniFile.GetInt("MQ_CONF", "ShmLogicConKey", 0, &LOGIC_TO_CONNECT_KEY);
    // objIniFile.GetInt("MQ_CONF", "ShmPersisLogicKey", 0, &PERSIS_TO_LOGIC_KEY);
    // objIniFile.GetInt("MQ_CONF", "ShmLogicPersisKey", 0, &LOGIC_TO_PERSIS_KEY);
    // objIniFile.GetInt("MQ_CONF", "ShmQueueSize", 0, &SHM_QUEUE_SIZE);
    // objIniFile.GetString("MQ_CONF", "PersisExchangePath", "",DEFAULT_EXCHANGE_PATH , sizeof(DEFAULT_EXCHANGE_PATH));
    // objIniFile.GetString("MQ_CONF", "PersisQueuePath", "",DEFAULT_QUEUE_PATH , sizeof(DEFAULT_QUEUE_PATH));
    // objIniFile.GetInt("MQ_CONF", "PersisMsgFileSize", 0, &DURABLE_MESSAGE_FILE_SIZE);
    return 0;
}

int ConnectServer::Init()
{
    if(InitConf(CONF_FILE_PATH)!=SUCCESS)
    {
        return ERROR;
    }
    InitSigHandler();

    //创建epollfd和listenfd
    m_iEpollFd=epoll_create(CLIENT_EPOLL_COUNT);//客户端最大连接数目 1000  创建一个epoll句柄进行事件监听
    if(m_iEpollFd==-1)
    {
        conSrvLogger.WriteLog(mq_log_err,"Create epoll failed,errmsg is %s",strerror(errno));
        conSrvLogger.Print(mq_log_err,"Create epoll failed,errmsg is %s",strerror(errno));
        return ERR_CONNECT_SERVER_INIT;
    }
    m_iListenFd=socket(AF_INET,SOCK_STREAM,0);//socket向网络发出请求
    //选择 AF_INET 的目的就是使用 IPv4 进行通信。因为 IPv4 使用 32 位地址，相比 IPv6 的 128 位来说，计算更快，便于用于局域网通信。
    if(m_iListenFd==-1)
    {
        conSrvLogger.WriteLog(mq_log_err,"Create listenfd failed,errmsg is %s",strerror(errno));
        conSrvLogger.Print(mq_log_err,"Create listenfd failed,errmsg is %s",strerror(errno));
        return ERR_CONNECT_SERVER_INIT;
    }
    if(FuncTool::SetNonBlock(m_iListenFd)!=FuncTool::SUCCESS)//设置非阻塞
    {
        conSrvLogger.WriteLog(mq_log_err,"set listenfd nonblocking failed,errmsg is %s",strerror(errno));
        conSrvLogger.Print(mq_log_err,"set listenfd nonblocking failed,errmsg is %s",strerror(errno));
        return ERR_CONNECT_SERVER_INIT;
    }
    int opt=1;
    int ret=setsockopt(m_iListenFd,SOL_SOCKET,SO_REUSEADDR,(char *)&opt,sizeof(opt));//设置套接字
    if(ret==-1)
    {
        conSrvLogger.WriteLog(mq_log_err,"set reuse addr failed,errmsg is %s",strerror(errno));
        conSrvLogger.Print(mq_log_err,"set reuse addr failed,errmsg is %s",strerror(errno));
        return ERR_CONNECT_SERVER_INIT;
    }
    struct sockaddr_in serverAddr;//处理网络通信的地址
    memset(&serverAddr,0,sizeof(serverAddr));
    serverAddr.sin_family=AF_INET;
    serverAddr.sin_port=htons(SERVER_DEFAULT_PORT);//11111
    serverAddr.sin_addr.s_addr=htonl(INADDR_ANY);//INADDR_ANY就是指定地址为0.0.0.0的地址，这个地址事实上表示不确定地址，或“所有地址”、“任意地址”。
//也就是表示本机的所有IP，因为有些机子不止一块网卡，多网卡的情况下，这个就表示所有网卡ip地址的意思。
    //客户端connect时，不能使用INADDR_ANY选项。必须指明要连接哪个服务器IP。
    //服务器默认IP
    // const static char* SERVER_DEFAULT_IP_ADDR="100.96.188.244";
    // //服务器默认端口
    // const static unsigned short SERVER_DEFAULT_PORT=11111;
    if(bind(m_iListenFd,(struct sockaddr *)&serverAddr,sizeof(serverAddr))<0)//绑定到对应的socket
    {
        conSrvLogger.WriteLog(mq_log_err,"bind listenfd failed,errmsg is %s",strerror(errno));
        conSrvLogger.Print(mq_log_err,"bind listenfd failed,errmsg is %s",strerror(errno));
        return ERR_CONNECT_SERVER_INIT;
    }
    if(listen(m_iListenFd,512)<0)//监听socket,及socket数量
    {
        conSrvLogger.WriteLog(mq_log_err,"listen failed,errmsg is %s",strerror(errno));
        conSrvLogger.Print(mq_log_err,"listen failed,errmsg is %s",strerror(errno));
        return ERR_CONNECT_SERVER_INIT;
    }
     //初始化连接管理对象
    conSrvLogger.Print(mq_log_err,"begain create client manager");
    m_pClientConnectManager=new ClientConnectManager();
    conSrvLogger.Print(mq_log_err,"end create client manager");
    ClientConnect *pClientConnect=m_pClientConnectManager->GetOneFreeConnect();//客户端连接池内获取一个可用的连接
    if(pClientConnect==NULL)
    {
        conSrvLogger.WriteLog(mq_log_err,"out of memory!");
        conSrvLogger.Print(mq_log_err,"out of memory!");
        return ERR_CONNECT_SERVER_INIT;
    }
    struct sockaddr_in cliAddr;
    pClientConnect->m_iSockfd=m_iListenFd;//把创建的socket连接给他
    pClientConnect->m_iClientAddr=cliAddr.sin_addr.s_addr;//32位ipv4地址
    struct epoll_event event;//注册所感兴趣的事件和回传所发生待处理的事件
    event.events=EPOLLIN;//表示可读
    FuncTool::MakeEpollEvent(event,pClientConnect);//创建事件
    if(epoll_ctl(m_iEpollFd,EPOLL_CTL_ADD,m_iListenFd,&event)<0)//用于控制某个文件描述符上的事件,注册事件
    {
        conSrvLogger.WriteLog(mq_log_err,"add listen fd failed!,errmsg is %s",strerror(errno));
        conSrvLogger.Print(mq_log_err,"add listen fd failed!,errmsg is %s",strerror(errno));
        return ERR_CONNECT_SERVER_INIT;
    }

    //初始化和业务逻辑层的通信通道 共享内存的队列
    m_pQueueToLogicLayer=new ShmQueue();
    m_pQueueLogicToConnect=new ShmQueue();
    //接入层和业务逻辑层通信的共享内存的key
    // const static int CONNECT_TO_LOGIC_KEY=12345;
    // const static int LOGIC_TO_CONNECT_KEY=54321;
    //共享内存大小
    // const static int SHM_QUEUE_SIZE=1024*1024*100;
    m_pQueueToLogicLayer->Init(CONNECT_TO_LOGIC_KEY,SHM_QUEUE_SIZE);
    m_pQueueLogicToConnect->Init(LOGIC_TO_CONNECT_KEY,SHM_QUEUE_SIZE);
    return SUCCESS;
}

int ConnectServer::GetClientPackageFromBuff(char *ipBuff,int ipBuffLen,int iClientIndex,char **opNextPack,int *opPackLen)
{
    ClientPackageHead header;
    if(ipBuffLen<(int)sizeof(header))
    {
        return ERR_CONNECT_SERVER_GET_PACKAGE;
    }
    char *pTemp=ipBuff;
    int offset=FuncTool::ReadShort(pTemp,header.m_iPackLen);
    if(ipBuffLen<header.m_iPackLen)
    {
        return ERR_CONNECT_SERVER_GET_PACKAGE;
    }
    if(header.m_iPackLen<sizeof(header))
    {
        return ERR_CONNECT_SERVER_GET_PACKAGE;
    }
    pTemp+=offset;
    //保存客户端下标
    FuncTool::WriteInt(pTemp,iClientIndex);

    //下一个包位置
    *opNextPack=ipBuff+header.m_iPackLen;
    *opPackLen=header.m_iPackLen;
    return SUCCESS;
}

int ConnectServer::SendDataToClient(const char *ipData,int iDataLen,ClientConnect *ipClient)
{
    if(ipClient==NULL||ipClient->m_iSockfd<=0)
    {
        return ERR_CONNECT_SERVER_SEND_PACKAGE;
    }
    //等待套接字可写
    struct pollfd fds[1];
    fds[0].fd=ipClient->m_iSockfd;
    fds[0].events=POLLOUT|POLLERR;//请求的事件 
    while(true)
    {
        int ret=poll(fds,1,-1);//监视的描述符的数目。 最后一个参数是指定poll在返回前没有接收事件时应该等待的时间，小于0表示出错
        if(ret<0)
        {
            if(errno==EINTR||errno==EAGAIN)
            {
                continue;
            }
            return ERR_CONNECT_SERVER_SEND_PACKAGE;
        }
        else if(ret==0)
        {
            return ERR_CONNECT_SERVER_SEND_PACKAGE;
        }
        else if(fds[0].revents&POLLOUT)//普通数据可写
        {
            break;
        }
        else
        {
            return ERR_CONNECT_SERVER_SEND_PACKAGE;
        }
    }
    ipClient->m_tLastRecvTime=time(NULL);
    int iRet=FuncTool::Sendn(ipClient->m_iSockfd,ipData,iDataLen);
    if(iRet==FuncTool::ERROR)
    {
        return ERR_CONNECT_SERVER_SEND_PACKAGE;
    }
    return SUCCESS;
}


int ConnectServer::ProcessEpollData(struct epoll_event *ipEvents,int iNum)
{
    for(int i=0;i<iNum;++i)
    {
        conSrvLogger.WriteLog(mq_log_info,"begain process socket...,index is %d",i);
        conSrvLogger.Print(mq_log_info,"begain process socket...index is %d",i);
        if(ipEvents[i].events&EPOLLIN)//监听的事件可读
        {
            ClientConnect *pClientConnect=(ClientConnect *)FuncTool::GetEventDataPtr(ipEvents[i]);//获取监听事件数据
            if(pClientConnect==NULL)
            {
                continue;
            }
            int sockfd=pClientConnect->m_iSockfd;
            if(sockfd==m_iListenFd)//客户端的套接字和监听的套接字相同
            {
                struct sockaddr_in cliAddr;
                memset(&cliAddr,0,sizeof(cliAddr));
                socklen_t len=sizeof(cliAddr);
                int confd=accept(m_iListenFd,(struct sockaddr *)&cliAddr,&len);//监听的套接字，端对端地址及大小//接收客户端的连接
                if(confd<=0)
                {
                    continue;
                }
                conSrvLogger.WriteLog(mq_log_info,"accepted new connection...");
                conSrvLogger.Print(mq_log_info,"accepted new connection...");
                FuncTool::SetNonBlock(confd);
                int iSendBufSize=SOCK_SEND_BUFF_SIZE;
                int iRevBufSize=SOCK_RECV_BUFF_SIZE;
                setsockopt(confd,SOL_SOCKET,SO_SNDBUF,(char *)&iSendBufSize,sizeof(int));//接收缓冲区
                setsockopt(confd,SOL_SOCKET,SO_RCVBUF,(char *)&iRevBufSize,sizeof(int));//发送缓冲区
                struct epoll_event event;
                event.events=EPOLLIN;
                //获取一个连接对象
                ClientConnect *pClientConnect=m_pClientConnectManager->GetOneFreeConnect();
                if(pClientConnect==NULL)
                {
                    conSrvLogger.WriteLog(mq_log_err,"no free connect obj...");
                    conSrvLogger.Print(mq_log_err,"no free connect obj...");
                    close(confd);
                    continue;
                }
                pClientConnect->m_iSockfd=confd;
                pClientConnect->m_iClientAddr=cliAddr.sin_addr.s_addr;
                pClientConnect->m_tLastRecvTime=time(NULL);
                FuncTool::MakeEpollEvent(event,pClientConnect);//获取事件数据
                epoll_ctl(m_iEpollFd,EPOLL_CTL_ADD,confd,&event);//注册套接字上的事件
                //添加到在线用户链
                pClientConnect->m_bInMap=true;
                m_pClientConnectManager->AddOnlineClient(pClientConnect);
                continue;
            }
            //现有连接有数据过来
            conSrvLogger.WriteLog(mq_log_info,"recv data from socket...");
            conSrvLogger.Print(mq_log_info,"recv data from socket...");
            int iRecvBytes=pClientConnect->m_pRecvEnd-pClientConnect->m_pRecvTail;
            int ret=recv(sockfd,pClientConnect->m_pRecvTail,iRecvBytes,0);
            //s-已建立连接的套接字；buf-存放接收到的数据的缓冲区指针；len-buf的长度；flags-调用方式
            if(ret<0)
            {
                if(errno==EAGAIN)//在非阻塞模式下调用了阻塞操作
                {
                    //当前缓冲区无数据可读
                    continue;
                }
                else
                {
                    conSrvLogger.WriteLog(mq_log_err,"client closed...");
                    conSrvLogger.Print(mq_log_err,"client closed...");
                    //告知业务逻辑层
                    char pMsgPack[MAX_CLINT_PACKAGE_LENGTH];
                    int iPackLen=MAX_CLINT_PACKAGE_LENGTH;
                    memset(pMsgPack,0,sizeof(pMsgPack));
                    int ret=0;
                    ClientExitMessage oMsg(pClientConnect->m_iIndex);
                    if((ret=oMsg.GetMessagePack(pMsgPack,&iPackLen))!=Message::SUCCESS)
                    {
                        conSrvLogger.WriteLog(mq_log_err,"tell logic server client close failed...");
                        conSrvLogger.Print(mq_log_err,"tell logic server client close failed...");
                    }
                    m_pQueueToLogicLayer->Enqueue(pMsgPack,iPackLen);//入队
                    m_pClientConnectManager->ClientExit(pClientConnect,m_iEpollFd);//退出客户端
                    
                    continue;
                }
            }
            else if(ret==0)
            {
                conSrvLogger.WriteLog(mq_log_err,"client closed...");
                conSrvLogger.Print(mq_log_err,"client closed...");
                //告知业务逻辑层
                char pMsgPack[MAX_CLINT_PACKAGE_LENGTH];
                int iPackLen=MAX_CLINT_PACKAGE_LENGTH;
                memset(pMsgPack,0,sizeof(pMsgPack));
                int ret=0;
                ClientExitMessage oMsg(pClientConnect->m_iIndex);
                if((ret=oMsg.GetMessagePack(pMsgPack,&iPackLen))!=Message::SUCCESS)
                {
                    conSrvLogger.WriteLog(mq_log_err,"tell logic server client close failed...");
                    conSrvLogger.Print(mq_log_err,"tell logic server client close failed...");
                }
                m_pQueueToLogicLayer->Enqueue(pMsgPack,iPackLen);
                m_pClientConnectManager->ClientExit(pClientConnect,m_iEpollFd);
                continue;
            }
            pClientConnect->m_tLastRecvTime=time(NULL);
            pClientConnect->m_pRecvTail+=ret;//接收尾部后移
            int packCount=0;
            char *pCurPack=pClientConnect->m_pRecvHead;
            char *pNextPack=NULL;
            int buffLen=pClientConnect->m_pRecvTail-pClientConnect->m_pRecvHead;//计算当前接收到的数据长度
            int packLen=0;
            //将收到的数据分包处理
            while(true)
            {
                ret=GetClientPackageFromBuff(pCurPack,buffLen,pClientConnect->m_iIndex,&pNextPack,&packLen);
                if(ret!=SUCCESS)
                {
                    //说明接收到的包不是一个完整包,将接收数据的头部和尾部前移，方便下一次接收
                    int iMoveCount=pClientConnect->m_pRecvTail-pClientConnect->m_pRecvHead;
                    memmove(pClientConnect->m_pRecvBuff,pClientConnect->m_pRecvHead,iMoveCount);//拷贝
                    pClientConnect->m_pRecvHead=pClientConnect->m_pRecvBuff;
                    pClientConnect->m_pRecvTail=pClientConnect->m_pRecvHead+iMoveCount;
                    break;
                }
                //LOG_INFOLOG_INFO(0, 0, "push one msg to logic : %d",iTestSend++);
                while(m_pQueueToLogicLayer->Enqueue(pCurPack,packLen)==ShmQueue::ERR_SHM_QUEUE_FULL)
                {
                    //队列满了就持续等待不能将消息丢弃
                    //LOG_ERROR(0, 0, "shm queue is full");
                }
                ++m_iRecvCount;
                if(m_iRecvCount%10000==0)
                {
                    //LOG_INFO(0, 0, "recv %d msg %s",m_iRecvCount,pCurPack+80);
                }
                pClientConnect->m_pRecvHead+=packLen;//每将一个包放入共享内存队列，接收数据的头部前移
                conSrvLogger.WriteLog(mq_log_info,"one client pack put into shmqueue...");
                conSrvLogger.Print(mq_log_info,"one client pack put into shmqueue...");
                if(buffLen>packLen)
                {
                    //还有下一个包
                    pCurPack=pNextPack;
                    buffLen-=packLen;
                    conSrvLogger.WriteLog(mq_log_info,"pack index is %d,recv next pack...",packCount);
                    conSrvLogger.Print(mq_log_info,"pack index is %d,recv next pack...",packCount);
                }
                else
                {
                    //说明接收的都是一个个完整包，并且所有包已经接收完毕，此时将接收数据的头尾重置
                    pClientConnect->m_pRecvHead=pClientConnect->m_pRecvBuff;
                    pClientConnect->m_pRecvTail=pClientConnect->m_pRecvBuff;
                    break;
                }
                ++packCount;
            }
        }
        else
        {
            //收到其他事件，断开连接
            ClientConnect *pClientConnect=(ClientConnect *)FuncTool::GetEventDataPtr(ipEvents[i]);
            conSrvLogger.WriteLog(mq_log_err,"client closed...");
            conSrvLogger.Print(mq_log_err,"client closed...");
            if(pClientConnect==NULL)
            {
                continue;
            }
            //告知业务逻辑层
            char pMsgPack[MAX_CLINT_PACKAGE_LENGTH];
            int iPackLen=MAX_CLINT_PACKAGE_LENGTH;
            memset(pMsgPack,0,sizeof(pMsgPack));
            int ret=0;
            ClientExitMessage oMsg(pClientConnect->m_iIndex);
            if((ret=oMsg.GetMessagePack(pMsgPack,&iPackLen))!=Message::SUCCESS)
            {
                conSrvLogger.WriteLog(mq_log_err,"tell logic server client close failed...");
                conSrvLogger.Print(mq_log_err,"tell logic server client close failed...");
            }
            m_pQueueToLogicLayer->Enqueue(pMsgPack,iPackLen);
            m_pClientConnectManager->ClientExit(pClientConnect,m_iEpollFd);
        }
    }
    return SUCCESS;
}


int ConnectServer::Run()
{
    struct epoll_event *pEvents=new struct epoll_event[CLIENT_EPOLL_COUNT];

    while(!m_bStop)
    {
        //记录本轮是否有数据需要处理
        bool epollEmpty=false;
        bool queueDataEmpty=false;

        ConnectSrvTimer controlTimer;//当前队列数据时间
        conSrvLogger.WriteLog(mq_log_info,"begain loop...");
        conSrvLogger.Print(mq_log_info,"begain loop...");
        controlTimer.Begain();
        int num=epoll_wait(m_iEpollFd,pEvents,CLIENT_EPOLL_COUNT,0);//等待事件的产生  返回需要处理的事件数目
        if(num==0)
        {
            epollEmpty=true;
        }
        else
        {
             ProcessEpollData(pEvents,num);//处理本轮所有的事件
        }
        
         //记录处理完epoll数据的时间
        controlTimer.EpollDown();
        conSrvLogger.WriteLog(mq_log_info,"process socket down");
        conSrvLogger.Print(mq_log_info,"process socket down");
        conSrvLogger.WriteLog(mq_log_info,"begain process shmqueue data from logic server...");
        conSrvLogger.Print(mq_log_info,"begain process shmqueue data from logic server...");
        controlTimer.GetMaxTimeForQueueData();
        bool bRecvData=true;
        int iDateRecvCount=0;
        char pBuff[MAX_SERVER_PACKAGE_LENGTH];
        memset(pBuff,0,MAX_SERVER_PACKAGE_LENGTH);
        vector<ClientConnect *>vSendClients;
        while(bRecvData)
        {
            int iLen=MAX_SERVER_PACKAGE_LENGTH;
            int ret=m_pQueueLogicToConnect->Dequeue(pBuff,&iLen);//接收的数据出队
            if(ret==ShmQueue::ERR_SHM_QUEUE_EMPTY)
            {
                conSrvLogger.WriteLog(mq_log_info,"shmqueue is empty");
                conSrvLogger.Print(mq_log_info,"shmqueue is empty");
                break;
            }
            else if(ret!=ShmQueue::SUCCESS)
            {
                conSrvLogger.WriteLog(mq_log_err,"get data from shmqueue failed,errMsg is %s",m_pQueueLogicToConnect->GetErrMsg());
                //conSrvLogger.Print(mq_log_err,"get data from shmqueue failed,errMsg is %s",m_pQueueLogicToConnect->GetErrMsg());
                continue;
            }
            ++iDateRecvCount;

            //接收到的包长度至少为客户端包头长度
            ClientPackageHead header;
            if(iLen<(int)sizeof(header))
            {
                conSrvLogger.WriteLog(mq_log_err,"shmqueue data is tool small");
                conSrvLogger.Print(mq_log_err,"shmqueue data is tool small");
                continue;
            }
            int offset=FuncTool::ReadShort(pBuff,header.m_iPackLen);
            //查找对应客户端下标
            char *pTemp=pBuff+offset;
            offset=FuncTool::ReadInt(pTemp,header.m_iClientIndex);

            //根据下标查找客户
            ClientConnect *pClientConnect=m_pClientConnectManager->FindClient(header.m_iClientIndex);
            if(pClientConnect==NULL)
            {
                conSrvLogger.WriteLog(mq_log_err,"client isn't exist");
                conSrvLogger.Print(mq_log_err,"client isn't exist");
                //告知业务逻辑层
                char pMsgPack[MAX_CLINT_PACKAGE_LENGTH];
                int iPackLen=MAX_CLINT_PACKAGE_LENGTH;
                memset(pMsgPack,0,sizeof(pMsgPack));
                int ret=0;
                ClientExitMessage oMsg(header.m_iClientIndex);
                if((ret=oMsg.GetMessagePack(pMsgPack,&iPackLen))!=Message::SUCCESS)
                {
                    conSrvLogger.WriteLog(mq_log_err,"tell logic server client close failed...");
                    conSrvLogger.Print(mq_log_err,"tell logic server client close failed...");
                }
                m_pQueueToLogicLayer->Enqueue(pMsgPack,iPackLen);
                continue;
            }
            //读取消息类型
            pTemp+=offset;
            offset=FuncTool::ReadShort(pTemp,header.m_iCmdId);
            if(header.m_iCmdId!=CMD_CLIENT_PULL_MESSAGE&&header.m_iCmdId!=CMD_SERVER_PUSH_MESSAGE
            &&header.m_iCmdId!=CMD_SERVER_REPLY_MESSAGE&&header.m_iCmdId!=CMD_SERVER_ACK_MESSAGE)
            {
                conSrvLogger.WriteLog(mq_log_err,"msg type not fit");
                conSrvLogger.Print(mq_log_err,"msg type not fit");
                continue;
            }
            ++m_iSendCount;
            if(m_iSendCount%10000==0)
            {
                //LOG_INFO(0, 0, "send %d msg %s",m_iSendCount,pBuff+12);
            }
            //将消息放入缓冲区套接字发送缓冲区，若缓冲区空间不足，则先发送数据，再放入发送缓冲区
            int iFreeSpace=pClientConnect->m_pSendEnd-pClientConnect->m_pSendTail;
            if(iFreeSpace>=iLen)
            {
                memcpy(pClientConnect->m_pSendTail,pBuff,iLen);
                pClientConnect->m_pSendTail+=iLen;
                vSendClients.push_back(pClientConnect);
            }
            else
            {
                //发送消息给客户
                conSrvLogger.WriteLog(mq_log_info,"send one pack to client...");
                conSrvLogger.Print(mq_log_info,"send one pack to client...");
                //LOG_INFO(0, 0, "begain send one msg to client %d",iTestRecv++);
                int iSendSize=pClientConnect->m_pSendTail-pClientConnect->m_pSendHead;
                ret=SendDataToClient(pClientConnect->m_pSendHead,iSendSize,pClientConnect);
                //LOG_INFO(0, 0, "end send one msg to client %d",iTestRecv);
                if(ret!=ConnectServer::SUCCESS)
                {
                    //说明对方关闭了
                    conSrvLogger.WriteLog(mq_log_err,"client closed...");
                    conSrvLogger.Print(mq_log_err,"client closed...");
                    //告知业务逻辑层
                    char pMsgPack[MAX_CLINT_PACKAGE_LENGTH];
                    int iPackLen=MAX_CLINT_PACKAGE_LENGTH;
                    memset(pMsgPack,0,sizeof(pMsgPack));
                    int ret=0;
                    ClientExitMessage oMsg(pClientConnect->m_iIndex);
                    if((ret=oMsg.GetMessagePack(pMsgPack,&iPackLen))!=Message::SUCCESS)
                    {
                        conSrvLogger.WriteLog(mq_log_err,"tell logic server client close failed...");
                        conSrvLogger.Print(mq_log_err,"tell logic server client close failed...");
                    }
                    m_pQueueToLogicLayer->Enqueue(pMsgPack,iPackLen);
                    m_pClientConnectManager->ClientExit(pClientConnect,m_iEpollFd);
                }
                else
                {
                    pClientConnect->m_pSendTail=pClientConnect->m_pSendBuff;
                    memcpy(pClientConnect->m_pSendTail,pBuff,iLen);
                    pClientConnect->m_pSendTail+=iLen;
                    vSendClients.push_back(pClientConnect);
                }
            }
            //每读取100个就检查一下是否还可以继续读
            if(iDateRecvCount>=100)
            {
                bRecvData=controlTimer.HaveTimeForQueueData();
            }
        }
        //遍历所有的客户将数据发送出去
        for(int i=0;i<vSendClients.size();++i)
        {
            //发送消息给客户
            ClientConnect *pClientConnect=vSendClients[i];
            conSrvLogger.WriteLog(mq_log_info,"send one pack to client...");
            conSrvLogger.Print(mq_log_info,"send one pack to client...");
            //LOG_INFO(0, 0, "begain send one msg to client %d",iTestRecv++);
            int iSendSize=pClientConnect->m_pSendTail-pClientConnect->m_pSendHead;
            int ret=SendDataToClient(pClientConnect->m_pSendHead,iSendSize,pClientConnect);
            //LOG_INFO(0, 0, "end send one msg to client %d",iTestRecv);
            if(ret!=ConnectServer::SUCCESS)
            {
                //说明对方关闭了
                conSrvLogger.WriteLog(mq_log_err,"client closed...");
                conSrvLogger.Print(mq_log_err,"client closed...");
                //告知业务逻辑层
                char pMsgPack[MAX_CLINT_PACKAGE_LENGTH];
                int iPackLen=MAX_CLINT_PACKAGE_LENGTH;
                memset(pMsgPack,0,sizeof(pMsgPack));
                int ret=0;
                ClientExitMessage oMsg(pClientConnect->m_iIndex);
                if((ret=oMsg.GetMessagePack(pMsgPack,&iPackLen))!=Message::SUCCESS)
                {
                    conSrvLogger.WriteLog(mq_log_err,"tell logic server client close failed...");
                    conSrvLogger.Print(mq_log_err,"tell logic server client close failed...");
                }
                m_pQueueToLogicLayer->Enqueue(pMsgPack,iPackLen);
                m_pClientConnectManager->ClientExit(pClientConnect,m_iEpollFd);
            }
            else
            {
                pClientConnect->m_pSendTail=pClientConnect->m_pSendBuff;
            }
        }
        conSrvLogger.WriteLog(mq_log_info,"process shmqueue data down");
        conSrvLogger.Print(mq_log_info,"process shmqueue data down");
        controlTimer.QueueDataDown();
        qu
        queueDataEmpty=(iDateRecvCount==0);
        if(queueDataEmpty&&epollEmpty)
        {
            usleep(1000);
        }
    }
    delete []pEvents;
    pEvents=NULL;
    return SUCCESS;
}

int main(int argc,char *argv[])
{
    //初始化log对象
    char pLogPath[MQ_MAX_PATH_LEN]=DEFAULT_CONNECT_SERVER_LOG_PATH;//./mq_conSrv_log/
    if(conSrvLogger.Init(pLogPath)!=Logger::SUCCESS)
    {
        printf("logger init failed!");
        return -1;
    }

    //后台运行进程
    int ret=0;
    if((ret=FuncTool::DaemonInit())!=FuncTool::SUCCESS)
    {
        conSrvLogger.WriteLog(mq_log_err,"Daemoninit faild!,return value is %d",ret);
        conSrvLogger.Print(mq_log_err,"Daemoninit faild!,return value is %d",ret);
        return -1;
    }
    //开启服务
    conSrvLogger.WriteLog(mq_log_info,"connect server init...");
    conSrvLogger.Print(mq_log_info,"connect server init...");
    if((ret=ConnectServer::GetInstance()->Init())!=ConnectServer::SUCCESS)
    {
        conSrvLogger.WriteLog(mq_log_err,"connect server init failed!");
        conSrvLogger.Print(mq_log_err,"connect server init failed!");
        return -1;
    }
    conSrvLogger.WriteLog(mq_log_info,"connect server run...");
    conSrvLogger.Print(mq_log_info,"connect server run...");
    ConnectServer::GetInstance()->Run();
    return 0;
}
