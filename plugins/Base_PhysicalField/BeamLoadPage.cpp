#include "BeamLoadPage.h"
#include "ui_BeamLoadPage.h"

#include <QDebug>

#include "PickManager.h"
#include <PublicVariables.h>
#include <PublicConstants.h>

BeamLoadPage::BeamLoadPage(QWidget *parent)
    : BasePage("BeamLoadPage")
    , ui(new Ui::BeamLoadPage)
{
    ui->setupUi(this);

    // 一些默认值（你可按需要改）
    ui->spinBoxLDTYPE->setValue(0);
    ui->doubleSpinBoxA->setValue(0.0);
    ui->doubleSpinBoxP->setValue(0.0);
    ui->spinBoxIDIR->setValue(1);

}

BeamLoadPage::~BeamLoadPage()
{
    delete ui;
}

void BeamLoadPage::beforeLoad()
{
    PickManager::instance();
    PickManager::instance().updateStructure();

    // 2D 时禁用 ZP（写出时固定为 0）
    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const bool is2D = (NDIM == 2);

    qDebug() << "[BeamLoadPage] beforeLoad activate Edge pick, uuid=" << m_uuid;
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
}

void BeamLoadPage::beforeUnload()
{
    qDebug() << "[BeamLoadPage] beforeUnload deactivate Edge pick, uuid=" << m_uuid;
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
}


int BeamLoadPage::ldTypeValue() const { return ui->spinBoxLDTYPE->value(); }
double BeamLoadPage::aValue() const    { return ui->doubleSpinBoxA->value(); }
double BeamLoadPage::pValue() const    { return ui->doubleSpinBoxP->value(); }
int BeamLoadPage::idirValue() const    { return ui->spinBoxIDIR->value(); }

