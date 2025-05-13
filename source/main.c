#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <time.h>

#define PrintWithTimestampf(...) _PrintWithTimestamp(__VA_ARGS__)

static char g_macroPATH[MAX_PATH];
static char g_mumuPATH[MAX_PATH];

static BOOL DirectoryExists(const char* path)
{
    DWORD attrib = GetFileAttributesA(path);
    return (attrib != INVALID_FILE_ATTRIBUTES) && (attrib & FILE_ATTRIBUTE_DIRECTORY);
}

static void _PrintWithTimestamp(const wchar_t* format, ...)
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_s(&tm, &t);

    wchar_t timebuf[64];
    wcsftime(timebuf, sizeof(timebuf) / sizeof(wchar_t), L"[%Y-%m-%d %H:%M:%S] ", &tm);
    wprintf(L"%s", timebuf);

    va_list args;
    va_start(args, format);
    vwprintf(format, args);
    va_end(args);
}

// 콘솔 출력 인코딩을 CP949(EUC-KR)로 설정 (한글 깨짐 방지)
static void SetKoreanOutputEncoding()
{
    SetConsoleOutputCP(949);
}

// 인스턴스 제목 패턴
static const char* getInstanceTitle(int index)
{
    static char title[256];
    snprintf(title, sizeof(title), "%d", index);
    return title;
}

// 윈도우로부터 프로세스 ID 얻기
static DWORD GetProcessIdFromWindow(HWND hwnd)
{
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid;
}

void ForceWindowCheck(HWND hwnd)
{
    // 마우스 이동을 시뮬레이션 → 이벤트 큐를 자극
    PostMessage(hwnd, WM_MOUSEMOVE, 0, 0);
    PostMessage(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
    PostMessage(hwnd, WM_NCACTIVATE, TRUE, 0);
    Sleep(100); // 메시지 루프 처리 시간 주기
}
// 해당 창이 응답 중인지 확인
BOOL IsWindowHung(HWND hwnd)
{
    DWORD_PTR result;
    return !SendMessageTimeout(hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 2000, &result);
}


BOOL IsReallyHung(HWND hwnd)
{
    DWORD_PTR result;
    return !SendMessageTimeout(hwnd, WM_MOUSEMOVE, 0, 0,
        SMTO_ABORTIFHUNG | SMTO_NORMAL, 2000, &result);
}
// 프로세스 강제 종료
static void KillProcess(DWORD pid)
{
    while (1)
    {
        HANDLE hProc = OpenProcess(SYNCHRONIZE |  PROCESS_TERMINATE, FALSE, pid);
        if (hProc == NULL)
            return;
        TerminateProcess(hProc, 0);

        DWORD waitResult = WaitForSingleObject(hProc, 5000);
        if (waitResult == WAIT_OBJECT_0) {
            //printf("Process terminated.\n");
            CloseHandle(hProc);
            return;
        } else {
            //printf("Process did not terminate within timeout.\n");
            Sleep(1000);
        }
        CloseHandle(hProc);
    }
}

// playerName에 해당하는 MuMu 폴더명(번호)를 찾음
static int GetMumuIndexFromPlayerName(const char* title)
{
    WIN32_FIND_DATAA findData;
    char findPath[MAX_PATH];
    snprintf(findPath, sizeof(findPath), "%s\\vms\\*", g_mumuPATH);
    HANDLE hFind = FindFirstFileA(findPath, &findData);

    if (hFind == INVALID_HANDLE_VALUE)
        return -1;

    do {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {

            char jsonPath[512];
            snprintf(jsonPath, sizeof(jsonPath),
                "%s\\vms\\%s\\configs\\extra_config.json",
                g_mumuPATH, findData.cFileName);

            FILE* fp = fopen(jsonPath, "r");
            if (!fp) {
                continue;
            }
            char buf[2048] = {0};
            fread(buf, 1, sizeof(buf) - 1, fp);
            fclose(fp);

            char* pos = strstr(buf, "\"playerName\"");
            if (!pos) {
                continue;
            }

            char* start = strchr(pos, '"');

            if (start) start = strchr(start + 1, '"');
            if (start) start = strchr(start + 1, '"');

            if (!start) {
                continue;
            }

            char* end = strchr(start + 1, '"');
            if (!end)
                continue;
            if (end - start - 1 >= 256)
                continue;

            char playerName[256] = {0};
            strncpy(playerName, start + 1, end - start - 1);
            playerName[end - start - 1] = '\0';
            if (strcmp(playerName, title) != 0)
                continue;
                
            // MuMuPlayerGlobal-12.0-5 → 마지막 숫자 추출
            const char* dash = strrchr(findData.cFileName, '-');
            if (dash && isdigit(*(dash + 1))) {
                return atoi(dash + 1);
            }
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
    return -1;
}

int GetAdbPortFromVMConfig(const char* configPath)
{
    FILE* fp = fopen(configPath, "r");
    if (!fp) return -1;

    char buf[4096] = {0};
    fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);

    char* pos = strstr(buf, "\"adb\"");
    if (!pos) return -1;

    char* portStart = strstr(pos, "\"host_port\"");
    if (!portStart) return -1;

    int port;
    if (sscanf(portStart, "\"host_port\"%*[^0-9]%d", &port) == 1) {
        return port;
    }

    return -1;
}

// ADB ping 실행을 위한 쓰레드 구조체
typedef struct {
    int port;
    BOOL success;
} AdbPingArgs;

DWORD WINAPI AdbPingThread(LPVOID lpParam) {
    AdbPingArgs* args = (AdbPingArgs*)lpParam;
    char cmd[MAX_PATH];
    snprintf(cmd, sizeof(cmd),
        "\"%s\\shell\\adb.exe\" -s 127.0.0.1:%d shell echo ping >nul 2>&1",
        g_mumuPATH, args->port);

    int result = system(cmd);
    args->success = (result == 0);
    return 0;
}

// ADB ping을 쓰레드로 실행하고 3초 안에 응답 확인
BOOL IsAdbAlive(int port) {
    AdbPingArgs args;
    args.port = port;
    args.success = FALSE;

    HANDLE hThread = CreateThread(NULL, 0, AdbPingThread, &args, 0, NULL);
    if (!hThread) return FALSE;

    DWORD waitResult = WaitForSingleObject(hThread, 3000);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateThread(hThread, 1);
        CloseHandle(hThread);
        return FALSE;
    }

    CloseHandle(hThread);
    return args.success;
}
// MuMu 특정 인스턴스 실행
static void LaunchMuMuInstance(int instanceNum)
{
    char cmd[512];
    char exePath[MAX_PATH];

    snprintf(cmd, sizeof(cmd), "-v %d", instanceNum);
    snprintf(exePath, sizeof(exePath), "%s\\shell\\MuMuPlayer.exe", g_mumuPATH);
    ShellExecuteA(NULL, "open", exePath, cmd, NULL, SW_SHOWNORMAL);
}

static void LaunchInstanceAHK(const char *ahk_name)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "%s\\Scripts\\%s", g_macroPATH, ahk_name);
    ShellExecuteA(NULL, "open", cmd, NULL, NULL, SW_SHOWNORMAL);
}

static BOOL checkAHK(const char *ahk_name)
{
    HWND hwnd = FindWindowA(NULL, ahk_name);
    if (hwnd == NULL)
    {
        Sleep(5000);
        hwnd = FindWindowA(NULL, ahk_name);
        if (hwnd == NULL)
        {
            LaunchInstanceAHK(ahk_name);
        }
        Sleep(5000);
        return FALSE;
    }

    ForceWindowCheck(hwnd);

    if (!IsWindowHung(hwnd) && !IsReallyHung(hwnd))
    {
        return TRUE;
    }
    PrintWithTimestampf(L"[STUCK] AHK %S 응답 없음 → 재시작\n", ahk_name);
    return FALSE;
}

static int checkMUMU(HWND hwnd, const char *title, int realIndex)
{

    ForceWindowCheck(hwnd);

    if (!IsWindowHung(hwnd) && !IsReallyHung(hwnd)) {
        //wprintf(L"[OK] 인스턴스 %d 정상 동작\n", i);

        char adb_config_path[MAX_PATH];
        snprintf(adb_config_path, sizeof(adb_config_path) - 1,
            "%s\\vms\\MuMuPlayerGlobal-12.0-%u\\configs\\vm_config.json", g_mumuPATH, realIndex);
        int port = GetAdbPortFromVMConfig(adb_config_path);
        if (port < 0) {
            PrintWithTimestampf(L"[ERROR] '%S'에 해당하는 adb port 번호를 찾을 수 없음\n", title);
            return TRUE;
        }

        if (IsAdbAlive(port))
        {
            //printf("%s %u live\n", title, port);
            return TRUE;
        }
        PrintWithTimestampf(L"[ERROR] '%S'에 해당하는 adb is die\n", title);
    }
    return FALSE;
}
static void run(int instanceCount)
{
    if (instanceCount <= 0)
    {
        PrintWithTimestampf(L"유효하지 않은 인스턴스 수: %d\n", instanceCount);
        Sleep(1000 * 5);
        exit(0);
    }

    for (int i = 1; i <= instanceCount; ++i)
    {
        char ahk_name[256];
        const char* title = getInstanceTitle(i);
        HWND hwnd;

        hwnd = FindWindowA(NULL, title);
        snprintf(ahk_name, sizeof(ahk_name) - 1, "%d.ahk", i);
        int realIndex = GetMumuIndexFromPlayerName(title);
        if (realIndex == -1) {
            PrintWithTimestampf(L"[ERROR] '%S'에 해당하는 MuMu 인스턴스 번호를 찾을 수 없음\n", title);
            continue;
        }

        if (!hwnd) {
            PrintWithTimestampf(L"[INFO] 인스턴스 %d 창 없음\n", i);
            goto restart;
        }

        if (checkAHK(ahk_name) && checkMUMU(hwnd, title, realIndex))
        {
            continue;
        }

allrestart:
        PrintWithTimestampf(L"[STUCK] 인스턴스 %d 응답 없음 → 재시작\n", i);
        DWORD pid = GetProcessIdFromWindow(hwnd);
        KillProcess(pid);

restart:
        snprintf(ahk_name, sizeof(ahk_name) - 1, "%d.ahk", i);
        hwnd = FindWindowA(NULL, ahk_name);
        while (hwnd != NULL)
        {
            pid = GetProcessIdFromWindow(hwnd);
            if (pid <= 0)
                break;
            KillProcess(pid);

            Sleep(1000);
            hwnd = FindWindowA(NULL, ahk_name);
        }
        LaunchMuMuInstance(realIndex);
        Sleep(1000 * 10); //wait load mumu
        LaunchInstanceAHK(ahk_name);
    }
}

static int IsRunningAsAdmin()
{
    BOOL isAdmin = FALSE;
    HANDLE token;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation;
        DWORD size;
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
            isAdmin = elevation.TokenIsElevated;
        }
        CloseHandle(token);
    }
    return isAdmin;
}

static void setMacroPath(const char *macro_path)
{
    if (macro_path == NULL)
    {
        GetCurrentDirectoryA(MAX_PATH, g_macroPATH);
    }
    else{
        strcpy(g_macroPATH, macro_path);
    }

    char path[512];
    snprintf(path, sizeof(path),
        "%s\\Scripts", g_macroPATH);

    if (!DirectoryExists(path))
    {
        PrintWithTimestampf(L"%S를 찾을수 없다. %S를 확인해야된다. \n", path, macro_path);
        Sleep(5000);
        exit(0);
    }
}

static void setMuMuPath(void)
{
    char iniPath[MAX_PATH];
    snprintf(iniPath, sizeof(iniPath), "%s\\Settings.ini", g_macroPATH);

    DWORD charsRead = GetPrivateProfileStringA(
        "UserSettings",        // 섹션 이름
        "folderPath",          // 키 이름
        "",                    // 기본값 (없을 경우)
        g_mumuPATH,            // 값을 저장할 버퍼
        sizeof(g_mumuPATH),    // 버퍼 크기
        iniPath                // INI 파일 경로
    );

    if (charsRead == 0 || !DirectoryExists(g_mumuPATH)) {
        PrintWithTimestampf(L"[ERROR] 설정 파일에서 folderPath를 불러오지 못했거나 폴더가 존재하지 않습니다: %S\n", g_mumuPATH);
        exit(0);
    }

    size_t len = strlen(g_mumuPATH);
    if (g_mumuPATH[len - 1] != '\\') {
        snprintf(g_mumuPATH + len, sizeof(g_mumuPATH) - len, "\\MuMuPlayerGlobal-12.0");
    } else {
        snprintf(g_mumuPATH + len, sizeof(g_mumuPATH) - len, "MuMuPlayerGlobal-12.0");
    }
}

static void print_help(const char* progname)
{
    PrintWithTimestampf(L"사용법: %S [인스턴스개수] [매크로폴더경로 (선택)]\n", progname);
    wprintf(L"\n");
    wprintf(L"옵션:\n");
    wprintf(L"  %S --help, -h     도움말 출력\n", progname);
    wprintf(L"\n");
    wprintf(L"예시:\n");
    wprintf(L"  %S 5\n", progname);
    wprintf(L"  %S 5 C:\\PTCGPB\n", progname);
    wprintf(L"\n");
    wprintf(L"Settings.ini 파일에서 MuMu 설치 경로(folderPath)를 자동으로 읽습니다.\n");
}

int main(int argc, char* argv[])
{
    SetKoreanOutputEncoding(); // 한글 출력 인코딩 설정
    setlocale(LC_ALL, "");    // 로케일 설정

    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_help(argv[0]);
        return 0;
    }

    if (argc < 2) {
        PrintWithTimestampf(L"사용법: %S [인스턴스 개수]\n", argv[0]);
        return 1;
    }
    if (argc >= 3)
    {
        setMacroPath(argv[2]);
    } else
    {
        setMacroPath(NULL);
    }
    setMuMuPath();

    int instanceCount = atoi(argv[1]);
    while (1)
    {
        run(instanceCount);
        Sleep(1000 * 60);
    }
    return 0;
}

