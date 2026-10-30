#include "ProjectModel.h"

#include <QFile>
#include <QSaveFile>
#include <QSet>
#include <QMap>
#include <QTextStream>
#include <QDomDocument>
#include <QDomElement>
#include <QDateTime>

ProjectModel::ProjectModel(QObject* parent)
    : QObject(parent), projectName("Untitled")
{}

ProjectModel::~ProjectModel() {
    clear();
}

void ProjectModel::markDirty() {
    m_dirty = true;
    emit changed();
}

void ProjectModel::clearDirty() {
    m_dirty = false;
}

void ProjectModel::clear() {
    qDeleteAll(pous);
    pous.clear();
    projectName = "Untitled";
    filePath.clear();
    // metadata
    author.clear();
    companyName.clear();
    productVersion  = "1";
    description.clear();
    creationDateTime.clear();
    modificationDateTime.clear();
    // build settings
    targetType = "Linux";
    driver.clear();
    mode       = "NCC";
    compiler   = "gcc";
    cflags.clear();
    linker     = "gcc";
    ldflags.clear();
    m_dirty = false;
    m_sourcePlcOpen   = QDomDocument();
    m_isPlcOpenSource = false;
}

// -------------------------------------------------------
// POU 管理
// -------------------------------------------------------
PouModel* ProjectModel::addPou(const QString& name, PouType type, PouLanguage lang) {
    auto* pou = new PouModel(name, type, lang);
    pous.append(pou);
    m_dirty = true;
    emit pouAdded(pou);
    emit changed();
    return pou;
}

void ProjectModel::removePou(const QString& name) {
    for (int i = 0; i < pous.size(); ++i) {
        if (pous[i]->name == name) {
            delete pous.takeAt(i);
            m_dirty = true;
            emit pouRemoved(name);
            emit changed();
            return;
        }
    }
}

PouModel* ProjectModel::findPou(const QString& name) const {
    for (PouModel* p : pous)
        if (p->name == name) return p;
    return nullptr;
}

bool ProjectModel::pouNameExists(const QString& name) const {
    return findPou(name) != nullptr;
}

// -------------------------------------------------------
// XML 保存（路由到 PLCopen 或 TiZi 自有格式）
// -------------------------------------------------------
bool ProjectModel::saveToFile(const QString& path) {
    m_error.clear();
    for (auto* pou : pous) {
        if (pou->graphicalXml.isEmpty()) continue;
        QDomDocument graph;
        if (!graph.setContent(pou->graphicalXml.mid(pou->graphicalXml.indexOf('\n') + 1))) {
            m_error = "Invalid graphical XML in POU " + pou->name;
            return false;
        }
    }
    if (m_isPlcOpenSource)
        return savePlcOpen(path);
    return saveTiZiNative(path);
}

// ── TiZi 自有格式保存 ────────────────────────────────────────
bool ProjectModel::saveTiZiNative(const QString& path) {
    QDomDocument doc;
    QDomProcessingInstruction pi = doc.createProcessingInstruction(
        "xml", "version=\"1.0\" encoding=\"UTF-8\"");
    doc.appendChild(pi);

    QDomElement root = doc.createElement("TiZiProject");
    root.setAttribute("name",    projectName);
    root.setAttribute("version", "1");
    // 构建设置
    root.setAttribute("targetType", targetType);
    root.setAttribute("mode",       mode);
    if (!driver.isEmpty())
        root.setAttribute("driver", driver);
    root.setAttribute("author", author);
    root.setAttribute("companyName", companyName);
    root.setAttribute("productVersion", productVersion);
    root.setAttribute("description", description);
    root.setAttribute("creationDateTime", creationDateTime);
    root.setAttribute("compiler", compiler);
    root.setAttribute("cflags", cflags);
    root.setAttribute("linker", linker);
    root.setAttribute("ldflags", ldflags);
    doc.appendChild(root);

    for (PouModel* pou : pous) {
        QDomElement pouElem = doc.createElement("pou");
        pouElem.setAttribute("name",     pou->name);
        pouElem.setAttribute("type",     PouModel::typeToString(pou->pouType));
        pouElem.setAttribute("language", PouModel::langToString(pou->language));

        // description
        QDomElement descElem = doc.createElement("description");
        descElem.appendChild(doc.createTextNode(pou->description));
        pouElem.appendChild(descElem);

        // variables
        QDomElement varsElem = doc.createElement("variables");
        for (const VariableDecl& v : pou->variables) {
            QDomElement varElem = doc.createElement("var");
            varElem.setAttribute("name",    v.name);
            varElem.setAttribute("class",   v.varClass);
            varElem.setAttribute("type",    v.type);
            varElem.setAttribute("init",    v.initValue);
            varElem.setAttribute("comment", v.comment);
            varsElem.appendChild(varElem);
        }
        pouElem.appendChild(varsElem);

        // graphical body（LD/FBD/SFC）或文本 code（ST/IL）
        if (!pou->graphicalXml.isEmpty()) {
            // 保存图形 XML（"LD\n<LD>...</LD>" 格式）
            QDomElement graphElem = doc.createElement("graphical");
            graphElem.appendChild(doc.createTextNode(pou->graphicalXml));
            pouElem.appendChild(graphElem);
        } else {
            QDomElement codeElem = doc.createElement("code");
            if (!pou->code.isEmpty())
                codeElem.appendChild(doc.createTextNode(pou->code));
            pouElem.appendChild(codeElem);
        }

        root.appendChild(pouElem);
    }

    return writeDocument(path, doc);
}

bool ProjectModel::writeDocument(const QString& path, const QDomDocument& doc)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        m_error = file.errorString();
        return false;
    }
    const QByteArray bytes = doc.toByteArray(2);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        m_error = file.errorString();
        return false;
    }
    filePath = path;
    m_error.clear();
    clearDirty();
    emit changed();
    return true;
}

namespace {
QDomElement ensureChild(QDomDocument& doc, QDomElement parent, const QString& tag)
{
    QDomElement child = parent.firstChildElement(tag);
    if (child.isNull()) { child = doc.createElement(tag); parent.appendChild(child); }
    return child;
}
QString typeName(const QDomElement& variable)
{
    const auto type = variable.firstChildElement("type").firstChildElement();
    return type.tagName() == "derived" ? type.attribute("name") : type.tagName();
}
QString groupName(const QString& scope)
{
    static const QMap<QString, QString> groups = {
        {"Input", "inputVars"}, {"Output", "outputVars"}, {"InOut", "inOutVars"},
        {"External", "externalVars"}, {"Local", "localVars"}, {"Global", "globalVars"}};
    return groups.value(scope, "localVars");
}
bool isVarGroup(const QString& tag)
{
    return QStringList{"inputVars", "outputVars", "inOutVars", "externalVars",
                       "localVars", "globalVars", "tempVars"}.contains(tag);
}
QString documentation(const QDomElement& element)
{
    return element.firstChildElement("documentation").text().trimmed();
}
void setDocumentation(QDomDocument& doc, QDomElement element, const QString& text)
{
    if (documentation(element) == text) return;
    auto node = element.firstChildElement("documentation");
    if (text.isEmpty()) { element.removeChild(node); return; }
    node = ensureChild(doc, element, "documentation");
    while (!node.firstChild().isNull()) node.removeChild(node.firstChild());
    auto paragraph = doc.createElement("xhtml:p");
    paragraph.setAttribute("xmlns:xhtml", "http://www.w3.org/1999/xhtml");
    paragraph.appendChild(doc.createTextNode(text));
    node.appendChild(paragraph);
}
}

// ── PLCopen XML 格式保存（Beremiz 兼容）────────────────────────
bool ProjectModel::savePlcOpen(const QString& path) {
    // 以原始文档为基础进行克隆
    QDomDocument doc = m_sourcePlcOpen.cloneNode(true).toDocument();
    QDomElement  docRoot = doc.documentElement();

    // ── 更新 fileHeader ──────────────────────────────────────
    QDomElement fhEl = docRoot.firstChildElement("fileHeader");
    if (!fhEl.isNull()) {
        fhEl.setAttribute("companyName",    companyName);
        fhEl.setAttribute("author",         author);
        fhEl.setAttribute("productVersion", productVersion);
    }

    // ── 更新 contentHeader ───────────────────────────────────
    QDomElement chEl = docRoot.firstChildElement("contentHeader");
    if (!chEl.isNull()) {
        chEl.setAttribute("name",    projectName);
        chEl.setAttribute("comment", description);
        chEl.setAttribute("modificationDateTime",
                          QDateTime::currentDateTime().toString(Qt::ISODate));
    }

    // ── 更新/新增 TiZiBuild ──────────────────────────────────
    QDomElement buildEl = docRoot.firstChildElement("TiZiBuild");
    if (buildEl.isNull()) {
        buildEl = doc.createElement("TiZiBuild");
        // 插在 <instances> 之前（或末尾）
        QDomElement inst = docRoot.firstChildElement("instances");
        if (!inst.isNull()) docRoot.insertBefore(buildEl, inst);
        else docRoot.appendChild(buildEl);
    }
    buildEl.setAttribute("targetType", targetType);
    buildEl.setAttribute("driver",     driver);
    buildEl.setAttribute("mode",       mode);
    buildEl.setAttribute("compiler",   compiler);
    buildEl.setAttribute("cflags",     cflags);
    buildEl.setAttribute("linker",     linker);
    buildEl.setAttribute("ldflags",    ldflags);

    QDomElement types = ensureChild(doc, docRoot, "types");
    QDomElement pouList = ensureChild(doc, types, "pous");
    while (!pouList.firstChild().isNull()) pouList.removeChild(pouList.firstChild());

    QMap<QString, QString> renamed;
    QSet<QString> liveNames;
    for (PouModel* pou : pous) {
        liveNames.insert(pou->name);
        const QString oldName = pou->sourceElement.attribute("name");
        if (!oldName.isEmpty() && oldName != pou->name) renamed.insert(oldName, pou->name);
        QDomElement pn = pou->sourceElement.isNull() ? doc.createElement("pou")
            : doc.importNode(pou->sourceElement, true).toElement();
        pn.setAttribute("name", pou->name);
        pn.setAttribute("pouType", PouModel::typeToString(pou->pouType));
        setDocumentation(doc, pn, pou->description);
        QDomElement iface = ensureChild(doc, pn, "interface");
        for (QDomElement group = iface.firstChildElement(); !group.isNull();) {
            QDomElement next = group.nextSiblingElement();
            if (isVarGroup(group.tagName())) iface.removeChild(group);
            group = next;
        }
        // Keep the return type and other interface metadata. Rebuild only the
        // editable variable list; keep each variable's original group attributes.
        QMap<QString, QDomElement> groups;
        for (const VariableDecl& v : pou->variables) {
            QString tag = groupName(v.varClass);
            if (v.varClass == "Local" && v.sourceGroup.tagName() == "tempVars") tag = "tempVars";
            QDomElement group = doc.createElement(tag);
            const auto attrs = v.sourceGroup.attributes();
            QString key = tag;
            for (int i = 0; i < attrs.count(); ++i) {
                const auto attr = attrs.item(i).toAttr();
                group.setAttribute(attr.name(), attr.value());
                key += "|" + attr.name() + "=" + attr.value();
            }
            if (!groups.contains(key)) { groups.insert(key, group); iface.appendChild(group); }
            group = groups.value(key);
            QDomElement variable = v.sourceElement.isNull() ? doc.createElement("variable")
                : doc.importNode(v.sourceElement, true).toElement();
            variable.setAttribute("name", v.name);
            if (v.sourceElement.isNull() || typeName(v.sourceElement) != v.type) {
                auto type = ensureChild(doc, variable, "type");
                while (!type.firstChild().isNull()) type.removeChild(type.firstChild());
                const QSet<QString> builtin = {"BOOL", "SINT", "INT", "DINT", "LINT",
                    "USINT", "UINT", "UDINT", "ULINT", "REAL", "LREAL", "TIME", "DATE",
                    "TIME_OF_DAY", "TOD", "DATE_AND_TIME", "DT", "STRING", "WSTRING",
                    "BYTE", "WORD", "DWORD", "LWORD"};
                auto valueType = doc.createElement(builtin.contains(v.type) ? v.type : "derived");
                if (!builtin.contains(v.type)) valueType.setAttribute("name", v.type);
                type.appendChild(valueType);
            }
            const QString oldInit = v.sourceElement.firstChildElement("initialValue")
                .firstChildElement("simpleValue").attribute("value");
            if (v.sourceElement.isNull() || oldInit != v.initValue) {
                auto init = variable.firstChildElement("initialValue");
                if (!init.isNull()) variable.removeChild(init);
                if (!v.initValue.isEmpty()) {
                    init = doc.createElement("initialValue");
                    auto value = doc.createElement("simpleValue");
                    value.setAttribute("value", v.initValue);
                    init.appendChild(value); variable.appendChild(init);
                }
            }
            setDocumentation(doc, variable, v.comment);
            group.appendChild(variable);
        }
        auto body = ensureChild(doc, pn, "body");
        const QString language = PouModel::langToString(pou->language);
        if (pou->language == PouLanguage::ST || pou->language == PouLanguage::IL) {
            while (!body.firstChild().isNull()) body.removeChild(body.firstChild());
            auto lang = doc.createElement(language);
            auto paragraph = doc.createElement("xhtml:p");
            paragraph.setAttribute("xmlns:xhtml", "http://www.w3.org/1999/xhtml");
            paragraph.appendChild(doc.createTextNode(pou->code));
            lang.appendChild(paragraph); body.appendChild(lang);
        } else {
            QDomDocument graphical;
            if (!pou->graphicalXml.isEmpty()) {
                if (!graphical.setContent(pou->graphicalXml.mid(pou->graphicalXml.indexOf('\n') + 1))) {
                    m_error = "Invalid graphical XML in POU " + pou->name;
                    return false;
                }
            } else graphical.appendChild(graphical.createElement(language));
            while (!body.firstChild().isNull()) body.removeChild(body.firstChild());
            body.appendChild(doc.importNode(graphical.documentElement(), true));
        }
        pouList.appendChild(pn);
    }
    // Keep references attached to the imported POU identity on rename.
    for (const QString& tag : {QString("block"), QString("derived"), QString("pouInstance")}) {
    const auto all = doc.elementsByTagName(tag);
    for (int i = 0; i < all.count(); ++i) {
        auto e = all.at(i).toElement();
        const QString attr = e.tagName() == "derived" ? "name" : "typeName";
        const QString old = e.attribute(attr);
        if (renamed.contains(old)) e.setAttribute(attr, renamed.value(old));
    }
    }
    // Remove scheduled instances of deleted programs, leaving unrelated/vendor data intact.
    QSet<QString> oldNames;
    const auto oldPous = m_sourcePlcOpen.elementsByTagName("pou");
    for (int i = 0; i < oldPous.count(); ++i) oldNames.insert(oldPous.at(i).toElement().attribute("name"));
    const auto instances = doc.elementsByTagName("pouInstance");
    for (int i = instances.count() - 1; i >= 0; --i) {
        auto e = instances.at(i).toElement();
        const auto name = e.attribute("typeName");
        if (oldNames.contains(name) && !liveNames.contains(name) && !renamed.contains(name))
            e.parentNode().removeChild(e);
    }
    if (!writeDocument(path, doc)) return false;
    m_sourcePlcOpen = doc;
    // Refresh origins only after successful commit; subsequent saves must not
    // restore stale actions/interfaces or lose rename identity.
    for (PouModel* pou : pous) {
        for (auto pn = pouList.firstChildElement("pou"); !pn.isNull(); pn = pn.nextSiblingElement("pou")) {
            if (pn.attribute("name") != pou->name) continue;
            pou->sourceElement = pn;
            for (auto& v : pou->variables) {
                const auto variables = pn.firstChildElement("interface").elementsByTagName("variable");
                for (int i = 0; i < variables.count(); ++i) {
                    auto ve = variables.at(i).toElement();
                    if (ve.attribute("name") == v.name) {
                        v.sourceElement = ve; v.sourceGroup = ve.parentNode().toElement();
                        v.type = typeName(ve); break;
                    }
                }
            }
            if (!pou->graphicalXml.isEmpty()) {
                QDomDocument graph;
                graph.appendChild(graph.importNode(pn.firstChildElement("body").firstChildElement(), true));
                pou->graphicalXml = PouModel::langToString(pou->language) + "\n" + graph.toString(2);
            }
            break;
        }
    }
    return true;
}

// -------------------------------------------------------
// XML 读档
// -------------------------------------------------------
bool ProjectModel::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QFile::ReadOnly))
        return false;

    QDomDocument doc;
    if (!doc.setContent(&file))
        return false;

    clear();

    QDomElement root = doc.documentElement();

    // ── PLCopen XML 格式（Beremiz/TiZi 的 .tizi PLCopen 文件） ──
    if (root.tagName() == "project") {
        return loadPlcOpenXml(doc, path);
    }

    // ── TiZi 自有格式 ──
    if (root.tagName() != "TiZiProject")
        return false;

    projectName = root.attribute("name", "Untitled");
    targetType  = root.attribute("targetType", "Linux");
    mode        = root.attribute("mode", "NCC");
    driver      = root.attribute("driver");

    author = root.attribute("author", author);
    companyName = root.attribute("companyName", companyName);
    productVersion = root.attribute("productVersion", productVersion);
    description = root.attribute("description", description);
    creationDateTime = root.attribute("creationDateTime", creationDateTime);
    compiler = root.attribute("compiler", compiler);
    cflags = root.attribute("cflags", cflags);
    linker = root.attribute("linker", linker);
    ldflags = root.attribute("ldflags", ldflags);
    QDomNodeList pouNodes = root.elementsByTagName("pou");
    for (int i = 0; i < pouNodes.count(); ++i) {
        QDomElement pe = pouNodes.at(i).toElement();
        if (pe.isNull()) continue;

        const QString name = pe.attribute("name");
        PouType    type    = PouModel::typeFromString(pe.attribute("type",     "functionBlock"));
        PouLanguage lang   = PouModel::langFromString(pe.attribute("language", "LD"));

        PouModel* pou  = new PouModel(name, type, lang);
        pou->description = pe.firstChildElement("description").text();
        pou->code        = pe.firstChildElement("code").text();
        // 图形内容（LD/FBD/SFC），存在 <graphical> 元素中
        QDomElement graphElem = pe.firstChildElement("graphical");
        if (!graphElem.isNull())
            pou->graphicalXml = graphElem.text();

        QDomNodeList varNodes = pe.firstChildElement("variables").elementsByTagName("var");
        for (int j = 0; j < varNodes.count(); ++j) {
            QDomElement ve = varNodes.at(j).toElement();
            VariableDecl v;
            v.name      = ve.attribute("name");
            v.varClass  = ve.attribute("class");
            v.type      = ve.attribute("type");
            v.initValue = ve.attribute("init");
            v.comment   = ve.attribute("comment");
            pou->variables.append(v);
        }
        pous.append(pou);
    }

    filePath = path;
    m_dirty  = false;
    emit changed();
    return true;
}

// -------------------------------------------------------
// PLCopen XML 导入（IEC 61131-3 标准格式，Beremiz 兼容）
// -------------------------------------------------------
bool ProjectModel::loadPlcOpenXml(const QDomDocument& doc, const QString& path)
{
    QDomElement root = doc.documentElement(); // <project>

    // ── fileHeader ──
    QDomElement fh = root.firstChildElement("fileHeader");
    companyName    = fh.attribute("companyName");
    author         = fh.attribute("author");
    productVersion = fh.attribute("productVersion", "1");
    creationDateTime = fh.attribute("creationDateTime");

    // ── contentHeader ──
    QDomElement hdr = root.firstChildElement("contentHeader");
    projectName          = hdr.attribute("name", "Imported Project");
    modificationDateTime = hdr.attribute("modificationDateTime");
    description          = hdr.attribute("comment");

    // ── TiZiBuild (TiZi 扩展，可选) ──
    QDomElement build = root.firstChildElement("TiZiBuild");
    if (!build.isNull()) {
        targetType = build.attribute("targetType", "Linux");
        driver     = build.attribute("driver");
        mode       = build.attribute("mode", "NCC");
        compiler   = build.attribute("compiler",   "gcc");
        cflags     = build.attribute("cflags");
        linker     = build.attribute("linker",     "gcc");
        ldflags    = build.attribute("ldflags");
    }

    // 辅助函数：把 PLCopen varClass 组名映射到我们的字符串
    auto classStr = [](const QString& tagName) -> QString {
        if (tagName == "inputVars")    return "Input";
        if (tagName == "outputVars")   return "Output";
        if (tagName == "inOutVars")    return "InOut";
        if (tagName == "localVars")    return "Local";
        if (tagName == "externalVars") return "External";
        if (tagName == "globalVars")   return "Global";
        return "Local";
    };

    // 辅助函数：解析 <type><BOOL/>|<INT/>|<derived name="..."/> 等
    auto parseType = [](const QDomElement& typeElem) -> QString {
        QDomElement child = typeElem.firstChildElement();
        if (child.isNull()) return "BOOL";
        if (child.tagName() == "derived") return child.attribute("name");
        return child.tagName(); // BOOL INT REAL …
    };

    // ── 遍历 <types><pous><pou> ──
    QDomElement types = root.firstChildElement("types");
    QDomElement pousEl = types.firstChildElement("pous");
    QDomNodeList pouNodes = pousEl.elementsByTagName("pou");

    for (int i = 0; i < pouNodes.count(); ++i) {
        QDomElement pe = pouNodes.at(i).toElement();
        if (pe.isNull()) continue;

        const QString name    = pe.attribute("name");
        const QString pouType = pe.attribute("pouType"); // function/functionBlock/program
        PouType type = PouModel::typeFromString(pouType);

        // ── 解析接口变量 ──
        QDomElement iface = pe.firstChildElement("interface");
        QList<VariableDecl> vars;

        // 遍历所有 varGroup
        for (QDomElement grp = iface.firstChildElement();
             !grp.isNull(); grp = grp.nextSiblingElement()) {
            const QString cls = classStr(grp.tagName());
            QDomNodeList varNodes = grp.elementsByTagName("variable");
            for (int j = 0; j < varNodes.count(); ++j) {
                QDomElement ve = varNodes.at(j).toElement();
                VariableDecl v;
                v.sourceElement = ve;
                v.sourceGroup = grp;
                v.name     = ve.attribute("name");
                v.varClass = cls;
                v.type     = parseType(ve.firstChildElement("type"));
                // 初始值
                QDomElement iv = ve.firstChildElement("initialValue");
                if (!iv.isNull())
                    v.initValue = iv.firstChildElement("simpleValue").attribute("value");
                // 注释 (xhtml:p CDATA)
                QDomElement doc2 = ve.firstChildElement("documentation");
                if (!doc2.isNull()) {
                    QDomElement p = doc2.firstChildElement("p");
                    if (p.isNull()) {
                        // 带命名空间前缀
                        QDomNodeList ps = doc2.childNodes();
                        for (int k = 0; k < ps.count(); ++k) {
                            QDomElement pe2 = ps.at(k).toElement();
                            if (!pe2.isNull()) { v.comment = pe2.text().trimmed(); break; }
                        }
                    } else {
                        v.comment = p.text().trimmed();
                    }
                }
                vars.append(v);
            }
        }

        // ── 解析 body ──
        QDomElement body = pe.firstChildElement("body");
        QDomElement bodyChild = body.firstChildElement(); // ST / IL / LD / FBD / SFC
        const QString langTag = bodyChild.tagName().toUpper();

        PouLanguage lang = PouLanguage::ST;
        if (langTag == "ST")  lang = PouLanguage::ST;
        else if (langTag == "IL")  lang = PouLanguage::IL;
        else if (langTag == "LD")  lang = PouLanguage::LD;
        else if (langTag == "FBD") lang = PouLanguage::FBD;
        else if (langTag == "SFC") lang = PouLanguage::SFC;

        PouModel* pou = new PouModel(name, type, lang);
        pou->variables = vars;
        pou->sourceElement = pe;
        pou->description = documentation(pe);

        if (lang == PouLanguage::ST || lang == PouLanguage::IL) {
            // 文本体：从 <xhtml:p> CDATA 取内容
            QDomNodeList ps = bodyChild.childNodes();
            for (int j = 0; j < ps.count(); ++j) {
                QDomElement p = ps.at(j).toElement();
                if (!p.isNull()) {
                    pou->code = p.text();
                    break;
                }
            }
        } else {
            // 图形体：保存原始 XML 供渲染
            pou->graphicalXml = bodyChild.tagName() + "\n"; // lang prefix
            QDomDocument tmp;
            tmp.appendChild(tmp.importNode(bodyChild, true));
            pou->graphicalXml += tmp.toString(2);
        }

        pous.append(pou);
    }

    // 保留完整的原始 PLCopen 文档，供 savePlcOpen 使用
    m_sourcePlcOpen   = doc;
    m_isPlcOpenSource = true;

    filePath = path;
    m_dirty  = false;
    emit changed();
    return true;
}
