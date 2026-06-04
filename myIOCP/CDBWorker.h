#pragma once

#include <queue>
#include "CDBConnector.h"
#include "DBJob.h"

class CDBWorker
{
public:
    CDBWorker();
    ~CDBWorker();

    bool Start(const wchar_t* connStr);

    void Stop();

    void PushJob(const DBJob& job);

    bool PopResult(DBResult* out);

    int GetJobSize();
    int GetResultSize();

private:
    HANDLE       hThread;
    volatile bool isRun;

    CDBConnector db;

    std::queue<DBJob> jobs;
    CRITICAL_SECTION  csJobs;

    std::queue<DBResult> results;
    CRITICAL_SECTION     csResults;

    static unsigned __stdcall ThreadFunc(void* arg);
    void WorkerLoop();

    bool popJob(DBJob* out);                 
    void pushResult(const DBResult& r);      

    void processLogin(const DBJob& job);
    void processSignUp(const DBJob& job);
};
