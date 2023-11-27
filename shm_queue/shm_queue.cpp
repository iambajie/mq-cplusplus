//c/c++
#include<stdio.h>
#include<string.h>
//linux
#include<sys/types.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<errno.h>
//user define
#include "shm_queue.h"

using namespace WSMQ;


ShmQueue::ShmQueue()
{
    memset(m_pErrMsg,0,sizeof(m_pErrMsg));
    m_pQueueHead=NULL;
    m_pSemLock=NULL;
    m_pMemAddr=NULL;
    m_iQueueSize=0;
}

ShmQueue::~ShmQueue()
{
    if(m_pSemLock)
    {
        delete m_pSemLock;
        m_pSemLock=NULL;
    }
}

int ShmQueue::Init(int iShmKey,int iQueueSize)
{
    m_iQueueSize=iQueueSize;
    //创建信号量互斥锁
    m_pSemLock=new SemLock();
    int ret=m_pSemLock->Init(iShmKey);
    if(ret!=SUCCESS)
    {
        snprintf(m_pErrMsg,sizeof(m_pErrMsg),"Create Semaphore failed! key is %d,errmsg is %s",iShmKey,strerror(errno));
        printf("%s",m_pErrMsg);
        return ERR_SHM_QUEUE_INIT_LOCK;
    }
    //创建或者打开共享内存
    int shmId=shmget(iShmKey,sizeof(QueueHead)+iQueueSize,IPC_CREAT|IPC_EXCL|0666);//IPC_CREAT   如果共享内存不存在，则创建一个共享内存，否则打开操作。
//PC_EXCL    只有在共享内存不存在的时候，新的共享内存才建立，否则就产生错误
    bool isExist=false;
    if(shmId==-1)//失败返回-1
    {
        if(errno==EEXIST)//不成功返回-1，errno储存错误原因  EEXIST 预建立key所致的共享内存，但已经存在
        {
            isExist=true;
            if((shmId=shmget(iShmKey,iQueueSize,0666))<0)
            {
                snprintf(m_pErrMsg,sizeof(m_pErrMsg),"Shm existed,but open failed! key is %d,errmsg is %s",iShmKey,strerror(errno));
                printf("%s",m_pErrMsg);
                return ERR_SHM_QUEUE_OPEN_SHM;
            }
            else
            {
                m_pMemAddr=(char *)shmat(shmId,NULL,0);//如果成功，返回共享存储段地址，出错返回-1
                if(m_pMemAddr==(void *)-1)
                {
                    snprintf(m_pErrMsg,sizeof(m_pErrMsg),"Shm open succeed,but shmat failed! key is %d,errmsg is %s",iShmKey,strerror(errno));
                    printf("%s",m_pErrMsg);
                    return ERR_SHM_QUEUE_AT_SHM;
                }
            }
        }
        else
        {
            snprintf(m_pErrMsg,sizeof(m_pErrMsg),"Shmget failed! key is %d,errmsg is %s",iShmKey,strerror(errno));
            printf("%s",m_pErrMsg);
            return ERR_SHM_QUEUE_INIT_SHM;
        }
    }
    else//成功返回共享存储的id
    {
        m_pMemAddr=(char *)shmat(shmId,NULL,0);
        if(m_pMemAddr==(void *)-1)
        {
            snprintf(m_pErrMsg,sizeof(m_pErrMsg),"Shm open succeed,but shmat failed! key is %d,errmsg is %s",iShmKey,strerror(errno));
            printf("%s",m_pErrMsg);
            return ERR_SHM_QUEUE_AT_SHM;
        }
    }

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
    m_pMemAddr+=sizeof(QueueHead);
    return SUCCESS;
}

int ShmQueue::Enqueue(const char *ipDate,int iDateLen)
{
    if(m_pQueueHead==NULL)
    {
        snprintf(m_pErrMsg,sizeof(m_pErrMsg),"Shm Queue uninitialized!");
        return ERROR;
    }

    char pTail[6];
    memset(pTail,TAIL_FLAG,sizeof(pTail));
    int ret=SUCCESS;
    //m_pSemLock->Lock();


// cas( int * pVal, int oldVal, int newVal );

// pVal 表示要比较和替换数值的地址，oldVal表示期望的值，newVal表示希望替换成的值。在多线程中使用时，一般是下面这样。

//     volitale int myValue;
//    ......

//     while( !cas32( &myValue, myValue, myValue+1 ) ) {
//         ....
//     }

// 这是什么意思呢？
// 我们知道，在多线程里面，如果多个线程同时在写一个变量，并且不进行同步的时候，这个变量的值就会不准确。比如两个线程同时在对一个变量进行+1操作的时候，他们分别需要进行三个操作，读入变量值（到寄存器），值+1，写入变量值（到变量内存地址）。这三个操作是无法保证原子性的，也就无法保证变量在读入，+1后，原变量没有被别的线程修改。
// cas在这里做的实际上就是在变量没有被其他线程修改，或者被修改但是又恢复到我们期望读入的值的时候，修改变量的值。这句话很拗口，
//其实看CAS的参数就很容易理解，pVal传递进去的是变量的地址，cas通过这个来读取变量真实的值，oldVal传递的是变量在函数调用时的值，用来跟真实值进行比较，而newVal传递进去的是期望变量赋予的新的值。
//根据上面的cas的代码含义我们知道，当变量真实值不等于调用时的值的时候，是不会赋予变量新的值的。所以我们使用了一个WHILE来等待这个赋值成立。同时我们给与myValue一个volitale修饰，用意是让while中调用cas函数时，读取myValue的当前值，而不是寄存器中保存的值，以免变量值的“过期”，从而让这个cas可以在没有其他线程来修改myValue的时候执行成功，从而实现lock free的修改myValue。

    //获取使用权 无锁队列？
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

    //计算剩余空间，看能否存放
    int freeSpaceSize=0;
    if((m_pQueueHead->m_iHead<m_pQueueHead->m_iTail)||(m_pQueueHead->m_iHead==m_pQueueHead->m_iTail&&m_pQueueHead->m_iBlockNum==0))
    {
        freeSpaceSize=m_pQueueHead->m_iLen-(m_pQueueHead->m_iTail-m_pQueueHead->m_iHead);
    }
    else if(m_pQueueHead->m_iHead>m_pQueueHead->m_iTail)
    {
        freeSpaceSize=m_pQueueHead->m_iHead-m_pQueueHead->m_iTail;
    }
    else
    {
        freeSpaceSize=0;
    }

    if(freeSpaceSize<sizeof(DateBlockHead)+iDateLen+sizeof(pTail))
    {
        snprintf(m_pErrMsg,sizeof(m_pErrMsg),"shm queue is full！");
        ret=ERR_SHM_QUEUE_FULL;
    }
    else
    {
        int saveIndex=m_pQueueHead->m_iTail;
        DateBlockHead blockHeader;//初始化数据块头部
        blockHeader.m_iIndex=m_pQueueHead->m_iTail;//当前块起始位置
        blockHeader.m_iDateLen=iDateLen;//数据长度
        int tailRightSpace=m_pQueueHead->m_iLen-m_pQueueHead->m_iTail;

        //若是尾数据块位置在首数据块位置之前，或者尾数据块之后剩余空间足够大，可以直接全部存放
        if(m_pQueueHead->m_iTail<m_pQueueHead->m_iHead||tailRightSpace>=int(iDateLen+sizeof(DateBlockHead)+sizeof(pTail)))
        {
            memcpy(m_pMemAddr+saveIndex,&blockHeader,sizeof(blockHeader));
            saveIndex+=sizeof(blockHeader);
            memcpy(m_pMemAddr+saveIndex,ipDate,iDateLen);
            saveIndex+=iDateLen;
            memcpy(m_pMemAddr+saveIndex,pTail,sizeof(pTail));
        }
        else
        {
            //保存数据头
            if(tailRightSpace>=(int)sizeof(DateBlockHead))
            {
                memcpy(m_pMemAddr+saveIndex,&blockHeader,sizeof(blockHeader));
                saveIndex=(saveIndex+sizeof(blockHeader))%m_pQueueHead->m_iLen;//取余
                tailRightSpace-=sizeof(blockHeader);
            }
            else
            {
                memcpy(m_pMemAddr+saveIndex,&blockHeader,tailRightSpace);
                saveIndex=0;
                memcpy(m_pMemAddr+saveIndex,((char *)&blockHeader)+tailRightSpace,sizeof(DateBlockHead)-tailRightSpace);
                saveIndex=(saveIndex+sizeof(DateBlockHead)-tailRightSpace)%m_pQueueHead->m_iLen;
                tailRightSpace=m_pQueueHead->m_iHead-saveIndex;
            }

            //保存数据体
            if(tailRightSpace>=iDateLen)
            {
                memcpy(m_pMemAddr+saveIndex,ipDate,iDateLen);
                saveIndex=(saveIndex+iDateLen)%m_pQueueHead->m_iLen;
                tailRightSpace-=iDateLen;
            }
            else
            {
                memcpy(m_pMemAddr+saveIndex,ipDate,tailRightSpace);
                saveIndex=0;
                memcpy(m_pMemAddr+saveIndex,ipDate+tailRightSpace,iDateLen-tailRightSpace);
                saveIndex=(saveIndex+iDateLen-tailRightSpace)%m_pQueueHead->m_iLen;
                tailRightSpace=m_pQueueHead->m_iHead-saveIndex;
            }

            //保存尾部标志
            if(tailRightSpace>=(int)sizeof(pTail))
            {
                memcpy(m_pMemAddr+saveIndex,pTail,sizeof(pTail));
            }
            else
            {
                memcpy(m_pMemAddr+saveIndex,pTail,tailRightSpace);
                saveIndex=0;
                memcpy(m_pMemAddr+saveIndex,pTail+tailRightSpace,sizeof(pTail)-tailRightSpace);
            }
        }

        //更新尾部位置及数据块个数
        m_pQueueHead->m_iTail=(m_pQueueHead->m_iTail+iDateLen+sizeof(DateBlockHead)+sizeof(pTail))%m_pQueueHead->m_iLen;
        m_pQueueHead->m_iBlockNum++;
    }
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
    }
    //m_pSemLock->UnLock();
    return ret;
}

int ShmQueue::Dequeue(char *opBuf,int *iopBufLen)
{
    if(opBuf==NULL||iopBufLen==NULL)
    {
        snprintf(m_pErrMsg,sizeof(m_pErrMsg),"invaild input data!");
        return ERROR;
    }
    if(m_pQueueHead==NULL)
    {
        snprintf(m_pErrMsg,sizeof(m_pErrMsg),"Shm Queue uninitialized!");
        return ERROR;
    }
    int ret=SUCCESS;
    //m_pSemLock->Lock();
    //获取使用权
    while(true)
    {
        rmb();
        if(!CAS32(&m_pQueueHead->m_iUsedNum,0,1))
        {
            continue;
        }
        wmb();
        break;
    }
    if(m_pQueueHead->m_iBlockNum>0||m_pQueueHead->m_iHead!=m_pQueueHead->m_iTail)
    {
        int readIndex=m_pQueueHead->m_iHead;
        int headRightSpace=m_pQueueHead->m_iLen-m_pQueueHead->m_iHead;
        //读取数据块头
        DateBlockHead blockHeader;
        memset(&blockHeader,0,sizeof(blockHeader));
        if(headRightSpace>=sizeof(blockHeader))
        {
            memcpy(&blockHeader,m_pMemAddr+readIndex,sizeof(blockHeader));
            readIndex=(readIndex+sizeof(blockHeader))%m_pQueueHead->m_iLen;
        }
        else
        {
            memcpy(&blockHeader,m_pMemAddr+readIndex,headRightSpace);
            readIndex=0;
            memcpy(((char *)&blockHeader)+headRightSpace,m_pMemAddr+readIndex,sizeof(blockHeader)-headRightSpace);
            readIndex=(readIndex+sizeof(blockHeader)-headRightSpace)%m_pQueueHead->m_iLen;
        }
        
        //缓冲区长度不够则返回
        if(*iopBufLen<blockHeader.m_iDateLen)
        {
            *iopBufLen=blockHeader.m_iDateLen;
            m_pSemLock->UnLock();//解锁
            return ERR_SHM_QUEUE_BUF_SMALL;
        }

        //若数据块位置不是队列首位置，则查找下一个数据块位置或者置空队列
        if(blockHeader.m_iIndex!=m_pQueueHead->m_iHead)
        {
            if(m_pQueueHead->m_iBlockNum>0)
            {
                int tailFlagCount=0;
                while(true)
                {
                    if(m_pMemAddr[readIndex]==TAIL_FLAG)
                    {
                        ++tailFlagCount;
                    }
                    else
                    {
                        tailFlagCount=0;
                    }
                    readIndex=(readIndex+1)%m_pQueueHead->m_iLen;
                    if(tailFlagCount==6)
                    {
                        m_pQueueHead->m_iHead=readIndex;
                        m_pQueueHead->m_iBlockNum--;
                        break;
                    }
                }
                snprintf(m_pErrMsg,sizeof(m_pErrMsg),"Shm queue read error,head has been reseted!");
                ret=ERR_SHM_QUEUE_DATE_RESET;
            }
            else
            {
                m_pQueueHead->m_iHead=0;
                m_pQueueHead->m_iTail=0;
                m_pQueueHead->m_iBlockNum=0;
                m_pQueueHead->m_iLen=m_iQueueSize;
                snprintf(m_pErrMsg,sizeof(m_pErrMsg),"Shm Queue empty!");
                ret=ERR_SHM_QUEUE_EMPTY;
            }
        }
        else
        {
            //读取长度不对直接置空队列
            if(blockHeader.m_iDateLen<0)
            {
                m_pQueueHead->m_iHead=0;
                m_pQueueHead->m_iTail=0;
                m_pQueueHead->m_iBlockNum=0;
                m_pQueueHead->m_iLen=m_iQueueSize;
                snprintf(m_pErrMsg,sizeof(m_pErrMsg),"Shm Queue empty!");
                ret=ERR_SHM_QUEUE_EMPTY;
            }
            else
            {
                *iopBufLen=blockHeader.m_iDateLen;

                //读取数据部分
                headRightSpace=m_pQueueHead->m_iLen-readIndex;
                if(headRightSpace>=blockHeader.m_iDateLen)
                {
                    memcpy(opBuf,m_pMemAddr+readIndex,blockHeader.m_iDateLen);
                    readIndex=(readIndex+blockHeader.m_iDateLen)%m_pQueueHead->m_iLen;
                    headRightSpace-=blockHeader.m_iDateLen;
                }
                else
                {
                    memcpy(opBuf,m_pMemAddr+readIndex,headRightSpace);
                    readIndex=0;
                    memcpy(opBuf+headRightSpace,m_pMemAddr+readIndex,blockHeader.m_iDateLen-headRightSpace);
                    readIndex=(readIndex+blockHeader.m_iDateLen-headRightSpace)%m_pQueueHead->m_iLen;
                    headRightSpace=m_pQueueHead->m_iLen-readIndex;
                }

                //读取尾部部分
                char pTail[6];
                memset(pTail,TAIL_FLAG,sizeof(pTail));
                char pReadDate[6];
                memset(pReadDate,0,sizeof(pReadDate));
                if(headRightSpace>=(int)sizeof(pTail))
                {
                     memcpy(pReadDate,m_pMemAddr+readIndex,sizeof(pReadDate));
                }
                else
                {
                    memcpy(pReadDate,m_pMemAddr+readIndex,headRightSpace);
                    readIndex=0;
                    memcpy(pReadDate+headRightSpace,m_pMemAddr+readIndex,sizeof(pReadDate)-headRightSpace);
                }

                //更新队列头及数据数目
                m_pQueueHead->m_iHead=(m_pQueueHead->m_iHead+sizeof(blockHeader)+blockHeader.m_iDateLen+sizeof(pTail))%m_pQueueHead->m_iLen;
                m_pQueueHead->m_iBlockNum--;

                //比较尾部标志是否正确
                if(memcmp(pTail,pReadDate,sizeof(pTail))!=0)
                {
                    snprintf(m_pErrMsg,sizeof(m_pErrMsg),"Get data tail error! FLAG is %s,get tail is %s",pTail,pReadDate);
                    ret=ERR_SHM_QUEUE_DATE_TAIL;
                }
            }
            
        } 
    }
    else
    {
        snprintf(m_pErrMsg,sizeof(m_pErrMsg),"Shm Queue empty!");
        ret=ERR_SHM_QUEUE_EMPTY;
    }
    //m_pSemLock->UnLock();
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
    }
    return ret;
}

int ShmQueue::GetDateBlockNum()
{
    if(m_pSemLock==NULL)
    {
        snprintf(m_pErrMsg,sizeof(m_pErrMsg),"Shm Queue uninitialized!");
        return ERROR;
    }
    int num=0;
    m_pSemLock->Lock();
    num=m_pQueueHead->m_iBlockNum;
    m_pSemLock->UnLock();
    return num;
}