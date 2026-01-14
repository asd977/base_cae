#pragma once

#include <BasePage.h>
#include <QList>
#include <vtkType.h>

class QLabel;
class QTableWidget;
class QVBoxLayout;

class LoadSpecPageBase : public BasePage
{
public:
    LoadSpecPageBase(const QString &title, const QString &description, QWidget *parent = nullptr);

    QString title() const;
    QString description() const;

    void beforeLoad() override {}
    void beforeUnload() override {}

    virtual void setSelectedFaceIds(const QList<vtkIdType> &faceIds);

protected:
    QVBoxLayout *layout() const;
    QTableWidget *createTable(const QStringList &headers);

private:
    QString m_title;
    QString m_description;
    QLabel *m_label = nullptr;
    QVBoxLayout *m_layout = nullptr;
};
