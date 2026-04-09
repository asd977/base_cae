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
    void onCurrentRowChanged(int currentRow, int currentColumn, int previousRow, int previousColumn);

  private:
    void addRow(int oldMatype, int newMatype);
    void refreshRowHighlight(int row);
    void syncMappingsToManager();
    void refreshCurrentRowMarker(int currentRow);

  private:
    MaterialManager* m_manager = nullptr;
    QTableWidget* m_table = nullptr;
    QMap<int, QString> m_oldOptions;
    QMap<int, QString> m_newOptions;
    bool m_loading = false;
};
