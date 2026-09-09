#pragma once

#include <BasePage.h>

#include <QSet>
#include <QVector>
#include <array>
#include <vtkSmartPointer.h>
#include <vtkType.h>

class QComboBox;
class QJsonArray;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
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
    void updatePickingMode();
    void applyEditingSurfaceStyle();
    void beginSurfaceRegionPick();
    void handleSurfaceFaceSelection();
    void applySurfaceSeedFace(vtkIdType seedFaceKey);
    void clearSurfaceSelection();
    QVector<vtkIdType> growSurfaceRegion(vtkIdType seedFaceKey,
                                         QString* errMsg = nullptr) const;
    QVector<PreviewBar> buildSurfaceBars(QString* errMsg = nullptr,
                                         int* selectedFaceCount = nullptr) const;
    void beginCoordinatePick(PickTarget target);
    void finishCoordinatePick();
    void applyPickedCoordinate(int nodeId, double x, double y, double z);
    void updateDefinitionUi();
    bool updateEndFromLengthDirection(bool showWarning = false);
    void updateLengthLabel();
    void updateArrayUi();
    void refreshRecords();
    void initializeMaterialNumber();
    void updateMaterialStatus();
    int resolveMaterialNumberForCreate();
    void ensurePreviewActors();
    void removePreviewActors();
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
    QDoubleSpinBox* m_surfaceSpacing = nullptr;
    QLabel* m_surfaceStatus = nullptr;
    QPushButton* m_pickSurfaceButton = nullptr;
    QPushButton* m_clearSurfaceButton = nullptr;

    QWidget* m_manualArrayPanel = nullptr;
    QComboBox* m_arrayMode = nullptr;
    QWidget* m_arrayRow = nullptr;
    QSpinBox* m_arrayCount = nullptr;
    QDoubleSpinBox* m_arraySpacing = nullptr;
    QComboBox* m_arrayAxis = nullptr;
    QLabel* m_materialStatus = nullptr;
    QLabel* m_actionStatus = nullptr;
    QPushButton* m_createButton = nullptr;
    QTableWidget* m_records = nullptr;

    PickTarget m_pickTarget = PickTarget::None;
    std::array<double, 3> m_legacyArrayDirection{{0.0, 0.0, 0.0}};
    int m_assignedMatype = 0;
    bool m_pageActive = false;
    bool m_surfacePickArmed = false;
    bool m_surfaceSelectionUpdating = false;
    QSet<vtkIdType> m_surfaceRegionFaceKeys;
    VisualWidgetService* m_visual = nullptr;
    vtkSmartPointer<vtkPolyData> m_previewPolyData;
    vtkSmartPointer<vtkActor> m_previewActor;
    vtkSmartPointer<vtkBillboardTextActor3D> m_startLabelActor;
    vtkSmartPointer<vtkBillboardTextActor3D> m_endLabelActor;
};
