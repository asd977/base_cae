#include "GeoStressMeasurePointPage.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <vtkDataArray.h>
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>

#include <QCheckBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextStream>
#include <QVBoxLayout>

namespace
{
QString valueOrZero(const QLineEdit* edit)
{
    const QString value = edit ? edit->text().trimmed() : QString();
    return value.isEmpty() ? QStringLiteral("0") : value;
}

QStringList splitDataLine(const QString& line)
{
    return line.trimmed().split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
}
}

GeoStressMeasurePointPage::GeoStressMeasurePointPage(QWidget* parent)
    : BasePage("GeoStressRegressionMeasurePoint")
{
    Q_UNUSED(parent);
    auto* mainLayout = new QVBoxLayout(this);

    auto* pickGroup = new QGroupBox("测点选择", this);
    auto* pickLayout = new QVBoxLayout(pickGroup);
    auto* pickHintLabel = new QLabel("提示：Shift+拖拽框选，Ctrl+拖拽取消选择", pickGroup);
    pickHintLabel->setWordWrap(true);

    m_pointIdEdit = new QLineEdit(pickGroup);
    m_pointIdEdit->setReadOnly(true);
    m_clearBtn = new QPushButton("清空所选", pickGroup);
    m_pickEnableCheckBox = new QCheckBox("开启点选", pickGroup);

    auto* pointRow = new QHBoxLayout;
    pointRow->addWidget(new QLabel("实测点", pickGroup));
    pointRow->addWidget(m_pointIdEdit, 1);
    pointRow->addWidget(m_clearBtn);

    auto* actionRow = new QHBoxLayout;
    actionRow->addWidget(m_pickEnableCheckBox);
    actionRow->addStretch(1);

    pickLayout->addWidget(pickHintLabel);
    pickLayout->addLayout(pointRow);
    pickLayout->addLayout(actionRow);
    mainLayout->addWidget(pickGroup);

    auto* dataGroup = new QGroupBox("实测地应力数据", this);
    auto* form = new QFormLayout(dataGroup);
    m_xEdit = new QLineEdit("0", dataGroup);
    m_yEdit = new QLineEdit("0", dataGroup);
    m_zEdit = new QLineEdit("0", dataGroup);
    m_sgxEdit = new QLineEdit("0", dataGroup);
    m_sgyEdit = new QLineEdit("0", dataGroup);
    m_sgzEdit = new QLineEdit("0", dataGroup);
    m_txyEdit = new QLineEdit("0", dataGroup);
    m_tyzEdit = new QLineEdit("0", dataGroup);
    m_tzxEdit = new QLineEdit("0", dataGroup);
    m_importBtn = new QPushButton("导入数据", dataGroup);

    form->addRow("XP", m_xEdit);
    form->addRow("YP", m_yEdit);
    m_zLabel = new QLabel("ZP", dataGroup);
    form->addRow(m_zLabel, m_zEdit);
    form->addRow("SGX", m_sgxEdit);
    form->addRow("SGY", m_sgyEdit);
    m_sgzLabel = new QLabel("SGZ", dataGroup);
    form->addRow(m_sgzLabel, m_sgzEdit);
    form->addRow("TXY", m_txyEdit);
    m_tyzLabel = new QLabel("TYZ", dataGroup);
    form->addRow(m_tyzLabel, m_tyzEdit);
    m_tzxLabel = new QLabel("TZX", dataGroup);
    form->addRow(m_tzxLabel, m_tzxEdit);
    form->addRow("", m_importBtn);
    mainLayout->addWidget(dataGroup);
    mainLayout->addStretch(1);

    connect(m_clearBtn, &QPushButton::clicked, this, &GeoStressMeasurePointPage::onClearClicked);
    connect(m_importBtn, &QPushButton::clicked, this, &GeoStressMeasurePointPage::onImportClicked);
    connect(m_pickEnableCheckBox, &QCheckBox::toggled, this,
            [this](bool on)
            {
                PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, on);
            });

    applyDimensionUi();
}

void GeoStressMeasurePointPage::beforeLoad()
{
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
    QObject::connect(&PickManager::instance(), &PickManager::nodePicked, this,
                     &GeoStressMeasurePointPage::onNodePicked, Qt::UniqueConnection);
    if (m_pickEnableCheckBox)
        m_pickEnableCheckBox->setChecked(false);
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, false);
    applyDimensionUi();
}

void GeoStressMeasurePointPage::beforeUnload()
{
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, true);
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
    QObject::disconnect(&PickManager::instance(), &PickManager::nodePicked, this,
                        &GeoStressMeasurePointPage::onNodePicked);
}

QJsonObject GeoStressMeasurePointPage::saveJson()
{
    QJsonObject obj;
    obj["x"] = m_xEdit->text().trimmed();
    obj["y"] = m_yEdit->text().trimmed();
    obj["z"] = m_zEdit->text().trimmed();
    obj["sgx"] = m_sgxEdit->text().trimmed();
    obj["sgy"] = m_sgyEdit->text().trimmed();
    obj["sgz"] = m_sgzEdit->text().trimmed();
    obj["txy"] = m_txyEdit->text().trimmed();
    obj["tyz"] = m_tyzEdit->text().trimmed();
    obj["tzx"] = m_tzxEdit->text().trimmed();

    QJsonArray arr;
    for (vtkIdType id : PickManager::instance().getSelectedPointIds(getPageType(), m_uuid, GridKind::Solid))
        arr.append(qint64(id));
    obj["picked_points"] = arr;

    return obj;
}

void GeoStressMeasurePointPage::loadJson(const QJsonObject& obj)
{
    m_xEdit->setText(obj.value("x").toString(m_xEdit->text()));
    m_yEdit->setText(obj.value("y").toString(m_yEdit->text()));
    m_zEdit->setText(obj.value("z").toString(m_zEdit->text()));
    m_sgxEdit->setText(obj.value("sgx").toString(m_sgxEdit->text()));
    m_sgyEdit->setText(obj.value("sgy").toString(m_sgyEdit->text()));
    m_sgzEdit->setText(obj.value("sgz").toString(m_sgzEdit->text()));
    m_txyEdit->setText(obj.value("txy").toString(m_txyEdit->text()));
    m_tyzEdit->setText(obj.value("tyz").toString(m_tyzEdit->text()));
    m_tzxEdit->setText(obj.value("tzx").toString(m_tzxEdit->text()));

    QVector<vtkIdType> ids;
    for (const auto& v : obj.value("picked_points").toArray())
        ids.push_back(v.toVariant().toLongLong());
    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, ids, true);

    m_pointIdEdit->setText(ids.isEmpty() ? QString() : QString::number(ids.first()));
    if (!obj.contains("x") && !ids.isEmpty())
    {
        vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
        const vtkIdType pid = ids.first();
        if (grid && pid >= 0 && pid < grid->GetNumberOfPoints())
        {
            double xyz[3] = {0, 0, 0};
            grid->GetPoint(pid, xyz);
            setCoordinateValues(xyz[0], xyz[1], xyz[2]);
        }
    }
    applyDimensionUi();
}

GeoStressMeasurePointPage::MeasurePoint GeoStressMeasurePointPage::point() const
{
    MeasurePoint p;
    p.x = valueOrZero(m_xEdit);
    p.y = valueOrZero(m_yEdit);
    p.z = valueOrZero(m_zEdit);
    p.sgx = valueOrZero(m_sgxEdit);
    p.sgy = valueOrZero(m_sgyEdit);
    p.sgz = valueOrZero(m_sgzEdit);
    p.txy = valueOrZero(m_txyEdit);
    p.tyz = valueOrZero(m_tyzEdit);
    p.tzx = valueOrZero(m_tzxEdit);
    return p;
}

void GeoStressMeasurePointPage::onClearClicked()
{
    PickManager::instance().clearSelection(getPageType(), m_uuid, PickMode::Node);
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
    m_pointIdEdit->clear();
}

void GeoStressMeasurePointPage::onImportClicked()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this, "导入实测地应力数据", QString(), "Data Files (*.txt *.dat *.csv);;All Files (*)");
    if (filePath.trimmed().isEmpty())
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream in(&file);
    while (!in.atEnd())
    {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        if (applyImportedValues(splitDataLine(line)))
            return;
    }
}

void GeoStressMeasurePointPage::onNodePicked(const QString& domain, const QString& activeUuid,
                                             GridKind, vtkIdType pid, int nodeId, double x,
                                             double y, double z, const QString&)
{
    if (domain != getPageType() || activeUuid != m_uuid)
        return;

    m_pointIdEdit->setText(QString::number(nodeId));
    setCoordinateValues(x, y, z);
    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid,
                                                QVector<vtkIdType>{pid}, true);
}

int GeoStressMeasurePointPage::currentDimension() const
{
    return PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt() == 2 ? 2 : 3;
}

void GeoStressMeasurePointPage::applyDimensionUi()
{
    const bool is3D = currentDimension() == 3;
    if (m_zLabel)
        m_zLabel->setVisible(is3D);
    if (m_zEdit)
        m_zEdit->setVisible(is3D);
    if (m_sgzLabel)
        m_sgzLabel->setVisible(is3D);
    if (m_sgzEdit)
        m_sgzEdit->setVisible(is3D);
    if (m_tyzLabel)
        m_tyzLabel->setVisible(is3D);
    if (m_tyzEdit)
        m_tyzEdit->setVisible(is3D);
    if (m_tzxLabel)
        m_tzxLabel->setVisible(is3D);
    if (m_tzxEdit)
        m_tzxEdit->setVisible(is3D);
}

bool GeoStressMeasurePointPage::applyImportedValues(const QStringList& values)
{
    const int dim = currentDimension();
    const int minColumns = dim == 3 ? 9 : 5;
    if (values.size() < minColumns)
        return false;

    for (int col = 0; col < minColumns; ++col)
    {
        bool ok = false;
        values.value(col).toDouble(&ok);
        if (!ok)
            return false;
    }

    int i = 0;
    m_xEdit->setText(values.value(i++));
    m_yEdit->setText(values.value(i++));
    if (dim == 3)
        m_zEdit->setText(values.value(i++));
    else
        m_zEdit->setText("0");
    m_sgxEdit->setText(values.value(i++));
    m_sgyEdit->setText(values.value(i++));
    if (dim == 3)
        m_sgzEdit->setText(values.value(i++));
    m_txyEdit->setText(values.value(i++));
    if (dim == 3)
    {
        m_tyzEdit->setText(values.value(i++));
        m_tzxEdit->setText(values.value(i++));
    }
    applyDimensionUi();
    return true;
}

void GeoStressMeasurePointPage::setCoordinateValues(double x, double y, double z)
{
    m_xEdit->setText(QString::number(x, 'g', 12));
    m_yEdit->setText(QString::number(y, 'g', 12));
    m_zEdit->setText(currentDimension() == 3 ? QString::number(z, 'g', 12) : QStringLiteral("0"));
}
