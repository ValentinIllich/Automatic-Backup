#include "cleanupdialog.h"
#include "ui_cleanupdialog.h"
#include "backupengine.h"
#include "backupExecuter.h"

#include <QFileDialog>
#include <QDateTime>
#include <QContextMenuEvent>
#include <QMenu>
#include <QProcess>
#include <QLineEdit>
#include <QcalendarWidget>
#include <QMessageBox>
#include <QFontMetrics>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

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
, m_engine(new backupEngine(*this))
{
  ui->setupUi(this);

  m_metrics = new QFontMetrics(ui->label->font());
  ui->buttonBox->button(QDialogButtonBox::Cancel)->setDisabled(true);

  QStringList header;
  header << "Path" << "Size" << "modified" << "full path";
  ui->treeView->setHeaderLabels(header);
  ui->treeView->hideColumn(3);
}

cleanupDialog::~cleanupDialog()
{
  if( m_dirStructValid && m_dirStructChanged )
    saveDirStruct();

  delete m_metrics;
  delete ui;
}

void cleanupDialog::saveDirStruct()
{
  QString tocSummaryFile = m_path+"/tocsummary.crcs";
  if( !backupDirstruct::convertToTocFile(tocSummaryFile,m_rootEntry ) )
      ;
}

void cleanupDialog::setPaths(QString const &srcpath,QString const &dstpath)
{
  m_sourcePath = srcpath;
  m_analyzingPath = dstpath;
  setWindowTitle("cleaning up "+dstpath);
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

  QString tocSummaryFile = m_path+"/tocsummary.crcs";
  if( backupDirstruct::convertFromTocFile(tocSummaryFile,m_rootEntry,m_dirCount)/*canReadFromTocFile(path,entry)*/ )
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
  int chars = 0;
  if( !text.isEmpty() )
  {
    chars = (int)((double)ui->label->width() / (double)metrics.width(text) * 0.8 * (double)text.length());
    if( chars<text.length() )
      ui->label->setText("..."+text.right(chars));
    else
      ui->label->setText(text);
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

  //double GBytes = size / 1024.0 / 1024.0 / 1024.0;
  double MBytes = size / 1024.0 / 1024.0;
  //double KBytes = size / 1024.0;

  /*if( GBytes>1.0 )
        result.sprintf("%8.3f GB",GBytes);
    else if( MBytes>1.0 )*/
  result.sprintf("%10.3f MB",MBytes);
  /*else if( KBytes>1.0 )
        result.sprintf("%8.3f KB",KBytes);
    else if( size>1.0 )
        result.sprintf("%8.3f B",size);*/
  return result;
}

void cleanupDialog::operationFinishedEvent()
{
  //populate QTreeWidget
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
  item->setData(3,Qt::UserRole,QVariant(qint64(m_rootEntry)));
  item->setExpanded(true);

  ui->buttonBox->button(QDialogButtonBox::Cancel)->setDisabled(true);
  ui->buttonBox->button(QDialogButtonBox::Ok)->setDisabled(false);

  ui->treeView->setSortingEnabled(true);

  m_engine->setProgressText("");
  m_engine->setProgressMaximum(1);
  m_engine->setProgressValue(0);
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
    setLimitDate();

    if( startingPath!=m_path )
    {
      analyzePath(startingPath);
      m_path = startingPath;
    }
    else
      operationFinishedEvent();
  }
}

static QString exclusions = "/Volumes";
static QDateTime lastTime;

QDataStream &operator<<(QDataStream &out, const struct fileTocEntry &src);
QDataStream &operator>>(QDataStream &in, struct fileTocEntry &dst);

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
  //m_chars = (ui->label->width() / metrics.width(startingPath) * startingPath.length()) - 5;

  ui->buttonBox->button(QDialogButtonBox::Cancel)->setDisabled(false);
  ui->buttonBox->button(QDialogButtonBox::Ok)->setDisabled(true);

  m_engine->start(true);
}

void cleanupDialog::setLimitDate()
{
  QDialog d(this);
  QVBoxLayout vert(&d);
  QHBoxLayout hor(&d);
  QCalendarWidget w(&d);
  QPushButton cancel("Cancel");
  QPushButton prevy("<<");
  QPushButton prevm("<");
  QPushButton nextm(">");
  QPushButton nexty(">>");
  QPushButton butt("OK");
  vert.addWidget(&w);
  hor.addWidget(&cancel);
  hor.addWidget(&prevy);
  hor.addWidget(&prevm);
  hor.addWidget(&nextm);
  hor.addWidget(&nexty);
  hor.addWidget(&butt);
  vert.addLayout(&hor);
  w.setSelectedDate(m_lastmodified.date());
  d.connect(&cancel,SIGNAL(clicked()),&d,SLOT(reject()));
  d.connect(&prevy,SIGNAL(clicked()),&w,SLOT(showPreviousYear()));
  d.connect(&prevm,SIGNAL(clicked()),&w,SLOT(showPreviousMonth()));
  d.connect(&nextm,SIGNAL(clicked()),&w,SLOT(showNextMonth()));
  d.connect(&nexty,SIGNAL(clicked()),&w,SLOT(showNextYear()));
  d.connect(&butt,SIGNAL(clicked()),&d,SLOT(accept()));
  d.setWindowTitle("find files older than");
  d.resize(d.sizeHint());
  if( d.exec()==QDialog::Accepted )
  {
    m_lastmodified = QDateTime(w.selectedDate());
    m_analyze = false;
  }
  else
  {
    m_lastmodified = QDateTime::currentDateTime();
    m_analyze = true;
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
    QString name = fileInfo.fileName();
    if( fileInfo.isDir() )
    {
      if( (name!="." && name!="..") )
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

  entry->updateDirInfos(fileSizes,lastModifiedFile);

  if( m_run ) m_dirStructValid = m_dirStructChanged = true;
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
    dirItem->setText(1,formatSize(it.value()->m_tocData.m_size));
    dirItem->setText(2,QDateTime::fromMSecsSinceEpoch(it.value()->m_tocData.m_modify).toString("yyyy/MM/dd-hh:mm"));
    dirItem->setText(3,it.value()->absoluteFilePath());
    dirItem->setData(3,Qt::UserRole,QVariant(qint64(it.value())));
    //dirItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    dirItem->setBackgroundColor(0,Qt::lightGray);
    dirItem->setBackgroundColor(1,Qt::lightGray);

    if( depth>1 )
      dirItem->setExpanded(false);
    else
    {
      dirItem->setExpanded(true);
      dirItem->treeWidget()->scrollToItem(dirItem);
      ui->treeView->resizeColumnToContents(0);
    }
    populateTree(it.value(),dirItem,depth,processedDirs);
    processedDirs++;

    if( dirItem->childCount()==0 ) delete dirItem;

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

      fileItem->setText(0,(*it2)->m_name);
      fileItem->setText(1,formatSize((*it2)->m_tocData.m_size));
      fileItem->setText(2,filedate.toString("yyyy/MM/dd-hh:mm"));
      fileItem->setText(3,(*it2)->absoluteFilePath());
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
  if (isKDERunning()) {
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
      /*if( item->childCount()==0 )*/ item=item->parent(); // open containing directory, not selected item
      /*            QString path;
            do
            {
                if( path.length()>0 )
                    path = item->text(0) + "/" + path;
                else
                    path = item->text(0);

                item = item->parent();
            } while( item );
*/
      openFile(/*path*/item->text(3));
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
      QTreeWidgetItem *parent = item->parent();
      traverseItems(item,dirSize);
      while( parent )
      {
        dirEntry *entry = (dirEntry*)parent->data(3,Qt::UserRole).toLongLong();
        if( entry )
        {
          //entry->m_tocData.m_size -= dirSize;
          parent->setText(1,formatSize(entry->m_tocData.m_size));
        }
        parent = parent->parent();
      }

    }
  }
}

bool cleanupDialog::traverseItems(QTreeWidgetItem *startingItem,double &dirSize)
{
  bool ret = true;

  QFileInfo info(startingItem->text(3));
  if( info.isDir() && !m_cancel )
  {
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
//      else
//        QMessageBox::warning(0,"dir error","directory\n"+startingItem->text(3)+"\nseems to have (hidden) content.");
    }
    if( !ret )
    {
      dirEntry *entry = (dirEntry*)startingItem->data(3,Qt::UserRole).toLongLong();
      startingItem->setText(1,formatSize(entry->m_tocData.m_size));
    }
    //else
    //    QMessageBox::warning(0,"dir",list.join(";"));
  }
  else if(!m_cancel)
  {
    const QTreeWidgetItem *item = startingItem;
    QFile::setPermissions(item->text(3),QFile::WriteOwner|QFile::WriteUser|QFile::WriteGroup|QFile::WriteOther);
    if( QFile::remove(item->text(3)) )
    {
      dirEntry *entry = (dirEntry*)item->data(3,Qt::UserRole).toLongLong();
      if( entry->m_parent ) entry->m_parent->deleteFile(entry);

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
