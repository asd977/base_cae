#include "PointForcePage.h"
#include "ui_PointForcePage.h"

#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QMessageBox>
#include <QDebug>
#include <ServiceRegistry.h>
#include <PublicVariables.h>

#include "PickManager.h"
#include <PublicConstants.h>
#include <services/MeshModuleControllerService.h>

PointForcePage::PointForcePage(QWidget* parent)
    : BasePage("PointForcePage")
    , ui(new Ui::PointForcePage)
{
    ui->setupUi(this);

}

PointForcePage::~PointForcePage() = default;

void PointForcePage::beforeLoad()
{
    PickManager::instance();
    PickManager::instance().updateStructure();

    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);

    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const bool is3D = (NDIM == 3);

    // 1. 对于 3D 特有的参数，2D 时置灰 (Enabled = false)
    ui->spinFz->setEnabled(is3D);
    ui->spinMx->setEnabled(is3D);
    ui->spinMy->setEnabled(is3D);

    // 建议：对应的 Label 也同步置灰，视觉上更统一
    ui->labelFz->setEnabled(is3D);
    ui->labelMx->setEnabled(is3D);
    ui->labelMy->setEnabled(is3D);

    // 2. 对于 Mz/M，始终可用，仅根据维度切换标签文字
    ui->spinMz->setEnabled(true);
    ui->labelMz->setText(is3D ? "MZ" : "M");
}

void PointForcePage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
}

double PointForcePage::fxValue() const { return ui->spinFx->value(); }
double PointForcePage::fyValue() const { return ui->spinFy->value(); }
double PointForcePage::fzValue() const { return ui->spinFz->value(); }

double PointForcePage::mxValue() const { return ui->spinMx->value(); }
double PointForcePage::myValue() const { return ui->spinMy->value(); }
double PointForcePage::mzValue() const { return ui->spinMz->value(); }
