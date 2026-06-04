#include "CDBWorker.h"
#include <process.h>
#include <cstdio>

CDBWorker::CDBWorker()
    : hThread(NULL)
    , isRun(false)
{
    InitializeCriticalSection(&csJobs);
    InitializeCriticalSection(&csResults);
}

CDBWorker::~CDBWorker()
{
    Stop();
    DeleteCriticalSection(&csJobs);
    DeleteCriticalSection(&csResults);
}

bool CDBWorker::Start(const wchar_t* connStr)
{
    if (!db.Connect(connStr))
    {
        printf("[DBWorker] Connect 실패\n");
        return false;
    }

    isRun = true;
    hThread = (HANDLE)_beginthreadex(nullptr, 0, ThreadFunc, this, 0, nullptr);
    if (!hThread)
    {
        printf("[DBWorker] thread 생성 실패\n");
        isRun = false;
        db.Disconnect();
        return false;
    }
    return true;
}

void CDBWorker::Stop()
{
    if (!isRun)
        return;

    isRun = false;

    if (hThread)
    {
        
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
        hThread = NULL;
    }

    db.Disconnect();
}

void CDBWorker::PushJob(const DBJob& job)
{
    EnterCriticalSection(&csJobs);
    jobs.push(job);
    LeaveCriticalSection(&csJobs);
}

bool CDBWorker::PopResult(DBResult* out)
{
    EnterCriticalSection(&csResults);
    if (results.empty())
    {
        LeaveCriticalSection(&csResults);
        return false;
    }
    *out = results.front();
    results.pop();
    LeaveCriticalSection(&csResults);
    return true;
}

int CDBWorker::GetJobSize()
{
    EnterCriticalSection(&csJobs);
    int n = (int)jobs.size();
    LeaveCriticalSection(&csJobs);
    return n;
}

int CDBWorker::GetResultSize()
{
    EnterCriticalSection(&csResults);
    int n = (int)results.size();
    LeaveCriticalSection(&csResults);
    return n;
}

unsigned __stdcall CDBWorker::ThreadFunc(void* arg)
{
    CDBWorker* self = static_cast<CDBWorker*>(arg);
    self->WorkerLoop();
    return 0;
}

bool CDBWorker::popJob(DBJob* out)
{
    EnterCriticalSection(&csJobs);
    if (jobs.empty())
    {
        LeaveCriticalSection(&csJobs);
        return false;
    }
    *out = jobs.front();
    jobs.pop();
    LeaveCriticalSection(&csJobs);
    return true;
}

void CDBWorker::pushResult(const DBResult& r)
{
    EnterCriticalSection(&csResults);
    results.push(r);
    LeaveCriticalSection(&csResults);
}

void CDBWorker::WorkerLoop()
{
    
    while (isRun)
    {
        DBJob job;
        if (popJob(&job))
        {
            if (job.type == DB_JOB_LOGIN)
                processLogin(job);
            else if (job.type == DB_JOB_SIGNUP)
                processSignUp(job);
            
        }
        else
        {
            
            Sleep(1);
        }
    }

    DBJob job;
    while (popJob(&job))
    {
        if (job.type == DB_JOB_LOGIN)
            processLogin(job);
        else if (job.type == DB_JOB_SIGNUP)
            processSignUp(job);
    }
}

void CDBWorker::processLogin(const DBJob& job)
{
    DBResult r;
    r.type   = DB_JOB_LOGIN;
    r.sid    = job.sid;
    r.result = 1;        
    r.userNo = 0;
    r.nickname[0] = L'\0';

    int     outResult   = 1;
    int     outUserNo   = 0;
    wchar_t outNickname[33] = { 0 };

    if (db.Login(job.account, job.password, &outResult, &outUserNo, outNickname, 33))
    {
        r.result = outResult;
        r.userNo = outUserNo;
        wcsncpy_s(r.nickname, _countof(r.nickname), outNickname, _TRUNCATE);
    }
    else
    {
        
        printf("[DBWorker] Login ODBC 실패 sid=%llu\n", (unsigned long long)job.sid);
    }

    pushResult(r);
}

void CDBWorker::processSignUp(const DBJob& job)
{
    DBResult r;
    r.type   = DB_JOB_SIGNUP;
    r.sid    = job.sid;
    r.result = 1;
    r.userNo = 0;
    r.nickname[0] = L'\0';

    int outResult = 1;
    int outUserNo = 0;

    if (db.SignUp(job.account, job.password, job.nickname, &outResult, &outUserNo))
    {
        r.result = outResult;
        r.userNo = outUserNo;
        
        wcsncpy_s(r.nickname, _countof(r.nickname), job.nickname, _TRUNCATE);
    }
    else
    {
        printf("[DBWorker] SignUp ODBC 실패 sid=%llu\n", (unsigned long long)job.sid);
    }

    pushResult(r);
}
