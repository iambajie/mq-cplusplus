// c/c++
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
// linux
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
// user define
#include "logger.h"

using namespace WSMQ;

Logger::Logger()
{
    m_pLogFile = NULL;
    memset(m_pLogPath, 0, MQ_MAX_PATH_LEN);
}

Logger::~Logger()
{
    if (m_pLogFile)
    {
        fclose(m_pLogFile);
    }
}

// 将日志文件的路径和名称设置为带有时间戳的特定格式[path/to/log/directory]/YYYYMMDD.log ，并创建对应的文件供后续日志记录使用
int Logger::Init(char pLogPath[MQ_MAX_PATH_LEN])
{
    memcpy(m_pLogPath, pLogPath, MQ_MAX_PATH_LEN);

    int ret = SUCCESS;
    umask(0);

    struct stat stBuf;
    if (lstat(m_pLogPath, &stBuf) < 0)
    {
        if (mkdir(m_pLogPath, 0777) == -1)
        {
            ret = ERR_LOG_INIT;
            printf("mkdir failed,errMsg is %s", strerror(errno));
        }
    }
    // 获取当前日期作为文件名称
    struct tm *tLocal;
    time_t now;
    time(&now);
    tLocal = localtime(&now);
    char pYear[5];
    memset(pYear, 0, sizeof(pYear));
    sprintf(pYear, "%d", tLocal->tm_year + 1900);
    strcat(m_pLogPath, pYear);
    char pMon[5];
    memset(pMon, 0, sizeof(pMon));
    sprintf(pMon, "%d", tLocal->tm_mon + 1);
    strcat(m_pLogPath, pMon);
    char pDay[5];
    memset(pDay, 0, sizeof(pDay));
    sprintf(pDay, "%d", tLocal->tm_mday);
    strcat(m_pLogPath, pDay);
    strcat(m_pLogPath, ".log");
    // 写方式打开创建文件
    m_pLogFile = fopen(m_pLogPath, "w+");
    if (NULL == m_pLogFile)
    {
        ret = ERR_LOG_INIT;
    }
    umask(0022);
    return SUCCESS;
}

// 将日志消息写入到指定的日志文件
// 通过将时间戳、日志级别和具体日志消息格式化后写入日志文件，实现了日志记录的功能
// 具体的日志消息内容由调用者提供的格式字符串和可变参数列表决定
int Logger::WriteLog(mq_log_level iLevel, const char *pFmt, ...)
{
    va_list ap;
    va_start(ap, pFmt);
    time_t now = time(NULL);
    char pBuf[64];
    strftime(pBuf, 64, "[%d %b %H:%M:%S]", gmtime(&now));
    char pLogLevel[10];
    if (iLevel == mq_log_err)
    {
        memcpy(pLogLevel, ERR_LOG, sizeof(pLogLevel));
    }
    else if (iLevel == mq_log_warn)
    {
        memcpy(pLogLevel, WARN_LOG, sizeof(pLogLevel));
    }
    else
    {
        memcpy(pLogLevel, INFO_LOG, sizeof(pLogLevel));
    }

    fprintf(m_pLogFile, "%s: %s ", pBuf, pLogLevel);
    // 将格式化的日志消息写入日志文件
    vfprintf(m_pLogFile, pFmt, ap);
    fprintf(m_pLogFile, "\n");
    fflush(m_pLogFile);
    va_end(ap);
    return SUCCESS;
}

// 将日志消息打印到标准输出（控制台）上
// 通过将时间戳、日志级别和具体日志消息格式化后写入日志文件，实现了日志记录的功能。
// 具体的日志消息内容由调用者提供的格式字符串和可变参数列表决定
int Logger::Print(mq_log_level iLevel, const char *pFmt, ...)
{
    if (iLevel == mq_log_err)
    {
        printf("%s:", ERR_LOG);
    }
    else if (iLevel == mq_log_warn)
    {
        printf("%s:", WARN_LOG);
    }
    else
    {
        printf("%s:", INFO_LOG);
    }
    va_list otherarg;
    va_start(otherarg, pFmt);
    char pLog[100];
    vsnprintf(pLog, 100, pFmt, otherarg);
    va_end(otherarg);
    printf("%s\n", pLog);
    return SUCCESS;
}