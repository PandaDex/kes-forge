#pragma once
#include <QMainWindow>
#include <QMap>

class AssetBrowser;
class AssetImporter;
class EditorContext;
class Viewport;
class QActionGroup;
class QComboBox;
class QLabel;
class QSplitter;
class QStackedWidget;
class QMenu;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;
    bool openFile(const QString& path);

protected:
    void closeEvent(QCloseEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    void disconnectChildSignals();
    void createActions();
    void createMenus();
    void createToolbars();
    void createViewports();
    void createDocks();
    void createStatusBar();

    bool maybeSave();
    bool save();
    bool saveAs();
    void newMap();
    void open();
    void updateTitle();
    void updateSelectionStatus();
    void toggleMaximize();
    void togglePerspectiveView(bool maximized);
    void setMaximizedView(Viewport* view);
    void buildMap();
    void chooseContentDir();
    bool openProject(const QString &path = QString());
    void promptForProject();
    bool toolBusy();
    void addRecentFile(const QString& path);
    void rebuildRecentMenu();
    void autosave();
    void checkProblems();
    void showTransformDialog();
    void tieToEntity();
    void hollow();
    void flipSelection(bool vertical);
    void createAssetsMenu();

    EditorContext* m_ctx;
    AssetImporter* m_importer = nullptr;
    AssetBrowser* m_assets = nullptr;
    Viewport* m_views[4] = {};
    QSplitter* m_vsplit = nullptr;
    QSplitter* m_rows[2] = {};
    Viewport* m_maximizedView = nullptr;

    QActionGroup* m_toolGroup = nullptr;
    QMap<QString, QAction*> m_toolActions;
    QAction *m_newAct, *m_openAct, *m_saveAct, *m_saveAsAct, *m_quitAct;
    QAction *m_undoAct, *m_redoAct, *m_dupAct, *m_delAct, *m_selAllAct, *m_selNoneAct, *m_centerAct;
    QAction *m_snapAct, *m_gridUpAct, *m_gridDownAct, *m_maxAct, *m_perspectiveAct;
    QAction *m_buildAct, *m_newClassAct, *m_reloadDefsAct, *m_contentAct, *m_aboutAct;
    QAction *m_openProjectAct = nullptr;
    QAction *m_cutAct, *m_copyAct, *m_pasteAct, *m_selClassAct;
    QAction *m_hideSelAct, *m_hideUnselAct, *m_unhideAct;
    QAction *m_carveAct, *m_hollowAct, *m_tieAct, *m_toWorldAct, *m_transformAct, *m_snapSelAct;
    QAction *m_flipHAct, *m_flipVAct, *m_checkAct;
    QMenu* m_recentMenu = nullptr;
    QComboBox* m_gridBox = nullptr;
    QStackedWidget* m_optionsStack = nullptr;
    QMap<QString, int> m_optionsIndex;
    QLabel *m_stCursor = nullptr, *m_stSel = nullptr, *m_stGrid = nullptr;
};
