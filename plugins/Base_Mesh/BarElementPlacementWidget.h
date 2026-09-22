#pragma once

#include <BasePage.h>
#include <PickManager.h>

#include <QVector>
#include <array>
#include <vtkSmartPointer.h>

class QCheckBox;
class QComboBox;
class QJsonArray;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;
class QWidget;
class VisualWidgetService;
class vtkActor;
class vtkBillboardTextActor3D;
class vtkPolyData;

class BarElementPlacementWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit BarElementPlacementWidget(QWidget* parent = nullptr);

    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    void refreshData();

  private slots:
    void createBars();
    void removeSelectedRecord();

  private:
    enum class PlacementMode
    {
        Manual = 0,
        SurfaceSemiAuto = 1
    };

    enum class DefinitionMode
    {
        HeadLengthDirection = 0,
        TwoPoints = 1
    };

    enum class PickTarget
    {
        None,
        Start,
        End,
        DirectionPoint
    };

    struct PreviewBar
    {
        std::array<double, 3> start{{0.0, 0.0, 0.0}};
        std::array<double, 3> end{{0.0, 0.0, 0.0}};
        std::array<double, 3> direction{{0.0, 0.0, 0.0}};
    };

    PlacementMode placementMode() const;
    DefinitionMode definitionMode() const;
    bool isSurfaceSemiAuto() const;
    void updatePlacementUi();
    void handlePlacementModeChanged();
    void activateSurfacePick();
    void deactivateSurfacePick();
    void updateSurfacePickStatus();
    void applyWholeSurfacePick(vtkIdType seedFaceKey, bool selected);
    void scheduleSurfacePreview();
    void clearSurfaceSelection();
    QVector<PreviewBar> buildSurfaceBars(QString* errMsg = nullptr,
                                         int* selectedFaceCount = nullptr,
                                         int* selectedRegionCount = nullptr);
    void beginCoordinatePick(PickTarget target);
    void finishCoordinatePick();
    void applyPickedCoordinate(int nodeId, double x, double y, double z);
    void updateDefinitionUi();
    bool updateEndFromLengthDirection(bool showWarning = false);
    void updateLengthLabel();
    void setPickStatus(const QString& text);
    void setActionStatus(const QString& text);
    void updateArrayUi();
    void refreshRecords();
    void initializeMaterialNumber();
    void updateMaterialStatus();
    int resolveMaterialNumberForCreate();
    void ensureRecordHighlightActor();
    void showRecordHighlight(int recordId);
    void clearRecordHighlight();
    void ensurePreviewActors();
    void removePreviewActors();
    void clearPreviewGeometry();
    void markManualParametersChanged();
    void updatePreview();
    QString previewPropId(const QString& suffix) const;
    QDoubleSpinBox* coordinateSpin(double value = 0.0);
    QDoubleSpinBox* directionSpin(double value = 0.0);
    std::array<double, 3> currentArrayOffset() const;
    void loadLegacyArrayOffset(const QJsonArray& offset);

    QComboBox* m_placementMode = nullptr;
    QWidget* m_manualPanel = nullptr;
    QWidget* m_surfacePanel = nullptr;
    QComboBox* m_definitionMode = nullptr;
    QLabel* m_endRowLabel = nullptr;
    QDoubleSpinBox* m_start[3]{};
    QDoubleSpinBox* m_end[3]{};
    QPushButton* m_pickStartButton = nullptr;
    QPushButton* m_pickEndButton = nullptr;
    QPushButton* m_pickDirectionButton = nullptr;
    QWidget* m_lengthDirectionPanel = nullptr;
    QDoubleSpinBox* m_length = nullptr;
    QDoubleSpinBox* m_direction[3]{};
    QLabel* m_pickStatus = nullptr;
    QLabel* m_lengthLabel = nullptr;

    QDoubleSpinBox* m_surfaceLength = nullptr;
    QSpinBox* m_surfaceGridIntervalU = nullptr;
    QSpinBox* m_surfaceGridIntervalV = nullptr;
    QCheckBox* m_surfaceWholeFace = nullptr;
    QComboBox* m_surfaceDirectionMode = nullptr;
    QLabel* m_surfaceStatus = nullptr;
    QCheckBox* m_surfacePickCheckBox = nullptr;
    QPushButton* m_clearSurfaceButton = nullptr;
    QTimer* m_surfacePreviewTimer = nullptr;

    QWidget* m_manualArrayPanel = nullptr;
    QComboBox* m_arrayMode = nullptr;
    QWidget* m_arrayRow = nullptr;
    QSpinBox* m_arrayCount = nullptr;
    QDoubleSpinBox* m_arraySpacing = nullptr;
    QComboBox* m_arrayAxis = nullptr;
    QSpinBox* m_installationLayer = nullptr;
    QLabel* m_materialStatus = nullptr;
    QLabel* m_actionStatus = nullptr;
    QPushButton* m_createButton = nullptr;
    QTableWidget* m_records = nullptr;
    QLabel* m_recordEmptyLabel = nullptr;

    PickTarget m_pickTarget = PickTarget::None;
    std::array<double, 3> m_legacyArrayDirection{{0.0, 0.0, 0.0}};
    int m_assignedMatype = 0;
    bool m_pageActive = false;
    bool m_surfacePickActive = false;
    bool m_manualPickActive = false;
    bool m_manualReadyToAdd = true;
    // 每次区域点击的 seed 面。网格间隔以各自 seed 为起点向四周计算。
    QVector<vtkIdType> m_surfaceLayoutSeedFaces;
    VisualWidgetService* m_visual = nullptr;
    vtkSmartPointer<vtkPolyData> m_previewPolyData;
    vtkSmartPointer<vtkActor> m_previewActor;
    vtkSmartPointer<vtkPolyData> m_recordHighlightPolyData;
    vtkSmartPointer<vtkActor> m_recordHighlightActor;
    vtkSmartPointer<vtkPolyData> m_surfaceGuidePolyData;
    vtkSmartPointer<vtkActor> m_surfaceGuideActor;
    vtkSmartPointer<vtkBillboardTextActor3D> m_surfaceULabelActor;
    vtkSmartPointer<vtkBillboardTextActor3D> m_surfaceVLabelActor;
    std::array<double, 3> m_surfaceGuideOrigin{{0.0, 0.0, 0.0}};
    std::array<double, 3> m_surfaceGuideNormal{{0.0, 0.0, 0.0}};
    std::array<double, 3> m_surfaceGuideU{{0.0, 0.0, 0.0}};
    std::array<double, 3> m_surfaceGuideV{{0.0, 0.0, 0.0}};
    double m_surfaceGuideScale = 0.0;
    bool m_surfaceGuideValid = false;
    vtkSmartPointer<vtkBillboardTextActor3D> m_startLabelActor;
    vtkSmartPointer<vtkBillboardTextActor3D> m_endLabelActor;
};
