#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

#include "BasePage.h" // 保持你现有继承关系

namespace Ui
{
class OrthogonalSampling;
}

class OrthogonalSampling : public BasePage
{
    Q_OBJECT
  public:
    explicit OrthogonalSampling(QWidget* parent = nullptr);
    ~OrthogonalSampling() override;

  private:
    void wireSignals();
    int addParamRow(const QString& name = QString(), const QString& values = QString(),
                    bool numeric = true);
    void removeSelectedRows();
    void beforeLoad() override;
    void beforeUnload() override;
    QStringList splitValues(const QString& text) const;
    QJsonObject toJsonParams() const;
    void runPythonOrth_File();

    void loadOrthOutputsFromDir(const QString& dir);
    void loadSampleTable(const QString& path);
    void loadStatText(const QString& path);
    void loadImageIfExists(const QString& path);

  protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

  private:
    Ui::OrthogonalSampling* ui = nullptr;
};
