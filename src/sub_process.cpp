#include <stdexcept>
#include "sub_process.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace native_impl {

struct SubProcess {
    explicit SubProcess(const std::string& cmd, const std::string& input, const std::string& pwd) {
        std::memset(&pi, 0, sizeof(PROCESS_INFORMATION));

        {
            SECURITY_ATTRIBUTES saAttr;
            saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
            saAttr.bInheritHandle = TRUE;
            saAttr.lpSecurityDescriptor = NULL;

            if (!CreatePipe(&outputHandleRead, &outputHandleWrite, &saAttr, 0)) {
                good = false;
                error = "Failed to create pipe";
                return;
            }

            if (!CreatePipe(&inputHandleRead, &inputHandleWrite, &saAttr, 0)) {
                good = false;
                error = "Failed to create pipe";
                return;
            }
        }

        STARTUPINFOA si = { 0 };
        si.cb = sizeof(si);
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = outputHandleWrite;
        si.hStdError = outputHandleWrite;
        si.hStdInput = inputHandleRead;

        BOOL success = CreateProcessA(
            NULL,
            (LPSTR)cmd.c_str(),
            NULL,
            NULL,
            TRUE,
            CREATE_NO_WINDOW,
            NULL,
            (LPCSTR)(pwd.empty() ? NULL : pwd.c_str()),
            &si,
            &pi
        );
        if (!success) {
            good = false;
            error = "Failed to create process";
            return;
        }
        
        // Close handles to the stdin and stdout pipes no longer needed by the child process.
        // If they are not explicitly closed, there is no way to recognize that the child process has ended.
        CloseHandle(outputHandleWrite);
        CloseHandle(inputHandleRead);

        if (!input.empty()) {
            std::string minput = input + "\n";

            DWORD writtenBytes;
            success = WriteFile(inputHandleWrite, (LPCVOID)minput.c_str(), (DWORD)minput.size(), &writtenBytes, NULL);
        }

        good = true;
        error = "";
    }

    bool ReadChar(char& c) {
        DWORD dwRead;
        BOOL bSuccess = FALSE;

        bSuccess = ReadFile( outputHandleRead, &c, 1, &dwRead, NULL);
        
        return bSuccess && (dwRead == 1);
    }

    ~SubProcess() {
        CloseHandle(inputHandleWrite);
        CloseHandle(outputHandleRead);
        TerminateProcess(pi.hProcess, 0);

        // Close handles to the child process and its primary thread.
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    PROCESS_INFORMATION pi;
    HANDLE outputHandleRead;
    HANDLE outputHandleWrite;
    HANDLE inputHandleRead;
    HANDLE inputHandleWrite;
    std::string error;
    bool good;
};

}

#endif


SubProcess::SubProcess(const std::string& cmd, const std::string& pwd)
{
    handle = static_cast<void*>(new native_impl::SubProcess{ cmd , "", pwd });
}

SubProcess::~SubProcess()
{
    delete static_cast<native_impl::SubProcess*>(handle);
}

bool SubProcess::ReadChar(char& c)
{
    auto native = static_cast<native_impl::SubProcess*>(handle);
    return native->ReadChar(c);
}

bool SubProcess::Good()
{
    return static_cast<native_impl::SubProcess*>(handle)->good;
}