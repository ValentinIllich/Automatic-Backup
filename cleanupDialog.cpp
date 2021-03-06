#include "cleanupDialog.h"
#include "ui_cleanupDialog.h"
#include "backupEngine.h"
#include "backupExecuter.h"
#include "Utilities.h"

#include <QStyledItemDelegate>
#include <QPainter>
#include <QThread>
#include <QFileDialog>
#include <QDateTime>
#include <QContextMenuEvent>
#include <QMenu>
#include <QProcess>
#include <QLineEdit>
#include <QCalendarWidget>
#include <QMessageBox>
#include <QFontMetrics>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

class CMyDelegate : public QStyledItemDelegate
{
public:
    CMyDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem & option, const QModelIndex & index) const override;
};

void CMyDelegate::paint(QPainter* painter, const QStyleOptionViewItem & option, const QModelIndex & index) const
{
    QStyleOptionViewItem itemOption(option);
    initStyleOption(&itemOption, index);

    //itemOption.rect.adjust(0, 0, 0, 0);  // Make the item rectangle 10 pixels smaller from the left side.

    // And now you can draw a bottom border.
    QTreeWidgetItem *item = (QTreeWidgetItem*)index.internalPointer();
    if( item && /*!item->isSelected() &&*/ (index.column()==0) )
    {
      double percent = 100;
      dirEntry *entry = (dirEntry*)item->data(3,Qt::UserRole).toLongLong();
      if( entry && entry->m_parent )
      {
        qint64 size = entry->m_tocData.m_size;
        qint64 parsize = entry->m_parent->m_tocData.m_size;
        percent = double(size * 100) / double(parsize);
        if( percent>100.0 ) percent = 100.0;
      }
      int w = double(itemOption.rect.width() * percent +0.5) / 100;
      /*if( item->childCount()>0 )
        painter->fillRect(itemOption.rect,Qt::lightGray);*/
      if( item->isSelected() )
      {
        painter->fillRect(itemOption.rect,Qt::blue);
        painter->setPen(Qt::white);
      }
      else
      {
        painter->setPen(Qt::black);
      }
      painter->fillRect(itemOption.rect.x(),itemOption.rect.y(),w,itemOption.rect.height(),Qt::gray);

      painter->drawText(itemOption.rect,item->text(0));
      if( percent>1.0 )
        painter->drawText(itemOption.rect,QString::number(percent,'f',0)+" %",QTextOption(Qt::AlignRight));
      else
        painter->drawText(itemOption.rect,QString::number(percent,'f',1)+" %",QTextOption(Qt::AlignRight));
    }
    else
      // Draw your item content.
      QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &itemOption, painter, nullptr);
}

class actionThread : public QThread
{
public:
  actionThread(std::function<void()> fn)
    : m_fn(fn)
  {
  }
  virtual ~actionThread()
  {
  }

  void processAction()
  {
    start();
    while( !isRunning() )
      qApp->processEvents();
    while( !isFinished() )
      qApp->processEvents();

    wait();
  }

protected:
  virtual void run() override
  {
    m_fn();
  }

private:
  std::function<void()> m_fn;
};

cleanupDialog::cleanupDialog(QWidget *parent)
: QDialog(parent), ui(new Ui::cleanupDialog)
, m_analyze(false)
, m_run(false)
, m_ignore(false)
, m_cancel(false)
, m_depth(0)
, m_chars(0)
, m_metrics(NULL)
, m_rootEntry(NULL)
, m_dirCount(0)
, m_dirStructValid(false)
, m_dirStructChanged(false)
, m_doRescan(false)
, m_engine(new backupEngine(*this))
{
  ui->setupUi(this);

  m_metrics = new QFontMetrics(ui->label->font());
  ui->buttonBox->button(QDialogButtonBox::Cancel)->setDisabled(true);

  QStringList header;
  header << "Path" << "Size" << "modified" << "full path";
  ui->treeView->setHeaderLabels(header);
  ui->treeView->hideColumn(3);

  CMyDelegate* delegate = new CMyDelegate(ui->treeView);
  ui->treeView->setItemDelegate(delegate);

  ui->rescanButt->hide();
}

cleanupDialog::~cleanupDialog()
{
  m_engine->processEventsAndWait();

  delete m_metrics;
  delete m_rootEntry;
  delete m_engine;
  delete ui;
}

void cleanupDialog::saveDirStruct()
{
  m_engine->setProgressText("creating table of contents...");
  m_engine->setProgressMaximum(0);
  m_engine->setProgressValue(0);

  actionThread saver([this]()
    {
      QString tocSummaryFile = backupDirStruct::getTocSummaryFile(m_path);
      if( !backupDirStruct::convertToTocFile(tocSummaryFile,m_rootEntry ) )
          QApplication::postEvent(this,new QEvent(QEvent::User));
    });
  saver.processAction();
}

void cleanupDialog::setPaths(QString const &srcpath,QString const &dstpath)
{
  m_sourcePath = srcpath;
  m_analyzingPath = dstpath;
  setWindowTitle("cleaning up "+dstpath);

  checkForBackupPath(m_analyzingPath);
}

void cleanupDialog::threadedCopyOperation()
{
  //nop
}

void cleanupDialog::threadedVerifyOperation()
{
  //scan directories

  delete m_rootEntry;
  m_rootEntry = new dirEntry(0,m_path);

  m_dirStructValid = false;
  m_dirStructChanged = false;
  m_dirCount = 0;

  QString tocSummaryFile = backupDirStruct::getTocSummaryFile(m_path);
  m_engine->setProgressText("reading table of contents from disk...");
  m_engine->setProgressMaximum(0);
  m_engine->setProgressValue(0);
  if( backupDirStruct::convertFromTocFile(tocSummaryFile,m_rootEntry,m_dirCount)/*canReadFromTocFile(path,entry)*/ )
    m_dirStructValid = true;
  else
    scanRelativePath(m_path,m_rootEntry,m_dirCount);

  m_engine->setProgressText("");
  m_engine->setProgressMaximum(1);
  m_engine->setProgressValue(0);
}

void cleanupDialog::processProgressMaximum(int maximum)
{
  ui->progressBar->setMaximum(maximum);
}

void cleanupDialog::processProgressValue(int value)
{
  ui->progressBar->setValue(value);
}

void cleanupDialog::processProgressText(const QString &text)
{
  QFontMetrics metrics(ui->label->font());
  if( !text.isEmpty() )
  {
    QString messagetext = text;
    while( ui->label->width()<metrics.width(messagetext) )
    {
      int len = messagetext.length() - 4;
      messagetext = text.left(len/2)+"..."+messagetext.right(len/2);
    }
      ui->label->setText(messagetext);
  }
  else
    ui->label->setText(text);
}

void cleanupDialog::processFileNameText(const QString &/*text*/)
{
  //nop
}

QString formatSize( double size )
{
  QString result;

  double MBytes = size / 1024.0 / 1024.0;
  result.sprintf("%'10.3f MB",MBytes);
  return result;
}

void cleanupDialog::operationFinishedEvent()
{
  ui->treeView->clear();
  ui->treeView->setSortingEnabled(false);

  QStringList rootColumns;
  rootColumns << m_path << "0" << QDateTime::fromMSecsSinceEpoch(m_rootEntry->m_tocData.m_modify).toString("yyyy/MM/dd-hh:mm") << m_path;
  ui->treeView->setColumnCount(4);
  QTreeWidgetItem *item = new QTreeWidgetItem(ui->treeView,rootColumns);
  ui->treeView->addTopLevelItem(item);

  m_engine->setProgressMaximum(m_dirCount);
  m_engine->setProgressValue(0);

  int depth = 0;
  int dirsProcessed = 0;
  populateTree(m_rootEntry,item,depth,dirsProcessed);

  item->setText(1,formatSize(m_rootEntry->m_tocData.m_size/*m_totalbytes*/));
  item->setTextAlignment(1,Qt::AlignRight);
  item->setData(0,Qt::UserRole,QVariant(qint64(m_rootEntry)));
  item->setData(3,Qt::UserRole,QVariant(qint64(m_rootEntry)));
  item->setExpanded(true);

  ui->buttonBox->button(QDialogButtonBox::Cancel)->setDisabled(true);
  ui->buttonBox->button(QDialogButtonBox::Ok)->setDisabled(false);

  ui->label->setText("sorting view...");
  m_engine->setProgressMaximum(0);
  m_engine->setProgressValue(0);
  qApp->processEvents(QEventLoop::WaitForMoreEvents, 1000);

  ui->treeView->resizeColumnToContents(0);
  ui->treeView->resizeColumnToContents(1);
  ui->treeView->setSortingEnabled(true);
  ui->treeView->sortByColumn(1,Qt::DescendingOrder);

  discInfo info = getDiscInfo(m_analyzingPath);
  m_engine->setProgressText(QString::number(info.m_capacity)+"% of disk space used ("+formatSize(info.m_availableBytes)+" total)");
  m_engine->setProgressMaximum(1);
  m_engine->setProgressValue(0);

  m_doRescan = false;
}

void cleanupDialog::doAnalyze()
{
  QString startingPath;

  if( m_analyzingPath.isEmpty() )
  {
    startingPath = QFileDialog::getExistingDirectory(this, tr("Open Directory"),m_path,QFileDialog::ShowDirsOnly);
  }
  else
    startingPath = m_analyzingPath;

  if( !startingPath.isEmpty() )
  {
    QFile dir1(startingPath);
    if( !dir1.exists(startingPath) )
    {
      ui->analyzebutt->setDisabled(true);
      QMessageBox box(QMessageBox::NoIcon,"removable media not found","'"+startingPath+"' not found.\n\nPlease insert removable media containing this path.",QMessageBox::Cancel,this);
      box.exec();
    }
    else
    {
      checkForBackupPath(startingPath);

      setLimitDate();

      if( (startingPath!=m_path) || !m_dirStructValid )
      {
        ui->treeView->clear();

        analyzePath(startingPath);
      }
      else
        operationFinishedEvent();

      m_analyzingPath = startingPath;
    }
  }
}

void cleanupDialog::doRescan()
{
  QString startingPath;

  startingPath = QFileDialog::getExistingDirectory(this, tr("Rescan Directory"),m_path,QFileDialog::ShowDirsOnly);
  if( !startingPath.isEmpty() )
  {
    QString tocSummaryFile = backupDirStruct::getTocSummaryFile(startingPath);
    if( QFile::exists(tocSummaryFile) )
      QFile::remove(tocSummaryFile);

    m_dirStructValid = false;
    m_analyzingPath = startingPath;

    m_doRescan = true;
    doAnalyze();
  }
}

void cleanupDialog::doFinish()
{
  if( m_dirStructValid && m_dirStructChanged )
    saveDirStruct();

  {
    m_engine->setProgressText("cleaning up...");
    m_engine->setProgressMaximum(0);
    m_engine->setProgressValue(0);

    actionThread saver([this]()
      {
        delete m_rootEntry;
        m_rootEntry = nullptr;
      });
    saver.processAction();
  }

  accept();
}

void cleanupDialog::analyzePath(QString const &path)
{
  if( m_dirStructValid && m_dirStructChanged )
    saveDirStruct();

  ui->progressBar->setTextVisible(true);
  m_engine->setProgressText("");
  m_engine->setProgressMaximum(0);
  m_engine->setProgressValue(0);

  m_run = true;

  m_path = path;
  m_depth = path.count('/');

  ui->buttonBox->button(QDialogButtonBox::Cancel)->setDisabled(false);
  ui->buttonBox->button(QDialogButtonBox::Ok)->setDisabled(true);

  m_engine->start(true);
}

void cleanupDialog::setLimitDate()
{
  QDateTime limit = backupExecuter::getLimitDate(this,m_lastmodified);
  if( limit.isValid() )
  {
    m_lastmodified = limit;
    //m_analyze = false;
  }
  else
  {
    m_lastmodified = QDateTime::currentDateTime();
    //m_analyze = true;
  }
}

void cleanupDialog::scanRelativePath( QString const &path, dirEntry *entry, int &dirCount )
{
  m_engine->setProgressText(path);

  QDir dir(path);
  dir.setFilter(QDir::Dirs | QDir::Files | QDir::Hidden | QDir::NoSymLinks);
  dir.setSorting(QDir::Size | QDir::Reversed);

  QFileInfoList list = dir.entryInfoList();
  qint64 lastModifiedFile = 0;
  qint64 fileSizes = 0;
  for (int i = 0; m_run && (i < list.size()); ++i)
  {
    QFileInfo fileInfo = list.at(i);
    QString prefix = "";
    QString name = backupDirStruct::cutFilenamePrefix(fileInfo.fileName(),&prefix);
    if( fileInfo.isDir() )
    {
      if( name!="." && name!=".." && !backupDirStruct::isSummaryFile(name) )
      {
        dirEntry *newEntry = new dirEntry(entry,name);
        entry->m_dirs[name] = newEntry;

        scanRelativePath(path+"/"+name,newEntry,dirCount);
        dirCount++;
      }
    }
    else
    {
      fileTocEntry tocentry;
      tocentry.m_prefix = prefix;
      tocentry.m_tocId = 0;//m_nextTocId++;
      tocentry.m_size = fileInfo.size();
      tocentry.m_modify = fileInfo.lastModified().toMSecsSinceEpoch();;
      tocentry.m_crc = 0;

      dirEntry *newEntry = new dirEntry(entry,name);
      newEntry->m_tocData = tocentry;

      entry->m_files.append(newEntry);

      fileSizes += tocentry.m_size;
      if( tocentry.m_modify>lastModifiedFile ) lastModifiedFile = tocentry.m_modify;
    }
  }

  if( m_run )
  {
    entry->updateDirInfos(fileSizes,lastModifiedFile);
    m_dirStructValid = m_dirStructChanged = true;
  }
  else
  {
    delete m_rootEntry;
    m_rootEntry = new dirEntry(0,m_path);
    m_dirStructValid = m_dirStructChanged = false;
  }
}

void cleanupDialog::populateTree( dirEntry *entry, QTreeWidgetItem *item, int &depth, int &processedDirs )
{
  depth++;

  static quint64 lastmsecs = 0;
  quint64 msecs = QDateTime::currentDateTime().toMSecsSinceEpoch();
  if( (msecs-lastmsecs)>200 )
  {
    m_engine->setProgressText(entry->absoluteFilePath());
    m_engine->setProgressValue(processedDirs);
    qApp->processEvents();
    lastmsecs = msecs;
  }

  entry->m_tocData.m_size = 0;
  entry->m_tocData.m_modify = 0;

  QMap<QString,dirEntry*>::iterator it = entry->m_dirs.begin();
  while( m_run && it!=entry->m_dirs.end() )
  {
    QTreeWidgetItem *dirItem = new QTreeWidgetItem(item);
    dirItem->setText(0,it.key());
    dirItem->setText(3,it.value()->absoluteFilePath());
    dirItem->setData(0,Qt::UserRole,QVariant(qint64(it.value())));
    dirItem->setData(3,Qt::UserRole,QVariant(qint64(it.value())));

    populateTree(it.value(),dirItem,depth,processedDirs);
    processedDirs++;

    //dirItem->setBackgroundColor(0,Qt::lightGray);
    //dirItem->setBackgroundColor(1,Qt::lightGray);
    dirItem->setText(1,formatSize(it.value()->m_tocData.m_size));
    dirItem->setTextAlignment(1,Qt::AlignRight);
    dirItem->setText(2,QDateTime::fromMSecsSinceEpoch(it.value()->m_tocData.m_modify).toString("yyyy/MM/dd-hh:mm"));
    if( depth>0 )
      dirItem->setExpanded(false);
    else
    {
      dirItem->setExpanded(true);
      dirItem->treeWidget()->scrollToItem(dirItem);
      ui->treeView->resizeColumnToContents(0);
    }

    if( !m_doRescan && (dirItem->childCount()==0) ) delete dirItem;

    ++it;
  }

  qint64 lastModifiedFile = 0;
  qint64 fileSizes = 0;

  QList<dirEntry*>::iterator it2 = entry->m_files.begin();
  while( m_run && it2!=entry->m_files.end() )
  {
    QDateTime filedate = QDateTime::fromMSecsSinceEpoch((*it2)->m_tocData.m_modify);
    if( filedate<m_lastmodified )
    {
      QTreeWidgetItem *fileItem = new QTreeWidgetItem(item);

      fileItem->setText(0,(*it2)->m_tocData.m_prefix+(*it2)->m_name);
      fileItem->setText(1,formatSize((*it2)->m_tocData.m_size));
      fileItem->setTextAlignment(1,Qt::AlignRight);
      fileItem->setText(2,filedate.toString("yyyy/MM/dd-hh:mm"));
      fileItem->setText(3,(*it2)->absoluteFilePath());
      fileItem->setData(0,Qt::UserRole,QVariant(qint64(*it2)));
      fileItem->setData(3,Qt::UserRole,QVariant(qint64(*it2)));

      fileSizes += (*it2)->m_tocData.m_size;
      if( (*it2)->m_tocData.m_modify>lastModifiedFile ) lastModifiedFile = (*it2)->m_tocData.m_modify;
    }

    ++it2;
  }

  entry->updateDirInfos(fileSizes,lastModifiedFile);

  depth--;
}

void cleanupDialog::doBreak()
{
  m_run = false;
}

void cleanupDialog::openFile( QString const &fn )
{
  QString name = /*"file:/C:/Qt/myProject/test.html"*/"file://"+fn;
  QString webbrowser;

#if defined(Q_OS_WIN)
  ShellExecuteA((HWND)winId(), 0, name.toLocal8Bit(), 0, 0, SW_SHOWNORMAL);
#endif
#if defined(Q_OS_MAC)
  webbrowser = "open";
#elif defined(Q_WS_X11)
  if (isKDERunning())
  {
    webbrowser = "kfmclient";
  }
#endif

  if( !webbrowser.isEmpty() )
  {
    QProcess *proc = new QProcess(this);
    //		show.startDetached("notepad " + fn);
    QObject::connect(proc, SIGNAL(finished(int)), proc, SLOT(deleteLater()));

    QStringList args;
    if (webbrowser == QLatin1String("kfmclient"))
      args.append(QLatin1String("exec"));
    args.append(name);
    proc->start(webbrowser, args);
  }
}

void cleanupDialog::checkForBackupPath(QString const &path)
{
    QString tocSummaryFile = backupDirStruct::getTocSummaryFile(path);
    if( QFile::exists(tocSummaryFile) )
    {
      if( m_analyzingPath.isEmpty() )
      {
        ui->analyzebutt->setText("Analyze Files...");
        ui->rescanButt->show();
      }
      else
      {
        ui->analyzebutt->setText("Analyze Backup...");
        ui->rescanButt->show();
      }
    }
    else
    {
        ui->analyzebutt->setText("Analyze Directory...");
        ui->rescanButt->hide();
    }
}

void cleanupDialog::contextMenuEvent ( QContextMenuEvent * e )
{
  QMenu menu(this);
  QAction *expand = menu.addAction("Expand All...");
  QAction *collapse = menu.addAction("Collapse All...");
  menu.addSeparator();
  QAction *reveal = menu.addAction("Reveal in Shell...");
  menu.addSeparator();
  QAction *cleanup = menu.addAction("Clean Up...");
  if( m_analyze ) cleanup->setDisabled(true);

  QAction *selected = menu.exec(e->globalPos());

  if( selected==expand )
  {
    ui->treeView->expandAll();
  }
  else if( selected==collapse )
  {
    ui->treeView->collapseAll();
  }
  else if( selected==reveal )
  {
    QList<QTreeWidgetItem*> sels = ui->treeView->selectedItems();
    if( sels.count()>0 )
    {
      const QTreeWidgetItem *item = sels.first();
      item=item->parent();
      openFile(item->text(3));
    }
  }
  else if( selected==cleanup )
  {
    m_ignore = false;
    m_cancel = false;

    QList<QTreeWidgetItem*> sels = ui->treeView->selectedItems();
    if( sels.count()>0 )
    {
      QTreeWidgetItem *item = const_cast<QTreeWidgetItem*>(sels.first());
      double dirSize = 0;
      //QTreeWidgetItem *parent = item->parent();

      QGuiApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
      traverseItems(item,dirSize);
      QGuiApplication::restoreOverrideCursor();
      /*while( parent )
      {
        dirEntry *entry = (dirEntry*)parent->data(3,Qt::UserRole).toLongLong();
        if( entry )
        {
          parent->setText(1,formatSize(entry->m_tocData.m_size));
          parent->setTextAlignment(1,Qt::AlignRight);
        }
        parent = parent->parent();
      }*/

    }
  }
}

void cleanupDialog::customEvent(QEvent */*event*/)
{
  QMessageBox::warning(this,"write error","could not create table of contents file.\nPlease check if destination disk is full.");
}

bool cleanupDialog::traverseItems(QTreeWidgetItem *startingItem,double &dirSize)
{
  bool ret = true;

  QFileInfo info(startingItem->text(3));
  if( info.isDir() && !m_cancel )
  {
    qDebug("dir: %s",startingItem->text(3).toLatin1().data());
    startingItem->setExpanded(true);
    startingItem->treeWidget()->scrollToItem(startingItem);

    static quint64 lastmsecs = 0;
    quint64 msecs = QDateTime::currentDateTime().toMSecsSinceEpoch();
    if( (msecs-lastmsecs)>200 )
    {
      qApp->processEvents();
      lastmsecs = msecs;
    }

    // is a directory
    QList<QTreeWidgetItem*> items;
    // traverseItems will change the child list, so we have to use an own list
    for( int i=0; i<startingItem->childCount() && !m_cancel; i++ )
      items.append(startingItem->child(i));
    for( int j=0; j<items.count() && !m_cancel; j++ )
      traverseItems(items.at(j),dirSize);

    QDir dir(startingItem->text(3));
    QStringList list(dir.entryList());
    if( list.count()<=2 )
    {
      QFile::remove(startingItem->text(3)+"/.DS_Store");
      QString name = dir.dirName();
      dir.cdUp();
      if( dir.rmdir(name) )
      {
        dirEntry *entry = (dirEntry*)startingItem->data(3,Qt::UserRole).toLongLong();
        if( entry->m_parent ) entry->m_parent->deleteDir(entry);

        delete entry;
        delete startingItem;

        m_dirStructChanged = true;
        ret = false;
      }
      else
        qDebug("++++ rmdir failed: %s\n",name.toLatin1().data());
    }
    else
      qDebug("nem: %s",startingItem->text(3).toLatin1().data());
    if( ret )
    {
      dirEntry *entry = (dirEntry*)startingItem->data(3,Qt::UserRole).toLongLong();
      startingItem->setText(1,formatSize(entry->m_tocData.m_size));
      startingItem->setTextAlignment(1,Qt::AlignRight);
    }
  }
  else if(!m_cancel)
  {
    qDebug("fil: %s",startingItem->text(3).toLatin1().data());
    const QTreeWidgetItem *item = startingItem;
    QFile::setPermissions(item->text(3),QFile::WriteOwner|QFile::WriteUser|QFile::WriteGroup|QFile::WriteOther);
    if( QFile::remove(item->text(3)) )
    {
      dirEntry *entry = (dirEntry*)item->data(3,Qt::UserRole).toLongLong();
      if( entry->m_parent ) entry->m_parent->deleteFile(entry);

      while( item->parent() )
      {
        dirEntry *it = (dirEntry*)item->parent()->data(3,Qt::UserRole).toLongLong();
        item->parent()->setText(1,formatSize(it->m_tocData.m_size));
        item = item->parent();
      }

      delete entry;
      delete startingItem;

      dirSize += (double)(info.size());

      m_dirStructChanged = true;
      ret = false;
    }
    else if( !m_ignore )
    {
      switch(
      QMessageBox::warning(0,"file error","cannot remove file\n"+item->text(3)+"\nThis file is write protected.",QMessageBox::Ok | QMessageBox::Cancel | QMessageBox::Ignore)
      )
      {
        case QMessageBox::Cancel:
          m_cancel = true;
          break;
        case QMessageBox::Ignore:
          m_ignore = true;
          break;
        default:
          break;
      }
    }
  }

  return ret;
}
