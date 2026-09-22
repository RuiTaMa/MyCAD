#pragma once

#include <QString>

#include <AIS_Shape.hxx>
#include <TopoDS_Shape.hxx>

#include <vector>

enum class ObjectKind
{
    Solid,
    Sketch,
    Imported
};

enum class SketchTool
{
    None,
    Line,
    Rectangle,
    Circle
};

enum class TaskKind
{
    None,
    Sketch,
    Pad,
    Pocket
};

enum class DatumPlane
{
    None,
    XY,
    YZ,
    ZX
};

struct ModelObject
{
    QString name;
    ObjectKind kind = ObjectKind::Solid;
    TopoDS_Shape shape;
    Handle(AIS_Shape) presentation;
    bool visible = true;
};

struct SnapshotObject
{
    QString name;
    ObjectKind kind = ObjectKind::Solid;
    TopoDS_Shape shape;
    bool visible = true;
};

using Snapshot = std::vector<SnapshotObject>;
