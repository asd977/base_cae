#include "GlobalParameterSettingWidget.h"

#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <algorithm>

#include "PublicVariables.h"
#include "ui_GlobalParameterSettingWidget.h"

static constexpr int COL_KEY = 0;
static constexpr int COL_VALUE = 1;

GlobalParameterSettingWidget::GlobalParameterSettingWidget(
    const QString& pageUuid, QWidget* parent)
    : ui(new Ui::GlobalParameterSettingWidget),
      m_pageUuid(pageUuid),
      BasePage("GlobalParameterSettingWidget") {
  ui->setupUi(this);
  setupUiBehavior();
  reloadFromPublicVariables();
}

GlobalParameterSettingWidget::~GlobalParameterSettingWidget() { delete ui; }

void GlobalParameterSettingWidget::setupUiBehavior() {
  auto table = ui->kvTable;
  Q_ASSERT(table);

  table->setColumnCount(2);
  table->setHorizontalHeaderLabels({tr("Key"), tr("Value")});
  table->horizontalHeader()->setStretchLastSection(true);
  table->verticalHeader()->setVisible(false);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table->setEditTriggers(QAbstractItemView::DoubleClicked |
                         QAbstractItemView::SelectedClicked |
                         QAbstractItemView::EditKeyPressed);

  connect(table, &QTableWidget::itemChanged, this,
          &GlobalParameterSettingWidget::onItemChanged);
  if (auto btn = ui->addRowBtn)
    connect(btn, &QPushButton::clicked, this,
            &GlobalParameterSettingWidget::onAddRow);
  if (auto btn = ui->removeRowBtn)
    connect(btn, &QPushButton::clicked, this,
            &GlobalParameterSettingWidget::onRemoveSelectedRows);
}

void GlobalParameterSettingWidget::setPageUuid(const QString& uuid) {
  if (m_pageUuid == uuid) return;
  m_pageUuid = uuid;
  reloadFromPublicVariables();
}

void GlobalParameterSettingWidget::reloadFromPublicVariables() {
  auto table = ui->kvTable;
  m_blockItemChanged = true;
  table->clearContents();
  table->setRowCount(0);

  const auto kv = PublicVariables::instance().snapshotForOwner(m_pageUuid);
  for (auto it = kv.constBegin(); it != kv.constEnd(); ++it)
    addRow(it.key(), it.value());

  m_blockItemChanged = false;
}

void GlobalParameterSettingWidget::addRow(const QString& key,
                                          const QVariant& value) {
  auto table = ui->kvTable;

  m_blockItemChanged = true;

  const int r = table->rowCount();
  table->insertRow(r);

  auto* keyItem = new QTableWidgetItem(key);
  keyItem->setData(Qt::UserRole, key);  // 记录“旧键名”（可能为空）

  auto* valItem = new QTableWidgetItem(value.toString());

  table->setItem(r, COL_KEY, keyItem);
  table->setItem(r, COL_VALUE, valItem);

  m_blockItemChanged = false;
}

void GlobalParameterSettingWidget::onAddRow() { addRow(); }

void GlobalParameterSettingWidget::onRemoveSelectedRows() {
  auto table = ui->kvTable;
  auto sel = table->selectionModel()->selectedRows();
  if (sel.isEmpty()) return;

  std::sort(sel.begin(), sel.end(),
            [](const QModelIndex& a, const QModelIndex& b) {
              return a.row() > b.row();
            });

  for (const auto& idx : sel) {
    auto* keyItem = table->item(idx.row(), COL_KEY);
    const QString key = keyItem ? keyItem->text().trimmed() : QString();
    if (!key.isEmpty()) PublicVariables::instance().remove(key, m_pageUuid);

    table->removeRow(idx.row());
  }
}

QVariant GlobalParameterSettingWidget::guessTyped(const QString& text) const {
  bool okI = false, okD = false;
  const QString s = text.trimmed();
  int iv = s.toInt(&okI);
  double dv = s.toDouble(&okD);
  const QString lower = s.toLower();

  if (okI && QString::number(iv) == s) return iv;
  if (okD) return dv;
  if (lower == "true") return true;
  if (lower == "false") return false;
  return s;
}

bool GlobalParameterSettingWidget::keyExistsInTable(const QString& key,
                                                    int excludeRow) const {
  const QString k = key.trimmed();
  if (k.isEmpty()) return false;

  auto* table = ui->kvTable;
  const int rows = table->rowCount();
  for (int r = 0; r < rows; ++r) {
    if (r == excludeRow) continue;
    auto* it = table->item(r, COL_KEY);
    if (!it) continue;
    if (it->text().trimmed() == k) return true;
  }
  return false;
}
void GlobalParameterSettingWidget::onItemChanged(QTableWidgetItem* item) {
  if (m_blockItemChanged || !item) return;

  auto table = ui->kvTable;
  const int row = item->row();

  if (item->column() == COL_KEY) {
    const int row = item->row();
    const QString newKey = item->text().trimmed();
    const QString oldKey = item->data(Qt::UserRole).toString();

    if (newKey.isEmpty()) {
      QMessageBox::warning(this, tr("无效键名"), tr("键名不能为空。"));
      m_blockItemChanged = true;
      item->setText(oldKey);
      m_blockItemChanged = false;
      return;
    }

    if (keyExistsInTable(newKey, row)) {
      QMessageBox::warning(
          this, tr("重复键"),
          tr("键 [%1] 在当前页面内已存在，请使用不同的键名。").arg(newKey));
      m_blockItemChanged = true;
      item->setText(oldKey);
      m_blockItemChanged = false;
      return;
    }

    if (newKey == oldKey) return;  // 未修改

    if (oldKey.isEmpty()) {
      // 新建键：注册 + 写入初值（若有）
      QString conflict;
      if (!PublicVariables::instance().registerKey(newKey, m_pageUuid,
                                                   &conflict)) {
        QMessageBox::warning(
            this, tr("键名冲突"),
            tr("键 [%1] 已被 UUID=%2 使用。")
                .arg(newKey, conflict.isEmpty() ? tr("未知") : conflict));
        m_blockItemChanged = true;
        item->setText(oldKey);
        m_blockItemChanged = false;
        return;
      }

      auto* valItem = ui->kvTable->item(row, COL_VALUE);
      const QVariant v = guessTyped(valItem ? valItem->text() : QString());
      if (!PublicVariables::instance().set(newKey, v, m_pageUuid)) {
        QMessageBox::warning(this, tr("写入失败"),
                             tr("键 [%1] 的初始值写入失败。").arg(newKey));
      }

      item->setData(Qt::UserRole, newKey);
      return;
    } else {
      // 重命名：再次确认不会与本页其它行重复（前面已查，不必重复查）
      QString conflict;
      if (!PublicVariables::instance().renameKey(oldKey, newKey, m_pageUuid,
                                                 &conflict)) {
        QMessageBox::warning(
            this, tr("键名冲突"),
            tr("键 [%1] 已被 UUID=%2 使用，或原键不属于本页面。")
                .arg(newKey, conflict.isEmpty() ? tr("未知") : conflict));
        m_blockItemChanged = true;
        item->setText(oldKey);
        m_blockItemChanged = false;
        return;
      }
      item->setData(Qt::UserRole, newKey);
      return;
    }
  } else if (item->column() == COL_VALUE) {
    auto* keyItem = table->item(row, COL_KEY);
    if (!keyItem) return;

    const QString key = keyItem->text().trimmed();
    if (key.isEmpty()) return;

    const QVariant v = guessTyped(item->text());
    if (!PublicVariables::instance().set(key, v, m_pageUuid)) {
      QMessageBox::warning(
          this, tr("写入失败"),
          tr("键 [%1] 的值写入失败（可能该键并不归属于本页面）。").arg(key));
      m_blockItemChanged = true;
      item->setText(PublicVariables::instance().get(key).toString());
      m_blockItemChanged = false;
    }
  }
}
