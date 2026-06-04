#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#include "CChatServer.h"
#include <conio.h>
#include <locale.h>

int main()
{
    setlocale(LC_ALL, "Korean");

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int workerCount = si.dwNumberOfProcessors * 2;
    _CrtMemState s1, s2, s3;

    CChatServer server;
    server.StartContent();
    if (!server.Start("", 6000, workerCount, false, MAX_SESSION)) {
        printf("Start failed\n");
        return 1;
    }
    _CrtMemCheckpoint(&s1);
    printf("Server running on port 6000. Press 'q' to quit.\n");
    char c;
    while (scanf_s("%c", &c, 1) != EOF) {
        if (c == 's' || c == 'S') {
            _CrtMemCheckpoint(&s1);
                printf("[CHECKPOINT] s1 re-recorded (steady-state baseline)\n");
        }
        if (c == 'q' || c == 'Q') break;
    }
    _CrtMemCheckpoint(&s2);
    if (_CrtMemDifference(&s3, &s1, &s2)) {
        printf("\n[LEAK] CRT heap difference detected:\n");
        _CrtMemDumpStatistics(&s3);
    }
    else {
        printf("\n[OK] no CRT heap difference (delta 0)\n");
    }

    server.Stop();
    server.StopContent();
    return 0;
}
