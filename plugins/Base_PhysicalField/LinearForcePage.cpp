#include "LinearForcePage.h"
#include "ui_LinearForcePage.h"

#include <PickManager.h>

LinearForcePage::LinearForcePage(QWidget *parent)
    : BasePage("LinearForcePage")
    , ui(new Ui::LinearForcePage)
{
    ui->setupUi(this);
}

LinearForcePage::~LinearForcePage()
{
    delete ui;
}

void LinearForcePage::beforeLoad()
{
    PickManager::instance();
    PickManager::instance().updateStructure();

    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Edge);

}

void LinearForcePage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(),m_uuid,PickMode::Edge);
}

double LinearForcePage::fxValue()
{
    return ui->lineEditX->text().toDouble();
}

double LinearForcePage::fyValue()
{
    return ui->lineEditY->text().toDouble();
}

double LinearForcePage::fzValue()
{
    return ui->lineEditZ->text().toDouble();
}
