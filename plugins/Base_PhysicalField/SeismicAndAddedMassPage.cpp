#include "SeismicAndAddedMassPage.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QHeaderView>

#include "PickManager.h"
#include "ui_SeismicAndAddedMassPage.h"

SeismicAndAddedMassPage::SeismicAndAddedMassPage(QWidget* parent)
    : BasePage("SeismicAndAddedMassPage"), ui(new Ui::SeismicAndAddedMassPage) {
  ui->setupUi(this);

  // 表格初始化：4列 T AX AY AZ
  ui->tableSeismic->setColumnCount(4);
  ui->tableSeismic->setHorizontalHeaderLabels({"T", "AX", "AY", "AZ"});
  ui->tableSeismic->setRowCount(0);
  ui->tableSeismic->setAlternatingRowColors(true);
  ui->tableSeismic->setSelectionBehavior(QAbstractItemView::SelectRows);
  ui->tableSeismic->setSelectionMode(QAbstractItemView::ExtendedSelection);
  ui->tableSeismic->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Stretch);

  ui->doubleSpinAmpX->setValue(0.0);
  ui->doubleSpinAmpY->setValue(0.0);
  ui->doubleSpinAmpZ->setValue(0.0);

  ensureAtLeastOneRow();
}

SeismicAndAddedMassPage::~SeismicAndAddedMassPage() { delete ui; }

void SeismicAndAddedMassPage::beforeLoad() {
  PickManager::instance();

  // 这个页面后面还要做“附加质量面”，用 Face 拾取更合适（先占位）
  qDebug() << "[SeismicAndAddedMassPage] beforeLoad activate Face pick uuid="
           << m_uuid;
  PickManager::instance().activate(getPageType(), m_uuid, PickMode::Face);

  const int NDIM =
      PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
  const bool is2D = (NDIM == 2);

  qDebug() << "[SeismicAndAddedMassPage] NDIM=" << NDIM
           << "rows=" << ui->tableSeismic->rowCount();
}

void SeismicAndAddedMassPage::beforeUnload() {
  qDebug()
      << "[SeismicAndAddedMassPage] beforeUnload deactivate Face pick uuid="
      << m_uuid;
  PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Face);
}

double SeismicAndAddedMassPage::ampX() const {
  return ui->doubleSpinAmpX->value();
}
double SeismicAndAddedMassPage::ampY() const {
  return ui->doubleSpinAmpY->value();
}
double SeismicAndAddedMassPage::ampZ() const {
  return ui->doubleSpinAmpZ->value();
}

void SeismicAndAddedMassPage::ensureAtLeastOneRow() {
  if (ui->tableSeismic->rowCount() <= 0) ui->tableSeismic->insertRow(0);
}

bool SeismicAndAddedMassPage::cellToDouble(int row, int col,
                                           double& out) const {
  out = 0.0;
  auto* item = ui->tableSeismic->item(row, col);
  if (!item) return false;

  const QString s = item->text().trimmed();
  if (s.isEmpty()) return false;

  bool ok = false;
  out = s.toDouble(&ok);
  return ok;
}

QVector<QPair<double, double>> SeismicAndAddedMassPage::seriesX() const {
  QVector<QPair<double, double>> out;
  const int rN = ui->tableSeismic->rowCount();
  out.reserve(rN);

  for (int r = 0; r < rN; ++r) {
    double t = 0.0, ax = 0.0;
    bool okT = cellToDouble(r, 0, t);
    if (!okT) continue;

    // AX 允许空 -> 0
    bool okA = cellToDouble(r, 1, ax);
    if (!okA) ax = 0.0;

    out.push_back({t, ax});
  }
  qDebug() << "[SeismicSeries] X parsed N=" << out.size();
  return out;
}

QVector<QPair<double, double>> SeismicAndAddedMassPage::seriesY() const {
  QVector<QPair<double, double>> out;
  const int rN = ui->tableSeismic->rowCount();
  out.reserve(rN);

  for (int r = 0; r < rN; ++r) {
    double t = 0.0, ay = 0.0;
    bool okT = cellToDouble(r, 0, t);
    if (!okT) continue;

    bool okA = cellToDouble(r, 2, ay);
    if (!okA) ay = 0.0;

    out.push_back({t, ay});
  }
  qDebug() << "[SeismicSeries] Y parsed N=" << out.size();
  return out;
}

QVector<QPair<double, double>> SeismicAndAddedMassPage::seriesZ() const {
  QVector<QPair<double, double>> out;
  const int rN = ui->tableSeismic->rowCount();
  out.reserve(rN);

  for (int r = 0; r < rN; ++r) {
    double t = 0.0, az = 0.0;
    bool okT = cellToDouble(r, 0, t);
    if (!okT) continue;

    bool okA = cellToDouble(r, 3, az);
    if (!okA) az = 0.0;

    out.push_back({t, az});
  }
  qDebug() << "[SeismicSeries] Z parsed N=" << out.size();
  return out;
}

// ===== Buttons =====
void SeismicAndAddedMassPage::on_btnAddRow_clicked() {
  int r = ui->tableSeismic->rowCount();
  ui->tableSeismic->insertRow(r);
  qDebug() << "[SeismicTable] add row ->" << r;
}

void SeismicAndAddedMassPage::on_btnDeleteRow_clicked() {
  auto sel = ui->tableSeismic->selectionModel()->selectedRows();
  if (sel.isEmpty()) return;

  // 倒序删除避免行号变化
  std::sort(sel.begin(), sel.end(),
            [](const QModelIndex& a, const QModelIndex& b) {
              return a.row() > b.row();
            });

  for (const QModelIndex& idx : sel) {
    qDebug() << "[SeismicTable] delete row" << idx.row();
    ui->tableSeismic->removeRow(idx.row());
  }
  ensureAtLeastOneRow();
}

void SeismicAndAddedMassPage::on_btnClear_clicked() {
  qDebug() << "[SeismicTable] clear all";
  ui->tableSeismic->setRowCount(0);
  ensureAtLeastOneRow();
}
