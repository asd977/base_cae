#ifndef VECTORSETTINGWIDGET_H
#define VECTORSETTINGWIDGET_H

#include <BasePage.h>
#include <services/VisualWidgetService.h>
#include <vtkGlyph3D.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>

#include <QVariantMap>
#include <QWidget>

class vtkActor;
class vtkArrowSource;
class vtkDataArray;

namespace Ui
{
class VectorSettingWidget;
}

class VectorSettingWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit VectorSettingWidget(VisualWidgetService* postProcessingWidget,
                                 QWidget* parent = nullptr);
    ~VectorSettingWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& object) override;

    void setFileName(const QString& file);

  private slots:
    void on_analysisComboBox_currentIndexChanged(int index);
    void on_scaleSpinBox_valueChanged(double value);
    void on_thicknessSpinBox_valueChanged(double value);
    void on_stepComboBox_currentIndexChanged(int index);
    void on_resultDirComboBox_currentIndexChanged(int index);
    void on_resultTimeComboBox_currentIndexChanged(int index);

  private:
    bool ensureDataLoaded();
    void populateComponentCombos();
    bool buildVectorArraysFromSelections(vtkUnstructuredGrid* grid, double& magMin,
                                         double& magMax);
    void rebuildPipeline();
    void requestRender() const;
    static vtkDataArray* arrayByName(vtkUnstructuredGrid* grid, const QString& name);
    void loadFileName(QString dataPath);
    void refreshResultGroups();
    void applyPendingState();
    double computeAutoBaseScale(vtkUnstructuredGrid* grid);
    int computeAutoMaskRatio(vtkUnstructuredGrid* grid) const;
    QString vectorLegendTitle() const;
    vtkSmartPointer<vtkUnstructuredGrid> readVtu(const QString& fileName);

  private:
    Ui::VectorSettingWidget* ui{nullptr};
    VisualWidgetService* m_visualService{nullptr};

    QString m_fileName;
    vtkSmartPointer<vtkUnstructuredGrid> m_grid;
    vtkSmartPointer<vtkActor> m_vectorActor;
    vtkSmartPointer<vtkGlyph3D> m_glyph;
    vtkSmartPointer<vtkArrowSource> m_arrowSource;

    double m_scaleFactor{1.0};
    double m_thicknessFactor{1.0};
    int m_maskOnRatio{1};
    QString m_dataPath;
    bool m_loadedOnce{false};
    bool m_hasPendingState{false};
    bool m_updatingControls{false};
    QVariantMap m_pendingState;
};

#endif // VECTORSETTINGWIDGET_H
