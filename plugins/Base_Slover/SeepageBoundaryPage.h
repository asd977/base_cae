#pragma once

#include <BasePage.h>
#include <QString>
#include <QTimer>
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

    bool exportDatFile();

private slots:
    void onClearClicked();
    void onRealtimeRefresh();

private:
    QVector<int> selectedNodeIds();
    void refreshPreview();

private:
    Ui::SeepageBoundaryPage* ui = nullptr;
    QString m_boundaryTitle;
    QString m_exportFileName;
    QTimer m_realtimeRefreshTimer;
};
