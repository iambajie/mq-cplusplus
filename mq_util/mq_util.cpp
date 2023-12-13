// c/c++
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <string>
// linux
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/errno.h>
#include <time.h>
#include <dirent.h>
// user define
#include "mq_util.h"

using namespace WSMQ;

// 将当前进程转变成一个守护进程
int FuncTool::DaemonInit()
{
    pid_t pid; // 内核中的进程表的索引
    // 1.脱离原有的父进程和控制终端
    if ((pid = fork()) == -1) // 创建进程，返回进程号
    {
        return ERR_FUNC_TOOL_INIT_DAEMON;
    }
    else if (pid > 0)
    {
        exit(0);
    }
    if ((pid = setsid()) == -1) // 为进程创建一个新会话
    {
        return ERR_FUNC_TOOL_INIT_DAEMON;
    }
    signal(SIGHUP, SIG_IGN); // 第一个参数表示需要处理的信号值（SIGHUP），第二个参数为处理函数或者是一个表示，这里，SIG_IGN表示忽略SIGHUP那个注册的信号
    signal(SIGPIPE, SIG_IGN);
    // 2.防止重新获取控制终端
    if ((pid = fork()) == -1)
    {
        return ERR_FUNC_TOOL_INIT_DAEMON;
    }
    else if (pid > 0)
    {
        exit(0);
    }
    umask(0); // 设置允许当前进程创建文件或者目录最大可操作的权限
    return SUCCESS;
}

int FuncTool::SetNonBlock(int iFd)
{
    int flags = 0;
    // 获取当前标志(F_GETFL)和设置新的标志(F_SETFL)
    if ((flags = fcntl(iFd, F_GETFL, 0)) < 0 || fcntl(iFd, F_SETFL, flags | O_NONBLOCK) < 0) // 取得/设置fd的文件状态标志，使I/O变成非阻塞模式
    {
        return ERR_FUNC_TOOL_SET_NONBLOCK;
    }
    return SUCCESS;
}

int FuncTool::MakeEpollEvent(struct epoll_event &iEvent, void *const ptr) // epoll自带结构体epoll_event
{
    EightBytes temp;
    temp.ptr = ptr;
    iEvent.data.u64 = temp.u64;
    return SUCCESS;
}

void *FuncTool::GetEventDataPtr(const struct epoll_event &iEvent)
{
    EightBytes temp; // 联合体类型
    temp.u64 = iEvent.data.u64;
    return temp.ptr;
}

// start: 对内存进行读写操作
int FuncTool::ReadBool(const void *ipBuffer, bool &ibVal)
{
    memcpy(&ibVal, ipBuffer, sizeof(bool)); // 内存拷贝，从存储区 str2 复制 n 个字节到存储区 str1
    return sizeof(bool);
}

int FuncTool::WriteBool(void *ipBuffer, bool ibVal)
{
    memcpy(ipBuffer, &ibVal, sizeof(bool));
    return sizeof(bool);
}

int FuncTool::ReadByte(const void *ipBuffer, unsigned char &iVal)
{
    memcpy(&iVal, ipBuffer, sizeof(unsigned char));
    return sizeof(unsigned char);
}

int FuncTool::ReadByte(const void *ipBuffer, char &iVal)
{
    memcpy(&iVal, ipBuffer, sizeof(char));
    return sizeof(char);
}

int FuncTool::WriteByte(void *ipBuffer, unsigned char iVal)
{
    memcpy(ipBuffer, &iVal, sizeof(unsigned char));
    return sizeof(unsigned char);
}

int FuncTool::WriteByte(void *ipBuffer, char iVal)
{
    memcpy(ipBuffer, &iVal, sizeof(char));
    return sizeof(char);
}

// short、int、long 需要转换字节序
int FuncTool::ReadShort(const void *ipBuffer, unsigned short &oVal, int iToHostOrder /* = 1*/)
{
    memcpy(&oVal, ipBuffer, sizeof(unsigned short));
    if (iToHostOrder == 1)
    {
        oVal = ntohs(oVal); //  将一个无符号短整形数从网络字节顺序转换为主机字节顺序。
    }
    return sizeof(unsigned short);
}

int FuncTool::ReadShort(const void *ipBuffer, short &oVal, int iToHostOrder /* = 1*/)
{
    memcpy(&oVal, ipBuffer, sizeof(short));
    if (iToHostOrder == 1)
    {
        oVal = ntohs(oVal);
    }
    return sizeof(short);
}

int FuncTool::WriteShort(void *ipBuffer, unsigned short iVal, int iToNetOrder /* = 1*/)
{
    if (iToNetOrder == 1)
    {
        iVal = htons(iVal); // 将主机的无符号短整形数转换成网络字节顺序。
    }
    memcpy(ipBuffer, &iVal, sizeof(unsigned short));
    return sizeof(unsigned short);
}

int FuncTool::WriteShort(void *ipBuffer, short iVal, int iToNetOrder /* = 1*/)
{
    if (iToNetOrder == 1)
    {
        iVal = htons(iVal);
    }
    memcpy(ipBuffer, &iVal, sizeof(short));
    return sizeof(short);
}

int FuncTool::ReadInt(const void *ipBuffer, unsigned int &iVal, int iToHostOrder /* = 1*/)
{
    memcpy(&iVal, ipBuffer, sizeof(unsigned int));
    if (iToHostOrder == 1)
    {
        iVal = ntohl(iVal); //  将一个无符号长整形数从网络字节顺序转换为主机字节顺序。
    }
    return sizeof(unsigned int);
}

int FuncTool::ReadInt(const void *ipBuffer, int &iVal, int iToHostOrder /* = 1*/)
{
    memcpy(&iVal, ipBuffer, sizeof(int));
    if (iToHostOrder == 1)
    {
        iVal = ntohl(iVal);
    }
    return sizeof(int);
}

int FuncTool::WriteInt(void *ipBuffer, unsigned int iVal, int iToNetOrder /* = 1*/)
{
    if (iToNetOrder == 1)
    {
        iVal = htonl(iVal); //  将主机的无符号长整形数转换成网络字节顺序。
    }
    memcpy(ipBuffer, &iVal, sizeof(unsigned int));
    return sizeof(unsigned int);
}

int FuncTool::WriteInt(void *ipBuffer, int iVal, int iToNetOrder /* = 1*/)
{
    if (iToNetOrder == 1)
    {
        iVal = htonl(iVal);
    }
    memcpy(ipBuffer, &iVal, sizeof(int));
    return sizeof(int);
}

int FuncTool::ReadBuf(const void *ipSrc, void *ipDest, int iLen)
{
    memcpy(ipDest, ipSrc, iLen);
    return iLen;
}

int FuncTool::WriteBuf(void *ipDest, const void *ipSrc, int iLen)
{
    memcpy(ipDest, ipSrc, iLen);
    return iLen;
}

// end : 对内存进行读写操作

int FuncTool::Sendn(int iSockfd, const void *ipBuffer, int iLen)
{
    int iLeft = iLen;
    const char *pTemp = (const char *)ipBuffer;
    while (iLeft > 0)
    {
        // 将pTemp指向的内存区域中的iLeft个字节发送到套接字描述符iSockfd代表的网络连接
        int ret = send(iSockfd, pTemp, iLeft, 0); // s-已建立连接的套接字；buf-存放将要发送的数据的缓冲区指针；len-发送缓冲区中的字符数；flags-控制数据传输方式：
        if (ret < 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
            {
                // 套接字发送缓冲区无空闲空间，则继续等待
                continue;
            }
            return ERROR;
        }
        else if (ret == 0)
        {
            return ERROR;
        }
        // 减去已经发送的字节数
        iLeft -= ret;
        // 前移相应字节数
        pTemp += ret;
    }
    return SUCCESS;
}

int FuncTool::Recvn(int iSockfd, void *opBuffer, int iLen)
{
    int iLeft = iLen;
    char *pTemp = (char *)opBuffer;
    int iRet = 0;
    while (iLeft > 0)
    {
        // 从套接字描述符iSockfd代表的网络连接中接收iLeft个字节的数据，并将数据存放到pTemp指向的内存区域中
        iRet = recv(iSockfd, pTemp, iLeft, 0);
        if (iRet < 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
            {
                // 套接字发送缓冲区无空闲空间，则继续等待
                continue;
            }
            return ERROR;
        }
        else if (iRet == 0)
        {
            return ERROR;
        }
        iLeft -= iRet;
        pTemp += iRet;
    }
    return SUCCESS;
}

// 转换tSec为字符串格式的时间戳
const char *FuncTool::GetLogHeadTime(time_t tSec, time_t tUsec)
{
    static char szGetLogHeadTimeRet[64] = {0};
    struct tm *stSecTime = localtime(&tSec); // 转换时间格式
    int year = 1900 + stSecTime->tm_year;    // 从1900年开始的年数
    int month = stSecTime->tm_mon + 1;       // 从0开始的月数
    int day = stSecTime->tm_mday;            // 从1开始的天数
    int hour = stSecTime->tm_hour;           // 从0开始的小时数
    int min = stSecTime->tm_min;             // 从0开始的分钟数
    int sec = stSecTime->tm_sec;             // 从0开始的秒数

    sprintf(szGetLogHeadTimeRet, "%s%04d-%02d-%02d %02d:%02d:%02d.%06ld", ":", year, month, day, hour, min, sec, tUsec);
    return szGetLogHeadTimeRet;
}

int FuncTool::CheckDir(const char *ipPath)
{
    if (NULL == ipPath)
    {
        return ERROR;
    }

    // 所有用户拥有所有权限
    umask(0);

    struct stat stBuf;
    // 获取ipPath的文件状态信息, 存储在stBuf中
    if (lstat(ipPath, &stBuf) < 0) // lstat函数类似于stat但是当命名的文件是一个符号链接时，lstat返回该符号链接的有关信息，
                                   // 而不是由该符号链接引用文件的信息。第二个参数buf是指针，它指向一个用于保存文件描述信息的结构，由函数填写结构内容。
    {
        // 多线程或者多进程同时创建文件夹： errno == EEXIST
        if (mkdir(ipPath, 0777) != -1 || errno == EEXIST) // 创建所有者拥有读，写和执行操作的权限的文件
        {
            return SUCCESS;
        }
    }
    else
    {
        return SUCCESS;
    }

    // 所有者有全部权限，而组用户和其他用户只有读和执行权限
    umask(0022);

    return SUCCESS;
}

// 创建完整的目录路径： 逐级分解给定的路径，逐级检查路径是否存在，不存在则创建
int FuncTool::MakeDir(const char *ipPath, bool bIsFilePath /*=false*/)
{
    if (ipPath == NULL)
    {
        return ERROR;
    }

    int iLength = strlen(ipPath);

    if (iLength > 0)
    {
        char szDir[512] = {0};
        for (int i = 0; i < iLength; i++)
        {
            szDir[i] = ipPath[i];
            if (ipPath[i] == '/' && i != 0)
            {
                if (CheckDir(szDir) != SUCCESS)
                {
                    return ERROR;
                }
            }
        }
        // bIsFilePath参数为false（表示创建的是目录而非文件）
        if (szDir[iLength - 1] != '/' && !bIsFilePath)
        {
            if (CheckDir(szDir) != SUCCESS)
            {
                return ERROR;
            }
        }
    }
    return SUCCESS;
}

// stat(): 当路径是一个符号链接时，stat()会返回这个链接所指向的文件或目录的状态信息。
// lstat(): 当路径是一个符号链接时，lstat()会返回关于符号链接本身的状态信息，而不是它所指向的目标。

bool FuncTool::IsFileExist(const char *ipPath)
{
    if (ipPath == NULL)
    {
        return ERROR;
    }
    struct stat buffer;
    // 如果stat函数能成功获取到文件的状态信息（也就是说文件存在），那么它会返回0，此时函数返回true表示文件存在；否则，stat函数会返回非0值
    return (stat(ipPath, &buffer) == 0); // stat函数返回与此命名文件有关的信息结构
}

// 删除目录下的所有子文件和子目录
int FuncTool::RemoveDir(const char *ipPath)
{
    DIR *pDir = opendir(ipPath);
    if (!pDir)
    {
        return ERROR;
    }
    struct dirent *dir;
    struct stat st;
    while ((dir = readdir(pDir)) != NULL) // 读一个目录
    {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
        {
            continue;
        }
        std::string sub_path = ipPath;
        sub_path = sub_path + "/" + dir->d_name;
        if (lstat(sub_path.c_str(), &st) == -1)
        {
            continue;
        }
        if (S_ISDIR(st.st_mode)) // 是判断一个路径是否为目录 st_mode判断文件类型
        {
            if (RemoveDir(sub_path.c_str()) == -1) // 如果是目录文件，递归删除
            {
                closedir(pDir); // 关闭与指针dir相联系的目录流
                return ERROR;
            }
            rmdir(sub_path.c_str());
        }
        else if (S_ISREG(st.st_mode)) // 判断是否是常规文件
        {
            unlink(sub_path.c_str()); // 如果是普通文件，则unlink 删除pathname 指定的目录项，并将由pathname所引用的文件的链接数减一
        }
        else
        {
            continue;
        }
    }
    if (rmdir(ipPath) == -1) // delete dir itself.
    {
        closedir(pDir);
        return ERROR;
    }
    closedir(pDir);
    return SUCCESS;
}

// 删除指定路径下的文件或目录
int FuncTool::RemoveFile(const char *ipPath)
{
    if (ipPath == NULL)
    {
        return ERROR;
    }
    struct stat st;
    if (lstat(ipPath, &st) == -1)
    {
        return SUCCESS;
    }
    // 判断是否是常规文件
    if (S_ISREG(st.st_mode))
    {
        if (unlink(ipPath) == -1)
        {
            return ERROR;
        }
    }
    else if (S_ISDIR(st.st_mode))
    {
        if (strcmp(ipPath, ".") == 0 || strcmp(ipPath, "..") == 0)
        {
            return ERROR;
        }
        if (RemoveDir(ipPath) == -1)
        {
            return ERROR;
        }
    }
    return SUCCESS;
}