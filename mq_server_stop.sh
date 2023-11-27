#!/bin/bash


if [ $# != 1 ] ; then #参数个数

echo "USAGE: $0 sig_val" 

exit 1; #非正常运行导致退出程序

fi 

sig_val=$1

process_name='mq_'

pid=$(ps -ef | grep $process_name | grep -v grep | awk '{print $2}') #只包含cpu关键字的进程筛选结果作为输入给awk '{print $2}'，这个部分的作用是提取输入的第二列，而第二列正是进程的PID

for i in $pid;
do
	kill -$sig_val $i
	if [ $? != 0 ]; then #最后运行的命令的结束代码（返回值）即执行上一个指令的返回值 (显示最后命令的退出状态。0表示没有错误，其他任何值表明有错误)
		echo $i' stop failed'
	else
		echo $i' stop succeed'
	fi
done

#输入参数
#1：挂起
#2：关闭
