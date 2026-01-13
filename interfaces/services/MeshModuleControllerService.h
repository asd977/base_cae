#ifndef MESH_MODULE_CONTROLLER_SERVICE_H
#define MESH_MODULE_CONTROLLER_SERVICE_H

#include <QSet>
#include <QVector>
#include <QString>

class IiinGridModel;

struct MeshModuleControllerService
{
    virtual ~MeshModuleControllerService() {}
	virtual void newProject() = 0;
	virtual bool saveProject() = 0;
	virtual bool loadProject() = 0;
	virtual void closeProject() = 0;
    virtual bool currentModel(IiinGridModel &model) const = 0;
    virtual QString currentModelFileName() const = 0;
    virtual QSet<int> getAvailableMatypes() const = 0;
    virtual void highlightMatypes(const QSet<int> &matypes,
                                  int selectedMatype,
                                  int previewMatype,
                                  int preDeleteMatype) = 0;
    virtual bool ensureConstraintModel(QString *errMsg = nullptr) = 0;
    virtual bool loadConstraintFile(const QString &fileName, QString *errMsg = nullptr) = 0;
    virtual bool saveConstraintFile(const QString &fileName, QString *errMsg = nullptr) const = 0;
    virtual bool generateDefaultConstraintFile(const QString &fileName, QString *errMsg = nullptr) = 0;
    virtual bool setNodeConstraint(int nodeId,
                                   const QVector<int> &dofs,
                                   QString *errMsg = nullptr) = 0;
    virtual bool setConstraintsForMatype(int matype, const QVector<int> &dofs) = 0;
    virtual bool setConstraintsForPlane(double a,
                                        double b,
                                        double c,
                                        double d,
                                        double tolerance,
                                        const QVector<int> &dofs) = 0;
    virtual bool currentConstraintState(QVector<QVector<int>> &constraints,
                                        int &madof) const = 0;
    virtual void setConstraintVisibility(bool showUx, bool showUy, bool showUz) = 0;
};

#endif // MESH_MODULE_CONTROLLER_SERVICE_H
