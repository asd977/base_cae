#include "SolverResultFocusPage.h"

#include "ui_SolverResultFocusPage.h"

SolverResultFocusPage::SolverResultFocusPage(QWidget* parent) : BasePage("SolverResultFocusPage"), ui(new Ui::SolverResultFocusPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

SolverResultFocusPage::~SolverResultFocusPage()
{
    delete ui;
}
