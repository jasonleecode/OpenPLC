#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSaveFile>
#include <QMessageBox>
#include <QAbstractButton>
#include <QFileDialog>
#include <QInputDialog>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QTableWidget>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QProcess>
#include <QCloseEvent>
#include <QTimer>
#include <QSettings>
#include "../src/app/MainWindow.h"
#include "../src/app/ProjectManager.h"
#include "../src/editor/scene/PlcOpenViewer.h"
#include "../src/editor/items/ContactItem.h"
#include "../src/editor/items/CoilItem.h"
#include "../src/editor/items/FunctionBlockItem.h"
#include "../src/editor/items/VarBoxItem.h"
#include "../src/utils/UndoStack.h"
#include "../src/core/compiler/StGenerator.h"

namespace {
void write(const QString& path, const QByteArray& content)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(content) != content.size()) qFatal("Cannot write fixture");
}
QByteArray read(const QString& path)
{
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
}
QDomDocument body(PlcOpenViewer& scene)
{
    const auto xml = scene.toXmlString(); QDomDocument doc;
    if (!doc.setContent(xml.mid(xml.indexOf('\n') + 1))) qFatal("Invalid scene XML");
    return doc;
}
QDomElement element(const QDomDocument& doc, int id)
{
    for (auto e = doc.documentElement().firstChildElement(); !e.isNull(); e = e.nextSiblingElement())
        if (e.attribute("localId").toInt() == id) return e;
    return {};
}
template<class T> T* item(PlcOpenViewer& scene, int id = -1)
{
    for (auto* gi : scene.items())
        if (auto* result = dynamic_cast<T*>(gi); result && (id == -1 || gi->data(0).toInt() == id)) return result;
    return nullptr;
}
void mouse(PlcOpenViewer& scene, QEvent::Type type, QPointF point, Qt::MouseButton button = Qt::LeftButton)
{
    QGraphicsSceneMouseEvent ev(type);
    ev.setScenePos(point); ev.setLastScenePos(point); ev.setButton(button);
    ev.setButtons(type == QEvent::GraphicsSceneMouseRelease ? Qt::NoButton : button);
    QApplication::sendEvent(&scene, &ev);
}
void chooseMessage(QMessageBox::StandardButton button, std::function<void()> next = {})
{
    QTimer::singleShot(0, [button, next] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box || !box->button(button)) qFatal("Expected message box/button");
        box->button(button)->click();
        if (next) QTimer::singleShot(0, next);
    });
}
QString ldBody()
{
    return R"(LD
<LD><contact localId="1" negated="false" edge="rising" width="30" height="20"><position x="100" y="100"/><connectionPointIn><relPosition x="0" y="10"/></connectionPointIn><connectionPointOut><relPosition x="30" y="10"/></connectionPointOut><variable>X</variable></contact>
<coil localId="2" negated="true" width="30" height="20"><position x="200" y="100"/><connectionPointIn><relPosition x="0" y="10"/><connection refLocalId="1"><position x="200" y="110"/><position x="130" y="110"/></connection></connectionPointIn><connectionPointOut/><variable>Y</variable></coil>
<contact localId="3" negated="false" width="30" height="20"><position x="100" y="200"/><connectionPointIn/><connectionPointOut/><variable>Z</variable></contact>
<comment localId="90"><content><p>keep extension/comment</p></content></comment></LD>)";
}
}

class EditorPersistenceTest : public QObject {
    Q_OBJECT
private slots:
    void projectRoundtrip();
    void atomicFailureAndNativeMetadata();
    void importedSceneHistory();
    void newSceneWireAndReconnection();
    void namedFbdPortsAndUnexposedXml();
    void savedFixtureSemantics();
    void savedProgramExecutes();
    void unsavedChoices();
    void uiDirtyAndSaveFailure();
};

void EditorPersistenceTest::projectRoundtrip()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto path = dir.filePath("project.xml");
    write(path, R"(<project><fileHeader/><contentHeader name="Audit"/><types><dataTypes><dataType name="Custom"><baseType><INT/></baseType></dataType></dataTypes><pous>
<pou name="main" pouType="program"><interface><localVars retain="true"><variable name="X" address="%IX0.0"><type><BOOL/></type><initialValue><simpleValue value="TRUE"/></initialValue></variable><variable name="A"><type><array><dimension lower="0" upper="9"/><baseType><INT/></baseType></array></type></variable></localVars></interface><body><ST><p>X := TRUE;</p></ST></body><actions><action name="Keep"><body><ST><p>X := FALSE;</p></ST></body></action></actions><addData><vendor/></addData></pou>
<pou name="old" pouType="functionBlock"><interface/><body><ST><p/></ST></body></pou></pous></types><instances><configurations><configuration name="config"><resource name="resource1"><task name="t" interval="T#10ms"><pouInstance name="main_instance" typeName="main"/></task></resource></configuration></configurations></instances></project>)");
    ProjectModel p; QVERIFY(p.loadFromFile(path));
    auto* main = p.findPou("main"); QVERIFY(main);
    main->name = "renamed"; main->description = "描述 < & >"; main->code.clear();
    main->variables[0].name = "Changed"; main->variables[0].initValue.clear();
    main->variables[0].comment = "输入 <&>";
    main->variables.append({"NewVar", "Output", "INT", "7", "new"});
    p.removePou("old");
    auto* added = p.addPou("added", PouType::FunctionBlock, PouLanguage::ST);
    added->code = "(* 新增 *)";
    QVERIFY2(p.saveToFile(path), qPrintable(p.errorString()));
    QVERIFY(p.saveToFile(path)); // A second save must not reintroduce the original DOM.
    ProjectModel loaded; QVERIFY(loaded.loadFromFile(path));
    QVERIFY(!loaded.findPou("main")); QVERIFY(!loaded.findPou("old"));
    QVERIFY(loaded.findPou("added"));
    auto* reloaded = loaded.findPou("renamed"); QVERIFY(reloaded);
    QVERIFY(reloaded->code.isEmpty()); QCOMPARE(reloaded->description, main->description);
    QCOMPARE(reloaded->variables.size(), 3);
    QCOMPARE(reloaded->variables[0].name, QString("Changed"));
    QVERIFY(reloaded->variables[0].initValue.isEmpty());
    QCOMPARE(reloaded->variables[0].comment, QString("输入 <&>"));
    QDomDocument doc; QVERIFY(doc.setContent(read(path)));
    QCOMPARE(doc.elementsByTagName("dimension").count(), 1);
    QCOMPARE(doc.elementsByTagName("vendor").count(), 1);
    QCOMPARE(doc.elementsByTagName("action").count(), 1);
    QCOMPARE(doc.elementsByTagName("dataType").count(), 1);
    QCOMPARE(doc.elementsByTagName("localVars").at(0).toElement().attribute("retain"), QString("true"));
    QCOMPARE(doc.elementsByTagName("pouInstance").at(0).toElement().attribute("typeName"), QString("renamed"));
    QCOMPARE(reloaded->variables[0].sourceElement.attribute("address"), QString("%IX0.0"));
    reloaded->variables.removeAt(0);
    QVERIFY(loaded.saveToFile(path));
    ProjectModel again; QVERIFY(again.loadFromFile(path));
    QCOMPARE(again.findPou("renamed")->variables.size(), 2);
    QCOMPARE(again.findPou("renamed")->variables.first().name, QString("A"));
}

void EditorPersistenceTest::atomicFailureAndNativeMetadata()
{
    QTemporaryDir dir;
    ProjectModel p;
    auto* main = p.addPou("main", PouType::Program, PouLanguage::ST);
    main->code = "(* < & 中文 *)";
    p.author = "author"; p.description = "description"; p.cflags = "-O2";
    p.compiler = "custom-cc"; p.driver = "linux"; p.mode = "NCC";
    const auto path = dir.filePath("native.tizi");
    QVERIFY(p.saveToFile(path)); const auto original = read(path);
    ProjectModel reloaded; QVERIFY(reloaded.loadFromFile(path));
    QCOMPARE(reloaded.author, p.author); QCOMPARE(reloaded.description, p.description);
    QCOMPARE(reloaded.cflags, p.cflags); QCOMPARE(reloaded.compiler, p.compiler);
    QCOMPARE(reloaded.findPou("main")->code, main->code);
    main->graphicalXml = "LD\n<LD>";
    p.markDirty(); QVERIFY(!p.saveToFile(path));
    QCOMPARE(read(path), original); QVERIFY(p.isDirty());
    QVERIFY(!p.errorString().isEmpty());
    main->graphicalXml.clear();
    QVERIFY(!p.saveToFile(dir.path())); // Existing directory: deterministic failure even as root.
    QCOMPARE(p.filePath, path); QVERIFY(p.isDirty()); QCOMPARE(read(path), original);
}

void EditorPersistenceTest::importedSceneHistory()
{
    PlcOpenViewer scene; scene.loadFromXmlString(ldBody());
    auto* contact = item<ContactItem>(scene, 1); auto* coil = item<CoilItem>(scene, 2);
    auto* wire = item<WireItem>(scene);
    QVERIFY(contact); QVERIFY(coil); QVERIFY(wire);
    QCOMPARE(contact->contactType(), ContactItem::PositiveTransition);
    QCOMPARE(coil->coilType(), CoilItem::Negated);
    QSignalSpy dirty(&scene, &LadderScene::contentChanged);
    editItemText(contact, contact->tagName(), "Changed", [contact](const QString& v) { contact->setTagName(v); });
    QCOMPARE(element(body(scene), 1).firstChildElement("variable").text(), QString("Changed"));
    scene.undoStack()->undo(); QCOMPARE(element(body(scene), 1).firstChildElement("variable").text(), QString("X"));
    scene.undoStack()->redo(); QVERIFY(dirty.count() > 0);
    const auto before = contact->pos(); const auto pathBefore = wire->path();
    scene.undoStack()->push(new MoveItemsCmd({{contact, before, before + QPointF(40, 40)}}, "Move"));
    QVERIFY(wire->path() != pathBefore);
    QCOMPARE(wire->path().pointAtPercent(0), contact->rightPort());
    scene.undoStack()->undo(); QCOMPARE(contact->pos(), before);
    scene.undoStack()->push(new DeleteItemsCmd(&scene, {contact}, "Delete"));
    QVERIFY(element(body(scene), 1).isNull());
    QCOMPARE(body(scene).elementsByTagName("connection").count(), 0);
    scene.undoStack()->undo();
    QVERIFY(!element(body(scene), 1).isNull());
    QCOMPARE(body(scene).elementsByTagName("connection").count(), 1);
    scene.addFunctionBlock("TON", QPointF(400, 400));
    QCOMPARE(body(scene).elementsByTagName("block").count(), 1);
    scene.undoStack()->undo(); QCOMPARE(body(scene).elementsByTagName("block").count(), 0);
    // Branch history destroys the detached block; no stale item/connection pointers may remain.
    scene.addFunctionBlock("CTU", QPointF(400, 400));
    QCOMPARE(body(scene).elementsByTagName("block").count(), 1);
    QCOMPARE(body(scene).elementsByTagName("comment").count(), 1);
    PlcOpenViewer reloaded; reloaded.loadFromXmlString(scene.toXmlString());
    QCOMPARE(item<ContactItem>(reloaded, 1)->tagName(), QString("Changed"));
    QCOMPARE(body(reloaded).elementsByTagName("connection").count(), 1);
    // Reset with an undone command must not double-delete scene objects.
    scene.undoStack()->undo(); scene.loadFromXmlString(ldBody());
    QVERIFY(item<WireItem>(scene));
}

void EditorPersistenceTest::newSceneWireAndReconnection()
{
    PlcOpenViewer scene; scene.initEmpty("FBD");
    scene.addFunctionBlock("CTU", QPointF(300, 100));
    auto* fb = item<FunctionBlockItem>(scene); QVERIFY(fb);
    auto xml = body(scene); QVERIFY(xml.elementsByTagName("inputVariables").count() > 0);
    QCOMPARE(xml.elementsByTagName("outputVariables").at(0).toElement().elementsByTagName("variable").count(), fb->outputCount());
    scene.undoStack()->undo(); QCOMPARE(body(scene).elementsByTagName("block").count(), 0);
    scene.undoStack()->redo(); QCOMPARE(body(scene).elementsByTagName("block").count(), 1);

    scene.loadFromXmlString(ldBody());
    auto* c = item<ContactItem>(scene, 1); auto* z = item<ContactItem>(scene, 3);
    auto* w = item<WireItem>(scene); QVERIFY(c && z && w);
    // Reconnect using the production scene mouse-event path and undo it.
    mouse(scene, QEvent::GraphicsSceneMousePress, c->rightPort());
    mouse(scene, QEvent::GraphicsSceneMouseRelease, z->rightPort());
    QCOMPARE(body(scene).elementsByTagName("connection").at(0).toElement().attribute("refLocalId"), QString("3"));
    scene.undoStack()->undo();
    QCOMPARE(body(scene).elementsByTagName("connection").at(0).toElement().attribute("refLocalId"), QString("1"));
    scene.undoStack()->redo();
    PlcOpenViewer reloaded; reloaded.loadFromXmlString(scene.toXmlString());
    QCOMPARE(body(reloaded).elementsByTagName("connection").at(0).toElement().attribute("refLocalId"), QString("3"));

    scene.undoStack()->push(new DeleteItemsCmd(&scene, {w}, "Delete Wire"));
    QCOMPARE(body(scene).elementsByTagName("connection").count(), 0);
    scene.setMode(Mode_AddWire);
    mouse(scene, QEvent::GraphicsSceneMousePress, c->rightPort());
    mouse(scene, QEvent::GraphicsSceneMousePress, QPointF(330, 330));
    mouse(scene, QEvent::GraphicsSceneMousePress, item<CoilItem>(scene, 2)->leftPort());
    mouse(scene, QEvent::GraphicsSceneMousePress, QPointF(), Qt::RightButton);
    QCOMPARE(body(scene).elementsByTagName("connection").count(), 1);
    scene.undoStack()->undo(); QCOMPARE(body(scene).elementsByTagName("connection").count(), 0);
    scene.undoStack()->redo(); QCOMPARE(body(scene).elementsByTagName("connection").count(), 1);
}

void EditorPersistenceTest::namedFbdPortsAndUnexposedXml()
{
    PlcOpenViewer scene; scene.initEmpty("FBD");
    scene.addFunctionBlock("CTU", QPointF(200, 200));
    auto* fb = item<FunctionBlockItem>(scene); QVERIFY(fb);
    auto* output = new VarBoxItem("Count", VarBoxItem::OutVar);
    output->setData(0, 12000); output->setPos(600, 400);
    scene.undoStack()->push(new AddItemCmd(&scene, output, "Add Output"));
    scene.setMode(Mode_AddWire);
    const int cv = fb->outputPortIndex("CV"); QVERIFY(cv >= 0);
    // Drawing backwards must still preserve output→input orientation and CV (not first output ENO).
    mouse(scene, QEvent::GraphicsSceneMousePress, output->leftPort());
    mouse(scene, QEvent::GraphicsSceneMousePress, QPointF(450, 400));
    mouse(scene, QEvent::GraphicsSceneMousePress, fb->outputPortPos(cv));
    mouse(scene, QEvent::GraphicsSceneMousePress, {}, Qt::RightButton);
    auto doc = body(scene);
    QCOMPARE(doc.elementsByTagName("connection").count(), 1);
    QCOMPARE(doc.elementsByTagName("connection").at(0).toElement().attribute("formalParameter"), QString("CV"));
    PlcOpenViewer loaded; loaded.loadFromXmlString(scene.toXmlString());
    QCOMPARE(body(loaded).elementsByTagName("connection").at(0).toElement().attribute("formalParameter"), QString("CV"));
    loaded.undoStack()->push(new DeleteItemsCmd(&loaded, {item<FunctionBlockItem>(loaded)}, "Delete"));
    QCOMPARE(body(loaded).elementsByTagName("connection").count(), 0);
    loaded.undoStack()->undo();
    QCOMPARE(body(loaded).elementsByTagName("connection").at(0).toElement().attribute("formalParameter"), QString("CV"));

    // A connector is not rendered by this editor yet, but an unrelated edit
    // must not discard its topology or vendor metadata during save.
    loaded.loadFromXmlString(R"(FBD
<FBD><connector localId="7" name="bus"><position x="10" y="10"/></connector>
<outVariable localId="8"><position x="100" y="100"/><expression>Before</expression><connectionPointIn><connection refLocalId="7" vendor="kept"><position x="100" y="100"/><position x="10" y="10"/><addData><vendor/></addData></connection></connectionPointIn></outVariable></FBD>)");
    item<VarBoxItem>(loaded)->setExpression("After");
    doc = body(loaded);
    QCOMPARE(doc.elementsByTagName("connector").count(), 1);
    QCOMPARE(doc.elementsByTagName("connection").count(), 1);
    QCOMPARE(doc.elementsByTagName("connection").at(0).toElement().attribute("vendor"), QString("kept"));
    QCOMPARE(doc.elementsByTagName("vendor").count(), 1);
    QCOMPARE(doc.elementsByTagName("expression").at(0).toElement().text(), QString("After"));

    loaded.loadFromXmlString(R"(FBD
<FBD><connector localId="90000" name="bus"/><block localId="1" typeName="ADD" instanceName="ImportedName"><position x="10" y="10"/><inputVariables/><outputVariables/></block></FBD>)");
    QCOMPARE(item<FunctionBlockItem>(loaded, 1)->instanceName(), QString("ImportedName"));
    loaded.addFunctionBlock("ADD", QPointF(500, 300));
    doc = body(loaded);
    QCOMPARE(element(doc, 90000).tagName(), QString("connector"));
    QCOMPARE(element(doc, 90001).tagName(), QString("block"));
    QVERIFY(!element(doc, 90001).hasAttribute("instanceName"));
    QCOMPARE(element(doc, 1).attribute("instanceName"), QString("ImportedName"));

    auto combined = ldBody();
    combined.replace("negated=\"false\" edge=\"rising\"", "negated=\"true\" edge=\"rising\"");
    combined.replace("<coil localId=\"2\"", "<coil storage=\"set\" localId=\"2\"");
    loaded.loadFromXmlString(combined);
    doc = body(loaded);
    QCOMPARE(element(doc, 1).attribute("negated"), QString("true"));
    QCOMPARE(element(doc, 1).attribute("edge"), QString("rising"));
    QCOMPARE(element(doc, 2).attribute("negated"), QString("true"));
    QCOMPARE(element(doc, 2).attribute("storage"), QString("set"));

    const auto sfc = QString("SFC\n<SFC><step localId=\"1\" name=\"Start\" initialStep=\"true\"><position x=\"10\" y=\"20\"/></step></SFC>");
    loaded.loadFromXmlString(sfc); QVERIFY(loaded.isReadOnly());
    const auto unchanged = loaded.toXmlString();
    loaded.addFunctionBlock("TON", QPointF(200, 200));
    QCOMPARE(loaded.toXmlString(), unchanged);
    loaded.setMode(Mode_AddWire); QCOMPARE(loaded.currentMode(), Mode_Select);
}

void EditorPersistenceTest::savedFixtureSemantics()
{
    QTemporaryDir dir;
    const QString source = TIZI_TEST_SOURCE_DIR;
    QStringList fixtures;
    for (const auto& path : {source + "/tests/fixtures", source + "/tests/first_steps"})
        for (const auto& name : QDir(path).entryList({"*.tizi"}, QDir::Files)) fixtures << path + "/" + name;
    for (const auto& fixture : fixtures) {
        ProjectModel p; QVERIFY2(p.loadFromFile(fixture), qPrintable(fixture));
        const auto original = StGenerator::fromFile(fixture);
        QVERIFY2(!original.isEmpty(), qPrintable(fixture));
        for (auto* pou : p.pous) {
            if (pou->graphicalXml.isEmpty()) continue;
            PlcOpenViewer scene; scene.loadFromXmlString(pou->graphicalXml);
            pou->graphicalXml = scene.toXmlString();
        }
        const auto saved = dir.filePath(QFileInfo(fixture).fileName());
        QVERIFY2(p.saveToFile(saved), qPrintable(p.errorString()));
        const auto generated = StGenerator::fromFile(saved);
        QCOMPARE(generated, original);
        ProjectModel reload; QVERIFY(reload.loadFromFile(saved));
        QVERIFY(reload.saveToFile(saved));
        QCOMPARE(StGenerator::fromFile(saved), original);
    }
}

void EditorPersistenceTest::savedProgramExecutes()
{
    QTemporaryDir dir;
    ProjectModel p; auto* main = p.addPou("main", PouType::Program, PouLanguage::LD);
    main->variables = {{"X", "Local", "BOOL", "", ""}, {"Y", "Local", "BOOL", "", ""}, {"Z", "Local", "BOOL", "", ""}};
    PlcOpenViewer scene; scene.loadFromXmlString(ldBody());
    // Change the input that actually drives the output, save and run the new program.
    item<ContactItem>(scene, 1)->setTagName("Z");
    main->graphicalXml = scene.toXmlString();
    const auto saved = dir.filePath("program.tizi"); QVERIFY(p.saveToFile(saved));
    const auto st = StGenerator::fromFile(saved); QVERIFY(!st.isEmpty());
    const auto stFile = dir.filePath("program.st"); write(stFile, st.toUtf8());
    const QString matiec = MATIEC_DIR;
    auto run = [](const QString& program, const QStringList& args) {
        QProcess proc; proc.start(program, args);
        if (!proc.waitForStarted() || !proc.waitForFinished(30000)) return QByteArray("process timeout");
        if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode())
            return QByteArray("exit=") + QByteArray::number(proc.exitCode()) + proc.readAllStandardError() + proc.readAllStandardOutput();
        return QByteArray();
    };
    QCOMPARE(run(matiec + "/iec2c", {"-p", "-i", "-I", matiec + "/lib", "-T", dir.path(), stFile}), QByteArray());
    const auto testC = dir.filePath("test.c");
    write(testC, R"(#include "iec_std_lib.h"
#include "POUS.h"
TIME __CURRENT_TIME; BOOL __DEBUG = 0;
extern void config_init__(void); extern void config_run__(unsigned long);
extern MAIN RESOURCE1__MAIN_INSTANCE;
int main(void) {
 config_init__();
 RESOURCE1__MAIN_INSTANCE.X.value = 1;
 RESOURCE1__MAIN_INSTANCE.Z.value = 0;
 config_run__(0);
 if (!RESOURCE1__MAIN_INSTANCE.Y.value) return 1;
 RESOURCE1__MAIN_INSTANCE.Z.value = 1;
 config_run__(1);
 if (RESOURCE1__MAIN_INSTANCE.Y.value) return 2;
 config_run__(2);
 return RESOURCE1__MAIN_INSTANCE.Y.value ? 0 : 3;
})");
    const auto binary = dir.filePath("program");
    QCOMPARE(run("cc", {"-w", "-I", matiec + "/lib/C", "-I", dir.path(), testC,
        dir.filePath("config.c"), dir.filePath("resource1.c"), "-lm", "-o", binary}), QByteArray());
    QCOMPARE(run(binary, {}), QByteArray());

    // Also execute a newly drawn function graph, rather than only imported LD.
    ProjectModel fbd; auto* program = fbd.addPou("main", PouType::Program, PouLanguage::FBD);
    program->variables = {{"Result", "Local", "INT", "", ""}};
    PlcOpenViewer graph; graph.initEmpty("FBD");
    graph.addFunctionBlock("ADD", QPointF(300, 100));
    auto* add = item<FunctionBlockItem>(graph); QVERIFY(add); QVERIFY(add->instanceName().isEmpty());
    auto box = [&](const QString& expression, VarBoxItem::Role role, QPointF position, int id) {
        auto* v = new VarBoxItem(expression, role); v->setData(0, id); v->setPos(position);
        graph.undoStack()->push(new AddItemCmd(&graph, v, "Add Variable")); return v;
    };
    auto* a = box("INT#2", VarBoxItem::InVar, QPointF(0, 100), 20000);
    auto* b = box("INT#3", VarBoxItem::InVar, QPointF(0, 240), 20001);
    auto* result = box("Result", VarBoxItem::OutVar, QPointF(600, 100), 20002);
    auto connectPorts = [&](QPointF from, QPointF to) {
        graph.setMode(Mode_AddWire);
        mouse(graph, QEvent::GraphicsSceneMousePress, from);
        mouse(graph, QEvent::GraphicsSceneMousePress, to);
        mouse(graph, QEvent::GraphicsSceneMousePress, {}, Qt::RightButton);
    };
    connectPorts(a->rightPort(), add->inputPortPos(add->inputPortIndex("IN1")));
    connectPorts(b->rightPort(), add->inputPortPos(add->inputPortIndex("IN2")));
    connectPorts(add->outputPortPos(add->outputPortIndex("OUT")), result->leftPort());
    program->graphicalXml = graph.toXmlString();
    QCOMPARE(body(graph).elementsByTagName("connection").count(), 3);
    const auto fbdPath = dir.filePath("fbd.tizi"); QVERIFY(fbd.saveToFile(fbdPath));
    const auto fbdSt = StGenerator::fromFile(fbdPath); QVERIFY(!fbdSt.isEmpty());
    write(stFile, fbdSt.toUtf8());
    QCOMPARE(run(matiec + "/iec2c", {"-p", "-i", "-I", matiec + "/lib", "-T", dir.path(), stFile}), QByteArray());
    write(testC, R"(#include "iec_std_lib.h"
#include "POUS.h"
TIME __CURRENT_TIME;
BOOL __DEBUG = 0;
extern void config_init__(void);
extern void config_run__(unsigned long tick);
extern MAIN RESOURCE1__MAIN_INSTANCE;
int main(void) { config_init__(); config_run__(0); return RESOURCE1__MAIN_INSTANCE.RESULT.value == 5 ? 0 : 1; }
)");
    QCOMPARE(run("cc", {"-w", "-I", matiec + "/lib/C", "-I", dir.path(), testC,
        dir.filePath("config.c"), dir.filePath("resource1.c"), "-lm", "-o", binary}), QByteArray());
    QCOMPARE(run(binary, {}), QByteArray());
}

void EditorPersistenceTest::unsavedChoices()
{
    QTemporaryDir dir;
    ProjectModel fixture; fixture.addPou("target", PouType::Program, PouLanguage::ST);
    const auto target = dir.filePath("target.tizi"); QVERIFY(fixture.saveToFile(target));
    ProjectManager manager(nullptr); ProjectModel* current = nullptr;
    connect(&manager, &ProjectManager::projectCreated, &manager, [&](ProjectModel* p) { p->setParent(&manager); current = p; });
    manager.buildDefaultProject(); QVERIFY(current); auto* original = current;
    current->filePath = dir.filePath("original.tizi"); current->markDirty();
    chooseMessage(QMessageBox::Cancel); QVERIFY(!manager.openProject(target)); QCOMPARE(current, original);
    current->filePath = dir.path();
    chooseMessage(QMessageBox::Save, [] { chooseMessage(QMessageBox::Ok); });
    QVERIFY(!manager.openProject(target)); QCOMPARE(current, original); QVERIFY(current->isDirty());
    current->filePath.clear();
    chooseMessage(QMessageBox::Save, [] {
        QTimer::singleShot(0, [] {
            auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (!dialog) qFatal("Expected Save As dialog"); dialog->reject();
        });
    });
    QVERIFY(!manager.confirmSaveChanges()); QVERIFY(current->isDirty());
    current->filePath = dir.filePath("original.tizi");
    chooseMessage(QMessageBox::Save); QVERIFY(manager.openProject(target)); QVERIFY(current != original);
    QVERIFY(QFileInfo::exists(dir.filePath("original.tizi")));
    current->markDirty(); auto* beforeDiscard = current;
    chooseMessage(QMessageBox::Discard); QVERIFY(manager.openProject(target)); QVERIFY(current != beforeDiscard);
    current->markDirty();
    chooseMessage(QMessageBox::Cancel); manager.newProject(); QVERIFY(current->isDirty());
    current->findPou("target")->code = "(* latest edit *)";
    chooseMessage(QMessageBox::Save); QVERIFY(manager.openProject(target));
    QCOMPARE(current->findPou("target")->code, QString("(* latest edit *)"));
}

void EditorPersistenceTest::uiDirtyAndSaveFailure()
{
    QTemporaryDir dir;
    MainWindow window;
    auto* project = window.m_project; QVERIFY(project);
    auto* st = project->findPou("CounterST"); QVERIFY(st);
    window.openPouTab(st);
    auto* editor = window.findChild<QPlainTextEdit*>("stEditor"); QVERIFY(editor);
    project->clearDirty(); editor->setPlainText("Out := 17;"); QVERIFY(project->isDirty());
    auto* table = editor->parentWidget()->findChild<QTableWidget*>("varTable");
    if (!table) table = window.findChild<QTableWidget*>("varTable");
    QVERIFY(table); project->clearDirty(); table->item(0, 1)->setText("Changed"); QVERIFY(project->isDirty());
    project->clearDirty();
    auto* scene = window.m_sceneMap.first(); QVERIFY(scene);
    scene->addFunctionBlock("TON", QPointF(200, 200)); QVERIFY(project->isDirty());
    project->clearDirty(); scene->undoStack()->undo(); QVERIFY(project->isDirty());
    // All executable entry points must stop if the attempted save fails.
    project->filePath = dir.path();
    chooseMessage(QMessageBox::Ok); window.buildProject();
    QVERIFY(window.m_lastBuildOutput.isEmpty());
    QVERIFY(!window.m_consoleEdit->toPlainText().contains("Generating IEC"));
    chooseMessage(QMessageBox::Ok); window.openSmartSim(); QVERIFY(!window.m_smartSimWindow);
    window.m_connState = PlcConnState::Connected; window.m_plcUri = "SIM://SmartSim";
    chooseMessage(QMessageBox::Ok); window.downloadProject(); QVERIFY(!window.m_smartSimWindow);
    QCloseEvent cancelled;
    chooseMessage(QMessageBox::Cancel); QApplication::sendEvent(&window, &cancelled); QVERIFY(!cancelled.isAccepted());
    QCloseEvent discarded;
    chooseMessage(QMessageBox::Discard); QApplication::sendEvent(&window, &discarded); QVERIFY(discarded.isAccepted());
    // Replace a project while the deferred fit-to-view callback is pending.
    project->clearDirty();
    ProjectModel replacement; replacement.addPou("fresh", PouType::Program, PouLanguage::LD);
    const auto path = dir.filePath("replacement.tizi"); QVERIFY(replacement.saveToFile(path));
    QVERIFY(window.m_projectManager->openProject(path));
    QTest::qWait(70);
    QVERIFY(window.m_project->findPou("fresh"));
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    app.setOrganizationName("TiZiTests"); app.setApplicationName("Persistence");
    EditorPersistenceTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "editor_persistence_test.moc"
