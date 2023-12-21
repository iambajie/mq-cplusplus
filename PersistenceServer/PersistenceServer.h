#ifndef INCLUDE_PERSISTENCE_H
#define INCLUDE_PERSISTENCE_H
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include "../shm_queue/shm_queue.h"
using std::list;
using std::string;
using std::unordered_map;
using std::unordered_set;
using std::vector;
namespace WSMQ
{
    class PersistenceServer
    {
    public:
        const static int SUCCESS = 0;
        const static int ERROR = -1;

    public:
        static PersistenceServer *GetInstance();
        static void Destroy();
        int Init();
        int Run();

    protected:
        int InitConf(const char *ipPath);
        PersistenceServer();
        ~PersistenceServer();
        int InitSigHandler();
        static void SigTermHandler(int iSig) { m_bStop = true; }
        int OnCreateExchange(char *ipBuffer, int iLen);
        int OnCreateQueue(char *ipBuffer, int iLen);
        int OnDeleteExchange(char *ipBuffer, int iLen);
        int OnDeleteQueue(char *ipBuffer, int iLen);
        int OnSubcribe(char *ipBuffer, int iLen);
        int OnBinding(char *ipBuffer, int iLen);
        int OnPublishMessage(char *ipBuffer, int iLen, string istrQueueName, int iDurableIndex);
        int OnConsumeMessage(char *ipBuffer, int iLen);
        string ConvertIndexToString(int iIndex);
        // 初始化文件信息
        int InitFileInfo();
        // 找到文件夹中所有的index文件名称
        int FindIndexFiles(const char *ipDir, vector<string> &ovIndexFiles);
        // 处理index文件
        int ProcessIndexFile(string istrQueueName, const char *ipFile);
        // 清理文件，将连续的消费数目超过50%的文件进行合并
        int ClearUpFile(int &oClearCount);
        // 计算该组文件消费比率
        double CalculateCondsumeRate(const char *ipFile);
        // 合并文件
        int MergeFiles(const char *ipQueueName, const char *ipFileName1, const char *ipFileName2);

    protected:
        // 有一个名为 "MyQueue" 的消息队列，与此队列相关的文件包括 "File1", "File2" 和 "File3"。
        // 当前正在写入的文件是 "File3"，消费者的消费索引指向 "File2"
        unordered_map<string, string> m_mMsgQueueTailFile;                      // 跟踪每个队列当前正在写入的文件
                                                                                //{
                                                                                //     "MyQueue": "File3"
                                                                                // }
        unordered_map<string, unordered_map<int, string>> m_mMsgQueueIndexFile; // 存储每个队列中各个消费索引对应的文件
                                                                                // {
                                                                                //     "MyQueue": {
                                                                                //         1: "File2"
                                                                                //     }
                                                                                // }

        unordered_map<string, list<string>> m_mMsgQueueList; // 每个队列所有组文件名链表，从小到大排序
                                                             //{
                                                             //     "MyQueue": ["File1", "File2", "File3"]
                                                             // }
        ShmQueue *m_pQueueFromLogic;
        ShmQueue *m_pQueueToLogic;
        static bool m_bStop;
        static PersistenceServer *m_pPersistenceServer;
    };
    bool PersistenceServer::m_bStop = true;
    PersistenceServer *PersistenceServer::m_pPersistenceServer = NULL;
}

#endif // INCLUDE_PERSISTENCE_H
