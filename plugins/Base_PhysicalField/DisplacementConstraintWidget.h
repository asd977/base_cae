#pragma once

#include <BasePage.h>
#include <vtkObject.h>
#include <vtkSmartPointer.h>

#include <QHash>
#include <QVector>

#include "PickManager.h"

class vtkUnstructuredGrid;
class vtkCellPicker;
class vtkCallbackCommand;
class vtkIntArray;

class vtkAssembly;
class vtkOrientationMarkerWidget;

class vtkRenderer;
class vtkRenderWindowInteractor;

namespace Ui
{
class DisplacementConstraintWidget;
}

class VisualWidgetService;
class DisplacementConstraintSelectionManager;

class DisplacementConstraintWidget : public BasePage
{
    Q_OBJECT
    friend class DisplacementConstraintSelectionManager;

  public:
    explicit DisplacementConstraintWidget(QWidget* parent = nullptr);
    ~DisplacementConstraintWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    int uxFlag();
    int uyFlag();
    int uzFlag();

    // 刷新当前位移约束节点的全局方向指示器。
    // 指示器固定在视口右下角，并随主视图相机旋转；约束方向通过轴端约束板表达。
    void refreshConstraintVisualization(bool renderNow = true);

  private slots:

    void onNodePicked(const QString& domain, const QString& activeUuid, GridKind, vtkIdType,
                      int nodeId, double x, double y, double z, const QString& prevUuid);
    void onGroupChanged(int index);
    void onDimChanged(int index);
    void onSetAllBarsSuspended();
    void onSelectByCoordinateRange();
    void onUseMeshBounds();

  private:
    void refreshGroupCombo(int dimFilter);
    void updateCoordinateControlState();
    void clearConstraintVisualization(bool renderNow = true);
    QString constraintVisualizationPropId() const;

  private:
    Ui::DisplacementConstraintWidget* ui = nullptr;
    bool m_coordinateRangeLoaded = false;
    bool m_constraintPageActive = false;
    VisualWidgetService* m_visual = nullptr;
    vtkSmartPointer<vtkAssembly> m_constraintMarkerAssembly;
    vtkSmartPointer<vtkOrientationMarkerWidget> m_constraintMarkerWidget;
};
