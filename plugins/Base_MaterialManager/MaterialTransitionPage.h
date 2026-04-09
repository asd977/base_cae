#pragma once

#include <BasePage.h>

#include <QMap>
#include <QPair>
#include <QVector>

class MaterialManager;
class QTableWidget;

class MaterialTransitionPage : public BasePage
{
    Q_OBJECT
  public:
    explicit MaterialTransitionPage(MaterialManager* manager, QWidget* parent = nullptr);

    void beforeLoad() override;
    void beforeUnload() override;

    void reloadFromManager();

  private slots:
    void onAddRow();
    void onDeleteRow();
    void onSave();

  private:
    void addRow(int oldMatype, int newMatype);
    void refreshRowNumbers(int row);

  private:
    MaterialManager* m_manager = nullptr;
    QTableWidget* m_table = nullptr;
    QMap<int, QString> m_oldOptions;
    QMap<int, QString> m_newOptions;
};

