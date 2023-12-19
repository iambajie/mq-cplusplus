#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "ini_file.h"

namespace WSMQ
{

    // 去除输入字符串szString两端的空白字符（包括空格、制表符、回车和换行）
    void TrimString(char *szString)
    {
        int iLen;
        int n, iLeftStart;

        iLen = strlen(szString);
        if (iLen == 0)
            return;

        // 从右边（字符串末尾）开始剪裁
        for (n = iLen - 1; n >= 0; --n)
        {
            if (szString[n] == ' ' || szString[n] == '\t' || szString[n] == '\r' || szString[n] == '\n')
                szString[n] = 0;
            else
                break;
        }

        iLen = strlen(szString);
        if (iLen == 0)
            return;

        // 从左边（字符串开头）开始剪裁
        for (n = 0; n < iLen; ++n)
        {
            if (szString[n] != ' ' && szString[n] != '\t' && szString[n] != '\r' && szString[n] != '\n')
                break;
        }

        // 左边没有需要剪裁的空白字符 直接返回
        if (n == 0)
            return;

        iLeftStart = n;

        // 将一块内存区域的内容复制到另一块内存区域
        memmove(szString, &(szString[iLeftStart]), iLen - iLeftStart + 1);
    }

    CIniFile::CIniFile(const char *szIniFile)
    {
        FILE *pFile;

        // 使用缓冲区(m_szBuffer)来存储整个INI文件的内容
        m_szBuffer = NULL;
        m_iSize = 0;

        struct stat stFileStat;
        if (stat(szIniFile, &stFileStat) != 0)
        {
            return;
        }

        m_iSize = stFileStat.st_size;

        if (m_iSize <= 0)
            return;

        m_szBuffer = (char *)malloc(m_iSize + 1);
        if (m_szBuffer == NULL)
            return;
        memset(m_szBuffer, 0, m_iSize + 1);

        pFile = fopen(szIniFile, "r");
        // size_t fread(void *ptr, size_t size, size_t count, FILE *stream);

        // ptr是指向接收数据的内存区域的指针
        // size是每个数据项的大小（以字节为单位。
        // count是要读取的数据项的数量
        // stream是要读取的输入文件流
        size_t siRead = fread(m_szBuffer, m_iSize, 1, pFile);
        if (siRead != 1)
        {
            free(m_szBuffer);
            m_szBuffer = NULL;
        }
        fclose(pFile);
    }

    CIniFile::~CIniFile()
    {
        if (m_szBuffer != NULL)
        {
            free(m_szBuffer);
        }
    }

    // 查询的section名称(szSection)、项名称(szItem)、默认值(szDefault)、用于存放结果的字符数组(szValue)，以及这个数组的长度(iValueLen)
    int CIniFile::GetString(const char *szSection, const char *szItem,
                            const char *szDefault, char *szValue, const int iValueLen)
    {
        char *pszSection = NULL, *pszNextSection = NULL;
        char *pszItem = NULL, *pszValue = NULL, *pszTestSection;
        char *pszTemp;
        int n, iPos, iSectionLen, iItemLen;

        strncpy(szValue, szDefault, iValueLen);
        iSectionLen = strlen(szSection);
        iItemLen = strlen(szItem);

        if (!IsValid())
            return E_INI_FILE;

        // 在m_szBuffer缓冲区中查找指定的section
        pszTemp = m_szBuffer;
        while (pszTemp < m_szBuffer + m_iSize)
        {
            // 搜索第一次出现的字符串
            pszSection = strstr(pszTemp, szSection);
            if (pszSection == NULL)
            {
                return E_INI_FILE;
            }

            // 找到的section就在文件的开头 前面没有[ 不合法
            if (pszSection == m_szBuffer)
            {
                return E_INI_FILE;
            }

            // 判断检查找到的section是否被[和]符号包围
            if (*(pszSection - 1) != '[' || *(pszSection + iSectionLen) != ']')
            {
                // 搜索位置移动到找到的section之后
                pszTemp = pszSection + iSectionLen;
                continue;
            }
            // 确保section名称在行首
            if (pszSection > m_szBuffer + 1)
            {
                // 避免找section1遇到
                // [section1]
                // key1 = value1
                // key2 = section1

                if (*(pszSection - 2) != '\n')
                {
                    pszTemp = pszSection + iSectionLen;
                    continue;
                }
            }

            pszTemp = pszSection;
            break;
        }

        // 找到下一个section的开始位置 下一个左方括号'['的位置
        pszTestSection = pszTemp;
        while (pszTestSection != NULL)
        {
            pszNextSection = strstr(pszTestSection, "[");
            if (pszNextSection != NULL)
            {
                // 检查找到的'['前面的字符是否为换行符'\n'。如果是，说明找到的'['是新的一行的开始，可以被认为是新section的开始
                if (*(pszNextSection - 1) == '\n')
                {
                    break;
                }
                else
                {
                    // 如果'['之前的字符不是换行符，可能意味着这个'['处于注释或值等其他地方，而不是新section的开始
                    // 在这种情况下，更新pszTestSection为当前找到的'['之后的位置，然后返回到循环的开始，继续寻找下一个出现的'['
                    pszTestSection = pszNextSection + 1;
                }
            }
            else
            {
                break;
            }
        }
        // 如果在当前位置之后没有找到新的section，则将文件的末尾视为下一个"虚拟"section的开始，以便于处理文件的剩余内容
        if (pszNextSection == NULL)
        {
            pszNextSection = m_szBuffer + m_iSize;
        }

        // locate item
        while (pszTemp < m_szBuffer + m_iSize)
        {
            pszItem = strstr(pszTemp, szItem);
            // 没有找到指定的item
            if (pszItem == NULL)
            {
                return E_INI_FILE;
            }

            // 找到的item不在当前section内（即在pszNextSection或之后）
            if (pszItem >= pszNextSection)
            {
                return E_INI_FILE;
            }

            // 找到的item不是从新的一行开始
            if (*(pszItem - 1) != '\n')
            {
                pszTemp = pszItem + iItemLen;
                continue;
            }
            // item后面既不是空格，也不是等号'='，也不是制表符，则说明这不是一个正确的键值对，更新pszTemp为当前找到的item之后的位置
            if (*(pszItem + iItemLen) != ' ' && *(pszItem + iItemLen) != '=' && *(pszItem + iItemLen) != '\t')
            {
                pszTemp = pszItem + iItemLen;
                continue;
            }

            // 在找到的item中寻找等号'='
            pszValue = strstr(pszItem, "=");
            if (pszValue == NULL)
            {
                return E_INI_FILE;
            }
            // 跳过'='
            pszValue++;

            // pszValue已经超出了文件末尾
            if (pszValue >= m_szBuffer + m_iSize)
            {
                return E_INI_FILE;
            }

            // 提取并存储value
            iPos = 0;
            // 当遇到回车符'\r'、换行符'\n'或结束符'\0'时，停止循环
            for (n = 0; iPos < iValueLen - 1 && pszValue[n] != '\r' && pszValue[n] != '\n' && pszValue[n] != 0; ++n)
            {
                // 开始是空格或制表符，跳过
                if ((pszValue[n] == ' ' || pszValue[n] == '\t') && iPos == 0)
                    continue;
                // 遇到分号';' 可能是注释
                if (pszValue[n] == ';')
                {
                    // 不是value的开始处
                    if (n > 0)
                    {
                        // 下一个字符也是分号，则说明这是字符串中的";"，而不是注释的开始。将其存入szValue
                        if (pszValue[n + 1] == ';')
                        {
                            szValue[iPos] = pszValue[n];
                            iPos++;
                            n++;
                            continue;
                        }
                    }
                    break;
                }

                szValue[iPos] = pszValue[n];
                iPos++;
            }
            // 字符串的结束
            szValue[iPos] = 0;
            break;
        }

        // trim string
        TrimString(szValue);

        return SUCCESS;
    }

    int CIniFile::GetInt(const char *szSection, const char *szItem,
                         const int iDefault, int *piValue)
    {
        char szInt[32] =
            {0};

        if (!IsValid())
            return E_INI_FILE;

        if (GetString(szSection, szItem, "", szInt, sizeof(szInt)) != 0)
        {
            *piValue = iDefault;
            return E_INI_FILE;
        }
        if (strncmp(szInt, "0x", 2) == 0 || strncmp(szInt, "0X", 2) == 0)
            sscanf(szInt + 2, "%x", piValue);
        else
            *piValue = atoi(szInt);

        return SUCCESS;
    }

    // 从INI文件中读取特定section（章节）和item（项）的无符号长整型值的方法
    int CIniFile::GetULongLong(const char *szSection, const char *szItem, unsigned long long ullDefault,
                               unsigned long long *pullValue)
    {
        char szULongLong[32] =
            {0};

        if (!IsValid())
            return E_INI_FILE;

        if (GetString(szSection, szItem, "", szULongLong, sizeof(szULongLong)) != 0)
        {
            *pullValue = ullDefault;
            return E_INI_FILE;
        }
        // 使用 strtoull 函数将其转换为无符号长整型值, NULL表示不关心转换停止的位置
        *pullValue = strtoull(szULongLong, NULL, 10);

        return SUCCESS;
    }

    int CIniFile::IsValid()
    {
        if (m_szBuffer == NULL || m_iSize <= 0)
        {
            return 0;
        }
        else
        {
            return 1;
        }
    }

}
