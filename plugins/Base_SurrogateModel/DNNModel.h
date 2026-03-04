#ifndef DNNMODEL_H
#define DNNMODEL_H

#include <BasePage.h>
#include <services/OutputService.h>

#include <QLineSeries>
#include <QProcess>
#include <QTextStream>
#include <QWidget>
#include <QtCharts>

namespace Ui
{
class DNNModel;
}

class DNNModel : public BasePage
{
    Q_OBJECT

  public:
    explicit DNNModel(QWidget* parent = nullptr);
    ~DNNModel();

    void beforeLoad() override;   // 仅首次初始化
    void beforeUnload() override; // 无需处理

    bool readFromStream(QTextStream& in);
    bool writeToStream(QTextStream& out);
  private slots:
    void on_browseSourceFilepushButton_clicked();

    void on_importSourceFilePushButton_clicked();

    void on_startTrainModel_clicked();

    void on_continueTrainModel_clicked();

    void on_addLayerButton_clicked();

    void on_deleteLayerButton_clicked();

    void on_layerWidget_itemSelectionChanged();
    void updateLineGraph();

  private:
    Ui::DNNModel* ui;

    QString m_workPath;
    QStringList m_variableNames;
    QProcess* m_trainModelProcess;
    QProcess* m_splitProcess;

    OutputService* m_outputService;
    bool isFinished;
    void on_neuronLineEdit_editingFinished();
    void on_activationComboBox_currentTextChanged(const QString& text);
    void updateSelectedLayer();
    void getLayerParameters(QString& layerSizesStr, QString& activationFunctionsStr);
    void setStyle();

    QLineSeries* actualSeries;
    QLineSeries* predictedSeries;
    QChart* chart;
    QValueAxis* axisX;
    QValueAxis* axisY;
    void connectOutput();
};

#endif // DNNMODEL_H
