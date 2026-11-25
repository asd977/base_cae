#include "SeismicAndAddedMassPage.h"
#include <CompactNumberDelegate.h>

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QJsonArray>
#include <QMessageBox>
#include <QStringList>
#include <QTextStream>

#include <cmath>
#include <limits>

#include "PickManager.h"
#include "ui_SeismicAndAddedMassPage.h"

SeismicAndAddedMassPage::SeismicAndAddedMassPage(QWidget* parent)
    : BasePage("SeismicAndAddedMassPage"), ui(new Ui::SeismicAndAddedMassPage)
{
    ui->setupUi(this);
    ui->tableSeismic->setItemDelegate(new CompactNumberDelegate(ui->tableSeismic));

    // 表格初始化：4列 T AX AY AZ
    ui->tableSeismic->setColumnCount(4);
    ui->tableSeismic->setHorizontalHeaderLabels({"T", "AX", "AY", "AZ"});
    ui->tableSeismic->setRowCount(0);
    ui->tableSeismic->setAlternatingRowColors(true);
    ui->tableSeismic->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableSeismic->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->tableSeismic->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableSeismic->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    ui->doubleSpinAmpX->setValue(0.0);
    ui->doubleSpinAmpY->setValue(0.0);
    ui->doubleSpinAmpZ->setValue(0.0);

    ensureAtLeastOneRow();
}

SeismicAndAddedMassPage::~SeismicAndAddedMassPage()
{
    delete ui;
}

void SeismicAndAddedMassPage::beforeLoad()
{
    PickManager::instance();

    // 这个页面后面还要做“附加质量面”，用 Face 拾取更合适（先占位）
    qInfo() << "[SeismicAndAddedMassPage] beforeLoad activate Face pick uuid=" << m_uuid;
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Face);

    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const bool is2D = (NDIM == 2);

    qInfo() << "[SeismicAndAddedMassPage] NDIM=" << NDIM
             << "rows=" << ui->tableSeismic->rowCount();
}

void SeismicAndAddedMassPage::beforeUnload()
{
    qInfo() << "[SeismicAndAddedMassPage] beforeUnload deactivate Face pick uuid=" << m_uuid;
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Face);
}

QJsonObject SeismicAndAddedMassPage::saveJson()
{
    QJsonObject obj;
    obj["amp_x"] = ui->doubleSpinAmpX->value();
    obj["amp_y"] = ui->doubleSpinAmpY->value();
    obj["amp_z"] = ui->doubleSpinAmpZ->value();

    QJsonArray rows;
    const int rowCount = ui->tableSeismic->rowCount();
    for (int r = 0; r < rowCount; ++r)
    {
        QJsonObject row;
        for (int c = 0; c < 4; ++c)
        {
            const auto* item = ui->tableSeismic->item(r, c);
            row[QString("c%1").arg(c)] = item ? item->text().trimmed() : QString();
        }
        rows.append(row);
    }
    obj["rows"] = rows;
    return obj;
}

void SeismicAndAddedMassPage::loadJson(const QJsonObject& obj)
{
    ui->doubleSpinAmpX->setValue(obj.value("amp_x").toDouble(ui->doubleSpinAmpX->value()));
    ui->doubleSpinAmpY->setValue(obj.value("amp_y").toDouble(ui->doubleSpinAmpY->value()));
    ui->doubleSpinAmpZ->setValue(obj.value("amp_z").toDouble(ui->doubleSpinAmpZ->value()));

    const QJsonArray rows = obj.value("rows").toArray();
    ui->tableSeismic->setRowCount(rows.size());

    for (int r = 0; r < rows.size(); ++r)
    {
        const QJsonObject row = rows[r].toObject();
        for (int c = 0; c < 4; ++c)
        {
            const QString text = row.value(QString("c%1").arg(c)).toString();
            if (text.isEmpty())
                continue;
            ui->tableSeismic->setItem(r, c, new QTableWidgetItem(text));
        }
    }
    ensureAtLeastOneRow();
}

double SeismicAndAddedMassPage::ampX() const
{
    return ui->doubleSpinAmpX->value();
}
double SeismicAndAddedMassPage::ampY() const
{
    return ui->doubleSpinAmpY->value();
}
double SeismicAndAddedMassPage::ampZ() const
{
    return ui->doubleSpinAmpZ->value();
}

void SeismicAndAddedMassPage::ensureAtLeastOneRow()
{
    if (ui->tableSeismic->rowCount() <= 0)
        ui->tableSeismic->insertRow(0);
}

bool SeismicAndAddedMassPage::cellToDouble(int row, int col, double& out) const
{
    out = 0.0;
    auto* item = ui->tableSeismic->item(row, col);
    if (!item)
        return false;

    const QString s = item->text().trimmed();
    if (s.isEmpty())
        return false;

    bool ok = false;
    out = s.toDouble(&ok);
    return ok;
}

QVector<QPair<double, double>> SeismicAndAddedMassPage::seriesX() const
{
    QVector<QPair<double, double>> out;
    const int rN = ui->tableSeismic->rowCount();
    out.reserve(rN);

    for (int r = 0; r < rN; ++r)
    {
        double t = 0.0, ax = 0.0;
        bool okT = cellToDouble(r, 0, t);
        if (!okT)
            continue;

        // AX 允许空 -> 0
        bool okA = cellToDouble(r, 1, ax);
        if (!okA)
            ax = 0.0;

        out.push_back({t, ax});
    }
    qInfo() << "[SeismicSeries] X parsed N=" << out.size();
    return out;
}

QVector<QPair<double, double>> SeismicAndAddedMassPage::seriesY() const
{
    QVector<QPair<double, double>> out;
    const int rN = ui->tableSeismic->rowCount();
    out.reserve(rN);

    for (int r = 0; r < rN; ++r)
    {
        double t = 0.0, ay = 0.0;
        bool okT = cellToDouble(r, 0, t);
        if (!okT)
            continue;

        bool okA = cellToDouble(r, 2, ay);
        if (!okA)
            ay = 0.0;

        out.push_back({t, ay});
    }
    qInfo() << "[SeismicSeries] Y parsed N=" << out.size();
    return out;
}

QVector<QPair<double, double>> SeismicAndAddedMassPage::seriesZ() const
{
    QVector<QPair<double, double>> out;
    const int rN = ui->tableSeismic->rowCount();
    out.reserve(rN);

    for (int r = 0; r < rN; ++r)
    {
        double t = 0.0, az = 0.0;
        bool okT = cellToDouble(r, 0, t);
        if (!okT)
            continue;

        bool okA = cellToDouble(r, 3, az);
        if (!okA)
            az = 0.0;

        out.push_back({t, az});
    }
    qInfo() << "[SeismicSeries] Z parsed N=" << out.size();
    return out;
}

bool SeismicAndAddedMassPage::validateWaveInput(QString* errorMessage) const
{
    if (errorMessage)
        errorMessage->clear();

    const int rowCount = ui->tableSeismic->rowCount();
    int validRows = 0;
    bool hasAccelerationValue = false;
    double previousTime = -std::numeric_limits<double>::infinity();

    for (int row = 0; row < rowCount; ++row)
    {
        QStringList cells;
        cells.reserve(4);
        bool rowIsEmpty = true;
        for (int col = 0; col < 4; ++col)
        {
            const auto* item = ui->tableSeismic->item(row, col);
            const QString text = item ? item->text().trimmed() : QString();
            cells.push_back(text);
            rowIsEmpty = rowIsEmpty && text.isEmpty();
        }

        // 允许表格末尾保留空白编辑行；完全空的行不作为地震波数据。
        if (rowIsEmpty)
            continue;

        bool timeOk = false;
        const double time = cells.at(0).toDouble(&timeOk);
        if (!timeOk || !std::isfinite(time))
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("地震加速度表第 %1 行的时间 T 无效。")
                                    .arg(row + 1);
            return false;
        }
        if (time < previousTime)
        {
            if (errorMessage)
                *errorMessage = QStringLiteral("地震加速度表第 %1 行的时间小于上一条数据，请按时间顺序填写。")
                                    .arg(row + 1);
            return false;
        }

        for (int col = 1; col < 4; ++col)
        {
            if (cells.at(col).isEmpty())
                continue; // 与现有导出逻辑一致：空的 AX/AY/AZ 按 0 处理。

            bool accelerationOk = false;
            const double acceleration = cells.at(col).toDouble(&accelerationOk);
            if (!accelerationOk || !std::isfinite(acceleration))
            {
                const QString axis = col == 1 ? QStringLiteral("AX")
                                     : col == 2 ? QStringLiteral("AY")
                                                : QStringLiteral("AZ");
                if (errorMessage)
                    *errorMessage = QStringLiteral("地震加速度表第 %1 行的 %2 无效。")
                                        .arg(row + 1)
                                        .arg(axis);
                return false;
            }
            hasAccelerationValue = true;
        }

        previousTime = time;
        ++validRows;
    }

    if (validRows <= 0)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("地震加速度表中没有有效的时程数据。请至少填写一行时间与加速度数据。");
        return false;
    }
    if (!hasAccelerationValue)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("地震加速度表只有时间数据，请至少填写一个 AX、AY 或 AZ 加速度值。");
        return false;
    }

    return true;
}

// ===== Buttons =====
void SeismicAndAddedMassPage::on_btnAddRow_clicked()
{
    int r = ui->tableSeismic->rowCount();
    ui->tableSeismic->insertRow(r);
    qInfo() << "[SeismicTable] add row ->" << r;
}

void SeismicAndAddedMassPage::on_btnDeleteRow_clicked()
{
    auto sel = ui->tableSeismic->selectionModel()->selectedRows();
    if (sel.isEmpty())
        return;

    // 倒序删除避免行号变化
    std::sort(sel.begin(), sel.end(),
              [](const QModelIndex& a, const QModelIndex& b)
              {
                  return a.row() > b.row();
              });

    for (const QModelIndex& idx : sel)
    {
        qInfo() << "[SeismicTable] delete row" << idx.row();
        ui->tableSeismic->removeRow(idx.row());
    }
    ensureAtLeastOneRow();
}

void SeismicAndAddedMassPage::on_btnClear_clicked()
{
    qInfo() << "[SeismicTable] clear all";
    ui->tableSeismic->setRowCount(0);
    ensureAtLeastOneRow();
}

void SeismicAndAddedMassPage::on_btnImportCsv_clicked()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("导入地震波表格"), QString(),
        tr("逗号分隔数据文件 (*.csv);;所有文件 (*)"));
    if (filePath.isEmpty())
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("导入失败"), tr("无法打开文件：%1").arg(filePath));
        return;
    }

    QVector<QVector<double>> rows;
    QTextStream in(&file);
    int lineNo = 0;

    while (!in.atEnd())
    {
        const QString line = in.readLine().trimmed();
        ++lineNo;
        if (line.isEmpty())
            continue;

        const QStringList cols = line.split(',', Qt::KeepEmptyParts);
        if (cols.size() < 4)
            continue;

        double t = 0.0, x = 0.0, y = 0.0, z = 0.0;
        auto parseValue = [](QString s, double& out)
        {
            s = s.trimmed();
            if (s.startsWith('"') && s.endsWith('"') && s.size() >= 2)
                s = s.mid(1, s.size() - 2);
            bool ok = false;
            out = s.toDouble(&ok);
            return ok;
        };

        const bool okT = parseValue(cols[0], t);
        const bool okX = parseValue(cols[1], x);
        const bool okY = parseValue(cols[2], y);
        const bool okZ = parseValue(cols[3], z);
        if (!okT || !okX || !okY || !okZ)
        {
            if (lineNo == 1)
                continue; // 兼容标题行: 时间,x,y,z
            continue;
        }

        rows.push_back({t, x, y, z});
    }

    if (rows.isEmpty())
    {
        QMessageBox::information(this, tr("导入结果"),
                                 tr("未读取到有效数据，请确认 CSV 列顺序为：Time, X, Y, Z。"));
        return;
    }

    ui->tableSeismic->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r)
    {
        for (int c = 0; c < 4; ++c)
        {
            auto* item = new QTableWidgetItem(QString::number(rows[r][c], 'f', 6));
            ui->tableSeismic->setItem(r, c, item);
        }
    }

    qInfo() << "[SeismicTable] import csv rows=" << rows.size() << "file=" << filePath;
}
