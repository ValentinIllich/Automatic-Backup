#include "cleanupDialog.h"
#include "ui_cleanupDialog.h"
#include "backupEngine.h"
#include "backupExecuter.h"
#include "Utilities.h"
#include "crc32.h"

#include <QStyledItemDelegate>
#include <QPainter>
#include <QThread>
#include <QFileDialog>
#include <QInputDialog>
#include <QDateTime>
#include <QContextMenuEvent>
#include <QMenu>
#include <QProcess>
#include <QLineEdit>
#include <QCalendarWidget>
#include <QMessageBox>
#include <QFontMetrics>
#include <QDebug>

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
      double percent = 0;
      dirEntry *entry = (dirEntry*)item->data(3,Qt::UserRole).toLongLong();
      if( entry && entry->m_parent )
      {
        qint64 size = entry->m_tocData.m_size;
        qint64 parsize = entry->m_parent->m_tocData.m_size;
        percent = parsize==0 ? 0 : double(size * 100) / double(parsize);
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
      else if( item->parent()!=nullptr )
      {
        if( entry && item->childCount()>0 ) painter->fillRect(itemOption.rect,Qt::lightGray);
        QColor col;
        //col.setGreen(255-2*percent);
        //col.setRed(percent);
        //col.setBlue(0);
        // h=0 rot h=120 grün
        // s=100
        // L=50
        col.setHsl(100-percent,220,128);
        painter->fillRect(itemOption.rect.x(),itemOption.rect.y(),w,itemOption.rect.height(),col/*Qt::gray*/);
        painter->setPen(Qt::black);
      }
      else if( item->childCount()>0 )
        painter->fillRect(itemOption.rect,Qt::lightGray);

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
  {
    m_hashMap.clear();
    scanRelativePath(m_path,m_rootEntry,m_dirCount);
  }

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
  result.sprintf("%'15.3f MB",MBytes);
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
  item->setFont(1,QFont("Courier",12));
  item->setFont(2,QFont("Courier",12));
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
  m_engine->setProgressText(QString::number(info.m_capacity)+"% of disk space used ("+formatSize(info.m_freeBytes)+" remaining)");
  m_engine->setProgressMaximum(1);
  m_engine->setProgressValue(0);

  QTreeWidgetItem *duplicatesBase = nullptr;
  for( auto &it : qAsConst(m_hashMap) )
  {
    if( it.size()>1 )
    {
        if( duplicatesBase==nullptr )
        {
          duplicatesBase = new QTreeWidgetItem(ui->treeView,rootColumns);
          ui->treeView->addTopLevelItem(duplicatesBase);
          duplicatesBase->setText(0,"identical files found:");
          duplicatesBase->setText(1,"");
          duplicatesBase->setText(2,"");
        }
        QTreeWidgetItem *entry = new QTreeWidgetItem(duplicatesBase);
        QFileInfo info(it.at(0));
        entry->setText(0,info.fileName());
        for( int i=0; i<it.size(); i++ )
        {
          QFileInfo info(it.at(i));
          QTreeWidgetItem *item1 = new QTreeWidgetItem(entry);
          item1->setText(0,info.path());
          item1->setText(3,info.path());
          QTreeWidgetItem *item2 = new QTreeWidgetItem(item1);
          item2->setText(0,info.fileName());
          item2->setText(3,it.at(i));
        }
    }
  }

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

      QString tocSummaryFile = backupDirStruct::getTocSummaryFile(startingPath);
      QFileInfo info(tocSummaryFile);
      if( info.exists() )
      {
        if( info.lastModified().daysTo(QDateTime::currentDateTime())>30 )
          QMessageBox::information(this,"information","The Table of Contents in ths directory is older than 30 days. Please consider doing a rescan.");
      }
      else
      {
        if( QMessageBox::question(0,"checksum reset","Recalculate all verify file checksums\nduring scan (will be slower)?",QMessageBox::Yes,QMessageBox::No)==QMessageBox::Yes )
          m_resetCrc = true;
        else
          m_resetCrc = false;
      }

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
  QString startingPath = m_analyzingPath;

//  startingPath = QFileDialog::getExistingDirectory(this, tr("Rescan Directory"),m_path,QFileDialog::ShowDirsOnly);
//  if( !startingPath.isEmpty() )
  {
    QString tocSummaryFile = backupDirStruct::getTocSummaryFile(startingPath);
    if( QFile::exists(tocSummaryFile) )
      QFile::remove(tocSummaryFile);

    m_dirStructValid = false;
    m_analyzingPath = startingPath;

    m_doRescan = true;
    m_fileFilter = "";
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

    ui->treeView->clear();
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

      ///////
      const int buffsize = 1*1024*1024;

      Crc32 crcsum;
      quint32 currentCrc = 0;
      QFile src(fileInfo.absoluteFilePath());
      if( src.open(QIODevice::ReadOnly) )
      {
        crcsum.initInstance(0);
        while( m_run )
        {
          QByteArray bytes = src.read(buffsize);
          crcsum.pushData(0,bytes.data(),bytes.length());
          if( !m_resetCrc )
            break;
          if( bytes.length()<buffsize )
            break;
        }
        currentCrc = crcsum.releaseInstance(0);
        if( m_run && m_resetCrc )
          tocentry.m_crc = currentCrc;
        src.close();
      }
      /////////

      dirEntry *newEntry = new dirEntry(entry,name);
      newEntry->m_tocData = tocentry;

      entry->m_files.append(newEntry);

      QString fullPath = newEntry->absoluteFilePath();
      if( !fullPath.contains("._") && currentCrc>0 )
        m_hashMap[currentCrc].push_back(fullPath);

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

void cleanupDialog::displayResult( QWidget *parent, QString const &text, QString const windowTitle )
{
  QDialog d(parent);
  QVBoxLayout *l = new QVBoxLayout(&d);
  QTextEdit *ed = new QTextEdit(&d);
  QPushButton *ok = new QPushButton(&d);
  l->addWidget(ed);
  l->addWidget(ok);

  ed->setFont(QFont("Courier"));
  ed->setLineWrapMode(QTextEdit::NoWrap);
  ed->setWordWrapMode(QTextOption::NoWrap);
  ed->setReadOnly(true);
  ed->setText(text);
  ok->setText("OK");
  ok->setSizePolicy(QSizePolicy::Maximum,QSizePolicy::Fixed);
  connect(ok,SIGNAL(clicked()),&d,SLOT(accept()));
  d.setWindowTitle(windowTitle);
  d.resize(800,600);
  d.exec();
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
    dirItem->setFont(1,QFont("Courier",12));
    dirItem->setTextAlignment(1,Qt::AlignRight);
    dirItem->setText(2,QDateTime::fromMSecsSinceEpoch(it.value()->m_tocData.m_modify).toString("yyyy/MM/dd-hh:mm"));
    dirItem->setFont(2,QFont("Courier",12));
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

  QStringList selections = m_fileFilter.isEmpty() ? QStringList() : m_fileFilter.split(";");

  QList<dirEntry*>::iterator it2 = entry->m_files.begin();
  while( m_run && it2!=entry->m_files.end() )
  {
    QDateTime filedate = QDateTime::fromMSecsSinceEpoch((*it2)->m_tocData.m_modify);
    bool fileSelected = true;
    if( !selections.isEmpty() )
    {
      fileSelected = false;
      for( auto & selection : qAsConst(selections) )
      {
        if( (*it2)->absoluteFilePath().contains(selection) )
          fileSelected = true;
      }
    }
    if( filedate<m_lastmodified && fileSelected )
    {
      QTreeWidgetItem *fileItem = new QTreeWidgetItem(item);

      fileItem->setText(0,(*it2)->m_tocData.m_prefix+(*it2)->m_name);
      fileItem->setText(1,formatSize((*it2)->m_tocData.m_size));
      fileItem->setFont(1,QFont("Courier",12));
      fileItem->setTextAlignment(1,Qt::AlignRight);
      fileItem->setText(2,filedate.toString("yyyy/MM/dd-hh:mm"));
      fileItem->setFont(2,QFont("Courier",12));
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
  QString name;
  QString webbrowser;

  QStringList args;

#if defined(Q_OS_WIN)
  name = "file://"+fn;
  ShellExecuteA((HWND)winId(), 0, name.toLocal8Bit(), 0, 0, SW_SHOWNORMAL);
#endif
#if defined(Q_OS_MAC)
  webbrowser = "open";
  name = fn;
#elif defined(Q_WS_X11)
  if (isKDERunning())
  {
    webbrowser = "kfmclient";
    args.append(QLatin1String("exec"));
    name = "file://"+fn;
  }
#endif

  if( !webbrowser.isEmpty() )
  {
    QProcess *proc = new QProcess(this);
    QObject::connect(proc, SIGNAL(finished(int)), proc, SLOT(deleteLater()));

    args.append(name);

    qDebug() << webbrowser << args;
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
  QAction *filter = menu.addAction("Set Filter...");
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
  else if( selected==filter )
  {
    bool ok;
    QString text = QInputDialog::getText(this, ("display only files"),
                                         "file name contains:", QLineEdit::Normal,
                                         m_fileFilter, &ok);
    if (ok)
    {
        m_fileFilter = text;

        doAnalyze();
    }
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
      discInfo info = getDiscInfo(m_analyzingPath);
      m_engine->setProgressText(QString::number(info.m_capacity)+"% of disk space used ("+formatSize(info.m_freeBytes)+" remaining)");
      m_engine->setProgressMaximum(1);
      m_engine->setProgressValue(0);

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
      QFile::remove(startingItem->text(3)+"/._.DS_Store");
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
      startingItem->setFont(1,QFont("Courier",12));
      startingItem->setTextAlignment(1,Qt::AlignRight);
    }
  }
  else if(!m_cancel)
  {
    qDebug("fil: %s",startingItem->text(3).toLatin1().data());
    const QTreeWidgetItem *item = startingItem;
    QFile::setPermissions(item->text(3),QFile::WriteOwner|QFile::WriteUser|QFile::WriteGroup|QFile::WriteOther);
    bool removedFromDisk = true;
    if( QFile::exists(item->text(3)) && !QFile::remove(item->text(3)) )
      removedFromDisk = false;
    if( removedFromDisk )
    {
      dirEntry *entry = (dirEntry*)item->data(3,Qt::UserRole).toLongLong();
      if( entry )
      {
        if( entry->m_parent ) entry->m_parent->deleteFile(entry);

        while( item->parent() )
        {
          dirEntry *it = (dirEntry*)item->parent()->data(3,Qt::UserRole).toLongLong();
          item->parent()->setText(1,formatSize(it->m_tocData.m_size));
          item->parent()->setFont(1,QFont("Courier",12));
          item = item->parent();
        }

        delete entry;
      }

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
