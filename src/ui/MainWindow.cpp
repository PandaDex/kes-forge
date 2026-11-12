#include "ui/MainWindow.h"
#include "assets/AssetEditors.h"
#include "assets/AssetImporter.h"
#include "assets/AssetTemplates.h"
#include "compile/MapCompiler.h"
#include "core/EditorContext.h"
#include "core/MapOperations.h"
#include "render/Viewport.h"
#include "ui/AssetBrowser.h"
#include "ui/EntityClassDialog.h"
#include "ui/OutlinerPanel.h"
#include "ui/PropertiesPanel.h"
#include "ui/Theme.h"
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMimeData>
#include <QStandardPaths>
#include <QTimer>
#include <QUndoView>
#include <QDesktopServices>
#include <QUrl>

MainWindow::MainWindow()
{
    m_ctx = new EditorContext(this);
    m_importer = new AssetImporter(m_ctx, this);
    setObjectName("MainWindow");
    resize(1500, 920);
    setDockNestingEnabled(true);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    createActions();
    createViewports();
    createDocks();
    createMenus();
    createToolbars();
    createStatusBar();

    connect(m_ctx->doc(), &MapDocument::modifiedChanged, this, &MainWindow::updateTitle);
    connect(m_ctx->doc(), &MapDocument::pathChanged, this, &MainWindow::updateTitle);
    connect(m_ctx, &EditorContext::projectChanged, this, &MainWindow::updateTitle);
    updateTitle();

    QSettings s;
    if (s.contains("window/geometry"))
    {
        restoreGeometry(s.value("window/geometry").toByteArray());
        restoreState(s.value("window/state").toByteArray());
    }
    m_ctx->setActiveViewport(m_views[0]);
    setAcceptDrops(true);

    auto* autosaveTimer = new QTimer(this);
    autosaveTimer->setInterval(3 * 60 * 1000);
    connect(autosaveTimer, &QTimer::timeout, this, &MainWindow::autosave);
    autosaveTimer->start();

    m_ctx->log(tr("Forge ready. Entity classes: %1 point, %2 solid.")
            .arg(m_ctx->registry()->classes(ClassKind::Point).size())
            .arg(m_ctx->registry()->classes(ClassKind::Solid).size()));

    if (m_ctx->project().isValid())
        m_ctx->log(tr("Project: %1 (%2)").arg(m_ctx->project().name, m_ctx->project().filePath));
    else
        QTimer::singleShot(0, this, &MainWindow::promptForProject); // once the window is visible
}

MainWindow::~MainWindow()
{
    disconnectChildSignals();
}

void MainWindow::disconnectChildSignals()
{
    for (QObject* sender : findChildren<QObject*>())
        QObject::disconnect(sender, nullptr, this, nullptr);
}

void MainWindow::createActions()
{
    auto* doc = m_ctx->doc();
    auto mk = [this](const QString& text, const QKeySequence& key = {})
    {
        auto* a = new QAction(text, this);
        if (!key.isEmpty())
            a->setShortcut(key);
        return a;
    };
    m_newAct = mk(tr("&New"), QKeySequence::New);
    m_openAct = mk(tr("&Open..."), QKeySequence::Open);
    m_saveAct = mk(tr("&Save"), QKeySequence::Save);
    m_saveAsAct = mk(tr("Save &As..."), QKeySequence::SaveAs);
    m_quitAct = mk(tr("&Quit"), QKeySequence::Quit);
    connect(m_newAct, &QAction::triggered, this, &MainWindow::newMap);
    connect(m_openAct, &QAction::triggered, this, &MainWindow::open);
    connect(m_saveAct, &QAction::triggered, this, [this]
        { save(); });
    connect(m_saveAsAct, &QAction::triggered, this, [this]
        { saveAs(); });
    connect(m_quitAct, &QAction::triggered, this, &QWidget::close);

    // Undo/redo go through toolBusy() so they can't fire in the middle of a drag.
    QUndoStack* stack = doc->undoStack();
    m_undoAct = mk(tr("&Undo"), QKeySequence::Undo);
    m_redoAct = mk(tr("&Redo"));
    m_redoAct->setShortcuts({ QKeySequence::Redo, QKeySequence("Ctrl+Y") });
    m_undoAct->setEnabled(stack->canUndo());
    m_redoAct->setEnabled(stack->canRedo());
    connect(stack, &QUndoStack::canUndoChanged, m_undoAct, &QAction::setEnabled);
    connect(stack, &QUndoStack::canRedoChanged, m_redoAct, &QAction::setEnabled);
    connect(stack, &QUndoStack::undoTextChanged, this, [this](const QString& t)
        { m_undoAct->setText(t.isEmpty() ? tr("&Undo") : tr("&Undo %1").arg(t)); });
    connect(stack, &QUndoStack::redoTextChanged, this, [this](const QString& t)
        { m_redoAct->setText(t.isEmpty() ? tr("&Redo") : tr("&Redo %1").arg(t)); });
    connect(m_undoAct, &QAction::triggered, this, [this, stack]
        { if (!toolBusy()) stack->undo(); });
    connect(m_redoAct, &QAction::triggered, this, [this, stack]
        { if (!toolBusy()) stack->redo(); });

    m_cutAct = mk(tr("Cu&t"), QKeySequence::Cut);
    m_copyAct = mk(tr("&Copy"), QKeySequence::Copy);
    m_pasteAct = mk(tr("&Paste"), QKeySequence::Paste);
    m_selClassAct = mk(tr("Select All of Same &Class"), QKeySequence("Ctrl+Shift+E"));
    m_hideSelAct = mk(tr("&Hide Selected"), QKeySequence(Qt::Key_H));
    m_hideUnselAct = mk(tr("Hide &Unselected"), QKeySequence("Shift+H"));
    m_unhideAct = mk(tr("Unhide &All"), QKeySequence(Qt::Key_U));
    connect(m_cutAct, &QAction::triggered, this, [this]
        { if (!toolBusy()) MapOps::cut(m_ctx); });
    connect(m_copyAct, &QAction::triggered, this, [this]
        { MapOps::copy(m_ctx); });
    connect(m_pasteAct, &QAction::triggered, this, [this]
        {
        if (toolBusy()) return;
        if (!MapOps::paste(m_ctx)) statusBar()->showMessage(tr("Nothing to paste."), 3000); });
    connect(m_selClassAct, &QAction::triggered, this, [this]
        { MapOps::selectSameClass(m_ctx); });
    connect(m_hideSelAct, &QAction::triggered, this, [this]
        { if (!toolBusy()) MapOps::hideSelected(m_ctx); });
    connect(m_hideUnselAct, &QAction::triggered, this, [this]
        { if (!toolBusy()) MapOps::hideUnselected(m_ctx); });
    connect(m_unhideAct, &QAction::triggered, this, [this]
        { if (!toolBusy()) MapOps::unhideAll(m_ctx); });

    m_carveAct = mk(tr("&Carve"), QKeySequence("Ctrl+Shift+C"));
    m_hollowAct = mk(tr("&Hollow..."), QKeySequence("Ctrl+H"));
    m_tieAct = mk(tr("&Tie to Entity..."), QKeySequence("Ctrl+T"));
    m_toWorldAct = mk(tr("&Move to World"), QKeySequence("Ctrl+Shift+W"));
    m_transformAct = mk(tr("T&ransform..."), QKeySequence("Ctrl+M"));
    m_snapSelAct = mk(tr("&Snap Selected to Grid"), QKeySequence("Ctrl+B"));
    m_flipHAct = mk(tr("Flip &Horizontally"), QKeySequence("Ctrl+L"));
    m_flipVAct = mk(tr("Flip &Vertically"), QKeySequence("Ctrl+I"));
    m_checkAct = mk(tr("&Check for Problems"), QKeySequence("Alt+P"));
    m_carveAct->setToolTip(tr("Subtract the selected solids from every solid they touch (Ctrl+Shift+C)"));
    m_hollowAct->setToolTip(tr("Turn the selected solids into hollow rooms (Ctrl+H)"));
    m_tieAct->setToolTip(tr("Combine the selected solids into one solid entity (Ctrl+T)"));
    m_toWorldAct->setToolTip(tr("Turn the selected solid entities back into static geometry"));
    m_checkAct->setToolTip(tr("Validate the map without building it (Alt+P)"));
    connect(m_carveAct, &QAction::triggered, this, [this]
        { if (!toolBusy()) MapOps::carve(m_ctx); });
    connect(m_hollowAct, &QAction::triggered, this, &MainWindow::hollow);
    connect(m_tieAct, &QAction::triggered, this, &MainWindow::tieToEntity);
    connect(m_toWorldAct, &QAction::triggered, this, [this]
        { if (!toolBusy()) MapOps::moveToWorld(m_ctx); });
    connect(m_transformAct, &QAction::triggered, this, &MainWindow::showTransformDialog);
    connect(m_snapSelAct, &QAction::triggered, this, [this]
        { if (!toolBusy()) MapOps::snapToGrid(m_ctx); });
    connect(m_flipHAct, &QAction::triggered, this, [this]
        { flipSelection(false); });
    connect(m_flipVAct, &QAction::triggered, this, [this]
        { flipSelection(true); });
    connect(m_checkAct, &QAction::triggered, this, &MainWindow::checkProblems);

    m_dupAct = mk(tr("&Duplicate"), QKeySequence("Ctrl+D"));
    m_delAct = mk(tr("D&elete"), QKeySequence::Delete);
    m_selAllAct = mk(tr("Select &All"), QKeySequence::SelectAll);
    m_selNoneAct = mk(tr("Select &None"), QKeySequence("Ctrl+Shift+A"));
    m_centerAct = mk(tr("&Center Views on Selection"), QKeySequence("Ctrl+E"));
    connect(m_dupAct, &QAction::triggered, this, [this]
        {
        if (toolBusy()) return;
        const float g = float(m_ctx->gridSize());
        m_ctx->doc()->duplicateSelection({g, g, 0}); });
    connect(m_delAct, &QAction::triggered, this, [this]
        { if (!toolBusy()) m_ctx->doc()->deleteObjects(m_ctx->doc()->selection()); });
    connect(m_selAllAct, &QAction::triggered, this, [this]
        { m_ctx->doc()->selectAll(); });
    connect(m_selNoneAct, &QAction::triggered, this, [this]
        { m_ctx->doc()->clearSelection(); });
    connect(m_centerAct, &QAction::triggered, this, [this]
        {
        AABB b = m_ctx->doc()->selectionBounds();
        if (!b.valid()) for (const auto& kvp : m_ctx->doc()->objects()) b.expand(kvp.second.worldBounds());
        m_ctx->focusOn(b); });

    m_snapAct = mk(tr("Snap to &Grid"), QKeySequence("Shift+W"));
    m_snapAct->setCheckable(true);
    m_snapAct->setChecked(m_ctx->snapEnabled());
    m_gridUpAct = mk(tr("&Larger Grid"), QKeySequence(Qt::Key_BracketRight));
    m_gridDownAct = mk(tr("&Smaller Grid"), QKeySequence(Qt::Key_BracketLeft));
    m_maxAct = mk(tr("&Maximize Active View"), QKeySequence("Shift+Z"));
    m_perspectiveAct = mk(tr("3D Only"));
    m_perspectiveAct->setCheckable(true);
    m_perspectiveAct->setToolTip(tr("Show the 3D perspective view by itself"));
    connect(m_snapAct, &QAction::toggled, m_ctx, &EditorContext::setSnapEnabled);
    connect(m_gridUpAct, &QAction::triggered, this, [this]
        { m_ctx->setGridSize(m_ctx->gridSize() * 2); });
    connect(m_gridDownAct, &QAction::triggered, this, [this]
        { m_ctx->setGridSize(m_ctx->gridSize() / 2); });
    connect(m_maxAct, &QAction::triggered, this, &MainWindow::toggleMaximize);
    connect(m_perspectiveAct, &QAction::toggled, this, &MainWindow::togglePerspectiveView);

    m_buildAct = mk(tr("&Build Map..."), QKeySequence(Qt::Key_F9));
    m_newClassAct = mk(tr("&New Entity Class..."));
    m_reloadDefsAct = mk(tr("&Reload Entity Definitions"), QKeySequence("Ctrl+Shift+R"));
    m_contentAct = mk(tr("Set &Content Directory..."));
    m_contentAct->setToolTip(tr("Use a bare content folder instead of a .kproject"));
    m_openProjectAct = mk(tr("Open &Project..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(m_openProjectAct, &QAction::triggered, this, [this]
        { openProject(); });
    m_aboutAct = mk(tr("&About Forge"));
    connect(m_buildAct, &QAction::triggered, this, &MainWindow::buildMap);
    connect(m_newClassAct, &QAction::triggered, this, [this]
        { EntityClassDialog(m_ctx, this).exec(); });
    connect(m_reloadDefsAct, &QAction::triggered, this, [this]
        { m_ctx->registry()->reloadAll(); });
    connect(m_contentAct, &QAction::triggered, this, &MainWindow::chooseContentDir);
    connect(m_aboutAct, &QAction::triggered, this, [this]
        { QMessageBox::about(this, tr("About Forge"), tr("<b>Forge</b> - a 3D level editor for KES Engine")); });

    m_toolGroup = new QActionGroup(this);
    for (Tool* t : m_ctx->tools()->tools())
    {
        auto* a = new QAction(Theme::toolIcon(t->id()), t->title(), this);
        a->setCheckable(true);
        if (!t->shortcut().isEmpty())
            a->setShortcut(QKeySequence(t->shortcut()));
        a->setToolTip(QString("<b>%1</b> (%2)<br>%3").arg(t->title(), t->shortcut(), t->statusHint().toHtmlEscaped()));
        m_toolGroup->addAction(a);
        m_toolActions[t->id()] = a;
        const QString id = t->id();
        connect(a, &QAction::triggered, this, [this, id]
            {
            if (toolBusy()) {
                if (auto *cur = m_toolActions.value(m_ctx->tools()->current()->id())) cur->setChecked(true);
                return;
            }
            m_ctx->tools()->setCurrent(id); });
    }
    connect(m_ctx->tools(), &ToolManager::currentChanged, this, [this](Tool* t)
        {
        if (auto* a = m_toolActions.value(t->id())) a->setChecked(true);
        if (m_optionsStack && m_optionsIndex.contains(t->id())) m_optionsStack->setCurrentIndex(m_optionsIndex[t->id()]);
        statusBar()->showMessage(t->statusHint());
        for (Viewport* v : m_ctx->viewports()) v->update(); });
    if (auto* a = m_toolActions.value(m_ctx->tools()->current()->id()))
        a->setChecked(true);
}

void MainWindow::createMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_openProjectAct);
    file->addSeparator();
    file->addActions({ m_newAct, m_openAct });
    m_recentMenu = file->addMenu(tr("Open &Recent"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    file->addSeparator();
    file->addActions({ m_saveAct, m_saveAsAct });
    file->addSeparator();
    file->addAction(m_quitAct);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addActions({ m_undoAct, m_redoAct });
    edit->addSeparator();
    edit->addActions({ m_cutAct, m_copyAct, m_pasteAct, m_dupAct, m_delAct });
    edit->addSeparator();
    edit->addActions({ m_selAllAct, m_selNoneAct, m_selClassAct, m_centerAct });
    edit->addSeparator();
    edit->addActions({ m_hideSelAct, m_hideUnselAct, m_unhideAct });
    connect(edit, &QMenu::aboutToShow, this, [this]
        { m_pasteAct->setEnabled(MapOps::canPaste()); });
    connect(edit, &QMenu::aboutToHide, this, [this]
        { m_pasteAct->setEnabled(true); });

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addActions({ m_snapAct, m_gridUpAct, m_gridDownAct });
    view->addSeparator();
    view->addActions({ m_maxAct, m_perspectiveAct });
    view->addSeparator();
    QMenu* panels = view->addMenu(tr("&Panels"));
    for (QDockWidget* d : findChildren<QDockWidget*>())
        panels->addAction(d->toggleViewAction());

    QMenu* tools = menuBar()->addMenu(tr("&Tools"));
    for (Tool* t : m_ctx->tools()->tools())
        tools->addAction(m_toolActions[t->id()]);
    tools->addSeparator();
    tools->addActions({ m_carveAct, m_hollowAct });
    tools->addSeparator();
    tools->addActions({ m_tieAct, m_toWorldAct });
    tools->addSeparator();
    tools->addActions({ m_transformAct, m_snapSelAct, m_flipHAct, m_flipVAct });
    tools->addSeparator();
    tools->addActions({ m_newClassAct, m_reloadDefsAct, m_contentAct });

    QMenu* map = menuBar()->addMenu(tr("&Map"));
    map->addAction(m_checkAct);
    map->addAction(m_buildAct);

    createAssetsMenu();

    menuBar()->addMenu(tr("&Help"))->addAction(m_aboutAct);
}

void MainWindow::createAssetsMenu()
{
    QMenu* assets = menuBar()->addMenu(tr("&Assets"));
    QAction* import = assets->addAction(tr("&Import..."), this, [this]
        { m_importer->importDialog(); });
    import->setShortcut(QKeySequence(tr("Ctrl+Shift+I")));
    import->setStatusTip(tr("Import models, animations, textures and sounds into their content folders"));

    QMenu* create = assets->addMenu(tr("&New"));
    for (const AssetTemplate& tmpl : assetTemplates())
        create->addAction(tmpl.label, this, [this, tmpl]
            { m_assets->createFromTemplate(tmpl, QString()); });

    QMenu* launch = assets->addMenu(tr("&Launch"));
    connect(launch, &QMenu::aboutToShow, this, [this, launch]
        {
        launch->clear();
        for (const QString &program : AssetEditors::programs())
            launch->addAction(program, this, [this, program]
                              { AssetEditors::launch(m_ctx, this, program); }); });

    assets->addSeparator();
    assets->addAction(tr("Open Content &Folder"), this, [this]
        {
        if (!m_ctx->projectDir().isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_ctx->projectDir())); });
}

void MainWindow::createToolbars()
{
    auto* strip = new QToolBar(tr("Tools"), this);
    strip->setObjectName("toolStrip");
    strip->setMovable(false);
    strip->setIconSize(QSize(26, 26));
    strip->setToolButtonStyle(Qt::ToolButtonIconOnly);
    for (Tool* t : m_ctx->tools()->tools())
        strip->addAction(m_toolActions[t->id()]);
    addToolBar(Qt::LeftToolBarArea, strip);

    auto* top = new QToolBar(tr("Main"), this);
    top->setObjectName("mainBar");
    top->setMovable(false);
    top->addActions({ m_newAct, m_openAct, m_saveAct });
    top->addSeparator();
    top->addActions({ m_undoAct, m_redoAct });
    top->addSeparator();
    top->addWidget(new QLabel(tr(" Grid ")));
    m_gridBox = new QComboBox;
    for (int g = 1; g <= 512; g *= 2)
        m_gridBox->addItem(QString::number(g), g);
    m_gridBox->setCurrentIndex(m_gridBox->findData(m_ctx->gridSize()));
    top->addWidget(m_gridBox);
    top->addAction(m_snapAct);
    top->addSeparator();
    top->addAction(m_perspectiveAct);
    top->addSeparator();
    top->addActions({ m_carveAct, m_hollowAct, m_tieAct });
    top->addSeparator();
    top->addActions({ m_checkAct, m_buildAct });
    addToolBar(Qt::TopToolBarArea, top);

    connect(m_gridBox, qOverload<int>(&QComboBox::activated), this, [this](int i)
        { m_ctx->setGridSize(m_gridBox->itemData(i).toInt()); });
    connect(m_ctx, &EditorContext::gridChanged, this, [this]
        {
        const int i = m_gridBox->findData(m_ctx->gridSize());
        QSignalBlocker b(m_gridBox);
        if (i >= 0) m_gridBox->setCurrentIndex(i);
        m_stGrid->setText(tr("Grid: %1 %2").arg(m_ctx->gridSize()).arg(m_ctx->snapEnabled() ? tr("(snap)") : tr("(free)"))); });
    connect(m_ctx, &EditorContext::snapChanged, this, [this]
        {
        QSignalBlocker b(m_snapAct);
        m_snapAct->setChecked(m_ctx->snapEnabled());
        m_stGrid->setText(tr("Grid: %1 %2").arg(m_ctx->gridSize()).arg(m_ctx->snapEnabled() ? tr("(snap)") : tr("(free)"))); });
}

void MainWindow::createViewports()
{
    const ViewKind kinds[4] = { ViewKind::Perspective, ViewKind::Top, ViewKind::Front, ViewKind::Side };
    for (int i = 0; i < 4; ++i)
        m_views[i] = new Viewport(m_ctx, kinds[i]);
    m_rows[0] = new QSplitter(Qt::Horizontal);
    m_rows[1] = new QSplitter(Qt::Horizontal);
    m_rows[0]->addWidget(m_views[0]);
    m_rows[0]->addWidget(m_views[1]);
    m_rows[1]->addWidget(m_views[2]);
    m_rows[1]->addWidget(m_views[3]);
    m_vsplit = new QSplitter(Qt::Vertical);
    m_vsplit->addWidget(m_rows[0]);
    m_vsplit->addWidget(m_rows[1]);
    for (QSplitter* s : { m_vsplit, m_rows[0], m_rows[1] })
    {
        s->setChildrenCollapsible(false);
        s->setHandleWidth(3);
    }
    connect(m_rows[0], &QSplitter::splitterMoved, this, [this]
        { m_rows[1]->setSizes(m_rows[0]->sizes()); });
    connect(m_rows[1], &QSplitter::splitterMoved, this, [this]
        { m_rows[0]->setSizes(m_rows[1]->sizes()); });
    setCentralWidget(m_vsplit);
}

void MainWindow::createDocks()
{
    auto addDock = [this](const QString& title, QWidget* w, Qt::DockWidgetArea area)
    {
        auto* d = new QDockWidget(title, this);
        d->setObjectName(title);
        d->setWidget(w);
        addDockWidget(area, d);
        return d;
    };

    m_optionsStack = new QStackedWidget;
    for (Tool* t : m_ctx->tools()->tools())
    {
        QWidget* panel = t->createOptionsPanel(m_optionsStack);
        if (!panel)
        {
            panel = new QLabel(t->statusHint());
            static_cast<QLabel*>(panel)->setWordWrap(true);
            static_cast<QLabel*>(panel)->setAlignment(Qt::AlignTop | Qt::AlignLeft);
            panel->setContentsMargins(8, 8, 8, 8);
        }
        m_optionsIndex[t->id()] = m_optionsStack->addWidget(panel);
    }
    m_optionsStack->setCurrentIndex(m_optionsIndex.value(m_ctx->tools()->current()->id()));

    auto* outliner = addDock(tr("Outliner"), new OutlinerPanel(m_ctx), Qt::RightDockWidgetArea);
    auto* options = addDock(tr("Tool Options"), m_optionsStack, Qt::RightDockWidgetArea);
    auto* props = addDock(tr("Properties"), new PropertiesPanel(m_ctx), Qt::RightDockWidgetArea);
    splitDockWidget(outliner, props, Qt::Vertical);
    tabifyDockWidget(outliner, options);
    outliner->raise();

    m_assets = new AssetBrowser(m_ctx, m_importer);
    connect(m_assets, &AssetBrowser::openMapRequested, this, [this](const QString& p)
        { if (maybeSave()) openFile(p); });
    auto* browser = addDock(tr("Asset Browser"), m_assets, Qt::BottomDockWidgetArea);
    auto* console = addDock(tr("Console"), new ConsolePanel(m_ctx), Qt::BottomDockWidgetArea);
    auto* historyView = new QUndoView(m_ctx->doc()->undoStack());
    historyView->setEmptyLabel(tr("<original map>"));
    auto* history = addDock(tr("History"), historyView, Qt::BottomDockWidgetArea);
    tabifyDockWidget(browser, console);
    tabifyDockWidget(console, history);
    browser->raise();

    resizeDocks({ outliner, props }, { 380, 460 }, Qt::Vertical);
    resizeDocks({ outliner }, { 360 }, Qt::Horizontal);
    resizeDocks({ browser }, { 190 }, Qt::Vertical);
}

void MainWindow::createStatusBar()
{
    m_stCursor = new QLabel;
    m_stSel = new QLabel;
    m_stGrid = new QLabel;
    for (QLabel* l : { m_stCursor, m_stSel, m_stGrid })
    {
        l->setMinimumWidth(130);
        statusBar()->addPermanentWidget(l);
    }
    m_stGrid->setText(tr("Grid: %1 %2").arg(m_ctx->gridSize()).arg(tr("(snap)")));
    connect(m_ctx, &EditorContext::cursorText, m_stCursor, &QLabel::setText);
    connect(m_ctx->doc(), &MapDocument::selectionChanged, this, &MainWindow::updateSelectionStatus);
    connect(m_ctx->doc(), &MapDocument::objectsChanged, this, &MainWindow::updateSelectionStatus);
    updateSelectionStatus();
    statusBar()->showMessage(m_ctx->tools()->current()->statusHint());
}

void MainWindow::updateSelectionStatus()
{
    const auto& sel = m_ctx->doc()->selection();
    if (sel.isEmpty())
    {
        m_stSel->setText(tr("%1 objects").arg(m_ctx->doc()->objects().size()));
        return;
    }
    const QVector3D s = m_ctx->doc()->selectionBounds().size();
    m_stSel->setText(tr("Selected %1  (%2 x %3 x %4)").arg(sel.size()).arg(int(s.x())).arg(int(s.y())).arg(int(s.z())));
}

void MainWindow::toggleMaximize()
{
    Viewport* active = m_ctx->activeViewport();
    if (!active)
        return;

    setMaximizedView(m_maximizedView == active ? nullptr : active);
}

void MainWindow::togglePerspectiveView(bool maximized)
{
    if (maximized)
    {
        setMaximizedView(m_views[0]);
    }
    else if (m_maximizedView == m_views[0])
    {
        setMaximizedView(nullptr);
    }
}

void MainWindow::setMaximizedView(Viewport* view)
{
    m_maximizedView = view;
    bool vis[4];
    for (int i = 0; i < 4; ++i)
        vis[i] = !m_maximizedView || m_views[i] == m_maximizedView;
    for (int i = 0; i < 4; ++i)
        m_views[i]->setVisible(vis[i]);
    m_rows[0]->setVisible(vis[0] || vis[1]);
    m_rows[1]->setVisible(vis[2] || vis[3]);
    if (m_maximizedView)
        m_maximizedView->setFocus();

    const QSignalBlocker blocker(m_perspectiveAct);
    m_perspectiveAct->setChecked(m_maximizedView == m_views[0]);
}

void MainWindow::buildMap()
{
    const QString mapPath = m_ctx->doc()->filePath();
    const QFileInfo sourceInfo(mapPath);
    const QString defaultName = mapPath.isEmpty()
        ? QDir(m_ctx->mapsDir()).filePath("untitled.fmapc")
        : sourceInfo.dir().filePath(sourceInfo.completeBaseName() + ".fmapc");
    QString outputPath = QFileDialog::getSaveFileName(
        this, tr("Build Map"), defaultName, tr("Forge Compiled Map (*.fmapc)"));
    if (outputPath.isEmpty())
        return;
    if (QFileInfo(outputPath).suffix().isEmpty())
        outputPath += ".fmapc";
    if (!mapPath.isEmpty() &&
        QFileInfo(outputPath).absoluteFilePath().compare(QFileInfo(mapPath).absoluteFilePath(), Qt::CaseInsensitive) == 0)
    {
        QMessageBox::warning(this, tr("Build Map"), tr("The compiled output must not overwrite the source map."));
        return;
    }

    CompileContext context;
    context.settings.outputPath = outputPath;
    context.registry = m_ctx->registry();
    context.materials = m_ctx->materials();
    context.map = m_ctx->doc()->snapshot();
    const bool success = createMapCompiler()->compile(context);
    for (const QString& line : context.log)
    {
        const LogLevel level = line.startsWith("Error:") ? LogLevel::Error
            : line.startsWith("Warning:")                ? LogLevel::Warning
                                                         : LogLevel::Info;
        m_ctx->log(line, level);
    }

    if (success)
    {
        statusBar()->showMessage(tr("Map compiled: %1").arg(QFileInfo(outputPath).fileName()), 6000);
        return;
    }

    const QString details = context.log.join('\n');
    QMessageBox::critical(this, tr("Build Map Failed"),
        tr("The map could not be compiled.\n\n%1").arg(details));
}

void MainWindow::chooseContentDir()
{
    const QString d = QFileDialog::getExistingDirectory(this, tr("Content Directory"), m_ctx->projectDir());
    if (!d.isEmpty())
        m_ctx->setProjectDir(d);
}

void MainWindow::updateTitle()
{
    const QString p = m_ctx->doc()->filePath();
    const QString map = p.isEmpty() ? tr("untitled") : QFileInfo(p).fileName();
    const QString project = m_ctx->project().isValid() ? m_ctx->project().name : tr("no project");
    setWindowTitle(QString("%1[*] - %2 - Forge").arg(map, project));
    setWindowModified(m_ctx->doc()->isModified());
}

bool MainWindow::openProject(const QString& path)
{
    QString file = path;
    if (file.isEmpty())
    {
        QString startDir = m_ctx->project().isValid() ? QFileInfo(m_ctx->project().filePath).absolutePath() : QString();
#ifdef FORGE_SOURCE_DIR
        if (startDir.isEmpty())
            startDir = QDir::cleanPath(QString(FORGE_SOURCE_DIR) + "/../../game");
#endif
        file = QFileDialog::getOpenFileName(this, tr("Open KES Project"), startDir, tr("KES Project (*.kproject)"));
        if (file.isEmpty())
            return false;
    }
    QString error;
    if (!m_ctx->loadProject(file, &error))
    {
        QMessageBox::critical(this, tr("Open Project"), error);
        return false;
    }
    const KesProject& p = m_ctx->project();
    m_ctx->log(tr("Project: %1 - content %2, maps %3, materials %4, textures %5")
            .arg(p.name, p.assetRoot, p.mapsDir, p.materialsDir, p.texturesDir));
    statusBar()->showMessage(tr("Opened project %1").arg(p.name), 5000);
    return true;
}

void MainWindow::promptForProject()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("Open Project"));
    box.setIcon(QMessageBox::Question);
    box.setText(tr("Open a KES project (.kproject)?"));
    box.setInformativeText(tr("The project file tells Forge where the game's maps, materials and textures are. "
                              "Without one, Forge uses %1 as the content folder.")
            .arg(QDir::toNativeSeparators(m_ctx->projectDir())));
    QPushButton* open = box.addButton(tr("Open Project..."), QMessageBox::AcceptRole);
    box.addButton(tr("Continue Without Project"), QMessageBox::RejectRole);
    box.setDefaultButton(open);
    box.exec();
    if (box.clickedButton() == open)
        openProject();
}

bool MainWindow::maybeSave()
{
    if (!m_ctx->doc()->isModified())
        return true;
    const auto r = QMessageBox::warning(this, tr("Forge"), tr("The map has unsaved changes. Save them?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Save)
        return save();
    return r == QMessageBox::Discard;
}

bool MainWindow::save()
{
    if (m_ctx->doc()->filePath().isEmpty())
        return saveAs();
    QString err;
    if (!m_ctx->doc()->save(m_ctx->doc()->filePath(), &err))
    {
        QMessageBox::critical(this, tr("Save failed"), err);
        return false;
    }
    m_ctx->log(tr("Saved %1").arg(m_ctx->doc()->filePath()));
    addRecentFile(m_ctx->doc()->filePath());
    return true;
}

bool MainWindow::saveAs()
{
    QString f = QFileDialog::getSaveFileName(this, tr("Save Map"), m_ctx->mapsDir(), tr("Forge Map (*.fmap)"));
    if (f.isEmpty())
        return false;
    if (!f.endsWith(".fmap"))
        f += ".fmap";
    QString err;
    if (!m_ctx->doc()->save(f, &err))
    {
        QMessageBox::critical(this, tr("Save failed"), err);
        return false;
    }
    m_ctx->log(tr("Saved %1").arg(f));
    addRecentFile(f);
    return true;
}

void MainWindow::newMap()
{
    if (!toolBusy() && maybeSave())
        m_ctx->doc()->clear();
}

void MainWindow::open()
{
    if (!maybeSave())
        return;
    const QString f = QFileDialog::getOpenFileName(this, tr("Open Map"), m_ctx->mapsDir(), tr("Forge Map (*.fmap)"));
    if (!f.isEmpty())
        openFile(f);
}

bool MainWindow::openFile(const QString& path)
{
    if (toolBusy())
        return false;
    QString err;
    if (!m_ctx->doc()->load(path, &err))
    {
        QMessageBox::critical(this, tr("Open failed"), err);
        return false;
    }
    m_ctx->log(tr("Opened %1 (%2 objects)").arg(path).arg(m_ctx->doc()->objects().size()));
    addRecentFile(path);
    AABB all;
    for (const auto& kvp : m_ctx->doc()->objects())
        all.expand(kvp.second.worldBounds());
    if (all.valid())
        m_ctx->focusOn(all);
    return true;
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (!maybeSave())
    {
        e->ignore();
        return;
    }
    QSettings s;
    s.setValue("window/geometry", saveGeometry());
    s.setValue("window/state", saveState());
    disconnectChildSignals();
    e->accept();
}

bool MainWindow::toolBusy()
{
    if (!m_ctx->tools()->isBusy())
        return false;
    statusBar()->showMessage(tr("Finish or cancel (Esc) the current drag first."), 3000);
    return true;
}

void MainWindow::addRecentFile(const QString& path)
{
    QSettings s;
    QStringList files = s.value("recentFiles").toStringList();
    const QString abs = QFileInfo(path).absoluteFilePath();
    files.removeAll(abs);
    files.prepend(abs);
    while (files.size() > 10)
        files.removeLast();
    s.setValue("recentFiles", files);
}

void MainWindow::rebuildRecentMenu()
{
    m_recentMenu->clear();
    const QStringList files = QSettings().value("recentFiles").toStringList();
    int n = 0;
    for (const QString& f : files)
    {
        if (!QFileInfo::exists(f))
            continue;
        QAction* a = m_recentMenu->addAction(QString("&%1  %2").arg(++n).arg(QFileInfo(f).fileName()));
        a->setStatusTip(QDir::toNativeSeparators(f));
        connect(a, &QAction::triggered, this, [this, f]
            { if (maybeSave()) openFile(f); });
    }
    if (n == 0)
    {
        m_recentMenu->addAction(tr("(none)"))->setEnabled(false);
        return;
    }
    m_recentMenu->addSeparator();
    connect(m_recentMenu->addAction(tr("Clear List")), &QAction::triggered, this, []
        { QSettings().remove("recentFiles"); });
}

void MainWindow::autosave()
{
    MapDocument* doc = m_ctx->doc();
    if (!doc->isModified() || doc->objects().empty() || m_ctx->tools()->isBusy())
        return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/autosave";
    QDir().mkpath(dir);
    const QString base = doc->filePath().isEmpty() ? QStringLiteral("untitled") : QFileInfo(doc->filePath()).completeBaseName();
    const QString path = QDir(dir).filePath(base + ".autosave.fmap");
    QString err;
    if (doc->saveCopy(path, &err))
        statusBar()->showMessage(tr("Autosaved to %1").arg(QDir::toNativeSeparators(path)), 4000);
    else
        m_ctx->log(tr("Autosave failed: %1").arg(err), LogLevel::Warning);
}

void MainWindow::checkProblems()
{
    CompileContext context;
    context.registry = m_ctx->registry();
    context.map = m_ctx->doc()->snapshot();
    const bool ok = validateMap(context);
    int warnings = 0, errors = 0;
    for (const QString& line : context.log)
    {
        const LogLevel level = line.startsWith("Error:") ? LogLevel::Error
            : line.startsWith("Warning:")                ? LogLevel::Warning
                                                         : LogLevel::Info;
        warnings += level == LogLevel::Warning;
        errors += level == LogLevel::Error;
        m_ctx->log(line, level);
    }
    if (ok && warnings == 0)
    {
        statusBar()->showMessage(tr("Check for Problems: no problems found."), 5000);
        return;
    }
    QMessageBox::warning(this, tr("Check for Problems"),
        tr("%1 error(s), %2 warning(s). See the Console for details.").arg(errors).arg(warnings));
}

void MainWindow::tieToEntity()
{
    if (toolBusy())
        return;
    QStringList classes;
    for (const ClassDef* c : m_ctx->registry()->classes(ClassKind::Solid))
        classes << c->name;
    if (classes.isEmpty())
    {
        QMessageBox::information(this, tr("Tie to Entity"), tr("No solid entity classes are defined. Add a @SolidClass to an .dec file."));
        return;
    }
    static QString lastClass = QStringLiteral("func_detail");
    const int current = std::max<int>(0, int(classes.indexOf(lastClass)));
    bool ok = false;
    const QString cls = QInputDialog::getItem(this, tr("Tie to Entity"), tr("Solid entity class:"), classes, current, false, &ok);
    if (!ok || cls.isEmpty())
        return;
    lastClass = cls;
    MapOps::tieToEntity(m_ctx, cls);
}

void MainWindow::hollow()
{
    if (toolBusy())
        return;
    static int thickness = 16;
    bool ok = false;
    const int t = QInputDialog::getInt(this, tr("Hollow"), tr("Wall thickness (negative grows outward):"), thickness, -1024, 1024, 1, &ok);
    if (!ok || t == 0)
        return;
    thickness = t;
    MapOps::hollow(m_ctx, float(t));
}

void MainWindow::flipSelection(bool vertical)
{
    if (toolBusy())
        return;
    const Viewport* v = m_ctx->activeViewport();
    int axis = vertical ? 2 : 0;
    if (v && v->isOrtho())
        axis = vertical ? v->camera().axisV() : v->camera().axisH();
    MapOps::flip(m_ctx, axis);
}

void MainWindow::showTransformDialog()
{
    if (toolBusy() || m_ctx->doc()->selection().isEmpty())
        return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Transform"));
    auto* form = new QFormLayout(&dlg);
    auto* mode = new QComboBox(&dlg);
    mode->addItems({ tr("Move"), tr("Rotate (degrees)"), tr("Scale") });
    form->addRow(tr("Mode"), mode);
    QDoubleSpinBox* val[3];
    auto* row = new QWidget(&dlg);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    const char* axes = "XYZ";
    for (int i = 0; i < 3; ++i)
    {
        val[i] = new QDoubleSpinBox(row);
        val[i]->setRange(-65536, 65536);
        val[i]->setDecimals(3);
        val[i]->setPrefix(QString(QChar(axes[i])) + ": ");
        h->addWidget(val[i]);
    }
    form->addRow(tr("Values"), row);
    auto* note = new QLabel(tr("Rotation and scale pivot around the centre of the selection."), &dlg);
    note->setObjectName("hint");
    form->addRow(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(mode, qOverload<int>(&QComboBox::currentIndexChanged), &dlg, [val](int m)
        { for (auto *s : val) s->setValue(m == 2 ? 1.0 : 0.0); });
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QVector3D v(float(val[0]->value()), float(val[1]->value()), float(val[2]->value()));
    const QVector3D c = m_ctx->doc()->selectionBounds().center();
    QMatrix4x4 m;
    switch (mode->currentIndex())
    {
    case 0:
        m.translate(v);
        MapOps::transformSelection(m_ctx, m, tr("Transform: Move"));
        break;
    case 1:
        m.translate(c);
        m.rotate(v.z(), 0, 0, 1);
        m.rotate(v.y(), 0, 1, 0);
        m.rotate(v.x(), 1, 0, 0);
        m.translate(-c);
        MapOps::transformSelection(m_ctx, m, tr("Transform: Rotate"), true);
        break;
    default:
        if (std::abs(v.x()) < 1e-3f || std::abs(v.y()) < 1e-3f || std::abs(v.z()) < 1e-3f)
        {
            QMessageBox::warning(this, tr("Transform"), tr("Scale factors can't be zero."));
            return;
        }
        m.translate(c);
        m.scale(v);
        m.translate(-c);
        MapOps::transformSelection(m_ctx, m, tr("Transform: Scale"));
        break;
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile() && (u.toLocalFile().endsWith(".fmap", Qt::CaseInsensitive) || AssetImporter::canImport(u.toLocalFile())))
        {
            e->acceptProposedAction();
            return;
        }
}

void MainWindow::dropEvent(QDropEvent* e)
{
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile() && u.toLocalFile().endsWith(".fmap", Qt::CaseInsensitive))
        {
            e->acceptProposedAction();
            if (maybeSave())
                openFile(u.toLocalFile());
            return;
        }

    QStringList sources;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile() && AssetImporter::canImport(u.toLocalFile()))
            sources << u.toLocalFile();
    if (!sources.isEmpty())
    {
        e->acceptProposedAction();
        m_importer->import(sources);
    }
}
