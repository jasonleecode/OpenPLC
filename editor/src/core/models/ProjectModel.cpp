#include "ProjectModel.h"

#include <QFile>
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
            graphElem.appendChild(doc.createCDATASection(pou->graphicalXml));
            pouElem.appendChild(graphElem);
        } else {
            QDomElement codeElem = doc.createElement("code");
            if (!pou->code.isEmpty())
                codeElem.appendChild(doc.createCDATASection(pou->code));
            pouElem.appendChild(codeElem);
        }

        root.appendChild(pouElem);
    }

    QFile file(path);
    if (!file.open(QFile::WriteOnly | QFile::Text))
        return false;

    QTextStream stream(&file);
    doc.save(stream, 2);

    filePath = path;
    m_dirty  = false;
    return true;
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

    QDomNodeList pouNodes = doc.elementsByTagName("pou");

    for (PouModel* pou : pous) {
        // 找到 name 匹配的 <pou> 节点
        for (int i = 0; i < pouNodes.count(); ++i) {
            QDomElement pn = pouNodes.at(i).toElement();
            if (pn.attribute("name") != pou->name) continue;

            QDomElement bodyElem = pn.firstChildElement("body");
            if (bodyElem.isNull()) break;

            if (!pou->graphicalXml.isEmpty()) {
                // 图形体（LD/FBD/SFC）：替换 <body> 的第一子节点
                int nl = pou->graphicalXml.indexOf('\n');
                const QString bodyXml = pou->graphicalXml.mid(nl + 1);
                QDomDocument bdoc;
                if (bdoc.setContent(bodyXml)) {
                    QDomElement newBodyChild = bdoc.documentElement();
                    // 清空旧子节点
                    while (!bodyElem.firstChild().isNull())
                        bodyElem.removeChild(bodyElem.firstChild());
                    // 导入并追加新节点
                    QDomNode imported = doc.importNode(newBodyChild, true);
                    bodyElem.appendChild(imported);
                }
            } else if (!pou->code.isEmpty()) {
                // 文本体（ST/IL）：更新 CDATA 内容
                updateStBody(doc, pn, pou->code);
            }
            break;
        }
    }

    QFile f(path);
    if (!f.open(QFile::WriteOnly | QFile::Text)) return false;
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    // doc.toString() 已包含原文档的 <?xml ...?> 声明，不需要手动补充
    ts << doc.toString(2);

    filePath = path;
    m_dirty  = false;
    return true;
}

// ── 更新 ST/IL body 中的 CDATA 内容 ──────────────────────────
void ProjectModel::updateStBody(QDomDocument& doc,
                                 QDomElement&  pouElem,
                                 const QString& code)
{
    QDomElement body = pouElem.firstChildElement("body");
    if (body.isNull()) return;
    QDomElement lang = body.firstChildElement(); // <ST> or <IL>
    if (lang.isNull()) return;

    // 找到第一个子元素（可能是 xhtml:p 或 p）
    QDomElement pElem;
    QDomNodeList children = lang.childNodes();
    for (int k = 0; k < children.count(); ++k) {
        pElem = children.at(k).toElement();
        if (!pElem.isNull()) break;
    }

    if (!pElem.isNull()) {
        while (!pElem.firstChild().isNull())
            pElem.removeChild(pElem.firstChild());
        pElem.appendChild(doc.createCDATASection(code));
    }
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
