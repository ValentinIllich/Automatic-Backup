#ifndef DIALOG_H
#define DIALOG_H

#include "backupExecuter.h"

#include <QtGui/QDialog>
#include <qtreewidgetitem>

#include <qmap.h>

namespace Ui
{
    class cleanupDialog;
}

class dirEntry
{
public:
  dirEntry(dirEntry *parent,QString const &name)
    : m_parent(parent)
    ,m_name(name)
  {
    m_tocData.m_tocId = 0;
    m_tocData.m_size = 0;
    m_tocData.m_modify = 0;
    m_tocData.m_crc = 0;
  }

  void updateDirInfo(qint64 dirSizes,qint64 lastModified)
  {
    dirEntry *parent = this;
    while( parent )
    {
      parent->m_tocData.m_size += dirSizes;
      if( lastModified>parent->m_tocData.m_modify ) parent->m_tocData.m_modify = lastModified;
      parent = parent->m_parent;
    }
  }
  QString absoluteFilePath()
  {
    QString path = "";

    dirEntry *parent = this;
    while( parent )
    {
      if( !path.isEmpty() ) path.prepend("/");
      path = parent->m_name + path;
      parent = parent->m_parent;
    }

    return path;
  }

  dirEntry *m_parent;
  QString m_name;
  backupExecuter::fileTocEntry m_tocData;

  QMap<QString,dirEntry*> m_dirs;
  QList<dirEntry*> m_files;
};

class cleanupDialog : public QDialog, public IBackupOperationsInterface
{
    Q_OBJECT

public:
    cleanupDialog(QWidget *parent = 0);
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
    void doBreak();
    void analyzePath(QString const &path);
    void setLimitDate();

protected:
    virtual void contextMenuEvent ( QContextMenuEvent * e );

private:
    void scanRelativePath( QString const &path, double &Bytes, dirEntry *entry, bool &isEmpty );
    bool canReadFromTocFile( QString const &path, dirEntry *entry );
    void populateTree( dirEntry *entry, QTreeWidgetItem *item, int &depth );

    bool traverseItems(QTreeWidgetItem *startingItem,double &dirSize);
    void openFile( QString const &fn );

//    QMap<QString,struct crcInfo> crcSummary;
//    QMap<QString,QMap<QString,backupExecuter::fileTocEntry> > archiveContent;

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
    //QTreeWidgetItem *m_item;
    dirEntry *m_rootEntry;
    QDateTime m_lastmodified;
    double m_totalbytes;

    backupEngine *m_engine;
};

#endif // DIALOG_H
