#include "GeoStressPointPage.h"
#include "ui_GeoStressPointPage.h"

#include <QPushButton>
#include <QMessageBox>
#include <QDebug>

#include <PublicVariables.h>
#include <PublicConstants.h>

#include <ServiceRegistry.h>

#include "PhySicalField.h"

#include <PickManager.h>

// VTK
#include <vtkUnstructuredGrid.h>

GeoStressPointPage::GeoStressPointPage(QWidget* parent)
    : BasePage("GeoStressPointPage")
    , ui(new Ui::GeoStressPointPage)
{
    ui->setupUi(this);

    // 默认值
    ui->spinSx->setValue(0.0);
    ui->spinSy->setValue(0.0);
    ui->spinSz->setValue(0.0);
    ui->spinSxy->setValue(0.0);
    ui->spinSxz->setValue(0.0);
    ui->spinSyz->setValue(0.0);

}

GeoStressPointPage::~GeoStressPointPage()
{
    delete ui;
}

void GeoStressPointPage::beforeLoad()
{
    PickManager::instance();
    PickManager::instance().updateStructure();

    m_dim = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    if (m_dim != 2) m_dim = 3;

    applyDimensionUi(m_dim);

    QObject::connect(&PickManager::instance(),
                     &PickManager::nodePicked,
                     this,
                     &GeoStressPointPage::onNodePicked,
                     Qt::UniqueConnection);

    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
}

void GeoStressPointPage::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);

    QObject::disconnect(&PickManager::instance(),
                        &PickManager::nodePicked,
                        this,
                        &GeoStressPointPage::onNodePicked);
}

void GeoStressPointPage::applyDimensionUi(int dim)
{
    const bool is2D = (dim == 2);

    // 统一定义一个禁用样式
    QString disabledStyle = is2D ? "color: #A0A0A0; background-color: #F5F5F5;" : "";

    auto disableWidget = [&](QWidget* w) {
        w->setEnabled(!is2D);
        w->setStyleSheet(disabledStyle); // 显式设置样式
    };

    disableWidget(ui->spinSz);
    disableWidget(ui->spinSxz);
    disableWidget(ui->spinSyz);
    disableWidget(ui->labelSz);
    disableWidget(ui->labelSxz);
    disableWidget(ui->labelSyz);
    disableWidget(ui->labelZ);
    disableWidget(ui->labelZValue);

    if (is2D) {
        ui->spinSz->setValue(0.0);
        ui->spinSxz->setValue(0.0);
        ui->spinSyz->setValue(0.0);
        ui->labelZValue->setText("0");
    }
}

double GeoStressPointPage::sxValue()  const { return ui->spinSx->value();  }
double GeoStressPointPage::syValue()  const { return ui->spinSy->value();  }
double GeoStressPointPage::szValue()  const { return ui->spinSz->value();  }
double GeoStressPointPage::sxyValue() const { return ui->spinSxy->value(); }
double GeoStressPointPage::sxzValue() const { return ui->spinSxz->value(); }
double GeoStressPointPage::syzValue() const { return ui->spinSyz->value(); }

void GeoStressPointPage::refreshPrevOwnerTitle(const QString& prevUuid)
{
    // 你说 prevUuid 没有互斥就设为 -1（或空字符串），这里都兼容
    if (prevUuid.isEmpty() || prevUuid == "-1")
    {
        ui->labelPrevOwnerValue->clear();
        return;
    }

    auto* svc = ServiceRegistry::instance().getService<PhysicalFieldService>();
    if (!svc)
    {
        ui->labelPrevOwnerValue->setText(prevUuid); // 兜底：显示 uuid
        return;
    }

    const QString title = svc->pageTitleByUuid(prevUuid);
    ui->labelPrevOwnerValue->setText(title.isEmpty() ? prevUuid : title);
}

void GeoStressPointPage::onNodePicked(const QString& domain,
                                      const QString& activeUuid,
                                      GridKind,
                                      vtkIdType,
                                      int nodeId,
                                      double x, double y, double z,
                                      const QString& prevUuid)
{
    if (domain != getPageType()) return;
    if (activeUuid != m_uuid) return;

    ui->labelNodeIdValue->setText(QString::number(nodeId));
    ui->labelXValue->setText(QString::number(x, 'g', 12));
    ui->labelYValue->setText(QString::number(y, 'g', 12));

    if (m_dim == 2)
        ui->labelZValue->setText("0");
    else
        ui->labelZValue->setText(QString::number(z, 'g', 12));

    refreshPrevOwnerTitle(prevUuid);
}
