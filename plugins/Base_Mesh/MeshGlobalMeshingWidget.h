#pragma once

#include <BasePage.h>

#include <QString>
#include <QThread>
#include <QWidget>

class QThread;

namespace Ui
{
class MeshGlobalMeshingWidget;
}

struct Params
{
    QString inputStepPath;
    int dimension = 3;
    int elemType = 3;
    double lenMin = 0.5;
    double lenMax = 5.0;
    double sizeFactor = 1.0;
    int elementOrder = 1;
    int algorithm = 1;
    int smoothing = 5;
    bool fixShape = true;
    double tolerance = 1e-6;
    bool exportVol = true;
    bool exportVtk = true;
    bool exportNas = true;
};

class MeshGlobalWorker : public QObject
{
    Q_OBJECT
  public:
    explicit MeshGlobalWorker(Params p, QObject* parent = nullptr);

  signals:
    void log(const QString& s);
    void finishedOK(const QString& vol, const QString& vtk, const QString& nas);
    void finishedFail(const QString& err);

  public slots:
    void run();

  public:
    QString m_tempDirPath;
    QString m_tempStepPath;
    QString m_tempVolPath;
    QString m_tempVtkPath;
    QString m_tempNasPath;

  private:
    Params m_p;
};

class MeshGlobalMeshingWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit MeshGlobalMeshingWidget(QWidget* parent = nullptr);
    ~MeshGlobalMeshingWidget();

    void beforeLoad() override;
    void beforeUnload() override;

  signals:
    void meshGenerated(const QString& volPath, const QString& vtkPath, const QString& nasPath);
    void logMessage(const QString& msg);

  private slots:
    void onGenerate();
    void onExportAs();
    void onOpenTempDir();
    void onDimChanged(int idx);
    void onElemTypeChanged(int idx);
    void on_generateButton_clicked();

  private:
    void initCombos();
    void refreshAlgoCombo(int dim);
    void refreshElemCombo(int dim);
    Params collectParams() const;

    QString tempDirPath() const;
    QString tempStepPath() const;
    QString tempVolPath() const;
    QString tempVtkPath() const;
    QString tempNasPath() const;

    void appendLog(const QString& s);
    void showMeshVtk(const QString& vtkPath);

  private:
    Ui::MeshGlobalMeshingWidget* ui = nullptr;
    QThread* m_thread = nullptr;
    bool m_lastGeneratedHex8 = false;
};
