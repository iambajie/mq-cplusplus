// c/c++
#include <string.h>
#include <stdio.h>
// linux
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <errno.h>
#include <time.h>
// user define
#include "sem_lock.h"

using namespace WSMQ;

union semun
{
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

// 清除错误消息并将信号量键和ID设置为0
SemLock::SemLock()
{
    memset(m_pErrMsg, 0, sizeof(m_pErrMsg));
    m_iSemKey = 0;
    // 信号量初始化的时候是0
    m_iSemId = 0;
}

SemLock::~SemLock()
{
}

int SemLock::Init(int iSemKey)
{
    m_iSemKey = iSemKey;
    // 创建了一个权限为666的信号量
    m_iSemId = semget(m_iSemKey, 1, IPC_CREAT | 0666); // 创建一个新的信号量或获取一个已经存在的信号量的键值。 IPC_CREAT如果信号量不存在，则创建一个信号量，否则获取
    if (m_iSemId == -1)
    {
        snprintf(m_pErrMsg, sizeof(m_pErrMsg), "Create semaphore failed! key is %d,errmsg is %s", m_iSemKey, strerror(errno));
        return ERR_SEM_LOCK_INIT;
    }
    semun arg;
    semid_ds semDs;
    arg.buf = &semDs;
    // IPC_STAT读取一个信号量集的数据结构semid_ds，并将其存储在semun中的buf参数中
    int ret = semctl(m_iSemId, 0, IPC_STAT, arg);
    // 从关联于 semid 的内核数据结构复制数据到 arg.buf 指向的semid_ds 数据结构
    if (ret == -1)
    {
        snprintf(m_pErrMsg, sizeof(m_pErrMsg), "Get semaphore state failed! key is %d,semaphore index is %d,errmsg is %s", m_iSemKey, 0, strerror(errno));
        return ERR_SEM_LOCK_INIT;
    }
    // 未曾使用或者上次op操作超过3分钟则释放锁
    if (semDs.sem_otime == 0 || ((semDs.sem_otime > 0) && (time(NULL) - semDs.sem_otime > 3 * 60)))
    {
        semun arg;
        arg.val = 1;
        // 信号量用作互斥锁。当它为1时，标志着临界区域（即需要被保护以防止多个线程同时访问的代码片段）是空闲的；当它为0时，表示临界区域正在被某个线程占用。
        ret = semctl(m_iSemId, 0, SETVAL, arg); // 值设置为1
        if (ret == -1)
        {
            snprintf(m_pErrMsg, sizeof(m_pErrMsg), "Set semaphore val faild! key is %d,semaphore index is %d,errmsg is %s", m_iSemKey, 0, strerror(errno));
            return ERR_SEM_LOCK_INIT;
        }
    }
    return SUCCESS;
}
// 结构体含义
//  struct sembuf
//  {
//    unsigned short int sem_num;   /* 信号量的序号从0~nsems-1 */
//    short int sem_op;            /* 对信号量的操作，>0, 0, <0 */
//    short int sem_flg;            /* 操作标识：0， IPC_WAIT, SEM_UNDO */
//  };
int SemLock::Lock()
{
    if (m_iSemId == 0)
    {
        snprintf(m_pErrMsg, sizeof(m_pErrMsg), "Semaphore uninitialized!");
        return ERR_SEM_LOCK_LOCK;
    }
    struct sembuf arg;
    arg.sem_num = 0;
    arg.sem_op = -1;        // sem_op < 0，对该信号量执行等待操作
    arg.sem_flg = SEM_UNDO; // IPC_WAIT，使对信号量的操作时非阻塞的   SEM_UNDO，系统会跟踪此信号量上的更改，并在进程结束时自动撤销这些更改。
    while (1)
    {
        // 尝试获取（锁定）一个信号量。如果信号量当前不可用（即值为0），进程应该等待而不是立即返回
        int ret = semop(m_iSemId, &arg, 1); // 信号来操作处理的函数  用户改变信号量的值。也就是使用资源还是释放资源使用权
        // 如果sem_op的值为负数，而其绝对值又大于信号的现值，操作将会阻塞，直到信号值大于或等于sem_op的绝对值
        if (ret == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            snprintf(m_pErrMsg, sizeof(m_pErrMsg), "Acquire semaphore faild! key is %d,semaphore index is %d,errmsg is %s", m_iSemKey, 0, strerror(errno));
            return ERR_SEM_LOCK_LOCK;
        }
        else
        {
            break;
        }
    }
    return SUCCESS;
}

int SemLock::UnLock()
{
    if (m_iSemId == 0)
    {
        snprintf(m_pErrMsg, sizeof(m_pErrMsg), "Semaphore uninitialized!");
        return ERR_SEM_LOCK_LOCK;
    }
    struct sembuf arg;
    arg.sem_num = 0;
    arg.sem_op = 1; // 对信号量挂起操作
    arg.sem_flg = SEM_UNDO;
    while (1)
    {
        int ret = semop(m_iSemId, &arg, 1); // sem_op如果其值为正数，该值会加到现有的信号内含值中。通常用于释放所控资源的使用权
        if (ret == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            snprintf(m_pErrMsg, sizeof(m_pErrMsg), "Release semaphore faild! key is %d,semaphore index is %d,errmsg is %s", m_iSemKey, 0, strerror(errno));
            return ERR_SEM_LOCK_LOCK;
        }
        else
        {
            break;
        }
    }
    return SUCCESS;
}