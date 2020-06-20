#ifndef DIALOG_H
#define DIALOG_H

#include "backupExecuter.h"
#include "backupDirStruct.h"

#include <QDialog>
#include <QTreeWidgetItem>

#include <qmap.h>

namespace Ui
{
    class cleanupDialog;
}

class cleanupDialog : public QDialog, public IBackupOperationsInterface
{
    Q_OBJECT

public:
    cleanupDialog(QWidget *parent = nullptr);
    virtual ~cleanupDialog();

    void setPaths(QString const &srcpath,QString const &dstpath);

    virtual void threadedCopyOperation();
    virtual void threadedVerifyOperation();
    virtual void operationFinishedEvent();

    virtual void processProgressMaximum(int maximum);
    virtual void processProgressValue(int value);
    virtual void processProgressText(QString const &text);
    virtual void processFileNameText(QString const &text);

public slots:
    void doAnalyze();
    void doRescan();
    void doBreak();
    void analyzePath(QString const &path);
    void setLimitDate();

protected:
    virtual void contextMenuEvent ( QContextMenuEvent * e );

private:
    void scanRelativePath( QString const &path, dirEntry *entry, int &dirCount );
    void populateTree( dirEntry *entry, QTreeWidgetItem *item, int &depth, int &processedDirs );

    bool traverseItems(QTreeWidgetItem *startingItem,double &dirSize);
    void openFile( QString const &fn );

    void checkForBackupPath(QString const &path);
    void saveDirStruct();

    Ui::cleanupDialog *ui;

    QString m_analyzingPath;
    QString m_sourcePath;

    bool m_analyze;
    bool m_run;
    bool m_ignore;
    bool m_cancel;

    int m_depth;
    int m_chars;
    QFontMetrics *m_metrics;

    QString m_path;
    dirEntry *m_rootEntry;
    QDateTime m_lastmodified;
    int m_dirCount;

    bool m_dirStructValid;
    bool m_dirStructChanged;

    backupEngine *m_engine;
};

#endif // DIALOG_H
