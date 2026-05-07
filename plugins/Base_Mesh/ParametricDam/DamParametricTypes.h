#pragma once

#include <QString>
#include <QStringList>

enum class DamObjectType
{
    GravityDam = 2, // 重力坝
    FaceSlabDam = 3, // 面板坝
    ClayCoreDam = 4, // 黏土心墙坝
    AsphaltCoreDam = 5, // 沥青心墙坝
    HomogeneousDam = 8 // 均质坝
};

struct DamParametricContext
{
    QString projectDir; // 对应 Fortran 的 DIR
    QString solverRoot; // 对应 Fortran 的 DIRSET

    int iobject = 8;
    int ndimModel = 3;

    bool useDebugExe = true;
};

struct MsgJzDamParams
{
    // 使用 1-based，保持和 Fortran P001 ~ P035 一致
    QString p[36];

    QString& P(int index)
    {
        return p[index];
    }

    const QString& P(int index) const
    {
        return p[index];
    }

    static MsgJzDamParams makeDefault();
};

QString damObjectName(int iobject);
QString damDialogTitle(int iobject, int ndimModel);
void applyDamObjectLabels(int iobject, MsgJzDamParams& params);
