#include "DamParametricCalculator.h"

#include <QtMath>

bool DamParametricCalculator::readDouble(const QString& text, double& value, const QString& name,
                                         QString* error)
{
    bool ok = false;
    value = text.trimmed().toDouble(&ok);

    if (!ok)
    {
        if (error)
            *error = QString("%1 不是合法数字：%2").arg(name, text);
        return false;
    }

    return true;
}

bool DamParametricCalculator::readInt(const QString& text, int& value, const QString& name,
                                      QString* error)
{
    bool ok = false;
    value = text.trimmed().toInt(&ok);

    if (!ok)
    {
        if (error)
            *error = QString("%1 不是合法整数：%2").arg(name, text);
        return false;
    }

    return true;
}

QString DamParametricCalculator::f4(double value)
{
    return QString::number(value, 'f', 4);
}

bool DamParametricCalculator::calculate(int iobject, MsgJzDamParams& p, QString* error)
{
    double B1 = 0.0;
    double HBOT = 0.0;
    double HTOP = 0.0;
    double W1 = 0.0;
    int NLAYER = 0;
    double B2 = 0.0;
    double B3 = 0.0;
    double W2 = 0.0;
    double W3 = 0.0;
    double AMX = 0.0;
    double AMZ = 0.0;
    double W4 = 0.0;
    double W5 = 0.0;
    double T1 = 0.0;
    double T2 = 0.0;

    if (!readDouble(p.P(3), B1, "B1", error))
        return false;
    if (!readDouble(p.P(4), HBOT, "HBOT", error))
        return false;
    if (!readDouble(p.P(5), HTOP, "HTOP", error))
        return false;
    if (!readDouble(p.P(6), W1, "W1", error))
        return false;
    if (!readInt(p.P(9), NLAYER, "NLAYER", error))
        return false;
    if (!readDouble(p.P(10), B2, "B2", error))
        return false;
    if (!readDouble(p.P(11), B3, "B3", error))
        return false;
    if (!readDouble(p.P(12), W2, "W2", error))
        return false;
    if (!readDouble(p.P(13), W3, "W3", error))
        return false;
    if (!readDouble(p.P(15), AMX, "AMX", error))
        return false;
    if (!readDouble(p.P(16), AMZ, "AMZ", error))
        return false;
    if (!readDouble(p.P(25), W4, "W4", error))
        return false;
    if (!readDouble(p.P(27), W5, "W5", error))
        return false;
    if (!readDouble(p.P(29), T1, "T1", error))
        return false;
    if (!readDouble(p.P(31), T2, "T2", error))
        return false;

    double WF = 0.0;
    double BZ1 = 0.0;
    double BZ2 = 0.0;

    if (iobject == 3)
    {
        WF = T2;

        const QStringList values = p.P(34).simplified().split(' ', Qt::SkipEmptyParts);
        if (values.size() < 3)
        {
            if (error)
                *error = "面板坝 P034 需要包含 3 个数字：T1 BZ1 BZ2，例如：0.4 2.0 2.5";
            return false;
        }

        bool ok1 = false;
        bool ok2 = false;
        bool ok3 = false;

        T1 = values[0].toDouble(&ok1);
        BZ1 = values[1].toDouble(&ok2);
        BZ2 = values[2].toDouble(&ok3);

        Q_UNUSED(WF);
        Q_UNUSED(BZ2);

        if (!ok1 || !ok2 || !ok3)
        {
            if (error)
                *error = "面板坝 P034 解析失败，应为 3 个数字：T1 BZ1 BZ2";
            return false;
        }
    }

    if (NLAYER <= 0)
    {
        if (error)
            *error = "NLAYER 必须大于 0";
        return false;
    }

    if (HTOP <= HBOT)
    {
        if (error)
            *error = "坝顶高程 HTOP 必须大于坝底高程 HBOT";
        return false;
    }

    if (qFuzzyIsNull(B2) || qFuzzyIsNull(B3))
    {
        if (error)
            *error = "B2/B3 不能为 0，否则无法计算 ALFA1/ALFA2";
        return false;
    }

    const double pi = 3.14159265358979323846;
    const double HEIT = HTOP - HBOT;

    const double ALFA1 = qAtan(HEIT / B3) * 180.0 / pi;
    const double ALFA2 = qAtan(HEIT / B2) * 180.0 / pi;

    double BETA1 = 0.0;
    double BETA2 = 0.0;

    if (iobject == 3)
    {
        const double den1 = W2 - BZ1 - 0.5 * W1;
        const double den2 = W3 - 0.5 * W1;

        if (qFuzzyIsNull(den1) || qFuzzyIsNull(den2))
        {
            if (error)
                *error = "面板坝 BETA 计算分母为 0，请检查 W1/W2/W3/BZ1";
            return false;
        }

        BETA1 = qAtan(HEIT / den1) * 180.0 / pi;
        BETA2 = qAtan(HEIT / den2) * 180.0 / pi;
    }
    else if (iobject == 4 || iobject == 5 || iobject == 8)
    {
        const double den1 = W2 - 0.5 * W1;
        const double den2 = W3 - 0.5 * W1;

        if (qFuzzyIsNull(den1) || qFuzzyIsNull(den2))
        {
            if (error)
                *error = "BETA 计算分母为 0，请检查 W1/W2/W3";
            return false;
        }

        BETA1 = qAtan(HEIT / den1) * 180.0 / pi;
        BETA2 = qAtan(HEIT / den2) * 180.0 / pi;
    }
    else if (iobject == 2)
    {
        BETA1 = 90.0;

        if (W2 > 0.0)
        {
            if (qFuzzyIsNull(W3))
            {
                if (error)
                    *error = "重力坝 W3 不能为 0";
                return false;
            }

            BETA1 = qAtan((HEIT - T1) / W3) * 180.0 / pi;
        }

        if (qFuzzyIsNull(W2))
        {
            if (error)
                *error = "重力坝 W2 不能为 0";
            return false;
        }

        BETA2 = qAtan((HEIT - T2) / W2) * 180.0 / pi;
    }
    else
    {
        if (error)
            *error = QString("不支持的 IOBJECT：%1").arg(iobject);
        return false;
    }

    const double WM = static_cast<int>(2.0 * AMX * (W1 + W2 + W3)) * 1.0;
    const double BM = static_cast<int>(2.0 * AMZ * (B1 + B2 + B3)) * 1.0;
    const double HM = static_cast<int>(HEIT + (HTOP - HBOT) * 0.8) * 1.0;
    const double HPACK = HEIT / static_cast<double>(NLAYER);

    p.P(1) = f4(ALFA1);
    p.P(2) = f4(ALFA2);
    p.P(7) = f4(BETA1);
    p.P(8) = f4(BETA2);
    p.P(19) = f4(WM);
    p.P(20) = f4(BM);
    p.P(21) = f4(HEIT);
    p.P(22) = f4(HPACK);
    p.P(23) = f4(HM);

    if (W1 > W2 + W3)
    {
        if (error)
            *error = "参数错误：坝底顺河向长度小于坝顶宽度，即 W1 > W2 + W3";
        return false;
    }

    if (WM <= W2 + W3)
    {
        if (error)
            *error = "参数错误：模型顺河向总长度过小，即 WM <= W2 + W3";
        return false;
    }

    if (BM <= B1 + B2 + B3)
    {
        if (error)
            *error = "参数错误：模型横河向总长度过小，即 BM <= B1 + B2 + B3";
        return false;
    }

    if (iobject == 3)
    {
        if (W4 > W5)
        {
            if (error)
                *error = "面板坝参数错误：坝底面板宽度小于坝顶面板宽度，即 W4 > W5";
            return false;
        }

        if (W4 + T1 >= 0.5 * W1)
        {
            if (error)
                *error = "面板坝参数错误：面板、垫层总厚度超过坝顶宽度一半，即 W4 + T1 >= 0.5 * W1";
            return false;
        }

        if (W5 + T1 >= W2)
        {
            if (error)
                *error = "面板坝参数错误：面板、垫层、过渡层总厚度超过坝底宽度，即 W5 + T1 >= W2";
            return false;
        }
    }

    if (iobject == 4)
    {
        if (0.5 * W4 + T1 + T2 >= 0.5 * W1)
        {
            if (error)
                *error = "黏土心墙坝参数错误：心墙半厚度 + 过滤层 + 过渡层超过坝顶宽度一半";
            return false;
        }

        if (0.5 * W5 + T1 + T2 >= W2)
        {
            if (error)
                *error = "黏土心墙坝参数错误：心墙半厚度 + 过滤层 + 过渡层超过上游坝坡宽度";
            return false;
        }

        if (0.5 * W5 + T1 + T2 >= W3)
        {
            if (error)
                *error = "黏土心墙坝参数错误：心墙半厚度 + 过滤层 + 过渡层超过下游坝坡宽度";
            return false;
        }
    }

    if (iobject == 5)
    {
        if (W4 + W5 + T1 > 0.5 * W1)
        {
            if (error)
                *error = "沥青心墙坝参数错误：斜墙及过渡层总宽度超过坝顶宽度一半";
            return false;
        }

        if (T2 <= W5)
        {
            if (error)
                *error = "沥青心墙坝参数错误：下游过渡层宽度过小，即 T2 <= W5";
            return false;
        }

        if (T2 - W5 > 0.5 * W1)
        {
            if (error)
                *error = "沥青心墙坝参数错误：下游过渡层宽度过大，即 T2 - W5 > 0.5 * W1";
            return false;
        }
    }

    if (iobject == 2)
    {
        if (T1 >= T2 && T1 > 0.0 && T2 > 0.0)
        {
            if (error)
                *error = "重力坝参数错误：上下游直立段高度和转折点高度不协调，即 T1 >= T2";
            return false;
        }
    }

    return true;
}
