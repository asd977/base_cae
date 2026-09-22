#pragma once

#include <QWidget>

#include "../workbench/GeometryInspectionTools.h"

class QComboBox;
class GeometryWorkbench;

namespace Ui
{
class GeometryCheckDialog;
class GeometryMeasureDialog;
}

class GeometryCheckDialog : public QWidget
{
    Q_OBJECT
  public:
    explicit GeometryCheckDialog(GeometryWorkbench* workbench, QWidget* parent = nullptr);
    ~GeometryCheckDialog() override;

  private slots:
    void refreshObjects();
    void runCheck();
    void repairCurrent();
    void focusCurrent();

  private:
    QString currentUuid() const;
    void fillReportTable();

    Ui::GeometryCheckDialog* ui = nullptr;
    GeometryWorkbench* m_workbench = nullptr;
    GeometryInspectionTools::CheckReport m_report;
};

class GeometryMeasureDialog : public QWidget
{
    Q_OBJECT
  public:
    explicit GeometryMeasureDialog(GeometryWorkbench* workbench, QWidget* parent = nullptr);
    ~GeometryMeasureDialog() override;

  private slots:
    void refreshObjects();
    void refreshSingleIds();
    void refreshSingleInfo();
    void refreshPairIds();
    void calculatePair();
    void focusSingleObject();
    void clearHighlights();

  private:
    QString comboUuid(QComboBox* combo) const;
    int comboDim(QComboBox* combo) const;
    void fillIdCombo(QComboBox* objectCombo, QComboBox* typeCombo, QComboBox* idCombo);

    Ui::GeometryMeasureDialog* ui = nullptr;
    GeometryWorkbench* m_workbench = nullptr;
};
