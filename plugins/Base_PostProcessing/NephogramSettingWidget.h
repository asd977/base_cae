#ifndef NEPHOGRAMSETTINGWIDGET_H
#define NEPHOGRAMSETTINGWIDGET_H

#include <BasePage.h>
#include <vtkActor.h>
#include <vtkActor2D.h>
#include <vtkDataSetMapper.h>
#include <vtkCallbackCommand.h>
#include <vtkLookupTable.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>

#include <QString>
#include <QWidget>

class VisualWidgetService;
class vtkDataArray;
class vtkObject;
class vtkScalarBarActor;
namespace Ui
{
class NephogramSettingWidget;
}

class NephogramSettingWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit NephogramSettingWidget(VisualWidgetService* postProcessingWidget,
                                    QWidget* parent = nullptr);
    ~NephogramSettingWidget();

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    void selectGridNode(vtkObject* caller, unsigned long eventId, void* callData);

  private slots:
    void on_analysisComboBox_currentIndexChanged(int index);
    void on_stepComboBox_currentIndexChanged(int index);
    void on_scalarComboBox_currentIndexChanged(int index);
    void on_resultDirComboBox_currentIndexChanged(int index);
    void on_resultTimeComboBox_currentIndexChanged(int index);

  private:
    void ensureActorAttached();
    void syncScalarBarFromCurrent();
    void updateNephogram(const QString& selectedScalarName);
    void loadFileName(const QString& dataPath);
    void initResultDirectoryOptions();
    QString selectedResultDirectoryPath() const;
    void readTecplot(const QString& fileName);
    void readVtu(const QString& fileName);

    QString currentScalarName() const;
    QString currentScalarDisplayName() const;
    void refreshCurrentVisualization();
    void updateRangeEditorState();
    void setRangeControls(double minV, double maxV);
    bool displayRange(double dataMin, double dataMax, double& minV, double& maxV) const;
    void applyLegendAppearance(vtkScalarBarActor* scalarBar, double minV, double maxV) const;
    void installLegendDragInteraction();
    void removeLegendDragInteraction();
    bool handleLegendDragEvent(unsigned long eventId);
    static void legendDragCallback(vtkObject* caller, unsigned long eventId,
                                   void* clientData, void* callData);

    vtkDataArray* findScalarArray(vtkUnstructuredGrid* grid, const QString& scalarName,
                                  bool& usePointData) const;
    void updateLookupTableRange(vtkLookupTable* lut, double minV, double maxV) const;
    void configureMapper(vtkDataSetMapper* mapper, const QString& scalarName, bool usePointData,
                         double minV, double maxV, vtkLookupTable* lut) const;

    void updateContourOverlay(const QString& scalarName, bool usePointData, double minV,
                              double maxV);
    void clearContourOverlay();

  private:
    Ui::NephogramSettingWidget* ui{nullptr};
    VisualWidgetService* m_visualService{nullptr};

    vtkSmartPointer<vtkActor> m_actor;
    vtkSmartPointer<vtkDataSetMapper> m_mapper;
    vtkSmartPointer<vtkUnstructuredGrid> m_grid;

    QString m_dataPath;
    QString m_loadedFileName;
    QString m_currentArrayName;
    bool m_initialized{false};
    bool m_actorAttached{false};
    bool m_loadingState{false};
    bool m_updatingRangeControls{false};
    bool m_hasLegendPosition{false};
    double m_legendPositionX{0.0};
    double m_legendPositionY{0.0};

    vtkSmartPointer<vtkCallbackCommand> m_legendDragCallback;
    unsigned long m_middlePressObserverId{0};
    unsigned long m_mouseMoveObserverId{0};
    unsigned long m_middleReleaseObserverId{0};
    bool m_draggingLegend{false};
    double m_legendDragOffsetX{0.0};
    double m_legendDragOffsetY{0.0};

    vtkSmartPointer<vtkActor> m_contourActor;
    vtkSmartPointer<vtkActor2D> m_contourLabelActor;
};

#endif
