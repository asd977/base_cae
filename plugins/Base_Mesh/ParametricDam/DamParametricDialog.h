#pragma once

#include "DamParametricTypes.h"
#include "FillPrepIO.h"

#include <QDialog>

namespace Ui
{
class DamParametricDialog;
}

class DamParametricDialog : public QDialog
{
    Q_OBJECT

  public:
    DamParametricDialog(const DamParametricContext& ctx, const MsgJzDamParams& params,
                        QWidget* parent = nullptr);
    ~DamParametricDialog() override;

  signals:
    void iiinGenerated(const QString& iiinPath);

  private slots:
    void onConvertClicked();
    void onGenerateClicked();

  private:
    void applyContextToUi();
    void applyParamsToUi();
    bool collectParamsFromUi(QString* error);

    void applyFillPrepToUi();
    bool collectFillPrepFromUi(QString* error);

    void updateObjectLabels();

  private:
    Ui::DamParametricDialog* ui = nullptr;
    DamParametricContext m_ctx;
    MsgJzDamParams m_params;
    FillPrepParams m_fillPrep;
};
