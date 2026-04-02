#include "GeoStressMeasurePointPage.h"

#include <vtkDataArray.h>
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>

#include <QFormLayout>
#include <QJsonArray>
#include <QLineEdit>
#include <QPushButton>

GeoStressMeasurePointPage::GeoStressMeasurePointPage(QWidget* parent)
    : BasePage("GeoStressRegressionMeasurePoint")
{
    Q_UNUSED(parent);
    auto* form = new QFormLayout(this);

    m_pointIdEdit = new QLineEdit(this);
    m_pointIdEdit->setReadOnly(true);
    m_clearBtn = new QPushButton("清空所选", this);
    m_sgxEdit = new QLineEdit("0", this);
    m_sgyEdit = new QLineEdit("0", this);
    m_txyEdit = new QLineEdit("0", this);
    m_iiEdit = new QLineEdit("1", this);

    form->addRow("实测点(通过PickManager选点)", m_pointIdEdit);
    form->addRow("", m_clearBtn);
    form->addRow("SGX", m_sgxEdit);
    form->addRow("SGY", m_sgyEdit);
    form->addRow("TXY", m_txyEdit);
    form->addRow("序号", m_iiEdit);

    connect(m_clearBtn, &QPushButton::clicked, this, &GeoStressMeasurePointPage::onClearClicked);
}

void GeoStressMeasurePointPage::beforeLoad()
{
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
}

void GeoStressMeasurePointPage::beforeUnload()
{
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
}

QJsonObject GeoStressMeasurePointPage::saveJson()
{
    QJsonObject obj;
    obj["sgx"] = m_sgxEdit->text().trimmed();
    obj["sgy"] = m_sgyEdit->text().trimmed();
    obj["txy"] = m_txyEdit->text().trimmed();
    obj["ii"] = m_iiEdit->text().trimmed();

    QJsonArray arr;
    for (vtkIdType id : PickManager::instance().getSelectedPointIds(getPageType(), m_uuid, GridKind::Solid))
        arr.append(qint64(id));
    obj["picked_points"] = arr;

    return obj;
}

void GeoStressMeasurePointPage::loadJson(const QJsonObject& obj)
{
    m_sgxEdit->setText(obj.value("sgx").toString(m_sgxEdit->text()));
    m_sgyEdit->setText(obj.value("sgy").toString(m_sgyEdit->text()));
    m_txyEdit->setText(obj.value("txy").toString(m_txyEdit->text()));
    m_iiEdit->setText(obj.value("ii").toString(m_iiEdit->text()));

    QVector<vtkIdType> ids;
    for (const auto& v : obj.value("picked_points").toArray())
        ids.push_back(v.toVariant().toLongLong());
    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, ids, true);

    m_pointIdEdit->setText(ids.isEmpty() ? QString() : QString::number(ids.first()));
}

GeoStressMeasurePointPage::MeasurePoint GeoStressMeasurePointPage::point() const
{
    MeasurePoint p;
    p.sgx = m_sgxEdit->text().trimmed();
    p.sgy = m_sgyEdit->text().trimmed();
    p.txy = m_txyEdit->text().trimmed();
    p.ii = m_iiEdit->text().trimmed();

    const auto ids = PickManager::instance().getSelectedPointIds(getPageType(), m_uuid, GridKind::Solid);
    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    if (!ids.isEmpty() && grid)
    {
        const vtkIdType pid = ids.first();
        if (pid >= 0 && pid < grid->GetNumberOfPoints())
        {
            double xyz[3] = {0, 0, 0};
            grid->GetPoint(pid, xyz);
            p.x = QString::number(xyz[0]);
            p.y = QString::number(xyz[1]);
        }
    }
    if (p.x.isEmpty())
        p.x = "0";
    if (p.y.isEmpty())
        p.y = "0";
    return p;
}

void GeoStressMeasurePointPage::onClearClicked()
{
    PickManager::instance().clearSelection(getPageType(), m_uuid, PickMode::Node);
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
    m_pointIdEdit->clear();
}
