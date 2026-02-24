#include "PouModel.h"

PouModel::PouModel(const QString& name, PouType type, PouLanguage lang)
    : name(name), pouType(type), language(lang)
{}

QString PouModel::typeToString(PouType t) {
    switch (t) {
    case PouType::Program:       return "program";
    case PouType::FunctionBlock: return "functionBlock";
    case PouType::Function:      return "function";
    }
    return "functionBlock";
}

PouType PouModel::typeFromString(const QString& s) {
    if (s == "program")       return PouType::Program;
    if (s == "function")      return PouType::Function;
    return PouType::FunctionBlock;
}

QString PouModel::langToString(PouLanguage l) {
    switch (l) {
    case PouLanguage::LD:  return "LD";
    case PouLanguage::ST:  return "ST";
    case PouLanguage::IL:  return "IL";
    case PouLanguage::FBD: return "FBD";
    case PouLanguage::SFC: return "SFC";
    }
    return "LD";
}

PouLanguage PouModel::langFromString(const QString& s) {
    if (s == "ST")  return PouLanguage::ST;
    if (s == "IL")  return PouLanguage::IL;
    if (s == "FBD") return PouLanguage::FBD;
    if (s == "SFC") return PouLanguage::SFC;
    return PouLanguage::LD;
}

QString PouModel::langTabPrefix(PouLanguage l) {
    return langToString(l);
}
