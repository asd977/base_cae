#pragma once

#include <BasePage.h>
#include <vtkObject.h>
#include <vtkSmartPointer.h>

#include <QHash>
#include <QVector>

#include "PickManager.h"

class vtkUnstructuredGrid;
class vtkActor;
class vtkPoints;
class vtkVertexGlyphFilter;
class vtkPolyDataMapper;
class vtkPolyData;
class vtkCellPicker;
class vtkCallbackCommand;
class vtkIntArray;
class QDoubleSpinBox;
class QComboBox;

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
    int rotationFlag() const;
    bool hasPrescribedDisplacement() const;
    bool isPrescribed(int axis) const;
    double prescribedValue(int axis) const;

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
    void refreshOverlapFeedback(bool renderNow = true);
    void clearOverlapVisualization(bool renderNow = true);
    QString overlapVisualizationPropId() const;
    QString formatConstraintFlags(const QVector<int>& flags, bool conflict = false) const;
    void registerConstraintFlags();

  private:
    Ui::DisplacementConstraintWidget* ui = nullptr;
    QDoubleSpinBox* m_prescribed[3] = {};
    QComboBox* m_rotation=nullptr;
    QDoubleSpinBox* m_rotationValue=nullptr;
    bool m_coordinateRangeLoaded = false;
    bool m_constraintPageActive = false;
    VisualWidgetService* m_visual = nullptr;
    vtkSmartPointer<vtkAssembly> m_constraintMarkerAssembly;
    vtkSmartPointer<vtkOrientationMarkerWidget> m_constraintMarkerWidget;

    vtkSmartPointer<vtkPoints> m_overlapPoints;
    vtkSmartPointer<vtkPolyData> m_overlapPolyData;
    vtkSmartPointer<vtkVertexGlyphFilter> m_overlapGlyph;
    vtkSmartPointer<vtkPolyDataMapper> m_overlapMapper;
    vtkSmartPointer<vtkActor> m_overlapActor;
};
