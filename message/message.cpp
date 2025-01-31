// c/c++
#include <string.h>
// linux

// user define
#include "message.h"
#include "../mq_util/mq_util.h"

using namespace WSMQ;

// 使用初始化列表，构造函数可以直接对成员变量进行赋值
CreateExchangeMessage::CreateExchangeMessage(const string &istrName, unsigned short iExchangeType, bool ibDurable /*=false*/, bool ibAutoDel /*=true*/)
    : Message(CMD_CREATE_EXCNANGE), m_strExchangeName(istrName), m_iExchangeType(iExchangeType), m_bDurable(ibDurable), m_bAutoDel(ibAutoDel) // comdef.h 标记为1
{
}

// 将对象的数据打包到指定的缓冲区
int CreateExchangeMessage::GetMessagePack(char *ipBuf, int *iopBuffLen)
{
    if (m_strExchangeName.size() > MAX_NAME_LENGTH) // comdef.h 32
    {
        return ERR_MSG_NAME_TOO_LONG; // 名字太长返回-500
    }
    // sizeof(ClientPackageHead)：这是ClientPackageHead结构体的大小，包含网络传输的包头信息，占据一定的字节。
    // MAX_NAME_LENGTH：用于存储字符串名称的缓冲区的大小
    // sizeof(m_iExchangeType)：用于存储交换类型的值
    // sizeof(m_bDurable)：用于指示消息是否持久化的标志位
    // sizeof(m_bAutoDel)：用于指示是否自动删除消息的标志位
    unsigned short iMsgLen = sizeof(ClientPackageHead) + MAX_NAME_LENGTH + sizeof(m_iExchangeType) + sizeof(m_bDurable) + sizeof(m_bAutoDel); // comdef.h ClientPackageHead网络包头部大小
    if (*iopBuffLen < iMsgLen)
    {
        return ERR_MSG_BUF_TOO_SMALL; // 能够放入的消息太短返回-501
    }
    char *pTemp = ipBuf; // 函数输入
    // 写包长度
    int offset = FuncTool::WriteShort(pTemp, iMsgLen); // mq_util.h   staic int WriteShort(void *ipBuffer, unsigned short iVal, int iToNetOrder = 1);
    // 数组下标
    pTemp += offset;
    offset = FuncTool::WriteInt(pTemp, m_iIndex);
    // 写消息类型
    pTemp += offset;
    offset = FuncTool::WriteShort(pTemp, m_iCmdId);
    // 写名称
    pTemp += offset;
    offset = FuncTool::WriteBuf(pTemp, m_strExchangeName.c_str(), m_strExchangeName.size());
    // 写exchange 类型
    pTemp += MAX_NAME_LENGTH;
    offset = FuncTool::WriteShort(pTemp, m_iExchangeType);
    // 写持久化信息
    pTemp += offset;
    offset = FuncTool::WriteBool(pTemp, m_bDurable);
    // 写自动删除信息
    pTemp += offset;
    offset = FuncTool::WriteBool(pTemp, m_bAutoDel);
    *iopBuffLen = iMsgLen; // 将创建信息长度赋值
    return SUCCESS;
}

CreateQueueMessage::CreateQueueMessage(const string &istrName, short iPriority /*=-1*/, bool ibDurable /*=false*/, bool ibAutoDel /*=true*/)
    : Message(CMD_CREATE_QUEUE), m_strQueueName(istrName), m_iPriority(iPriority), m_bDurable(ibDurable), m_bAutoDel(ibAutoDel) // 队列为2
{
}

// 将消息打包到指定的缓冲区ipBuf中
int CreateQueueMessage::GetMessagePack(char *ipBuf, int *iopBuffLen)
{
    if (m_strQueueName.size() > MAX_NAME_LENGTH)
    {
        return ERR_MSG_NAME_TOO_LONG;
    }
    unsigned short iMsgLen = sizeof(ClientPackageHead) + MAX_NAME_LENGTH + sizeof(m_iPriority) + sizeof(m_bDurable) + sizeof(m_bAutoDel);
    if (*iopBuffLen < iMsgLen)
    {
        return ERR_MSG_BUF_TOO_SMALL;
    }
    char *pTemp = ipBuf;
    // 写包长度
    int offset = FuncTool::WriteShort(pTemp, iMsgLen);
    // 数组下标
    pTemp += offset;
    offset = FuncTool::WriteInt(pTemp, m_iIndex);
    // 写消息类型
    pTemp += offset;
    offset = FuncTool::WriteShort(pTemp, m_iCmdId);
    // 写名称
    pTemp += offset;
    offset = FuncTool::WriteBuf(pTemp, m_strQueueName.c_str(), m_strQueueName.size());
    // 写优先级
    pTemp += MAX_NAME_LENGTH;
    offset = FuncTool::WriteShort(pTemp, m_iPriority);
    // 写持久化信息
    pTemp += offset;
    offset = FuncTool::WriteBool(pTemp, m_bDurable);
    // 写自动删除信息
    pTemp += offset;
    offset = FuncTool::WriteBool(pTemp, m_bAutoDel);
    *iopBuffLen = iMsgLen;
    return SUCCESS;
}

CreateBindingMessage::CreateBindingMessage(const string &istrExName, const string &istrQueueName, const string &istrKey)
    : Message(CMD_CREATE_BINDING), m_strExchangeName(istrExName), m_strQueueName(istrQueueName), m_strBindingKey(istrKey)
{
}

// 将消息打包到指定的缓冲区ipBuf中
int CreateBindingMessage::GetMessagePack(char *ipBuf, int *iopBuffLen)
{
    // 检查交换机名称、队列名称和绑定键的长度是否超过最大限制
    if (m_strExchangeName.size() > MAX_NAME_LENGTH)
    {
        return ERR_MSG_NAME_TOO_LONG;
    }
    if (m_strQueueName.size() > MAX_NAME_LENGTH)
    {
        return ERR_MSG_NAME_TOO_LONG;
    }
    if (m_strBindingKey.size() > MAX_NAME_LENGTH)
    {
        return ERR_MSG_NAME_TOO_LONG;
    }
    unsigned short iMsgLen = sizeof(ClientPackageHead) + MAX_NAME_LENGTH + MAX_NAME_LENGTH + MAX_NAME_LENGTH;
    if (*iopBuffLen < iMsgLen)
    {
        return ERR_MSG_BUF_TOO_SMALL;
    }
    char *pTemp = ipBuf;
    // 写包长度
    int offset = FuncTool::WriteShort(pTemp, iMsgLen);
    // 数组下标
    pTemp += offset;
    offset = FuncTool::WriteInt(pTemp, m_iIndex);
    // 写消息类型
    pTemp += offset;
    offset = FuncTool::WriteShort(pTemp, m_iCmdId);
    // 写exchange名称
    pTemp += offset;
    offset = FuncTool::WriteBuf(pTemp, m_strExchangeName.c_str(), m_strExchangeName.size());
    // 写queue名称
    pTemp += MAX_NAME_LENGTH;
    offset = FuncTool::WriteBuf(pTemp, m_strQueueName.c_str(), m_strQueueName.size());
    // 写bindingkey
    pTemp += MAX_NAME_LENGTH;
    offset = FuncTool::WriteBuf(pTemp, m_strBindingKey.c_str(), m_strBindingKey.size());
    *iopBuffLen = iMsgLen;
    return SUCCESS;
}

CreatePublishMessage::CreatePublishMessage(const string &istrExName, const string &istrKey,
                                           const string &istrMsgBody, short iPriority /*=-1*/, bool ibDurable /*=false*/, unsigned char iConfirmLevel /*=0*/)
    : Message(CMD_CREATE_PUBLISH), m_strExchangeName(istrExName), m_strRoutingKey(istrKey), m_strMsgBody(istrMsgBody),
      m_iPriority(iPriority), m_bDurable(ibDurable), m_iMsgSeq(-1), m_iConfirmLevel(iConfirmLevel)
{
}

int CreatePublishMessage::GetMessagePack(char *ipBuf, int *iopBuffLen)
{
    if (m_strExchangeName.size() > MAX_NAME_LENGTH)
    {
        return ERR_MSG_NAME_TOO_LONG;
    }
    if (m_strRoutingKey.size() > MAX_NAME_LENGTH)
    {
        return ERR_MSG_NAME_TOO_LONG;
    }
    unsigned short iMsgLen = sizeof(ClientPackageHead) + MAX_NAME_LENGTH + MAX_NAME_LENGTH + sizeof(m_iPriority) + sizeof(m_bDurable) + sizeof(m_iMsgSeq) + sizeof(m_iConfirmLevel) + m_strMsgBody.size();
    if (*iopBuffLen < iMsgLen)
    {
        return ERR_MSG_BUF_TOO_SMALL;
    }
    char *pTemp = ipBuf;
    // 写包长度
    int offset = FuncTool::WriteShort(pTemp, iMsgLen);
    // 数组下标
    pTemp += offset;
    offset = FuncTool::WriteInt(pTemp, m_iIndex);
    // 写消息类型
    pTemp += offset;
    offset = FuncTool::WriteShort(pTemp, m_iCmdId);
    // 写exchange名称
    pTemp += offset;
    offset = FuncTool::WriteBuf(pTemp, m_strExchangeName.c_str(), m_strExchangeName.size());
    // 写routingkey
    pTemp += MAX_NAME_LENGTH;
    offset = FuncTool::WriteBuf(pTemp, m_strRoutingKey.c_str(), m_strRoutingKey.size());
    // 写优先级
    pTemp += MAX_NAME_LENGTH;
    offset = FuncTool::WriteShort(pTemp, m_iPriority);
    // 写持久化信息
    pTemp += offset;
    offset = FuncTool::WriteBool(pTemp, m_bDurable);
    // 写消息序号
    pTemp += offset;
    offset = FuncTool::WriteInt(pTemp, m_iMsgSeq);
    // 写消息确认级别
    pTemp += offset;
    offset = FuncTool::WriteByte(pTemp, m_iConfirmLevel);
    // 写消息体
    pTemp += offset;
    offset = FuncTool::WriteBuf(pTemp, m_strMsgBody.c_str(), m_strMsgBody.size());
    *iopBuffLen = iMsgLen;
    return SUCCESS;
}

CreateRecvMessage::CreateRecvMessage(const string &istrQueueName, unsigned char iConfirmLevel /*=0*/)
    : Message(CMD_CREATE_RECV), m_strQueueName(istrQueueName), m_iConfirmLevel(iConfirmLevel)
{
}

int CreateRecvMessage::GetMessagePack(char *ipBuf, int *iopBuffLen)
{
    if (m_strQueueName.size() > MAX_NAME_LENGTH)
    {
        return ERR_MSG_NAME_TOO_LONG;
    }
    unsigned short iMsgLen = sizeof(ClientPackageHead) + MAX_NAME_LENGTH + sizeof(m_iConfirmLevel);
    if (*iopBuffLen < iMsgLen)
    {
        return ERR_MSG_BUF_TOO_SMALL;
    }
    char *pTemp = ipBuf;
    // 写包长度
    int offset = FuncTool::WriteShort(pTemp, iMsgLen);
    // 数组下标
    pTemp += offset;
    offset = FuncTool::WriteInt(pTemp, m_iIndex);
    // 写消息类型
    pTemp += offset;
    offset = FuncTool::WriteShort(pTemp, m_iCmdId);
    // 写queue名称
    pTemp += offset;
    offset = FuncTool::WriteBuf(pTemp, m_strQueueName.c_str(), m_strQueueName.size());
    // 写确认级别
    pTemp += MAX_NAME_LENGTH;
    offset = FuncTool::WriteByte(pTemp, m_iConfirmLevel);
    *iopBuffLen = iMsgLen;
    return SUCCESS;
}

CreateSubscribeMessage::CreateSubscribeMessage(const string &istrQueueName, unsigned char iConfirmLevel /*=0*/)
    : Message(CMD_CREATE_SUBCRIBE), m_strQueueName(istrQueueName), m_iConfirmLevel(iConfirmLevel)
{
}

int CreateSubscribeMessage::GetMessagePack(char *ipBuf, int *iopBuffLen)
{
    if (m_strQueueName.size() > MAX_NAME_LENGTH)
    {
        return ERR_MSG_NAME_TOO_LONG;
    }
    unsigned short iMsgLen = sizeof(ClientPackageHead) + MAX_NAME_LENGTH + sizeof(m_iConfirmLevel);
    if (*iopBuffLen < iMsgLen)
    {
        return ERR_MSG_BUF_TOO_SMALL;
    }
    char *pTemp = ipBuf;
    // 写包长度
    int offset = FuncTool::WriteShort(pTemp, iMsgLen);
    // 数组下标
    pTemp += offset;
    offset = FuncTool::WriteInt(pTemp, m_iIndex);
    // 写消息类型
    pTemp += offset;
    offset = FuncTool::WriteShort(pTemp, m_iCmdId);
    // 写queue名称
    pTemp += offset;
    offset = FuncTool::WriteBuf(pTemp, m_strQueueName.c_str(), m_strQueueName.size());
    // 写确认级别
    pTemp += MAX_NAME_LENGTH;
    offset = FuncTool::WriteByte(pTemp, m_iConfirmLevel);

    *iopBuffLen = iMsgLen;
    return SUCCESS;
}

int DeleteExchangeMessage::GetMessagePack(char *ipBuf, int *iopBuffLen)
{
    if (m_strExchangeName.size() > MAX_NAME_LENGTH)
    {
        return ERR_MSG_NAME_TOO_LONG;
    }
    unsigned short iMsgLen = sizeof(ClientPackageHead) + MAX_NAME_LENGTH;
    if (*iopBuffLen < iMsgLen)
    {
        return ERR_MSG_BUF_TOO_SMALL;
    }
    char *pTemp = ipBuf;
    // 写包长度
    int offset = FuncTool::WriteShort(pTemp, iMsgLen);
    // 数组下标
    pTemp += offset;
    offset = FuncTool::WriteInt(pTemp, m_iIndex);
    // 写消息类型
    pTemp += offset;
    offset = FuncTool::WriteShort(pTemp, m_iCmdId);
    // 写名称
    pTemp += offset;
    offset = FuncTool::WriteBuf(pTemp, m_strExchangeName.c_str(), m_strExchangeName.size());
    *iopBuffLen = iMsgLen;
    return SUCCESS;
}

int DeleteQueueMessage::GetMessagePack(char *ipBuf, int *iopBuffLen)
{
    if (m_strQueueName.size() > MAX_NAME_LENGTH)
    {
        return ERR_MSG_NAME_TOO_LONG;
    }
    unsigned short iMsgLen = sizeof(ClientPackageHead) + MAX_NAME_LENGTH;
    if (*iopBuffLen < iMsgLen)
    {
        return ERR_MSG_BUF_TOO_SMALL;
    }
    char *pTemp = ipBuf;
    // 写包长度
    int offset = FuncTool::WriteShort(pTemp, iMsgLen);
    // 数组下标
    pTemp += offset;
    offset = FuncTool::WriteInt(pTemp, m_iIndex);
    // 写消息类型
    pTemp += offset;
    offset = FuncTool::WriteShort(pTemp, m_iCmdId);
    // 写名称
    pTemp += offset;
    offset = FuncTool::WriteBuf(pTemp, m_strQueueName.c_str(), m_strQueueName.size());
    *iopBuffLen = iMsgLen;
    return SUCCESS;
}

int CancelSubscribeMessage::GetMessagePack(char *ipBuf, int *iopBuffLen)
{
    if (m_strQueueName.size() > MAX_NAME_LENGTH)
    {
        return ERR_MSG_NAME_TOO_LONG;
    }
    unsigned short iMsgLen = sizeof(ClientPackageHead) + MAX_NAME_LENGTH;
    if (*iopBuffLen < iMsgLen)
    {
        return ERR_MSG_BUF_TOO_SMALL;
    }
    char *pTemp = ipBuf;
    // 写包长度
    int offset = FuncTool::WriteShort(pTemp, iMsgLen);
    // 数组下标
    pTemp += offset;
    offset = FuncTool::WriteInt(pTemp, m_iIndex);
    // 写消息类型
    pTemp += offset;
    offset = FuncTool::WriteShort(pTemp, m_iCmdId);
    // 写名称
    pTemp += offset;
    offset = FuncTool::WriteBuf(pTemp, m_strQueueName.c_str(), m_strQueueName.size());
    return SUCCESS;
}

SeverStoreMessage::SeverStoreMessage(const string &istrMsgBody, short iPriority /*=-1*/, bool ibDurable /*=false*/)
    : Message(CMD_CLIENT_PULL_MESSAGE), m_strMsgBody(istrMsgBody), m_iPriority(iPriority), m_bDurable(ibDurable)
{
    m_iConfirmLevel = CONSUMER_NO_ACK;
    m_iMsgSeq = -1;
    m_iDurableIndex = -1;
}

int SeverStoreMessage::GetMessagePack(char *ipBuf, int *iopBuffLen)
{
    char *pTemp = ipBuf;
    unsigned short iMsgLen = sizeof(ClientPackageHead) + sizeof(m_iConfirmLevel) + sizeof(m_iMsgSeq) + m_strMsgBody.size();
    if (*iopBuffLen < iMsgLen)
    {
        return ERR_MSG_BUF_TOO_SMALL;
    }
    // 写包长度
    int offset = FuncTool::WriteShort(pTemp, iMsgLen);
    // 数组下标
    pTemp += offset;
    offset = FuncTool::WriteInt(pTemp, m_iIndex);
    // 写消息类型
    pTemp += offset;
    offset = FuncTool::WriteShort(pTemp, m_iCmdId);
    // 写确认级别
    pTemp += offset;
    offset = FuncTool::WriteByte(pTemp, m_iConfirmLevel);
    // 写消息序号
    pTemp += offset;
    offset = FuncTool::WriteInt(pTemp, m_iMsgSeq);
    // 写消息体
    pTemp += offset;
    offset = FuncTool::WriteBuf(pTemp, m_strMsgBody.c_str(), m_strMsgBody.size());
    *iopBuffLen = iMsgLen;
    return SUCCESS;
}

// 将持久化消息序列化为字符串格式，并存储在指定的字符缓冲区ipBuffer中，同时更新iBuffLen以记录实际序列化的字符串长度
int SeverStoreMessage::SerializeDurableToString(char *ipBuffer, int &iBuffLen)
{
    unsigned short iLen = sizeof(unsigned short) + sizeof(unsigned short) + MAX_NAME_LENGTH + sizeof(m_iDurableIndex) + sizeof(m_iPriority) + m_strMsgBody.size();
    if (iLen > iBuffLen)
    {
        return ERROR;
    }
    char *pBuff = ipBuffer;
    // 写包长度
    int offset = FuncTool::WriteShort(pBuff, iLen);
    pBuff += offset;
    // 写消息类型
    offset = FuncTool::WriteShort(pBuff, CMD_CREATE_PUBLISH);
    pBuff += offset;
    // 写queue名称
    offset = FuncTool::WriteBuf(pBuff, m_strQueueName.c_str(), m_strQueueName.size());
    pBuff += MAX_NAME_LENGTH;
    // 写持久化索引
    offset = FuncTool::WriteInt(pBuff, m_iDurableIndex);
    pBuff += offset;
    // 写优先级
    offset = FuncTool::WriteShort(pBuff, m_iPriority);
    pBuff += offset;
    // 写消息体
    offset = FuncTool::WriteBuf(pBuff, m_strMsgBody.c_str(), m_strMsgBody.size());
    iBuffLen = iLen;
    return SUCCESS;
}

// 服务器回复消息的类
ActReplyMessage::ActReplyMessage(bool ibSucceed, const string &istrMsgBody)
    : Message(CMD_SERVER_REPLY_MESSAGE), m_bSucceed(ibSucceed), m_strMsgBody(istrMsgBody)
{
}

int ActReplyMessage::GetMessagePack(char *ipBuf, int *iopBuffLen)
{
    char *pTemp = ipBuf;
    unsigned short iMsgLen = sizeof(ClientPackageHead) + sizeof(m_bSucceed) + m_strMsgBody.size() + 1;
    if (*iopBuffLen < iMsgLen)
    {
        return ERR_MSG_BUF_TOO_SMALL;
    }
    // 写包长度
    int offset = FuncTool::WriteShort(pTemp, iMsgLen);
    // 数组下标
    pTemp += offset;
    offset = FuncTool::WriteInt(pTemp, m_iIndex);
    // 写消息类型
    pTemp += offset;
    offset = FuncTool::WriteShort(pTemp, m_iCmdId);
    // 写成功标识
    pTemp += offset;
    offset = FuncTool::WriteBool(pTemp, m_bSucceed);
    // 写消息体
    pTemp += offset;
    offset = FuncTool::WriteBuf(pTemp, m_strMsgBody.c_str(), m_strMsgBody.size());
    *iopBuffLen = iMsgLen;
    return SUCCESS;
}

//客户端确认消息的类
ClientAckMessage::ClientAckMessage(unsigned char iConfirmLevel, int iAckSeq) : Message(CMD_CLIENT_ACK_MESSAGE), m_iConfirmLevel(iConfirmLevel), m_iAckSeq(iAckSeq)
{
}

int ClientAckMessage::GetMessagePack(char *ipBuf, int *iopBuffLen)
{
    char *pTemp = ipBuf;
    unsigned short iMsgLen = sizeof(ClientPackageHead) + sizeof(m_iConfirmLevel) + sizeof(m_iAckSeq);
    if (*iopBuffLen < iMsgLen)
    {
        return ERR_MSG_BUF_TOO_SMALL;
    }
    // 写包长度
    int offset = FuncTool::WriteShort(pTemp, iMsgLen);
    // 数组下标
    pTemp += offset;
    offset = FuncTool::WriteInt(pTemp, m_iIndex);
    // 写消息类型
    pTemp += offset;
    offset = FuncTool::WriteShort(pTemp, m_iCmdId);
    // 写确认级别
    pTemp += offset;
    offset = FuncTool::WriteByte(pTemp, m_iConfirmLevel);
    // 写确认号
    pTemp += offset;
    offset = FuncTool::WriteInt(pTemp, m_iAckSeq);
    *iopBuffLen = iMsgLen;
    return SUCCESS;
}

//服务器确认消息的类
ServerAckMessage::ServerAckMessage(int iAckSeq) : Message(CMD_SERVER_ACK_MESSAGE), m_iAckSeq(iAckSeq)
{
}

int ServerAckMessage::GetMessagePack(char *ipBuf, int *iopBuffLen)
{
    char *pTemp = ipBuf;
    unsigned short iMsgLen = sizeof(ClientPackageHead) + sizeof(m_iAckSeq);
    if (*iopBuffLen < iMsgLen)
    {
        return ERR_MSG_BUF_TOO_SMALL;
    }
    // 写包长度
    int offset = FuncTool::WriteShort(pTemp, iMsgLen);
    // 数组下标
    pTemp += offset;
    offset = FuncTool::WriteInt(pTemp, m_iIndex);
    // 写消息类型
    pTemp += offset;
    offset = FuncTool::WriteShort(pTemp, m_iCmdId);
    // 写确认号
    pTemp += offset;
    offset = FuncTool::WriteInt(pTemp, m_iAckSeq);
    *iopBuffLen = iMsgLen;
    return SUCCESS;
}

//客户端退出消息的类
ClientExitMessage::ClientExitMessage(int iClientIndex) : Message(CMD_CLIENT_EXIT)
{
    m_iIndex = iClientIndex;
}

int ClientExitMessage::GetMessagePack(char *ipBuf, int *iopBuffLen)
{
    char *pTemp = ipBuf;
    unsigned short iMsgLen = sizeof(ClientPackageHead);
    if (*iopBuffLen < iMsgLen)
    {
        return ERR_MSG_BUF_TOO_SMALL;
    }
    // 写包长度
    int offset = FuncTool::WriteShort(pTemp, iMsgLen);
    // 数组下标
    pTemp += offset;
    offset = FuncTool::WriteInt(pTemp, m_iIndex);
    // 写消息类型
    pTemp += offset;
    offset = FuncTool::WriteShort(pTemp, m_iCmdId);
    return SUCCESS;
}