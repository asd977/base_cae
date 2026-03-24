#ifndef SLOPESTABILITYWIDGET_H
#define SLOPESTABILITYWIDGET_H

#include <BasePage.h>

#include <QJsonObject>

#include <QString>
#include <QVector>

class QPushButton;
class QLabel;
class QTableWidget;

class SlopeStabilityWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit SlopeStabilityWidget(QWidget* parent = nullptr);

    void beforeLoad() override;
    void beforeUnload() override;

    bool exportIslopDatFile() const;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void computeRequested();

  private:
    struct DatItem
    {
        int lineNo = 0;
        QString key;
        QString value;
        QString description;
    };

    void initDefaultItems();
    void refreshTable();

  private:
    QVector<DatItem> m_items;
    QTableWidget* m_table = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QPushButton* m_computeButton = nullptr;
};

#endif // SLOPESTABILITYWIDGET_H
