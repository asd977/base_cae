#pragma once
#include "BasePage.h"

class QEvent;

namespace Ui { class DatasetEvaluation; }

class DatasetEvaluation : public BasePage
{
    Q_OBJECT
public:
    explicit DatasetEvaluation(QWidget *parent = nullptr);
    ~DatasetEvaluation();

    // BasePage lifecycle hooks（如需）
    void beforeLoad() override;
    void beforeUnload() override;

private:
    Ui::DatasetEvaluation *ui;

    // 状态
    QStringList m_allParams;   // 数据集中可用的数值列
    QStringList m_usedParams;  // 已勾选参与评估的列（顺序即维度顺序）

    // 交互
    void wireSignals();
    void onDatasetKindChanged();
    void detectParamsFromDataset();
    void rebuildTestTable();
    void addTestRow();
    void removeSelectedTestRows();

    // IO
    QString makeTestJsonFile() const;
    bool    runRbfEval(const QString& trainPayloadPath,
                       const QString& testPayloadPath,
                       double threshold,
                       QString* outTxtPath,
                       QString* outPngPath,
                       QString* consoleOut);

    void    loadRbfOutputToTable(const QString& outTxtPath);
    void    loadImageIfExists(const QString& pngPath);

    // 工具
    static QString defaultOrthFile();
    static QString defaultLhsFile();
    static QString defaultPythonExe();
    static QString defaultScriptPath();
    static QStringList tryDetectHeaders(const QString& path, QString* err);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;
};
