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
    bool ensureTransparentDomainSurface();
    void syncScalarBarFromCurrent();
    bool syncScalarBarFromResolved(vtkUnstructuredGrid* displayGrid, vtkDataArray* array,
                                   bool usePointData, double dataMin, double dataMax);
    void updateNephogram(const QString& selectedScalarName, bool renderNow = true);
    void loadFileName(const QString& dataPath);
    void initResultDirectoryOptions();
    QString selectedResultDirectoryPath() const;
    void readTecplot(const QString& fileName);
    void readVtu(const QString& fileName);

    QString currentScalarName() const;
    QString currentScalarDisplayName() const;
    void refreshCurrentVisualization(bool renderNow = true);
    void updateRangeEditorState();
    void setRangeControls(double minV, double maxV);
    bool displayRange(double dataMin, double dataMax, double& minV, double& maxV) const;
    void applyLegendAppearance(vtkScalarBarActor* scalarBar, double minV, double maxV) const;

    vtkDataArray* findScalarArray(vtkUnstructuredGrid* grid, const QString& scalarName,
                                  bool& usePointData) const;
    void updateLookupTableRange(vtkLookupTable* lut, double minV, double maxV) const;
    void configureMapper(vtkDataSetMapper* mapper, const QString& scalarName, bool usePointData,
                         double minV, double maxV, vtkLookupTable* lut) const;
    bool prepareDisplaySurface(vtkUnstructuredGrid* sourceGrid, const QString& scalarName,
                               bool sourceUsePointData, bool& mapperUsePointData);
    void invalidateDisplaySurface();

    void updateContourOverlay(const QString& scalarName, bool usePointData, double minV,
                              double maxV);
    void clearContourOverlay();
    void invalidateContourCache();

  private:
    Ui::NephogramSettingWidget* ui{nullptr};
    VisualWidgetService* m_visualService{nullptr};

    vtkSmartPointer<vtkActor> m_actor;
    vtkSmartPointer<vtkDataSetMapper> m_mapper;
    vtkSmartPointer<vtkUnstructuredGrid> m_grid;
    // m_displaySurface 仅用于等值线的外表面缓存。
    vtkSmartPointer<vtkPolyData> m_displaySurface;
    bool m_displaySurfaceUsePointData{true};
    QString m_displaySurfaceFilterSignature;

    // 半透明模式使用的“外表面 + MATYPE/LAYER 域界面”只构造一次。
    // 它与等值线 Surface 分离，避免拖动透明度时反复遍历全部体单元。
    vtkSmartPointer<vtkPolyData> m_transparentDomainSurface;
    bool m_usingTransparentDomainSurface{false};

    QString m_dataPath;
    QString m_loadedFileName;
    QString m_currentArrayName;
    bool m_initialized{false};
    bool m_actorAttached{false};
    bool m_resultOptionsPreparedByLoadJson{false};
    bool m_loadingState{false};
    bool m_updatingRangeControls{false};
    bool m_hasLegendPosition{false};
    double m_legendPositionX{0.0};
    double m_legendPositionY{0.0};

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
