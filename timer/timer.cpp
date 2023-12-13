
// c/c++
#include <stdlib.h>
// linux

// user define
#include "timer.h"

using namespace WSMQ;

// 开始计时
int BaseTimer::Begain()
{
    gettimeofday(&m_tBegainTime, NULL);
    return SUCCESS;
}

int ConnectSrvTimer::EpollDown()
{
    gettimeofday(&m_tEpollDoneTime, NULL);
    // 记录 epoll 完成的时间，并计算从 Begain 到 EpollDown 的耗时，单位是微秒
    m_iEpollTime = ((m_tEpollDoneTime.tv_sec - m_tBegainTime.tv_sec) * 1000000 + (m_tEpollDoneTime.tv_usec - m_tBegainTime.tv_usec)) / 1000;
    return SUCCESS;
}

int ConnectSrvTimer::QueueDataDown()
{
    gettimeofday(&m_tShmQueueDataDoneTime, NULL);
    // 记录队列数据完成的时间，并计算从 EpollDown 到 QueueDataDown的耗时，单位是微秒
    m_iQueueDataTime = ((m_tShmQueueDataDoneTime.tv_sec - m_tEpollDoneTime.tv_sec) * 1000000 + (m_tShmQueueDataDoneTime.tv_usec - m_tEpollDoneTime.tv_usec)) / 1000;
    return SUCCESS;
}

int ConnectSrvTimer::GetMaxTimeForQueueData()
{
    // 为队列数据处理预留的最大时间
    m_iMaxTimeForQueueData = 100 - m_iEpollTime;
    if (m_iMaxTimeForQueueData < 50)
    {
        m_iMaxTimeForQueueData = 100;
    }
    return m_iMaxTimeForQueueData;
}

bool ConnectSrvTimer::HaveTimeForQueueData()
{
    struct timeval now;
    gettimeofday(&now, NULL);
    // 检查是否还有时间用于队列数据。方法是获取当前时间并计算从 EpollDown 到现在的时间
    return ((now.tv_sec - m_tEpollDoneTime.tv_sec) * 1000000 + (now.tv_usec - m_tEpollDoneTime.tv_usec)) / 1000 < m_iMaxTimeForQueueData;
}

int LogicSrvTimer::PushMessageDown()
{
    gettimeofday(&m_tPushDoneTime, NULL);
    // 记录消息推送完成的时间，并计算从 Begain 到 PushMessageDown的耗时
    m_iPushTime = ((m_tPushDoneTime.tv_sec - m_tBegainTime.tv_sec) * 1000000 + (m_tPushDoneTime.tv_usec - m_tBegainTime.tv_usec)) / 1000;
    return SUCCESS;
}

int LogicSrvTimer::QueueDataDown()
{
    gettimeofday(&m_tShmQueueDataDoneTime, NULL);
    // 记录队列数据完成的时间，并计算从 PushMessageDown 到 QueueDataDown的耗时
    m_iQueueDataTime = ((m_tShmQueueDataDoneTime.tv_sec - m_tPushDoneTime.tv_sec) * 1000000 + (m_tShmQueueDataDoneTime.tv_usec - m_tPushDoneTime.tv_usec)) / 1000;
    return SUCCESS;
}

int LogicSrvTimer::GetMaxTimeForQueueData()
{
    // 为队列数据处理预留的最大时间
    m_iMaxTimeForQueueData = 100 - m_iPushTime;
    if (m_iMaxTimeForQueueData < 50)
    {
        m_iMaxTimeForQueueData = 100;
    }
    return m_iMaxTimeForQueueData;
}

bool LogicSrvTimer::HaveTimeForQueueData()
{
    struct timeval now;
    gettimeofday(&now, NULL);
    // 检查是否还有时间用于队列数据
    return ((now.tv_sec - m_tPushDoneTime.tv_sec) * 1000000 + (now.tv_usec - m_tPushDoneTime.tv_usec)) / 1000 < m_iMaxTimeForQueueData;
}