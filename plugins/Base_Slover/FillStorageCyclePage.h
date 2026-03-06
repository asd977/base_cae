#pragma once

#include <BasePage.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class QSpinBox;
class QTableWidget;

namespace Ui
{
class FillStorageCyclePage;
}

class FillStorageCyclePage : public BasePage
{
    Q_OBJECT

  public:
    explicit FillStorageCyclePage(QWidget* parent = nullptr);
    ~FillStorageCyclePage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    QString sectionText() const;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  private:
    void setupConnections();
    void ensureTableRows(QTableWidget* table, int rows) const;
    void syncSpinWithTableRows(QSpinBox* spin, QTableWidget* table) const;
    void addTableRow(QTableWidget* table, QSpinBox* spin);
    void removeTableRow(QTableWidget* table, QSpinBox* spin);
    void openTableEditor(const QString& title, QTableWidget* table, QSpinBox* spin);
    QJsonArray tableToJson(QTableWidget* table) const;
    void jsonToTable(const QJsonArray& arr, QTableWidget* table) const;
    QString tableItemText(QTableWidget* table, int row, int col,
                          const QString& defaultValue = QString()) const;
    QString buildSectionText() const;
    void updatePreview();

  private:
    Ui::FillStorageCyclePage* ui = nullptr;
};
