#pragma once
#include <QPlainTextEdit>
#include <QWidget>

class AssetFilterModel;
class AssetImporter;
class EditorContext;
class QFileSystemModel;
class QLabel;
class QListView;
class QMenu;
class QTreeView;
class QLineEdit;
struct AssetTemplate;

class AssetBrowser : public QWidget
{
    Q_OBJECT
public:
    AssetBrowser(EditorContext *ctx, AssetImporter *importer, QWidget *parent = nullptr);

    // Folder shown in the grid, "" when it is the content root (imports then use type folders).
    QString targetFolder() const;
    void openAsset(const QString &path);
    void createFromTemplate(const AssetTemplate &tmpl, const QString &folder);
    void reveal(const QString &path);
    void fillNewMenu(QMenu *menu);

signals:
    void openMapRequested(const QString &path);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setRoot(const QString &dir);
    void showFolder(const QString &dir);
    QString materialPath(const QModelIndex &proxyIndex) const;
    void applyToSelection(const QString &material);
    void showContextMenu(const QPoint &pos);
    void updateCurrent();

    EditorContext *m_ctx;
    AssetImporter *m_importer;
    QFileSystemModel *m_dirs;
    QFileSystemModel *m_files;
    AssetFilterModel *m_proxy;
    QTreeView *m_tree;
    QListView *m_list;
    QLineEdit *m_filter;
    QLabel *m_current;
    QString m_folder;
};

class ConsolePanel : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit ConsolePanel(EditorContext *ctx, QWidget *parent = nullptr);
};
