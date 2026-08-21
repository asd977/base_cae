#ifndef PHYSICALFIELDSERVICE_H
#define PHYSICALFIELDSERVICE_H

#include <QString>

class BasePage;

struct PhysicalFieldService
{
    virtual ~PhysicalFieldService() {};
    virtual BasePage* getPhysicalFiledSettingWidget() = 0;
    virtual void newProject() = 0;
    virtual bool saveProject() = 0;
    virtual bool loadProject() = 0;
    virtual void generateIIIDFile() = 0;
    virtual void generateIIWLFile() = 0;
    virtual bool importIIIDFile(const QString& filePath, QString* errorMessage = nullptr) = 0;

    // “约束与荷载 -> 地震加速度”是全工程共享的地震输入。
    // 动力工况只决定是否使用，不在求解模块重复维护第二份表格。
    virtual bool validateSeismicAcceleration(QString* errorMessage = nullptr) const = 0;

    // 定位到“约束与荷载 -> 地震加速度”。若尚未创建，则自动创建后打开。
    virtual bool openSeismicAccelerationPage(QString* errorMessage = nullptr) = 0;

    virtual QString pageTitleByUuid(const QString& uuid) const = 0;
    virtual void closeProject() = 0;
};
#endif // PHYSICALFIELDSERVICE_H
