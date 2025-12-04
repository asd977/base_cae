#ifndef SOLVER_H
#define SOLVER_H

#include "CurveWidget.h"
#include "mfem.hpp"
#include <ProjectNode.h>
#include <services/SolverService.h>
class Solver :public SolverService
{
public:
    Solver();
    void test();

    void newProject();
    bool saveProject();
    bool loadProject();
    void closeProject();

private:
    CurveWidget *m_curveWidget;
    ProjectNode *m_root;

};

#endif // SOLVER_H
