#ifndef CURRENTDISTRIBUTIONWIDGETBASE_H
#define CURRENTDISTRIBUTIONWIDGETBASE_H

#include <BasePage.h>
#include <ProjectNode.h>
#include <QVector>
#include <QString>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QPushButton>

class CurrentDistributionWidgetBase : public BasePage
{
    Q_OBJECT

public:
    explicit CurrentDistributionWidgetBase(const QString &pageName, const QString &mode, QWidget *parent = nullptr);
    ~CurrentDistributionWidgetBase() override = default;

    void beforeLoad() override;
    void beforeUnload() override;

    void setNode(ProjectNode *node);

protected slots:
    void onElectrolyteClicked();
    void onElectrodeClicked();
    void onInsulationClicked();
    void onOutputBrowse();
    void onRunClicked();

protected:
    void connectCommonUi();
    void refreshSelectionDisplays();
    void syncConfigToSolver();
    void loadConfigFromSolver();

    QString mode() const { return m_mode; }

    virtual QLineEdit *outputDirLineEdit() const = 0;
    virtual QPushButton *outputBrowseButton() const = 0;
    virtual QLineEdit *electrolyteLineEdit() const = 0;
    virtual QLineEdit *electrodeLineEdit() const = 0;
    virtual QLineEdit *insulationLineEdit() const = 0;
    virtual QPushButton *electrolyteButton() const = 0;
    virtual QPushButton *electrodeButton() const = 0;
    virtual QPushButton *insulationButton() const = 0;
    virtual QPushButton *runButton() const = 0;

    virtual QDoubleSpinBox *conductivitySpin() const = 0;
    virtual QDoubleSpinBox *anodePotentialSpin() const = 0;
    virtual QDoubleSpinBox *cathodePotentialSpin() const = 0;
    virtual QDoubleSpinBox *exchangeCurrentSpin() const { return nullptr; }
    virtual QDoubleSpinBox *anodicCoeffSpin() const { return nullptr; }
    virtual QDoubleSpinBox *cathodicCoeffSpin() const { return nullptr; }
    virtual QDoubleSpinBox *diffusivitySpin() const { return nullptr; }
    virtual QDoubleSpinBox *concentrationSpin() const { return nullptr; }

private:
    void cacheCurrentSelections(int dim, QVector<int> &store);

    ProjectNode *m_node = nullptr;
    QString m_mode;

    QVector<int> m_electrolyteVolumes;
    QVector<int> m_electrodeFaces;
    QVector<int> m_insulationFaces;
};

#endif // CURRENTDISTRIBUTIONWIDGETBASE_H
