#pragma once
#include <algorithm>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace SmeshFailureExplanation {
inline bool isViscousConstructionFailure(const QString& nativeError)
{
    const QString lower = nativeError.toLower();
    if (lower.contains(QStringLiteral("surface_element_limit:"))) return false;
    if (lower.contains(QStringLiteral("smesh_non_layered_domain_failure"))) return false;
    // An invalid Wall-to-Partition selection cannot be repaired by changing
    // the viscous extrusion method. Avoid repeating the same fatal precheck.
    if (lower.contains(QStringLiteral("bl_wall_selection"))) return false;
    if (lower.contains(QStringLiteral("bl_target_scope"))) return false;
    if (lower.contains(QStringLiteral("must be prepared in geometry first")) ||
        lower.contains(QStringLiteral("invalid viscouslayers")) ||
        lower.contains(QStringLiteral("not yet supported case")))
        return false;
    // The native layer shrinker can invoke NETGEN_2D to remesh its proxy face.
    // Its nested Surface meshing error must not mask the enclosing shrink error.
    if (lower.contains(QStringLiteral("can't shrink")) ||
        lower.contains(QStringLiteral("cannot shrink")))
        return true;
    if (lower.contains(QStringLiteral("ngexception at surface meshing")))
        return false;
    return lower.contains(QStringLiteral("viscous")) ||
           lower.contains(QStringLiteral("boundary mesh is overlapping"));
}

// Bridge validation failures are geometry/selection errors, NOT failed
// extrusion methods. Keep the native message in the log and show an actionable
// short Chinese explanation in the task UI.
inline QString wallSelectionFailure(const QString& nativeError)
{
    if(nativeError.contains(QStringLiteral("BL_TARGET_SCOPE")))
        return QStringLiteral("请先同时选择尚未生成的边界层目标域，或从总节点生成。边界层共享端面须在相邻域体填充前准备。");
    if (!nativeError.contains(QStringLiteral("BL_WALL_SELECTION"))) return {};
    const QString lower = nativeError.toLower();
    if (lower.contains(QStringLiteral("shared interface")) ||
        lower.contains(QStringLiteral("shared by solids")))
        return QStringLiteral("所选壁面是两个实体的共享面。请勾选“仅向指定域内加层”，"
                              "并只选择接收边界层的一侧实体（通常是流体域）。");
    if (lower.contains(QStringLiteral("stale")) ||
        lower.contains(QStringLiteral("no verified image")) ||
        lower.contains(QStringLiteral("cannot locate")))
        return QStringLiteral("所选壁面与当前几何不一致，请清空原有壁面后在图形区重新点选。");
    if (lower.contains(QStringLiteral("target")) ||
        lower.contains(QStringLiteral("scope")))
        return QStringLiteral("所选壁面不属于当前边界层目标域，请检查“仅向指定域内加层”的实体选择。");
    return QStringLiteral("壁面与 Partition 拓扑映射未通过校验，请检查 Wall 选择与当前几何。");
}

// A volume-fill failure with layers enabled is not proof of excessive layer
// thickness. NETGEN may receive an overlapping original skin or proxy skin.
inline QString boundaryLayerFailure(const QString& nativeError, double totalThickness,
                                    bool automaticMethods)
{
    const QString lower = nativeError.toLower();
    const bool overlapping = lower.contains(QStringLiteral("boundary mesh is overlapping"));
    const bool shrinking = lower.contains(QStringLiteral("can't shrink")) ||
                           lower.contains(QStringLiteral("cannot shrink"));
    const bool geomsearch = lower.contains(QStringLiteral("illegal position in geomsearch"));
    const bool bothStages = shrinking && geomsearch;
    QString reason;
    if (bothStages) {
        // Automatic mode may produce distinct failures in distinct algorithms.
        // Never present the Face Offset shrink failure as the only root cause
        // when NETGEN filling failed for two other methods.
        reason = QStringLiteral("不同挤出方法在不同阶段失败：部分方法无法调整加层面与不加层面的交界网格，"
            "其他方法在 NETGEN 三维填充时发生几何搜索异常。优先核对实际 Wall 面、内部共享面、"
            "失败实体的局部几何与表面网格；不能仅据此判断层厚过大。");
    } else if (overlapping) {
        reason = QStringLiteral("NETGEN 检测到表面网格重叠，需要区分原始表面与加层后的内表面；"
            "不能仅据此判定层厚过大。");
    } else if (shrinking) {
        reason = QStringLiteral("SMESH 无法调整加层面与不加层面的交界表面网格。"
            "请核对实际 Wall、拐角、间隙和表面网格；不能仅据此判定层厚过大。");
    } else if (geomsearch) {
        reason = QStringLiteral("NETGEN 三维填充时发生几何搜索异常。"
            "先核对实际加层面和失败实体的表面网格，再检查局部几何与层厚比例。");
    } else {
        reason = QStringLiteral("SMESH 边界层生成失败，具体原因见日志中的原生诊断。");
    }
    // The report contains SMESH Face shape IDs, their global owner counts,
    // and whether a viscous hypothesis was assigned to each face.
    const QString next = QStringLiteral("如日志包含 failed_solid_*.boundary_faces.csv，"
        "对照 CAD 面 Shape ID 检查 Wall/内部接口；某些异常编号可能属于临时面。无需重新 Partition。 ");
    return QStringLiteral("边界层生成失败。%1 当前总厚度为 %2（几何长度单位）。%3%4详细诊断已记录在日志中。")
        .arg(reason).arg(totalThickness, 0, 'g', 8)
        .arg(next)
        .arg(automaticMethods
            ? QStringLiteral("本次自动挤出方法均未成功。")
            : QString());
}

// Presentation only. Keep the native failure and never change mesh assignments.
inline QString describe(const QString& nativeError, const QJsonArray& domains = {})
{
    QStringList details;
    QSet<int> failed;
    const QRegularExpression index(QStringLiteral("original solid=(\\d+)"));
    auto matches = index.globalMatch(nativeError);
    while (matches.hasNext()) failed.insert(matches.next().captured(1).toInt());
    QStringList names;
    QList<int> ordered = failed.values();
    std::sort(ordered.begin(), ordered.end());
    for (int solid : ordered) {
        QString name; int count = 0;
        for (const auto& value : domains) {
            const auto domain = value.toObject();
            if (domain.value(QStringLiteral("brep_index")).toInt() != solid) continue;
            ++count; name = domain.value(QStringLiteral("name")).toString();
        }
        if (count == 1 && !name.isEmpty()) names.append(name);
    }
    if (!names.isEmpty()) details.append(QStringLiteral("失败域：%1。").arg(names.join(QStringLiteral("、"))));
    if (nativeError.contains(QStringLiteral("Native sweep cap topology mismatch")))
        details.append(QStringLiteral("所选起始面和目标面的 CAD 边数不同，不能直接一一对应扫掠。请明确改用四面体，或建立端面拓扑对应的分块；加密不能修复此处端面拓扑差异。"));
    if (nativeError.contains(QStringLiteral("native geometry applicability check rejected")))
        details.append(QStringLiteral("指定扫掠域未通过 SMESH 原生几何适用性检查。可明确改用四面体，或分块后再设置扫掠；本次未自动改变算法。"));
    if (nativeError.contains(QStringLiteral("Composite 'horizontal' edges are not supported")))
        details.append(QStringLiteral("原生扫掠算法不支持当前端面的复合边。请核对扫掠对象和端面；调整网格密度不能保证解决这类拓扑限制。"));
    if (nativeError.contains(QStringLiteral("Can't find regular quadrangle mesh on a side face")) ||
        nativeError.contains(QStringLiteral("More than 2 faces with not quadrangle elements")))
        details.append(QStringLiteral("扫掠侧面的网格不满足原生算法要求，需要核对源面、目标面及相邻网格任务。"));
    if (nativeError.contains(QStringLiteral("surface mesh not consistent")))
        details.append(QStringLiteral("体网格收到的表面网格不一致，需要检查共享面及边界离散。修改总体尺寸不能保证解决。"));
    if (nativeError.contains(QStringLiteral("boundary mesh is overlapping")))
        details.append(QStringLiteral("原生网格器检测到表面网格重叠，需要核对曲面精度和接口面。"));
    if (nativeError.contains(QStringLiteral("Illegal position in Geomsearch")))
        details.append(QStringLiteral("NETGEN 在体填充过程中触发几何搜索异常；该提示本身不能证明表面重叠或几何损坏。若体精细度为“很粗”，可明确改用原生默认的“标准”再验证。本次未自动修改任何参数。"));
    return details.join(QLatin1Char('\n'));
}
}
