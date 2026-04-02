#pragma once

#include <BasePage.h>

class QComboBox;
class QPushButton;

class CaveExcavationBodyPage : public BasePage
{
    Q_OBJECT
  public:
    explicit CaveExcavationBodyPage(QWidget* parent = nullptr);

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    QVector<int> selectedElementIds() const;

  private slots:
    void onClearClicked();
    void onGroupChanged(int index);

  private:
    void setupBatchGroupCombo();

  private:
    QComboBox* m_groupCombo = nullptr;
    QPushButton* m_batchAddBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
};
