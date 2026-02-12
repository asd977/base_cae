#ifndef LINEARFORCEPAGE_H
#define LINEARFORCEPAGE_H

#include <BasePage.h>
#include <QWidget>
class QComboBox;

namespace Ui {
class LinearForcePage;
}

class LinearForcePage : public BasePage
{
    Q_OBJECT

public:
    explicit LinearForcePage(QWidget *parent = nullptr);
    ~LinearForcePage();

    void beforeLoad();
    void beforeUnload();

    double fxValue();
    double fyValue();
    double fzValue();

private slots:
    void onGroupChanged(int index);
    void onDimChanged(int index);

private:
    void setupBatchGroupCombo();
    void refreshGroupCombo();

private:
    Ui::LinearForcePage *ui;
    QComboBox* m_groupCombo = nullptr;
};

#endif // LINEARFORCEPAGE_H
