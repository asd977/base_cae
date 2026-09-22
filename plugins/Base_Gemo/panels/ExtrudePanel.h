#pragma once

#include <BasePage.h>
#include <CadTypes.h>

#include <QJsonObject>
#include <QList>
#include <QVector>
#include <QString>

class QTimer;

namespace Ui
{
class ExtrudeDialog;
}


class ExtrudeDialog : public BasePage
{
    Q_OBJECT

public:
    explicit ExtrudeDialog(QWidget* parent = nullptr);
    ~ExtrudeDialog() override;

    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    void setInitialProfile(const QString& uuid);

private slots:
    void onApplyClicked();

private:
    struct ProfileInfo
    {
        QString uuid;
        QString displayName;
        int closedRegionCount = 0;
        int openEdgeCount = 0;
        bool valid = false;
        QString message;
    };

    void refreshProfiles();
    void updateCurrentProfileInfo();
    void updateParameterVisibility();
    void updateStatus(const QString& text, bool ok);
    void syncProfileHighlight();
    void startDirectFaceSelection();
    void toggleSelectedFace(const QString& uuid, int faceIndex);
    void syncSelectedFaceHighlight();

    QString currentProfileUuid() const;
    bool currentProfileInfo(ProfileInfo& info) const;
    ExtrudeParameters parameters() const;
    QJsonObject currentState() const;
    bool validateInputs(QString* message = nullptr) const;
    void updatePreview();
    void clearPreview(bool renderNow = true);

private:
    Ui::ExtrudeDialog* ui = nullptr;

    QString m_profileUuid;
    QJsonObject m_committedState;
    QString m_missingProfileUuid;
    QList<ProfileInfo> m_profiles;
    QVector<CadFaceRef> m_selectedFaces;
    QTimer* m_previewTimer = nullptr;
};
