#pragma once

#include <BasePage.h>

#include <QJsonObject>
#include <QString>
#include <QMap>

class QLineEdit;
class QWidget;
class QPlainTextEdit;
class QTableWidget;
class QJsonArray;

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
    QString sectionText(int caseIndex) const;
    bool usesDamBaseUplift() const;
    bool usesLinearDamBaseUplift() const;
    bool usesDamFacePressure() const;
    void setDynamicOptionsAvailable(bool available);
    bool validateInputs(QString* errorMessage) const;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void requirementsChanged();

  private:
    void initValueEditors();
    void loadWetMappingsFromService();
    QString valueOf(const QString& key) const;
    void setValueOf(const QString& key, const QString& value);
    QString tableItemText(QTableWidget* table, int row, int col, const QString& fallback) const;
    QJsonArray tableToJson(QTableWidget* table) const;
    void jsonToTable(const QJsonArray& arr, QTableWidget* table, int minRows);
    void updatePreview();
    void updateConditionalVisibility();
  private:
    Ui::FillStorageCyclePage* ui = nullptr;
    QMap<QString, QWidget*> m_editors;
    QTableWidget* m_wetTable = nullptr;
    QTableWidget* m_dropTable = nullptr;
    QTableWidget* m_circleTable = nullptr;
    bool m_dynamicOptionsAvailable = true;
};
