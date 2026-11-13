#include "ui/MainWindow.h"
#include "ui/KesStyle.h"
#include "compile/MapCompiler.h"
#include "core/EditorContext.h"
#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QTextStream>
#include <QSurfaceFormat>

int main(int argc, char** argv)
{
    if (argc == 4 && QString::fromLocal8Bit(argv[1]) == "--compile")
    {
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName("Forge");
        QCoreApplication::setOrganizationName("kes");

        EditorContext context;
        QString error;
        // The project nearest to the map wins over the last project opened in the UI.
        const QString mapPath = QString::fromLocal8Bit(argv[2]);
        const QString projectPath = KesProject::findUpwards(QFileInfo(mapPath).absolutePath());
        if (!projectPath.isEmpty() && !context.loadProject(projectPath, &error, false))
            QTextStream(stderr) << "Warning: " << error << '\n';
        if (context.project().isValid())
            QTextStream(stdout) << "Project: " << context.project().filePath << '\n';
        if (!context.doc()->load(mapPath, &error))
        {
            QTextStream(stderr) << "Error: " << error << '\n';
            return 1;
        }

        CompileContext compile;
        compile.settings.outputPath = QString::fromLocal8Bit(argv[3]);
        compile.registry = context.registry();
        compile.materials = context.materials();
        compile.map = context.doc()->snapshot();
        const bool success = createMapCompiler()->compile(compile);
        for (const QString& line : compile.log)
            QTextStream(success ? stdout : stderr) << line << '\n';
        return success ? 0 : 1;
    }

    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QApplication::setApplicationName("Forge");
    QApplication::setOrganizationName("kes");
    kes::style::apply(app);

    MainWindow w;
    w.show();
    if (argc > 1) w.openFile(QString::fromLocal8Bit(argv[1]));
    return app.exec();
}
