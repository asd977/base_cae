#ifndef SOLVER_H
#define SOLVER_H

#include "CurveWidget.h"
#include "mfem.hpp"
class Solver
{
public:
    Solver();
    void test();
private:
    CurveWidget *m_curveWidget;

};

#endif // SOLVER_H
