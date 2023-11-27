# mq
用c++实现的一个简单消息中间件

## 运行

- step1:终端输入`make`生成服务端和客户端文件：通过makefile生成服务端和客户端文件：

（1）按序生成mq_normal_producer mq_normal_consumer1 mq_normal_consumer2 mq_ack_producer mq_ack_consumer mq_multi_producer mq_priority_consumer mq_multi_consumer mq_priority_producer mq_pull_consumer mq_durable_producer mq_durable_consumer （客户端）

（2）mq_conSrv mq_logicSrv mq_persisSrv（服务端）

- step2:通过shell脚本调用程序:

服务端

- 启动：输入`./mq_server.start.sh`启动服务端
- 关闭：输入`./mq_server.stop.sh`+对应数字，关闭服务端


数字含义同linux下kill命令的数字含义：

```
1) SIGHUP 2) SIGINT 3) SIGQUIT 4) SIGILL
5) SIGTRAP 6) SIGABRT 7) SIGEMT 8) SIGFPE
9) SIGKILL 10) SIGBUS 11) SIGSEGV 12) SIGSYS
13) SIGPIPE 14) SIGALRM 15) SIGTERM 16) SIGUSR1
17) SIGUSR2 18) SIGCHLD 19) SIGPWR 20) SIGWINCH
21) SIGURG 22) SIGIO 23) SIGSTOP 24) SIGTSTP
25) SIGCONT 26) SIGTTIN 27) SIGTTOU 28) SIGVTALRM
29) SIGPROF 30) SIGXCPU 31) SIGXFSZ 32) SIGWAITING
33) SIGLWP 34) SIGFREEZE 35) SIGTHAW 36) SIGCANCEL
37) SIGLOST 39) SIGRTMIN 40) SIGRTMIN+1 41) SIGRTMIN+2
42) SIGRTMIN+3 43) SIGRTMAX-3 44) SIGRTMAX-2 45) SIGRTMAX-1
46) SIGRTMAX
```

客户端

- 一个生产者对一个消费者：输入`./mq_normal_producer  ./mq_normal_consumer `启动客户端的生产者和消费者
- 一个生产者对多个消费者：工作队列（多个消费者同时处理，在多个工作人员之间分配耗时的任务）：输入`./multi_producer_start.sh 1 10`启动客户端，创建一个生产者，循环发送长度为10的消息
- 持久化消息：输入`./mq_durable_producer  ./mq_duravle_consumer `启动客户端的生产者和消费者

## 功能实现

- 服务器端是基于共享内存的队列，服务器端分为三部分，由接入层、逻辑层、持久化层构成

  接入层负责与客户端建立连接，接收客户端创建的消息包及回复相应的网络包

  逻辑层通过共享内存与接入层通信，主要负责消息处理与回复：接收接入层传入的订阅信息、出错信息、需要持久化信息等，将订阅的数据和需要回复的数据包回传给接入层

  持久化层接收逻辑层传入共享内存的信息，将需要持久化的数据按照类别顺序落地磁盘

- 服务器端服务端采用epoll I/O多路复用技术

  IO性能不会随着事件描述符的增大而线性减少，每轮处理数据，通过epoll_wait所有要处理的事件，如果是当前监听的套接字则添加到用户连接，接收数据

- 通过Compare & Set实现无锁操作，避免多线程竞争

  服务器端通过共享内存读写数据，为避免竞争，将队列结构中国设置使用人数，每次轮询使用人数如果是0，则设置为1，即获得了队列的使用权，反之则有其他线程在进行操作，继续等待。

- 通过服务器端的持久化层将数据顺序落地磁盘，实现消息持久化减少随机读写的寻址开销

  当接收到需要持久化的数据包时，按照包的类型保存，同时保存每次写入的offsett提供下一次持久化写位置文件

  当宕机重启时，逻辑层负责从本地读取持久化文件来恢复数据

- 服务器和客户端通过tcp连接

- 客户端有两种：消息生产者，消息消费者

  可以实现三种exchange模式的处理，fanout，在绑定当前交换机的所有队列中广播消息，direct，转发给key匹配队列，topic，和每个key进行字符串匹配，匹配上的转发消息

  



## 一、框架

### 整体框架

![](https://cdn.jsdelivr.net/gh/iamxpf/pageImage/images/20210602162238.png)



### 服务器：消息队列服务器

1. 接入层ConnectServer.cpp

   （1）通过socket和客户端建立tcp连接，见“Connection:网络连接”部分

   （2）和业务逻辑层通信

   m_pQueueToLogicLayer(12345) enqueue方法：当出错时传输出错信息

   m_pQueueLogicToConnect(54321) deqeue方法：接收订阅的数据

2. 业务逻辑层logicServer.cpp

   （1）和接入层通信

   m_pQueueFromConnect(12345) deqeue方法：处理共享内存的数据时接收业务逻辑层的数据
   m_pQueueToConnect(54321)  enqueue方法：发送订阅的消息，以及需要回复的消息

   （2）和持久化层通信

   m_pQueueToPersis(65432) enqueue方法：发送需要持久化的数据

3. 持久化层PersistenceServer.cpp

   和业务逻辑层通信：m_pQueueFromLogic(65432) deqeue方法将需要持久化数据出队并保存

通信通道：基于共享内存的队列

1. 初始化：创建信号量互斥锁，打开对应共享内存

   信号量互斥锁

   ```c++
   m_iSemId=semget(m_iSemKey,1,IPC_CREAT|0666);//创建一个新的信号量或获取一个已经存在的信号量的键值。 IPC_CREAT如果信号量不存在，则创建一个信号量，否则获取
   semun arg;
   semid_ds semDs;
   arg.buf=&semDs;
   int ret=semctl(m_iSemId,0,IPC_STAT,arg);//从关联于 semid 的内核数据结构复制数据到 arg.buf 指向的semid_ds 数据结构
   //未曾使用或者上次op操作超过3分钟则释放锁
   if(semDs.sem_otime==0||((semDs.sem_otime>0)&&(time(NULL)-semDs.sem_otime>3*60)))
   {
     semun arg;
     arg.val=1;
     ret=semctl(m_iSemId,0,SETVAL,arg);//值设置为1
   }
   ```

   基于共享内存的队列

   ```c++
   //创建或者打开共享内存
   int shmId=shmget(iShmKey,sizeof(QueueHead)+iQueueSize,IPC_CREAT|IPC_EXCL|0666);
   m_pMemAddr=(char *)shmat(shmId,NULL,0);//启动共享内存
   //初始化队列头
   if(isExist)//共享内存之前存在
   {
     m_pQueueHead=(QueueHead *)(m_pMemAddr);//队列头部重新指向
   }
   else
   {
     memset(m_pMemAddr,0,iQueueSize+sizeof(QueueHead));
     m_pQueueHead=(QueueHead *)(m_pMemAddr);
     m_pQueueHead->m_iBlockNum=0;
     m_pQueueHead->m_iLen=iQueueSize;
     m_pQueueHead->m_iHead=0;
     m_pQueueHead->m_iTail=0;
     m_pQueueHead->m_iUsedNum=0;
   }
   ```

2. 多线程下的入队和出队操作

   （1）原因：多线程之间使用队列是一定需要做到同步的，在一个线程读写的时候，阻塞另一个线程。设置过程时间很短暂，所以没有必要使用锁。使用原子操作：要不操作全部完成要么一点都不开始。这和数据库的事务不同，因为原子操作没有回滚。也就是说原子操作，一旦开始某个操作，那么就必须要完全的完成。

   （2）使用方法：**CAS原子操作**——Compare & Set，或是 Compare & Swap。

   核心思想：队列结构设置使用人数，每次轮询使用人数如果是0，则设置为1，即获得了队列的使用权，反之则有其他线程在进行操作，继续等待。类似自旋锁

   核心代码：

   ```c++
   //获取使用权
   while(true)
   {
     rmb();//rmb() 確保 barrier 之前的 read operation 都能在 barrier 之後的 read operation 之前發生，簡單來說就是確保 barrier 前後的 read operation 的順序
     if(!CAS32(&m_pQueueHead->m_iUsedNum,0,1)) ??
     {
       continue;
     }
     wmb();//wmb() 如同 rmb() 但是只針對 write operation
     break;
   }
   
   //...
   //操作队列
   //...
   
   //释放使用权
   while(true)
   {
     rmb();
     if(!CAS32(&m_pQueueHead->m_iUsedNum,1,0))
     {
       continue;
     }
     wmb();
     break;
   ```

   

### 客户端

包含消息生产者和消费者

#### 生产者

（1）和服务端建立连接

（2）消息格式 CreatePublishMessage

- 设置消息格式 4 = CMD_CREATE_PUBLISH
- 设置绑定的交换机名称：m_strExchangeName(istrExName)
- 绑定键名称：m_strRoutingKey(istrKey)
- 消息主体：m_strMsgBody(istrMsgBody)
- 优先级：m_iPriority(iPriority)
- 是否可持久化：m_bDurable(ibDurable)
- 消息序列号：m_iMsgSeq(-1)
- 确认级别：m_iConfirmLevel(iConfirmLevel)

#### 交换机

交换器从生产者那收到消息后，根据Routing Key、Exchange Type和Binding key联合使用，分发消息到queue中。

消息格式：CreateExchangeMessage

- 设置标志位为1 = CMD_CREATE_EXCNANGE

- 设置交换机名称：m_strExchangeName(istrName)

- 交换机类型：m_iExchangeType(iExchangeType)，三种类型可以选择EXCHANGE_TYPE_FANOUT=1（广播），EXCHANGE_TYPE_DIRECT=2（绑定键与该消息的路由键完全匹配的队列），EXCHANGE_TYPE_TOPIC=3（根据主题）;

- 是否持久化：m_bDurable(ibDurable)

- 是否自动删除m_bAutoDel(ibAutoDel)

  

#### 队列

消息最终被送到Queue中并等待consumer取走，一个message可以被同时拷贝到多个queue中。

消息格式：CreateQueueMessage

- 设置标志位2 = CMD_CREATE_QUEUE
- 设置队列名称 m_strQueueName(istrName)
- 设置优先级m_iPriority(iPriority)
- 是否持久化：m_bDurable(ibDurable)
- 是否自动删除m_bAutoDel(ibAutoDel)

#### 绑定

通过Binding将Exchange与Queue关联起来，在绑定（Binding）Exchange与Queue的同时，一般会指定一个binding key。可以将多个Queue绑定到同一个Exchange上，并且可以指定相同的Binding Key。

消息格式：CreateBindingMessage

- 设置标志位3 = CMD_CREATE_BINDING
- 绑定的交换机的名称：m_strExchangeName(istrExName)
- 绑定的队列的名称m_strQueueName(istrQueueName)
- 绑定键的名称：m_strBindingKey(istrKey)

1.Publisher:消息的生产者，向交换器Exchange发布消息，发送消息时还要指定Routing Key。

7.Consumer:消息的消费者，如果有多个消费者同时订阅同一个Queue中的消息，Queue中的消息会被平摊给多个消费者。

### Connection:网络连接

客户端通过tcp进行网络连接（Producer和Consumer都是通过TCP连接到Server的）

#### 服务器端

（1）初始化：int ConnectServer::Init()

套接字：socket->SetNonBlock->setsockopt

核心代码：

```c++
m_iListenFd=socket(AF_INET,SOCK_STREAM,0)；
SetNonBlock(m_iListenFd)；
int ret=setsockopt(m_iListenFd,SOL_SOCKET,SO_REUSEADDR,(char *)&opt,sizeof(opt));
struct sockaddr_in serverAddr;
memset(&serverAddr,0,sizeof(serverAddr));
serverAddr.sin_family=AF_INET;
serverAddr.sin_port=htons(SERVER_DEFAULT_PORT);
serverAddr.sin_addr.s_addr=htonl(INADDR_ANY);
bind(m_iListenFd,(struct sockaddr *)&serverAddr,sizeof(serverAddr);
listen(m_iListenFd,512);
```

I/O多路复用：epoll_create（LT模式）->epoll_ctl（EPOLLIN读事件）

核心代码：

```c++
m_iEpollFd=epoll_create(CLIENT_EPOLL_COUNT);
struct epoll_event event;
event.events=EPOLLIN;
```

（2）运行：int ConnectServer::Run()

每轮处理数据：epoll_wait（所有要处理的事件）->读事件（ipEvents[i].events&EPOLLIN）:如果是监听的套接字accept->SetNonBlock->添加到用户连接  ->recv（接收数据）：处理接收到的数据

核心代码：

```c++
int num=epoll_wait(m_iEpollFd,pEvents,CLIENT_EPOLL_COUNT,0);
for(int i=0;i<iNum;++i) {
  if(ipEvents[i].events&EPOLLIN) {
    //获取当前连接pClientConnect
    int sockfd=pClientConnect->m_iSockfd;
    if(sockfd==m_iListenFd)
    {
      struct sockaddr_in cliAddr;
      memset(&cliAddr,0,sizeof(cliAddr));
      socklen_t len=sizeof(cliAddr);
      int confd=accept(m_iListenFd,(struct sockaddr *)&cliAddr,&len);//接收监听端口的连接
      SetNonBlock(confd);
      int iSendBufSize=SOCK_SEND_BUFF_SIZE;
      int iRevBufSize=SOCK_RECV_BUFF_SIZE;
      setsockopt(confd,SOL_SOCKET,SO_SNDBUF,(char *)&iSendBufSize,sizeof(int));
      setsockopt(confd,SOL_SOCKET,SO_RCVBUF,(char *)&iRevBufSize,sizeof(int));
  }
  int ret=recv(sockfd,pClientConnect->m_pRecvTail,iRecvBytes,0);//准备接收数据
}
else {
  //收到其他事件，断开连接
}
```



#### 客户端

Producer和Consumer生产者和消费者

（1）初始化：int Client::BuildConnection(const char *ipSeverIp,unsigned short iServerPort)

套接字：socket->connect->SetNonBlock->setsockopt

核心代码：

```c++
m_iSockfd=socket(AF_INET,SOCK_STREAM,0);
sockaddr_in serverAddr;
memset(&serverAddr,0,sizeof(serverAddr));
serverAddr.sin_family=AF_INET;
serverAddr.sin_port=htons(iServerPort);
serverAddr.sin_addr.s_addr=inet_addr(ipSeverIp);
int ret=connect(m_iSockfd,(struct sockaddr *)&serverAddr,sizeof(serverAddr));
FuncTool::SetNonBlock(m_iSockfd);
int iSendBufSize=SOCK_SEND_BUFF_SIZE;
int iRevBufSize=SOCK_RECV_BUFF_SIZE;
setsockopt(m_iSockfd,SOL_SOCKET,SO_SNDBUF,(char *)&iSendBufSize,sizeof(int));
setsockopt(m_iSockfd,SOL_SOCKET,SO_RCVBUF,(char *)&iRevBufSize,sizeof(int));
```

（2）运行：

以创建交换机为例

**初始化**：初始化交换机消息头CreateExchangeMessage（oMsg），调用GetMessagePack，将要创建的内容写入消息包

**发送创建数据**：I/O多路复用：poll（监听写事件）

poll等待套接字可写后发送数据包

核心代码：

```c++
//等待套接字可写
struct pollfd fds[1];//定义pollfd结构的文件句柄
fds[0].fd=m_iSockfd;
fds[0].events=POLLOUT|POLLERR;//监听写事件
while(true)
{
  int ret=poll(fds,1,-1);
  if(fds[0].revents&POLLOUT)
  {
    break;
  }
}
int ret=send(iSockfd,pTemp,iLeft,0);//发送数据
```

**接收创建回复**：I/O多路复用：poll（监听读事件）

核心代码：

```
do
{
  struct pollfd fds[1];//定义pollfd结构的文件句柄
  fds[0].fd=m_iSockfd;
  fds[0].events=POLLIN|POLLERR;//监听读事件
  while(true)
  {
    int ret=poll(fds,1,-1);
    if(fds[0].revents&POLLIN)
    {
      break;
    }
  }
  ret = recv(m_iSockfd,m_pRecvTail,iFreeSpace,0);
} while (ret!=SUCCESS);
//读取消息，处理消息
```

（发送数据只有发送，监听写事件即可，不需要回复，仅记录确认号即可）

#### 连接

输入./mq_server.start.sh启动服务端

输入multi_producer_start.sh 1 10启动客户端，创建一个生产者，循环发送长度为10的消息

服务端日志

![](https://cdn.jsdelivr.net/gh/iamxpf/pageImage/images/20210524164800.png)







## 二、可以改进的方向

（1） mq 得支持可伸缩性，需要的时候快速扩容，就可以增加吞吐量和容量?

设计个分布式的系统呗，参照一下 kafka 的设计理念，broker -> topic -> partition，每个 partition 放一个机器，就存一部分数据。如果现在资源不够了，简单啊，给 topic 增加 partition，然后做数据迁移，增加机器，不就可以存放更多数据，提供更高的吞 吐量了?

（3） mq 的可用性啊?

具体参考之前可用性那个环节讲解的 kafka 的高可用保障机制。多副本 -> leader & follower -> broker 挂了重新选举 leader 即可对 外服务。

（4）能不能支持数据 0 丢失?

可以的，参考我们之前说的那个 kafka 数据零丢失方案。

## 三、遇到的问题

1、问题：tcp连接中socket端口号填的非本机，连接报错

tcp连接端口的可重用:设置SO_REUSEADDR

    int opt=1;
    int ret=setsockopt(m_iListenFd,SOL_SOCKET,SO_REUSEADDR,(char *)&opt,sizeof(opt));//设置套接字

2、poll调用问题：

当有生产者生产消息时，消费者才能读到（poll监听事件），反之poll会一直阻塞等待

所以需要先启动生产者，再启动消费者

## 四、整体流程

服务端类调用图

![](https://cdn.jsdelivr.net/gh/iamxpf/pageImage/images/20210514152704.png)

客户端（生产者和多个消费者）类调用图

![](https://cdn.jsdelivr.net/gh/iamxpf/pageImage/images/20210514152721.png)

