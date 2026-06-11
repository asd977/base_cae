#ifndef OUTPUTWIDGET_H
#define OUTPUTWIDGET_H

#include <QVector>
#include <QWidget>

class QShowEvent;


namespace Ui
{
class OutputWidget;
}

class OutputWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit OutputWidget(QWidget* parent = nullptr);
    ~OutputWidget();

    void outputMessage(const QString& str);
    void showEvent(QShowEvent* event);

  private slots:
    void onClearClicked();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    Ui::OutputWidget* ui;
};

#endif // OUTPUTWIDGET_H
