#pragma once

#include <QPair>
#include <QJsonObject>
#include <QVector>

#include "BasePage.h"

namespace Ui
{
class SeismicAndAddedMassPage;
}

class SeismicAndAddedMassPage : public BasePage
{
    Q_OBJECT
  public:
    explicit SeismicAndAddedMassPage(QWidget* parent = nullptr);
    ~SeismicAndAddedMassPage() override;

    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    // AMP（写到第一行的第二列）
    double ampX() const;
    double ampY() const;
    double ampZ() const;

    // 从表格解析出的序列 (T, A*)
    QVector<QPair<double, double>> seriesX() const; // (T, AX)
    QVector<QPair<double, double>> seriesY() const; // (T, AY)
    QVector<QPair<double, double>> seriesZ() const; // (T, AZ)

  private slots:
    void on_btnAddRow_clicked();
    void on_btnDeleteRow_clicked();
    void on_btnClear_clicked();
    void on_btnImportCsv_clicked();

  private:
    bool cellToDouble(int row, int col, double& out) const;
    void ensureAtLeastOneRow();

  private:
    Ui::SeismicAndAddedMassPage* ui = nullptr;
};
