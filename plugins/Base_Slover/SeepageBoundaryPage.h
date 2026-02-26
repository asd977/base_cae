#pragma once

#include <BasePage.h>
#include <QString>
#include <QVector>
#include <QJsonObject>

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

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

private slots:
    void onClearClicked();
    void onDimChanged(int index);
    void onGroupChanged(int index);

private:
    QVector<int> selectedNodeIds();
    void setupBatchGroupCombo();
    void refreshGroupCombo();

private:
    Ui::SeepageBoundaryPage* ui = nullptr;
    QString m_boundaryTitle;
    QString m_exportFileName;
};
