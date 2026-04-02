#ifndef GEOSTRESSREGRESSIONWIDGET_H
#define GEOSTRESSREGRESSIONWIDGET_H

#include <BasePage.h>
#include <QVector>
#include <QStringList>

class QWidget;

namespace Ui
{
class GeoStressRegressionWidget;
}

class GeoStressRegressionWidget : public BasePage
{
    Q_OBJECT

  public:
    struct MeasureRow
    {
        QString x;
        QString y;
        QString sgx;
        QString sgy;
        QString txy;
        QString ii;
    };
    struct FaceAttrRow
    {
        QString idir;
        QString height;
        QString gama;
        QString pgrout;
        QString ii;
    };
    struct ExportPayload
    {
        QVector<MeasureRow> measureRows;
        QVector<FaceAttrRow> faceRows;
        QStringList sideFaceFiles;
        QString forceFile;
        int forceEnabled = 0;
    };

    explicit GeoStressRegressionWidget(QWidget* parent = nullptr);
    ~GeoStressRegressionWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

    bool exportIGEOSDatFile(const ExportPayload& payload) const;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void computeRequested();

  private slots:
    void onComputeClicked();

  private:
    Ui::GeoStressRegressionWidget* ui;
};

#endif // GEOSTRESSREGRESSIONWIDGET_H
