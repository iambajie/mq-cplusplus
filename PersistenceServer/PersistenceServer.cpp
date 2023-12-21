// c/c++
#include <string.h>
#include <algorithm>
// linux
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
// user define
#include "PersistenceServer.h"
#include "../common_def/comdef.h"
#include "../mq_util/mq_util.h"
#include "../logger/logger.h"
#include "../ini_file/ini_file.h"

using namespace WSMQ;
using std::to_string;

// 全局对象
// 处理与逻辑模块间通信的持久化服务器，具有启动、停止和交换数据的功能
Logger PersisServerLogger;

PersistenceServer::PersistenceServer()
{
    // 从逻辑结构接收数据
    m_pQueueFromLogic = NULL;
    // 从逻辑结构发送数据
    m_pQueueToLogic = NULL;
    m_bStop = false;
    m_pPersistenceServer = NULL;
}

PersistenceServer::~PersistenceServer()
{
    if (m_pQueueToLogic)
    {
        delete m_pQueueToLogic;
        m_pQueueToLogic = NULL;
    }
    if (m_pQueueFromLogic)
    {
        delete m_pQueueFromLogic;
        m_pQueueFromLogic = NULL;
    }
}

// 单例模式
PersistenceServer *PersistenceServer::GetInstance()
{
    // 在程序运行期间只有一个 PersistenceServer 对象存在
    if (m_pPersistenceServer == NULL)
    {
        m_pPersistenceServer = new PersistenceServer();
    }
    return m_pPersistenceServer;
}

void PersistenceServer::Destroy()
{
    if (m_pPersistenceServer)
    {
        delete m_pPersistenceServer;
        m_pPersistenceServer = NULL;
    }
}

int PersistenceServer::InitSigHandler()
{
    // 描述操作系统应如何响应特定信号
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = SigTermHandler;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGQUIT, &act, NULL);

    sigset_t set;
    sigemptyset(&set);
    // 严重的程序执行错误:段错误、总线错误、中止信号、非法指令和浮点异常
    sigaddset(&set, SIGSEGV);
    sigaddset(&set, SIGBUS);
    sigaddset(&set, SIGABRT);
    sigaddset(&set, SIGILL);
    sigaddset(&set, SIGFPE);
    // 解除阻塞set集合中的信号，即使得进程可以接收到这些信号
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    return SUCCESS;
}

int PersistenceServer::InitConf(const char *ipPath)
{
    if (!FuncTool::IsFileExist(ipPath))
    {
        return -1;
    }
    // 处理ini文件
    CIniFile objIniFile(ipPath);
    objIniFile.GetInt("MQ_CONF", "MaxCliPackSize", 0, &MAX_CLINT_PACKAGE_LENGTH);
    objIniFile.GetInt("MQ_CONF", "MaxSrvPackSize", 0, &MAX_SERVER_PACKAGE_LENGTH);
    objIniFile.GetInt("MQ_CONF", "MaxNameLength", 0, &MAX_NAME_LENGTH);
    objIniFile.GetString("MQ_CONF", "ServerIP", "", SERVER_DEFAULT_IP_ADDR, sizeof(SERVER_DEFAULT_IP_ADDR));
    objIniFile.GetInt("MQ_CONF", "ServerPort", 0, (int *)&SERVER_DEFAULT_PORT);
    objIniFile.GetInt("MQ_CONF", "EpollCount", 0, &CLIENT_EPOLL_COUNT);
    objIniFile.GetInt("MQ_CONF", "SockSendBufSize", 32, &SOCK_SEND_BUFF_SIZE);
    objIniFile.GetInt("MQ_CONF", "SockRecvBufSize", 0, &SOCK_RECV_BUFF_SIZE);
    objIniFile.GetInt("MQ_CONF", "ShmConLogicKey", 0, &CONNECT_TO_LOGIC_KEY);
    objIniFile.GetInt("MQ_CONF", "ShmLogicConKey", 0, &LOGIC_TO_CONNECT_KEY);
    objIniFile.GetInt("MQ_CONF", "ShmPersisLogicKey", 0, &PERSIS_TO_LOGIC_KEY);
    objIniFile.GetInt("MQ_CONF", "ShmLogicPersisKey", 0, &LOGIC_TO_PERSIS_KEY);
    objIniFile.GetInt("MQ_CONF", "ShmQueueSize", 0, &SHM_QUEUE_SIZE);
    objIniFile.GetString("MQ_CONF", "PersisExchangePath", "", DEFAULT_EXCHANGE_PATH, sizeof(DEFAULT_EXCHANGE_PATH));
    objIniFile.GetString("MQ_CONF", "PersisQueuePath", "", DEFAULT_QUEUE_PATH, sizeof(DEFAULT_QUEUE_PATH));
    objIniFile.GetInt("MQ_CONF", "PersisMsgFileSize", 0, &DURABLE_MESSAGE_FILE_SIZE);
    return 0;
}

int PersistenceServer::Init()
{
    // 1.初始化配置文件
    if (InitConf(CONF_FILE_PATH) != SUCCESS)
    {
        return ERROR;
    }
    // 2.初始化信号处理器
    InitSigHandler();
    // 3.初始化两个用于与业务逻辑层进行通信的共享内存队列
    m_pQueueFromLogic = new ShmQueue();
    m_pQueueToLogic = new ShmQueue();
    m_pQueueFromLogic->Init(LOGIC_TO_PERSIS_KEY, SHM_QUEUE_SIZE);
    m_pQueueToLogic->Init(PERSIS_TO_LOGIC_KEY, SHM_QUEUE_SIZE);

    // 4.初始化文件信息
    InitFileInfo();

    return SUCCESS;
}

// 创建一个新的exchange
int PersistenceServer::OnCreateExchange(char *ipBuffer, int iLen)
{
    // 读exchange 名称
    char pExchangeName[MAX_NAME_LENGTH];
    memset(pExchangeName, 0, sizeof(pExchangeName));
    char *pTemp = ipBuffer;
    int offset = FuncTool::ReadBuf(pTemp, pExchangeName, sizeof(pExchangeName));
    string strExchangeName(pExchangeName);

    // 检查对应文件夹是否存在
    string strExchangeDir = DEFAULT_EXCHANGE_PATH + strExchangeName;
    if (!FuncTool::IsFileExist(strExchangeDir.c_str()))
    {
        FuncTool::MakeDir(strExchangeDir.c_str());
    }
    // 写方式打开创建文件
    string strExchangeFile = strExchangeDir + "/info.bin";
    FILE *pExFile = fopen(strExchangeFile.c_str(), "wb+");
    if (pExFile == NULL)
    {
        return ERROR;
    }
    // 写入数据
    fwrite(ipBuffer, iLen, 1, pExFile);
    fflush(pExFile);
    fclose(pExFile);
    return SUCCESS;
}

int PersistenceServer::OnCreateQueue(char *ipBuffer, int iLen)
{
    // 读名称
    char *pTemp = ipBuffer;
    char pQueueName[MAX_NAME_LENGTH];
    memset(pQueueName, 0, sizeof(pQueueName));
    int offset = FuncTool::ReadBuf(pTemp, pQueueName, sizeof(pQueueName));
    string strQueueName(pQueueName);

    // 检查对应文件夹是否存在
    string strQueueDir = DEFAULT_QUEUE_PATH + strQueueName;
    if (!FuncTool::IsFileExist(strQueueDir.c_str()))
    {
        FuncTool::MakeDir(strQueueDir.c_str());
    }
    // 写方式打开创建文件
    string strQueueFile = strQueueDir + "/info.bin";
    FILE *pQueueFile = fopen(strQueueFile.c_str(), "wb+");
    if (pQueueFile == NULL)
    {
        return ERROR;
    }
    // 写入数据
    fwrite(ipBuffer, iLen, 1, pQueueFile);
    fflush(pQueueFile);
    fclose(pQueueFile);
    return SUCCESS;
}

int PersistenceServer::OnDeleteExchange(char *ipBuffer, int iLen)
{
    // 读exchange 名称
    char pExchangeName[MAX_NAME_LENGTH];
    memset(pExchangeName, 0, sizeof(pExchangeName));
    char *pTemp = ipBuffer;
    int offset = FuncTool::ReadBuf(pTemp, pExchangeName, sizeof(pExchangeName));
    string strExchangeName(pExchangeName);

    // 检查对应文件夹是否存在
    string strExchangeDir = DEFAULT_EXCHANGE_PATH + strExchangeName;
    if (FuncTool::IsFileExist(strExchangeDir.c_str()))
    {
        FuncTool::RemoveDir(strExchangeDir.c_str());
    }
    return SUCCESS;
}

int PersistenceServer::OnDeleteQueue(char *ipBuffer, int iLen)
{
    // 读名称
    char *pTemp = ipBuffer;
    char pQueueName[MAX_NAME_LENGTH];
    memset(pQueueName, 0, sizeof(pQueueName));
    int offset = FuncTool::ReadBuf(pTemp, pQueueName, sizeof(pQueueName));
    string strQueueName(pQueueName);

    // 检查对应文件夹是否存在
    string strQueueDir = DEFAULT_QUEUE_PATH + strQueueName;
    if (FuncTool::IsFileExist(strQueueDir.c_str()))
    {
        FuncTool::RemoveDir(strQueueDir.c_str());
    }
    // 当删除一个队列时，也需要清理与之相关的所有资源，包括在映射中的条目。这样可以释放内存，避免出现无效指向，保持数据的一致性
    if (m_mMsgQueueTailFile.find(strQueueName) != m_mMsgQueueTailFile.end())
    {
        m_mMsgQueueTailFile.erase(strQueueName);
    }
    return SUCCESS;
}

// 处理队列订阅的请求
// 并没有真正进行 "订阅" 的操作 更新某个订阅者列表
int PersistenceServer::OnSubcribe(char *ipBuffer, int iLen)
{
    // 读名称
    char *pTemp = ipBuffer;
    char pQueueName[MAX_NAME_LENGTH];
    memset(pQueueName, 0, sizeof(pQueueName));
    int offset = FuncTool::ReadBuf(pTemp, pQueueName, sizeof(pQueueName));
    string strQueueName(pQueueName);
    // 检查对应文件夹是否存在
    string strQueueDir = DEFAULT_QUEUE_PATH + strQueueName;
    if (!FuncTool::IsFileExist(strQueueDir.c_str()))
    {
        return ERROR;
    }
    // 写方式打开创建文件
    string strSubscribeFile = strQueueDir + "/subscribe.bin";
    FILE *pSubscribeFile = fopen(strSubscribeFile.c_str(), "wb+");
    if (pSubscribeFile == NULL)
    {
        return ERROR;
    }
    // 写入数据
    fwrite(ipBuffer, iLen, 1, pSubscribeFile);
    fflush(pSubscribeFile);
    fclose(pSubscribeFile);
    return SUCCESS;
}

// 处理交换机的绑定请求
// 并没有真正进行 "绑定" 的操作 更新某个交换器与队列的绑定列表
int PersistenceServer::OnBinding(char *ipBuffer, int iLen)
{
    // 读exchange 名称
    char pExchangeName[MAX_NAME_LENGTH];
    memset(pExchangeName, 0, sizeof(pExchangeName));
    char *pTemp = ipBuffer;
    int offset = FuncTool::ReadBuf(pTemp, pExchangeName, sizeof(pExchangeName));
    string strExchangeName(pExchangeName);

    // 检查对应文件夹是否存在
    string strExchangeDir = DEFAULT_EXCHANGE_PATH + strExchangeName;
    if (!FuncTool::IsFileExist(strExchangeDir.c_str()))
    {
        return ERROR;
    }
    // 写方式打开创建文件
    string strExchangeFile = strExchangeDir + "/binding.bin";
    FILE *pBindingFile = fopen(strExchangeFile.c_str(), "wb+");
    if (pBindingFile == NULL)
    {
        return ERROR;
    }
    // 写入数据
    fwrite(ipBuffer, iLen, 1, pBindingFile);
    fflush(pBindingFile);
    fclose(pBindingFile);
    return SUCCESS;
}

// 处理消息的发布请求
// 从输入缓冲区 ipBuffer 中读取数据，并将其写入一个文件中，该文件位于给定路径下以队列名命名的文件夹内, 同时创建新的数据和索引文件
// 假设有一个简单的消息队列，其中包含以下消息：
// Queue: ["Message 1", "Message 2", "Message 3", "Message 4"]
// 每个消息在队列中都有一个唯一的索引，这就是 iDurableIndex。例如，"Message 1" 的 iDurableIndex 可能是 0，“Message 2” 的 iDurableIndex 是 1，以此类推。

// 当系统运行时，它会将每个消息以及对应的 iDurableIndex 写入数据文件和索引文件中。例如，当处理 "Message 2" 时，系统可能执行以下操作：

// 将 "Message 2" 写入 .data 文件。
// 将 iDurableIndex=1 和该消息在 .data 文件中的位置写入 .index 文件。
// 同时，在处理完 "Message 2" 后，系统还会更新偏移文件，记录下最新的 iDurableIndex=1。

// 现在，如果系统突然崩溃并重新启动，它需要找出上次已经处理到哪个消息。
// 系统可以查看偏移文件，从中读取到 iDurableIndex=1，然后系统知道 "Message 1" 和 "Message 2" 已经被处理过（即已经持久化），
// 而 "Message 3" 和 "Message 4" 还未处理。然后，通过 .index 文件，系统可以定位到 "Message 3" 在 .data 文件中的位置，然后继续从 "Message 3" 开始处理。

int PersistenceServer::OnPublishMessage(char *ipBuffer, int iLen, string istrQueueName, int iDurableIndex)
{
    // 检查对应文件夹是否存在
    string strQueueDir = DEFAULT_QUEUE_PATH + istrQueueName;
    if (!FuncTool::IsFileExist(strQueueDir.c_str()))
    {
        return ERROR;
    }
    // 判断是否需要新建文件
    string strWriteFileName;
    bool bNeedCreate = false;
    bool bFileFull = false;
    int iWritePos = 0;
    if (m_mMsgQueueTailFile.find(istrQueueName) == m_mMsgQueueTailFile.end())
    {
        bNeedCreate = true;
    }
    else
    {
        strWriteFileName = m_mMsgQueueTailFile[istrQueueName];
        string strWriteFilePath = strQueueDir + "/" + strWriteFileName + ".data";
        struct stat buffer;
        if (stat(strWriteFilePath.c_str(), &buffer) == -1)
        {
            bNeedCreate = true;
        }
        if (buffer.st_size + iLen > DURABLE_MESSAGE_FILE_SIZE)
        {
            bNeedCreate = true;
            bFileFull = true;
        }
        else
        {
            iWritePos = buffer.st_size;
        }
    }
    // 新建文件
    FILE *pDataFile = NULL;
    FILE *pIndexFile = NULL;
    string strDataPath;
    string strIndexPath;
    if (bNeedCreate)
    {
        string strFileName = ConvertIndexToString(iDurableIndex);
        strDataPath = strQueueDir + "/" + strFileName + ".data";
        strIndexPath = strQueueDir + "/" + strFileName + ".index";
        m_mMsgQueueTailFile[istrQueueName] = strFileName;
        PersisServerLogger.WriteLog(mq_log_info, "new file created,name is %s", strFileName.c_str());
        PersisServerLogger.Print(mq_log_info, "new file created,name is %s", strFileName.c_str());
        // 保存消息对应文件
        m_mMsgQueueIndexFile[istrQueueName].insert({iDurableIndex, strFileName});
        // 增加消息链表
        m_mMsgQueueList[istrQueueName].push_back(strFileName);
    }
    else
    {
        strDataPath = strQueueDir + "/" + strWriteFileName + ".data";
        strIndexPath = strQueueDir + "/" + strWriteFileName + ".index";
        // 保存消息对应文件
        m_mMsgQueueIndexFile[istrQueueName].insert({iDurableIndex, strWriteFileName});
    }
    // 追加写入
    pDataFile = fopen(strDataPath.c_str(), "ab+");
    pIndexFile = fopen(strIndexPath.c_str(), "ab+");
    string strOffsetFile = strQueueDir + "/write_offset.bin";
    // 写方式打开创建文件 会覆盖原有内容
    FILE *pOffsetFile = fopen(strOffsetFile.c_str(), "wb+");
    if (pDataFile == NULL || pIndexFile == NULL || pOffsetFile == NULL)
    {
        return ERROR;
    }
    // 写入数据
    // 数据文件中写入的是原始消息数据
    // 索引文件中写入的是消息的索引和对应的写入位置
    // 偏移文件中写入的是当前的持久化索引

    // 1.写入数据到数据文件: 将 ipBuffer 指向的 iLen 字节的数据写入到数据文件 pDataFile
    fwrite(ipBuffer, iLen, 1, pDataFile);
    fflush(pDataFile);
    fclose(pDataFile);

    // 2.写入数据到索引文件: 将消息的持久化索引和数据文件中的写入位置写入到索引文件 pIndexFile
    // iDurableIndex 用作了消息队列持久化过程中的关键索引 用来追踪或标识应该被持久化到磁盘的消息的位置或编
    iLen = sizeof(int) + sizeof(int);
    char pBuff[10];
    memset(pBuff, 0, sizeof(pBuff));
    char *pTemp = pBuff;
    int offset = FuncTool::WriteInt(pTemp, iDurableIndex);
    pTemp += offset;
    offset = FuncTool::WriteInt(pTemp, iWritePos);
    fwrite(pBuff, iLen, 1, pIndexFile);
    fflush(pIndexFile);
    fclose(pIndexFile);

    // 3.写入数据到偏移文件: 将当前的持久化索引写入到偏移文件 pOffsetFile
    // 偏移文件只保存了最后写入磁盘的消息的索引, 每次都会被覆写，而不是追加新的内容
    char pIndex[10];
    memset(pIndex, 0, sizeof(pIndex));
    FuncTool::WriteInt(pIndex, iDurableIndex);
    fwrite(pIndex, sizeof(iDurableIndex), 1, pOffsetFile);
    fflush(pOffsetFile);
    fclose(pOffsetFile);
    return SUCCESS;
}

// 处理消息消费，并在消息全部被消费时删除相应文件
// ipBuffer 表示输入的消息缓冲区，用于读取和处理消息； iLen 是输入消息的长度
int PersistenceServer::OnConsumeMessage(char *ipBuffer, int iLen)
{
    // 读名称
    char *pTemp = ipBuffer;
    char pQueueName[MAX_NAME_LENGTH];
    memset(pQueueName, 0, sizeof(pQueueName));
    int offset = FuncTool::ReadBuf(pTemp, pQueueName, sizeof(pQueueName));
    string strQueueName(pQueueName);
    // 读消费下标
    pTemp += MAX_NAME_LENGTH;
    int iIndex;
    offset = FuncTool::ReadInt(pTemp, iIndex);
    // 检查对应文件夹是否存在
    string strQueueDir = DEFAULT_QUEUE_PATH + strQueueName;
    if (!FuncTool::IsFileExist(strQueueDir.c_str()))
    {
        return ERROR;
    }
    // 查找下标对应的文件组
    if (m_mMsgQueueIndexFile.find(strQueueName) == m_mMsgQueueIndexFile.end())
    {
        return ERROR;
    }
    unordered_map<int, string> indexFiles = m_mMsgQueueIndexFile[strQueueName];
    if (indexFiles.find(iIndex) == indexFiles.end())
    {
        return ERROR;
    }

    // 更新消费下标文件
    string strFileName = indexFiles[iIndex];
    string strFilePath = strQueueDir + "/" + indexFiles[iIndex] + ".consume";
    FILE *pFile = fopen(strFilePath.c_str(), "ab+");
    if (pFile == NULL)
    {
        return ERROR;
    }

    // 写入数据
    fwrite(pTemp, offset, 1, pFile);
    fflush(pFile);
    fclose(pFile);
    // 根据当前文件大小计算出已消费消息数目
    // 已消费的消息数量（iConsumeNum）和本组总消息数量（iMsgNum）
    struct stat buffer;
    if (stat(strFilePath.c_str(), &buffer) != 0)
    {
        return ERROR;
    }
    int iConsumeNum = buffer.st_size / (sizeof(int));

    // 根据同组index文件大小计算本组消息数目
    string strIndexPath = strQueueDir + "/" + strFileName + ".index";

    if (stat(strIndexPath.c_str(), &buffer) != 0)
    {
        return ERROR;
    }
    int iMsgNum = buffer.st_size / (sizeof(int) + sizeof(int));

    // 若所有消息已经消费则删除当前文件
    if (iConsumeNum == iMsgNum)
    {
        string strDataPath = strQueueDir + "/" + strFileName + ".data";
        FuncTool::RemoveFile(strDataPath.c_str());
        FuncTool::RemoveFile(strIndexPath.c_str());
        FuncTool::RemoveFile(strFilePath.c_str());
        // 若删除的是最后一组文件，则清空当前文件记录
        if (m_mMsgQueueTailFile.find(strQueueName) != m_mMsgQueueTailFile.end() && m_mMsgQueueTailFile[strQueueName] == strFileName)
        {
            m_mMsgQueueTailFile.erase(strQueueName);
        }
        // 遍历文件链表，删除该组文件
        if (m_mMsgQueueList.find(strQueueName) != m_mMsgQueueList.end())
        {
            list<string> &fileList = m_mMsgQueueList[strQueueName];
            list<string>::iterator itr = fileList.begin();
            while (itr != fileList.end())
            {
                if (*itr == strFileName)
                {
                    fileList.erase(itr++);
                }
                else
                {
                    itr++;
                }
            }
        }
    }
    return SUCCESS;
}

// 将一个整数转化为长度为10的字符串。如果原始整数的位数少于10位，那么在其前面补零以达到10位的长度
string PersistenceServer::ConvertIndexToString(int iIndex)
{
    string strIndex = to_string(iIndex);
    int count = 10 - strIndex.size();
    if (count > 0)
    {
        string strPrev(count, '0');
        strIndex = strPrev + strIndex;
    }
    return strIndex;
}

// 扫描默认队列路径下的所有子文件夹（每个子文件夹可以看作一个消息队列），找出其中的所有索引文件
// 并进行一些预处理 排序索引文件名、处理索引文件、记录索引文件名等
int PersistenceServer::InitFileInfo()
{
    // 建立消息下标和索引文件的关系
    // 查看queue文件夹是否存在
    string strQueueDir = DEFAULT_QUEUE_PATH;
    if (!FuncTool::IsFileExist(strQueueDir.c_str()))
    {
        return SUCCESS;
    }
    // 遍历文件夹中的所有子文件夹
    DIR *pDir = opendir(strQueueDir.c_str());
    if (!pDir)
    {
        return ERROR;
    }
    struct dirent *dir;
    struct stat st;
    char pBuffer[MAX_RDWR_FILE_BUFF_SIZE];
    while ((dir = readdir(pDir)) != NULL)
    {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
        {
            continue;
        }
        string sub_path = strQueueDir;
        sub_path = sub_path + "/" + dir->d_name;
        if (lstat(sub_path.c_str(), &st) == -1)
        {
            continue;
        }
        if (S_ISDIR(st.st_mode))
        {
            // 获取文件夹中所有的index文件名称
            vector<string> vIndexFiles;
            FindIndexFiles(sub_path.c_str(), vIndexFiles);
            // 排序保证顺序从小到大
            std::sort(vIndexFiles.begin(), vIndexFiles.end());
            // 遍历所有文件，依次进行处理
            for (int i = 0; i < vIndexFiles.size(); ++i)
            {
                ProcessIndexFile(dir->d_name, vIndexFiles[i].c_str());
                // 将文件名记录到链表
                m_mMsgQueueList[dir->d_name].push_back(vIndexFiles[i]);
            }
        }
    }
    closedir(pDir);
    return SUCCESS;
}

// 在给定的目录（ipDir）中查找所有的索引文件，并把这些文件的名称保存到向量（ovIndexFiles）
int PersistenceServer::FindIndexFiles(const char *ipDir, vector<string> &ovIndexFiles)
{
    // 遍历文件夹中的所有子文件夹
    DIR *pDir = opendir(ipDir);
    if (!pDir)
    {
        return ERROR;
    }
    struct dirent *dir;
    struct stat st;
    char pBuffer[MAX_RDWR_FILE_BUFF_SIZE];
    while ((dir = readdir(pDir)) != NULL)
    {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
        {
            continue;
        }
        string sub_path = ipDir;
        sub_path = sub_path + "/" + dir->d_name;
        if (lstat(sub_path.c_str(), &st) == -1)
        {
            continue;
        }
        string strName = dir->d_name;
        if (S_ISREG(st.st_mode))
        {
            int iPos = strName.find(".index");
            if (iPos == string::npos)
            {
                continue;
            }
            //.index 前面的部分作为索引文件名
            strName = strName.substr(0, iPos);
            ovIndexFiles.push_back(strName);
        }
    }
    closedir(pDir);
    return SUCCESS;
}

// 处理消息队列的索引文件, 将索引文件的内容读取到内存中，并构造一个映射为了后续使用
int PersistenceServer::ProcessIndexFile(string istrQueueName, const char *ipFile)
{
    string strFile = ipFile;
    string strIndexFile = DEFAULT_QUEUE_PATH + istrQueueName + "/" + strFile + ".index";
    int indexFd = open(strIndexFile.c_str(), O_RDONLY);
    if (indexFd == -1)
    {
        return ERROR;
    }
    struct stat buf;
    if (fstat(indexFd, &buf) == -1 || buf.st_size <= 0)
    {
        return ERROR;
    }
    int indexFileSize = buf.st_size;

    // 将文件映射到进程的地址空间的系统调用
    char *pIndexStart = (char *)mmap(NULL, indexFileSize, PROT_READ, MAP_PRIVATE, indexFd, 0);
    if (pIndexStart == NULL)
    {
        return ERROR;
    }
    // 遍历整个内存映射区（即索引文件的全部内容），并从中读取出所有的索引条目
    int iPos = 0;
    while (iPos < indexFileSize)
    {
        // 读取消息下标及位置
        int iIndex = 0;
        int offset = FuncTool::ReadInt(pIndexStart + iPos, iIndex);
        iPos += offset;
        int iDataPos = 0;
        offset = FuncTool::ReadInt(pIndexStart + iPos, iDataPos);
        iPos += offset;
        m_mMsgQueueIndexFile[istrQueueName].insert({iIndex, ipFile});
    }
    munmap(pIndexStart, indexFileSize);
    return SUCCESS;
}

// 遍历每个队列的文件链表，将有效信息小于50%的连续两个文件进行合并。记录了处理（即已成功合并）的文件数量 oClearCount
int PersistenceServer::ClearUpFile(int &oClearCount)
{
    oClearCount = 0;
    unordered_map<string, list<string>>::iterator itr1 = m_mMsgQueueList.begin();
    while (itr1 != m_mMsgQueueList.end() && oClearCount == 0)
    {
        string strQueueName = itr1->first;
        list<string> fileList = itr1->second;
        string strPrev = "";
        list<string>::iterator itr2 = fileList.begin();
        while (itr2 != fileList.end())
        {
            if (!strPrev.empty())
            {
                string strPrevPath = DEFAULT_QUEUE_PATH + strQueueName + "/" + strPrev;
                string strCurPath = DEFAULT_QUEUE_PATH + strQueueName + "/" + *itr2;
                // 计算当前组文件和前一组文件的消费比例
                double prevConsumeRate = CalculateCondsumeRate(strPrevPath.c_str());
                double curConsumeRate = CalculateCondsumeRate(strCurPath.c_str());
                // 若两组消费比例均大于0.5则进行合并
                if (prevConsumeRate > 0.5 && curConsumeRate > 0.5)
                {
                    if (MergeFiles(strQueueName.c_str(), strPrev.c_str(), itr2->c_str()) == SUCCESS)
                    {
                        // 合并成功删除当前节点
                        fileList.erase(itr2++);
                        ++oClearCount;
                    }
                    else
                    {
                        ++itr2;
                    }
                }
                else
                {
                    ++itr2;
                }
            }
            else
            {
                ++itr2;
            }
        }
        ++itr1;
    }
    return SUCCESS;
}

double PersistenceServer::CalculateCondsumeRate(const char *ipFile)
{
    string strFile = ipFile;
    string strIndexFile = strFile + ".index";
    string strConsumeFile = strFile + ".consume";
    // 若index文件不存在，直接返回0
    struct stat buf;
    if (stat(strIndexFile.c_str(), &buf) == -1 || buf.st_size <= 0)
    {
        return 0;
    }
    int indexFileSize = buf.st_size;

    // 消费文件不存在，说明未消费，返回0
    if (stat(strConsumeFile.c_str(), &buf) == -1 || buf.st_size <= 0)
    {
        return 0;
    }
    int consumeFileSize = buf.st_size;

    // 计算消息个数和已消费个数
    int iMsgNum = indexFileSize / (sizeof(int) + sizeof(int));
    int iConsumeNum = consumeFileSize / (sizeof(int));
    double rate = (iConsumeNum * 1.0) / iMsgNum;
    return rate;
}

int PersistenceServer::MergeFiles(const char *ipQueueName, const char *ipFileName1, const char *ipFileName2)
{
    // 获取数据文件、索引文件、消费文件是否存在
    string strQueueName = ipQueueName;
    string strFile1 = DEFAULT_QUEUE_PATH + strQueueName + "/" + ipFileName1;
    string strIndexFile1 = strFile1 + ".index";
    string strDataFile1 = strFile1 + ".data";
    string strConsumeFile1 = strFile1 + ".consume";
    string strFile2 = DEFAULT_QUEUE_PATH + strQueueName + "/" + ipFileName2;
    string strIndexFile2 = strFile2 + ".index";
    string strDataFile2 = strFile2 + ".data";
    string strConsumeFile2 = strFile2 + ".consume";

    int indexFd1 = open(strIndexFile1.c_str(), O_RDONLY);
    if (indexFd1 == -1)
    {
        return ERROR;
    }
    struct stat buf;
    if (fstat(indexFd1, &buf) == -1 || buf.st_size <= 0)
    {
        return ERROR;
    }
    int indexFileSize1 = buf.st_size;

    int dataFd1 = open(strDataFile1.c_str(), O_RDONLY);
    if (dataFd1 == -1)
    {
        return ERROR;
    }
    if (fstat(dataFd1, &buf) == -1 || buf.st_size <= 0)
    {
        return ERROR;
    }
    int dataFileSize1 = buf.st_size;

    int consumeFd1 = open(strConsumeFile1.c_str(), O_RDONLY);
    if (consumeFd1 == -1)
    {
        return ERROR;
    }
    if (fstat(consumeFd1, &buf) == -1 || buf.st_size <= 0)
    {
        return ERROR;
    }
    int consumeFileSize1 = buf.st_size;

    // 将文件映射到内存
    char *pIndexStart1 = (char *)mmap(NULL, indexFileSize1, PROT_READ, MAP_PRIVATE, indexFd1, 0);
    if (pIndexStart1 == NULL)
    {
        return ERROR;
    }
    char *pDataStart1 = (char *)mmap(NULL, dataFileSize1, PROT_READ, MAP_PRIVATE, dataFd1, 0);
    if (pDataStart1 == NULL)
    {
        munmap(pIndexStart1, indexFileSize1);
        return ERROR;
    }
    char *pConsumeStart1 = (char *)mmap(NULL, consumeFileSize1, PROT_READ, MAP_PRIVATE, consumeFd1, 0);
    if (pConsumeStart1 == NULL)
    {
        munmap(pIndexStart1, indexFileSize1);
        munmap(pDataStart1, dataFileSize1);
        return ERROR;
    }

    int indexFd2 = open(strIndexFile2.c_str(), O_RDONLY);
    if (indexFd2 == -1)
    {
        munmap(pIndexStart1, indexFileSize1);
        munmap(pDataStart1, dataFileSize1);
        munmap(pConsumeStart1, consumeFileSize1);
        return ERROR;
    }
    if (fstat(indexFd2, &buf) == -1 || buf.st_size <= 0)
    {
        munmap(pIndexStart1, indexFileSize1);
        munmap(pDataStart1, dataFileSize1);
        munmap(pConsumeStart1, consumeFileSize1);
        return ERROR;
    }
    int indexFileSize2 = buf.st_size;

    int dataFd2 = open(strDataFile2.c_str(), O_RDONLY);
    if (dataFd2 == -1)
    {
        munmap(pIndexStart1, indexFileSize1);
        munmap(pDataStart1, dataFileSize1);
        munmap(pConsumeStart1, consumeFileSize1);
        return ERROR;
    }
    if (fstat(dataFd2, &buf) == -1 || buf.st_size <= 0)
    {
        munmap(pIndexStart1, indexFileSize1);
        munmap(pDataStart1, dataFileSize1);
        munmap(pConsumeStart1, consumeFileSize1);
        return ERROR;
    }
    int dataFileSize2 = buf.st_size;

    int consumeFd2 = open(strConsumeFile2.c_str(), O_RDONLY);
    if (consumeFd2 == -1)
    {
        munmap(pIndexStart1, indexFileSize1);
        munmap(pDataStart1, dataFileSize1);
        munmap(pConsumeStart1, consumeFileSize1);
        return ERROR;
    }
    if (fstat(consumeFd2, &buf) == -1 || buf.st_size <= 0)
    {
        munmap(pIndexStart1, indexFileSize1);
        munmap(pDataStart1, dataFileSize1);
        munmap(pConsumeStart1, consumeFileSize1);
        return ERROR;
    }
    int consumeFileSize2 = buf.st_size;

    // 将文件映射到内存
    char *pIndexStart2 = (char *)mmap(NULL, indexFileSize2, PROT_READ, MAP_PRIVATE, indexFd2, 0);
    if (pIndexStart2 == NULL)
    {
        munmap(pIndexStart1, indexFileSize1);
        munmap(pDataStart1, dataFileSize1);
        munmap(pConsumeStart1, consumeFileSize1);
        return ERROR;
    }
    char *pDataStart2 = (char *)mmap(NULL, dataFileSize2, PROT_READ, MAP_PRIVATE, dataFd2, 0);
    if (pDataStart2 == NULL)
    {
        munmap(pIndexStart1, indexFileSize1);
        munmap(pDataStart1, dataFileSize1);
        munmap(pConsumeStart1, consumeFileSize1);
        munmap(pIndexStart2, indexFileSize2);
        return ERROR;
    }
    char *pConsumeStart2 = (char *)mmap(NULL, consumeFileSize2, PROT_READ, MAP_PRIVATE, consumeFd2, 0);
    if (pConsumeStart2 == NULL)
    {
        munmap(pIndexStart1, indexFileSize1);
        munmap(pDataStart1, dataFileSize1);
        munmap(pConsumeStart1, consumeFileSize1);
        munmap(pIndexStart2, indexFileSize2);
        munmap(pDataStart2, dataFileSize2);
        return ERROR;
    }

    // 将已经消费消息存入set
    unordered_set<int> consumeIndexSet;
    int iPos = 0;
    while (iPos < consumeFileSize1)
    {
        int iIndex = 0;
        int offset = FuncTool::ReadInt(pConsumeStart1 + iPos, iIndex);
        consumeIndexSet.insert(iIndex);
        iPos += offset;
    }
    munmap(pConsumeStart1, consumeFileSize1);

    // 存放消息的缓冲区
    char pDataBuff[DURABLE_MESSAGE_FILE_SIZE];
    memset(pDataBuff, 0, sizeof(pDataBuff));
    // 存放index的缓冲区
    char *pIndexBuff = new char[indexFileSize1];
    memset(pIndexBuff, 0, indexFileSize1);

    // 遍历所有消息，将未消费消息添加进队列
    iPos = 0;
    int iDataLen = 0;
    int iIndexLen = 0;
    while (iPos < indexFileSize1)
    {
        // 读取消息下标及位置
        int iIndex = 0;
        int offset = FuncTool::ReadInt(pIndexStart1 + iPos, iIndex);
        iPos += offset;
        int iDataPos = 0;
        offset = FuncTool::ReadInt(pIndexStart1 + iPos, iDataPos);
        iPos += offset;
        // 若消息已经被消费，则跳过
        if (consumeIndexSet.find(iIndex) != consumeIndexSet.end())
        {
            if (m_mMsgQueueIndexFile.find(strQueueName) != m_mMsgQueueIndexFile.end())
            {
                unordered_map<int, string> &indexMap = m_mMsgQueueIndexFile[strQueueName];
                indexMap.erase(iIndex);
            }
            continue;
        }
        // 拷贝未消费数据
        memcpy(pIndexBuff, pIndexStart1 + iPos - sizeof(int) - sizeof(int), sizeof(int) + sizeof(int));
        iIndexLen += sizeof(int) + sizeof(int);
        char *pBuff = pDataStart1 + iDataPos;
        unsigned short iMsgLen = 0;
        offset = FuncTool::ReadShort(pBuff, iMsgLen);
        memcpy(pDataBuff + iDataLen, pBuff, iMsgLen);
        iDataLen += iMsgLen;
    }
    munmap(pDataStart1, dataFileSize1);
    munmap(pIndexStart1, indexFileSize1);

    // 更新文件内容
    FILE *pDataFile = fopen(strDataFile1.c_str(), "wb+");
    FILE *pIndexFile = fopen(strIndexFile1.c_str(), "wb+");
    if (pDataFile == NULL || pIndexFile == NULL)
    {
        delete[] pIndexBuff;
        return ERROR;
    }
    // 写入数据
    fwrite(pDataBuff, iDataLen, 1, pDataFile);
    fflush(pDataFile);
    fclose(pDataFile);
    fwrite(pIndexBuff, iIndexLen, 1, pIndexFile);
    fflush(pIndexFile);
    fclose(pIndexFile);

    // 将已经消费消息存入set
    consumeIndexSet.clear();
    iPos = 0;
    while (iPos < consumeFileSize2)
    {
        int iIndex = 0;
        int offset = FuncTool::ReadInt(pConsumeStart2 + iPos, iIndex);
        consumeIndexSet.insert(iIndex);
        iPos += offset;
    }
    munmap(pConsumeStart2, consumeFileSize2);

    // 存放消息的缓冲区
    memset(pDataBuff, 0, sizeof(pDataBuff));
    // 存放index的缓冲区
    memset(pIndexBuff, 0, indexFileSize1);

    // 遍历所有消息，将未消费消息添加进队列
    iPos = 0;
    iDataLen = 0;
    iIndexLen = 0;
    while (iPos < indexFileSize2)
    {
        // 读取消息下标及位置
        int iIndex = 0;
        int offset = FuncTool::ReadInt(pIndexStart2 + iPos, iIndex);
        iPos += offset;
        int iDataPos = 0;
        offset = FuncTool::ReadInt(pIndexStart2 + iPos, iDataPos);
        iPos += offset;
        // 若消息已经被消费，则跳过
        if (consumeIndexSet.find(iIndex) != consumeIndexSet.end())
        {
            if (m_mMsgQueueIndexFile.find(strQueueName) != m_mMsgQueueIndexFile.end())
            {
                unordered_map<int, string> &indexMap = m_mMsgQueueIndexFile[strQueueName];
                indexMap.erase(iIndex);
            }
            continue;
        }
        // 拷贝未消费数据
        memcpy(pIndexBuff, pIndexStart2 + iPos - sizeof(int) - sizeof(int), sizeof(int) + sizeof(int));
        iIndexLen += sizeof(int) + sizeof(int);
        char *pBuff = pDataStart2 + iDataPos;
        unsigned short iMsgLen = 0;
        offset = FuncTool::ReadShort(pBuff, iMsgLen);
        memcpy(pDataBuff + iDataLen, pBuff, iMsgLen);
        iDataLen += iMsgLen;
        // 更新下标与文件组的对应关系
        if (m_mMsgQueueIndexFile.find(strQueueName) != m_mMsgQueueIndexFile.end())
        {
            unordered_map<int, string> &indexMap = m_mMsgQueueIndexFile[strQueueName];
            indexMap[iIndex] = ipFileName1;
        }
    }
    munmap(pDataStart2, dataFileSize2);
    munmap(pIndexStart2, indexFileSize2);

    // 更新文件内容
    pDataFile = fopen(strDataFile2.c_str(), "ab+");
    pIndexFile = fopen(strIndexFile2.c_str(), "ab+");
    if (pDataFile == NULL || pIndexFile == NULL)
    {
        delete[] pIndexBuff;
        return ERROR;
    }
    // 写入数据
    fwrite(pDataBuff, iDataLen, 1, pDataFile);
    fflush(pDataFile);
    fclose(pDataFile);
    fwrite(pIndexBuff, iIndexLen, 1, pIndexFile);
    fflush(pIndexFile);
    fclose(pIndexFile);
    delete[] pIndexBuff;
    // 删除第二组文件
    FuncTool::RemoveFile(strIndexFile2.c_str());
    FuncTool::RemoveFile(strDataFile2.c_str());
    FuncTool::RemoveFile(strConsumeFile2.c_str());
    // 删除第一组文件中的消费文件，因为当前文件组消息均是未消费的
    FuncTool::RemoveFile(strConsumeFile2.c_str());
    return SUCCESS;
}

int PersistenceServer::Run()
{
    while (!m_bStop)
    {
        int iDateRecvCount = 0;
        char pBuff[MAX_CLINT_PACKAGE_LENGTH];
        bool bShmQueueEmpty = false;
        PersisServerLogger.WriteLog(mq_log_info, "ready ro recv data...");
        PersisServerLogger.Print(mq_log_info, "ready ro recv data...");
        while (true)
        {
            memset(pBuff, 0, MAX_CLINT_PACKAGE_LENGTH);
            int iLen = MAX_CLINT_PACKAGE_LENGTH;
            int ret = m_pQueueFromLogic->Dequeue(pBuff, &iLen);
            if (ret == ShmQueue::ERR_SHM_QUEUE_EMPTY)
            {
                bShmQueueEmpty = (iDateRecvCount == 0);
                break;
            }
            else if (ret != ShmQueue::SUCCESS)
            {
                continue;
            }
            ++iDateRecvCount;

            // 包长度检查
            if (iLen < (int)sizeof(unsigned short))
            {
                continue;
            }
            unsigned short iPackLen;
            char *pTemp = pBuff;
            int offset = FuncTool::ReadShort(pTemp, iPackLen);
            if (iPackLen != iLen)
            {
                continue;
            }

            // 读取包类型
            unsigned short iCmdId;
            pTemp += offset;
            offset = FuncTool::ReadShort(pTemp, iCmdId);
            pTemp += offset;
            PersisServerLogger.WriteLog(mq_log_info, "recv pack from conSrv,len is %d cmd type is %d", iLen, iCmdId);
            PersisServerLogger.Print(mq_log_info, "recv pack from conSrv,len is %d cmd type is %d", iLen, iCmdId);
            switch (iCmdId)
            {
            case CMD_CREATE_EXCNANGE:
            {
                OnCreateExchange(pTemp, iLen - sizeof(unsigned short) - sizeof(unsigned short));
                break;
            }
            case CMD_CREATE_QUEUE:
            {
                OnCreateQueue(pTemp, iLen - sizeof(unsigned short) - sizeof(unsigned short));
                break;
            }
            case CMD_DELETE_EXCHANGE:
            {
                OnDeleteExchange(pTemp, iLen - sizeof(unsigned short) - sizeof(unsigned short));
                break;
            }
            case CMD_DELETE_QUEUE:
            {
                OnDeleteQueue(pTemp, iLen - sizeof(unsigned short) - sizeof(unsigned short));
                break;
            }
            case CMD_CREATE_SUBCRIBE:
            case CMD_CANCEL_SUBCRIBE:
            {
                OnSubcribe(pTemp, iLen - sizeof(unsigned short) - sizeof(unsigned short));
                break;
            }
            case CMD_CREATE_BINDING:
            {
                OnBinding(pTemp, iLen - sizeof(unsigned short) - sizeof(unsigned short));
                break;
            }
            case CMD_CREATE_PUBLISH:
            {
                // 读取队列名称
                char pQueueName[MAX_NAME_LENGTH];
                memset(pQueueName, 0, sizeof(pQueueName));
                offset = FuncTool::ReadBuf(pTemp, pQueueName, sizeof(pQueueName));
                string strQueueName(pQueueName);
                // 读持久化序号
                pTemp += MAX_NAME_LENGTH;
                int iDurableIndex = 0;
                offset = FuncTool::ReadInt(pTemp, iDurableIndex);
                OnPublishMessage(pBuff, iLen, strQueueName, iDurableIndex);
                break;
            }
            case CMD_CREATE_RECV:
            case CMD_SERVER_PUSH_MESSAGE:
            case CMD_CLIENT_ACK_MESSAGE:
            {
                OnConsumeMessage(pTemp, iLen - sizeof(unsigned short) - sizeof(unsigned short));
                break;
            }
            default:
                break;
            }
        }
        if (bShmQueueEmpty)
        {
            // 文件清理
            int iCount = 0;
            ClearUpFile(iCount);
            if (iCount == 0)
            {
                // 休息一段时间
                usleep(1000 * 10);
            }
        }
    }
    return SUCCESS;
}

// 初始化一个持久化服务器，并将其设置为后台运行，同时记录所有重要的操作步骤和结果
int main(int argc, char *argv[])
{
    // 初始化log对象
    char pLogPath[MQ_MAX_PATH_LEN] = DEFAULT_PERSIS_SERVER_LOG_PATH; //./mq_logicSrv_log/
    if (PersisServerLogger.Init(pLogPath) != Logger::SUCCESS)
    {
        printf("logger init failed!");
        return -1;
    }
    // 后台运行进程
    PersisServerLogger.WriteLog(mq_log_info, "persis server init...");
    PersisServerLogger.Print(mq_log_info, "persis server init...");
    int ret = 0;
    if ((ret = FuncTool::DaemonInit()) != FuncTool::SUCCESS)
    {
        PersisServerLogger.WriteLog(mq_log_err, "Daemoninit faild!,return value is %d", ret);
        PersisServerLogger.Print(mq_log_err, "Daemoninit faild!,return value is %d", ret);
        return -1;
    }
    // 开启服务
    PersisServerLogger.WriteLog(mq_log_info, "persis server run...");
    PersisServerLogger.Print(mq_log_info, "persis server run...");
    if ((ret = PersistenceServer::GetInstance()->Init()) != PersistenceServer::SUCCESS)
    {
        fprintf(stderr, "persis server init failed!");
        return -1;
    }
    PersistenceServer::GetInstance()->Run();
}