#pragma once

#include <QByteArray>
#include <QComboBox>
#include <QSignalBlocker>
#include <QString>
#include <QVector>

#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkDataSet.h>
#include <vtkPointData.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>

#include "ResultDisplayNames.h"

namespace PostFieldUtils
{
constexpr int NameRole = Qt::UserRole;
constexpr int AssociationRole = Qt::UserRole + 1;

struct Field
{
    QString name;
    int association = vtkDataObject::FIELD_ASSOCIATION_POINTS;
};

inline QString associationLabel(int association)
{
    return association == vtkDataObject::FIELD_ASSOCIATION_CELLS ? QStringLiteral("单元")
                                                                  : QStringLiteral("节点");
}

inline QString displayName(const QString& name, int association)
{
    return QStringLiteral("%1 [%2]")
        .arg(ResultDisplayNames::displayName(name), associationLabel(association));
}

inline Field selectedField(const QComboBox* combo)
{
    Field field;
    if (!combo || combo->currentIndex() < 0)
    {
        field.name.clear();
        return field;
    }

    field.name = combo->currentData(NameRole).toString();
    if (field.name.isEmpty())
        field.name = combo->currentText();
    field.association = combo->currentData(AssociationRole).toInt();
    if (field.association != vtkDataObject::FIELD_ASSOCIATION_CELLS)
        field.association = vtkDataObject::FIELD_ASSOCIATION_POINTS;
    return field;
}

inline vtkDataArray* array(vtkDataSet* dataSet, const Field& field)
{
    if (!dataSet || field.name.isEmpty())
        return nullptr;

    const QByteArray encodedName = field.name.toUtf8();
    if (field.association == vtkDataObject::FIELD_ASSOCIATION_CELLS)
        return dataSet->GetCellData() ? dataSet->GetCellData()->GetArray(encodedName.constData())
                                      : nullptr;
    return dataSet->GetPointData() ? dataSet->GetPointData()->GetArray(encodedName.constData())
                                   : nullptr;
}



inline QString convertedCellArrayName(const Field& field)
{
    return QStringLiteral("__POST_CELL_TO_POINT__%1").arg(field.name);
}

inline vtkSmartPointer<vtkUnstructuredGrid>
prepareCellScalarForPointConversion(vtkUnstructuredGrid* grid, const Field& field,
                                    QString* convertedArrayName)
{
    if (!grid || field.association != vtkDataObject::FIELD_ASSOCIATION_CELLS ||
        field.name.isEmpty())
        return nullptr;

    vtkDataArray* source = array(grid, field);
    if (!source)
        return nullptr;

    const QString internalName = convertedCellArrayName(field);
    vtkSmartPointer<vtkDataArray> sourceCopy;
    sourceCopy.TakeReference(vtkDataArray::SafeDownCast(source->NewInstance()));
    if (!sourceCopy)
        return nullptr;
    sourceCopy->DeepCopy(source);
    sourceCopy->SetName(internalName.toUtf8().constData());

    auto prepared = vtkSmartPointer<vtkUnstructuredGrid>::New();
    prepared->ShallowCopy(grid);
    prepared->GetCellData()->RemoveArray(field.name.toUtf8().constData());
    prepared->GetCellData()->AddArray(sourceCopy);
    prepared->GetCellData()->SetActiveScalars(internalName.toUtf8().constData());
    if (convertedArrayName)
        *convertedArrayName = internalName;
    return prepared;
}

inline bool selectField(QComboBox* combo, const QString& name, int association)
{
    if (!combo || name.isEmpty())
        return false;
    for (int i = 0; i < combo->count(); ++i)
    {
        if (combo->itemData(i, NameRole).toString() == name &&
            combo->itemData(i, AssociationRole).toInt() == association)
        {
            combo->setCurrentIndex(i);
            return true;
        }
    }
    return false;
}

inline void fillCombo(QComboBox* combo, vtkDataSet* dataSet)
{
    if (!combo)
        return;

    const Field previous = selectedField(combo);
    const QSignalBlocker blocker(combo);
    combo->clear();
    if (!dataSet)
        return;

    QVector<Field> fields;
    if (auto* pointData = dataSet->GetPointData())
    {
        for (int i = 0; i < pointData->GetNumberOfArrays(); ++i)
        {
            vtkDataArray* dataArray = pointData->GetArray(i);
            const char* name = pointData->GetArrayName(i);
            if (dataArray && dataArray->GetNumberOfComponents() == 1 && name && *name &&
                !ResultDisplayNames::isInternalField(QString::fromUtf8(name)))
                fields.push_back({QString::fromUtf8(name),
                                  vtkDataObject::FIELD_ASSOCIATION_POINTS});
        }
    }
    if (auto* cellData = dataSet->GetCellData())
    {
        for (int i = 0; i < cellData->GetNumberOfArrays(); ++i)
        {
            vtkDataArray* dataArray = cellData->GetArray(i);
            const char* name = cellData->GetArrayName(i);
            if (dataArray && dataArray->GetNumberOfComponents() == 1 && name && *name &&
                !ResultDisplayNames::isInternalField(QString::fromUtf8(name)))
                fields.push_back({QString::fromUtf8(name),
                                  vtkDataObject::FIELD_ASSOCIATION_CELLS});
        }
    }

    std::sort(fields.begin(), fields.end(), [](const Field& lhs, const Field& rhs) {
        const int byName = QString::compare(lhs.name, rhs.name, Qt::CaseInsensitive);
        if (byName != 0)
            return byName < 0;
        return lhs.association < rhs.association;
    });

    for (const Field& field : fields)
    {
        combo->addItem(displayName(field.name, field.association));
        const int index = combo->count() - 1;
        combo->setItemData(index, field.name, NameRole);
        combo->setItemData(index, field.association, AssociationRole);
    }

    int restoreIndex = -1;
    for (int i = 0; i < combo->count(); ++i)
    {
        if (combo->itemData(i, NameRole).toString() == previous.name &&
            combo->itemData(i, AssociationRole).toInt() == previous.association)
        {
            restoreIndex = i;
            break;
        }
    }
    combo->setCurrentIndex(restoreIndex >= 0 ? restoreIndex : (combo->count() > 0 ? 0 : -1));
}
} // namespace PostFieldUtils
