#include "GeoStressMeasurePointPage.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <vtkDataArray.h>
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <functional>
#include <limits>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPushButton>
#include <QRegularExpression>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>

#include "ui_GeoStressMeasurePointPage.h"

namespace
{
QString valueOrZero(const QString& text)
{
    const QString value = text.trimmed();
    return value.isEmpty() ? QStringLiteral("0") : value;
}

QStringList splitDataLine(const QString& line)
{
    return line.trimmed().split(QRegularExpression(QStringLiteral("[,\\s]+")), QString::SkipEmptyParts);
}

bool isNumericRow(const QStringList& values, int count)
{
    if (values.size() < count)
        return false;
    for (int i = 0; i < count; ++i)
    {
        bool ok = false;
        values.value(i).toDouble(&ok);
        if (!ok)
            return false;
    }
    return true;
}
}

GeoStressMeasurePointPage::GeoStressMeasurePointPage(QWidget* parent)
    : BasePage("GeoStressRegressionMeasurePoint"), ui(new Ui::GeoStressMeasurePointPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);

    ui->measureTable->setHorizontalHeaderLabels(QStringList() << "点号" << "XP" << "YP" << "ZP" << "SGX"
                                             << "SGY" << "SGZ" << "TXY" << "TYZ" << "TZX"
                                             << "pid");
    ui->measureTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->measureTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->measureTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
                             QAbstractItemView::SelectedClicked);
    ui->measureTable->horizontalHeader()->setStretchLastSection(false);
    ui->measureTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->measureTable->horizontalHeader()->setSectionResizeMode(ColX, QHeaderView::Stretch);
    ui->measureTable->horizontalHeader()->setSectionResizeMode(ColY, QHeaderView::Stretch);
    ui->measureTable->setColumnHidden(ColPid, true);

    connect(ui->clearButton, &QPushButton::clicked, this, &GeoStressMeasurePointPage::onClearClicked);
    connect(ui->importButton, &QPushButton::clicked, this, &GeoStressMeasurePointPage::onImportClicked);
    connect(ui->addManualRowButton, &QPushButton::clicked, this,
            &GeoStressMeasurePointPage::onAddManualRowClicked);
    connect(ui->appendSelectedPointsButton, &QPushButton::clicked, this,
            &GeoStressMeasurePointPage::onAppendSelectedPointsClicked);
    connect(ui->removeRowsButton, &QPushButton::clicked, this, &GeoStressMeasurePointPage::onRemoveRowsClicked);
    connect(ui->pickEnableCheckBox, &QCheckBox::toggled, this,
            [this](bool on)
            {
                PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, on);
            });

    applyDimensionUi();
}

GeoStressMeasurePointPage::~GeoStressMeasurePointPage()
{
    delete ui;
}

void GeoStressMeasurePointPage::beforeLoad()
{
    PickManager::instance().updateStructure();
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
    QObject::connect(&PickManager::instance(), &PickManager::nodePicked, this,
                     &GeoStressMeasurePointPage::onNodePicked, Qt::UniqueConnection);
    if (ui->pickEnableCheckBox)
        ui->pickEnableCheckBox->setChecked(false);
    PickManager::instance().setClickPickEnabled(getPageType(), m_uuid, false);
    applyDimensionUi();
    syncPickedPointIds();
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
    QJsonArray rows;
    for (int row = 0; row < ui->measureTable->rowCount(); ++row)
    {
        QJsonObject item;
        item["node_id"] = cellText(row, ColNodeId);
        item["x"] = cellText(row, ColX);
        item["y"] = cellText(row, ColY);
        item["z"] = cellText(row, ColZ);
        item["sgx"] = cellText(row, ColSgx);
        item["sgy"] = cellText(row, ColSgy);
        item["sgz"] = cellText(row, ColSgz);
        item["txy"] = cellText(row, ColTxy);
        item["tyz"] = cellText(row, ColTyz);
        item["tzx"] = cellText(row, ColTzx);
        item["pid"] = cellText(row, ColPid);
        rows.append(item);
    }
    obj["measure_points"] = rows;
    return obj;
}

void GeoStressMeasurePointPage::loadJson(const QJsonObject& obj)
{
    ui->measureTable->setRowCount(0);
    const QJsonArray rows = obj.value("measure_points").toArray();
    for (const auto& value : rows)
    {
        const QJsonObject item = value.toObject();
        appendRow(item.value("node_id").toString(), item.value("x").toString("0"),
                  item.value("y").toString("0"), item.value("z").toString("0"),
                  item.value("sgx").toString("0"), item.value("sgy").toString("0"),
                  item.value("sgz").toString("0"), item.value("txy").toString("0"),
                  item.value("tyz").toString("0"), item.value("tzx").toString("0"),
                  item.value("pid").toString("-1").toLongLong());
    }

    // 兼容旧工程：旧版本仅保存单个测点字段和 picked_points。
    if (rows.isEmpty() && (obj.contains("sgx") || obj.contains("picked_points")))
    {
        QString nodeId;
        vtkIdType pid = -1;
        const QJsonArray picked = obj.value("picked_points").toArray();
        if (!picked.isEmpty())
        {
            pid = picked.first().toVariant().toLongLong();
            nodeId = QString::number(pid);
        }
        appendRow(nodeId, obj.value("x").toString("0"), obj.value("y").toString("0"),
                  obj.value("z").toString("0"), obj.value("sgx").toString("0"),
                  obj.value("sgy").toString("0"), obj.value("sgz").toString("0"),
                  obj.value("txy").toString("0"), obj.value("tyz").toString("0"),
                  obj.value("tzx").toString("0"), pid);
    }

    applyDimensionUi();
    syncPickedPointIds();
}

GeoStressMeasurePointPage::MeasurePoint GeoStressMeasurePointPage::point() const
{
    const QVector<MeasurePoint> all = points();
    return all.isEmpty() ? MeasurePoint{} : all.first();
}

QVector<GeoStressMeasurePointPage::MeasurePoint> GeoStressMeasurePointPage::points() const
{
    QVector<MeasurePoint> out;
    out.reserve(ui->measureTable->rowCount());
    for (int row = 0; row < ui->measureTable->rowCount(); ++row)
    {
        MeasurePoint p;
        p.x = valueOrZero(cellText(row, ColX));
        p.y = valueOrZero(cellText(row, ColY));
        p.z = valueOrZero(cellText(row, ColZ));
        p.sgx = valueOrZero(cellText(row, ColSgx));
        p.sgy = valueOrZero(cellText(row, ColSgy));
        p.sgz = valueOrZero(cellText(row, ColSgz));
        p.txy = valueOrZero(cellText(row, ColTxy));
        p.tyz = valueOrZero(cellText(row, ColTyz));
        p.tzx = valueOrZero(cellText(row, ColTzx));
        out.push_back(p);
    }
    return out;
}

void GeoStressMeasurePointPage::onClearClicked()
{
    ui->measureTable->setRowCount(0);
    PickManager::instance().clearSelection(getPageType(), m_uuid, PickMode::Node);
    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
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
        appendImportedValues(splitDataLine(line));
    }
    syncPickedPointIds();
}

void GeoStressMeasurePointPage::onAddManualRowClicked()
{
    appendRow(QString(), "0", "0", currentDimension() == 3 ? "0" : "0", "0", "0", "0", "0", "0", "0");
}

void GeoStressMeasurePointPage::onAppendSelectedPointsClicked()
{
    const auto ids = PickManager::instance().getSelectedPointIds(getPageType(), m_uuid, GridKind::Solid);
    for (vtkIdType pid : ids)
        appendPointByPid(pid);
    syncPickedPointIds();
}

void GeoStressMeasurePointPage::onRemoveRowsClicked()
{
    QList<int> rows;
    const auto ranges = ui->measureTable->selectedRanges();
    for (const QTableWidgetSelectionRange& range : ranges)
        for (int row = range.topRow(); row <= range.bottomRow(); ++row)
            rows.push_back(row);
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    for (int row : rows)
        ui->measureTable->removeRow(row);
    syncPickedPointIds();
}

void GeoStressMeasurePointPage::onNodePicked(const QString& domain, const QString& activeUuid,
                                             GridKind, vtkIdType pid, int nodeId, double x,
                                             double y, double z, const QString&)
{
    if (domain != getPageType() || activeUuid != m_uuid)
        return;

    Q_UNUSED(nodeId);
    Q_UNUSED(x);
    Q_UNUSED(y);
    Q_UNUSED(z);

    appendPointByPid(pid);
    syncPickedPointIds();
}

int GeoStressMeasurePointPage::currentDimension() const
{
    return PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt() == 2 ? 2 : 3;
}

void GeoStressMeasurePointPage::applyDimensionUi()
{
    const bool is3D = currentDimension() == 3;
    if (ui->dimensionHintLabel)
    {
        ui->dimensionHintLabel->setText(
            is3D ? QStringLiteral("三维格式：XP YP ZP SGX SGY SGZ TXY TYZ TZX")
                 : QStringLiteral("二维格式：XP YP SGX SGY TXY"));
    }
    if (!ui->measureTable)
        return;
    ui->measureTable->setColumnHidden(ColZ, !is3D);
    ui->measureTable->setColumnHidden(ColSgz, !is3D);
    ui->measureTable->setColumnHidden(ColTyz, !is3D);
    ui->measureTable->setColumnHidden(ColTzx, !is3D);
    ui->measureTable->setColumnHidden(ColPid, true);
}

bool GeoStressMeasurePointPage::appendImportedValues(const QStringList& values)
{
    const int dim = currentDimension();
    const int minColumns = dim == 3 ? 9 : 5;
    if (!isNumericRow(values, minColumns))
        return false;

    int i = 0;
    const QString x = values.value(i++);
    const QString y = values.value(i++);
    const QString z = dim == 3 ? values.value(i++) : QStringLiteral("0");
    const QString sgx = values.value(i++);
    const QString sgy = values.value(i++);
    const QString sgz = dim == 3 ? values.value(i++) : QStringLiteral("0");
    const QString txy = values.value(i++);
    const QString tyz = dim == 3 ? values.value(i++) : QStringLiteral("0");
    const QString tzx = dim == 3 ? values.value(i++) : QStringLiteral("0");

    QString nodeId;
    const vtkIdType pid = resolvePointIdByCoordinates(x, y, z, &nodeId);
    appendRow(nodeId, x, y, z, sgx, sgy, sgz, txy, tyz, tzx, pid);
    return true;
}

bool GeoStressMeasurePointPage::appendPointByPid(vtkIdType pid)
{
    for (int row = 0; row < ui->measureTable->rowCount(); ++row)
    {
        bool ok = false;
        const qlonglong existingPid = cellText(row, ColPid).toLongLong(&ok);
        if (ok && existingPid == pid)
            return false;
    }

    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    if (!grid || pid < 0 || pid >= grid->GetNumberOfPoints())
        return false;

    double xyz[3] = {0, 0, 0};
    grid->GetPoint(pid, xyz);

    QString nodeId = QString::number(pid);
    if (auto* nodeIdArr = vtkDataArray::SafeDownCast(grid->GetPointData()->GetArray("node_id")))
        nodeId = QString::number(int(nodeIdArr->GetTuple1(pid)));

    appendRow(nodeId, QString::number(xyz[0], 'g', 12), QString::number(xyz[1], 'g', 12),
              currentDimension() == 3 ? QString::number(xyz[2], 'g', 12) : QStringLiteral("0"),
              "0", "0", "0", "0", "0", "0", pid);
    return true;
}

vtkIdType GeoStressMeasurePointPage::resolvePointIdByCoordinates(const QString& xText,
                                                                  const QString& yText,
                                                                  const QString& zText,
                                                                  QString* nodeId) const
{
    if (nodeId)
        nodeId->clear();

    bool okX = false, okY = false, okZ = false;
    const double x = xText.toDouble(&okX);
    const double y = yText.toDouble(&okY);
    const double z = currentDimension() == 3 ? zText.toDouble(&okZ) : 0.0;
    if (!okX || !okY || (currentDimension() == 3 && !okZ))
        return -1;

    PickManager::instance().updateStructure();
    vtkUnstructuredGrid* grid = PickManager::instance().solidGrid();
    if (!grid || grid->GetNumberOfPoints() <= 0)
        return -1;

    vtkIdType bestPid = -1;
    double bestDist2 = std::numeric_limits<double>::max();
    for (vtkIdType pid = 0; pid < grid->GetNumberOfPoints(); ++pid)
    {
        double p[3] = {0, 0, 0};
        grid->GetPoint(pid, p);
        const double ddx = p[0] - x;
        const double ddy = p[1] - y;
        const double ddz = currentDimension() == 3 ? (p[2] - z) : 0.0;
        const double dist2 = ddx * ddx + ddy * ddy + ddz * ddz;
        if (dist2 < bestDist2)
        {
            bestDist2 = dist2;
            bestPid = pid;
        }
    }

    if (bestPid < 0)
        return -1;

    if (nodeId)
    {
        *nodeId = QString::number(bestPid);
        if (auto* nodeIdArr = vtkDataArray::SafeDownCast(grid->GetPointData()->GetArray("node_id")))
            *nodeId = QString::number(int(nodeIdArr->GetTuple1(bestPid)));
    }
    return bestPid;
}

int GeoStressMeasurePointPage::appendRow(const QString& nodeId, const QString& x, const QString& y,
                                         const QString& z, const QString& sgx, const QString& sgy,
                                         const QString& sgz, const QString& txy, const QString& tyz,
                                         const QString& tzx, vtkIdType pid)
{
    const int row = ui->measureTable->rowCount();
    ui->measureTable->insertRow(row);
    setCellText(row, ColNodeId, nodeId, false);
    setCellText(row, ColX, x);
    setCellText(row, ColY, y);
    setCellText(row, ColZ, z);
    setCellText(row, ColSgx, sgx);
    setCellText(row, ColSgy, sgy);
    setCellText(row, ColSgz, sgz);
    setCellText(row, ColTxy, txy);
    setCellText(row, ColTyz, tyz);
    setCellText(row, ColTzx, tzx);
    setCellText(row, ColPid, QString::number(qint64(pid)), false);
    applyDimensionUi();
    return row;
}

QString GeoStressMeasurePointPage::cellText(int row, int col) const
{
    const QTableWidgetItem* item = ui->measureTable ? ui->measureTable->item(row, col) : nullptr;
    return item ? item->text().trimmed() : QString();
}

void GeoStressMeasurePointPage::setCellText(int row, int col, const QString& text, bool editable)
{
    auto* item = new QTableWidgetItem(text);
    Qt::ItemFlags flags = item->flags();
    if (!editable)
        flags &= ~Qt::ItemIsEditable;
    item->setFlags(flags);
    ui->measureTable->setItem(row, col, item);
}

void GeoStressMeasurePointPage::syncPickedPointIds()
{
    QVector<vtkIdType> ids;
    for (int row = 0; row < ui->measureTable->rowCount(); ++row)
    {
        bool ok = false;
        qlonglong pid = cellText(row, ColPid).toLongLong(&ok);
        if (!ok || pid < 0)
        {
            QString nodeId;
            pid = resolvePointIdByCoordinates(cellText(row, ColX), cellText(row, ColY),
                                              cellText(row, ColZ), &nodeId);
            if (pid >= 0)
            {
                setCellText(row, ColPid, QString::number(pid), false);
                setCellText(row, ColNodeId, nodeId, false);
            }
        }
        if (pid >= 0)
            ids.push_back(static_cast<vtkIdType>(pid));
    }
    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, ids, true);
}
