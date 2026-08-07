#pragma once

#include <BasePage.h>
#include <PickManager.h>

#include <QJsonObject>
#include <QString>

namespace Ui
{
class StressBoundaryPage;
}

class StressBoundaryPage : public BasePage
{
    Q_OBJECT
  public:
    explicit StressBoundaryPage(const QString& pageType, const QString& boundaryTitle,
                                const QString& exportFileName, QWidget* parent = nullptr);
    ~StressBoundaryPage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    bool exportDatFile();
    int selectionCount();

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void selectionCountChanged(int count);

  private slots:
    void onClearClicked();
    void onGroupChanged(int index);

  public:
    struct EdgeRow
    {
        int ke1 = 0;
        int ke2 = 0;
        int jd1 = 0;
        int jd2 = 0;
        int jd3 = 0;
        int jd4 = 0;
    };

  private:
    QVector<EdgeRow> selectedEdgeRows();
    QString pickDomain();
    void setupBatchGroupCombo();
    PickMode currentPickMode() const;
    void updateSelectionSummary();

  private:
    Ui::StressBoundaryPage* ui = nullptr;
    QString m_boundaryTitle;
    QString m_exportFileName;
};
