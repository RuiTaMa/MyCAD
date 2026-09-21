#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QFileDialog>
#include <QInputDialog>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEngine>
#include <QResizeEvent>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeWidget>
#include <QWheelEvent>
#include <QWidget>

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <BRep_Builder.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Quantity_Color.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_StepModelType.hxx>
#include <STEPControl_Writer.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <V3d_TypeOfOrientation.hxx>
#include <WNT_Window.hxx>

#include <vector>

struct ModelObject
{
    QString name;
    TopoDS_Shape shape;
    Handle(AIS_Shape) presentation;
};

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

    Handle(AIS_Shape) displayShape(const TopoDS_Shape& shape)
    {
        Handle(AIS_Shape) presentation = new AIS_Shape(shape);
        context_->Display(presentation, true);
        context_->SetDisplayMode(presentation, AIS_Shaded, true);
        fitAll();
        return presentation;
    }

    void removeShape(const Handle(AIS_Shape)& presentation)
    {
        if (!presentation.IsNull()) {
            context_->Remove(presentation, true);
        }
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

    void viewFront()
    {
        view_->SetProj(V3d_Yneg);
        fitAll();
    }

    void viewRight()
    {
        view_->SetProj(V3d_Xpos);
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
        lastMousePos_ = toOcctPoint(event->position());

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
        } else {
            context_->MoveTo(current.x(), current.y(), view_, false);
        }

        lastMousePos_ = current;
        update();
        event->accept();
    }

    void wheelEvent(QWheelEvent* event) override
    {
        const double factor = event->angleDelta().y() > 0 ? 1.15 : (1.0 / 1.15);
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

    void initializeViewer()
    {
        Handle(Aspect_DisplayConnection) displayConnection = new Aspect_DisplayConnection();
        driver_ = new OpenGl_GraphicDriver(displayConnection);

        viewer_ = new V3d_Viewer(driver_);
        viewer_->SetDefaultLights();
        viewer_->SetLightOn();
        viewer_->SetDefaultBackgroundColor(
            Quantity_Color(0.15, 0.17, 0.20, Quantity_TOC_RGB));

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

    QPoint lastMousePos_;
    bool rotating_ = false;
    bool panning_ = false;
};

class MainWindow final : public QMainWindow
{
public:
    MainWindow()
    {
        setWindowTitle("MyCAD V0.1.1");
        resize(1400, 900);

        view_ = new CadView(this);
        setCentralWidget(view_);

        modelDock_ = new QDockWidget(QString::fromUtf8("模型樹"), this);
        modelTree_ = new QTreeWidget(modelDock_);
        modelTree_->setHeaderLabel("MyCAD Model");
        modelDock_->setWidget(modelTree_);
        addDockWidget(Qt::LeftDockWidgetArea, modelDock_);

        buildMenus();
        statusBar()->showMessage(
            QString::fromUtf8("中鍵拖曳：旋轉｜Shift+中鍵：平移｜滾輪：縮放｜右鍵拖曳：旋轉"));

        addBox(100.0, 60.0, 20.0);
    }

private:
    void buildMenus()
    {
        QMenu* fileMenu = menuBar()->addMenu(QString::fromUtf8("檔案"));
        QMenu* modelMenu = menuBar()->addMenu(QString::fromUtf8("建模"));
        QMenu* viewMenu = menuBar()->addMenu(QString::fromUtf8("視圖"));

        QToolBar* modelBar = addToolBar(QString::fromUtf8("建模"));
        QToolBar* viewBar = addToolBar(QString::fromUtf8("視圖"));

        QAction* newAct = new QAction(QString::fromUtf8("新建"), this);
        QAction* importStepAct = new QAction(QString::fromUtf8("匯入 STEP"), this);
        QAction* exportStepAct = new QAction(QString::fromUtf8("匯出 STEP"), this);

        connect(newAct, &QAction::triggered, this, [this]() { newDocument(); });
        connect(importStepAct, &QAction::triggered, this, [this]() { importStep(); });
        connect(exportStepAct, &QAction::triggered, this, [this]() { exportStep(); });

        fileMenu->addAction(newAct);
        fileMenu->addSeparator();
        fileMenu->addAction(importStepAct);
        fileMenu->addAction(exportStepAct);

        QAction* boxAct = new QAction(QString::fromUtf8("方塊"), this);
        QAction* cylinderAct = new QAction(QString::fromUtf8("圓柱"), this);
        QAction* deleteAct = new QAction(QString::fromUtf8("刪除"), this);

        connect(boxAct, &QAction::triggered, this, [this]() { createBox(); });
        connect(cylinderAct, &QAction::triggered, this, [this]() { createCylinder(); });
        connect(deleteAct, &QAction::triggered, this, [this]() { deleteSelected(); });

        modelMenu->addActions({boxAct, cylinderAct, deleteAct});
        modelBar->addActions({boxAct, cylinderAct, deleteAct});

        QAction* axoAct = new QAction(QString::fromUtf8("等角"), this);
        QAction* topAct = new QAction(QString::fromUtf8("上視"), this);
        QAction* frontAct = new QAction(QString::fromUtf8("前視"), this);
        QAction* rightAct = new QAction(QString::fromUtf8("右視"), this);
        QAction* fitAct = new QAction("Fit All", this);

        connect(axoAct, &QAction::triggered, view_, [this]() { view_->viewAxo(); });
        connect(topAct, &QAction::triggered, view_, [this]() { view_->viewTop(); });
        connect(frontAct, &QAction::triggered, view_, [this]() { view_->viewFront(); });
        connect(rightAct, &QAction::triggered, view_, [this]() { view_->viewRight(); });
        connect(fitAct, &QAction::triggered, view_, [this]() { view_->fitAll(); });

        viewMenu->addActions({axoAct, topAct, frontAct, rightAct, fitAct});
        viewBar->addActions({axoAct, topAct, frontAct, rightAct, fitAct});
    }

    void newDocument()
    {
        objects_.clear();
        modelTree_->clear();
        view_->clearScene();
        objectCounter_ = 0;
        setWindowTitle("MyCAD V0.1");
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

        addBox(x, y, z);
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

        TopoDS_Shape shape = BRepPrimAPI_MakeCylinder(radius, height).Shape();
        addShape(QString("Cylinder_%1").arg(++objectCounter_), shape);
    }

    void addBox(double x, double y, double z)
    {
        TopoDS_Shape shape = BRepPrimAPI_MakeBox(x, y, z).Shape();
        addShape(QString("Box_%1").arg(++objectCounter_), shape);
    }

    void addShape(const QString& name, const TopoDS_Shape& shape)
    {
        if (shape.IsNull()) {
            return;
        }

        ModelObject obj;
        obj.name = name;
        obj.shape = shape;
        obj.presentation = view_->displayShape(shape);

        objects_.push_back(obj);
        rebuildTree();
        statusBar()->showMessage(name, 2500);
    }

    void rebuildTree()
    {
        modelTree_->clear();

        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
            auto* item = new QTreeWidgetItem(modelTree_);
            item->setText(0, objects_[i].name);
            item->setData(0, Qt::UserRole, i);
        }

        if (modelTree_->topLevelItemCount() > 0) {
            modelTree_->setCurrentItem(
                modelTree_->topLevelItem(modelTree_->topLevelItemCount() - 1));
        }
    }

    int selectedIndex() const
    {
        const auto items = modelTree_->selectedItems();
        if (items.isEmpty()) {
            return -1;
        }
        return items.front()->data(0, Qt::UserRole).toInt();
    }

    void deleteSelected()
    {
        const int index = selectedIndex();
        if (index < 0 || index >= static_cast<int>(objects_.size())) {
            return;
        }

        view_->removeShape(objects_[index].presentation);
        objects_.erase(objects_.begin() + index);
        rebuildTree();
        view_->fitAll();
    }

    TopoDS_Shape compoundShape() const
    {
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);

        for (const auto& obj : objects_) {
            builder.Add(compound, obj.shape);
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

        if (fileName.isEmpty()) {
            return;
        }

        STEPControl_Reader reader;
        const QByteArray path = fileName.toLocal8Bit();

        if (reader.ReadFile(path.constData()) != IFSelect_RetDone) {
            QMessageBox::critical(
                this,
                QString::fromUtf8("匯入失敗"),
                QString::fromUtf8("無法讀取 STEP 檔案。"));
            return;
        }

        reader.TransferRoots();
        TopoDS_Shape shape = reader.OneShape();

        if (shape.IsNull()) {
            QMessageBox::critical(
                this,
                QString::fromUtf8("匯入失敗"),
                QString::fromUtf8("STEP 檔案沒有可用幾何。"));
            return;
        }

        addShape(QString("STEP_%1").arg(++objectCounter_), shape);
    }

    void exportStep()
    {
        if (objects_.empty()) {
            QMessageBox::information(
                this,
                QString::fromUtf8("沒有模型"),
                QString::fromUtf8("目前沒有可匯出的幾何。"));
            return;
        }

        QString fileName = QFileDialog::getSaveFileName(
            this,
            QString::fromUtf8("匯出 STEP"),
            "MyCAD.step",
            "STEP (*.step *.stp)");

        if (fileName.isEmpty()) {
            return;
        }

        if (!fileName.endsWith(".step", Qt::CaseInsensitive) &&
            !fileName.endsWith(".stp", Qt::CaseInsensitive)) {
            fileName += ".step";
        }

        STEPControl_Writer writer;
        const TopoDS_Shape shape = compoundShape();

        if (writer.Transfer(shape, STEPControl_AsIs) != IFSelect_RetDone) {
            QMessageBox::critical(
                this,
                QString::fromUtf8("匯出失敗"),
                QString::fromUtf8("無法轉換 STEP 幾何。"));
            return;
        }

        const QByteArray path = fileName.toLocal8Bit();

        if (writer.Write(path.constData()) != IFSelect_RetDone) {
            QMessageBox::critical(
                this,
                QString::fromUtf8("匯出失敗"),
                QString::fromUtf8("無法寫入 STEP 檔案。"));
            return;
        }

        statusBar()->showMessage(
            QString::fromUtf8("已匯出 STEP：") + fileName,
            4000);
    }

    CadView* view_ = nullptr;
    QDockWidget* modelDock_ = nullptr;
    QTreeWidget* modelTree_ = nullptr;
    std::vector<ModelObject> objects_;
    int objectCounter_ = 0;
};

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("MyCAD");
    app.setOrganizationName("MyCAD Project");
    app.setApplicationVersion("0.1.1");

    MainWindow window;
    window.show();

    return app.exec();
}
