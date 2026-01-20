#pragma once

#include "BasePage.h"

class QPlainTextEdit;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QTableWidget;
class QPushButton;

class SeismicAndAddedMassPage : public BasePage
{
    Q_OBJECT
public:
    explicit SeismicAndAddedMassPage(QWidget* parent = nullptr);
    ~SeismicAndAddedMassPage() override;

    void beforeLoad() override {}
    void beforeUnload() override {}

private slots:
    void onGenerate();
    void onFillExampleX();
    void onFillExampleY();
    void onFillExampleZ();
    void onAddMassRow();
    void onRemoveMassRow();

private:
    // 解析多行文本为 double 列表（每行一个数；也允许空格分隔）
    static QVector<double> parseSeries(const QString& text, QString* err = nullptr);

    void dumpGeneratedToDebug();

private:
    // Seismic series
    QSpinBox*       m_ntbx = nullptr;
    QSpinBox*       m_ntby = nullptr;
    QSpinBox*       m_ntbz = nullptr;

    QPlainTextEdit* m_xSeries = nullptr;
    QPlainTextEdit* m_ySeries = nullptr;
    QPlainTextEdit* m_zSeries = nullptr;

    // Added mass
    QSpinBox*       m_nmm = nullptr;
    QSpinBox*       m_makod = nullptr;
    QDoubleSpinBox* m_hwupTop = nullptr;
    QDoubleSpinBox* m_hwupBot = nullptr;
    QCheckBox*      m_ifwest = nullptr;

    QTableWidget*   m_massTable = nullptr;

    QPushButton*    m_btnGenerate = nullptr;
    QPushButton*    m_btnAddRow = nullptr;
    QPushButton*    m_btnRemoveRow = nullptr;
    QPushButton*    m_btnExampleX = nullptr;
    QPushButton*    m_btnExampleY = nullptr;
    QPushButton*    m_btnExampleZ = nullptr;
};
