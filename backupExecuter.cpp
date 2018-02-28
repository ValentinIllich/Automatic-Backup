#include "backupExecuter.h"
#include "backupMain.h"
#include "Utilities.h"

#include <qlayout.h>

#include <qdir.h>
#include <qprogressdialog.h>
#include <qapplication.h>
#include <qsettings.h>

#include <qcheckbox.h>
#include <qcombobox.h>

#include <qlabel.h>
#include <qpushbutton.h>
#include <qlineedit.h>
#include <qprogressbar.h>
#include <qfileDialog.h>
#include <qmessagebox.h>
#include <qbuffer.h>
//#include <qvbox.h>
#include <qtextedit.h>
#include <qtextbrowser.h>
#include <qdatetimeedit.h>
#include <qdesktopwidget.h>
#include <qthread.h>
#include <qmenu.h>

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

backupExecuter::backupExecuter(QString const &name, QString const &src, QString const &dst,
                               QString const &flt, bool automatic,int repeat,
                               bool keepVers,int versions,bool zlib,
                               bool suspend, int breakafter)
: source(src)
, destination(dst)
, filter(flt)
, log(NULL)
, m_autoStart(automatic)
, m_isBatch(false)
, m_running(false)
, m_error(false)
//, background()
, m_dirsCreated(false)
, m_closed(false)
, m_background(false)
, m_closeAfterExecute(false)
//, m_batchRunning(false)
, m_askForShutDown(NULL)
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
  nameText->setText(name);
  sourceLab->setText(src);
  destLab->setText(dst);
  filterDesc->setText(flt);
  autoExec->setChecked(automatic);
  interval->setCurrentIndex(repeat);
  keepCopy->setChecked(keepVers);
  compress->setChecked(zlib);
  suspending->setChecked(suspend);
  timeout->setCurrentIndex(breakafter);
  versionsStr->setText(QString::number(versions));
  dateTime->setDate(QDate::currentDate());
  resize(width(),200);

  setWindowTitle("Backup Configuration '"+name+"'");

  connect(qApp->desktop(),SIGNAL(resized(int)),this,SLOT(screenResizedSlot(int)));

  QString summaryFile = backupDirstruct::getChecksumSummaryFile(destination);
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

  QString tocSummaryFile = backupDirstruct::getTocSummaryFile(destination);
  m_dirs.readFromFile(tocSummaryFile);

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
  saveData();
  delete m_engine;
}

void backupExecuter::screenResizedSlot( int /*screen*/ )
{
  changeVisibility();
}

void backupExecuter::saveData()
{
  if( checksumsChanged )
  {
    QString summaryFile = backupDirstruct::getChecksumSummaryFile(destination);
    QFile crcfile(summaryFile);
    crcfile.open(QIODevice::WriteOnly | QIODevice::Truncate);
    QDataStream str(&crcfile);
    str << 0x80000000;
    str << lastVerifiedK;
    str << crcSummary;
    crcfile.close();
  }
  checksumsChanged = false;

  QString tocSummaryFile = backupDirstruct::getTocSummaryFile(destination);
  m_dirs.writeToFile(tocSummaryFile);
}

void backupExecuter::changeVisibility()
{
  if( getBackground() )
  {
    tabWidget->hide();
    progresslabel->hide();
    executeButt->hide();

    if( m_isBatch )
    {
      actualfile->hide();
      cancelButt->hide();
      m_engine->setToolTipText(QString("Automatic Backup V")+BACKUP_VERSION+" running.\nProcessing Configuration '"+getTitle()+"'");
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
    if( m_isBatch )
    {
      tabWidget->hide();
      //        progressLab->hide();
      executeButt->hide();
    }

    resize( width(), 0 ); // dialog wil be as small as possible
    setWindowTitle("Processing Configuration '"+getTitle()+"'");
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
  int chars = 0;
  if( !text.isEmpty() )
  {
    chars = (int)((double)progresslabel->width() / (double)metrics.width(text) * 0.8 * (double)text.length());
    if( chars<text.length() )
      progresslabel->setText("..."+text.right(chars));
    else
      progresslabel->setText(text);
  }
  else
    progresslabel->setText(text);
}

void backupExecuter::processFileNameText(QString const &text)
{
  QFontMetrics metrics(actualfile->font());
  int chars = 0;
  if( !text.isEmpty() )
  {
    chars = (int)((double)actualfile->width() / (double)metrics.width(text) * 0.8 * (double)text.length());
    if( chars<text.length() )
      actualfile->setText("..."+text.right(chars));
    else
      actualfile->setText(text);
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

  if( verboseExecute->isChecked() || verboseMaintenance->isChecked() )
  {
    if( buff.open(QIODevice::Truncate|QIODevice::WriteOnly) )
      stream.setDevice(&buff);
  }
  else
  {
    log = openFile(QString::null);
    if( log->fileName()!=QString::null )
      stream.setDevice(log);
    else if( getAuto() )
      stream << "++++ problem found in backup: directory doesn't exist: couldn't open log file " << "\r\n";
    else
      QMessageBox::warning(this,"backup problems","couldn't open log file 'backup.log'");
  }

  QString str = "================ backup '" + getTitle() + "' starting on " + QDateTime::currentDateTime().toString() + " ================\r\n";
  stream << str; errstream.append(str);

  QStringList incexcl = getFlt().remove("+").split("-");
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
  if( verboseExecute->isChecked() || verboseMaintenance->isChecked() )
  {
    buff.close();
    displayResult(this,QString(buff.buffer()));
  }
  else
  {
    if( m_error )
    {
      if( m_closeAfterExecute )
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
        QApplication::alert(this);
        QMessageBox::warning(this,"backup problems","problems occured during backup.\nPlease refer to 'backup.log' for details.");
        displayResult(this,errstream,"backup errors");
      }

      m_error = false;
      errstream = "";
    }
    //buff.close();
    log->close();
  }

  if( m_isBatch ) return;

  QString str = "================ backup '" + getTitle() + "' ending on " + QDateTime::currentDateTime().toString() + " ================\r\n";
  stream << str;

  stream.setDevice(0);

  m_running=false;
  m_engine->setProgressText("");
  m_engine->setFileNameText("");
  cancelButt->setText("OK");
}

void backupExecuter::checkTimeout()
{
  if( suspending->isChecked() )
  {
    static int times[] = { 10,15,30,60,90,120,150,180,210,240 };
    int maxtime = times[timeout->currentIndex()]*60;

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

    directories.append(source);
    findDirectories(source);
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
        QString path = start+"/"+actual;
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
          if( verboseExecute->isChecked() )
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
          findDirectories(path);
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
        m_engine->setToolTipText(QString("Automatic Backup V")+BACKUP_VERSION+" running.\nProcessing Directory '"+*it+"'");
    m_engine->setFileNameText(*it);
    m_engine->setProgressValue(i);
    if( getBackground() )
      m_waiter.Sleep(20);

    QString srcpath = *it;
    QString dstpath = ensureDirExists(srcpath,source,destination);

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

      if( getBackground() )
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
      if( backupDirstruct::isTocSummaryFile(actualName) || backupDirstruct::isChecksumSummaryFile(actualName) )
        passed = false;

      if( !srcFile.isSymLink() && actualName!="." && actualName!=".." && !isAutoBackupCreatedFile(actualName) )
      {
        if( passed )
        {
          QString filePath = srcFile.dir().absolutePath();
          QString fileName = srcFile.fileName();
          qint64 modify = srcFile.lastModified().toMSecsSinceEpoch();

          m_engine->setFileNameText(filePath+"/"+fileName);
          QString relPath = filePath.mid(source.length()+1);
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
        QString relName = fullName.mid(source.length());
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
  QStringList relPaths;
  while( !dst.exists() )
  {
    //printf("%s not found in destination, so creating it\n",dst.absPath().toLatin1());
    if( verboseMaintenance->isChecked() )
      stream << dst.absolutePath() << " not found in destination, would be created\r\n";
    else
      stream << dst.absolutePath() << " not found in destination, so creating it\r\n";
    relPaths.append(dst.dirName());
    QString fullname = dst.absolutePath();
    int pos = fullname.lastIndexOf(dst.dirName());
    int len = dst.dirName().length();
    QString news = dst.absolutePath().remove(pos,len);
    dst.setPath( news );
  }
  while( !relPaths.isEmpty() )
  {
    QString newdir = relPaths.last();
    if( verboseMaintenance->isChecked() )
    {
      if( showTreeStruct->isChecked() )
        stream << dst.absolutePath() << " (would be creating "+newdir+")\r\n";
    }
    else
    {
      if( dst.mkdir(newdir) )
      {
        dst.cd(newdir);
        m_dirsCreated = true;
      }
      else
      {
        m_error = true;
        QString str = "++++ can't create directory '"+dst.absolutePath()+newdir+"'!\r\n";
        stream << str; errstream.append(str);
      }
    }
    relPaths.removeAll(newdir);
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

  QString prefix = backupDirstruct::createFileNamePrefix(getKeep(),getCompress());

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
    srcFile = source + relName;
    dstFile = destination + (prefix.isEmpty() ? relName : backupDirstruct::addFilenamePrefix(relName,prefix));

    QFileInfo srcInfo(srcFile);
    QString filePath = srcInfo.dir().absolutePath();
    QString relPath = filePath.mid(source.length()+1);
    QString fileName = srcInfo.fileName();
    qint64 modify = srcInfo.lastModified().toMSecsSinceEpoch();

    if( getKeep() )
    {
      QStringList toBeDeleted;
      m_dirs.keepFiles(relPath,fileName,getVersions()-1,toBeDeleted);

      QStringList::iterator it = toBeDeleted.begin();
      while (it!=toBeDeleted.end())
      {
        QString fullName = destination + "/" + *it;
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

        int written = 0;
        int bytes = src.size();

        stream << "copy " << srcFile << " to " << dstFile << " (" << bytes << " bytes) ";
        if( getCompress() )
        {
          dst.write ((const char *)(&header),sizeof(unsigned long));
          dst.write ((const char *)(&magic),sizeof(unsigned long));
          dst.write ((const char *)(&bytes),sizeof(int));
        }
        while( m_running )
        {
          checkTimeout();

          int nout=0;

          QByteArray bytes = src.read(buffsize);
          int n = bytes.size();
          if( getCompress() )
          {
            QByteArray cprs = qCompress(bytes);
            int n2 = cprs.size();
            dst.write ((const char *)(&n2),sizeof(int));
            nout = dst.write(cprs);
            written += n2;
            if( nout!=n2 )
            {
              m_error = true;
              QString str = "++++ destination drive is full. Cancelling the backup!\r\n";
              stream << str; errstream.append(str);
              if( getAuto() )
              {
                fullMessage msg(this,destination);
                msg.exec();
              }
              break;
            }
          }
          else
          {
            nout = dst.write(bytes);
            if( nout!=n )
            {
              m_error = true;
              QString str = "++++ destination drive is full. Cancelling the backup!\r\n";
              stream << str; errstream.append(str);
              if( getAuto() )
              {
                fullMessage msg(this,destination);
                msg.exec();
              }
              break;
            }
          }
          copiedk += (n/1024);
          m_engine->setProgressValue(copiedk);

          if( /*isBatch &&*/ getBackground() )
          {
            QTime actTime = QTime::currentTime();
            double ratio = ( (double)copiedk / (double)startTime.msecsTo(actTime) * 1000.0);
            QTime time(0,0);
            time = time.addSecs((int)((double)(kbytes_to_copy-copiedk)/(ratio)+0.5));
            setWindowTitle(time.toString("hh:mm:ss") +" remaining");
            if( m_isBatch )
              m_engine->setToolTipText(QString("Automatic Backup V")+BACKUP_VERSION+" running "+getTitle()+".\nProcessing File '"+*it2+"'...\n");
            m_waiter.Sleep(20);
          }

          if( n<buffsize )
            break;
        }
        dst.close();

        if( m_running )
        {
          if( getCompress() )
            stream << "--> " <<  " (" << written << " bytes)\r\n";
          else
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

QString backupExecuter::getTitle()
{
  return nameText->text();
}
QString backupExecuter::getSrc()
{
  return source;
}
QString backupExecuter::getDst()
{
  return destination;
}
QString backupExecuter::getFlt()
{
  filter = filterDesc->text();
  return filter;
}
bool backupExecuter::getAuto()
{
  return m_autoStart;
}
int backupExecuter::getInterval()
{
  return interval->currentIndex();
}
bool backupExecuter::getBackground()
{
  return m_background;
}
bool backupExecuter::getCompress()
{
  return compress->isChecked();
}
bool backupExecuter::getKeep()
{
  return keepCopy->isChecked();
}
int	backupExecuter::getVersions()
{
  return versionsStr->text().toInt();
}
bool backupExecuter::getSuspend()
{
  return suspending->isChecked();
}
int backupExecuter::getTimeout()
{
  return timeout->currentIndex();
}

void backupExecuter::setCloseAfterExecute(bool doIt)
{
  m_closeAfterExecute = doIt;
}

void backupExecuter::setAskForShutdown(bool *willShutdown)
{
  m_askForShutDown = willShutdown;
}

void backupExecuter::selSource()
{
  QString path = QFileDialog::getExistingDirectory(
  this,
  //"get existing directory",
  "Choose a directory",
  source
  );

  if( !path.isEmpty() )
  {
    path.replace("\\","/");
    sourceLab->setText(source = path);
  }
}
void backupExecuter::selDest()
{
  QString path = QFileDialog::getExistingDirectory(
  this,
  //"get existing directory",
  "Choose a directory",
  destination
  );

  if( !path.isEmpty() )
  {
    path.replace("\\","/");
    destLab->setText(destination = path);
  }
}
void backupExecuter::selAuto()
{
  if( autoExec->isChecked() )
    m_autoStart = true;
  else
    m_autoStart = false;
}

void backupExecuter::threadedCopyOperation()
{
  findDirectories();
  analyzeDirectories();
  if( m_running && !verboseExecute->isChecked() )
    copySelectedFiles();

  //if( /*m_running &&*/ m_dirsCreated ) // always check for duplicates even if cancelled
    findDuplicates();

  if( m_running && !m_isBatch )
    verifyBackup();

  if( m_running /*&& m_isBatch*/ )
  {
    stream << "updating backup time of automatic item '"+getTitle()+"'\r\n";
    QFile tst(getAutobackupCheckFile(""));
    if( tst.open(QIODevice::WriteOnly) )
    {
      QTextStream stream(&tst);
      stream << QDateTime::currentDateTime().toString("yyyyMMddhhmmss");
      tst.close();
    }
    else
      stream << "++++ couldn't create automatic test item\r\n";
  }
}

void backupExecuter::threadedVerifyOperation()
{
  verifyBackup();

  if( m_running /*&& m_isBatch*/ )
  {
    stream << "updating verify time of automatic item '"+getTitle()+"'\r\n";
    QFile tst(getAutobackupCheckFile("_chk"));
    if( tst.open(QIODevice::WriteOnly) )
    {
      QTextStream stream(&tst);
      stream << QDateTime::currentDateTime().toString("yyyyMMddhhmmss");
      tst.close();
    }
    else
      stream << "++++ couldn't create automatic test item\r\n";
  }
}

void backupExecuter::operationFinishedEvent()
{
  stoppingAction();

  if( verboseExecute->isChecked() || m_closed ) // when debugging this means: just like OK / when dialog closed: Cancel
    cancel();
  else if( m_closeAfterExecute )
    accept();								// this means: execution done, return to main window

  m_taskFinished = true;
}

void backupExecuter::doIt(bool runningInBackground)
{
  if( m_askForShutDown )
  {
    if( QMessageBox::question(0,"security question","Do you want the system to shut down automatically after execution?",QMessageBox::Yes,QMessageBox::No)==QMessageBox::Yes )
      *m_askForShutDown = true;
  }

  m_background = runningInBackground || backgroundExec->isChecked();

  QFile dir1(destination);
  if( !dir1.exists() )
  {
    mediaMessage msg(this,destination);
    msg.exec();
    if( msg.result()!=Accepted )
      return;
    destination = msg.getPath();
  }
  QFile dir2(source);
  if( !dir2.exists() )
  {
    mediaMessage msg(this,source);
    msg.exec();
    if( msg.result()!=Accepted )
      return;
    source = msg.getPath();
  }

  changeVisibility();

  startingAction();

  m_engine->start(false);
}

void backupExecuter::verifyIt(bool runningInBackground)
{
  if( m_askForShutDown )
  {
    if( QMessageBox::question(0,"security question","Do you want the system to shut down automatically after execution?",QMessageBox::Yes,QMessageBox::No)==QMessageBox::Yes )
      *m_askForShutDown = true;
  }

  m_background = runningInBackground || backgroundExec->isChecked();

  changeVisibility();

  startingAction(); // do verify only if not cancelled in batch

  m_engine->start(true);
}

void backupExecuter::processEventsAndWait()
{
    m_taskFinished = false;
    while( !m_taskFinished )
    {
        m_waiter.wait(50);
        qApp->processEvents();
    }
}

void backupExecuter::cancel() // OK (Cancel when running) pressed
{
  if( m_running )
  {
    cancelButt->setText("OK");
    m_engine->setProgressText("");
    m_engine->setFileNameText("");
    m_engine->setProgressValue(0);
    m_running=false;
  }
  else
    reject();								// this means: just OK pressed
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
  saveData();
  m_closed = true;
  cancel();
}

void backupExecuter::timerEvent(QTimerEvent *)
{
}

void backupExecuter::help()
{
  int w = 800;
  int h = 600;

  QDialog help(this);
  QVBoxLayout box(&help);
  QTextBrowser browse(&help);
  QPushButton ok("OK",&help);

  box.addWidget(&browse);
  box.addWidget(&ok);

  ok.setSizePolicy(QSizePolicy::Maximum,QSizePolicy::Fixed);
  connect(&ok,SIGNAL(clicked()),&help,SLOT(accept()));

  QFile file(":/helptext/help.html");
  file.open(QIODevice::ReadOnly);
  QTextStream stream(&file);
  browse.setText(stream.readAll());
  //browse.zoomIn(10);
  file.close();

  help.setWindowTitle("Filter Definition Help");
  help.setGeometry(80,80,w,h);
  //browse.resize(w,h);
  help.exec();
}

void backupExecuter::restoreDir()
{
  QString path = QFileDialog::getExistingDirectory(0,"Restore directory...",destination);
  if( !path.isEmpty() )
  {
    startingAction();
    restoreDirectory(path);
    stoppingAction();
  }
}
void backupExecuter::restoreFile()
{
  QString srcFile = QFileDialog::getOpenFileName(0,"Restore file...","","All Files (*.*)");
  if( !srcFile.isEmpty() )
  {
    startingAction();
    QFileInfo fi(srcFile);
    if( fi.isDir() )
    {
      restoreDirectory(srcFile);
    }
    else
    {
      QString dstFile = QFileDialog::getSaveFileName(0,"Store file as...","","All Files (*.*)");
      if( !dstFile.isEmpty() )
      {
        if( verboseMaintenance->isChecked() )
          stream << "---> " << "would restore file " << srcFile
          << " to file " << dstFile << "\r\n";
        else
          copyFile(srcFile,dstFile);
      }
    }
    stoppingAction();
  }
}
void backupExecuter::restoreDirectory(QString const &startPath)
{
  QDir dir(startPath);
  dir.setSorting(QDir::Name);
  const QFileInfoList &list = dir.entryInfoList();
  QFileInfoList::const_iterator it=list.begin();
  QFileInfo fi;

  if( verboseMaintenance->isChecked() )
    stream << "examining directory " << startPath << "\r\n";

  ensureDirExists(startPath,destination,source);

  m_engine->setFileNameText(startPath);
  //	progressBar->setValue(scanned++);
  qApp->processEvents();

  while ( it!=list.end() )
  {
    fi = *it;
    QString name = fi.fileName();
    if( name!=".." && name!="." )
    {
      QString srcfile = startPath+"/"+name;
      if( fi.isDir() )
      {
        restoreDirectory(srcfile);
      }
      else
      {
        QString relfile = srcfile.mid(destination.length());
        relfile = backupDirstruct::cutFilenamePrefix(relfile);
        QString destfile = source+relfile;
        if( verboseMaintenance->isChecked() )
          stream << "---> " << "would restore file " << srcfile
          << " to file " << destfile << "\r\n";
        else
          copyFile(srcfile,destfile);
      }
    }
    ++it;
  }
}
void backupExecuter::copyFile(QString const &srcFile, QString const &dstFile)
{
  QFile src(srcFile);
  if( src.open(QIODevice::ReadOnly) )
  {
    QFile dst(dstFile);
    if( dst.open(QIODevice::WriteOnly) )
    {
      unsigned long l1 = 0, l2 = 0;
      int bytes = 0, read = 0;
      int n = 0;

      src.read ((char *)(&l1),sizeof(unsigned long));
      src.read ((char *)(&l2),sizeof(unsigned long));
      if( l1==header && l2==magic )
      {
        src.read((char *)(&bytes),sizeof(int));
        while( read<bytes )
        {
          src.read((char *)(&n),sizeof(int));
          QByteArray data = src.read(n);
          QByteArray decode = qUncompress(data);
          dst.write(decode);
          read += decode.size();
        }
      }
      else
      {
        src.reset();
        QByteArray data = src.readAll();
        dst.write(data);
      }
      dst.close();
    }
    src.close();
  }
}

void backupExecuter::cleanup()
{
  QDate date;

  date = dateTime->date();

  startingAction();
  scanDirectory(date);
  findDuplicates();
  if( verboseMaintenance->isChecked() && sourceDuplicates->isChecked() ) findDuplicates(QString::null,true);
  stoppingAction();
}

void backupExecuter::deletePath(QString const &absolutePath,QString const &indent)
{
  QFileInfo fi(absolutePath);

  if( fi.isDir() )
  {
    scanDirectory(QDate::currentDate(),absolutePath,true);
  }
  else
  {
    if( verboseMaintenance->isChecked() || verboseExecute->isChecked() )
    {
      stream << indent << "---> " << "would remove file " << absolutePath
      << ", created("+fi.created().toString(Qt::ISODate)
      +") modified("+fi.lastModified().toString(Qt::ISODate)
      +")" << "\r\n";
    }
    else
    {
      QFile file(absolutePath);
      if( collectFiles->isChecked() )
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
        stream << "---> " << "removed file " << absolutePath << "\r\n";

        // remove file entry from CRC list
        if( crcSummary.contains(absolutePath) )
        {
          crcSummary.remove(absolutePath);
          checksumsChanged = true;
        }

        // check for empty directory; remove it if neccessary
        QFileInfo fi(absolutePath);
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
        QString str = "++++ can't remove file '"+absolutePath+"' file is write protected!\r\n";
        stream << str; errstream.append(str);
      }
    }
  }
}

backupExecuter::backupStatistics backupExecuter::getStatistics(QDate const &date,QString const &srcfile,QDate const &filemodified,qint64 const filesize,bool eraseAll)
{
  QDate today = QDate::currentDate();
  bool eraseIt = false;

  backupExecuter::backupStatistics result;

  if( eraseAll )
    eraseIt = true;
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
    backupStatistics results;
    totalCounts = results;
//    totalcount = yearcount = halfcount = quartercount = monthcount = daycount = 0;
//    totaldirkbytes = yearkbytes = halfkbytes = quarterkbytes = monthkbytes = daykbytes = 0;
    level = 0;
    m_engine->setProgressMaximum(m_dirs.size());
    if( m_dirs.size()==0 )
      scanDirectory(date,destination);
    else
    {
      tocDataContainerMap::iterator it1 = m_dirs.getFirstElement();
      while( m_running &&(it1!=m_dirs.getLastElement()) )
      {
        unsigned found = 0;
        unsigned long foundkbytes = 0;

        checkTimeout();

        QString path = source + "/" + it1.key();
        tocDataEntryMap::iterator it2 = it1.value().begin();
        m_engine->setFileNameText(path);
        m_engine->setProgressValue(scanned++);

        while( m_running && (it2!=it1.value().end()) )
        {
          tocDataEntryList entries = it2.value();
          tocDataEntryList::iterator it3 = entries.begin();
          while( it3!=entries.end() )
          {
            QDate filemodified = QDateTime::fromMSecsSinceEpoch((*it3).m_modify).date();
            QString srcfile = path+"/"+it2.key();

            results = getStatistics(date,srcfile,filemodified,(*it3).m_size,eraseAll);
            totalCounts += results;

            found++;
            foundkbytes += ((*it3).m_size/1024);
            ++it3;
          }
          ++it2;
        }

        if( results.count>0 )
        {
          if( showTreeStruct->isChecked() )
            stream << "      " << results.count << " removable files with " << results.dirkbytes << " Kbytes found in " << path << "\r\n";
          totalcount += results.count;
          totaldirkbytes += results.dirkbytes;
        }

        ++it1;
      }
    }
    stream << "----> " << totalCounts.daycount << " files younger than a month with " << totalCounts.daykbytes << " Kbytes found\r\n";
    stream << "----> " << totalCounts.monthcount << " files between one and three months with " << totalCounts.monthkbytes << " Kbytes found\r\n";
    stream << "----> " << totalCounts.quartercount << " files between three months and half a year with " << totalCounts.quarterkbytes << " Kbytes found\r\n";
    stream << "----> " << totalCounts.halfcount << " files between half a year and a year with " << totalCounts.halfkbytes << " Kbytes found\r\n";
    stream << "----> " << totalCounts.yearcount << " files older than a year with " << totalCounts.yearkbytes << " Kbytes found\r\n";
    stream << "====> " << totalCounts.yearcount+totalCounts.halfcount+totalCounts.quartercount+totalCounts.monthcount+totalCounts.daycount
           << " total files with " << totalCounts.yearkbytes+totalCounts.halfkbytes+totalCounts.quarterkbytes+totalCounts.monthkbytes+totalCounts.daykbytes << " Kbytes found\r\n";
    if( verboseMaintenance->isChecked() )
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
    QString configfile = getTitle();

    QDir dir(actPath);
    dir.setSorting(QDir::Name);
    const QFileInfoList &list = dir.entryInfoList();
    QFileInfoList::const_iterator it=list.begin();
    QFileInfo fi;

    m_engine->setFileNameText(actPath);
    m_engine->setProgressValue(scanned++);

    QString lastName = "";
    QString indent = "";

    if( showTreeStruct->isChecked() ) // bei Detail-Listing
    {
      for( int i=0; i<=level; i++ )
      {
        if( i<level )
          stream << "|  ";
        else
          stream << "+- ";
        indent.append("|  ");
      }
      QString relPath = actPath.mid(destination.length()+1);
      stream << "." << relPath << "\r\n";
    }

    while ( m_running && (it!=list.end()) )
    {
      checkTimeout();

      fi = *it;
      QString name = fi.fileName();
      if( name!=".." && name!="." && name!=configfile && !name.endsWith(".crcs") )
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
            filename = backupDirstruct::cutFilenamePrefix(filename);

            QString srcfile = source + (fi.absolutePath()+"/"+filename).mid(destination.length());

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
          }
        }
      }
      ++it;
    }

    if( showTreeStruct->isChecked() && (found>0) ) // bei Detail-Listing
    {
      stream << indent << "     " << found << " total files with " << foundkbytes << " Kbytes found in " << actPath << "\r\n";
    }

    if( count>0 )
    {
      if( showTreeStruct->isChecked() )
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
        findDuplicates(source,operatingOnSource);
      else
        findDuplicates(destination,operatingOnSource);
      if( verboseMaintenance->isChecked() || verboseExecute->isChecked() )
        stream << "====> " << totalcount << " duplicate files with " << totaldirkbytes << " Kbytes found\r\n";
      else
        stream << "====> " << totalcount << " duplicate files with " << totaldirkbytes << " Kbytes deleted\r\n";
      m_engine->setProgressMaximum(1);
      m_engine->setProgressValue(0);
    }
    else
    {
      QString actPath = startPath;
      QString configfile = getTitle();

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
            filename = backupDirstruct::cutFilenamePrefix(filename);

            QString srcfile = source + (fi.absolutePath()+"/"+filename).mid(destination.length());
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
                  stream << indent << "     " << "    (keeping file " << mappedFile << ") \r\n";

                  totaldirkbytes += size / 1024;
                  totalcount++;
                }
                else if( showTreeStruct->isChecked() )
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
              }
              else if( operatingOnSource || !QFile::exists(srcfile) )
              {
                //if( showTreeStruct->isChecked() )
                //    stream << indent << "---> " << "mapping " << fi.absolutePath()+"/"+filename << "\r\n";
                filemap.insert(filename,fi.absolutePath()+";"+filemodified.toString()+";"
                +QString::number(size));
              }
              else if( showTreeStruct->isChecked() )
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

    tocDataContainerMap::iterator it1 = m_dirs.getFirstElement();
    while( m_running &&(it1!=m_dirs.getLastElement()) )
    {
      tocDataEntryMap::iterator it2 = it1.value().begin();

      QList< QPair<QString,QString> > tobeRemovedFromToc;

      QString relpath = it1.key();
      m_engine->setFileNameText(destination + "/" + relpath);
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
            tobeRemovedFromToc.append(qMakePair(listPath,listFile));
            tobeRemovedFromToc.append(qMakePair(relpath,filename));
          }
          else if( showTreeStruct->isChecked() )
          {
            sourceFileRemoved = destination + "/" + relpath+"/"+it2.key();
            mappedFile = listPath+"/"+filename;
            stream << ""/*indent*/ << "     " << "  found file " << sourceFileRemoved
            << " but sizes are different (" << QString::number(listSize)
            << "/" << QString::number(m_dirs.getNewestEntry(it2).m_size) << ")"
            << "\r\n";
            stream << ""/*indent*/ << "     " << "(mapped file " << mappedFile << "/" << filename << ")"
            << "\r\n";
          }
        }
        else
        {
          filemap.insert(filename,relpath+";"+filename+";"+QString::number(m_dirs.getNewestEntry(it2).m_modify)+";"
            +QString::number(m_dirs.getNewestEntry(it2).m_size));
        }
        // hier war #if 0
        ++it2;
      }

      QList< QPair<QString,QString> >::iterator it3 = tobeRemovedFromToc.begin();
      while( it3!=tobeRemovedFromToc.end() )
      {
        QString relPath = (*it3).first;
        QString file = (*it3).second;
        QString srcPath = "";

        if( relPath=="." )
        {
            srcPath = source + "/" + file;
            relPath = "";
        }
        else
            srcPath = source + "/" + relPath + "/" + file;


        if( QFile::exists(srcPath) )
        {
          ++it3;
          continue;
        }

        tocDataEntryList &entries = m_dirs.getEntryList(relPath,file);
        tocDataEntryList::iterator it4 = entries.begin();
        while( it4!=entries.end() )
        {
          totaldirkbytes += (*it4).m_size / 1024;
          ++it4;
        }
        totalcount++;

        QStringList toBeDeleted;
        m_dirs.removeFile(relPath,file,toBeDeleted);

        QStringList::iterator it = toBeDeleted.begin();
        while (it!=toBeDeleted.end())
        {
          QString fullName = destination + "/" + *it;
          QFile::remove(fullName);
          ++it;
        }

        ++it3;
      }
      ++it1;
    }

    m_engine->setProgressMaximum(1);
    m_engine->setProgressValue(0);
  }
}

void backupExecuter::verifyBackup(QString const &startPath)
{
  if( startPath.isNull() )
  {
    if( verboseExecute->isChecked() )
      return;

    m_engine->setProgressText("verifying files in backup...");
    m_engine->setProgressMaximum(lastVerifiedK);
    verifiedK = 0;
    verifyBackup(destination);
    m_engine->setProgressMaximum(1);
    m_engine->setProgressValue(0);
    if ( m_running )
      lastVerifiedK = verifiedK;
  }
  else
  {
    QString actPath = startPath;
    QString configfile = getTitle();

    QDir dir(actPath);
    dir.setSorting(QDir::Name);
    const QFileInfoList &list = dir.entryInfoList();
    QFileInfoList::const_iterator it=list.begin();
    QFileInfo fi;

    m_engine->setFileNameText(actPath);
    //progressBar->setValue(scanned++);
    if( getBackground() )
      m_waiter.Sleep(20);

    QString lastFile = "";
    QString lastName = "";

    QString indent = "";

    while ( m_running && (it!=list.end()) )
    {
      checkTimeout();

      fi = *it;
      QString name = fi.fileName();
      if( name!=".." && name!="." && name!=configfile && !name.endsWith(".crcs") )
      {
        if( fi.isDir() )
        {
          verifyBackup(actPath+"/"+name);
        }
        else
        {
          QString filename = fi.fileName();
          filename = backupDirstruct::cutFilenamePrefix(filename);

          QString verifyFile = fi.absolutePath()+"/"+fi.fileName();
          //QString dstFile = fi.absolutePath()+"/"+filename;
          //QString srcfile = source + dstFile.mid(destination.length());

          QDateTime scanTime;
          bool willVerify = true;

          // check if this file has an own verify interval
          if( m_closeAfterExecute && crcSummary.contains(verifyFile) )
          {
            scanTime = QDateTime::fromTime_t(crcSummary[verifyFile].lastScan);
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
          }

          if( willVerify )
          {
            if( verboseMaintenance->isChecked() || verboseExecute->isChecked() )
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
                  if( getBackground() )
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
                  if( getBackground() )
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
              /*
                if( QFile::exists(verifyFile+".crcs") )
                {
                  QFile crcfile(verifyFile+".crcs");
                  crcfile.open(QIODevice::ReadOnly);
                  QDataStream str(&crcfile);
                  QVector<quint16> verify;
                  str >> verify;
                  crcfile.close();
                  if( verify!=crclist )
                    errFound = true;
                }
                else
                {
                  QFile crcfile(verifyFile+".crcs");
                  crcfile.open(QIODevice::WriteOnly | QIODevice::Truncate);
                  QDataStream str(&crcfile);
                  str << crclist;
                  crcfile.close();
                }
*/
              if( errFound )
              {
                m_error = true;
                QString str = "++++ verify error in backup file '" + verifyFile + "'!\r\n";
                stream << str; errstream.append(str);
              }
              else if( verboseMaintenance->isChecked() || verboseExecute->isChecked() )
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
          }
          else
          {
            if( verboseMaintenance->isChecked() || verboseExecute->isChecked() )
              stream << "--- skipping verify on '" << verifyFile << " , scanned on " << scanTime.toString() << "\r\n";
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
  return getSrc()+"/"+getTitle().remove(" ") + suffix + ".vibup";
}

bool backupExecuter::isAutoBackupCreatedFile(const QString &file)
{
  return backupDirstruct::isTocSummaryFile(file) || file.contains(".vibup");
}
