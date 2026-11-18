// Frontend-owned adapter for the legacy RUNQQ error-viewer protocol.
// The solver and its libraries remain unchanged. No GUI and no C-drive file.
#include <windows.h>
#include <fstream>
#include <string>
#include "SolverErrorLog.h"

int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int)
{
    std::ifstream control("D:\\IIIDIR.DAT",std::ios::binary);
    std::string directory;
    if(!std::getline(control,directory))return 2;
    const auto first=directory.find('\'');
    const auto last=directory.find('\'',first==std::string::npos?0:first+1);
    if(first==std::string::npos||last==std::string::npos)return 3;
    directory=directory.substr(first+1,last-first-1);
    std::ifstream log(directory+"III110.DAT",std::ios::binary);
    if(!log)return 4;
    log.seekg(0,std::ios::end);
    const auto size=log.tellg();
    log.seekg(size>std::streamoff(256*1024)?size-std::streamoff(256*1024):std::streamoff(0));
    const std::string text=nativeSolverErrorTail(std::string((std::istreambuf_iterator<char>(log)),{}));
    const HANDLE output=GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written=0;
    if(output!=nullptr&&output!=INVALID_HANDLE_VALUE)
        WriteFile(output,text.data(),static_cast<DWORD>(text.size()),&written,nullptr);
    return 1;
}
