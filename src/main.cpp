#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFileDialog>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEngine>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include <AIS_InteractiveContext.hxx>
#include <AIS_InteractiveObject.hxx>
#include <AIS_Shape.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Bnd_Box.hxx>
#include <BRep_Builder.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepTools.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IGESControl_Reader.hxx>
#include <IGESControl_Writer.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Quantity_Color.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_StepModelType.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <StlAPI_Reader.hxx>
#include <StlAPI_Writer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <V3d_TypeOfOrientation.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <WNT_Window.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include "document/ModelTypes.h"

static QString kindName(ObjectKind kind)
{
    switch (kind) {
    case ObjectKind::Solid:
        return QString::fromUtf8("實體");
    case ObjectKind::Sketch:
        return QString::fromUtf8("草圖");
    case ObjectKind::Imported:
        return QString::fromUtf8("匯入模型");
    }
    return QString();
}

static QString datumPlaneName(DatumPlane plane)
{
    switch (plane) {
    case DatumPlane::XY:
        return "XY Plane";
    case DatumPlane::YZ:
        return "YZ Plane";
    case DatumPlane::ZX:
        return "ZX Plane";
    case DatumPlane::None:
        break;
    }
    return QString();
}

class CadView final : public QWidget
{
public:
    explicit CadView(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_PaintOnScreen);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_NativeWindow);
        setAutoFillBackground(false);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        initializeViewer();
    }

    ~CadView() override
    {
        if (!context_.IsNull()) {
            context_->RemoveAll(false);
            context_.Nullify();
        }
        if (!view_.IsNull()) {
            view_->Remove();
            view_.Nullify();
        }
        viewer_.Nullify();
        driver_.Nullify();
    }

    void setSelectionCallback(
        std::function<void(const Handle(AIS_InteractiveObject)&)> callback)
    {
        selectionCallback_ = std::move(callback);
    }

    void setSketchCommittedCallback(
        std::function<void(const TopoDS_Shape&, const QString&)> callback)
    {
        sketchCommittedCallback_ = std::move(callback);
    }

    void setCancelCallback(std::function<void()> callback)
    {
        cancelCallback_ = std::move(callback);
    }

    void beginSketchTool(SketchTool tool, DatumPlane plane)
    {
        clearPreview();

        if (!gridPresentation_.IsNull()) {
            context_->Remove(gridPresentation_, false);
            gridPresentation_.Nullify();
        }

        currentSketchPlane_ = plane;
        sketchTool_ = tool;
        hasFirstSketchPoint_ = false;
        setSketchGridVisible(true);

        if (plane == DatumPlane::YZ) {
            viewRight();
        } else if (plane == DatumPlane::ZX) {
            viewFront();
        } else {
            viewTop();
        }

        setFocus(Qt::OtherFocusReason);
    }

    void cancelSketchInteraction()
    {
        clearPreview();
        sketchTool_ = SketchTool::None;
        hasFirstSketchPoint_ = false;
        setSketchGridVisible(false);
        update();
    }

    void showPreviewShape(const TopoDS_Shape& shape)
    {
        clearPreview();
        if (shape.IsNull()) {
            return;
        }

        previewPresentation_ = new AIS_Shape(shape);
        previewPresentation_->SetColor(
            Quantity_Color(0.15, 0.65, 1.0, Quantity_TOC_RGB));
        previewPresentation_->SetTransparency(0.55f);
        context_->Display(previewPresentation_, false);
        context_->SetDisplayMode(previewPresentation_, AIS_Shaded, false);
        context_->UpdateCurrentViewer();
    }

    void clearPreview()
    {
        if (!previewPresentation_.IsNull()) {
            context_->Remove(previewPresentation_, false);
            previewPresentation_.Nullify();
            context_->UpdateCurrentViewer();
        }
    }

    void selectPresentations(
        const std::vector<Handle(AIS_Shape)>& presentations)
    {
        context_->ClearSelected(false);
        for (const auto& presentation : presentations) {
            if (!presentation.IsNull()) {
                context_->AddOrRemoveSelected(presentation, false);
            }
        }
        context_->UpdateCurrentViewer();
    }

    Handle(AIS_Shape) displayShape(
        const TopoDS_Shape& shape,
        ObjectKind kind,
        bool visible = true)
    {
        Handle(AIS_Shape) presentation = new AIS_Shape(shape);
        context_->Display(presentation, false);

        if (kind == ObjectKind::Sketch) {
            context_->SetDisplayMode(presentation, AIS_WireFrame, false);
            presentation->SetColor(Quantity_Color(0.95, 0.72, 0.15, Quantity_TOC_RGB));
        } else {
            context_->SetDisplayMode(presentation, AIS_Shaded, false);
        }

        if (!visible) {
            context_->Erase(presentation, false);
        }

        context_->UpdateCurrentViewer();
        return presentation;
    }

    void removeShape(const Handle(AIS_Shape)& presentation)
    {
        if (!presentation.IsNull()) {
            context_->Remove(presentation, true);
        }
    }

    void setVisible(const Handle(AIS_Shape)& presentation, bool visible)
    {
        if (presentation.IsNull()) {
            return;
        }
        if (visible) {
            context_->Display(presentation, true);
        } else {
            context_->Erase(presentation, true);
        }
    }

    void setAllDisplayMode(
        const std::vector<ModelObject>& objects,
        bool wireframe)
    {
        for (const auto& obj : objects) {
            if (obj.presentation.IsNull()) {
                continue;
            }
            if (obj.kind == ObjectKind::Sketch || wireframe) {
                context_->SetDisplayMode(obj.presentation, AIS_WireFrame, false);
            } else {
                context_->SetDisplayMode(obj.presentation, AIS_Shaded, false);
            }
        }
        context_->UpdateCurrentViewer();
    }

    void clearScene()
    {
        context_->RemoveAll(true);
        update();
    }

    void fitAll()
    {
        if (!view_.IsNull()) {
            view_->FitAll(0.01, false);
            view_->ZFitAll();
            update();
        }
    }

    void viewAxo()
    {
        view_->SetProj(V3d_XposYnegZpos);
        fitAll();
    }

    void viewTop()
    {
        view_->SetProj(V3d_Zpos);
        fitAll();
    }

    void viewBottom()
    {
        view_->SetProj(V3d_Zneg);
        fitAll();
    }

    void viewFront()
    {
        view_->SetProj(V3d_Yneg);
        fitAll();
    }

    void viewBack()
    {
        view_->SetProj(V3d_Ypos);
        fitAll();
    }

    void viewRight()
    {
        view_->SetProj(V3d_Xpos);
        fitAll();
    }

    void viewLeft()
    {
        view_->SetProj(V3d_Xneg);
        fitAll();
    }

protected:
    QPaintEngine* paintEngine() const override
    {
        return nullptr;
    }

    void paintEvent(QPaintEvent*) override
    {
        if (!view_.IsNull()) {
            view_->Redraw();
        }
    }

    void resizeEvent(QResizeEvent*) override
    {
        if (!view_.IsNull()) {
            view_->MustBeResized();
            update();
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        setFocus(Qt::MouseFocusReason);
        lastMousePos_ = toOcctPoint(event->position());

        if (event->button() == Qt::LeftButton &&
            sketchTool_ != SketchTool::None) {
            gp_Pnt point;
            if (screenToSketchPlane(lastMousePos_, point)) {
                handleSketchClick(point);
            }
            event->accept();
            return;
        }

        if (event->button() == Qt::MiddleButton) {
            if (event->modifiers().testFlag(Qt::ShiftModifier)) {
                panning_ = true;
            } else {
                rotating_ = true;
                view_->StartRotation(lastMousePos_.x(), lastMousePos_.y());
            }
        } else if (event->button() == Qt::RightButton) {
            rotating_ = true;
            view_->StartRotation(lastMousePos_.x(), lastMousePos_.y());
        } else if (event->button() == Qt::LeftButton) {
            context_->MoveTo(lastMousePos_.x(), lastMousePos_.y(), view_, true);
            context_->SelectDetected();

            if (selectionCallback_) {
                Handle(AIS_InteractiveObject) selected;
                context_->InitSelected();
                if (context_->MoreSelected()) {
                    selected = context_->SelectedInteractive();
                }
                selectionCallback_(selected);
            }
        }

        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::MiddleButton ||
            event->button() == Qt::RightButton) {
            rotating_ = false;
            panning_ = false;
        }
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const QPoint current = toOcctPoint(event->position());

        if (rotating_) {
            view_->Rotation(current.x(), current.y());
        } else if (panning_) {
            const QPoint delta = current - lastMousePos_;
            view_->Pan(delta.x(), -delta.y());
        } else if (sketchTool_ != SketchTool::None &&
                   hasFirstSketchPoint_) {
            gp_Pnt point;
            if (screenToSketchPlane(current, point)) {
                updateSketchPreview(point);
            }
        } else {
            context_->MoveTo(current.x(), current.y(), view_, false);
        }

        lastMousePos_ = current;
        update();
        event->accept();
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Escape) {
            cancelSketchInteraction();
            if (cancelCallback_) {
                cancelCallback_();
            }
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void wheelEvent(QWheelEvent* event) override
    {
        const double factor =
            event->angleDelta().y() > 0 ? 1.15 : (1.0 / 1.15);
        view_->SetZoom(factor, true);
        update();
        event->accept();
    }

private:
    QPoint toOcctPoint(const QPointF& position) const
    {
        const qreal scale = devicePixelRatioF();
        return QPoint(
            qRound(position.x() * scale),
            qRound(position.y() * scale));
    }

    bool screenToSketchPlane(const QPoint& pixel, gp_Pnt& point) const
    {
        Standard_Real x = 0.0;
        Standard_Real y = 0.0;
        Standard_Real z = 0.0;
        Standard_Real vx = 0.0;
        Standard_Real vy = 0.0;
        Standard_Real vz = 0.0;

        view_->ConvertWithProj(
            pixel.x(), pixel.y(),
            x, y, z,
            vx, vy, vz);

        Standard_Real originCoord = z;
        Standard_Real directionCoord = vz;

        if (currentSketchPlane_ == DatumPlane::YZ) {
            originCoord = x;
            directionCoord = vx;
        } else if (currentSketchPlane_ == DatumPlane::ZX) {
            originCoord = y;
            directionCoord = vy;
        }

        if (std::abs(directionCoord) < 1.0e-12) {
            return false;
        }

        const Standard_Real t = -originCoord / directionCoord;
        point = gp_Pnt(
            x + t * vx,
            y + t * vy,
            z + t * vz);
        return true;
    }

    QPointF toSketchUV(const gp_Pnt& point) const
    {
        if (currentSketchPlane_ == DatumPlane::YZ) {
            return QPointF(point.Y(), point.Z());
        }
        if (currentSketchPlane_ == DatumPlane::ZX) {
            return QPointF(point.X(), point.Z());
        }
        return QPointF(point.X(), point.Y());
    }

    gp_Pnt fromSketchUV(double u, double v) const
    {
        if (currentSketchPlane_ == DatumPlane::YZ) {
            return gp_Pnt(0.0, u, v);
        }
        if (currentSketchPlane_ == DatumPlane::ZX) {
            return gp_Pnt(u, 0.0, v);
        }
        return gp_Pnt(u, v, 0.0);
    }

    gp_Dir sketchNormal() const
    {
        if (currentSketchPlane_ == DatumPlane::YZ) {
            return gp_Dir(1.0, 0.0, 0.0);
        }
        if (currentSketchPlane_ == DatumPlane::ZX) {
            return gp_Dir(0.0, 1.0, 0.0);
        }
        return gp_Dir(0.0, 0.0, 1.0);
    }

    TopoDS_Shape makeSketchShape(
        const gp_Pnt& first,
        const gp_Pnt& second,
        bool closedFace) const
    {
        if (sketchTool_ == SketchTool::Line) {
            return BRepBuilderAPI_MakeEdge(first, second).Shape();
        }

        const QPointF firstUV = toSketchUV(first);
        const QPointF secondUV = toSketchUV(second);

        if (sketchTool_ == SketchTool::Rectangle) {
            BRepBuilderAPI_MakePolygon polygon;
            polygon.Add(fromSketchUV(firstUV.x(), firstUV.y()));
            polygon.Add(fromSketchUV(secondUV.x(), firstUV.y()));
            polygon.Add(fromSketchUV(secondUV.x(), secondUV.y()));
            polygon.Add(fromSketchUV(firstUV.x(), secondUV.y()));
            polygon.Close();

            if (closedFace) {
                return BRepBuilderAPI_MakeFace(polygon.Wire()).Shape();
            }
            return polygon.Wire();
        }

        if (sketchTool_ == SketchTool::Circle) {
            const double du = secondUV.x() - firstUV.x();
            const double dv = secondUV.y() - firstUV.y();
            const double radius = std::sqrt(du * du + dv * dv);
            if (radius < 1.0e-6) {
                return TopoDS_Shape();
            }

            gp_Circ circle(
                gp_Ax2(first, sketchNormal()),
                radius);
            TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(circle).Edge();
            if (!closedFace) {
                return edge;
            }
            TopoDS_Wire wire = BRepBuilderAPI_MakeWire(edge).Wire();
            return BRepBuilderAPI_MakeFace(wire).Shape();
        }

        return TopoDS_Shape();
    }

    void handleSketchClick(const gp_Pnt& point)
    {
        if (!hasFirstSketchPoint_) {
            firstSketchPoint_ = point;
            hasFirstSketchPoint_ = true;
            return;
        }

        TopoDS_Shape shape =
            makeSketchShape(firstSketchPoint_, point, true);
        if (!shape.IsNull() && sketchCommittedCallback_) {
            QString baseName = "Sketch";
            if (sketchTool_ == SketchTool::Line) baseName = "SketchLine";
            if (sketchTool_ == SketchTool::Rectangle) baseName = "SketchRect";
            if (sketchTool_ == SketchTool::Circle) baseName = "SketchCircle";
            sketchCommittedCallback_(shape, baseName);
        }

        clearPreview();
        hasFirstSketchPoint_ = false;
    }

    void updateSketchPreview(const gp_Pnt& point)
    {
        TopoDS_Shape preview =
            makeSketchShape(firstSketchPoint_, point, false);

        clearPreview();
        if (preview.IsNull()) {
            return;
        }

        previewPresentation_ = new AIS_Shape(preview);
        previewPresentation_->SetColor(
            Quantity_Color(0.20, 0.75, 1.0, Quantity_TOC_RGB));
        context_->Display(previewPresentation_, false);
        context_->SetDisplayMode(
            previewPresentation_,
            AIS_WireFrame,
            false);
        context_->UpdateCurrentViewer();
    }

    void setSketchGridVisible(bool visible)
    {
        if (gridPresentation_.IsNull()) {
            BRep_Builder builder;
            TopoDS_Compound compound;
            builder.MakeCompound(compound);

            constexpr double extent = 100.0;
            constexpr double step = 10.0;

            for (int i = -10; i <= 10; ++i) {
                const double d = i * step;
                builder.Add(
                    compound,
                    BRepBuilderAPI_MakeEdge(
                        fromSketchUV(-extent, d),
                        fromSketchUV(extent, d)).Shape());
                builder.Add(
                    compound,
                    BRepBuilderAPI_MakeEdge(
                        fromSketchUV(d, -extent),
                        fromSketchUV(d, extent)).Shape());
            }

            gridPresentation_ = new AIS_Shape(compound);
            gridPresentation_->SetColor(
                Quantity_Color(0.68, 0.70, 0.72, Quantity_TOC_RGB));
            context_->Display(gridPresentation_, false);
            context_->SetDisplayMode(
                gridPresentation_,
                AIS_WireFrame,
                false);
        }

        if (visible) {
            context_->Display(gridPresentation_, false);
        } else {
            context_->Erase(gridPresentation_, false);
        }
        context_->UpdateCurrentViewer();
    }

    void initializeViewer()
    {
        Handle(Aspect_DisplayConnection) displayConnection =
            new Aspect_DisplayConnection();
        driver_ = new OpenGl_GraphicDriver(displayConnection);

        viewer_ = new V3d_Viewer(driver_);
        viewer_->SetDefaultLights();
        viewer_->SetLightOn();
        viewer_->SetDefaultBackgroundColor(
            Quantity_Color(0.42, 0.45, 0.49, Quantity_TOC_RGB));

        context_ = new AIS_InteractiveContext(viewer_);
        view_ = viewer_->CreateView();

        Handle(WNT_Window) nativeWindow =
            new WNT_Window(reinterpret_cast<Aspect_Handle>(winId()));

        view_->SetWindow(nativeWindow);
        if (!nativeWindow->IsMapped()) {
            nativeWindow->Map();
        }

        view_->SetProj(V3d_XposYnegZpos);
        view_->MustBeResized();
        view_->Redraw();
    }

    Handle(OpenGl_GraphicDriver) driver_;
    Handle(V3d_Viewer) viewer_;
    Handle(V3d_View) view_;
    Handle(AIS_InteractiveContext) context_;

    Handle(AIS_Shape) previewPresentation_;
    Handle(AIS_Shape) gridPresentation_;

    std::function<void(const Handle(AIS_InteractiveObject)&)> selectionCallback_;
    std::function<void(const TopoDS_Shape&, const QString&)> sketchCommittedCallback_;
    std::function<void()> cancelCallback_;

    SketchTool sketchTool_ = SketchTool::None;
    DatumPlane currentSketchPlane_ = DatumPlane::XY;
    gp_Pnt firstSketchPoint_;
    bool hasFirstSketchPoint_ = false;

    QPoint lastMousePos_;
    bool rotating_ = false;
    bool panning_ = false;
};

class MainWindow final : public QMainWindow
{
public:
    MainWindow()
    {
        setWindowTitle("MyCAD V0.4 CATIA Foundation");
        resize(1500, 920);

        view_ = new CadView(this);
        setCentralWidget(view_);

        view_->setSelectionCallback(
            [this](const Handle(AIS_InteractiveObject)& selected) {
                syncTreeFromViewport(selected);
            });
        view_->setSketchCommittedCallback(
            [this](const TopoDS_Shape& shape, const QString& baseName) {
                addShape(
                    QString("%1_%2").arg(baseName).arg(++objectCounter_),
                    ObjectKind::Sketch,
                    shape,
                    true);
                taskHelpLabel_->setText(
                    QString::fromUtf8(
                        "已建立草圖幾何。可繼續繪製，按 Esc 或「完成」離開草圖模式。"));
            });
        view_->setCancelCallback(
            [this]() {
                cancelTask(false);
            });

        buildModelDock();
        buildPropertyDock();
        buildMenus();

        statusBar()->showMessage(
            QString::fromUtf8(
                "中鍵拖曳：旋轉｜Shift+中鍵：平移｜滾輪：縮放｜右鍵拖曳：旋轉"));

        addBox(100.0, 60.0, 20.0, false);
        undoStack_.clear();
        redoStack_.clear();
    }

private:
    void buildModelDock()
    {
        modelDock_ = new QDockWidget(QString::fromUtf8("模型樹"), this);
        modelTree_ = new QTreeWidget(modelDock_);
        modelTree_->setHeaderLabel("MyCAD Model");
        modelTree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
        modelDock_->setWidget(modelTree_);
        addDockWidget(Qt::LeftDockWidgetArea, modelDock_);

        connect(
            modelTree_,
            &QTreeWidget::itemSelectionChanged,
            this,
            [this]() {
                updateProperties();
                syncViewportFromTree();
            });
    }

    void buildPropertyDock()
    {
        propertyDock_ =
            new QDockWidget(QString::fromUtf8("工作 / 屬性"), this);

        propertyTabs_ = new QTabWidget(propertyDock_);

        taskPage_ = new QWidget(propertyTabs_);
        auto* taskLayout = new QVBoxLayout(taskPage_);
        taskTitleLabel_ = new QLabel(
            QString::fromUtf8("沒有進行中的命令"),
            taskPage_);
        QFont titleFont = taskTitleLabel_->font();
        titleFont.setBold(true);
        titleFont.setPointSize(titleFont.pointSize() + 2);
        taskTitleLabel_->setFont(titleFont);

        taskHelpLabel_ = new QLabel(
            QString::fromUtf8(
                "從上方工具列選擇草圖或建模命令。"),
            taskPage_);
        taskHelpLabel_->setWordWrap(true);

        taskValueLabel_ =
            new QLabel(QString::fromUtf8("長度 (mm)"), taskPage_);
        taskValueSpin_ = new QDoubleSpinBox(taskPage_);
        taskValueSpin_->setDecimals(3);
        taskValueSpin_->setRange(-1000000.0, 1000000.0);
        taskValueSpin_->setValue(20.0);
        taskValueSpin_->setSingleStep(1.0);

        taskReverse_ =
            new QCheckBox(QString::fromUtf8("反向"), taskPage_);

        auto* form = new QFormLayout();
        form->addRow(taskValueLabel_, taskValueSpin_);
        form->addRow(QString(), taskReverse_);

        taskApplyButton_ =
            new QPushButton(QString::fromUtf8("套用"), taskPage_);
        taskCancelButton_ =
            new QPushButton(QString::fromUtf8("取消"), taskPage_);

        auto* buttonRow = new QHBoxLayout();
        buttonRow->addWidget(taskApplyButton_);
        buttonRow->addWidget(taskCancelButton_);

        taskLayout->addWidget(taskTitleLabel_);
        taskLayout->addWidget(taskHelpLabel_);
        taskLayout->addLayout(form);
        taskLayout->addStretch();
        taskLayout->addLayout(buttonRow);

        connect(
            taskValueSpin_,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            [this](double) { updateTaskPreview(); });
        connect(
            taskReverse_,
            &QCheckBox::toggled,
            this,
            [this](bool) { updateTaskPreview(); });
        connect(
            taskApplyButton_,
            &QPushButton::clicked,
            this,
            [this]() { applyTask(); });
        connect(
            taskCancelButton_,
            &QPushButton::clicked,
            this,
            [this]() { cancelTask(); });

        propertyTable_ = new QTableWidget(propertyTabs_);
        propertyTable_->setColumnCount(2);
        propertyTable_->setHorizontalHeaderLabels(
            {QString::fromUtf8("屬性"), QString::fromUtf8("值")});
        propertyTable_->horizontalHeader()->setStretchLastSection(true);
        propertyTable_->verticalHeader()->setVisible(false);
        propertyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);

        propertyTabs_->addTab(
            taskPage_,
            QString::fromUtf8("工作"));
        propertyTabs_->addTab(
            propertyTable_,
            QString::fromUtf8("屬性"));

        propertyDock_->setWidget(propertyTabs_);
        addDockWidget(Qt::RightDockWidgetArea, propertyDock_);

        showTaskIdle();
    }

    void buildMenus()
    {
        QMenu* fileMenu = menuBar()->addMenu(QString::fromUtf8("檔案"));
        QMenu* editMenu = menuBar()->addMenu(QString::fromUtf8("編輯"));
        QMenu* sketchMenu = menuBar()->addMenu(QString::fromUtf8("草圖"));
        QMenu* partDesignMenu =
            menuBar()->addMenu(QString::fromUtf8("零件設計"));
        QMenu* partMenu = menuBar()->addMenu(QString::fromUtf8("實體"));
        QMenu* transformMenu =
            menuBar()->addMenu(QString::fromUtf8("變換"));
        QMenu* measureMenu =
            menuBar()->addMenu(QString::fromUtf8("測量"));
        QMenu* viewMenu = menuBar()->addMenu(QString::fromUtf8("視圖"));

        QToolBar* workbenchBar =
            addToolBar(QString::fromUtf8("工作台"));
        workbenchBar->setMovable(false);

        workbenchCombo_ = new QComboBox(workbenchBar);
        workbenchCombo_->addItems({
            "Part Design",
            "Sketcher",
            "Assembly Design",
            "Drafting",
            "Generative Shape Design"
        });
        workbenchCombo_->setMinimumWidth(190);
        workbenchBar->addWidget(workbenchCombo_);

        sketchBar_ = addToolBar(QString::fromUtf8("草圖"));
        modelBar_ = addToolBar(QString::fromUtf8("建模"));
        viewBar_ = addToolBar(QString::fromUtf8("視圖"));

        sketchBar_->setMovable(false);
        modelBar_->setMovable(false);
        viewBar_->setMovable(false);

        QAction* newAct = new QAction(QString::fromUtf8("新建"), this);
        QAction* importStepAct =
            new QAction(QString::fromUtf8("匯入 STEP"), this);
        QAction* importIgesAct =
            new QAction(QString::fromUtf8("匯入 IGES"), this);
        QAction* importStlAct =
            new QAction(QString::fromUtf8("匯入 STL"), this);
        QAction* importBrepAct =
            new QAction(QString::fromUtf8("匯入 BREP"), this);
        QAction* exportStepAct =
            new QAction(QString::fromUtf8("匯出 STEP"), this);
        QAction* exportIgesAct =
            new QAction(QString::fromUtf8("匯出 IGES"), this);
        QAction* exportStlAct =
            new QAction(QString::fromUtf8("匯出 STL"), this);
        QAction* exportBrepAct =
            new QAction(QString::fromUtf8("匯出 BREP"), this);

        connect(newAct, &QAction::triggered, this, [this]() { newDocument(); });
        connect(importStepAct, &QAction::triggered, this, [this]() { importStep(); });
        connect(importIgesAct, &QAction::triggered, this, [this]() { importIges(); });
        connect(importStlAct, &QAction::triggered, this, [this]() { importStl(); });
        connect(importBrepAct, &QAction::triggered, this, [this]() { importBrep(); });
        connect(exportStepAct, &QAction::triggered, this, [this]() { exportStep(); });
        connect(exportIgesAct, &QAction::triggered, this, [this]() { exportIges(); });
        connect(exportStlAct, &QAction::triggered, this, [this]() { exportStl(); });
        connect(exportBrepAct, &QAction::triggered, this, [this]() { exportBrep(); });

        fileMenu->addAction(newAct);
        fileMenu->addSeparator();
        fileMenu->addActions({importStepAct, importIgesAct, importStlAct, importBrepAct});
        fileMenu->addSeparator();
        fileMenu->addActions({exportStepAct, exportIgesAct, exportStlAct, exportBrepAct});

        QAction* undoAct = new QAction(QString::fromUtf8("復原"), this);
        QAction* redoAct = new QAction(QString::fromUtf8("重做"), this);
        undoAct->setShortcut(QKeySequence::Undo);
        redoAct->setShortcut(QKeySequence::Redo);
        connect(undoAct, &QAction::triggered, this, [this]() { undo(); });
        connect(redoAct, &QAction::triggered, this, [this]() { redo(); });
        editMenu->addActions({undoAct, redoAct});

        QAction* sketchRectAct =
            new QAction(QString::fromUtf8("矩形草圖"), this);
        QAction* sketchCircleAct =
            new QAction(QString::fromUtf8("圓形草圖"), this);
        QAction* sketchLineAct =
            new QAction(QString::fromUtf8("直線草圖"), this);

        connect(sketchRectAct, &QAction::triggered, this, [this]() {
            startSketchTask(
                SketchTool::Rectangle,
                QString::fromUtf8("矩形草圖"),
                QString::fromUtf8("在工作區點一下第一個角，再點一下對角。"));
        });
        connect(sketchCircleAct, &QAction::triggered, this, [this]() {
            startSketchTask(
                SketchTool::Circle,
                QString::fromUtf8("圓形草圖"),
                QString::fromUtf8("先點圓心，再點圓周決定半徑。"));
        });
        connect(sketchLineAct, &QAction::triggered, this, [this]() {
            startSketchTask(
                SketchTool::Line,
                QString::fromUtf8("直線草圖"),
                QString::fromUtf8("點兩個端點建立直線。"));
        });

        sketchMenu->addActions({sketchRectAct, sketchCircleAct, sketchLineAct});
        sketchBar_->addActions({sketchRectAct, sketchCircleAct});

        QAction* padAct = new QAction(QString::fromUtf8("凸台 Pad"), this);
        QAction* pocketAct = new QAction(QString::fromUtf8("凹槽 Pocket"), this);
        QAction* revolveAct = new QAction(QString::fromUtf8("旋轉 Revolve"), this);
        QAction* holeAct = new QAction(QString::fromUtf8("孔 Hole"), this);

        connect(padAct, &QAction::triggered, this, [this]() { startPadTask(); });
        connect(pocketAct, &QAction::triggered, this, [this]() { startPocketTask(); });
        connect(revolveAct, &QAction::triggered, this, [this]() { revolveSketch(); });
        connect(holeAct, &QAction::triggered, this, [this]() { createHole(); });

        partDesignMenu->addActions({padAct, pocketAct, revolveAct, holeAct});
        modelBar_->addActions({padAct, pocketAct});

        QAction* boxAct = new QAction(QString::fromUtf8("方塊"), this);
        QAction* cylinderAct = new QAction(QString::fromUtf8("圓柱"), this);
        QAction* sphereAct = new QAction(QString::fromUtf8("球體"), this);
        QAction* coneAct = new QAction(QString::fromUtf8("圓錐"), this);
        QAction* torusAct = new QAction(QString::fromUtf8("圓環"), this);
        QAction* fuseAct = new QAction(QString::fromUtf8("聯集 Fuse"), this);
        QAction* cutAct = new QAction(QString::fromUtf8("差集 Cut"), this);
        QAction* commonAct = new QAction(QString::fromUtf8("交集 Common"), this);
        QAction* filletAct = new QAction(QString::fromUtf8("圓角 Fillet"), this);
        QAction* chamferAct = new QAction(QString::fromUtf8("倒角 Chamfer"), this);
        QAction* patternAct = new QAction(QString::fromUtf8("線性陣列"), this);
        QAction* deleteAct = new QAction(QString::fromUtf8("刪除"), this);
        deleteAct->setShortcut(QKeySequence::Delete);

        connect(boxAct, &QAction::triggered, this, [this]() { createBox(); });
        connect(cylinderAct, &QAction::triggered, this, [this]() { createCylinder(); });
        connect(sphereAct, &QAction::triggered, this, [this]() { createSphere(); });
        connect(coneAct, &QAction::triggered, this, [this]() { createCone(); });
        connect(torusAct, &QAction::triggered, this, [this]() { createTorus(); });
        connect(fuseAct, &QAction::triggered, this, [this]() { booleanFuse(); });
        connect(cutAct, &QAction::triggered, this, [this]() { booleanCut(); });
        connect(commonAct, &QAction::triggered, this, [this]() { booleanCommon(); });
        connect(filletAct, &QAction::triggered, this, [this]() { filletSelected(); });
        connect(chamferAct, &QAction::triggered, this, [this]() { chamferSelected(); });
        connect(patternAct, &QAction::triggered, this, [this]() { linearPattern(); });
        connect(deleteAct, &QAction::triggered, this, [this]() { deleteSelected(); });

        partMenu->addActions({boxAct, cylinderAct, sphereAct, coneAct, torusAct});
        partMenu->addSeparator();
        partMenu->addActions({fuseAct, cutAct, commonAct});
        partMenu->addSeparator();
        partMenu->addActions({filletAct, chamferAct, patternAct, deleteAct});
        modelBar_->addActions({boxAct, cylinderAct, filletAct, chamferAct});

        QAction* moveAct = new QAction(QString::fromUtf8("移動"), this);
        QAction* rotateAct = new QAction(QString::fromUtf8("旋轉物件"), this);
        QAction* mirrorXYAct = new QAction(QString::fromUtf8("鏡射 XY"), this);
        QAction* mirrorYZAct = new QAction(QString::fromUtf8("鏡射 YZ"), this);
        QAction* mirrorXZAct = new QAction(QString::fromUtf8("鏡射 XZ"), this);

        connect(moveAct, &QAction::triggered, this, [this]() { moveSelected(); });
        connect(rotateAct, &QAction::triggered, this, [this]() { rotateSelected(); });
        connect(mirrorXYAct, &QAction::triggered, this, [this]() { mirrorSelected("XY"); });
        connect(mirrorYZAct, &QAction::triggered, this, [this]() { mirrorSelected("YZ"); });
        connect(mirrorXZAct, &QAction::triggered, this, [this]() { mirrorSelected("XZ"); });

        transformMenu->addActions({moveAct, rotateAct});
        transformMenu->addSeparator();
        transformMenu->addActions({mirrorXYAct, mirrorYZAct, mirrorXZAct});

        QAction* measureAct =
            new QAction(QString::fromUtf8("幾何資訊"), this);
        connect(measureAct, &QAction::triggered, this, [this]() { showMeasurement(); });
        measureMenu->addAction(measureAct);

        QAction* axoAct = new QAction(QString::fromUtf8("等角"), this);
        QAction* topAct = new QAction(QString::fromUtf8("上視"), this);
        QAction* bottomAct = new QAction(QString::fromUtf8("下視"), this);
        QAction* frontAct = new QAction(QString::fromUtf8("前視"), this);
        QAction* backAct = new QAction(QString::fromUtf8("後視"), this);
        QAction* rightAct = new QAction(QString::fromUtf8("右視"), this);
        QAction* leftAct = new QAction(QString::fromUtf8("左視"), this);
        QAction* fitAct = new QAction("Fit All", this);
        fitAct->setShortcut(QKeySequence("F"));
        QAction* wireAct = new QAction(QString::fromUtf8("線框"), this);
        QAction* shadedAct = new QAction(QString::fromUtf8("著色"), this);
        QAction* hideAct = new QAction(QString::fromUtf8("隱藏"), this);
        QAction* showAct = new QAction(QString::fromUtf8("顯示"), this);

        connect(axoAct, &QAction::triggered, view_, [this]() { view_->viewAxo(); });
        connect(topAct, &QAction::triggered, view_, [this]() { view_->viewTop(); });
        connect(bottomAct, &QAction::triggered, view_, [this]() { view_->viewBottom(); });
        connect(frontAct, &QAction::triggered, view_, [this]() { view_->viewFront(); });
        connect(backAct, &QAction::triggered, view_, [this]() { view_->viewBack(); });
        connect(rightAct, &QAction::triggered, view_, [this]() { view_->viewRight(); });
        connect(leftAct, &QAction::triggered, view_, [this]() { view_->viewLeft(); });
        connect(fitAct, &QAction::triggered, view_, [this]() { view_->fitAll(); });
        connect(wireAct, &QAction::triggered, this, [this]() {
            view_->setAllDisplayMode(objects_, true);
        });
        connect(shadedAct, &QAction::triggered, this, [this]() {
            view_->setAllDisplayMode(objects_, false);
        });
        connect(hideAct, &QAction::triggered, this, [this]() { setSelectedVisibility(false); });
        connect(showAct, &QAction::triggered, this, [this]() { setSelectedVisibility(true); });

        viewMenu->addActions({axoAct, topAct, bottomAct, frontAct, backAct, rightAct, leftAct, fitAct});
        viewMenu->addSeparator();
        viewMenu->addActions({wireAct, shadedAct, hideAct, showAct});
        viewBar_->addActions({axoAct, topAct, frontAct, rightAct, fitAct});

        connect(
            workbenchCombo_,
            &QComboBox::currentTextChanged,
            this,
            [this](const QString& name) {
                updateWorkbench(name);
            });

        updateWorkbench("Part Design");
    }

    void updateWorkbench(const QString& name)
    {
        const bool partDesign = name == "Part Design";
        const bool sketcher = name == "Sketcher";

        sketchBar_->setVisible(partDesign || sketcher);
        modelBar_->setVisible(partDesign);
        viewBar_->setVisible(true);

        if (partDesign) {
            statusBar()->showMessage(
                QString::fromUtf8(
                    "Part Design：請在 Origin 選擇基準面，再建立 Sketch。"));
        } else if (sketcher) {
            statusBar()->showMessage(
                QString::fromUtf8(
                    "Sketcher：選取 XY / YZ / ZX Plane 後使用草圖工具。"));
        } else {
            statusBar()->showMessage(
                name + QString::fromUtf8(
                    " 工作台已建立入口，功能將在後續版本逐步加入。"));
        }
    }

    DatumPlane selectedDatumPlane() const
    {
        const auto items = modelTree_->selectedItems();
        if (items.size() != 1) {
            return DatumPlane::None;
        }

        const int code =
            items.front()->data(0, Qt::UserRole).toInt();

        if (code == -101) return DatumPlane::XY;
        if (code == -102) return DatumPlane::YZ;
        if (code == -103) return DatumPlane::ZX;
        return DatumPlane::None;
    }

    void showTaskIdle()
    {
        taskKind_ = TaskKind::None;
        taskPrimaryIndex_ = -1;
        taskSecondaryIndex_ = -1;

        taskTitleLabel_->setText(
            QString::fromUtf8("沒有進行中的命令"));
        taskHelpLabel_->setText(
            QString::fromUtf8(
                "從工具列選擇草圖或建模命令。3D 視窗與模型樹的選取會同步。"));
        taskValueLabel_->setVisible(false);
        taskValueSpin_->setVisible(false);
        taskReverse_->setVisible(false);
        taskApplyButton_->setVisible(false);
        taskCancelButton_->setVisible(false);
        propertyTabs_->setCurrentWidget(propertyTable_);
    }

    void startSketchTask(
        SketchTool tool,
        const QString& title,
        const QString& help)
    {
        const DatumPlane plane = selectedDatumPlane();
        if (plane == DatumPlane::None) {
            QMessageBox::information(
                this,
                QString::fromUtf8("建立草圖"),
                QString::fromUtf8(
                    "請先在 Part1 → Body → Origin 選取 XY Plane、YZ Plane 或 ZX Plane。"));
            return;
        }

        cancelTask();
        taskKind_ = TaskKind::Sketch;

        taskTitleLabel_->setText(
            title + " — " + datumPlaneName(plane));
        taskHelpLabel_->setText(
            help + "\n" +
            QString::fromUtf8("支援平面：") +
            datumPlaneName(plane));
        taskValueLabel_->setVisible(false);
        taskValueSpin_->setVisible(false);
        taskReverse_->setVisible(false);
        taskApplyButton_->setText(QString::fromUtf8("完成"));
        taskApplyButton_->setVisible(true);
        taskCancelButton_->setVisible(true);

        propertyTabs_->setCurrentWidget(taskPage_);
        view_->beginSketchTool(tool, plane);
        statusBar()->showMessage(
            QString::fromUtf8(
                "草圖模式：左鍵繪製｜中鍵旋轉｜Shift+中鍵平移｜Esc 離開"));
    }

    void startPadTask()
    {
        int index = -1;
        if (!selectedSketchFace(index)) {
            QMessageBox::information(
                this,
                "Pad",
                QString::fromUtf8(
                    "請先選取一個封閉的矩形或圓形草圖。"));
            return;
        }

        cancelTask();
        taskKind_ = TaskKind::Pad;
        taskPrimaryIndex_ = index;

        taskTitleLabel_->setText(
            QString::fromUtf8("Pad 凸台"));
        taskHelpLabel_->setText(
            QString::fromUtf8(
                "調整拉伸長度，3D 視窗會即時預覽。"));
        taskValueLabel_->setText(
            QString::fromUtf8("長度 (mm)"));
        taskValueLabel_->setVisible(true);
        taskValueSpin_->setVisible(true);
        taskValueSpin_->setRange(0.001, 1000000.0);
        taskValueSpin_->setValue(20.0);
        taskReverse_->setChecked(false);
        taskReverse_->setVisible(true);
        taskApplyButton_->setText(QString::fromUtf8("套用"));
        taskApplyButton_->setVisible(true);
        taskCancelButton_->setVisible(true);

        propertyTabs_->setCurrentWidget(taskPage_);
        updateTaskPreview();
    }

    void startPocketTask()
    {
        const auto indices = selectedIndices();
        if (indices.size() != 2) {
            QMessageBox::information(
                this,
                "Pocket",
                QString::fromUtf8(
                    "請在模型樹同時選取一個實體與一個封閉草圖。"));
            return;
        }

        int solidIndex = -1;
        int sketchIndex = -1;
        for (int index : indices) {
            if (objects_[index].kind == ObjectKind::Sketch) {
                sketchIndex = index;
            } else {
                solidIndex = index;
            }
        }

        if (solidIndex < 0 || sketchIndex < 0 ||
            !TopExp_Explorer(
                objects_[sketchIndex].shape,
                TopAbs_FACE).More()) {
            QMessageBox::information(
                this,
                "Pocket",
                QString::fromUtf8(
                    "需要一個實體與一個封閉草圖。"));
            return;
        }

        cancelTask();
        taskKind_ = TaskKind::Pocket;
        taskPrimaryIndex_ = solidIndex;
        taskSecondaryIndex_ = sketchIndex;

        taskTitleLabel_->setText(
            QString::fromUtf8("Pocket 凹槽"));
        taskHelpLabel_->setText(
            QString::fromUtf8(
                "調整切除深度，3D 視窗會即時預覽。"));
        taskValueLabel_->setText(
            QString::fromUtf8("深度 (mm)"));
        taskValueLabel_->setVisible(true);
        taskValueSpin_->setVisible(true);
        taskValueSpin_->setRange(0.001, 1000000.0);
        taskValueSpin_->setValue(20.0);
        taskReverse_->setChecked(false);
        taskReverse_->setVisible(true);
        taskApplyButton_->setText(QString::fromUtf8("套用"));
        taskApplyButton_->setVisible(true);
        taskCancelButton_->setVisible(true);

        propertyTabs_->setCurrentWidget(taskPage_);
        updateTaskPreview();
    }

    TopoDS_Shape buildTaskResult() const
    {
        if (taskKind_ == TaskKind::Pad &&
            taskPrimaryIndex_ >= 0 &&
            taskPrimaryIndex_ < static_cast<int>(objects_.size())) {
            double length = taskValueSpin_->value();
            if (taskReverse_->isChecked()) {
                length = -length;
            }
            return BRepPrimAPI_MakePrism(
                objects_[taskPrimaryIndex_].shape,
                gp_Vec(0.0, 0.0, length)).Shape();
        }

        if (taskKind_ == TaskKind::Pocket &&
            taskPrimaryIndex_ >= 0 &&
            taskSecondaryIndex_ >= 0 &&
            taskPrimaryIndex_ < static_cast<int>(objects_.size()) &&
            taskSecondaryIndex_ < static_cast<int>(objects_.size())) {
            double depth = taskValueSpin_->value();
            if (taskReverse_->isChecked()) {
                depth = -depth;
            }

            TopoDS_Shape tool =
                BRepPrimAPI_MakePrism(
                    objects_[taskSecondaryIndex_].shape,
                    gp_Vec(0.0, 0.0, depth)).Shape();

            return BRepAlgoAPI_Cut(
                objects_[taskPrimaryIndex_].shape,
                tool).Shape();
        }

        return TopoDS_Shape();
    }

    void updateTaskPreview()
    {
        if (taskKind_ != TaskKind::Pad &&
            taskKind_ != TaskKind::Pocket) {
            return;
        }

        try {
            TopoDS_Shape result = buildTaskResult();
            if (!result.IsNull()) {
                view_->showPreviewShape(result);
            }
        } catch (const Standard_Failure&) {
            view_->clearPreview();
        }
    }

    void applyTask()
    {
        if (taskKind_ == TaskKind::Sketch) {
            cancelTask();
            return;
        }

        if (taskKind_ != TaskKind::Pad &&
            taskKind_ != TaskKind::Pocket) {
            return;
        }

        try {
            TopoDS_Shape result = buildTaskResult();
            if (result.IsNull()) {
                return;
            }

            const TaskKind completedKind = taskKind_;
            const int primary = taskPrimaryIndex_;
            const int secondary = taskSecondaryIndex_;

            view_->clearPreview();
            pushUndo();

            if (completedKind == TaskKind::Pad) {
                addShape(
                    QString("Pad_%1").arg(++objectCounter_),
                    ObjectKind::Solid,
                    result,
                    false);
                if (primary >= 0 &&
                    primary < static_cast<int>(objects_.size())) {
                    objects_[primary].visible = false;
                    view_->setVisible(
                        objects_[primary].presentation,
                        false);
                }
            } else {
                addShape(
                    QString("Pocket_%1").arg(++objectCounter_),
                    ObjectKind::Solid,
                    result,
                    false);

                if (primary >= 0 &&
                    primary < static_cast<int>(objects_.size())) {
                    objects_[primary].visible = false;
                    view_->setVisible(
                        objects_[primary].presentation,
                        false);
                }
                if (secondary >= 0 &&
                    secondary < static_cast<int>(objects_.size())) {
                    objects_[secondary].visible = false;
                    view_->setVisible(
                        objects_[secondary].presentation,
                        false);
                }
            }

            rebuildTree();
            cancelTask();
        } catch (const Standard_Failure& e) {
            showOcctError("MyCAD", e);
        }
    }

    void cancelTask(bool notifyView = true)
    {
        view_->clearPreview();
        if (notifyView) {
            view_->cancelSketchInteraction();
        }
        showTaskIdle();
        statusBar()->showMessage(
            QString::fromUtf8(
                "中鍵拖曳：旋轉｜Shift+中鍵：平移｜滾輪：縮放"));
    }

    void syncViewportFromTree()
    {
        std::vector<Handle(AIS_Shape)> presentations;
        for (int index : selectedIndices()) {
            presentations.push_back(objects_[index].presentation);
        }
        view_->selectPresentations(presentations);
    }

    void syncTreeFromViewport(
        const Handle(AIS_InteractiveObject)& selected)
    {
        QSignalBlocker blocker(modelTree_);
        modelTree_->clearSelection();

        if (selected.IsNull()) {
            updateProperties();
            return;
        }

        Handle(AIS_Shape) selectedShape =
            Handle(AIS_Shape)::DownCast(selected);

        if (selectedShape.IsNull() ||
            modelTree_->topLevelItemCount() == 0) {
            updateProperties();
            return;
        }

        QTreeWidgetItem* part =
            modelTree_->topLevelItem(0);
        if (part == nullptr || part->childCount() == 0) {
            updateProperties();
            return;
        }

        QTreeWidgetItem* body = part->child(0);
        constexpr int featureOffset = 1; // Origin is child 0.

        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
            const int childIndex = i + featureOffset;
            if (objects_[i].presentation == selectedShape &&
                childIndex < body->childCount()) {
                QTreeWidgetItem* item =
                    body->child(childIndex);
                item->setSelected(true);
                modelTree_->setCurrentItem(item);
                break;
            }
        }

        updateProperties();
    }

    Snapshot captureSnapshot() const
    {
        Snapshot snapshot;
        snapshot.reserve(objects_.size());
        for (const auto& obj : objects_) {
            snapshot.push_back({obj.name, obj.kind, obj.shape, obj.visible});
        }
        return snapshot;
    }

    void restoreSnapshot(const Snapshot& snapshot)
    {
        view_->clearScene();
        objects_.clear();

        for (const auto& saved : snapshot) {
            ModelObject obj;
            obj.name = saved.name;
            obj.kind = saved.kind;
            obj.shape = saved.shape;
            obj.visible = saved.visible;
            obj.presentation =
                view_->displayShape(obj.shape, obj.kind, obj.visible);
            objects_.push_back(obj);
        }

        rebuildTree();
        view_->fitAll();
        updateProperties();
    }

    void pushUndo()
    {
        undoStack_.push_back(captureSnapshot());
        if (undoStack_.size() > 50) {
            undoStack_.erase(undoStack_.begin());
        }
        redoStack_.clear();
    }

    void undo()
    {
        if (undoStack_.empty()) {
            return;
        }
        redoStack_.push_back(captureSnapshot());
        Snapshot snapshot = undoStack_.back();
        undoStack_.pop_back();
        restoreSnapshot(snapshot);
    }

    void redo()
    {
        if (redoStack_.empty()) {
            return;
        }
        undoStack_.push_back(captureSnapshot());
        Snapshot snapshot = redoStack_.back();
        redoStack_.pop_back();
        restoreSnapshot(snapshot);
    }

    void newDocument()
    {
        cancelTask();
        pushUndo();
        objects_.clear();
        view_->clearScene();
        objectCounter_ = 0;
        setWindowTitle("MyCAD V0.4 CATIA Foundation");
        rebuildTree();
        updateProperties();
    }

    void createBox()
    {
        bool ok = false;
        const double x = QInputDialog::getDouble(
            this, QString::fromUtf8("方塊"),
            QString::fromUtf8("長度 X (mm)"),
            100.0, 0.001, 1000000.0, 3, &ok);
        if (!ok) return;

        const double y = QInputDialog::getDouble(
            this, QString::fromUtf8("方塊"),
            QString::fromUtf8("寬度 Y (mm)"),
            60.0, 0.001, 1000000.0, 3, &ok);
        if (!ok) return;

        const double z = QInputDialog::getDouble(
            this, QString::fromUtf8("方塊"),
            QString::fromUtf8("高度 Z (mm)"),
            20.0, 0.001, 1000000.0, 3, &ok);
        if (!ok) return;

        addBox(x, y, z, true);
    }

    void addBox(double x, double y, double z, bool recordUndo)
    {
        TopoDS_Shape shape = BRepPrimAPI_MakeBox(x, y, z).Shape();
        addShape(
            QString("Box_%1").arg(++objectCounter_),
            ObjectKind::Solid,
            shape,
            recordUndo);
    }

    void createCylinder()
    {
        bool ok = false;
        const double radius = QInputDialog::getDouble(
            this, QString::fromUtf8("圓柱"),
            QString::fromUtf8("半徑 (mm)"),
            25.0, 0.001, 1000000.0, 3, &ok);
        if (!ok) return;

        const double height = QInputDialog::getDouble(
            this, QString::fromUtf8("圓柱"),
            QString::fromUtf8("高度 (mm)"),
            50.0, 0.001, 1000000.0, 3, &ok);
        if (!ok) return;

        addShape(
            QString("Cylinder_%1").arg(++objectCounter_),
            ObjectKind::Solid,
            BRepPrimAPI_MakeCylinder(radius, height).Shape(),
            true);
    }

    void createSphere()
    {
        bool ok = false;
        const double radius = QInputDialog::getDouble(
            this, QString::fromUtf8("球體"),
            QString::fromUtf8("半徑 (mm)"),
            25.0, 0.001, 1000000.0, 3, &ok);
        if (!ok) return;

        addShape(
            QString("Sphere_%1").arg(++objectCounter_),
            ObjectKind::Solid,
            BRepPrimAPI_MakeSphere(radius).Shape(),
            true);
    }

    void createCone()
    {
        bool ok = false;
        const double r1 = QInputDialog::getDouble(
            this, QString::fromUtf8("圓錐"),
            QString::fromUtf8("底部半徑 (mm)"),
            30.0, 0.001, 1000000.0, 3, &ok);
        if (!ok) return;
        const double r2 = QInputDialog::getDouble(
            this, QString::fromUtf8("圓錐"),
            QString::fromUtf8("頂部半徑 (mm，可為 0)"),
            0.0, 0.0, 1000000.0, 3, &ok);
        if (!ok) return;
        const double h = QInputDialog::getDouble(
            this, QString::fromUtf8("圓錐"),
            QString::fromUtf8("高度 (mm)"),
            60.0, 0.001, 1000000.0, 3, &ok);
        if (!ok) return;

        addShape(
            QString("Cone_%1").arg(++objectCounter_),
            ObjectKind::Solid,
            BRepPrimAPI_MakeCone(r1, r2, h).Shape(),
            true);
    }

    void createTorus()
    {
        bool ok = false;
        const double majorR = QInputDialog::getDouble(
            this, QString::fromUtf8("圓環"),
            QString::fromUtf8("主半徑 (mm)"),
            40.0, 0.001, 1000000.0, 3, &ok);
        if (!ok) return;
        const double minorR = QInputDialog::getDouble(
            this, QString::fromUtf8("圓環"),
            QString::fromUtf8("管半徑 (mm)"),
            10.0, 0.001, majorR, 3, &ok);
        if (!ok) return;

        addShape(
            QString("Torus_%1").arg(++objectCounter_),
            ObjectKind::Solid,
            BRepPrimAPI_MakeTorus(majorR, minorR).Shape(),
            true);
    }

    void createRectangleSketch()
    {
        bool ok = false;
        const double width = QInputDialog::getDouble(
            this, QString::fromUtf8("矩形草圖"),
            QString::fromUtf8("寬度 X (mm)"),
            60.0, 0.001, 1000000.0, 3, &ok);
        if (!ok) return;

        const double height = QInputDialog::getDouble(
            this, QString::fromUtf8("矩形草圖"),
            QString::fromUtf8("高度 Y (mm)"),
            40.0, 0.001, 1000000.0, 3, &ok);
        if (!ok) return;

        const double x = QInputDialog::getDouble(
            this, QString::fromUtf8("矩形草圖"),
            QString::fromUtf8("左下角 X (mm)"),
            0.0, -1000000.0, 1000000.0, 3, &ok);
        if (!ok) return;

        const double y = QInputDialog::getDouble(
            this, QString::fromUtf8("矩形草圖"),
            QString::fromUtf8("左下角 Y (mm)"),
            0.0, -1000000.0, 1000000.0, 3, &ok);
        if (!ok) return;

        BRepBuilderAPI_MakePolygon polygon;
        polygon.Add(gp_Pnt(x, y, 0.0));
        polygon.Add(gp_Pnt(x + width, y, 0.0));
        polygon.Add(gp_Pnt(x + width, y + height, 0.0));
        polygon.Add(gp_Pnt(x, y + height, 0.0));
        polygon.Close();

        TopoDS_Face face = BRepBuilderAPI_MakeFace(polygon.Wire()).Face();
        addShape(
            QString("SketchRect_%1").arg(++objectCounter_),
            ObjectKind::Sketch,
            face,
            true);
        view_->viewTop();
    }

    void createCircleSketch()
    {
        bool ok = false;
        const double radius = QInputDialog::getDouble(
            this, QString::fromUtf8("圓形草圖"),
            QString::fromUtf8("半徑 (mm)"),
            25.0, 0.001, 1000000.0, 3, &ok);
        if (!ok) return;

        const double cx = QInputDialog::getDouble(
            this, QString::fromUtf8("圓形草圖"),
            QString::fromUtf8("中心 X (mm)"),
            0.0, -1000000.0, 1000000.0, 3, &ok);
        if (!ok) return;

        const double cy = QInputDialog::getDouble(
            this, QString::fromUtf8("圓形草圖"),
            QString::fromUtf8("中心 Y (mm)"),
            0.0, -1000000.0, 1000000.0, 3, &ok);
        if (!ok) return;

        gp_Circ circle(
            gp_Ax2(gp_Pnt(cx, cy, 0.0), gp_Dir(0.0, 0.0, 1.0)),
            radius);
        TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(circle).Edge();
        TopoDS_Wire wire = BRepBuilderAPI_MakeWire(edge).Wire();
        TopoDS_Face face = BRepBuilderAPI_MakeFace(wire).Face();

        addShape(
            QString("SketchCircle_%1").arg(++objectCounter_),
            ObjectKind::Sketch,
            face,
            true);
        view_->viewTop();
    }

    void createLineSketch()
    {
        bool ok = false;
        const double x1 = QInputDialog::getDouble(
            this, QString::fromUtf8("直線草圖"), "X1 (mm)",
            0.0, -1000000.0, 1000000.0, 3, &ok);
        if (!ok) return;
        const double y1 = QInputDialog::getDouble(
            this, QString::fromUtf8("直線草圖"), "Y1 (mm)",
            0.0, -1000000.0, 1000000.0, 3, &ok);
        if (!ok) return;
        const double x2 = QInputDialog::getDouble(
            this, QString::fromUtf8("直線草圖"), "X2 (mm)",
            50.0, -1000000.0, 1000000.0, 3, &ok);
        if (!ok) return;
        const double y2 = QInputDialog::getDouble(
            this, QString::fromUtf8("直線草圖"), "Y2 (mm)",
            0.0, -1000000.0, 1000000.0, 3, &ok);
        if (!ok) return;

        TopoDS_Edge edge =
            BRepBuilderAPI_MakeEdge(
                gp_Pnt(x1, y1, 0.0),
                gp_Pnt(x2, y2, 0.0)).Edge();

        addShape(
            QString("SketchLine_%1").arg(++objectCounter_),
            ObjectKind::Sketch,
            edge,
            true);
        view_->viewTop();
    }

    bool selectedSketchFace(int& index) const
    {
        const auto indices = selectedIndices();
        if (indices.size() != 1) {
            return false;
        }
        index = indices.front();
        if (objects_[index].kind != ObjectKind::Sketch) {
            return false;
        }
        TopExp_Explorer faceExp(objects_[index].shape, TopAbs_FACE);
        return faceExp.More();
    }

    void padSketch()
    {
        int index = -1;
        if (!selectedSketchFace(index)) {
            QMessageBox::information(
                this,
                QString::fromUtf8("Pad"),
                QString::fromUtf8("請選取一個封閉的矩形或圓形草圖。"));
            return;
        }

        bool ok = false;
        const double length = QInputDialog::getDouble(
            this, "Pad",
            QString::fromUtf8("拉伸長度 Z (mm，可輸入負值)"),
            20.0, -1000000.0, 1000000.0, 3, &ok);
        if (!ok || std::abs(length) < 1e-9) return;

        try {
            TopoDS_Shape result =
                BRepPrimAPI_MakePrism(
                    objects_[index].shape,
                    gp_Vec(0.0, 0.0, length)).Shape();
            addShape(
                QString("Pad_%1").arg(++objectCounter_),
                ObjectKind::Solid,
                result,
                true);
        } catch (const Standard_Failure& e) {
            showOcctError("Pad", e);
        }
    }

    void pocketSketch()
    {
        const auto indices = selectedIndices();
        if (indices.size() != 2) {
            QMessageBox::information(
                this,
                "Pocket",
                QString::fromUtf8("請同時選取一個實體與一個封閉草圖。"));
            return;
        }

        int solidIndex = -1;
        int sketchIndex = -1;
        for (int index : indices) {
            if (objects_[index].kind == ObjectKind::Sketch) {
                sketchIndex = index;
            } else {
                solidIndex = index;
            }
        }

        if (solidIndex < 0 || sketchIndex < 0 ||
            !TopExp_Explorer(objects_[sketchIndex].shape, TopAbs_FACE).More()) {
            QMessageBox::information(
                this,
                "Pocket",
                QString::fromUtf8("需要一個實體與一個封閉草圖。"));
            return;
        }

        bool ok = false;
        const double depth = QInputDialog::getDouble(
            this, "Pocket",
            QString::fromUtf8("切除深度 Z (mm，可輸入負值)"),
            20.0, -1000000.0, 1000000.0, 3, &ok);
        if (!ok || std::abs(depth) < 1e-9) return;

        try {
            TopoDS_Shape tool =
                BRepPrimAPI_MakePrism(
                    objects_[sketchIndex].shape,
                    gp_Vec(0.0, 0.0, depth)).Shape();
            TopoDS_Shape result =
                BRepAlgoAPI_Cut(objects_[solidIndex].shape, tool).Shape();
            addShape(
                QString("Pocket_%1").arg(++objectCounter_),
                ObjectKind::Solid,
                result,
                true);
            hideObject(solidIndex);
            hideObject(sketchIndex);
        } catch (const Standard_Failure& e) {
            showOcctError("Pocket", e);
        }
    }

    void revolveSketch()
    {
        int index = -1;
        if (!selectedSketchFace(index)) {
            QMessageBox::information(
                this,
                "Revolve",
                QString::fromUtf8("請選取一個封閉草圖。"));
            return;
        }

        bool ok = false;
        const QString axis = QInputDialog::getItem(
            this, "Revolve",
            QString::fromUtf8("旋轉軸"),
            {"X", "Y"}, 0, false, &ok);
        if (!ok) return;

        const double angleDeg = QInputDialog::getDouble(
            this, "Revolve",
            QString::fromUtf8("角度 (deg)"),
            360.0, -360.0, 360.0, 3, &ok);
        if (!ok || std::abs(angleDeg) < 1e-9) return;

        const double angleRad = angleDeg * std::acos(-1.0) / 180.0;
        const gp_Dir direction =
            axis == "X" ? gp_Dir(1.0, 0.0, 0.0) : gp_Dir(0.0, 1.0, 0.0);

        try {
            TopoDS_Shape result =
                BRepPrimAPI_MakeRevol(
                    objects_[index].shape,
                    gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), direction),
                    angleRad).Shape();
            addShape(
                QString("Revolve_%1").arg(++objectCounter_),
                ObjectKind::Solid,
                result,
                true);
        } catch (const Standard_Failure& e) {
            showOcctError("Revolve", e);
        }
    }

    void createHole()
    {
        const int index = singleSelectedSolid();
        if (index < 0) {
            QMessageBox::information(
                this,
                "Hole",
                QString::fromUtf8("請選取一個實體。"));
            return;
        }

        Bnd_Box box;
        BRepBndLib::Add(objects_[index].shape, box);
        double xmin, ymin, zmin, xmax, ymax, zmax;
        box.Get(xmin, ymin, zmin, xmax, ymax, zmax);

        bool ok = false;
        const double x = QInputDialog::getDouble(
            this, "Hole", "X (mm)",
            (xmin + xmax) * 0.5, -1000000.0, 1000000.0, 3, &ok);
        if (!ok) return;
        const double y = QInputDialog::getDouble(
            this, "Hole", "Y (mm)",
            (ymin + ymax) * 0.5, -1000000.0, 1000000.0, 3, &ok);
        if (!ok) return;
        const double radius = QInputDialog::getDouble(
            this, "Hole",
            QString::fromUtf8("半徑 (mm)"),
            5.0, 0.001, 1000000.0, 3, &ok);
        if (!ok) return;
        const double defaultDepth = std::max(1.0, zmax - zmin);
        const double depth = QInputDialog::getDouble(
            this, "Hole",
            QString::fromUtf8("深度 (mm)"),
            defaultDepth, 0.001, 1000000.0, 3, &ok);
        if (!ok) return;

        try {
            TopoDS_Shape tool =
                BRepPrimAPI_MakeCylinder(
                    gp_Ax2(
                        gp_Pnt(x, y, zmax + 1.0),
                        gp_Dir(0.0, 0.0, -1.0)),
                    radius,
                    depth + 1.0).Shape();

            TopoDS_Shape result =
                BRepAlgoAPI_Cut(objects_[index].shape, tool).Shape();
            addShape(
                QString("Hole_%1").arg(++objectCounter_),
                ObjectKind::Solid,
                result,
                true);
            hideObject(index);
        } catch (const Standard_Failure& e) {
            showOcctError("Hole", e);
        }
    }

    void booleanFuse()
    {
        booleanOperation("Fuse");
    }

    void booleanCut()
    {
        booleanOperation("Cut");
    }

    void booleanCommon()
    {
        booleanOperation("Common");
    }

    void booleanOperation(const QString& operation)
    {
        const auto indices = selectedIndices();
        if (indices.size() != 2) {
            QMessageBox::information(
                this,
                operation,
                QString::fromUtf8("請在模型樹同時選取兩個物件。"));
            return;
        }

        try {
            TopoDS_Shape result;
            if (operation == "Fuse") {
                result = BRepAlgoAPI_Fuse(
                    objects_[indices[0]].shape,
                    objects_[indices[1]].shape).Shape();
            } else if (operation == "Cut") {
                result = BRepAlgoAPI_Cut(
                    objects_[indices[0]].shape,
                    objects_[indices[1]].shape).Shape();
            } else {
                result = BRepAlgoAPI_Common(
                    objects_[indices[0]].shape,
                    objects_[indices[1]].shape).Shape();
            }

            addShape(
                QString("%1_%2").arg(operation).arg(++objectCounter_),
                ObjectKind::Solid,
                result,
                true);
            hideObject(indices[0]);
            hideObject(indices[1]);
        } catch (const Standard_Failure& e) {
            showOcctError(operation, e);
        }
    }

    void filletSelected()
    {
        const int index = singleSelectedSolid();
        if (index < 0) {
            QMessageBox::information(
                this, "Fillet",
                QString::fromUtf8("請選取一個實體。"));
            return;
        }

        bool ok = false;
        const double radius = QInputDialog::getDouble(
            this, "Fillet",
            QString::fromUtf8("圓角半徑 (mm)"),
            2.0, 0.001, 1000000.0, 3, &ok);
        if (!ok) return;

        try {
            BRepFilletAPI_MakeFillet maker(objects_[index].shape);
            for (TopExp_Explorer exp(objects_[index].shape, TopAbs_EDGE);
                 exp.More(); exp.Next()) {
                maker.Add(radius, TopoDS::Edge(exp.Current()));
            }
            maker.Build();
            if (!maker.IsDone()) {
                throw Standard_Failure("Fillet failed");
            }
            addShape(
                QString("Fillet_%1").arg(++objectCounter_),
                ObjectKind::Solid,
                maker.Shape(),
                true);
            hideObject(index);
        } catch (const Standard_Failure& e) {
            showOcctError("Fillet", e);
        }
    }

    void chamferSelected()
    {
        const int index = singleSelectedSolid();
        if (index < 0) {
            QMessageBox::information(
                this, "Chamfer",
                QString::fromUtf8("請選取一個實體。"));
            return;
        }

        bool ok = false;
        const double distance = QInputDialog::getDouble(
            this, "Chamfer",
            QString::fromUtf8("倒角距離 (mm)"),
            2.0, 0.001, 1000000.0, 3, &ok);
        if (!ok) return;

        try {
            BRepFilletAPI_MakeChamfer maker(objects_[index].shape);
            for (TopExp_Explorer exp(objects_[index].shape, TopAbs_EDGE);
                 exp.More(); exp.Next()) {
                maker.Add(distance, TopoDS::Edge(exp.Current()));
            }
            maker.Build();
            if (!maker.IsDone()) {
                throw Standard_Failure("Chamfer failed");
            }
            addShape(
                QString("Chamfer_%1").arg(++objectCounter_),
                ObjectKind::Solid,
                maker.Shape(),
                true);
            hideObject(index);
        } catch (const Standard_Failure& e) {
            showOcctError("Chamfer", e);
        }
    }

    void moveSelected()
    {
        const auto indices = selectedIndices();
        if (indices.empty()) {
            return;
        }

        bool ok = false;
        const double dx = QInputDialog::getDouble(
            this, QString::fromUtf8("移動"), "dX (mm)",
            0.0, -1000000.0, 1000000.0, 3, &ok);
        if (!ok) return;
        const double dy = QInputDialog::getDouble(
            this, QString::fromUtf8("移動"), "dY (mm)",
            0.0, -1000000.0, 1000000.0, 3, &ok);
        if (!ok) return;
        const double dz = QInputDialog::getDouble(
            this, QString::fromUtf8("移動"), "dZ (mm)",
            0.0, -1000000.0, 1000000.0, 3, &ok);
        if (!ok) return;

        gp_Trsf trsf;
        trsf.SetTranslation(gp_Vec(dx, dy, dz));
        transformSelected(indices, trsf, "Move");
    }

    void rotateSelected()
    {
        const auto indices = selectedIndices();
        if (indices.empty()) {
            return;
        }

        bool ok = false;
        const QString axis = QInputDialog::getItem(
            this, QString::fromUtf8("旋轉物件"),
            QString::fromUtf8("旋轉軸"),
            {"X", "Y", "Z"}, 2, false, &ok);
        if (!ok) return;

        const double angleDeg = QInputDialog::getDouble(
            this, QString::fromUtf8("旋轉物件"),
            QString::fromUtf8("角度 (deg)"),
            90.0, -3600.0, 3600.0, 3, &ok);
        if (!ok) return;

        gp_Dir dir(0.0, 0.0, 1.0);
        if (axis == "X") dir = gp_Dir(1.0, 0.0, 0.0);
        if (axis == "Y") dir = gp_Dir(0.0, 1.0, 0.0);

        gp_Trsf trsf;
        trsf.SetRotation(
            gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), dir),
            angleDeg * std::acos(-1.0) / 180.0);
        transformSelected(indices, trsf, "Rotate");
    }

    void mirrorSelected(const QString& plane)
    {
        const auto indices = selectedIndices();
        if (indices.empty()) {
            return;
        }

        gp_Ax2 mirrorPlane(
            gp_Pnt(0.0, 0.0, 0.0),
            gp_Dir(0.0, 0.0, 1.0));

        if (plane == "YZ") {
            mirrorPlane = gp_Ax2(
                gp_Pnt(0.0, 0.0, 0.0),
                gp_Dir(1.0, 0.0, 0.0));
        } else if (plane == "XZ") {
            mirrorPlane = gp_Ax2(
                gp_Pnt(0.0, 0.0, 0.0),
                gp_Dir(0.0, 1.0, 0.0));
        }

        gp_Trsf trsf;
        trsf.SetMirror(mirrorPlane);
        transformSelected(indices, trsf, "Mirror");
    }

    void transformSelected(
        const std::vector<int>& indices,
        const gp_Trsf& trsf,
        const QString& prefix)
    {
        pushUndo();
        try {
            for (int index : indices) {
                TopoDS_Shape transformed =
                    BRepBuilderAPI_Transform(
                        objects_[index].shape,
                        trsf,
                        true).Shape();
                addShape(
                    QString("%1_%2").arg(prefix).arg(++objectCounter_),
                    objects_[index].kind,
                    transformed,
                    false);
            }
        } catch (const Standard_Failure& e) {
            undoStack_.pop_back();
            showOcctError(prefix, e);
        }
    }

    void linearPattern()
    {
        const auto indices = selectedIndices();
        if (indices.size() != 1) {
            QMessageBox::information(
                this,
                QString::fromUtf8("線性陣列"),
                QString::fromUtf8("請選取一個物件。"));
            return;
        }

        bool ok = false;
        const QString axis = QInputDialog::getItem(
            this, QString::fromUtf8("線性陣列"),
            QString::fromUtf8("方向"),
            {"X", "Y", "Z"}, 0, false, &ok);
        if (!ok) return;

        const int count = QInputDialog::getInt(
            this, QString::fromUtf8("線性陣列"),
            QString::fromUtf8("數量（包含原件）"),
            3, 2, 100, 1, &ok);
        if (!ok) return;

        const double spacing = QInputDialog::getDouble(
            this, QString::fromUtf8("線性陣列"),
            QString::fromUtf8("間距 (mm)"),
            25.0, -1000000.0, 1000000.0, 3, &ok);
        if (!ok) return;

        pushUndo();
        const int sourceIndex = indices.front();

        for (int i = 1; i < count; ++i) {
            gp_Vec vec(0.0, 0.0, 0.0);
            if (axis == "X") vec = gp_Vec(spacing * i, 0.0, 0.0);
            if (axis == "Y") vec = gp_Vec(0.0, spacing * i, 0.0);
            if (axis == "Z") vec = gp_Vec(0.0, 0.0, spacing * i);

            gp_Trsf trsf;
            trsf.SetTranslation(vec);
            TopoDS_Shape transformed =
                BRepBuilderAPI_Transform(
                    objects_[sourceIndex].shape,
                    trsf,
                    true).Shape();
            addShape(
                QString("Pattern_%1").arg(++objectCounter_),
                objects_[sourceIndex].kind,
                transformed,
                false);
        }
    }

    void deleteSelected()
    {
        auto indices = selectedIndices();
        if (indices.empty()) {
            return;
        }

        pushUndo();
        std::sort(indices.rbegin(), indices.rend());

        for (int index : indices) {
            view_->removeShape(objects_[index].presentation);
            objects_.erase(objects_.begin() + index);
        }

        rebuildTree();
        view_->fitAll();
    }

    void setSelectedVisibility(bool visible)
    {
        const auto indices = selectedIndices();
        if (indices.empty()) {
            return;
        }

        pushUndo();
        for (int index : indices) {
            objects_[index].visible = visible;
            view_->setVisible(objects_[index].presentation, visible);
        }
        rebuildTree();
    }

    void hideObject(int index)
    {
        if (index < 0 || index >= static_cast<int>(objects_.size())) {
            return;
        }
        objects_[index].visible = false;
        view_->setVisible(objects_[index].presentation, false);
        rebuildTree();
    }

    void addShape(
        const QString& name,
        ObjectKind kind,
        const TopoDS_Shape& shape,
        bool recordUndo)
    {
        if (shape.IsNull()) {
            QMessageBox::warning(
                this,
                "MyCAD",
                QString::fromUtf8("幾何運算產生空的結果。"));
            return;
        }

        if (recordUndo) {
            pushUndo();
        }

        ModelObject obj;
        obj.name = name;
        obj.kind = kind;
        obj.shape = shape;
        obj.presentation = view_->displayShape(shape, kind, true);

        objects_.push_back(obj);
        rebuildTree();

        if (kind == ObjectKind::Sketch) {
            view_->viewTop();
        } else {
            view_->fitAll();
        }

        statusBar()->showMessage(name, 2500);
    }

    void rebuildTree()
    {
        QSignalBlocker blocker(modelTree_);
        modelTree_->clear();

        auto* part = new QTreeWidgetItem(modelTree_);
        part->setText(0, "Part1");
        part->setData(0, Qt::UserRole, -100);

        auto* body = new QTreeWidgetItem(part);
        body->setText(0, "Body");
        body->setData(0, Qt::UserRole, -200);

        auto* origin = new QTreeWidgetItem(body);
        origin->setText(0, "Origin");
        origin->setData(0, Qt::UserRole, -300);

        auto addPlane = [origin](
                            const QString& name,
                            int code) {
            auto* plane = new QTreeWidgetItem(origin);
            plane->setText(0, name);
            plane->setData(0, Qt::UserRole, code);
            plane->setToolTip(
                0,
                QString::fromUtf8("基準面 — 選取後可建立 Sketch"));
        };

        addPlane("XY Plane", -101);
        addPlane("YZ Plane", -102);
        addPlane("ZX Plane", -103);

        QFont partFont = part->font(0);
        partFont.setBold(true);
        part->setFont(0, partFont);

        QFont bodyFont = body->font(0);
        bodyFont.setBold(true);
        body->setFont(0, bodyFont);

        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
            auto* item = new QTreeWidgetItem(body);
            item->setText(
                0,
                objects_[i].visible
                    ? objects_[i].name
                    : objects_[i].name + QString::fromUtf8(" [隱藏]"));
            item->setData(0, Qt::UserRole, i);
            item->setToolTip(
                0,
                kindName(objects_[i].kind));
        }

        modelTree_->expandAll();

        if (!objects_.empty()) {
            QTreeWidgetItem* last =
                body->child(body->childCount() - 1);
            last->setSelected(true);
            modelTree_->setCurrentItem(last);
        } else {
            QTreeWidgetItem* xyPlane = origin->child(0);
            xyPlane->setSelected(true);
            modelTree_->setCurrentItem(xyPlane);
        }

        blocker.unblock();
        updateProperties();
        syncViewportFromTree();
    }

    std::vector<int> selectedIndices() const
    {
        std::vector<int> indices;
        const auto items = modelTree_->selectedItems();
        indices.reserve(items.size());

        for (const auto* item : items) {
            const int index = item->data(0, Qt::UserRole).toInt();
            if (index >= 0 && index < static_cast<int>(objects_.size())) {
                indices.push_back(index);
            }
        }

        std::sort(indices.begin(), indices.end());
        indices.erase(
            std::unique(indices.begin(), indices.end()),
            indices.end());
        return indices;
    }

    int singleSelectedSolid() const
    {
        const auto indices = selectedIndices();
        if (indices.size() != 1) {
            return -1;
        }
        if (objects_[indices.front()].kind == ObjectKind::Sketch) {
            return -1;
        }
        return indices.front();
    }

    void updateProperties()
    {
        propertyTable_->setRowCount(0);

        const DatumPlane datumPlane = selectedDatumPlane();
        if (datumPlane != DatumPlane::None) {
            addProperty(
                QString::fromUtf8("類型"),
                QString::fromUtf8("基準面"));
            addProperty(
                QString::fromUtf8("名稱"),
                datumPlaneName(datumPlane));
            addProperty(
                QString::fromUtf8("用途"),
                QString::fromUtf8("選取後可建立 Sketch"));
            return;
        }

        const auto indices = selectedIndices();

        if (indices.size() != 1) {
            addProperty(
                QString::fromUtf8("選取"),
                QString::number(indices.size()));
            return;
        }

        const ModelObject& obj = objects_[indices.front()];
        addProperty(QString::fromUtf8("名稱"), obj.name);
        addProperty(QString::fromUtf8("類型"), kindName(obj.kind));
        addProperty(
            QString::fromUtf8("顯示"),
            obj.visible ? QString::fromUtf8("是") : QString::fromUtf8("否"));

        GProp_GProps volumeProps;
        GProp_GProps areaProps;
        GProp_GProps lengthProps;

        BRepGProp::VolumeProperties(obj.shape, volumeProps);
        BRepGProp::SurfaceProperties(obj.shape, areaProps);
        BRepGProp::LinearProperties(obj.shape, lengthProps);

        addProperty(
            QString::fromUtf8("體積 (mm³)"),
            QString::number(volumeProps.Mass(), 'f', 3));
        addProperty(
            QString::fromUtf8("表面積 (mm²)"),
            QString::number(areaProps.Mass(), 'f', 3));
        addProperty(
            QString::fromUtf8("邊長總和 (mm)"),
            QString::number(lengthProps.Mass(), 'f', 3));

        Bnd_Box box;
        BRepBndLib::Add(obj.shape, box);
        if (!box.IsVoid()) {
            double xmin, ymin, zmin, xmax, ymax, zmax;
            box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
            addProperty(
                QString::fromUtf8("尺寸 X"),
                QString::number(xmax - xmin, 'f', 3));
            addProperty(
                QString::fromUtf8("尺寸 Y"),
                QString::number(ymax - ymin, 'f', 3));
            addProperty(
                QString::fromUtf8("尺寸 Z"),
                QString::number(zmax - zmin, 'f', 3));
        }
    }

    void addProperty(const QString& name, const QString& value)
    {
        const int row = propertyTable_->rowCount();
        propertyTable_->insertRow(row);
        propertyTable_->setItem(row, 0, new QTableWidgetItem(name));
        propertyTable_->setItem(row, 1, new QTableWidgetItem(value));
    }

    void showMeasurement()
    {
        const auto indices = selectedIndices();
        if (indices.size() != 1) {
            QMessageBox::information(
                this,
                QString::fromUtf8("測量"),
                QString::fromUtf8("請選取一個物件。"));
            return;
        }

        const ModelObject& obj = objects_[indices.front()];
        GProp_GProps volumeProps;
        GProp_GProps areaProps;
        GProp_GProps lengthProps;
        BRepGProp::VolumeProperties(obj.shape, volumeProps);
        BRepGProp::SurfaceProperties(obj.shape, areaProps);
        BRepGProp::LinearProperties(obj.shape, lengthProps);

        Bnd_Box box;
        BRepBndLib::Add(obj.shape, box);

        QString text =
            QString::fromUtf8("名稱：%1\n類型：%2\n體積：%3 mm³\n表面積：%4 mm²\n邊長總和：%5 mm")
                .arg(obj.name)
                .arg(kindName(obj.kind))
                .arg(volumeProps.Mass(), 0, 'f', 3)
                .arg(areaProps.Mass(), 0, 'f', 3)
                .arg(lengthProps.Mass(), 0, 'f', 3);

        if (!box.IsVoid()) {
            double xmin, ymin, zmin, xmax, ymax, zmax;
            box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
            text += QString::fromUtf8(
                "\n包圍盒：%1 × %2 × %3 mm")
                .arg(xmax - xmin, 0, 'f', 3)
                .arg(ymax - ymin, 0, 'f', 3)
                .arg(zmax - zmin, 0, 'f', 3);
        }

        QMessageBox::information(
            this,
            QString::fromUtf8("幾何資訊"),
            text);
    }

    TopoDS_Shape exportShape() const
    {
        const auto indices = selectedIndices();
        if (indices.size() == 1) {
            return objects_[indices.front()].shape;
        }

        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        for (const auto& obj : objects_) {
            if (obj.kind != ObjectKind::Sketch) {
                builder.Add(compound, obj.shape);
            }
        }
        return compound;
    }

    void importStep()
    {
        const QString fileName = QFileDialog::getOpenFileName(
            this,
            QString::fromUtf8("匯入 STEP"),
            {},
            "STEP (*.step *.stp)");
        if (fileName.isEmpty()) return;

        STEPControl_Reader reader;
        const QByteArray path = fileName.toLocal8Bit();

        if (reader.ReadFile(path.constData()) != IFSelect_RetDone) {
            QMessageBox::critical(
                this, QString::fromUtf8("匯入失敗"),
                QString::fromUtf8("無法讀取 STEP 檔案。"));
            return;
        }

        reader.TransferRoots();
        TopoDS_Shape shape = reader.OneShape();
        if (shape.IsNull()) {
            return;
        }

        addShape(
            QString("STEP_%1").arg(++objectCounter_),
            ObjectKind::Imported,
            shape,
            true);
    }

    void importIges()
    {
        const QString fileName = QFileDialog::getOpenFileName(
            this,
            QString::fromUtf8("匯入 IGES"),
            {},
            "IGES (*.iges *.igs)");
        if (fileName.isEmpty()) return;

        IGESControl_Reader reader;
        const QByteArray path = fileName.toLocal8Bit();

        if (reader.ReadFile(path.constData()) != IFSelect_RetDone) {
            QMessageBox::critical(
                this, QString::fromUtf8("匯入失敗"),
                QString::fromUtf8("無法讀取 IGES 檔案。"));
            return;
        }

        reader.TransferRoots();
        TopoDS_Shape shape = reader.OneShape();
        if (shape.IsNull()) {
            return;
        }

        addShape(
            QString("IGES_%1").arg(++objectCounter_),
            ObjectKind::Imported,
            shape,
            true);
    }

    void importStl()
    {
        const QString fileName = QFileDialog::getOpenFileName(
            this,
            QString::fromUtf8("匯入 STL"),
            {},
            "STL (*.stl)");
        if (fileName.isEmpty()) return;

        TopoDS_Shape shape;
        StlAPI_Reader reader;
        const QByteArray path = fileName.toLocal8Bit();

        if (!reader.Read(shape, path.constData()) || shape.IsNull()) {
            QMessageBox::critical(
                this, QString::fromUtf8("匯入失敗"),
                QString::fromUtf8("無法讀取 STL 檔案。"));
            return;
        }

        addShape(
            QString("STL_%1").arg(++objectCounter_),
            ObjectKind::Imported,
            shape,
            true);
    }

    void importBrep()
    {
        const QString fileName = QFileDialog::getOpenFileName(
            this,
            QString::fromUtf8("匯入 BREP"),
            {},
            "BREP (*.brep *.brp)");
        if (fileName.isEmpty()) return;

        TopoDS_Shape shape;
        BRep_Builder builder;
        const QByteArray path = fileName.toLocal8Bit();

        if (!BRepTools::Read(shape, path.constData(), builder) ||
            shape.IsNull()) {
            QMessageBox::critical(
                this, QString::fromUtf8("匯入失敗"),
                QString::fromUtf8("無法讀取 BREP 檔案。"));
            return;
        }

        addShape(
            QString("BREP_%1").arg(++objectCounter_),
            ObjectKind::Imported,
            shape,
            true);
    }

    void exportStep()
    {
        if (objects_.empty()) {
            return;
        }

        QString fileName = QFileDialog::getSaveFileName(
            this,
            QString::fromUtf8("匯出 STEP"),
            "MyCAD.step",
            "STEP (*.step *.stp)");
        if (fileName.isEmpty()) return;
        if (!fileName.endsWith(".step", Qt::CaseInsensitive) &&
            !fileName.endsWith(".stp", Qt::CaseInsensitive)) {
            fileName += ".step";
        }

        STEPControl_Writer writer;
        if (writer.Transfer(exportShape(), STEPControl_AsIs) != IFSelect_RetDone) {
            QMessageBox::critical(this, "STEP", QString::fromUtf8("STEP 轉換失敗。"));
            return;
        }

        const QByteArray path = fileName.toLocal8Bit();
        if (writer.Write(path.constData()) != IFSelect_RetDone) {
            QMessageBox::critical(this, "STEP", QString::fromUtf8("STEP 寫入失敗。"));
        }
    }

    void exportIges()
    {
        if (objects_.empty()) return;

        QString fileName = QFileDialog::getSaveFileName(
            this,
            QString::fromUtf8("匯出 IGES"),
            "MyCAD.igs",
            "IGES (*.iges *.igs)");
        if (fileName.isEmpty()) return;
        if (!fileName.endsWith(".igs", Qt::CaseInsensitive) &&
            !fileName.endsWith(".iges", Qt::CaseInsensitive)) {
            fileName += ".igs";
        }

        IGESControl_Writer writer;
        writer.AddShape(exportShape());
        writer.ComputeModel();

        const QByteArray path = fileName.toLocal8Bit();
        if (!writer.Write(path.constData())) {
            QMessageBox::critical(this, "IGES", QString::fromUtf8("IGES 寫入失敗。"));
        }
    }

    void exportStl()
    {
        if (objects_.empty()) return;

        QString fileName = QFileDialog::getSaveFileName(
            this,
            QString::fromUtf8("匯出 STL"),
            "MyCAD.stl",
            "STL (*.stl)");
        if (fileName.isEmpty()) return;
        if (!fileName.endsWith(".stl", Qt::CaseInsensitive)) {
            fileName += ".stl";
        }

        StlAPI_Writer writer;
        const QByteArray path = fileName.toLocal8Bit();
        if (!writer.Write(exportShape(), path.constData())) {
            QMessageBox::critical(this, "STL", QString::fromUtf8("STL 寫入失敗。"));
        }
    }

    void exportBrep()
    {
        if (objects_.empty()) return;

        QString fileName = QFileDialog::getSaveFileName(
            this,
            QString::fromUtf8("匯出 BREP"),
            "MyCAD.brep",
            "BREP (*.brep *.brp)");
        if (fileName.isEmpty()) return;
        if (!fileName.endsWith(".brep", Qt::CaseInsensitive) &&
            !fileName.endsWith(".brp", Qt::CaseInsensitive)) {
            fileName += ".brep";
        }

        const QByteArray path = fileName.toLocal8Bit();
        if (!BRepTools::Write(exportShape(), path.constData())) {
            QMessageBox::critical(this, "BREP", QString::fromUtf8("BREP 寫入失敗。"));
        }
    }

    void showOcctError(const QString& operation, const Standard_Failure& error)
    {
        QString message = QString::fromUtf8("幾何運算失敗。");
        if (error.GetMessageString() != nullptr) {
            message += "\n";
            message += QString::fromUtf8(error.GetMessageString());
        }
        QMessageBox::critical(this, operation, message);
    }

    CadView* view_ = nullptr;
    QDockWidget* modelDock_ = nullptr;
    QTreeWidget* modelTree_ = nullptr;
    QComboBox* workbenchCombo_ = nullptr;
    QToolBar* sketchBar_ = nullptr;
    QToolBar* modelBar_ = nullptr;
    QToolBar* viewBar_ = nullptr;

    QDockWidget* propertyDock_ = nullptr;
    QTabWidget* propertyTabs_ = nullptr;
    QWidget* taskPage_ = nullptr;
    QLabel* taskTitleLabel_ = nullptr;
    QLabel* taskHelpLabel_ = nullptr;
    QLabel* taskValueLabel_ = nullptr;
    QDoubleSpinBox* taskValueSpin_ = nullptr;
    QCheckBox* taskReverse_ = nullptr;
    QPushButton* taskApplyButton_ = nullptr;
    QPushButton* taskCancelButton_ = nullptr;
    QTableWidget* propertyTable_ = nullptr;

    TaskKind taskKind_ = TaskKind::None;
    int taskPrimaryIndex_ = -1;
    int taskSecondaryIndex_ = -1;

    std::vector<ModelObject> objects_;
    std::vector<Snapshot> undoStack_;
    std::vector<Snapshot> redoStack_;
    int objectCounter_ = 0;
};

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("MyCAD");
    app.setOrganizationName("MyCAD Project");
    app.setApplicationVersion("0.4.0");

    MainWindow window;
    window.show();

    return app.exec();
}
