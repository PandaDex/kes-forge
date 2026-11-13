#include "ui/AssetBrowser.h"
#include "ui/KesStyle.h"
#include "assets/AssetEditors.h"
#include "assets/AssetImporter.h"
#include "assets/AssetTemplates.h"
#include "core/EditorContext.h"
#include "tools/MaterialTool.h"
#include <QDateTime>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTimer>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
    constexpr int kThumb = 64;

    bool isCompiledAsset(const QFileInfo &info)
    {
        return info.completeBaseName().endsWith("_c", Qt::CaseInsensitive) ||
               info.suffix().compare("fmapc", Qt::CaseInsensitive) == 0;
    }

    QIcon materialThumbnail(const MaterialInfo &info)
    {
        QPixmap pixmap(kThumb, kThumb);
        if (!info.texture.isNull())
        {
            QImage image = info.texture.scaled(kThumb, kThumb, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                               .convertToFormat(QImage::Format_ARGB32);
            QPainter p(&image);
            p.setCompositionMode(QPainter::CompositionMode_Multiply);
            p.fillRect(image.rect(), info.color);
            p.end();
            pixmap = QPixmap::fromImage(image);
        }
        else
        {
            pixmap.fill(info.color);
        }
        QPainter p(&pixmap);
        p.setPen(QColor(0, 0, 0, 120));
        p.drawRect(0, 0, kThumb - 1, kThumb - 1);
        return QIcon(pixmap);
    }

    QColor typeColor(const QString &suffix)
    {
        static const QHash<QString, QColor> colors = {
            {"fmap", QColor(200, 70, 60)},
            {"kmodel", QColor(70, 130, 210)}, {"fbx", QColor(70, 130, 210)}, {"gltf", QColor(70, 130, 210)},
            {"glb", QColor(70, 130, 210)}, {"obj", QColor(70, 130, 210)},
            {"kanim", QColor(150, 90, 200)},
            {"ktex", QColor(70, 160, 90)}, {"png", QColor(70, 160, 90)}, {"jpg", QColor(70, 160, 90)},
            {"jpeg", QColor(70, 160, 90)}, {"bmp", QColor(70, 160, 90)}, {"tif", QColor(70, 160, 90)},
            {"tiff", QColor(70, 160, 90)},
            {"ksound", QColor(220, 140, 50)}, {"wav", QColor(220, 140, 50)}, {"mp3", QColor(220, 140, 50)},
            {"ogg", QColor(220, 140, 50)},
            {"rml", QColor(50, 160, 170)}, {"rcss", QColor(50, 160, 170)},
            {"slangnodes", QColor(200, 170, 50)}, {"slang", QColor(200, 170, 50)}, {"shader", QColor(200, 170, 50)},
        };
        return colors.value(suffix, QColor(100, 100, 110));
    }

    QIcon typeIcon(const QString &suffix)
    {
        static QHash<QString, QIcon> cache;
        auto cached = cache.constFind(suffix);
        if (cached != cache.cend())
            return cached.value();
        QPixmap pixmap(kThumb, kThumb);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(typeColor(suffix).darker(160));
        p.drawRoundedRect(QRectF(6, 4, kThumb - 12, kThumb - 8), 6, 6);
        p.setBrush(typeColor(suffix));
        p.drawRoundedRect(QRectF(6, kThumb - 24, kThumb - 12, 20), 4, 4);
        QFont font = p.font();
        font.setBold(true);
        font.setPixelSize(suffix.size() > 5 ? 9 : 12);
        p.setFont(font);
        p.setPen(Qt::white);
        p.drawText(QRectF(6, kThumb - 24, kThumb - 12, 20), Qt::AlignCenter, suffix.toUpper());
        p.end();
        return cache.insert(suffix, QIcon(pixmap)).value();
    }
}

// Thumbnails are made here, on the GUI thread; a QFileSystemModel icon provider runs on a worker thread.
class AssetFilterModel : public QSortFilterProxyModel
{
public:
    AssetFilterModel(EditorContext *ctx, QFileSystemModel *files, QObject *parent)
        : QSortFilterProxyModel(parent), m_ctx(ctx), m_files(files)
    {
        setSourceModel(files);
    }

    QString materialPath(const QModelIndex &index) const
    {
        const QFileInfo info = m_files->fileInfo(mapToSource(index));
        if (!info.isFile() || info.suffix().compare("mat", Qt::CaseInsensitive) != 0)
            return {};
        const QString root = m_ctx->materials()->root();
        if (root.isEmpty())
            return {};
        const QString relative = QDir(root).relativeFilePath(info.absoluteFilePath()).replace('\\', '/');
        return relative.startsWith("../") ? QString() : relative;
    }

    void clearThumbnails(const QModelIndex &parent)
    {
        m_thumbnails.clear();
        const int rows = rowCount(parent);
        if (rows > 0)
            emit dataChanged(index(0, 0, parent), index(rows - 1, 0, parent), {Qt::DecorationRole});
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == Qt::DecorationRole && index.column() == 0)
        {
            const QString material = materialPath(index);
            if (!material.isEmpty())
            {
                auto cached = m_thumbnails.constFind(material);
                if (cached != m_thumbnails.cend())
                    return cached.value();
                if (const MaterialInfo *info = m_ctx->materials()->find(material))
                {
                    const QIcon icon = materialThumbnail(*info);
                    m_thumbnails.insert(material, icon);
                    return icon;
                }
            }
            const QFileInfo info = m_files->fileInfo(mapToSource(index));
            if (info.isFile())
                return typeIcon(info.suffix().toLower());
        }
        return QSortFilterProxyModel::data(index, role);
    }

    Qt::DropActions supportedDragActions() const override { return Qt::CopyAction; }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        const QModelIndex index = m_files->index(row, 0, parent);
        const QFileInfo info = m_files->fileInfo(index);
        return info.isDir() || !isCompiledAsset(info);
    }

private:
    EditorContext *m_ctx;
    QFileSystemModel *m_files;
    mutable QHash<QString, QIcon> m_thumbnails;
};

AssetBrowser::AssetBrowser(EditorContext *ctx, AssetImporter *importer, QWidget *parent)
    : QWidget(parent), m_ctx(ctx), m_importer(importer)
{
    m_dirs = new QFileSystemModel(this);
    m_dirs->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
    m_files = new QFileSystemModel(this);
    m_files->setFilter(QDir::Files);
    m_files->setNameFilterDisables(false);
    m_proxy = new AssetFilterModel(ctx, m_files, this);

    m_tree = new QTreeView(this);
    m_tree->setModel(m_dirs);
    for (int c = 1; c < 4; ++c)
        m_tree->hideColumn(c);
    m_tree->setHeaderHidden(true);

    m_list = new QListView(this);
    m_list->setModel(m_proxy);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(kThumb, kThumb));
    m_list->setGridSize(QSize(kThumb + 40, kThumb + 34));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setWordWrap(true);
    m_list->setUniformItemSizes(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    // External drops are imported (eventFilter); the view itself never takes drops.
    m_list->setAcceptDrops(true);
    m_list->viewport()->setAcceptDrops(true);
    m_list->viewport()->installEventFilter(this);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Filter files..."));
    m_filter->addAction(kes::style::icon("search"), QLineEdit::LeadingPosition);
    m_filter->setClearButtonEnabled(true);

    auto *refresh = new QToolButton(this);
    refresh->setText(tr("Refresh"));
    refresh->setToolTip(tr("Rescan materials and refresh their thumbnails"));

    m_current = new QLabel(this);
    m_current->setToolTip(tr("Material used by the Material tool and given to new brushes"));

    auto *importButton = new QToolButton(this);
    importButton->setText(tr("Import..."));
    importButton->setToolTip(tr("Import models, animations, textures and sounds into the project"));
    auto *newButton = new QToolButton(this);
    newButton->setText(tr("New"));
    newButton->setToolTip(tr("Create an asset from a template"));
    newButton->setPopupMode(QToolButton::InstantPopup);
    auto *newMenu = new QMenu(newButton);
    newButton->setMenu(newMenu);
    connect(newMenu, &QMenu::aboutToShow, this, [this, newMenu]
            {
        newMenu->clear();
        fillNewMenu(newMenu); });
    connect(importButton, &QToolButton::clicked, this, [this]
            { m_importer->importDialog(targetFolder()); });

    auto *top = new QHBoxLayout;
    top->addWidget(importButton);
    top->addWidget(newButton);
    top->addWidget(m_filter, 1);
    top->addWidget(m_current);
    top->addWidget(refresh);

    auto *split = new QSplitter(this);
    split->addWidget(m_tree);
    auto *right = new QWidget(split);
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->addLayout(top);
    rl->addWidget(m_list);
    split->addWidget(right);
    split->setStretchFactor(1, 3);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(split);

    connect(m_tree, &QTreeView::clicked, this, [this](const QModelIndex &i)
            { showFolder(m_dirs->filePath(i)); });
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &t)
            { m_files->setNameFilters(t.isEmpty() ? QStringList() : QStringList{"*" + t + "*"}); });
    connect(m_list, &QListView::clicked, this, [this](const QModelIndex &i)
            {
        const QString material = materialPath(i);
        if (!material.isEmpty())
            m_ctx->setCurrentMaterial(material); });
    connect(m_list, &QListView::doubleClicked, this, [this](const QModelIndex &i)
            { openAsset(m_files->filePath(m_proxy->mapToSource(i))); });
    connect(m_list, &QListView::customContextMenuRequested, this, &AssetBrowser::showContextMenu);
    connect(refresh, &QToolButton::clicked, this, [this]
            { m_ctx->materials()->reload(); });
    connect(ctx->materials(), &MaterialLibrary::changed, this, [this]
            { m_proxy->clearThumbnails(m_list->rootIndex()); });
    connect(ctx, &EditorContext::currentMaterialChanged, this, [this]
            { updateCurrent(); });
    connect(ctx, &EditorContext::projectDirChanged, this, [this]
            { setRoot(m_ctx->projectDir()); });
    connect(importer, &AssetImporter::imported, this, [this](const QStringList &outputs)
            {
        if (!outputs.isEmpty())
            reveal(outputs.first()); });
    setRoot(ctx->projectDir());
    updateCurrent();
}

void AssetBrowser::setRoot(const QString &dir)
{
    m_dirs->setRootPath(dir);
    m_tree->setRootIndex(m_dirs->index(dir));
    showFolder(dir);
}

void AssetBrowser::showFolder(const QString &dir)
{
    m_folder = dir;
    m_files->setRootPath(dir);
    m_list->setRootIndex(m_proxy->mapFromSource(m_files->index(dir)));
}

QString AssetBrowser::targetFolder() const
{
    const QString root = m_ctx->projectDir();
    if (m_folder.isEmpty() || root.isEmpty() || QDir(m_folder) == QDir(root))
        return {};
    return m_folder;
}

void AssetBrowser::openAsset(const QString &path)
{
    if (AssetEditors::open(m_ctx, this, path) != AssetEditors::OpenResult::OpenInForge)
        return;
    if (path.endsWith(".fmap", Qt::CaseInsensitive))
        emit openMapRequested(path);
    else
        m_ctx->log(tr("Forge only opens maps; change the entry for .%1 in kAssetEditors.").arg(QFileInfo(path).suffix()), LogLevel::Warning);
}

void AssetBrowser::createFromTemplate(const AssetTemplate &tmpl, const QString &folder)
{
    const QString path = createAsset(m_ctx, this, tmpl, folder);
    if (path.isEmpty())
        return;
    if (tmpl.suffix == "mat")
        m_ctx->materials()->reload();
    reveal(path);
    if (tmpl.openAfterCreate)
        openAsset(path);
}

void AssetBrowser::reveal(const QString &path)
{
    const QString dir = QFileInfo(path).absolutePath();
    m_tree->setCurrentIndex(m_dirs->index(dir));
    showFolder(dir);
    // The file model fills in the new file asynchronously.
    QTimer::singleShot(250, this, [this, path]
                       {
        const QModelIndex index = m_proxy->mapFromSource(m_files->index(path));
        if (index.isValid())
        {
            m_list->setCurrentIndex(index);
            m_list->scrollTo(index);
        } });
}

void AssetBrowser::fillNewMenu(QMenu *menu)
{
    const QString folder = targetFolder();
    for (const AssetTemplate &tmpl : assetTemplates())
        menu->addAction(tmpl.label, this, [this, tmpl, folder]
                        { createFromTemplate(tmpl, folder); });
}

bool AssetBrowser::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_list->viewport())
        return QWidget::eventFilter(watched, event);

    const auto importable = [this](const QDropEvent *e)
    {
        QStringList files;
        if (e->source() == m_list || !e->mimeData()->hasUrls())
            return files;
        for (const QUrl &url : e->mimeData()->urls())
            if (url.isLocalFile() && AssetImporter::canImport(url.toLocalFile()))
                files << url.toLocalFile();
        return files;
    };

    switch (event->type())
    {
    case QEvent::DragEnter:
    case QEvent::DragMove:
    {
        auto *e = static_cast<QDropEvent *>(event);
        if (!importable(e).isEmpty())
        {
            e->setDropAction(Qt::CopyAction);
            e->accept();
        }
        else
        {
            e->ignore();
        }
        return true;
    }
    case QEvent::DragLeave:
        return true;
    case QEvent::Drop:
    {
        auto *e = static_cast<QDropEvent *>(event);
        const QStringList files = importable(e);
        if (!files.isEmpty())
        {
            e->setDropAction(Qt::CopyAction);
            e->accept();
            m_importer->import(files, targetFolder());
        }
        return true;
    }
    default:
        return QWidget::eventFilter(watched, event);
    }
}

QString AssetBrowser::materialPath(const QModelIndex &proxyIndex) const
{
    return m_proxy->materialPath(proxyIndex);
}

void AssetBrowser::applyToSelection(const QString &material)
{
    if (!MaterialOps::applyToObjects(m_ctx, m_ctx->doc()->selectionList(), material))
        m_ctx->log(tr("Select one or more solids to apply %1, or drag it onto a face.").arg(material), LogLevel::Warning);
}

void AssetBrowser::showContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    const QModelIndex index = m_list->indexAt(pos);
    if (!index.isValid())
    {
        fillNewMenu(menu.addMenu(tr("New")));
        menu.addAction(tr("Import..."), this, [this]
                       { m_importer->importDialog(targetFolder()); });
        if (!m_folder.isEmpty())
        {
            menu.addSeparator();
            const QString folder = m_folder;
            menu.addAction(tr("Show in Folder"), this, [folder]
                           { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); });
        }
        menu.exec(m_list->viewport()->mapToGlobal(pos));
        return;
    }
    const QString file = m_files->filePath(m_proxy->mapToSource(index));
    const QString material = materialPath(index);

    const QString editor = AssetEditors::editorLabel(file);
    QAction *open = menu.addAction(editor.isEmpty() ? tr("Open") : tr("Open in %1").arg(editor), this, [this, file]
                                   { openAsset(file); });
    QFont bold = open->font();
    bold.setBold(true);
    open->setFont(bold);
    if (AssetImporter::canImport(file))
        menu.addAction(tr("Import into Project"), this, [this, file]
                       { m_importer->import({file}, QFileInfo(file).absolutePath()); });
    menu.addSeparator();
    if (!material.isEmpty())
    {
        menu.addAction(tr("Set as Current Material"), this, [this, material]
                       { m_ctx->setCurrentMaterial(material); });
        menu.addAction(tr("Apply to Selection"), this, [this, material]
                       {
            m_ctx->setCurrentMaterial(material);
            applyToSelection(material); });
        menu.addSeparator();
    }
    menu.addAction(tr("Show in Folder"), this, [file]
                   { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(file).absolutePath())); });
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

void AssetBrowser::updateCurrent()
{
    const QString current = m_ctx->currentMaterial();
    m_current->setText(current.isEmpty() ? tr("Material: (none)") : tr("Material: %1").arg(QFileInfo(current).completeBaseName()));
}

ConsolePanel::ConsolePanel(EditorContext *ctx, QWidget *parent) : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setMaximumBlockCount(5000);
    setFont(QFont("monospace", 9));
    connect(ctx, &EditorContext::logMessage, this, [this](const QString &text, int level)
            {
        static const char* colors[3] = {"#dcdce0", "#f0c040", "#f06060"};
        appendHtml(QString("<span style='color:#7a7a80'>%1</span> <span style='color:%2'>%3</span>")
                       .arg(QDateTime::currentDateTime().toString("HH:mm:ss"), colors[std::clamp(level, 0, 2)], text.toHtmlEscaped())); });
}
