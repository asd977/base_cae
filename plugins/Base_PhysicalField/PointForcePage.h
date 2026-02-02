#pragma once

#include "BasePage.h"

class QDoubleSpinBox;
class QSpinBox;
class QPushButton;

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



private:
    Ui::PointForcePage* ui = nullptr;

};
