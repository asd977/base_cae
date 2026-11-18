#pragma once
#include <string>
#include <algorithm>
#include <cctype>

// Error-only replay for diagnostics written to unit 110 rather than stdout.
// Never replay the banner/progress that has already arrived through the pipe.
inline std::string nativeSolverErrorTail(const std::string& text)
{
    size_t error=std::string::npos;
    for(size_t pos=0;pos<text.size();)
    {
        const auto end=text.find('\n',pos);
        std::string line=text.substr(pos,end==std::string::npos?end:end-pos);
        const auto first=line.find_first_not_of(" \t\r");
        if(first!=std::string::npos)
        {
            line=line.substr(first);
            std::transform(line.begin(),line.end(),line.begin(),[](unsigned char c){return char(std::toupper(c));});
            if(line=="ERROR"||line.rfind("ERROR ",0)==0||line.rfind("ERROR:",0)==0||line.rfind("FORRTL: SEVERE",0)==0)
                error=pos;
        }
        if(end==std::string::npos)break;
        pos=end+1;
    }
    return error==std::string::npos?std::string():text.substr(error);
}
