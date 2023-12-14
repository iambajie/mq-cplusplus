// c/c++
#include <stdio.h>
#include <string.h>
// linux
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
// user define
#include "shm_queue.h"

using namespace WSMQ;

// 共享内存队列
ShmQueue::ShmQueue()
{
    // 错误消息
    memset(m_pErrMsg, 0, sizeof(m_pErrMsg));
    // 队列头部
    m_pQueueHead = NULL;
    // 信号量互斥锁
    m_pSemLock = NULL;
    // 共享内存地址
    m_pMemAddr = NULL;
    // 队列大小
    m_iQueueSize = 0;
}

ShmQueue::~ShmQueue()
{
    // 删除信号量互斥锁
    if (m_pSemLock)
    {
        delete m_pSemLock;
        m_pSemLock = NULL;
    }
}

int ShmQueue::Init(int iShmKey, int iQueueSize)
{
    m_iQueueSize = iQueueSize;
    // 创建信号量互斥锁, 用于进程间同步和互斥
    m_pSemLock = new SemLock();
    int ret = m_pSemLock->Init(iShmKey);
    if (ret != SUCCESS)
    {
        snprintf(m_pErrMsg, sizeof(m_pErrMsg), "Create Semaphore failed! key is %d,errmsg is %s", iShmKey, strerror(errno));
        printf("%s", m_pErrMsg);
        return ERR_SHM_QUEUE_INIT_LOCK;
    }
    // 创建或者打开共享内存
    // 根据键值 iShmKey 创建或获取一块大小为 sizeof(QueueHead) + iQueueSize 的共享内存。如果这块共享内存已经存在，则 shmget() 调用失败；否则，会创建一块新的共享内存，并返回其标识符 shmId。这块共享内存的权限被设置为 0666，即所有用户都可以读写。
    int shmId = shmget(iShmKey, sizeof(QueueHead) + iQueueSize, IPC_CREAT | IPC_EXCL | 0666); // IPC_CREAT   如果共享内存不存在，则创建一个共享内存，否则打开操作。
    // PC_EXCL    只有在共享内存不存在的时候，新的共享内存才建立，否则就产生错误
    bool isExist = false;
    if (shmId == -1) // 失败返回-1
    {
        // 多线程或者多进程已经创建了共享内存
        if (errno == EEXIST) // 不成功返回-1，errno储存错误原因  EEXIST 预建立key所致的共享内存，但已经存在
        {
            isExist = true;
            if ((shmId = shmget(iShmKey, iQueueSize, 0666)) < 0)
            {
                snprintf(m_pErrMsg, sizeof(m_pErrMsg), "Shm existed,but open failed! key is %d,errmsg is %s", iShmKey, strerror(errno));
                printf("%s", m_pErrMsg);
                return ERR_SHM_QUEUE_OPEN_SHM;
            }
            else
            {
                // 将获取的共享内存附加到当前进程
                m_pMemAddr = (char *)shmat(shmId, NULL, 0); // 如果成功，返回共享存储段地址，出错返回-1
                if (m_pMemAddr == (void *)-1)
                {
                    snprintf(m_pErrMsg, sizeof(m_pErrMsg), "Shm open succeed,but shmat failed! key is %d,errmsg is %s", iShmKey, strerror(errno));
                    printf("%s", m_pErrMsg);
                    return ERR_SHM_QUEUE_AT_SHM;
                }
            }
        }
        else
        {
            snprintf(m_pErrMsg, sizeof(m_pErrMsg), "Shmget failed! key is %d,errmsg is %s", iShmKey, strerror(errno));
            printf("%s", m_pErrMsg);
            return ERR_SHM_QUEUE_INIT_SHM;
        }
    }
    else // 成功返回共享存储的id
    {
        m_pMemAddr = (char *)shmat(shmId, NULL, 0);
        if (m_pMemAddr == (void *)-1)
        {
            snprintf(m_pErrMsg, sizeof(m_pErrMsg), "Shm open succeed,but shmat failed! key is %d,errmsg is %s", iShmKey, strerror(errno));
            printf("%s", m_pErrMsg);
            return ERR_SHM_QUEUE_AT_SHM;
        }
    }

    // 初始化队列头
    if (isExist) // 共享内存之前存在
    {
        m_pQueueHead = (QueueHead *)(m_pMemAddr); // 队列头部重新指向
    }
    else
    {
        memset(m_pMemAddr, 0, iQueueSize + sizeof(QueueHead));
        m_pQueueHead = (QueueHead *)(m_pMemAddr);
        m_pQueueHead->m_iBlockNum = 0;
        m_pQueueHead->m_iLen = iQueueSize;
        m_pQueueHead->m_iHead = 0;
        m_pQueueHead->m_iTail = 0;
        m_pQueueHead->m_iUsedNum = 0;
    }
    m_pMemAddr += sizeof(QueueHead);
    return SUCCESS;
}

/*
1.初始化和错误检查：首先检查共享内存队列是否已经初始化，如果没有，则返回错误。
2.获得锁：使用 CAS (Compare and Swap) 实现自旋锁，保证在多线程环境下操作的原子性。
3.计算空闲空间：根据头尾指针的位置，计算出队列中的剩余空间。
4.检查空间是否足够：检查队列中是否有足够的空间保存新的数据块（包括数据头、实际数据和尾部标志）。如果空间不足，则返回错误。
5.将数据加入队列：根据剩余空间的情况，优化地将数据头、实际数据和尾部标志写入队列。注意，这里处理了队列可能的环形特性，即当队列末尾空间不足时，会从队列开始处继续写入数据。
6.更新队列头信息：插入新数据后，更新队列的尾部位置和数据块数量。
7.释放锁：完成数据插入并更新队列信息后，释放锁，允许其他线程操作队列。
最后，函数返回 ret，这个变量在函数执行过程中可能被修改，用于表示函数执行的结果，比如成功或者因为某种原因失败。
*/
int ShmQueue::Enqueue(const char *ipDate, int iDateLen)
{
    if (m_pQueueHead == NULL)
    {
        snprintf(m_pErrMsg, sizeof(m_pErrMsg), "Shm Queue uninitialized!");
        return ERROR;
    }

    // 尾部标志写入6次
    char pTail[6];
    // 数据块尾部标志
    memset(pTail, TAIL_FLAG, sizeof(pTail));
    int ret = SUCCESS;
    // m_pSemLock->Lock();

    // cas( int * pVal, int oldVal, int newVal );

    // pVal 表示要比较和替换数值的地址，oldVal表示期望的值，newVal表示希望替换成的值。在多线程中使用时，一般是下面这样。

    //     volitale int myValue;
    //    ......

    //     while( !cas32( &myValue, myValue, myValue+1 ) ) {
    //         ....
    //     }

    // 自旋锁
    // 尝试获得一个“锁”，直到成功。如果其他线程已经获得了这个“锁”，那么当前线程就会等待，直到锁被释放
    while (true)
    {
        rmb(); // rmb() 確保 barrier 之前的 read operation 都能在 barrier 之後的 read operation 之前發生，簡單來說就是確保 barrier 前後的 read operation 的順序
        // 如果当前值已经为 1（可能另一个线程已经获取了锁），那么这个 CAS 操作将失败，返回 false
        if (!CAS32(&m_pQueueHead->m_iUsedNum, 0, 1))
        {
            continue;
        }
        wmb(); // wmb() 如同 rmb() 但是只針對 write operation
        break;
    }

    // 计算剩余空间，看能否存放
    int freeSpaceSize = 0;
    // 队列还有空闲空间，或者队列完全为空
    if ((m_pQueueHead->m_iHead < m_pQueueHead->m_iTail) || (m_pQueueHead->m_iHead == m_pQueueHead->m_iTail && m_pQueueHead->m_iBlockNum == 0))
    {
        freeSpaceSize = m_pQueueHead->m_iLen - (m_pQueueHead->m_iTail - m_pQueueHead->m_iHead);
    }
    // 头指针和尾指针之间的剩余空间大小
    else if (m_pQueueHead->m_iHead > m_pQueueHead->m_iTail)
    {
        freeSpaceSize = m_pQueueHead->m_iHead - m_pQueueHead->m_iTail;
    }
    else
    {
        freeSpaceSize = 0;
    }

    // 检查队列中是否有足够的空间来存储待插入的数据
    if (freeSpaceSize < sizeof(DateBlockHead) + iDateLen + sizeof(pTail))
    {
        snprintf(m_pErrMsg, sizeof(m_pErrMsg), "shm queue is full！");
        ret = ERR_SHM_QUEUE_FULL;
    }
    else
    {
        int saveIndex = m_pQueueHead->m_iTail;
        DateBlockHead blockHeader;                    // 初始化数据块头部
        blockHeader.m_iIndex = m_pQueueHead->m_iTail; // 当前块起始位置
        blockHeader.m_iDateLen = iDateLen;            // 数据长度
        // 环形队列尾指针（m_iTail）到队列末端的空间大小
        int tailRightSpace = m_pQueueHead->m_iLen - m_pQueueHead->m_iTail;

        // 若是尾数据块位置在首数据块位置之前，或者尾数据块之后剩余空间足够大，可以直接全部存放
        if (m_pQueueHead->m_iTail < m_pQueueHead->m_iHead || tailRightSpace >= int(iDateLen + sizeof(DateBlockHead) + sizeof(pTail)))
        {
            memcpy(m_pMemAddr + saveIndex, &blockHeader, sizeof(blockHeader));
            saveIndex += sizeof(blockHeader);
            memcpy(m_pMemAddr + saveIndex, ipDate, iDateLen);
            saveIndex += iDateLen;
            memcpy(m_pMemAddr + saveIndex, pTail, sizeof(pTail));
        }
        else
        {
            // 保存数据头
            if (tailRightSpace >= (int)sizeof(DateBlockHead))
            {
                memcpy(m_pMemAddr + saveIndex, &blockHeader, sizeof(blockHeader));
                // 使用取模运算符确保索引不超过队列长度
                saveIndex = (saveIndex + sizeof(blockHeader)) % m_pQueueHead->m_iLen; // 取余
                tailRightSpace -= sizeof(blockHeader);
            }
            else
            {
                // 不足够存放数据头
                // 数据块需要被分成两部分存储
                // 一部分在队列尾部，另一部分在队列头部。首先，它将数据块的一部分复制到队列的尾部，然后将 saveIndex 设置为 0，开始在队列头部存储剩余的数据
                memcpy(m_pMemAddr + saveIndex, &blockHeader, tailRightSpace);
                saveIndex = 0;
                memcpy(m_pMemAddr + saveIndex, ((char *)&blockHeader) + tailRightSpace, sizeof(DateBlockHead) - tailRightSpace);
                saveIndex = (saveIndex + sizeof(DateBlockHead) - tailRightSpace) % m_pQueueHead->m_iLen;
                tailRightSpace = m_pQueueHead->m_iHead - saveIndex;
            }

            // 保存数据体
            if (tailRightSpace >= iDateLen)
            {
                memcpy(m_pMemAddr + saveIndex, ipDate, iDateLen);
                saveIndex = (saveIndex + iDateLen) % m_pQueueHead->m_iLen;
                tailRightSpace -= iDateLen;
            }
            else
            {
                memcpy(m_pMemAddr + saveIndex, ipDate, tailRightSpace);
                saveIndex = 0;
                memcpy(m_pMemAddr + saveIndex, ipDate + tailRightSpace, iDateLen - tailRightSpace);
                saveIndex = (saveIndex + iDateLen - tailRightSpace) % m_pQueueHead->m_iLen;
                tailRightSpace = m_pQueueHead->m_iHead - saveIndex;
            }

            // 保存尾部标志
            if (tailRightSpace >= (int)sizeof(pTail))
            {
                memcpy(m_pMemAddr + saveIndex, pTail, sizeof(pTail));
            }
            else
            {
                memcpy(m_pMemAddr + saveIndex, pTail, tailRightSpace);
                saveIndex = 0;
                memcpy(m_pMemAddr + saveIndex, pTail + tailRightSpace, sizeof(pTail) - tailRightSpace);
            }
        }

        // 更新尾部位置及数据块个数
        m_pQueueHead->m_iTail = (m_pQueueHead->m_iTail + iDateLen + sizeof(DateBlockHead) + sizeof(pTail)) % m_pQueueHead->m_iLen;
        m_pQueueHead->m_iBlockNum++;
    }
    // 释放使用权
    while (true)
    {
        rmb();
        if (!CAS32(&m_pQueueHead->m_iUsedNum, 1, 0))
        {
            continue;
        }
        wmb();
        break;
    }
    // m_pSemLock->UnLock();
    return ret;
}

int ShmQueue::Dequeue(char *opBuf, int *iopBufLen)
{
    if (opBuf == NULL || iopBufLen == NULL)
    {
        snprintf(m_pErrMsg, sizeof(m_pErrMsg), "invaild input data!");
        return ERROR;
    }
    if (m_pQueueHead == NULL)
    {
        snprintf(m_pErrMsg, sizeof(m_pErrMsg), "Shm Queue uninitialized!");
        return ERROR;
    }
    int ret = SUCCESS;
    // m_pSemLock->Lock();
    // 获取使用权
    while (true)
    {
        rmb();
        if (!CAS32(&m_pQueueHead->m_iUsedNum, 0, 1))
        {
            continue;
        }
        wmb();
        break;
    }
    // 如果队列中有数据块，或者头尾指针不相等（说明有未完全读取的数据块），则开始读取操作
    if (m_pQueueHead->m_iBlockNum > 0 || m_pQueueHead->m_iHead != m_pQueueHead->m_iTail)
    {
        int readIndex = m_pQueueHead->m_iHead;
        // 从 readIndex 到队列头的剩余空间
        int headRightSpace = m_pQueueHead->m_iLen - m_pQueueHead->m_iHead;
        // 读取数据块头
        DateBlockHead blockHeader;
        memset(&blockHeader, 0, sizeof(blockHeader));
        // 剩余空间大于数据块大小
        if (headRightSpace >= sizeof(blockHeader))
        {
            memcpy(&blockHeader, m_pMemAddr + readIndex, sizeof(blockHeader));
            readIndex = (readIndex + sizeof(blockHeader)) % m_pQueueHead->m_iLen;
        }
        else
        {
            // 数据块被分成了两部分
            // 一部分在队列尾部，另一部分回绕到了队列头部。首先，它将队列尾部可用的数据拷贝到 blockHeader，然后将 readIndex 设为 0，开始从队列头部读取剩余的数据
            memcpy(&blockHeader, m_pMemAddr + readIndex, headRightSpace);
            readIndex = 0;
            memcpy(((char *)&blockHeader) + headRightSpace, m_pMemAddr + readIndex, sizeof(blockHeader) - headRightSpace);
            readIndex = (readIndex + sizeof(blockHeader) - headRightSpace) % m_pQueueHead->m_iLen;
        }

        // 缓冲区长度不够则返回
        if (*iopBufLen < blockHeader.m_iDateLen)
        {
            *iopBufLen = blockHeader.m_iDateLen;
            // m_pSemLock->UnLock(); // 解锁
            while (true)
            {
                rmb();
                if (!CAS32(&m_pQueueHead->m_iUsedNum, 1, 0))
                {
                    continue;
                }
                wmb();
                break;
            }
            return ERR_SHM_QUEUE_BUF_SMALL;
        }

        // 处理数据一致性和错误恢复
        // 若数据块位置不是队列首位置，则查找下一个数据块位置或者置空队列
        if (blockHeader.m_iIndex != m_pQueueHead->m_iHead)
        {
            // 队列中还有数据块
            if (m_pQueueHead->m_iBlockNum > 0)
            {
                int tailFlagCount = 0;
                while (true)
                {
                    if (m_pMemAddr[readIndex] == TAIL_FLAG)
                    {
                        ++tailFlagCount;
                    }
                    else
                    {
                        tailFlagCount = 0;
                    }
                    readIndex = (readIndex + 1) % m_pQueueHead->m_iLen;
                    // 代码：char pTail[6]; memset(pTail, TAIL_FLAG, sizeof(pTail));
                    // 找到下一个有效数据块位置
                    if (tailFlagCount == 6)
                    {
                        m_pQueueHead->m_iHead = readIndex;
                        m_pQueueHead->m_iBlockNum--;
                        break;
                    }
                }
                snprintf(m_pErrMsg, sizeof(m_pErrMsg), "Shm queue read error,head has been reseted!");
                ret = ERR_SHM_QUEUE_DATE_RESET;
            }
            else
            {
                // 队列为空
                m_pQueueHead->m_iHead = 0;
                m_pQueueHead->m_iTail = 0;
                m_pQueueHead->m_iBlockNum = 0;
                // 恢复到原始大小
                m_pQueueHead->m_iLen = m_iQueueSize;
                snprintf(m_pErrMsg, sizeof(m_pErrMsg), "Shm Queue empty!");
                ret = ERR_SHM_QUEUE_EMPTY;
            }
        }
        else
        {
            // 读取长度不对直接置空队列
            if (blockHeader.m_iDateLen < 0)
            {
                m_pQueueHead->m_iHead = 0;
                m_pQueueHead->m_iTail = 0;
                m_pQueueHead->m_iBlockNum = 0;
                m_pQueueHead->m_iLen = m_iQueueSize;
                snprintf(m_pErrMsg, sizeof(m_pErrMsg), "Shm Queue empty!");
                ret = ERR_SHM_QUEUE_EMPTY;
            }
            else
            {
                *iopBufLen = blockHeader.m_iDateLen;

                // 读取数据部分
                headRightSpace = m_pQueueHead->m_iLen - readIndex;
                if (headRightSpace >= blockHeader.m_iDateLen)
                {
                    memcpy(opBuf, m_pMemAddr + readIndex, blockHeader.m_iDateLen);
                    readIndex = (readIndex + blockHeader.m_iDateLen) % m_pQueueHead->m_iLen;
                    headRightSpace -= blockHeader.m_iDateLen;
                }
                else
                {
                    // 数据在队列中是环绕的，需要分两次进行复制：
                    // 第一次复制 headRightSpace 大小的数据，然后将 readIndex 设为0；
                    // 第二次复制剩余的数据，并更新 readIndex 和 headRightSpace
                    memcpy(opBuf, m_pMemAddr + readIndex, headRightSpace);
                    readIndex = 0;
                    memcpy(opBuf + headRightSpace, m_pMemAddr + readIndex, blockHeader.m_iDateLen - headRightSpace);
                    readIndex = (readIndex + blockHeader.m_iDateLen - headRightSpace) % m_pQueueHead->m_iLen;
                    headRightSpace = m_pQueueHead->m_iLen - readIndex;
                }

                // 读取尾部部分
                char pTail[6];
                memset(pTail, TAIL_FLAG, sizeof(pTail));
                char pReadDate[6];
                memset(pReadDate, 0, sizeof(pReadDate));
                if (headRightSpace >= (int)sizeof(pTail))
                {
                    memcpy(pReadDate, m_pMemAddr + readIndex, sizeof(pReadDate));
                }
                else
                {
                    memcpy(pReadDate, m_pMemAddr + readIndex, headRightSpace);
                    readIndex = 0;
                    memcpy(pReadDate + headRightSpace, m_pMemAddr + readIndex, sizeof(pReadDate) - headRightSpace);
                }

                // 更新队列头及数据数目
                m_pQueueHead->m_iHead = (m_pQueueHead->m_iHead + sizeof(blockHeader) + blockHeader.m_iDateLen + sizeof(pTail)) % m_pQueueHead->m_iLen;
                m_pQueueHead->m_iBlockNum--;

                // 比较尾部标志是否正确
                if (memcmp(pTail, pReadDate, sizeof(pTail)) != 0)
                {
                    snprintf(m_pErrMsg, sizeof(m_pErrMsg), "Get data tail error! FLAG is %s,get tail is %s", pTail, pReadDate);
                    ret = ERR_SHM_QUEUE_DATE_TAIL;
                }
            }
        }
    }
    else
    {
        snprintf(m_pErrMsg, sizeof(m_pErrMsg), "Shm Queue empty!");
        ret = ERR_SHM_QUEUE_EMPTY;
    }
    // m_pSemLock->UnLock();
    // 释放使用权
    while (true)
    {
        rmb();
        if (!CAS32(&m_pQueueHead->m_iUsedNum, 1, 0))
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
    if (m_pSemLock == NULL)
    {
        snprintf(m_pErrMsg, sizeof(m_pErrMsg), "Shm Queue uninitialized!");
        return ERROR;
    }
    int num = 0;
    m_pSemLock->Lock();
    num = m_pQueueHead->m_iBlockNum;
    m_pSemLock->UnLock();
    return num;
}