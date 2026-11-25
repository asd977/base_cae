#ifndef NEPHOGRAMSETTINGWIDGET_H
#define NEPHOGRAMSETTINGWIDGET_H

#include <BasePage.h>
#include <vtkActor.h>
#include <vtkActor2D.h>
#include <vtkDataSetMapper.h>
#include <vtkLookupTable.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>
#include <vtkType.h>
#include <vtkWeakPointer.h>

#include <QString>
#include <QWidget>

class VisualWidgetService;
class vtkDataArray;
class vtkDataSet;
class PostSurfacePipeline;
struct PostPreparedSurface;
class QLabel;
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

  private slots:
    void on_analysisComboBox_currentIndexChanged(int index);
    void on_stepComboBox_currentIndexChanged(int index);
    void on_scalarComboBox_currentIndexChanged(int index);
    void on_resultDirComboBox_currentIndexChanged(int index);
    void on_resultTimeComboBox_currentIndexChanged(int index);

  private:
    void ensureActorAttached();
    bool applyThresholdDisplay(vtkUnstructuredGrid* grid, bool usePointData, bool prepareOnly=false);
    void clearThresholdDisplay();
    vtkDataSet* surfaceDisplayData(vtkUnstructuredGrid* grid);
    void finishReadTecplot(const PostPreparedSurface&);
    void updatePreparationStatus();
    void updateSurfaceOutline(vtkUnstructuredGrid* displayGrid);
    void detachSurfaceOutline();
    void clearSurfaceOutline();
    bool syncScalarBarFromResolved(vtkUnstructuredGrid* displayGrid, vtkDataArray* array,
                                   bool usePointData, double dataMin, double dataMax);
    void updateNephogram(const QString& selectedScalarName, bool renderNow = true);
    void loadFileName(const QString& dataPath);
    void populateResultFileCombo(const QString& preferredFile = QString());
    void initResultDirectoryOptions();
    QString selectedResultDirectoryPath() const;
    void readTecplot(const QString& fileName);

    QString currentScalarName() const;
    QString currentScalarDisplayName() const;
    void refreshCurrentVisualization(bool renderNow = true);
    void updateRangeEditorState();

    vtkDataArray* findScalarArray(vtkUnstructuredGrid* grid, const QString& scalarName,
                                  bool& usePointData) const;
    void configureMapper(vtkDataSetMapper* mapper, const QString& scalarName, bool usePointData,
                         double minV, double maxV, vtkLookupTable* lut) const;
    bool prepareDisplaySurface(vtkUnstructuredGrid* sourceGrid, const QString& scalarName,
                               bool usePointData);
    void invalidateDisplaySurface();

    void updateContourOverlay(const QString& scalarName, bool usePointData, double minV,
                              double maxV);
    void clearContourOverlay();
    void invalidateContourCache();

  private:
    Ui::NephogramSettingWidget* ui{nullptr};
    VisualWidgetService* m_visualService{nullptr};

    vtkSmartPointer<vtkActor> m_actor;
    vtkSmartPointer<vtkActor> m_belowThresholdActor;
    vtkSmartPointer<vtkUnstructuredGrid> m_retainedThresholdGrid;
    vtkSmartPointer<vtkUnstructuredGrid> m_visibleVolumeGrid;
    PostSurfacePipeline* m_surfacePipeline{nullptr};
    QLabel* m_preparationStatus{nullptr};
    QString m_pendingScalar;
    vtkWeakPointer<vtkUnstructuredGrid> m_thresholdSource;
    vtkMTimeType m_thresholdSourceTime{0};
    QString m_thresholdField;
    double m_thresholdValue{0};
    bool m_thresholdPointData{true};
    vtkSmartPointer<vtkUnstructuredGrid> m_belowThresholdGrid;
    vtkSmartPointer<vtkDataSetMapper> m_mapper;
    vtkSmartPointer<vtkUnstructuredGrid> m_grid;
    // m_displaySurface 仅用于等值线的外表面缓存。
    vtkSmartPointer<vtkPolyData> m_displaySurface;
    QString m_displaySurfaceFilterSignature;

    QString m_dataPath;
    QString m_loadedFileName;
    qint64 m_loadedFileSize{-1};
    qint64 m_loadedFileModified{-1};
    QString m_cachedProjectDir;
    bool m_resultOptionsReady{false};
    QString m_currentArrayName;
    bool m_initialized{false};
    bool m_frameCameraOnReady{false};
    bool m_actorAttached{false};
    bool m_resultOptionsPreparedByLoadJson{false};
    bool m_loadingState{false};

    vtkSmartPointer<vtkActor> m_outlineActor;
    QString m_outlineFilterKey;
    vtkSmartPointer<vtkActor> m_contourActor;
    vtkSmartPointer<vtkActor2D> m_contourLabelActor;

    // 等值线几何缓存：切换色标、重新进入页面时不重复跑 ContourFilter。
    vtkSmartPointer<vtkPolyData> m_contourDataCache;
    QString m_contourCacheScalarName;
    vtkMTimeType m_contourCacheSurfaceMTime{0};
    int m_contourCacheLevels{0};
    double m_contourCacheMin{0.0};
    double m_contourCacheMax{0.0};
};

#endif
