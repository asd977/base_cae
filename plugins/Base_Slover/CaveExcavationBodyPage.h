#pragma once

#include <BasePage.h>
#include <PickManager.h>
#include <QSet>
#include <QString>
#include <vtkType.h>

class QComboBox;
class QPushButton;
class QListWidget;

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
    void onRemoveSelectedClicked();

  private:
    struct GroupOption
    {
        enum class Source
        {
            Physical = 0,
            Layer = 1,
            Matype = 2
        };
        Source source = Source::Physical;
        int id = -1;
        QString label;
    };

    void setupBatchGroupCombo();
    QVector<vtkIdType> resolveCellIdsForIndex(int index) const;
    QVector<vtkIdType> visibleFaceKeysForCellIds(const QVector<vtkIdType>& cellIds) const;
    QVector<int> toElementIds(const QVector<vtkIdType>& cellIds) const;
    void refreshElementListWidget();
    QString groupKeyForIndex(int index) const;
    bool setCurrentGroupByKey(const QString& key);
    static QString makeGroupKey(const GroupOption& option);
    static GroupOption parseGroupKey(const QString& key, bool* ok = nullptr);
    PickMode currentPickMode() const;

  private:
    QComboBox* m_groupCombo = nullptr;
    QPushButton* m_batchAddBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QListWidget* m_elemList = nullptr;
    QPushButton* m_removeSelectedBtn = nullptr;
    int m_lastHoveredIndex = -1;
    QSet<int> m_batchElementIds;
};
