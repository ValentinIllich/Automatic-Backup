#include "backupExecuter.h"
#include "backupMain.h"
#include "Utilities.h"

#include <QSettings>
#include <QFileDialog>
#include <QTextBrowser>
#include <QCalendarWidget>
#include <QDesktopWidget>
#include <QThread>
#include <QWidget>
#include <QMenu>

#define BUFF_SIZE (5*1024*1024)

QString backupExecuter::nullStr = "";
QFile backupExecuter::fileObj;

static const unsigned long header = 'VISL';
static const unsigned long magic = 'BKUP';

class waitHelper : public QThread
{
public:
  void Sleep(int msecs) { msleep(msecs); }
};

class fullMessage : public QMessageBox
{
public:
  fullMessage( QWidget *parent, QString const &path )
  : QMessageBox(QMessageBox::NoIcon,"destination drive full","destination drive is full.\n\nPlease remove some files.",QMessageBox::Cancel,parent)
  , m_path(path)
  {
    QApplication::alert(this);
    setIconPixmap(QApplication::style()->standardPixmap(QStyle::SP_DriveHDIcon));
    startTimer(60000);
  }
protected:
  virtual void timerEvent ( QTimerEvent */* event*/ )
  {
    accept();
  }
private:
  QString	m_path;
};
class mediaMessage : public QMessageBox
{
public:
  mediaMessage( QWidget *parent, QString const &path )
  : QMessageBox(QMessageBox::NoIcon,"removable media not found","'"+path+"' not found.\n\nPlease insert removable media containing this path.",QMessageBox::Cancel,parent)
  , m_path(path)
  {
    QApplication::alert(this);
    setIconPixmap(QApplication::style()->standardPixmap(QStyle::SP_DriveHDIcon));
    startTimer(2000);
  }
  QString getPath()
  {
    return m_path;
  }
protected:
  virtual void timerEvent ( QTimerEvent */* event*/ )
  {
    bool tryMultiple = false;
    int deviceNo = 3;
    do
    {
      if( m_path.length()>=2 )
      {
        if( m_path.at(1)==':' )
          m_path[1] = 'A'+deviceNo;

        QDir dir(m_path);
        if( dir.exists() )
          accept();

        if( m_path.at(1)==':' )
          tryMultiple = (deviceNo<10);
      }
    } while(tryMultiple);
  }
private:
  QString	m_path;
};

static waitHelper m_waiter;
static quint32 crcDataVersion;

backupExecuter::backupExecuter(backupConfigData &configData)
: m_config(configData)
, log(NULL)
, m_bringToFront(false)
, m_isBatch(false)
, m_running(false)
, m_error(false)
//, background()
, m_dirsCreated(false)
, m_closed(false)
, m_background(false)
, m_diskFull(false)
, m_runUnattended(false)
//, m_batchRunning(false)
, collectingDeleted(false)
, dircount(0)
, files_to_copy(0)
, kbytes_to_copy(0)
, lastVerifiedK(0)
, verifiedK(0)
, checksumsChanged(false)
, m_engine(new backupEngine(*this))
{
  setObjectName("backupExecuter");
  setupUi(this);

  setWindowTitle("Backup Configuration '"+m_config.m_sName+"'");

  connect(qApp->desktop(),SIGNAL(resized(int)),this,SLOT(screenResizedSlot(int)));

  loadData();

  QSettings settings;
  int titleheight = settings.value("WindowTitlebarHeight").toInt();
  int framewidth = settings.value("WindowFramewidth").toInt();
  int w = width();
  int h = minimumSizeHint().height();
  QRect screen = qApp->desktop()->availableGeometry();
  setGeometry(screen.x()+screen.width()/2-w/2-framewidth,screen.y()+screen.height()/2-(h+titleheight)/2,w,h);

  startTimer(100);
}

QDataStream &operator<<(QDataStream &out, const struct crcInfo &src)
{
  out << src.crc << (quint64)src.lastScan;
  return out;
}
QDataStream &operator>>(QDataStream &in, struct crcInfo &dst)
{
  quint64 timestamp;
  in >> dst.crc;
  if( crcDataVersion&0x80000000 )
  {
    in >> timestamp;
    dst.lastScan = timestamp;
  }
  return in;
}


backupExecuter::~backupExecuter()
{
  m_engine->processEventsAndWait();
  //saveData();
  delete m_engine;
}

void backupExecuter::screenResizedSlot( int /*screen*/ )
{
  changeVisibility();
}

void backupExecuter::loadData()
{
  crcSummary.clear();

  QString summaryFile = backupDirStruct::getChecksumSummaryFile(m_config.m_sDst);
  QFile crcfile(summaryFile);
  if( QFile::exists(summaryFile) )
  {
    crcfile.open(QIODevice::ReadOnly);
    QDataStream str(&crcfile);
    str >> crcDataVersion;
    if( crcDataVersion&0x80000000 )
      str >> lastVerifiedK;
    else
      lastVerifiedK = crcDataVersion;
    str >> crcSummary;
    crcfile.close();
  }
  else
    lastVerifiedK = 0;

  QString tocSummaryFile = backupDirStruct::getTocSummaryFile(m_config.m_sDst);
  m_dirs.readFromFile(tocSummaryFile);
}

void backupExecuter::saveData()
{
  bool isOk = true;
  if( checksumsChanged )
  {
    QString summaryFile = backupDirStruct::getChecksumSummaryFile(m_config.m_sDst);
    QFile crcfile(summaryFile);
    crcfile.open(QIODevice::WriteOnly | QIODevice::Truncate);
    QDataStream str(&crcfile);
    str << 0x80000000;
    str << lastVerifiedK;
    str << crcSummary;
    isOk = (crcfile.error()==QFileDevice::NoError);
    crcfile.close();
  }
  checksumsChanged = false;

  if( isOk && m_dirs.tocHasChanged() )
  {
    QString tocSummaryFile = backupDirStruct::getTocSummaryFile(m_config.m_sDst);
    isOk = m_dirs.writeToFile(tocSummaryFile);
  }

  if( !isOk )
  {
    QString str = "++++ destination drive is full. Could not update the TOC data!\r\n";
    stream << str; errstream.append(str);
    m_diskFull = true;
  }
}

void backupExecuter::changeVisibility()
{
  if( m_config.m_bBackground )
  {
    progresslabel->hide();

    if( m_isBatch )
    {
      actualfile->hide();
      cancelButt->hide();
      m_engine->setToolTipText(QString("Automatic Backup V")+BACKUP_STR_VERSION+" running.\nProcessing Configuration '"+m_config.m_sName+"'");
    }
    else
    {
      actualfile->setFixedWidth(450);
      actualfile->setAlignment(Qt::AlignRight);
    }

    // give the layouts time to process size chages
    qApp->processEvents(QEventLoop::ExcludeUserInputEvents,10000);

    int height = minimumSizeHint().height();
    int width = minimumSizeHint().width();

    setWindowOnScreen(this,width,height);
  }
  else
  {
    resize( width(), 0 ); // dialog wil be as small as possible
    setWindowTitle("Processing Configuration '"+m_config.m_sName+"'");
  }
}

void backupExecuter::startBatchProcessing()
{
  startingAction();

  m_isBatch = true;

  changeVisibility();
}
void backupExecuter::stopBatchProcessing()
{
  m_isBatch = false;

  stoppingAction();
}

QString const &backupExecuter::defaultAt( QStringList const &list, int ix )
{
  if( (ix<0) || (ix>=list.count()) )
    return nullStr;
  else
    return list.at(ix);
}

QFile *backupExecuter::openFile( QString const &filename, bool readOnly )
{
  QSettings settings;
  QString path = settings.value("LogFile","").toString();
  if( path.isEmpty() ) settings.setValue("LogFile",path = "./backup.log");

  if( fileObj.isOpen() ) fileObj.close();

  fileObj.setFileName(filename.isNull() ? path : path.replace("backup",filename));
  if( fileObj.size()>2000000 ) fileObj.remove();
  if( fileObj.open(readOnly ? QIODevice::ReadOnly : QIODevice::ReadWrite|QIODevice::Append) )
    return &fileObj;
  else
  {
    fileObj.setFileName(QString::null);
    return &fileObj;
  }
}

void backupExecuter::displayResult( QWidget *parent, QString const &text, QString const windowTitle )
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

void backupExecuter::setWindowOnScreen(QWidget *widget,int width,int height)
{
  QSettings settings;
  QString position = settings.value("BackupWindowPosition","ll").toString();
  int titleheight = (widget->objectName()=="backupExecuter") ? settings.value("WindowTitlebarHeight").toInt() : 0;
  int framewidth = (widget->objectName()=="backupExecuter") ? settings.value("WindowFramewidth").toInt() : 0;
  QRect screen = qApp->desktop()->availableGeometry();

  if( position=="ll" )
    widget->setGeometry(screen.x(),screen.y()+screen.height()-height,width,height); // lower left (the default)
  else if( position=="ur" )
    widget->setGeometry(screen.x()+screen.width()-width-framewidth,screen.y()+titleheight,width,height); // upper right
  else if( position=="ul" )
    widget->setGeometry(screen.x(),screen.y()+titleheight,width,height); // upper left
  else if( position=="lr" )
    widget->setGeometry(screen.x()+screen.width()-width-framewidth,screen.y()+screen.height()-height,width,height); // lower right
}

QDateTime backupExecuter::getLimitDate(QWidget *parent,const QDateTime &startingDate)
{
  QDateTime result;

  QDialog d(parent);
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
  w.setSelectedDate(startingDate.date());
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
    result = QDateTime(w.selectedDate());
  }
  else
  {
    result = QDateTime();
  }

  return result;
}

void backupExecuter::processProgressMaximum(int maximum)
{
  progressbar->setMaximum(maximum);
}

void backupExecuter::processProgressValue(int value)
{
  progressbar->setValue(value);
}

void backupExecuter::processProgressText(QString const &text)
{
  QFontMetrics metrics(progresslabel->font());
  if( !text.isEmpty() )
  {
    QString messagetext = text;
    while( progresslabel->width()<metrics.width(messagetext) )
    {
      int len = messagetext.length() - 4;
      messagetext = text.left(len/2)+"..."+messagetext.right(len/2);
    }
      progresslabel->setText(messagetext);
  }
  else
    progresslabel->setText(text);
}

void backupExecuter::processFileNameText(QString const &text)
{
  QFontMetrics metrics(actualfile->font());
  if( !text.isEmpty() )
  {
    QString messagetext = text;
    while( actualfile->width()<metrics.width(messagetext) )
    {
      int len = messagetext.length() - 4;
      messagetext = text.left(len/2)+"..."+messagetext.right(len/2);
    }
      actualfile->setText(messagetext);
  }
  else
    actualfile->setText(text);
}

void backupExecuter::startingAction()
{
  if( m_isBatch ) return;

  m_running=true;
  m_error=false;
  m_dirsCreated = false;
  cancelButt->setText("Cancel");

  if( m_config.m_bVerbose || m_config.m_bVerboseMaint )
  {
    if( buff.open(QIODevice::Truncate|QIODevice::WriteOnly) )
      stream.setDevice(&buff);
  }
  else
  {
    log = openFile(QString::null);
    if( log->fileName()!=QString::null )
      stream.setDevice(log);
    else if( m_config.m_bAuto )
      stream << "++++ problem found in backup: directory doesn't exist: couldn't open log file " << "\r\n";
    else
      QMessageBox::warning(this,"backup problems","couldn't open log file 'backup.log'");
  }

  QString str = "================ backup '" + m_config.m_sName + "' starting on " + QDateTime::currentDateTime().toString() + " ================\r\n";
  stream << str; errstream.append(str);

  QStringList incexcl = m_config.m_sFlt.remove("+").split("-");
  QString includingsstr = defaultAt(incexcl,0);
  QString excludingsstr = defaultAt(incexcl,1);

  QStringList includings = includingsstr.remove(")").split("(");
  QStringList excludings = excludingsstr.remove(")").split("(");

  dirincludes = defaultAt(includings,0).split( ";" );
  fileincludes = defaultAt(includings,1).split( ";" );
  direxcludes = defaultAt(excludings,0).split( ";" );
  fileexcludes = defaultAt(excludings,1).split( ";" );

  stream << "regarding directory names containing '" << defaultAt(includings,0) <<  "' but not '" << defaultAt(excludings,0) << "'\r\n";
  stream << "regarding file names containing '" << defaultAt(includings,1) << "' but not'" << defaultAt(excludings,1) << "'\r\n";

  directories.clear();
  filelist.clear();
  files_to_copy = 0;
  kbytes_to_copy = 0;

  dircount = 1;

  startTime = QDateTime::currentDateTime();
}
void backupExecuter::stoppingAction()
{
  QString str = "================ backup '" + m_config.m_sName + "' ending on " + QDateTime::currentDateTime().toString() + " ================\r\n";
  stream << str;

  if( m_config.m_bVerbose || m_config.m_bVerboseMaint )
  {
    buff.close();
    displayResult(this,QString(buff.buffer()));
  }
  else
  {
    if( m_error )
    {
      if( m_runUnattended )
      {
        QString str = "#### problem(s) found in backup: one or more files couldn't be copied to destination.\r\n";
        stream << str;
        QFile *fp = openFile("errors");
        if( fp->fileName()!=QString::null )
        {
          fp->write(errstream.toLatin1().data());
          fp->close();
        }
      }
      else
      {
        if( m_diskFull )
        {
          fullMessage msg(this,m_config.m_sDst);
          msg.exec();
        }
        else
        {
          QApplication::alert(this);
          QMessageBox::warning(this,"backup problems","problems occured during backup.\nPlease refer to 'backup.log' for details.");
          displayResult(this,errstream,"backup errors");
        }
      }

      m_error = false;
      errstream = "";
    }
    //buff.close();
    log->close();
  }

  m_running=false;
  if( m_isBatch ) return;

  stream.setDevice(0);

  m_engine->setProgressText("");
  m_engine->setFileNameText("");
  cancelButt->setText("OK");
}

bool backupExecuter::updateAutoBackupTime()
{
  stream << "updating backup time of automatic item '"+m_config.m_sName+"'\r\n";
  QFile tst(getAutobackupCheckFile(""));
  bool wasFound = tst.exists();
  if( tst.open(QIODevice::WriteOnly) )
  {
    QTextStream stream(&tst);
    stream << QDateTime::currentDateTime().toString("yyyyMMddhhmmss");
    tst.close();
  }
  else
    stream << "++++ couldn't create automatic test item\r\n";

  return wasFound;
}

bool backupExecuter::updateAutoVerifyTime()
{
  stream << "updating verify time of automatic item '"+m_config.m_sName+"'\r\n";
  QFile tst(getAutobackupCheckFile("_chk"));
  bool wasFound = tst.exists();
  if( tst.open(QIODevice::WriteOnly) )
  {
    QTextStream stream(&tst);
    stream << QDateTime::currentDateTime().toString("yyyyMMddhhmmss");
    tst.close();
  }
  else
    stream << "++++ couldn't create automatic test item\r\n";

  return wasFound;
}

void backupExecuter::checkTimeout()
{
  if( m_config.m_bsuspend )
  {
    static int times[] = { 10,15,30,60,90,120,150,180,210,240 };
    int maxtime = times[m_config.m_iTimeout]*60;

    if( startTime.secsTo(QDateTime::currentDateTime())>maxtime )
    {
      stream << "---> suspending backup due to given timeout\r\n";
      cancel();
    }
  }
}

void backupExecuter::findDirectories( QString const &start )
{
  if( start.isEmpty() )
  {
    m_engine->setProgressText("Finding Directories...");
    m_engine->setProgressMaximum(m_dirs.size());

    directories.append(m_config.m_sSrc+"/");
    findDirectories(m_config.m_sSrc+"/");
  }
  else
  {
    QDir dir(start);

    dir.setFilter( QDir::AllDirs | QDir::Dirs | QDir::NoSymLinks );
    //dir.setMatchAllDirs(true);
    dir.setNameFilters(QStringList("*"));

    if( !dir.exists() )
    {
      m_error = true;
      QString str = "++++ problem found in backup: given source directory doesn't exist: '" + start + "'\r\n";
      stream << str; errstream.append(str);
      return;
    }

    const QFileInfoList &list = dir.entryInfoList();
    QFileInfoList::const_iterator it = list.begin();
    QFileInfo fi;

    while ( m_running && (it!=list.end()) )
    {
      checkTimeout();

      fi = *it;
      QString actual = fi.fileName();
      if( actual!="." && actual!=".." )
      {
        QString path = start+actual;
        bool passed = true;
        bool stepinto = true;

        if( !dirincludes.isEmpty() )
          passed = false;
        for ( QStringList::Iterator it2 = dirincludes.begin(); it2 != dirincludes.end(); ++it2 )
        {
          if( (*it2).isEmpty() || path.contains(*it2) )
            passed = true;
        }
        for ( QStringList::Iterator it3 = direxcludes.begin(); passed && it3 != direxcludes.end(); ++it3 )
        {
          if( !(*it3).isEmpty() && path.contains(*it3) )
          {
            passed = false;
            stepinto = false;
          }
        }

        if( passed )
        {
          if( m_config.m_bVerbose )
            stream << "checking directory " << path << "\r\n";
        }
        else
        {
          stream << "        skipping directory " << path << "\r\n";
        }

        if( passed )
        {
          dircount++;
          if( dircount>progressbar->maximum() )
            m_engine->setProgressMaximum(0); // show busy indicator
          else
            m_engine->setProgressValue(dircount);
          m_engine->setFileNameText(path);
          directories.append(path);
        }
        if( stepinto )
          findDirectories(path+"/");
      }
      ++it;
    }
  }
}

void backupExecuter::analyzeDirectories()
{
  if( !m_running ) return;

  int i = 0;
  unsigned totalcount = 0;
  unsigned long totaldirkbytes = 0;

  m_engine->setProgressText("Scanning Directories...");
  m_engine->setProgressMaximum(dircount);

  for ( QStringList::Iterator it=directories.begin(); m_running && it!=directories.end(); ++it,i++ )
  {
    checkTimeout();

    if( m_isBatch )
        m_engine->setToolTipText(QString("Automatic Backup V")+BACKUP_STR_VERSION+" running.\nProcessing Directory '"+*it+"'");
    m_engine->setFileNameText(*it);
    m_engine->setProgressValue(i);
    if( m_config.m_bBackground )
      m_waiter.Sleep(20);

    QString srcpath = *it;
    QString dstpath = ensureDirExists(srcpath,m_config.m_sSrc,m_config.m_sDst);

    QDir dir(srcpath);

    dir.setFilter( QDir::Files | QDir::NoSymLinks );
    //dir.setMatchAllDirs(true);
    dir.setNameFilters(QStringList("*"));

    if( !dir.exists() )
    {
      m_error = true;
      QString str = "++++ problem found in backup: source directory doesn't exist: '"+srcpath+"'\r\n";
      stream << str; errstream.append(str);
      continue;
    }

    const QFileInfoList &list = dir.entryInfoList();
    QFileInfoList::const_iterator it2=list.begin();
    QFileInfo srcFile;

    unsigned count = 0;
    unsigned long dirkbytes = 0;

    while ( m_running && (it2!=list.end()) )
    {
      checkTimeout();

      if( m_config.m_bBackground )
        m_waiter.Sleep(20);

      srcFile = *it2;
      QString actualName = srcFile.fileName();
      QString fullName = srcFile.absoluteFilePath();
      bool passed = true;
      bool copy = true;

      if( !fileincludes.isEmpty() )
        passed = false;
      for ( QStringList::Iterator it3 = fileincludes.begin(); it3 != fileincludes.end(); ++it3 )
      {
        if( (*it3).isEmpty() || fullName.contains(*it3) )
          passed = true;
      }
      for ( QStringList::Iterator it4 = fileexcludes.begin(); passed && it4 != fileexcludes.end(); ++it4 )
      {
        if( !(*it4).isEmpty() && fullName.contains(*it4) )
          passed = false;
      }
      if( backupDirStruct::isTocSummaryFile(actualName) || backupDirStruct::isChecksumSummaryFile(actualName) )
        passed = false;

      if( isAutoBackupCreatedFile(actualName) )
      {
        copy = false;
      }
      else if( !srcFile.isSymLink() && actualName!="." && actualName!=".." )
      {
        if( passed )
        {
          QString filePath = srcFile.dir().absolutePath();
          QString fileName = srcFile.fileName();
          qint64 modify = srcFile.lastModified().toMSecsSinceEpoch();

          m_engine->setFileNameText(filePath+"/"+fileName);
          QString relPath = filePath.mid(m_config.m_sSrc.length()+1);
          if( m_dirs.exists(relPath,fileName) )
          {
            qint64 lastm = m_dirs.lastModified(relPath,fileName);
            if( modify>lastm )
              copy = true;
            else
              copy = false;
          }
          else
          {
            copy = true;
          }
        }
        else
        {
          stream << "        skipping file " << fullName << "\r\n";
          copy = false;
        }
      }

      if( copy )
      {
        qint64 filesize = srcFile.size();
        QString relName = fullName.mid(m_config.m_sSrc.length());
        filelist.append(relName);
        files_to_copy++;
        kbytes_to_copy += (filesize/1024);
        count++;
        dirkbytes += (filesize/1024);
      }

      ++it2;
    }

    if( count>0 )
    {
      stream << "---> " << count << " updated files with " << dirkbytes << " Kbytes found in " << srcpath << "\r\n";
      totalcount += count;
      totaldirkbytes += dirkbytes;
    }
  }
  if( totalcount>0 )
  {
    stream << "====> " << totalcount << " changed files with " << totaldirkbytes << " Kbytes\r\n";
  }
}

QString backupExecuter::ensureDirExists( QString const &fullPath, QString const &srcBase, QString const &dstBase )
{
  QString relpath = fullPath.mid(srcBase.length());
  QString dstpath = relpath.prepend(dstBase);

  QDir dst(dstpath);
  if( !dst.exists() )
  {
    if( m_config.m_bVerboseMaint )
      stream << dst.absolutePath() << " not found in destination, would be created\r\n";
    else
    {
      stream << dst.absolutePath() << " not found in destination, so creating it\r\n";

      if( !dst.mkpath(dstpath) )
      {
        m_error = true;
        QString str = "++++ can't create directory '"+dst.absolutePath()+"'!\r\n";
        stream << str; errstream.append(str);
      }
    }
  }

  return dstpath;
}

void backupExecuter::copySelectedFiles()
{
  QString srcFile;
  QString dstFile;
  char *buffer = NULL;
  unsigned int copiedFiles = 0;
  unsigned long copiedk = 0;

  if( !m_running ) return;

  const int buffsize = 5*1024*1024;

  buffer = new char[buffsize];

  m_engine->setProgressMaximum(kbytes_to_copy);

  QString prefix = backupDirStruct::createFileNamePrefix(m_config.m_bKeep,false);

  QTime startTime = QTime::currentTime();

  for ( QStringList::Iterator it2=filelist.begin(); m_running && it2!=filelist.end(); ++it2 )
  {
    checkTimeout();

    copiedFiles++;
    m_engine->setProgressText("processing "+QString::number(copiedFiles)+" of "+QString::number(files_to_copy)+" files...");

    if( !m_running )
      break;

    m_engine->setFileNameText(*it2);
    m_engine->setProgressValue(copiedk);

    QString relName = *it2;
    srcFile = m_config.m_sSrc + relName;
    dstFile = m_config.m_sDst + (prefix.isEmpty() ? relName : backupDirStruct::addFilenamePrefix(relName,prefix));

    QFileInfo srcInfo(srcFile);
    QString filePath = srcInfo.dir().absolutePath();
    QString relPath = filePath.mid(m_config.m_sSrc.length()+1);
    QString fileName = srcInfo.fileName();
    qint64 modify = srcInfo.lastModified().toMSecsSinceEpoch();

    if( m_config.m_bKeep )
    {
      QStringList toBeDeleted;
      m_dirs.keepFiles(relPath,fileName,m_config.m_iVersions-1,toBeDeleted);

      QStringList::iterator it = toBeDeleted.begin();
      while (it!=toBeDeleted.end())
      {
        QString fullName = m_config.m_sDst + "/" + *it;
        QFile::remove(fullName);
        ++it;
      }
    }

    QFile src(srcFile);
    if( src.open(QIODevice::ReadOnly) )
    {
      QFile dst(dstFile);
      if( dst.exists() ) dst.setPermissions(dst.permissions() |
            QFile::WriteOwner|QFile::WriteUser|QFile::WriteGroup|QFile::WriteOther);
      if( dst.open(QIODevice::WriteOnly) )
      {
        // remove file entry from CRC list
        if( crcSummary.contains(dstFile) )
        {
          crcSummary.remove(dstFile);
          checksumsChanged = true;
        }

        int bytes = src.size();

        stream << "copy " << srcFile << " to " << dstFile << " (" << bytes << " bytes) ";
        while( m_running )
        {
          checkTimeout();

          int nout=0;

          QByteArray bytes = src.read(buffsize);
          int n = bytes.size();
          {
            nout = dst.write(bytes);
            if( nout!=n )
            {
              m_error = true;
              QString str = "++++ destination drive is full. Cancelling the backup!\r\n";
              stream << str; errstream.append(str);
              m_diskFull = true;
              m_running = false;
              break;
            }
          }
          copiedk += (n/1024);
          m_engine->setProgressValue(copiedk);

          if( /*isBatch &&*/ m_config.m_bBackground )
          {
            QTime actTime = QTime::currentTime();
            double ratio = ( (double)copiedk / (double)startTime.msecsTo(actTime) * 1000.0);
            QTime time(0,0);
            time = time.addSecs((int)((double)(kbytes_to_copy-copiedk)/(ratio)+0.5));
            setWindowTitle(time.toString("hh:mm:ss") +" remaining");
            if( m_isBatch )
              m_engine->setToolTipText(QString("Automatic Backup V")+BACKUP_STR_VERSION+" running "+m_config.m_sName+".\nProcessing File '"+*it2+"'...\n");
            m_waiter.Sleep(20);
          }

          if( n<buffsize )
            break;
        }
        dst.close();

        if( m_running )
        {
          stream << "\r\n";

          // finally, fix modification time stamp to src file
          //QFileInfo fi(srcFile);
          setTimestamps(dstFile,srcInfo.lastModified());

          fileTocEntry entry;
          entry.m_prefix.clear();
          entry.m_tocId = m_dirs.nextTocId();
          entry.m_size = srcInfo.size();
          entry.m_modify = modify;
          entry.m_crc = 0;
          m_dirs.addFile(relPath,prefix+fileName,entry);
  //          lastModified2[filePath.toLatin1().data()][fileName.toLatin1().data()] = modify;
        }
        else
          dst.remove();
      }
      else
      {
        m_error = true;
        QString str = "++++ can't open destination file " + dstFile + "!\r\n";
        stream << str; errstream.append(str);
      }
      src.close();
    }
    else
    {
      m_error = true;
      QString str = "++++ can't open source file " + srcFile + "!\r\n";
      stream << str; errstream.append(str);
    }
  }

  m_engine->setProgressMaximum(1);
  m_engine->setProgressValue(0);

  delete[] buffer;
}

backupConfigData backupExecuter::getConfigData()
{
  return m_config;
}

void backupExecuter::setConfigData(const backupConfigData &config)
{
  m_config = config;
  loadData();
}

/*QString backupExecuter::getTitle()
{
  return m_config.m_sName;
}
QString backupExecuter::getSrc()
{
  return m_config.m_sSrc;
}
QString backupExecuter::getDst()
{
  return m_config.m_sDst;
}
QString backupExecuter::getFlt()
{
  return m_config.m_sFlt;
}
bool backupExecuter::getAuto()
{
  return m_config.m_bAuto;
}
int backupExecuter::getInterval()
{
  return m_config.m_iInterval;
}
bool backupExecuter::getBackground()
{
  return m_background;
}
bool backupExecuter::getCompress()
{
  return m_config.m_bVerify;
}
bool backupExecuter::getKeep()
{
  return m_config.m_bKeep;
}
int	backupExecuter::getVersions()
{
  return m_config.m_iVersions;
}
bool backupExecuter::getSuspend()
{
  return m_config.m_bsuspend;
}
int backupExecuter::getTimeout()
{
  return m_config.m_iTimeout;
}
*/
void backupExecuter::setUnattendedMode(bool doIt)
{
  m_closed = false;
  m_runUnattended = doIt;
}

void backupExecuter::threadedCopyOperation()
{
  findDirectories();
  analyzeDirectories();
  if( m_running && !m_config.m_bVerbose )
    copySelectedFiles();

  if( m_running )
    findDuplicates();

  if( m_config.m_bVerify && m_running && !m_isBatch )
    verifyBackup();

  if( m_running /*&& m_isBatch*/ )
  {
    // avoid verify upon initial run
    if( !updateAutoBackupTime() )
      updateAutoVerifyTime();
  }

  saveData();
}

void backupExecuter::threadedVerifyOperation()
{
  if( m_config.m_bVerify )
    verifyBackup();

  if( m_running /*&& m_isBatch*/ )
    updateAutoVerifyTime();

  saveData();
}

void backupExecuter::operationFinishedEvent()
{
  stoppingAction();

  if( m_config.m_bVerbose || m_closed )
    cancel();
  else
    close();
}

void backupExecuter::doIt(bool runningInBackground)
{
  if( m_running )
    return;

  bool cancelled = false;

  QFile dir1(m_config.m_sDst);
  if( !dir1.exists() )
  {
    mediaMessage msg(this,m_config.m_sDst);
    msg.exec();
    if( msg.result()!=QDialog::Accepted )
      cancelled = true;
    m_config.m_sDst = msg.getPath();
  }
  QFile dir2(m_config.m_sSrc);
  if( !dir2.exists() )
  {
    mediaMessage msg(this,m_config.m_sSrc);
    msg.exec();
    if( msg.result()!=QDialog::Accepted )
      cancelled = true;
    m_config.m_sSrc = msg.getPath();
  }

  if( cancelled )
    close();
  else
  {
    m_bringToFront = true;
    m_background = runningInBackground || m_config.m_bBackground;
    m_diskFull = false;

    changeVisibility();

    startingAction();

    m_engine->start(false);
  }
}

void backupExecuter::verifyIt(bool runningInBackground)
{
  m_background = runningInBackground || m_config.m_bBackground;

  changeVisibility();

  startingAction(); // do verify only if not cancelled in batch

  m_engine->start(true);
}

void backupExecuter::processEventsAndWait()
{
  m_engine->processEventsAndWait();
}

void backupExecuter::cancel() // OK (Cancel when running) pressed
{
  if( m_running )
  {
    m_engine->setProgressText("");
    m_engine->setFileNameText("");
    m_engine->setProgressValue(0);
    m_running=false;
  }

  close();
}

void backupExecuter::contextMenuEvent(QContextMenuEvent */*event*/)
{
  QMenu menu(this);
  QAction *abort = menu.addAction("Cancel...");
  QAction *quit = menu.addAction("Quit");
  QAction *result = menu.exec(QCursor::pos());
  if( result==abort )
  {
    cancel();
  }
  if( result==quit )
    qApp->quit();
}

void backupExecuter::closeEvent ( QCloseEvent */*event*/ )
{
  fileObj.close();
  //saveData();
  m_closed = true;
  cancel();
}

void backupExecuter::timerEvent(QTimerEvent *)
{
  if( m_bringToFront )
  {
    m_bringToFront = false;
    raise();
  }
}

void backupExecuter::cleanup()
{
  QDateTime date;
  switch( m_config.m_iLastModify )
  {
  case 0: // selecteed date
    date = getLimitDate(this,QDateTime::currentDateTime().addYears(-2));
    break;
    // 1w, 2w, 3w, 1m, 2m ,3m ,6m, 1y, 2y, 5y, 10y
  case 1:
    date = QDateTime::currentDateTime().addDays(-7);
    break;
  case 2:
    date = QDateTime::currentDateTime().addDays(-14);
    break;
  case 3:
    date = QDateTime::currentDateTime().addDays(-21);
    break;
  case 4:
    date = QDateTime::currentDateTime().addMonths(-1);
    break;
  case 5:
    date = QDateTime::currentDateTime().addMonths(-2);
    break;
  case 6:
    date = QDateTime::currentDateTime().addMonths(-3);
    break;
  case 7:
    date = QDateTime::currentDateTime().addMonths(-6);
    break;
  case 8:
    date = QDateTime::currentDateTime().addYears(-1);
    break;
  case 9:
    date = QDateTime::currentDateTime().addYears(-2);
    break;
  case 10:
    date = QDateTime::currentDateTime().addYears(-5);
    break;
  case 11:
    date = QDateTime::currentDateTime().addYears(-10);
    break;
  default:
    date = QDateTime::currentDateTime().addDays(-1);
    break;
  }

  if( date.isValid() )
  {
    startingAction();
    scanDirectory(date.date());
    findDuplicates();
    if( m_config.m_bFindSrcDupl ) findDuplicates(QString::null,true);
    stoppingAction();
  }

  close();
}

void backupExecuter::deletePath(QString const &absolutePath,QString const &indent)
{
  QString destinationPath = absolutePath;
  if( destinationPath.startsWith(m_config.m_sSrc) ) destinationPath = m_config.m_sDst + "/" + destinationPath.mid(m_config.m_sSrc.length()+1);

  QFileInfo fi(destinationPath);

  if( fi.isDir() )
  {
    scanDirectory(QDate::currentDate(),destinationPath,true);
  }
  else
  {
    if( m_config.m_bVerbose || m_config.m_bVerboseMaint )
    {
      if( fi.exists() )
      {
        stream << indent << "---> " << "would remove file " << destinationPath
        << ", created("+fi.created().toString(Qt::ISODate)
        +") modified("+fi.lastModified().toString(Qt::ISODate)
        +")" << "\r\n";
      }
      else
        stream << indent << "---> " << "file " << destinationPath << " does not exist any longer, removing from toc\r\n";
    }
    else
    {
      QFile file(destinationPath);

      if( file.exists() )
      {
        if( m_config.m_bCollectDeleted )
        {
            if( collectingDeleted )
            {
                if( !collectingPath.isEmpty() )
                    QFile::copy(absolutePath,collectingPath+"/"+fi.lastModified().toString("yyyy-MM-dd")+"_"+fi.fileName());
            }
            else
            {
              collectingDeleted = true;
              collectingPath = QFileDialog::getExistingDirectory (
                          this, "collecting deleted files here:", collectingPath );
            }
        }
        if( file.setPermissions(QFile::WriteOwner|QFile::WriteUser|QFile::WriteGroup|QFile::WriteOther)
        && file.remove() )
        {
          stream << "---> " << "removed file " << destinationPath << "\r\n";

          // remove file entry from CRC list
          if( crcSummary.contains(destinationPath) )
          {
            crcSummary.remove(destinationPath);
            checksumsChanged = true;
          }

          // check for empty directory; remove it if neccessary
          QDir dir(fi.dir());
          QStringList list(dir.entryList());
          while( !dir.isRoot() && (list.count()<=2) )
          {
            QFile::remove(dir.absolutePath()+"/.DS_Store");
            QString name = dir.dirName();
            dir.cdUp();
            if( !dir.rmdir(name) )
            {
              QMessageBox::warning(0,"dir error","directory\n"+dir.absolutePath()+"/"+name+"\nseems to have (hidden) content.");
              break;
            }
            else
              list = dir.entryList();
          }
        }
        else
        {
          QString str = "++++ can't remove file '"+destinationPath+"' file is write protected!\r\n";
          stream << str; errstream.append(str);
        }
      }
      QString relPath = fi.dir().path().mid(m_config.m_sDst.length()+1);
      QString filename = fi.fileName();
      m_toBeRemovedFromToc.append(qMakePair(relPath,filename));
    }
  }
}

backupExecuter::backupStatistics backupExecuter::getStatistics(QDate const &date,QString const &srcfile,QDate const &filemodified,qint64 const filesize,bool eraseAll)
{
  QDate today = QDate::currentDate();
  bool eraseIt = false;

  backupExecuter::backupStatistics result;

  if( eraseAll )
  {
    eraseIt = true;
    result.count++;
    result.dirkbytes += (filesize/1024);
  }
  else
  {
    if( filemodified.daysTo(date)>0 )
    {
      if( QFile::exists(srcfile) )
      {
        bool passed = true;

        // first, check directories filter
        if( !dirincludes.isEmpty() )
          passed = false;
        for ( QStringList::Iterator it2 = dirincludes.begin(); it2 != dirincludes.end(); ++it2 )
        {
          if( (*it2).isEmpty() || srcfile.contains(*it2) )
            passed = true;
        }
        for ( QStringList::Iterator it3 = direxcludes.begin(); passed && it3 != direxcludes.end(); ++it3 )
        {
          if( !(*it3).isEmpty() && srcfile.contains(*it3) )
            passed = false;
        }

        if( passed )
        {
          // second, check files filter
          if( !fileincludes.isEmpty() )
            passed = false;
          for ( QStringList::Iterator it3 = fileincludes.begin(); it3 != fileincludes.end(); ++it3 )
          {
            if( (*it3).isEmpty() || srcfile.contains(*it3) )
              passed = true;
          }
          for ( QStringList::Iterator it4 = fileexcludes.begin(); passed && it4 != fileexcludes.end(); ++it4 )
          {
            if( !(*it4).isEmpty() && srcfile.contains(*it4) )
              passed = false;
          }
        }

        if( !passed )
        {
          eraseIt = true;
          result.count++;
          result.dirkbytes += (filesize/1024);
        }
      }
      else
      {
        eraseIt = true;
        result.count++;
        result.dirkbytes += (filesize/1024);
      }
    }

    if( filemodified.daysTo(today)>365 )
    {
      result.yearcount++;
      result.yearkbytes += (filesize/1024);
    }
    else if( filemodified.daysTo(today)>182 )
    {
      result.halfcount++;
      result.halfkbytes += (filesize/1024);
    }
    else if( filemodified.daysTo(today)>91 )
    {
      result.quartercount++;
      result.quarterkbytes += (filesize/1024);
    }
    else if( filemodified.daysTo(today)>30 )
    {
      result.monthcount++;
      result.monthkbytes += (filesize/1024);
    }
    else
    {
      result.daycount++;
      result.daykbytes += (filesize  /1024);
    }
  }

  if( eraseIt )
  {
    deletePath(srcfile);
    stream << "     " << "    (file was filtered out or source does not exist any more) \r\n";
  }

  return result;
}

void backupExecuter::scanDirectory(QDate const &date, QString const &startPath, bool eraseAll)
{
  static int scanned;

  static unsigned totalcount;
  static unsigned long totaldirkbytes;

  static backupStatistics totalCounts;

  static int level;

  if( startPath.isNull() )
  {
    m_engine->setProgressText("Cleaning up Directories...");
    scanned = 0;
    totalcount = 0;
    totaldirkbytes = 0;
//    totalCounts = results;
//    totalcount = yearcount = halfcount = quartercount = monthcount = daycount = 0;
//    totaldirkbytes = yearkbytes = halfkbytes = quarterkbytes = monthkbytes = daykbytes = 0;
    level = 0;
    m_engine->setProgressMaximum(m_dirs.size());
    if( m_dirs.size()==0 || m_config.m_bScanDestPath )
      scanDirectory(date,m_config.m_sDst);
    else
    {
      m_toBeRemovedFromToc.clear();

      tocDataContainerMap::iterator it1 = m_dirs.getFirstElement();
      while( m_running &&(it1!=m_dirs.getLastElement()) )
      {
        unsigned found = 0;
        unsigned long foundkbytes = 0;

        checkTimeout();

        QString path = m_config.m_sSrc + "/" + it1.key();
        tocDataEntryMap::iterator it2 = it1.value().begin();
        m_engine->setFileNameText(path);
        m_engine->setProgressValue(scanned++);

        backupStatistics results;
        while( m_running && (it2!=it1.value().end()) )
        {
          tocDataEntryList entries = it2.value();
          tocDataEntryList::iterator it3 = entries.begin();
          while( it3!=entries.end() )
          {
            QDate filemodified = QDateTime::fromMSecsSinceEpoch((*it3).m_modify).date();
            QString srcfile = path+"/"+it2.key();

            results += getStatistics(date,srcfile,filemodified,(*it3).m_size,eraseAll);
            totalCounts += results;

            found++;
            foundkbytes += ((*it3).m_size/1024);
            ++it3;
          }
          ++it2;
        }

        if( results.count>0 )
        {
          if( m_config.m_bVerboseMaint )
            stream << "      " << results.count << " removable files with " << results.dirkbytes << " Kbytes found in " << path << "\r\n";
          totalcount += results.count;
          totaldirkbytes += results.dirkbytes;
        }

        ++it1;
      }

      deleteFilesFromDestination(m_toBeRemovedFromToc,totalcount,totaldirkbytes);
    }
    stream << "----> " << totalCounts.daycount << " files younger than a month with " << totalCounts.daykbytes << " Kbytes found\r\n";
    stream << "----> " << totalCounts.monthcount << " files between one and three months with " << totalCounts.monthkbytes << " Kbytes found\r\n";
    stream << "----> " << totalCounts.quartercount << " files between three months and half a year with " << totalCounts.quarterkbytes << " Kbytes found\r\n";
    stream << "----> " << totalCounts.halfcount << " files between half a year and a year with " << totalCounts.halfkbytes << " Kbytes found\r\n";
    stream << "----> " << totalCounts.yearcount << " files older than a year with " << totalCounts.yearkbytes << " Kbytes found\r\n";
    stream << "====> " << totalCounts.yearcount+totalCounts.halfcount+totalCounts.quartercount+totalCounts.monthcount+totalCounts.daycount
           << " total files with " << totalCounts.yearkbytes+totalCounts.halfkbytes+totalCounts.quarterkbytes+totalCounts.monthkbytes+totalCounts.daykbytes << " Kbytes found\r\n";
    if( m_config.m_bVerboseMaint )
      stream << "====> " << totalcount << " deletable files with " << totaldirkbytes << " Kbytes found\r\n";
    else
      stream << "====> " << totalcount << " files with " << totaldirkbytes << " Kbytes deleted\r\n";
    m_engine->setProgressMaximum(1);
  }
  else
  {
    unsigned count = 0;
    unsigned long dirkbytes = 0;
    unsigned found = 0;
    unsigned long foundkbytes = 0;

    QString actPath = startPath;
    QString configfile = m_config.m_sName;

    QDir dir(actPath);
    dir.setSorting(QDir::Name);
    const QFileInfoList &list = dir.entryInfoList();
    QFileInfoList::const_iterator it=list.begin();
    QFileInfo fi;

    m_engine->setFileNameText(actPath);
    m_engine->setProgressValue(scanned++);

    QString lastName = "";
    QString indent = "";

    if( m_config.m_bVerboseMaint && m_config.m_bScanDestPath ) // bei Detail-Listing
    {
      for( int i=0; i<=level; i++ )
      {
        if( i<level )
          stream << "|  ";
        else
          stream << "+- ";
        indent.append("|  ");
      }
      QString relPath = actPath.mid(m_config.m_sDst.length()+1);
      stream << "." << relPath << "\r\n";
    }

    while ( m_running && (it!=list.end()) )
    {
      checkTimeout();

      fi = *it;
      QString name = fi.fileName();
      if( name!=".." && name!="." && name!=configfile && !backupDirStruct::isSummaryFile(name) )
      {
        if( fi.isDir() )
        {
          level++;
          scanDirectory(date,actPath+"/"+name);
          level--;
        }
        else
        {
          found++;
          foundkbytes += (fi.size()/1024);

          bool eraseIt = false;

          if( eraseAll )
            eraseIt = true;
          else
          {
            QDate filemodified = fi.lastModified().date();
            QDate today = QDate::currentDate();
            //QString srcfile = source + fi.absoluteFilePath().remove(destination);//VIL

            QString filename = fi.fileName();
            filename = backupDirStruct::cutFilenamePrefix(filename);

            QString srcfile = m_config.m_sSrc + (fi.absolutePath()+"/"+filename).mid(m_config.m_sDst.length());

            if( filemodified.daysTo(date)>0 )
            {
              if( QFile::exists(srcfile) )
              {
                bool passed = true;

                // first, check directories filter
                if( !dirincludes.isEmpty() )
                  passed = false;
                for ( QStringList::Iterator it2 = dirincludes.begin(); it2 != dirincludes.end(); ++it2 )
                {
                  if( (*it2).isEmpty() || srcfile.contains(*it2) )
                    passed = true;
                }
                for ( QStringList::Iterator it3 = direxcludes.begin(); passed && it3 != direxcludes.end(); ++it3 )
                {
                  if( !(*it3).isEmpty() && srcfile.contains(*it3) )
                    passed = false;
                }

                if( passed )
                {
                  // second, check files filter
                  if( !fileincludes.isEmpty() )
                    passed = false;
                  for ( QStringList::Iterator it3 = fileincludes.begin(); it3 != fileincludes.end(); ++it3 )
                  {
                    if( (*it3).isEmpty() || srcfile.contains(*it3) )
                      passed = true;
                  }
                  for ( QStringList::Iterator it4 = fileexcludes.begin(); passed && it4 != fileexcludes.end(); ++it4 )
                  {
                    if( !(*it4).isEmpty() && srcfile.contains(*it4) )
                      passed = false;
                  }
                }

                if( !passed )
                {
                  eraseIt = true;
                  count++;
                  dirkbytes += (fi.size()/1024);
                }
              }
              else
              {
                eraseIt = true;
                count++;
                dirkbytes += (fi.size()/1024);
              }
            }

            if( filemodified.daysTo(today)>365 )
            {
              totalCounts.yearcount++;
              totalCounts.yearkbytes += (fi.size()/1024);
            }
            else if( filemodified.daysTo(today)>182 )
            {
              totalCounts.halfcount++;
              totalCounts.halfkbytes += (fi.size()/1024);
            }
            else if( filemodified.daysTo(today)>91 )
            {
              totalCounts.quartercount++;
              totalCounts.quarterkbytes += (fi.size()/1024);
            }
            else if( filemodified.daysTo(today)>30 )
            {
              totalCounts.monthcount++;
              totalCounts.monthkbytes += (fi.size()/1024);
            }
            else
            {
              totalCounts.daycount++;
              totalCounts.daykbytes += (fi.size()/1024);
            }
          }

          if( eraseIt )
          {
            deletePath(actPath+"/"+name,indent);
            stream << "     " << "    (file was filtered out or source does not exist any more) \r\n";
          }
        }
      }
      ++it;
    }

    if( m_config.m_bVerboseMaint && (found>0) ) // bei Detail-Listing
    {
      stream << indent << "     " << found << " total files with " << foundkbytes << " Kbytes found in " << actPath << "\r\n";
    }

    if( count>0 )
    {
      if( m_config.m_bVerboseMaint )
        stream << indent << "     " << count << " removable files with " << dirkbytes << " Kbytes found in " << actPath << "\r\n";
      totalcount += count;
      totaldirkbytes += dirkbytes;
    }
  }
}

int fileSize(QString const &actPath)
{
  int act_size = 0;
  QDir dir(actPath);
  const QFileInfoList &list = dir.entryInfoList();
  QFileInfoList::const_iterator it=list.begin();
  QFileInfo fi;

  while ( it!=list.end() )
  {
    fi = *it;
    QString name = fi.fileName();
    if( name!=".." && name!="." )
    {
      if( fi.isDir() )
      {
        act_size += fileSize(actPath+"/"+name);
      }
      else
        act_size += fi.size();
    }
    ++it;
  }
  return act_size;
}
void backupExecuter::findDuplicates(QString const &startPath,bool operatingOnSource)
{
  static int scanned;

  static unsigned totalcount;
  static unsigned long totaldirkbytes;

  if( operatingOnSource )
  {
    if( startPath.isNull() )
    {
      m_engine->setProgressText("Finding duplicate files...");
      m_engine->setProgressMaximum(0);
      scanned = 0;
      totaldirkbytes = 0;
      totalcount = 0;
      filemap.clear();
      if( operatingOnSource )
        findDuplicates(m_config.m_sSrc,operatingOnSource);
      else
        findDuplicates(m_config.m_sDst,operatingOnSource);
      if( m_config.m_bVerbose || m_config.m_bVerboseMaint )
        stream << "====> " << totalcount << " duplicate files with " << totaldirkbytes << " Kbytes found\r\n";
      else
        stream << "====> " << totalcount << " duplicate files with " << totaldirkbytes << " Kbytes deleted\r\n";
      m_engine->setProgressMaximum(1);
      m_engine->setProgressValue(0);
    }
    else
    {
      QString actPath = startPath;
      QString configfile = m_config.m_sName;

      QDir dir(actPath);
      dir.setSorting(QDir::Name);
      const QFileInfoList &list = dir.entryInfoList();
      QFileInfoList::const_iterator it=list.begin();
      QFileInfo fi;

      m_engine->setFileNameText(actPath);
      m_engine->setProgressValue(scanned++);

//      QString lastFile = "";
//      QString lastName = "";

      QString indent = "";

      while ( m_running && (it!=list.end()) )
      {
        checkTimeout();

        fi = *it;
        QString name = fi.fileName();
        if( name!=".." && name!="." && name!=configfile )
        {
          if( fi.isDir() && !QFile::exists(fi.absoluteFilePath()+"/Contents/PkgInfo") )
          {
            findDuplicates(actPath+"/"+name);
          }
          else
          {
            QDateTime filemodified = fi.lastModified();
            //QDate today = QDate::currentDate();

            QString filename = fi.fileName();
            filename = backupDirStruct::cutFilenamePrefix(filename);

            QString srcfile = m_config.m_sSrc + (fi.absolutePath()+"/"+filename).mid(m_config.m_sDst.length());
            //stream << "source file is " << srcfile << " \r\n";

            int size = 0;
            if( fi.isDir() )
              size = fileSize(fi.absoluteFilePath());
            else
              size = fi.size();

            //if( !QFile::exists(srcfile) )
            {
              if( filemap.contains(filename) )
              {
                QStringList fileinfs = filemap.value(filename).split(";");
                QString listPath = fileinfs.at(0);
                QDateTime listDate = QDateTime::fromString(fileinfs.at(1));
                int listSize = fileinfs.at(2).toInt();

                QString fileToDelete;
                QString mappedFile;

                if( listSize==size )
                {
                  // kepping actual file if it has source or if it is newer than list entry
                  if( (operatingOnSource || !QFile::exists(srcfile)) && listDate>=fi.lastModified() )
                  {
                    fileToDelete = fi.absoluteFilePath();
                    mappedFile = listPath+"/"+filename;
                  }
                  else
                  {
                    fileToDelete = listPath+"/"+filename;
                    mappedFile = fi.absoluteFilePath();
                    //if( QFile::exists(srcfile) )
                    filemap.remove(filename);
                    //else
                    filemap.insert(filename,fi.absolutePath()+";"+filemodified.toString()+";"
                    +QString::number(size));
                  }

                  deletePath(fileToDelete);
                  stream << indent << "     " << "    (was duplicate, keeping file " << mappedFile << ") \r\n";

                  totaldirkbytes += size / 1024;
                  totalcount++;
                }
#if 0
                else if( m_config.m_bShowTree )
                {
                  fileToDelete = fi.absoluteFilePath();
                  mappedFile = listPath+"/"+filename;
                  stream << indent << "     " << "  found file " << fileToDelete
                  << " but sizes are different (" << QString::number(listSize)
                  << "/" << QString::number(size) << ")"
                  << "\r\n";
                  stream << indent << "     " << "(mapped file " << listPath << "/" << filename << ")"
                  << "\r\n";
                }
#endif
              }
              else if( operatingOnSource || !QFile::exists(srcfile) )
              {
                //if( showTreeStruct->isChecked() )
                //    stream << indent << "---> " << "mapping " << fi.absolutePath()+"/"+filename << "\r\n";
                filemap.insert(filename,fi.absolutePath()+";"+filemodified.toString()+";"
                +QString::number(size));
              }
              else if( m_config.m_bVerboseMaint )
                stream << indent << "     " << "file " << fi.absoluteFilePath() << " has source, keeping it\r\n";
            }
          }
        }
        ++it;
      }
    }
  }
  else
  {
    m_engine->setProgressText("Finding duplicate files...");
    m_engine->setProgressMaximum(m_dirs.size());
    scanned = 0;
    totaldirkbytes = 0;
    totalcount = 0;
    filemap.clear();

    m_toBeRemovedFromToc.clear();

    tocDataContainerMap::iterator it1 = m_dirs.getFirstElement();
    while( m_running &&(it1!=m_dirs.getLastElement()) )
    {
      tocDataEntryMap::iterator it2 = it1.value().begin();

      QString relpath = it1.key();
      m_engine->setFileNameText(m_config.m_sDst + "/" + relpath);
      m_engine->setProgressValue(scanned++);

      while( m_running && (it2!=it1.value().end()) )
      {
        QString filename = it2.key();
        //QString srcfile = (path+"/"+filename);

        if( filemap.contains(filename) )
        {
          QStringList fileinfs = filemap.value(filename).split(";");
          QString listPath = fileinfs.at(0);
          QString listFile = fileinfs.at(1);
          //qint64 listDate = (fileinfs.at(2).toLongLong());
          int listSize = fileinfs.at(3).toLongLong();

          QString sourceFileRemoved;
          QString mappedFile;

          if( listSize==m_dirs.getNewestEntry(it2).m_size )
          {
            m_toBeRemovedFromToc.append(qMakePair(listPath,listFile));
            m_toBeRemovedFromToc.append(qMakePair(relpath,filename));
          }
#if 0
          else if( m_config.m_bShowTree )
          {
            sourceFileRemoved = m_config.m_sDst + "/" + relpath+"/"+it2.key();
            mappedFile = listPath+"/"+filename;
            stream << ""/*indent*/ << "     " << "  found file " << sourceFileRemoved
            << " but sizes are different (" << QString::number(listSize)
            << "/" << QString::number(m_dirs.getNewestEntry(it2).m_size) << ")"
            << "\r\n";
            stream << ""/*indent*/ << "     " << "(mapped file " << mappedFile << "/" << filename << ")"
            << "\r\n";
          }
#endif
        }
        else
        {
          filemap.insert(filename,relpath+";"+filename+";"+QString::number(m_dirs.getNewestEntry(it2).m_modify)+";"
            +QString::number(m_dirs.getNewestEntry(it2).m_size));
        }
        // hier war #if 0
        ++it2;
      }

      ++it1;
    }

    deleteFilesFromDestination(m_toBeRemovedFromToc,totalcount,totaldirkbytes);

    m_engine->setProgressMaximum(1);
    m_engine->setProgressValue(0);
  }
}

void backupExecuter::deleteFilesFromDestination(const QList<QPair<QString, QString> > &toBeRemovedFromToc, unsigned &totalcount, unsigned long &totaldirkbytes)
{
  QList< QPair<QString,QString> >::const_iterator it3 = toBeRemovedFromToc.begin();
  while( it3!=toBeRemovedFromToc.end() )
  {
    QString relPath = (*it3).first;
    QString file = (*it3).second;
    QString srcPath = m_config.m_sSrc + "/" + (relPath=="." ? "" : relPath + "/") + file;
    bool sourceFileFound = false;
    unsigned long historykbytes = 0;

    if( QFile::exists(srcPath) )
      sourceFileFound = true;
    else
    {
      tocDataEntryList &entries = m_dirs.getEntryList(relPath,file);
      tocDataEntryList::iterator it4 = entries.begin();
      while( it4!=entries.end() )
      {
        srcPath = m_config.m_sSrc + "/" + (relPath=="." ? "" : relPath + "/") + (*it4).m_prefix + file;

        if( QFile::exists(srcPath) )
          sourceFileFound = true;

        historykbytes += (*it4).m_size / 1024;
        ++it4;
      }
    }

    if( sourceFileFound )
    {
      ++it3;
      continue;
    }

    totalcount++;
    totaldirkbytes += historykbytes;

    QStringList toBeDeleted;
    m_dirs.removeFile(relPath,file,toBeDeleted);

    QStringList::iterator it = toBeDeleted.begin();
    while (it!=toBeDeleted.end())
    {
      if( !QFile::exists(m_config.m_sSrc + "/" + *it) )
      {
        QString fullName = m_config.m_sDst + "/" + *it;
        QFile::remove(fullName);
      }
      ++it;
    }

    ++it3;
  }
}

void backupExecuter::verifyBackup(QString const &startPath)
{
  static int scanned;

  if( startPath.isNull() )
  {
    if( m_config.m_bVerbose )
      return;

    m_engine->setProgressText("verifying files in backup...");
    m_engine->setProgressMaximum(lastVerifiedK);
    scanned = 0;
    verifiedK = 0;
    verifyBackup(m_config.m_sDst);
    m_engine->setProgressMaximum(1);
    m_engine->setProgressValue(0);
    if ( m_running )
      lastVerifiedK = verifiedK;
  }
  else
  {
    QString actPath = startPath;
    QString configfile = m_config.m_sName;

    QDir dir(actPath);
    dir.setSorting(QDir::Name);
    const QFileInfoList &list = dir.entryInfoList();
    QFileInfoList::const_iterator it=list.begin();
    QFileInfo fi;

    m_engine->setFileNameText(actPath);
    //progressBar->setValue(scanned++);
    if( m_config.m_bBackground )
      m_waiter.Sleep(20);

    QString lastFile = "";
    QString lastName = "";

    QString indent = "";

    while ( m_running && (it!=list.end()) )
    {
      checkTimeout();

      fi = *it;
      QString name = fi.fileName();
      if( name!=".." && name!="." && name!=configfile && !backupDirStruct::isSummaryFile(name) )
      {
        if( fi.isDir() )
        {
          verifyBackup(actPath+"/"+name);
        }
        else
        {
          QString filename = fi.fileName();
          filename = backupDirStruct::cutFilenamePrefix(filename);

          QString verifyFile = fi.absolutePath()+"/"+fi.fileName();
          //QString dstFile = fi.absolutePath()+"/"+filename;
          //QString srcfile = source + dstFile.mid(destination.length());

          QDateTime scanTime;
          bool willVerify = !isAutoBackupCreatedFile(filename);

          // check if this file has an own verify interval
          if( willVerify && crcSummary.contains(verifyFile) )
          {
            scanTime = QDateTime::fromTime_t(crcSummary[verifyFile].lastScan);
#if 0
            /*int verifydays = 30;
            switch( getInterval() )
            {
              case 0://daily
                verifydays = 7;
                break;
              case 1://weekly
                verifydays = 30;
                break;
              case 2://14 days
                verifydays = 30;
                break;
              case 3://monthly
                verifydays = 90;
                break;
              case 4://3 months
                verifydays = 180;
                break;
              case 5://yearly
                verifydays = 365;
                break;
            }*/
            if( false/*scanTime.daysTo(startTime)<=verifydays*/ )
            {
              willVerify = false;

              // verify of this file will be skipped, so just add its size to the progress
              verifiedK += (fi.size()/(qint64)1024);
              if( verifiedK<=lastVerifiedK )
                m_engine->setProgressValue(verifiedK);
              else
                m_engine->setProgressMaximum(0);
              if( getBackground() )
                m_waiter.Sleep(20);
            }
#endif
          }

          if( willVerify )
          {
            if( m_config.m_bVerbose || m_config.m_bVerboseMaint )
              stream << "--- verifying '" << verifyFile << "'...";
            QFile dst(verifyFile);
            if( dst.open(QIODevice::ReadOnly) )
            {
              //              QFile src(dstFile);
              //              if( dst.open(QIODevice::WriteOnly) )
              //              {
              unsigned long l1 = 0, l2 = 0;
              qint64 bytes = 0, read = 0;
              int n = 0;

              QVector<quint16> crclist(0);
              bool errFound = false;

              dst.read ((char *)(&l1),sizeof(unsigned long));
              dst.read ((char *)(&l2),sizeof(unsigned long));
              if( l1==header && l2==magic )
              {
                dst.read((char *)(&bytes),sizeof(int));
                while( m_running && (read<bytes) )
                {
                  if( dst.read((char *)(&n),sizeof(int))!=sizeof(int ))
                  {
                    errFound = true;
                    break;
                  }
                  QByteArray data = dst.read(n);
                  if( data.size()!=n )
                  {
                    errFound = true;
                    break;
                  }
                  QByteArray decode = qUncompress(data);
                  //dst.write(decode);
                  read += decode.size();
                  quint16 crc = qChecksum(decode.data(),decode.size());
                  crclist.append(crc);

                  verifiedK += (decode.size()/1024);
                  if( verifiedK<=lastVerifiedK )
                    m_engine->setProgressValue(verifiedK);
                  else
                    m_engine->setProgressMaximum(0);
                  if( m_config.m_bBackground )
                    m_waiter.Sleep(20);
                }
              }
              else
              {
                dst.reset();
                bytes = fi.size();
                while( m_running && (read<bytes) )
                {
                  QByteArray data = dst.read(BUFF_SIZE);
                  if( data.size()<(bytes-read) && data.size()!=BUFF_SIZE )
                  {
                    QString str = "++++ read error in backup file '"  + verifyFile + "'! size="
                                                                      + QString::number(bytes)+",read="
                                                                      + QString::number(read)+",last blockk="
                                                                      + QString::number(data.size())+"\r\n";
                    stream << str; errstream.append(str);
                    errFound = true;
                    read = bytes;
                    break;
                  }
                  read += data.size();
                  quint16 crc = qChecksum(data.data(),data.size());
                  crclist.append(crc);

                  verifiedK += (data.size()/1024);
                  if( verifiedK<=lastVerifiedK )
                    m_engine->setProgressValue(verifiedK);
                  else
                    m_engine->setProgressMaximum(0);
                  if( m_config.m_bBackground )
                    m_waiter.Sleep(20);
                }
                //if( bytes>10000 && bytes<20000 ) errFound=true;
                //dst.write(data);
              }
              if( m_running )
              {
                if( crcSummary.contains(verifyFile) )
                {
                  if( crcSummary[verifyFile].crc!=crclist )
                    errFound = true;
                }
                crcSummary[verifyFile].crc = crclist;
                crcSummary[verifyFile].lastScan = startTime.toTime_t();
                checksumsChanged = true;
              }

              if( errFound )
              {
                m_error = true;
                QString str = "++++ verify error in backup file '" + verifyFile + "'!\r\n";
                stream << str; errstream.append(str);
              }
              else if( m_config.m_bVerbose || m_config.m_bVerboseMaint )
                stream << " is OK.\r\n";
              dst.close();
            }
            else
            {
              m_error = true;
              QString str = "++++ could not open '" + verifyFile + "'!\r\n";
              stream << str; errstream.append(str);
            }
            //src.close();

            m_engine->setProgressText("verifying file " + QString::number(scanned++) + " of " + QString::number(crcSummary.size()) + "...");
          }
          else
          {
            if( m_config.m_bVerbose || m_config.m_bVerboseMaint )
              stream << "--- skipping verify on '" << verifyFile << " , last scanned on " << scanTime.toString() << "\r\n";
          }
        }
      }
      ++it;
    }
  }
  //}
}

QString backupExecuter::getAutobackupCheckFile(QString const &suffix)
{
  return m_config.m_sDst+"/"+m_config.m_sName.remove(" ") + suffix + ".vibup";
}

bool backupExecuter::isAutoBackupCreatedFile(const QString &file)
{
  return backupDirStruct::isTocSummaryFile(file) || file.contains(".vibup");
}
