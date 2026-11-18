#include "../solver/SolverErrorLog.h"
#include <iostream>
int main()
{
 const std::string error=" ERROR IN SUB TRAN3D:ONE SIDE OF TRIANGLE IS ZERO\n I=1 XYABC=0\n DAB=0\n";
 if(nativeSolverErrorTail("banner\nIAUTO=3\n"+error)!=error)return 1;
 if(!nativeSolverErrorTail("normal startup\nno failure\n").empty())return 2;
 if(nativeSolverErrorTail("old\nERROR old\nobsolete\n"+error)!=error)return 3;
 std::cout<<"PASS: error context retained without replaying startup/progress\n";
}
