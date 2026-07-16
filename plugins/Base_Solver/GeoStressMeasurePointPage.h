#pragma once

#include <BasePage.h>
#include <PickManager.h>
#include <QVector>
#include <QStringList>

namespace Ui
{
class GeoStressMeasurePointPage;
}

class GeoStressMeasurePointPage : public BasePage
{
    Q_OBJECT
  public:
    struct MeasurePoint
    {
        QString x;
        QString y;
        QString z;
        QString sgx;
        QString sgy;
        QString sgz;
        QString txy;
        QString tyz;
        QString tzx;
    };

    explicit GeoStressMeasurePointPage(QWidget* parent = nullptr);
    ~GeoStressMeasurePointPage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    MeasurePoint point() const;
    QVector<MeasurePoint> points() const;

  private slots:
    void onClearClicked();
    void onImportClicked();
    void onAddManualRowClicked();
    void onAppendSelectedPointsClicked();
    void onRemoveRowsClicked();
    void onNodePicked(const QString& domain, const QString& activeUuid, GridKind kind,
                      vtkIdType pid, int nodeId, double x, double y, double z,
                      const QString& prevUuid);

  private:
    enum Column
    {
        ColNodeId = 0,
        ColX,
        ColY,
        ColZ,
        ColSgx,
        ColSgy,
        ColSgz,
        ColTxy,
        ColTyz,
        ColTzx,
        ColPid,
        ColCount
    };

    int currentDimension() const;
    void applyDimensionUi();
    bool appendImportedValues(const QStringList& values);
    bool appendPointByPid(vtkIdType pid);
    vtkIdType resolvePointIdByCoordinates(const QString& xText, const QString& yText,
                                          const QString& zText, QString* nodeId = nullptr) const;
    int appendRow(const QString& nodeId, const QString& x, const QString& y, const QString& z,
                  const QString& sgx, const QString& sgy, const QString& sgz,
                  const QString& txy, const QString& tyz, const QString& tzx,
                  vtkIdType pid = -1);
    QString cellText(int row, int col) const;
    void setCellText(int row, int col, const QString& text, bool editable = true);
    void syncPickedPointIds();

  private:
    Ui::GeoStressMeasurePointPage* ui = nullptr;
};
