#pragma once

#include "sketch/SketchDocument.h"
#include "sketch/SketchShapeBuilder.h"
#include "sketch/SketchViewController.h"

#include <BasePage.h>

#include <QJsonObject>
#include <QList>
#include <memory>
#include <vector>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QEvent;
class QLabel;
class QGroupBox;
class QPushButton;
class QTableWidget;
class QWidget;

namespace Ui
{
class Sketch2DDialog;
}

enum class ProfileSegmentMode
{
    Line = 0,
    ArcThreePoint = 1,
    ArcCenterAngle = 2,
    ArcTangentEnd = 3
};

enum class StandaloneArcMode
{
    ThreePoint = 0,
    CenterAngle = 1
};

enum class SketchSnapKind
{
    None = 0,
    Point = 1,
    Grid = 2
};

class Sketch2DDialog : public BasePage
{
public:
    explicit Sketch2DDialog(QWidget* parent = nullptr);
    ~Sketch2DDialog() override;

    void beforeLoad() override;
    void beforeUnload() override;
    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;
    void setInitialCommand(GeometrySketch::Command command);
    void syncVisibilityFromTree(bool visible);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct SketchSnapshot
    {
        std::vector<GeometrySketch::SketchDocument> committed;
        GeometrySketch::SketchDocument active;
        std::vector<GeometrySketch::Point2D> referencePoints;
        int editingCommittedIndex = -1;
    };

    void initUi();
    void initEventBridge();
    void initViewCallbacks();

    GeometrySketch::PlaneSettings planeSettings() const;
    GeometrySketch::OutputMode outputMode() const;
    bool outputAsEntity() const;
    GeometrySketch::Command currentCommand() const;
    StandaloneArcMode standaloneArcMode() const;
    GeometrySketch::SketchDocument::ArcKind currentArcKind() const;
    void setCommand(GeometrySketch::Command command);
    void setStatus(const QString& text, bool ok = true);
    void updateInfoPanel();
    QString operationHintText() const;
    QString currentModeText() const;
    void refreshStatusLabel();
    void updateCursorInfo(double u, double v, SketchSnapKind snapKind, bool nearFirstPoint, bool orthoApplied);
    void resetCursorInfo();
    bool centerAngleControlPoints(GeometrySketch::Point2D& start, GeometrySketch::Point2D& center) const;
    void updateArcAngleDisplay(double sweep, bool active);
    bool previewCenterAngleSweep(double sweep);
    bool commitCenterAngleSweep(double sweep);
    void commitCenterAngleSweepFromInput();

    void enterSketchMode();
    void exitSketchMode();
    void hideCommittedPresentationForEditing();
    void updatePlaneAndView();
    void autoFitSketchView();

    void refreshPointTable();
    void refreshObjectTable();
    void updatePointTableTitle();
    int selectedCommittedIndex() const;
    QList<int> selectedCommittedIndices() const;
    void selectObjectRows(const QList<int>& rows, bool additive = false);
    void clearSketchSelection();
    QString documentTypeText(const GeometrySketch::SketchDocument& document) const;
    QString documentDetailText(const GeometrySketch::SketchDocument& document) const;
    QString documentStateText(const GeometrySketch::SketchDocument& document) const;
    bool documentEffectivelyClosed(const GeometrySketch::SketchDocument& document) const;
    int documentDisplayPointCount(const GeometrySketch::SketchDocument& document) const;
    int findNearestCommittedObject(double u, double v) const;
    bool findNearestEndpoint(double u, double v, int& objectIndex, int& pointIndex,
                             GeometrySketch::Point2D& point) const;
    bool documentSelectionBounds(const GeometrySketch::SketchDocument& document,
                                 double& minU, double& maxU, double& minV, double& maxV) const;
    double distanceToDocument(const GeometrySketch::SketchDocument& document, double u, double v) const;
    GeometrySketch::Command preferredCommandForDocument(const GeometrySketch::SketchDocument& document) const;
    void editSelectedObject();
    void deleteSelectedObject();
    void copySelectedObjects();
    void toggleSelectedConstruction();
    void syncDocumentFromTable();
    bool readPointRow(int row, double& u, double& v) const;
    void appendPointToTable(double u, double v, bool selectRow = true);

    double gridStep() const;
    double gridSnapStep() const;
    double precisionStep() const;
    double pointSnapRange() const;
    double autoCloseRange() const;
    bool orthoEnabled() const;
    bool applyOrthoFrom(const GeometrySketch::Point2D& base, double& u, double& v) const;
    bool snap(double& u, double& v, int* snappedIndex = nullptr, SketchSnapKind* snapKind = nullptr) const;
    bool isNearFirstPoint(double u, double v) const;
    bool tableShowsReferencePoints() const;
    bool hasActivePoints() const;
    bool hasAnySketchData() const;
    bool hasVisibleGeometryDocuments() const;
    double geometryTolerance() const;
    double pointSnapTolerance() const;
    double autoCloseTolerance() const;
    bool isDegenerateRectangle(const GeometrySketch::Point2D& a, const GeometrySketch::Point2D& b) const;
    bool isDegenerateRadius(double radius) const;
    bool isDegenerateArc(const GeometrySketch::Point2D& a, const GeometrySketch::Point2D& b,
                         const GeometrySketch::Point2D& c) const;
    bool isDegenerateCenterAngleArc(const GeometrySketch::Point2D& start,
                                    const GeometrySketch::Point2D& center,
                                    const GeometrySketch::Point2D& anglePoint,
                                    double arcSweep = 0.0) const;
    bool hasEditableActiveDocument() const;
    bool isEditingCommittedObject() const;
    const GeometrySketch::SketchDocument& documentForCommittedRow(int row) const;
    void selectObjectRow(int row);
    void cleanupGeneratedGeometry();
    void finishActiveDocumentBeforeNewObject();
    void pushUndo();
    void undoLast();
    void redoLast();
    void updateActionButtons();
    void clearPendingPoint();
    void resetCenterAngleTracking();
    double updateCenterAngleTracking(const GeometrySketch::Point2D& start,
                                     const GeometrySketch::Point2D& center,
                                     const GeometrySketch::Point2D& cursor);
    void finishCommand(const QString& message);
    std::vector<GeometrySketch::SketchDocument> visibleDocuments() const;
    std::vector<GeometrySketch::Point2D> visiblePoints() const;
    bool commitActiveDocument(const QString& message);
    void updateGeometryFromDocuments(const QString& message, bool showWarning);

    void handleSketchClick(double u, double v, bool rightButton);
    void handleSketchMove(double u, double v);
    bool beginControlPointDrag(double u, double v);
    void dragControlPointTo(double u, double v);
    void endControlPointDrag(double u, double v);
    void handleSketchBoxSelection(double u0, double v0, double u1, double v1,
                                  bool crossing, bool additive);
    void showSketchContextMenu();
    void setProfileSegmentMode(ProfileSegmentMode mode);
    void resetProfileSegmentState();
    bool appendSampledProfileSegment(const std::vector<GeometrySketch::Point2D>& sampled,
                                     const std::vector<GeometrySketch::Point2D>& displayPointsToAppend,
                                     const QString& message);
    bool appendTangentArcSegment(const GeometrySketch::Point2D& tangentPoint, const GeometrySketch::Point2D& end);
    bool endpointClosesActiveProfile(const GeometrySketch::Point2D& end) const;
    bool commitClosedActiveProfile();
    int findNearestPointInDocument(const GeometrySketch::SketchDocument& document, double u, double v) const;
    bool ensureCommittedObjectInEditMode(int row);
    void updatePointInActiveDocument(int pointIndex, double u, double v);
    void selectEndpoint(int objectIndex, int pointIndex, const GeometrySketch::Point2D& point);
    void clearEndpointSelection();
    void applySelectedPointCoordinates();
    bool updateCommittedPoint(int objectIndex, int pointIndex, double u, double v);
    void finalizeSketchEditing();
    void cancelCurrentCommand();
    void updateRubberLine(double u, double v);

    void updatePreview();
    void updateSelectionHighlight();
    void generateGeometry();
    void importDxfToSketch();

    void clearSketch();
    void removeSelectedPoint();
    void reverseProfile();
    void mergeProfileEndpoints();
    void checkProfileClosure();
    bool ensureProfileDocumentForEdit(const QString& actionName);
    void closeProfile();
    void finishOpenProfile();

private:
    bool m_isCurrentPage = false;
    bool m_loadingJson = false;
    bool m_visibilityOnEntry = false;
    bool m_editorPreviewVisible = true;
    bool m_hadRenderableDataOnEntry = false;
    bool m_visibilityExplicitlyChanged = false;
    QJsonObject m_stateOnEntry;
    bool m_hasPendingPoint = false;
    GeometrySketch::Command m_currentCommand = GeometrySketch::Command::Polyline;
    GeometrySketch::Point2D m_pendingPoint;
    ProfileSegmentMode m_profileSegmentMode = ProfileSegmentMode::Line;
    bool m_profileArcMidPending = false;
    GeometrySketch::Point2D m_profileArcMid;
    bool m_centerAngleTracking = false;
    GeometrySketch::Point2D m_centerAngleTrackStart;
    GeometrySketch::Point2D m_centerAngleTrackCenter;
    double m_centerAngleLastAngle = 0.0;
    double m_centerAngleSweep = 0.0;
    bool m_updatingArcAngleSpin = false;

    GeometrySketch::SketchDocument m_document;
    std::vector<GeometrySketch::SketchDocument> m_committedDocuments;
    std::vector<GeometrySketch::Point2D> m_referencePoints;
    int m_editingCommittedIndex = -1;
    GeometrySketch::SketchViewController m_view;

    Ui::Sketch2DDialog* ui = nullptr;
    QButtonGroup* m_toolGroup = nullptr;
    QString m_statusMessage;
    QString m_cursorStatusText;
    QString m_arcAngleStatusText;
    bool m_statusOk = true;
    int m_selectedPointObjectIndex = -1;
    int m_selectedPointIndex = -1;
    bool m_updatingPointCoordinates = false;
    bool m_draggingControlPoint = false;
    bool m_controlPointDragMoved = false;
    int m_draggedPointObjectIndex = -1;
    int m_draggedPointIndex = -1;

    std::vector<SketchSnapshot> m_undoStack;
    std::vector<SketchSnapshot> m_redoStack;
};
