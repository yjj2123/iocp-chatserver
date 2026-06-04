#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <sql.h>
#include <sqlext.h>

class CDBConnector
{
public:
    CDBConnector();
    ~CDBConnector();

    bool Connect(const wchar_t* connStr);

    void Disconnect();

    bool IsConnected() const { return isConnected; }

    bool Login(const wchar_t* account, const wchar_t* password,
               int* outResult, int* outUserNo,
               wchar_t* outNickname, int nickBufCount);

    bool SignUp(const wchar_t* account, const wchar_t* password, const wchar_t* nickname,
                int* outResult, int* outUserNo);

private:
    SQLHENV hEnv;
    SQLHDBC hDbc;
    bool    isConnected;

    void printDiag(SQLSMALLINT handleType, SQLHANDLE handle, const char* where);
};
