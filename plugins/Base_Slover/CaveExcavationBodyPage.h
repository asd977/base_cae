#pragma once

#include <BasePage.h>
#include <PickManager.h>
#include <QString>
#include <vtkType.h>

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

    QVector<int> selectedElementIds();

  private slots:
    void onClearClicked();
    void onGroupChanged(int index);

  private:
    struct GroupOption
    {
        enum class Source
        {
            Physical = 0,
            Layer = 1
        };
        Source source = Source::Physical;
        int id = -1;
        QString label;
    };

    void setupBatchGroupCombo();
    QVector<vtkIdType> resolveCellIdsForIndex(int index) const;
    QString groupKeyForIndex(int index) const;
    bool setCurrentGroupByKey(const QString& key);
    static QString makeGroupKey(const GroupOption& option);
    static GroupOption parseGroupKey(const QString& key, bool* ok = nullptr);
    PickMode currentPickMode() const;

  private:
    QComboBox* m_groupCombo = nullptr;
    QPushButton* m_batchAddBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
};
