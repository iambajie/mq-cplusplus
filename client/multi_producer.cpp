
//c/c++
#include<string.h>
#include<stdio.h>
#include<vector>
#include<string>
//linux
#include<sys/signal.h>
#include<unistd.h>
//user define
#include"client.h"
#include"../logger/logger.h"
using namespace WSMQ;
using std::vector;
using std::string;
//全局变量 
Logger producerLogger;
bool bStop=false;
bool bAlarm=false;
void sig_alarm(int iSig)
{
    bAlarm=true;
    alarm(1);//闹钟函数，当seconds秒后，触发SIGALRM信号
}
void sig_stop(int iSig)
{
    bStop=true;
}

int main(int argc,char *argv[])
{
    printf("start process\n");
    if(InitConf(CONF_FILE_PATH)!=0)
    {
        printf("init conf failed\n");
        return -1;
    }
    
    if(argc<3)
    {
        printf("<user msg>./mq_multi_producer message_len log_name\n");
        return -1;
    }
    int iLen=atoi(argv[1]);//消息长度

    if(iLen>MAX_CLINT_PACKAGE_LENGTH-100)//客户端发上来的包的最大值 1024*11
    {
        printf("too long message!\n");
        return -1;
    }
    if(iLen<=0)
    {
        printf("msg len must great zero!\n");
        return -1;
    }
    int iRet = 0;
    char pLogPath[MQ_MAX_PATH_LEN]=DEFAULT_CLI_LOG_PATH;
    if(producerLogger.Init(pLogPath)!=Logger::SUCCESS)
    {
        printf("logger init failed!");
        return -1;
    }
    //后台运行进程
    int ret=0;
    if((ret=FuncTool::DaemonInit())!=FuncTool::SUCCESS)//进程初始化
    {
        producerLogger.WriteLog(mq_log_err,"Daemoninit faild!,return value is %d",ret);
        return -1;
    }
    Producer *pProducter=new Producer();
    if(pProducter->BuildConnection()!=Producer::SUCCESS)
    {
        producerLogger.WriteLog(mq_log_err,"Build Connection failed!errMsg is %s\n",pProducter->GetErrMsg());
        delete pProducter;
    }
    printf("init sucess\n");

    
    //创建exchange queue并绑定
    if(pProducter->CreateExchange("exchange1",EXCHANGE_TYPE_DIRECT)!=Producer::SUCCESS)
    {
        producerLogger.WriteLog(mq_log_err,"Create exchange failed!errMsg is %s\n",pProducter->GetErrMsg());
    }
    printf("create exchange sucess\n");

    if(pProducter->CreateQueue("queue1")!=Producer::SUCCESS)
    {
        producerLogger.WriteLog(mq_log_err,"Create queue failed!errMsg is %s\n",pProducter->GetErrMsg());
    }
    printf("create queue sucess\n");

    if(pProducter->CreateBinding("exchange1","queue1","mq.base.test")!=Producer::SUCCESS)
    {
        producerLogger.WriteLog(mq_log_err,"Create binding failed!errMsg is %s\n",pProducter->GetErrMsg());
    }
    printf("create binding sucess\n");

    //创建指定长度的消息
    string strBase(iLen,'m');//指定为10
    printf("strBase:%s\n", strBase);
    int iLast=0;
    int i=0;
    int iSecond=0;
    //注册信号处理函数
    alarm(1);
    signal(SIGINT,sig_stop);//程序终止(interrupt)信号
    signal(SIGTERM,sig_stop);//发送到程序的终止请求
    signal(SIGALRM,sig_alarm);//在进程的计时器到期的时候，SIGALRM信号会被交付（delivered）给进程
    printf("alarm\n");
    while(!bStop)
    {
        string strMsg=strBase+":"+std::to_string(i);
        ret=pProducter->PuslishMessage("exchange1","mq.base.test",strMsg);
        if(ret!=Producer::SUCCESS)
        {
            producerLogger.WriteLog(mq_log_err,"send msg failed,err msg is %s\n",pProducter->GetErrMsg());
            return -1;
        }
        ++i;
        if(bAlarm)
        {
            bAlarm=false;
            int count=i-iLast;
            iLast=i;
            ++iSecond;
            producerLogger.WriteLog(mq_log_info,"send speed:%d/s\n",count);
        }
    }
    // if(iSecond!=0)
    // {
    //     LOG_INFO(0, 0,"average speed is %d/s,total num is %d\n",i/iSecond,i);
    // }  
    // if(vProducers[0]->DeleteExchange("exchange1")!=Client::SUCCESS)
    // {
    //     printf("Delete exchange failed!errMsg is %s\n",vProducers[0]->GetErrMsg());
    // }
    delete pProducter;
    pProducter=NULL;
}