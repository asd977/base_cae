#pragma once

#include <BasePage.h>
#include <QString>
#include <QVector>

namespace Ui {
class SeepageBoundaryPage;
}

class SeepageBoundaryPage : public BasePage
{
    Q_OBJECT
public:
    explicit SeepageBoundaryPage(const QString& pageType,
                                 const QString& boundaryTitle,
                                 const QString& exportFileName,
                                 QWidget* parent = nullptr);
    ~SeepageBoundaryPage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    bool exportDatFile() const;

private slots:
    void onRefreshClicked();
    void onClearClicked();
    void onExportClicked();

private:
    QVector<int> selectedNodeIds() const;
    void refreshPreview();

private:
    Ui::SeepageBoundaryPage* ui = nullptr;
    QString m_boundaryTitle;
    QString m_exportFileName;
};
