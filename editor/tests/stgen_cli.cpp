#include "src/core/compiler/StGenerator.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        QTextStream(stderr) << "usage: stgen_cli <project.tizi|project.xml>\n";
        return 2;
    }

    const QString st = StGenerator::fromFile(QString::fromLocal8Bit(argv[1]));
    if (st.isEmpty()) {
        QTextStream(stderr) << StGenerator::lastError() << "\n";
        return 1;
    }

    QTextStream(stdout) << st;
    return 0;
}
