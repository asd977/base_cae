#pragma once

#include "BasePage.h"

class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QComboBox;

namespace Ui { class PointForcePage; }

class PointForcePage : public BasePage
{
    Q_OBJECT
public:
    explicit PointForcePage(QWidget* parent = nullptr);
    ~PointForcePage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    double fxValue() const;
    double fyValue() const;
    double fzValue() const;
    double mxValue() const;
    double myValue() const;
    double mzValue() const;



private slots:
    void onGroupChanged(int index);
    void onDimChanged(int index);

private:
    void setupBatchGroupCombo();
    void refreshGroupCombo();

private:
    Ui::PointForcePage* ui = nullptr;
    QComboBox* m_groupCombo = nullptr;

};
