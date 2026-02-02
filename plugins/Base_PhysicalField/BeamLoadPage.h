#pragma once

#include "BasePage.h"

namespace Ui { class BeamLoadPage; }

class BeamLoadPage : public BasePage
{
    Q_OBJECT
public:
    explicit BeamLoadPage(QWidget *parent = nullptr);
    ~BeamLoadPage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    // ===== Part8 需要的参数 =====
    int    ldTypeValue() const; // LDTYPE
    double aValue() const;      // LOCATION / A
    double pValue() const;      // LOAD / P
    int    idirValue() const;   // ACTION_PLANE / IDIR


private:
    Ui::BeamLoadPage *ui = nullptr;
};
