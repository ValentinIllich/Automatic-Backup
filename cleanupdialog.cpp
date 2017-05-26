#include "cleanupdialog.h"
#include "ui_cleanupdialog.h"

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
double unformatSize( QString const &size )
{
  double scale = 1.0;

  if( size.contains("MB") )
    scale = 1024.0 * 1024.0;

  QString mantissa = size;
  mantissa = mantissa.replace("MB","");
  return mantissa.toDouble() * scale;
}

QDateTime lastmodified = QDateTime::currentDateTime();

void cleanupDialog::doAnalyze()
{
  QString startingPath;

  if( m_analyzingPath.isEmpty() )
  {
    startingPath = QFileDialog::getExistingDirectory(this, tr("Open Directory"),"/home",QFileDialog::ShowDirsOnly);
  }
  else
    startingPath = m_analyzingPath;

  if( !startingPath.isEmpty() )
    analyzePath(startingPath);
}

void cleanupDialog::analyzePath(QString const &path)
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
  w.setSelectedDate(lastmodified.date());
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
    lastmodified = QDateTime(w.selectedDate());
    m_analyze = false;
  }
  else
  {
    lastmodified = QDateTime::currentDateTime();
    m_analyze = true;
  }

  ui->treeView->clear();
  ui->treeView->setSortingEnabled(false);

  QStringList rootColumns;
  rootColumns << path << "0" << QFileInfo(path).lastModified().toString("yyyy/MM/dd-hh:mm") << path;
  ui->treeView->setColumnCount(4);
  QTreeWidgetItem *item = new QTreeWidgetItem(ui->treeView,rootColumns);
  ui->treeView->addTopLevelItem(item);

  ui->progressBar->setValue(0);
  ui->progressBar->setMinimum(0);
  ui->progressBar->setMaximum(0);
  ui->progressBar->setTextVisible(true);

  m_run = true;

  m_depth = path.count('/');
  //m_chars = (ui->label->width() / metrics.width(startingPath) * startingPath.length()) - 5;

  ui->buttonBox->button(QDialogButtonBox::Cancel)->setDisabled(false);
  ui->buttonBox->button(QDialogButtonBox::Ok)->setDisabled(true);

  double totalbytes = 0;
  bool isEmpty = false;
  scanRelativePath(path,totalbytes,item,isEmpty);
  item->setText(1,formatSize(totalbytes));
  item->setExpanded(true);

  ui->buttonBox->button(QDialogButtonBox::Cancel)->setDisabled(true);
  ui->buttonBox->button(QDialogButtonBox::Ok)->setDisabled(false);

  ui->treeView->setSortingEnabled(true);

  ui->progressBar->setMinimum(0);
  ui->progressBar->setMaximum(100);
  ui->progressBar->setValue(100);
  ui->label->setText("");
}

static QString exclusions = "/Volumes";
static QDateTime lastTime;

void cleanupDialog::scanRelativePath( QString const &path, double &Bytes, QTreeWidgetItem *item, bool &isEmpty )
{
  QDir dir(path);
  dir.setFilter(QDir::Dirs | QDir::Files | QDir::Hidden | QDir::NoSymLinks);
  dir.setSorting(QDir::Size | QDir::Reversed);

  QDateTime current = QDateTime::currentDateTime();
  if( (lastTime.time().msecsTo(current.time()))>100 )
  {
    int textw = m_metrics->width("...");
    m_chars = path.length();

    while( m_metrics->width(path.left(m_chars))+textw>ui->label->width() )
      m_chars-=5;

    static quint64 lastmsecs = 0;
    QTime acttime = QDateTime::currentDateTime().time();
    quint64 msecs = acttime.hour()*3600000 + acttime.minute()*60000 + acttime.second()*1000 + acttime.msec();

    if( (msecs-lastmsecs)>200 )
    {
      ui->label->setText(path.left(m_chars)+"...");
      qApp->processEvents();
      lastmsecs = msecs;
    }
  }

  QFileInfoList list = dir.entryInfoList();
  // if the dir contains only two items ,namely "." and "..", it's really empty.
  if( list.size()<=2 )
    isEmpty = true;
  else
    isEmpty = false;

  for (int i = 0; m_run && (i < list.size()); ++i)
  {
    QFileInfo fileInfo = list.at(i);
    QString name = fileInfo.fileName();
    if( fileInfo.isDir() )
    {
      if( (name!="." && name!=".." && !exclusions.contains(fileInfo.absoluteFilePath()) ) )
      {
        QTreeWidgetItem *dirItem = NULL;
        double dirBytes = 0;

        if( item!=NULL )
          dirItem = new QTreeWidgetItem(item);

        bool isEmpty = false;
        if( m_analyze && (fileInfo.absoluteFilePath().count('/')>=(m_depth+4)) )
        {
          // if analyzing only, don't resolve recursions deeper than 4 levels into tree
          scanRelativePath(path+"/"+name,dirBytes,NULL,isEmpty);
          if( dirItem!=NULL ) dirItem->setText(0,name+"...");
        }
        else
        {
          scanRelativePath(path+"/"+name,dirBytes,dirItem,isEmpty);
          if( dirItem!=NULL )
          {
            if( !isEmpty && dirItem->childCount()==0 )
            {
              // if no items are found, but directory is not empty, don't display it
              delete dirItem;
              dirItem = NULL;
            }
            else
              dirItem->setText(0,name);
          }
        }

        if( dirItem!=NULL )
        {
          dirItem->setText(1,formatSize(dirBytes));
          dirItem->setText(2,fileInfo.lastModified().toString("yyyy/MM/dd-hh:mm"));
          dirItem->setText(3,fileInfo.absoluteFilePath());

          //dirItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
          dirItem->setBackgroundColor(0,Qt::lightGray);
          dirItem->setBackgroundColor(1,Qt::lightGray);

          if( fileInfo.absoluteFilePath().count('/')>=(m_depth+2) )
            dirItem->setExpanded(false);
          else
          {
            dirItem->setExpanded(true);
            dirItem->treeWidget()->scrollToItem(dirItem);
            ui->treeView->resizeColumnToContents(0);
          }
        }

        Bytes += dirBytes;
      }
    }
    else
    {
      bool srcFound = false;

      if( !m_sourcePath.isEmpty() )
      {
        //QString srcName = m_sourcePath+fileInfo.absoluteFilePath().remove(m_analyzingPath);
        QString srcName = m_sourcePath+fileInfo.absoluteFilePath().mid(m_analyzingPath.length());
        srcFound = QFile::exists(srcName);
      }

      if( !srcFound && fileInfo.lastModified()<lastmodified )
      {
        double size = fileInfo.size();

        if( item!=NULL )
        {
          QTreeWidgetItem *fileItem = new QTreeWidgetItem(item);

          fileItem->setText(0,name);
          fileItem->setText(1,formatSize(size));
          fileItem->setText(2,fileInfo.lastModified().toString("yyyy/MM/dd-hh:mm"));
          fileItem->setText(3,fileInfo.absoluteFilePath());
        }

        Bytes += size;
      }
    }
  }
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
        double actual = unformatSize(parent->text(1)) - dirSize;
        if( actual<0 ) actual = 0;
        parent->setText(1,formatSize(actual));
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
    QTime acttime = QDateTime::currentDateTime().time();
    quint64 msecs = acttime.hour()*3600000 + acttime.minute()*60000 + acttime.second()*1000 + acttime.msec();

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
