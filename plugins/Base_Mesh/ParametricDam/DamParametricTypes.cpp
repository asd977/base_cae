#include "DamParametricTypes.h"

MsgJzDamParams MsgJzDamParams::makeDefault()
{
    MsgJzDamParams v;

    v.P(1) = "50.0"; // ALFA1
    v.P(2) = "30.0"; // ALFA2
    v.P(3) = "50.0"; // B1
    v.P(4) = "100.0"; // HBOT
    v.P(5) = "185.0"; // HTOP
    v.P(6) = "16.0"; // W1
    v.P(7) = "35.0"; // BETA1
    v.P(8) = "40.0"; // BETA2
    v.P(9) = "10"; // NLAYER
    v.P(10) = "100.0"; // B2
    v.P(11) = "120.0"; // B3
    v.P(12) = "150.0"; // W2
    v.P(13) = "180.0"; // W3
    v.P(14) = "0.0"; // HSIZE
    v.P(15) = "1.2"; // AMX
    v.P(16) = "1.2"; // AMZ
    v.P(17) = "2"; // NS_MB
    v.P(18) = "3"; // NS_GD
    v.P(19) = "0.0"; // WM
    v.P(20) = "0.0"; // BM
    v.P(21) = "85.0"; // HEIT
    v.P(22) = "0.0"; // HPACK
    v.P(23) = "10"; // HM

    v.P(24) = "坝顶上游水平宽度";
    v.P(25) = "0.30"; // W4
    v.P(26) = "坝底上游水平宽度";
    v.P(27) = "0.40"; // W5
    v.P(28) = "过滤层水平宽度";
    v.P(29) = "0.50"; // T1
    v.P(30) = "过渡层水平宽度";
    v.P(31) = "2.0"; // T2
    v.P(32) = "均质坝";
    v.P(33) = "趾板尺寸";
    v.P(34) = "0.4 2.0 2.5";
    v.P(35) = "3"; // NDIM_MODEL

    return v;
}

QString damObjectName(int iobject)
{
    switch (iobject)
    {
    case 8:
        return "均质坝";
    case 4:
        return "黏土心墙坝";
    case 3:
        return "面板坝";
    case 5:
        return "沥青心墙坝";
    case 2:
        return "重力坝";
    default:
        return QString("未知坝型(%1)").arg(iobject);
    }
}

QString damDialogTitle(int iobject, int ndimModel)
{
    return QString("%1D%2参数化建模").arg(ndimModel).arg(damObjectName(iobject));
}

void applyDamObjectLabels(int iobject, MsgJzDamParams& p)
{
    // 复刻 JZDAM_SUB 中根据 IOBJECT 修改 P024/P026/P028/P030/P032/P033 的逻辑
    if (iobject == 8)
    {
        p.P(24) = "无";
        p.P(26) = "无";
        p.P(28) = "无";
        p.P(30) = "无";
        p.P(32) = "均质坝";
    }
    else if (iobject == 2)
    {
        p.P(24) = "无";
        p.P(26) = "无";
        p.P(28) = "上游直立段高度";
        p.P(30) = "下游直立段高度";
        p.P(32) = "重力坝";
    }
    else if (iobject == 3)
    {
        p.P(24) = "坝顶面板水平宽度";
        p.P(26) = "坝底面板水平宽度";
        p.P(28) = "过滤层水平宽度";
        p.P(30) = "趾板/面板宽度 Wf";
        p.P(32) = "面板坝";
        p.P(33) = "趾板 T2、Bz1、Bz2";
    }
    else if (iobject == 4)
    {
        p.P(24) = "坝顶心墙厚度";
        p.P(26) = "坝底心墙厚度";
        p.P(28) = "过滤层水平宽度";
        p.P(30) = "过渡层水平宽度";
        p.P(32) = "黏土心墙坝";
    }
    else if (iobject == 5)
    {
        p.P(24) = "坝顶斜墙厚度";
        p.P(26) = "坝底斜墙轴线至上游边界距离";
        p.P(28) = "上游过渡层宽度";
        p.P(30) = "下游过渡层宽度";
        p.P(32) = "沥青心墙坝";
    }
}
