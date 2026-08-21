#ifndef MATERIALMANAGERSERVICE_H
#define MATERIALMANAGERSERVICE_H

#include <QString>
#include <QMap>
#include <QVector>
#include <QPair>
class BasePage;

struct MaterialManagerService
{
    virtual ~MaterialManagerService()
    {
    }

    virtual BasePage* getMaterialManagerWidget() = 0;

    virtual void newProject() = 0;
    virtual bool saveProject() = 0;
    virtual bool loadProject() = 0;
    virtual void addMaterialFromMenu(const QString& modelKey) = 0;
    virtual void showBuiltinMaterialListDialog() = 0;
    virtual void showBuiltinSeepageListDialog() = 0;
    virtual void showMaterialTransitionDialog() = 0;
    virtual QVector<QPair<int, int>> materialTransitions() const = 0;
    virtual void setMaterialTransitions(const QVector<QPair<int, int>>& transitions) = 0;
    // 工况级静力->动力材料关系只作为本次 IMAT.DAT 生成参数传入，
    // 不写回材料库，也不随工程保存。key=动力材料编号，value=静力材料编号。
    virtual bool generateIMATFile(const QMap<int, int>& damDynamicSourceMappings = {}) = 0;
    virtual bool hasConfiguredMaterials() const = 0;
    virtual QMap<int, QString> assignedMaterialLabels() const = 0;
    virtual QMap<int, QString> calculationMaterialLabels() const = 0;
    virtual QMap<int, QString> damStaticMaterialLabels() const = 0;
    virtual QMap<int, QString> damWetMaterialLabels(int sourceMatype) const = 0;
    virtual QMap<int, QString> damDynamicMaterialLabels() const = 0;
};

#endif // MATERIALMANAGERSERVICE_H
