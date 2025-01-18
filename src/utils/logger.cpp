#include <iostream>

static double timeFrequency;
static HANDLE consoleHandle;

Log::Log() {
#ifdef _DEBUG
    AllocConsole();
    SetConsoleTitleA("BetterVR Debugging Console");
    consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
#endif
    Log::print("Successfully started BetterVR!");
    LARGE_INTEGER timeLI;
    QueryPerformanceFrequency(&timeLI);
    timeFrequency = double(timeLI.QuadPart) / 1000.0;
}

Log::~Log() {
    Log::print("Shutting down BetterVR debugging console...");
#ifdef _DEBUG
    FreeConsole();
#endif
}

void Log::print(const char* message) {
#ifdef _DEBUG
    std::string messageStr(message);
    messageStr += "\n";
    DWORD charsWritten = 0;
    WriteConsoleA(consoleHandle, messageStr.c_str(), (DWORD)messageStr.size(), &charsWritten, NULL);
    OutputDebugStringA(messageStr.c_str());
#else
    std::cout << message << std::endl;
#endif
}

void Log::printTimeElapsed(const char* message_prefix, LARGE_INTEGER time) {
    LARGE_INTEGER timeNow;
    QueryPerformanceCounter(&timeNow);
    Log::print("{}: {} ms", message_prefix, double(time.QuadPart - timeNow.QuadPart) / timeFrequency);
}