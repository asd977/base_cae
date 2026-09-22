#pragma once

#include <BasePage.h>
#include <ImportGeometryBuilder.h>

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

namespace Ui
{
class GeometryImportWidget;
class StepImportPartDialog;
}

class GeometryImportWidget : public BasePage
{
    Q_OBJECT

public:
    explicit GeometryImportWidget(QWidget* parent = nullptr);
    ~GeometryImportWidget() override;

    void beforeLoad() override;
    void beforeUnload() override {}
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    QString getFilePath() const;
    void setFilePath(const QString& path);

private:
    StepImportOptions currentStepImportOptions() const;
    void applyStepImportOptions(const StepImportOptions& options);
    void updateStepOptionsVisibility();
    void updateImportStatus(const QString& text, bool error = false);

signals:
    void fileImported(const QString& filePath, const QString& uuid);

private slots:
    void onBrowseButtonClicked();
    void onImportButtonClicked();

private:
    Ui::GeometryImportWidget* ui = nullptr;
    QString m_lastImportedPath;
    StepImportOptions m_lastAppliedStepOptions;
    bool m_hasLoadedState = false;
};


class StepImportPartDialog : public BasePage
{
    Q_OBJECT

public:
    explicit StepImportPartDialog(QWidget* parent = nullptr);
    ~StepImportPartDialog() override;

    void beforeLoad() override;
    void beforeUnload() override {}
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    void setStepPart(const QString& importUuid,
                     int partIndex,
                     const QString& sourceKey,
                     const QString& originalName,
                     const QString& assemblyPath,
                     const QString& colorHex);

private:
    void refreshText();

    Ui::StepImportPartDialog* ui = nullptr;
    QString m_importUuid;
    int m_partIndex = -1;
    QString m_sourceKey;
    QString m_originalName;
    QString m_assemblyPath;
    QString m_colorHex;
};
