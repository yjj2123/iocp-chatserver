#pragma once

#include "CLanServer.h"   

enum DBJobType : BYTE
{
    DB_JOB_LOGIN  = 1,
    DB_JOB_SIGNUP = 2
};

struct DBJob
{
    DBJobType type;
    SessionID sid;

    wchar_t account[33];        
    wchar_t password[65];       
    wchar_t nickname[33];       

    DBJob()
        : type(DB_JOB_LOGIN)
        , sid(0)
    {
        account[0]  = L'\0';
        password[0] = L'\0';
        nickname[0] = L'\0';
    }
};

struct DBResult
{
    DBJobType type;
    SessionID sid;

    int result;                 
    int userNo;
    wchar_t nickname[33];

    DBResult()
        : type(DB_JOB_LOGIN)
        , sid(0)
        , result(1)
        , userNo(0)
    {
        nickname[0] = L'\0';
    }
};
