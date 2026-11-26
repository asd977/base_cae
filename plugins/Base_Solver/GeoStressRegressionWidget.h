#ifndef GEOSTRESSREGRESSIONWIDGET_H
#define GEOSTRESSREGRESSIONWIDGET_H

#include <BasePage.h>
#include <QVector>
#include <QStringList>

class QWidget;
class QLabel;

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
        QString z;
        QString sgx;
        QString sgy;
        QString sgz;
        QString txy;
        QString tyz;
        QString tzx;
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
        bool roofFileReady = false;
        int forceEnabled = 0;
    };

    explicit GeoStressRegressionWidget(QWidget* parent = nullptr);
    ~GeoStressRegressionWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

    bool exportIGEOSDatFile(const ExportPayload& payload) const;
    bool validateInputs(QString* errorMessage = nullptr) const;
    int calculationModeCode() const;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void computeRequested();
    void stopRequested();
    void calculationModeChanged();


  private slots:
    void onComputeClicked();
    void setComputing(bool computing);
    void updateModeUi();


  private:
    Ui::GeoStressRegressionWidget* ui;
    QLabel* m_modeHint = nullptr;
};

#endif // GEOSTRESSREGRESSIONWIDGET_H
