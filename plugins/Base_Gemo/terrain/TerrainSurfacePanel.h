#pragma once
#include <BasePage.h>
#include "TerrainSurfaceBuilder.h"
class QTableWidget;
class QLabel;
class QDoubleSpinBox;
class QPushButton;
class QCheckBox;
class QComboBox;

class TerrainSurfacePanel : public BasePage
{
public:
    explicit TerrainSurfacePanel(QWidget* parent=nullptr);
    ~TerrainSurfacePanel() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& state) override;
    void beforeLoad() override;
    void beforeUnload() override;
    bool importFile(const QString& path, QString& diagnostic);
    bool generate(QString& diagnostic);
    void setSources(const QVector<GeometryTerrain::Source>& sources);
    const GeometryTerrain::Result& result() const {return m_result;}
private:
    void refreshTable();
    void invalidate();
    void updatePreview(bool fitCamera=false);
    void fitView(const double bounds[6]);
    QVector<GeometryTerrain::Source> m_sources;
    GeometryTerrain::Result m_result;
    bool m_generated=false, m_refreshing=false;
    QString m_sourcePath;
    QTableWidget* m_table=nullptr;
    QLabel* m_status=nullptr;
    QDoubleSpinBox* m_spacing=nullptr;
    QComboBox* m_domainMode=nullptr;
    QCheckBox* m_showContours=nullptr;
};
