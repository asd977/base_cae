#pragma once
#include <BasePage.h>
#include "TerrainSurfaceBuilder.h"

class QTableWidget;
class QLabel;
class QDoubleSpinBox;
class QPushButton;
class QCheckBox;
class QComboBox;
class vtkActor;

class TerrainSurfacePanel : public BasePage
{
public:
    explicit TerrainSurfacePanel(QWidget* parent = nullptr);
    ~TerrainSurfacePanel() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& state) override;
    void beforeLoad() override;
    void beforeUnload() override;

    bool importFile(const QString& path, QString& diagnostic);
    bool generate(QString& diagnostic);
    void setSources(const QVector<GeometryTerrain::Source>& sources);
    const GeometryTerrain::Result& result() const { return m_result; }

    bool isSurfaceColorEnabled() const;
    bool isContoursEnabled() const;
    void invalidatePreview();

private:
    void refreshTable();
    void refreshRow(int row);
    void invalidate();
    void updatePreview(bool fitCamera = false);
    void fitView(const double bounds[6]);
    void updateBoundsFromSources();
    void updateGridStats();
    void updateModeControls();

    QVector<GeometryTerrain::Source> m_sources;
    GeometryTerrain::Result m_result;
    bool m_generated = false;
    bool m_refreshing = false;
    QString m_sourcePath;
    vtkSmartPointer<vtkActor> m_sourceActor;

    QTableWidget* m_table = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_gridStatsLabel = nullptr;
    QLabel* m_spacingLabel = nullptr;
    QDoubleSpinBox* m_spacing = nullptr;
    QDoubleSpinBox* m_xmin = nullptr;
    QDoubleSpinBox* m_xmax = nullptr;
    QDoubleSpinBox* m_ymin = nullptr;
    QDoubleSpinBox* m_ymax = nullptr;
    QPushButton* m_autoBoundsBtn = nullptr;
    QComboBox* m_algorithm = nullptr;
    QComboBox* m_domainMode = nullptr;
    QCheckBox* m_showContours = nullptr;
    QCheckBox* m_showSurface = nullptr;
    QCheckBox* m_createSolid = nullptr;
    QLabel* m_baseElevationLabel = nullptr;
    QDoubleSpinBox* m_baseElevation = nullptr;
};
