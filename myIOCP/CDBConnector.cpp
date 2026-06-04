#include "CDBConnector.h"
#include <cstdio>

static inline bool sqlOk(SQLRETURN ret)
{
    return (ret == SQL_SUCCESS) || (ret == SQL_SUCCESS_WITH_INFO);
}

CDBConnector::CDBConnector()
    : hEnv(SQL_NULL_HENV)
    , hDbc(SQL_NULL_HDBC)
    , isConnected(false)
{
}

CDBConnector::~CDBConnector()
{
    Disconnect();
}

bool CDBConnector::Connect(const wchar_t* connStr)
{
    if (isConnected)
        return true;

    SQLRETURN ret;

    ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &hEnv);
    if (!sqlOk(ret))
    {
        printf("[DB] SQLAllocHandle(ENV) 실패\n");
        return false;
    }

    ret = SQLSetEnvAttr(hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    if (!sqlOk(ret))
    {
        printDiag(SQL_HANDLE_ENV, hEnv, "SQLSetEnvAttr");
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
        hEnv = SQL_NULL_HENV;
        return false;
    }

    ret = SQLAllocHandle(SQL_HANDLE_DBC, hEnv, &hDbc);
    if (!sqlOk(ret))
    {
        printDiag(SQL_HANDLE_ENV, hEnv, "SQLAllocHandle(DBC)");
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
        hEnv = SQL_NULL_HENV;
        return false;
    }

    ret = SQLDriverConnectW(hDbc, NULL,
                            (SQLWCHAR*)connStr, SQL_NTS,
                            NULL, 0, NULL,
                            SQL_DRIVER_NOPROMPT);
    if (!sqlOk(ret))
    {
        printDiag(SQL_HANDLE_DBC, hDbc, "SQLDriverConnect");
        SQLFreeHandle(SQL_HANDLE_DBC, hDbc);
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
        hDbc = SQL_NULL_HDBC;
        hEnv = SQL_NULL_HENV;
        return false;
    }

    isConnected = true;
    printf("[DB] Connected\n");
    return true;
}

void CDBConnector::Disconnect()
{
    if (hDbc != SQL_NULL_HDBC)
    {
        if (isConnected)
            SQLDisconnect(hDbc);
        SQLFreeHandle(SQL_HANDLE_DBC, hDbc);
        hDbc = SQL_NULL_HDBC;
    }
    if (hEnv != SQL_NULL_HENV)
    {
        SQLFreeHandle(SQL_HANDLE_ENV, hEnv);
        hEnv = SQL_NULL_HENV;
    }
    isConnected = false;
}

bool CDBConnector::Login(const wchar_t* account, const wchar_t* password,
                         int* outResult, int* outUserNo,
                         wchar_t* outNickname, int nickBufCount)
{
    
    if (outResult)   *outResult = 1;   
    if (outUserNo)   *outUserNo = 0;
    if (outNickname && nickBufCount > 0) outNickname[0] = L'\0';

    if (!isConnected || hDbc == SQL_NULL_HDBC)
    {
        printf("[DB] Login: not connected\n");
        return false;
    }

    SQLHSTMT hStmt = SQL_NULL_HSTMT;
    SQLRETURN ret;

    ret = SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
    if (!sqlOk(ret))
    {
        printDiag(SQL_HANDLE_DBC, hDbc, "Login: AllocHandle(STMT)");
        return false;
    }

    SQLLEN accLen = SQL_NTS;
    SQLLEN pwLen  = SQL_NTS;

    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     32, 0, (SQLPOINTER)account, 0, &accLen);
    SQLBindParameter(hStmt, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     64, 0, (SQLPOINTER)password, 0, &pwLen);

    SQLWCHAR sql[] = L"EXEC sp_Login ?, ?";
    ret = SQLExecDirectW(hStmt, sql, SQL_NTS);
    if (!sqlOk(ret))
    {
        printDiag(SQL_HANDLE_STMT, hStmt, "Login: ExecDirect");
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return false;
    }

    SQLINTEGER result   = 0;
    SQLINTEGER userNo   = 0;
    SQLWCHAR   nickname[64] = { 0 };   

    SQLBindCol(hStmt, 1, SQL_C_LONG,  &result,  sizeof(result),  NULL);
    SQLBindCol(hStmt, 2, SQL_C_LONG,  &userNo,  sizeof(userNo),  NULL);
    SQLBindCol(hStmt, 3, SQL_C_WCHAR, nickname, sizeof(nickname), NULL);

    ret = SQLFetch(hStmt);
    if (ret == SQL_NO_DATA)
    {
        
        printf("[DB] Login: no result row\n");
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return false;
    }
    if (!sqlOk(ret))
    {
        printDiag(SQL_HANDLE_STMT, hStmt, "Login: Fetch");
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return false;
    }

    if (outResult) *outResult = (int)result;
    if (outUserNo) *outUserNo = (int)userNo;
    if (outNickname && nickBufCount > 0)
    {
        
        wcsncpy_s(outNickname, nickBufCount, nickname, _TRUNCATE);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return true;
}

bool CDBConnector::SignUp(const wchar_t* account, const wchar_t* password, const wchar_t* nickname,
                          int* outResult, int* outUserNo)
{
    if (outResult) *outResult = 1;     
    if (outUserNo) *outUserNo = 0;

    if (!isConnected || hDbc == SQL_NULL_HDBC)
    {
        printf("[DB] SignUp: not connected\n");
        return false;
    }

    SQLHSTMT hStmt = SQL_NULL_HSTMT;
    SQLRETURN ret;

    ret = SQLAllocHandle(SQL_HANDLE_STMT, hDbc, &hStmt);
    if (!sqlOk(ret))
    {
        printDiag(SQL_HANDLE_DBC, hDbc, "SignUp: AllocHandle(STMT)");
        return false;
    }

    SQLLEN accLen  = SQL_NTS;
    SQLLEN pwLen   = SQL_NTS;
    SQLLEN nickLen = SQL_NTS;

    SQLBindParameter(hStmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     32, 0, (SQLPOINTER)account,  0, &accLen);
    SQLBindParameter(hStmt, 2, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     64, 0, (SQLPOINTER)password, 0, &pwLen);
    SQLBindParameter(hStmt, 3, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
                     32, 0, (SQLPOINTER)nickname, 0, &nickLen);

    SQLWCHAR sql[] = L"EXEC sp_SignUp ?, ?, ?";
    ret = SQLExecDirectW(hStmt, sql, SQL_NTS);
    if (!sqlOk(ret))
    {
        printDiag(SQL_HANDLE_STMT, hStmt, "SignUp: ExecDirect");
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return false;
    }

    SQLINTEGER result = 0;
    SQLINTEGER userNo = 0;

    SQLBindCol(hStmt, 1, SQL_C_LONG, &result, sizeof(result), NULL);
    SQLBindCol(hStmt, 2, SQL_C_LONG, &userNo, sizeof(userNo), NULL);

    ret = SQLFetch(hStmt);
    if (ret == SQL_NO_DATA)
    {
        printf("[DB] SignUp: no result row\n");
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return false;
    }
    if (!sqlOk(ret))
    {
        printDiag(SQL_HANDLE_STMT, hStmt, "SignUp: Fetch");
        SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
        return false;
    }

    if (outResult) *outResult = (int)result;
    if (outUserNo) *outUserNo = (int)userNo;

    SQLFreeHandle(SQL_HANDLE_STMT, hStmt);
    return true;
}

void CDBConnector::printDiag(SQLSMALLINT handleType, SQLHANDLE handle, const char* where)
{
    SQLWCHAR    sqlState[6]  = { 0 };
    SQLWCHAR    message[512] = { 0 };
    SQLINTEGER  nativeError  = 0;
    SQLSMALLINT msgLen        = 0;

    SQLRETURN ret = SQLGetDiagRecW(handleType, handle, 1,
                                   sqlState, &nativeError,
                                   message, 512, &msgLen);
    if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO)
    {
        wprintf(L"[DB ERR] %hs | state=%ls native=%d msg=%ls\n",
                where, sqlState, (int)nativeError, message);
    }
    else
    {
        printf("[DB ERR] %s (진단 메시지 추출 실패)\n", where);
    }
}
