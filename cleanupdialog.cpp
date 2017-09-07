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
  delete m_metrics;
  delete ui;
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

  bool isEmpty = false;
  scanRelativePath(m_path,m_totalbytes,m_rootEntry,isEmpty);

  m_engine->setProgressText("");
  ui->progressBar->setMinimum(0);
  ui->progressBar->setMaximum(1);
  ui->progressBar->setValue(0);
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
/*double unformatSize( QString const &size )
{
  double scale = 1.0;
  if( size.contains("MB") )
    scale = 1024.0 * 1024.0;
  QString mantissa = size;
  mantissa = mantissa.replace("MB","");
  return mantissa.toDouble() * scale;
}*/

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

  int depth = 0;
  populateTree(m_rootEntry,item,depth);

  item->setText(1,formatSize(m_rootEntry->m_tocData.m_size/*m_totalbytes*/));
  item->setData(3,Qt::UserRole,QVariant(qint64(m_rootEntry)));
  item->setExpanded(true);

  ui->buttonBox->button(QDialogButtonBox::Cancel)->setDisabled(true);
  ui->buttonBox->button(QDialogButtonBox::Ok)->setDisabled(false);

  ui->treeView->setSortingEnabled(true);

  ui->progressBar->setMinimum(0);
  ui->progressBar->setMaximum(100);
  ui->progressBar->setValue(100);
  ui->label->setText("");
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

QDataStream &operator<<(QDataStream &out, const struct backupExecuter::fileTocEntry &src);
QDataStream &operator>>(QDataStream &in, struct backupExecuter::fileTocEntry &dst);

void cleanupDialog::analyzePath(QString const &path)
{

  ui->progressBar->setValue(0);
  ui->progressBar->setMinimum(0);
  ui->progressBar->setMaximum(0);
  ui->progressBar->setTextVisible(true);

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

void cleanupDialog::scanRelativePath( QString const &path, double &Bytes, dirEntry *entry, bool &isEmpty )
{
  if( canReadFromTocFile(path,entry) )
  {
    return;
  }

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

        double dirBytes = 0;
        scanRelativePath(path+"/"+name,dirBytes,newEntry,isEmpty);
      }
    }
    else
    {
      backupExecuter::fileTocEntry tocentry;
      tocentry.m_tocId = 0;//m_nextTocId++;
      tocentry.m_size = fileInfo.size();
      tocentry.m_modify = fileInfo.lastModified().toMSecsSinceEpoch();;
      tocentry.m_crc = 0;

      dirEntry *newEntry = new dirEntry(entry,name);
      newEntry->m_tocData = tocentry;

      entry->m_files.append(newEntry);

      Bytes += fileInfo.size();
      fileSizes += tocentry.m_size;
      if( tocentry.m_modify>lastModifiedFile ) lastModifiedFile = tocentry.m_modify;

//      entry->m_tocData.m_size += fileSizes;
//      if( lastModifiedFile>entry->m_tocData.m_modify ) entry->m_tocData.m_modify = lastModifiedFile;
    }
  }

  entry->updateDirInfo(fileSizes,lastModifiedFile);
}

bool cleanupDialog::canReadFromTocFile( QString const &path, dirEntry *entry )
{
  QMap<QString,QMap<QString,backupExecuter::fileTocEntry> > archiveContent;

  QString tocSummaryFile = path+"/tocsummary.crcs";
  QFile tocFile(tocSummaryFile);
  if( QFile::exists(tocSummaryFile) )
  {
    tocFile.open(QIODevice::ReadOnly);
    QDataStream str(&tocFile);
    str >> archiveContent;
    tocFile.close();

    QMap<QString,QMap<QString,backupExecuter::fileTocEntry> >::iterator it1 = archiveContent.begin();
    while( it1 != archiveContent.end() )
    {
      QString fullPath = it1.key();
      m_engine->setProgressText(fullPath);

      if( fullPath.startsWith("/") ) fullPath = fullPath.mid(1);
      QStringList paths = fullPath.split("/");

      QStringList::iterator it = paths.begin();
      dirEntry *currDir = entry;
      while ( it!=paths.end() )
      {
        QString currentPath = *it;
        if( currDir->m_dirs.contains(currentPath) )
          currDir = currDir->m_dirs[currentPath];
        else
        {
          dirEntry *newEntry = new dirEntry(currDir,currentPath);
          newEntry->m_tocData.m_tocId = 0;
          newEntry->m_tocData.m_size = 0;
          newEntry->m_tocData.m_modify = 0;
          newEntry->m_tocData.m_crc = 0;

          currDir->m_dirs[currentPath] = newEntry;
          currDir = newEntry;
        }
        ++it;
      }

      qint64 lastModifiedFile = 0;
      qint64 fileSizes = 0;
      if( !exclusions.contains(it1.key()) )
      {
        QMap<QString,backupExecuter::fileTocEntry>::iterator it2 = it1.value().begin();
        while( it2!=it1.value().end() )
        {
          backupExecuter::fileTocEntry tocentry;
          tocentry.m_tocId = it2.value().m_tocId;
          tocentry.m_size = it2.value().m_size;
          tocentry.m_modify = it2.value().m_modify;
          tocentry.m_crc = it2.value().m_crc;

          dirEntry *newEntry = new dirEntry(currDir,it2.key());
          newEntry->m_tocData = tocentry;

          currDir->m_files.append(newEntry);

          fileSizes += tocentry.m_size;
          if( tocentry.m_modify>lastModifiedFile ) lastModifiedFile = tocentry.m_modify;
          ++it2;
        }
      }

      currDir->updateDirInfo(fileSizes,lastModifiedFile);

      ++it1;
    }

    return true;
  }

  return false;
}

void cleanupDialog::populateTree( dirEntry *entry, QTreeWidgetItem *item, int &depth )
{
  depth++;

  static quint64 lastmsecs = 0;
  quint64 msecs = QDateTime::currentDateTime().toMSecsSinceEpoch();
  if( (msecs-lastmsecs)>200 )
  {
    qApp->processEvents();
    lastmsecs = msecs;
  }

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

    if( depth>2 )
      dirItem->setExpanded(false);
    else
    {
      dirItem->setExpanded(true);
      dirItem->treeWidget()->scrollToItem(dirItem);
      ui->treeView->resizeColumnToContents(0);
    }
    populateTree(it.value(),dirItem,depth);

    if( dirItem->childCount()==0 ) delete dirItem;

    ++it;
  }

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
    }

    ++it2;
  }

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
  ShellExecuteA(winId(), 0, name.toLocal8Bit(), 0, 0, SW_SHOWNORMAL);
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
          entry->m_tocData.m_size -= dirSize;
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

        ret = false;
      }
//      else
//        QMessageBox::warning(0,"dir error","directory\n"+startingItem->text(3)+"\nseems to have (hidden) content.");
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
