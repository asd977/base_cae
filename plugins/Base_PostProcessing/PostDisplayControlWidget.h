#pragma once

#include <QWidget>
#include <QString>
#include <vtkSmartPointer.h>

class VisualWidgetService;
class vtkCallbackCommand;
class vtkObject;
class QCheckBox;
class QGroupBox;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QFormLayout;

namespace Ui
{
class PostDisplayControlWidget;
}

class PostDisplayControlWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit PostDisplayControlWidget(VisualWidgetService* visualService,
                                      QWidget* parent = nullptr);
    ~PostDisplayControlWidget() override;

    void syncFromState();

  private:
    void syncRangeControls();
    void applyScalarBarAppearance(bool renderNow = true);
    void notifyRefresh(const QString& reason);
    void installScalarBarDragInteraction();
    void removeScalarBarDragInteraction();
    bool handleScalarBarDragEvent(unsigned long eventId);
    static void scalarBarDragCallback(vtkObject* caller, unsigned long eventId,
                                      void* clientData, void* callData);

  private:
    Ui::PostDisplayControlWidget* ui{nullptr};
    VisualWidgetService* m_visualService{nullptr};
    bool m_syncing{false};
    vtkSmartPointer<vtkCallbackCommand> m_scalarBarDragCallback;
    unsigned long m_middlePressObserverId{0};
    unsigned long m_mouseMoveObserverId{0};
    unsigned long m_middleReleaseObserverId{0};
    bool m_draggingScalarBar{false};
    double m_dragOffsetX{0.0};
    double m_dragOffsetY{0.0};

    QGroupBox* m_thresholdGroup{nullptr};
    QCheckBox* m_thresholdEnabled{nullptr};
    QDoubleSpinBox* m_thresholdValue{nullptr};
    QSpinBox* m_belowTransparency{nullptr};
    QWidget* m_meshEdgeSettings{nullptr};
    QFormLayout* m_meshEdgeForm{nullptr};
    QDoubleSpinBox* m_meshEdgeWidth{nullptr};
    QSpinBox* m_meshEdgeOpacity{nullptr};
    QCheckBox* m_showOutlineCheckBox{nullptr};
    QCheckBox* m_showContextOutlineCheckBox{nullptr};
    QCheckBox* m_showContextLinesCheckBox{nullptr};
    QComboBox* m_surfaceInterpolationComboBox{nullptr};
    QCheckBox* m_scalarInterpolationCheckBox{nullptr};
    QCheckBox* m_computeNormalsCheckBox{nullptr};
    QCheckBox* m_splitSharpEdgesCheckBox{nullptr};
    QDoubleSpinBox* m_normalsFeatureAngleSpinBox{nullptr};
    QComboBox* m_tickModeComboBox{nullptr};
    QDoubleSpinBox* m_tickStepSpinBox{nullptr};
    QCheckBox* m_reverseColorsCheckBox{nullptr};
    QComboBox* m_contourModeComboBox{nullptr};
};
