#ifndef SLOPESTABILITYWIDGET_H
#define SLOPESTABILITYWIDGET_H

#include <BasePage.h>

#include <QJsonObject>
#include <QMap>

class QLineEdit;
class QComboBox;
class QPushButton;
class QTableWidget;
class QWidget;

class SlopeStabilityWidget : public BasePage
{
    Q_OBJECT

  public:
    explicit SlopeStabilityWidget(QWidget* parent = nullptr);
    ~SlopeStabilityWidget() override = default;

    void beforeLoad() override;
    void beforeUnload() override;

    bool exportIslopDatFile() const;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void computeRequested();

  private slots:
    void onComputeClicked();

  private:
    QString quoteValue(const QString& value) const;
    QString lineValue(int lineNo) const;
    void setLineValue(int lineNo, const QString& value);
    QString defaultValueByLine(int lineNo) const;
    void initStrengthReserveTable();

  private:
    QMap<int, QWidget*> m_lineEditors;
    QTableWidget* m_strengthReserveTable = nullptr;
    QLineEdit* m_caseDirEdit = nullptr;
    QLineEdit* m_dynamicArgsEdit = nullptr;
    QLineEdit* m_accXEdit = nullptr;
    QLineEdit* m_accYEdit = nullptr;
    QLineEdit* m_accZEdit = nullptr;
};

#endif // SLOPESTABILITYWIDGET_H
