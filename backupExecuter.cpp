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
, collectingDeleted(false)
, checksumsChanged(false)
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

  QString summaryFile = destination+"/checksumsummary.crcs";
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
}

void backupExecuter::screenResizedSlot( int screen )
{
  changeVisibility();
}

void backupExecuter::saveData()
{
  if( checksumsChanged )
  {
    QString summaryFile = destination+"/checksumsummary.crcs";
    QFile crcfile(summaryFile);
    crcfile.open(QIODevice::WriteOnly | QIODevice::Truncate);
    QDataStream str(&crcfile);
    str << 0x80000000;
    str << lastVerifiedK;
    str << crcSummary;
    crcfile.close();
  }
  checksumsChanged = false;
}

void backupExecuter::changeVisibility()
{
  if( getBackground() )
  {
    tabWidget->hide();
    progressLab->hide();
    executeButt->hide();

    if( m_isBatch )
    {
      actualFile->hide();
      cancelButt->hide();
      setToolTip(QString("Automatic Backup V")+BACKUP_VERSION+" running.\nProcessing Configuration '"+getTitle()+"'");
    }
    else
    {
      actualFile->setFixedWidth(450);
      actualFile->setAlignment(Qt::AlignRight);
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

void backupExecuter::startingAction()
{
  if( m_isBatch ) return;

  QDateTime dt = QDateTime::currentDateTime();
  appendixcpy = dt.toString("yyyyMMdd");
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
          fp->write(errstream.toAscii());
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
  progressLab->setText("");
  actualFile->setText("");
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
    QFile dir1(destination);
    if( !dir1.exists() )
    {
      mediaMessage msg(this,destination);
      msg.exec();
      if( msg.result()!=Accepted )
      {
        m_running=false;
        return;
      }
      destination = msg.getPath();
    }
    QFile dir2(source);
    if( !dir2.exists() )
    {
      mediaMessage msg(this,source);
      msg.exec();
      if( msg.result()!=Accepted )
      {
        m_running=false;
        return;
      }
      source = msg.getPath();
    }

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

  progressLab->setText("Scanning Directories...");
  progressBar->setMaximum(dircount);

  for ( QStringList::Iterator it=directories.begin(); m_running && it!=directories.end(); ++it,i++ )
  {
    checkTimeout();

    if( m_isBatch )
      setToolTip(QString("Automatic Backup V")+BACKUP_VERSION+" running.\nProcessing Directory '"+*it+"'");
    actualFile->setText(*it);
    progressBar->setValue(i);
    qApp->processEvents();
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

      qApp->processEvents();
      if( getBackground() )
        m_waiter.Sleep(20);

      srcFile = *it2;
      QString actualName = srcFile.fileName();
      QString fullName = srcFile.absoluteFilePath();
      bool passed = true;
      bool copy = true;
      bool found = false;
      bool fix = false;

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

      if( !srcFile.isSymLink() && actualName!="." && actualName!=".." )
      {
        if( passed )
        {
          QDir dir(dstpath);
          // we must avoid the [] characters inside search mask (regexp) so
          // we replace them by the '?' wildcard and check the src and dst filenames later on
          QString filter = actualName.replace("[","?").replace("]","?");

          if( getCompress() ) filter += "_";
          if( getKeep() ) filter += ".*";

          const QFileInfoList &fl = dir.entryInfoList(QStringList(filter));
          QFileInfoList::const_iterator it5 = fl.begin();

          QFileInfo fi;

          int n = fl.count();
          int v = getVersions();
          int toDelete = n > v ? n-v : 0;
          int cnt = 0;

          while ( it5!=fl.end() ) {
            fi = *it5;
            QString destFilename = fi.fileName();
            if( getKeep() ) destFilename = destFilename.left(destFilename.length()-9);
            if( getCompress() ) destFilename = destFilename.left(destFilename.length()-1);
            if( srcFile.fileName()==destFilename )
            {
              // first of all, fix src modification time if neccessary (must be greater or equal to creation time)
/*              if( srcFile.created()>srcFile.lastModified() )
              {
                if( verboseExecute->isChecked() )
                  stream << "fix src file(s) "+filter+": "
                  +"c("+srcFile.created().toString(Qt::ISODate)
                  +") m("+srcFile.lastModified().toString(Qt::ISODate)
                  +"'\r\n";
                else
                  setTimestamps(srcFile.absoluteFilePath(),srcFile.created());
              }*/

              if( srcFile.lastModified()>fi.lastModified() )
              {
                found = true;
                copy = true;
              }
              else
              {
                found = true;
                copy = false;
                if( (fi.lastModified()>srcFile.lastModified()) )
                  fix = true;
                break;
              }
              if( getKeep() )
              {
                if( (cnt<(n-1)) && (cnt<toDelete) )//never will delete last that means latest backup version
                {
                  if( verboseExecute->isChecked() )
                    stream << "will delete the "+QString::number(cnt)+"the destination file '"+fi.absoluteFilePath()+"'\r\n";
                  QFile::remove(fi.absoluteFilePath());
                }
              }
            }
            ++it5;
            cnt++;
          }
          if( verboseExecute->isChecked() )
          {
            if( found )
            {
              if( copy )
                stream << "    file(s) "+filter+": "
                +"c("+srcFile.created().toString(Qt::ISODate)
                +") m("+srcFile.lastModified().toString(Qt::ISODate)
                +"), dst c("+fi.created().toString(Qt::ISODate)
                +") m("+fi.lastModified().toString(Qt::ISODate)
                +"), found dst was '"+fi.absoluteFilePath()+"'\r\n";
              if( fix )
                stream << "fix dst file(s) "+filter+": "
                +"c("+srcFile.created().toString(Qt::ISODate)
                +") m("+srcFile.lastModified().toString(Qt::ISODate)
                +"), dst c("+fi.created().toString(Qt::ISODate)
                +") m("+fi.lastModified().toString(Qt::ISODate)
                +"), found dst was '"+fi.absoluteFilePath()+"'\r\n";
            }
            else
              stream << "destination file(s) "+dstpath+"/"+filter+": not found.\r\n";
          }
          else
          {
            if( fix )
              setTimestamps(fi.absoluteFilePath(),srcFile.lastModified());
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
        QString relPath = fullName.mid(source.length());
        filelist.append(relPath);
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
  QString relPath;
  QString srcFile;
  QString dstFile;
  char *buffer = NULL;
  unsigned long copiedk = 0;

  if( !m_running ) return;

  const int buffsize = 5*1024*1024;

  buffer = new char[buffsize];

  progressLab->setText("processing Files...");
  progressBar->setMaximum(kbytes_to_copy);

  QTime startTime = QTime::currentTime();

  for ( QStringList::Iterator it2=filelist.begin(); m_running && it2!=filelist.end(); ++it2 )
  {
    checkTimeout();

    if( !m_running )
      break;

    actualFile->setText(*it2);
    progressBar->setValue(copiedk);
    qApp->processEvents();

    relPath = *it2;
    srcFile = source + relPath;
    dstFile = destination + relPath;
    if( getCompress() )
      dstFile += "_";
    if( getKeep() )
      dstFile += ("." + appendixcpy);

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
          progressBar->setValue(copiedk);
          qApp->processEvents();

          if( /*isBatch &&*/ getBackground() )
          {
            QTime actTime = QTime::currentTime();
            double ratio = ( (double)copiedk / (double)startTime.msecsTo(actTime) * 1000.0);
            QTime time(0,0);
            time = time.addSecs((int)((double)(kbytes_to_copy-copiedk)/(ratio)+0.5));
            setWindowTitle(time.toString("hh:mm:ss") +" remaining");
            if( m_isBatch )
              setToolTip(QString("Automatic Backup V")+BACKUP_VERSION+" running "+getTitle()+".\nProcessing File '"+*it2+"'...\n");
            qApp->processEvents();
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
          QFileInfo fi(srcFile);
          setTimestamps(dstFile,fi.lastModified());
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

  progressBar->setMaximum(1);
  progressBar->setValue(0);
  qApp->processEvents();

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

void backupExecuter::doIt(bool runningInBackground)
{
  m_background = runningInBackground || backgroundExec->isChecked();

  changeVisibility();

/*  if( m_quitAfterExecute )
  {
    if( QMessageBox::question(0,"security question","Do you want the system to shut down automatically after execution?",QMessageBox::Yes,QMessageBox::No)==QMessageBox::No )
      m_quitAfterExecute = false;
  }*/

  startingAction();

  findDirectories();
  analyzeDirectories();
  if( m_running && !verboseExecute->isChecked() )
    copySelectedFiles();

  if( /*m_running &&*/ m_dirsCreated ) // always check for duplicates even if cancelled
    findDuplicates();

  if( m_running && !m_isBatch )
    verifyBackup();

  if( m_running /*&& m_isBatch*/ )
  {
    stream << "updating backup time of automatic item '"+getTitle()+"'\r\n";
    QFile tst(getSrc()+"/"+getTitle().remove(" "));
    if( tst.open(QIODevice::WriteOnly) )
    {
      QTextStream stream(&tst);
      stream << appendixcpy;
      tst.close();
    }
    else
      stream << "++++ couldn't create automatic test item\r\n";
  }

  stoppingAction();

  if( verboseExecute->isChecked() || m_closed ) // when debugging this means: just like OK / when dialog closed: Cancel
    cancel();
  else if( m_closeAfterExecute )
    accept();								// this means: execution done, return to main window
}

void backupExecuter::verifyIt(bool runningInBackground)
{
  m_background = runningInBackground || backgroundExec->isChecked();

  changeVisibility();

  startingAction(); // do verify only if not cancelled in batch

  verifyBackup();

  if( m_running /*&& m_isBatch*/ )
  {
    stream << "updating verify time of automatic item '"+getTitle()+"'\r\n";
    QFile tst(getSrc()+"/"+getTitle().remove(" ")+"_chk");
    if( tst.open(QIODevice::WriteOnly) )
    {
      QTextStream stream(&tst);
      stream << appendixcpy;
      tst.close();
    }
    else
      stream << "++++ couldn't create automatic test item\r\n";
  }

  stoppingAction();

  if( verboseExecute->isChecked() || m_closed ) // when debugging this means: just like OK / when dialog closed: Cancel
    cancel();
  else if( m_closeAfterExecute )
    accept();								// this means: execution done, return to main window
}

void backupExecuter::cancel() // OK (Cancel when running) pressed
{
  if( m_running )
  {
    cancelButt->setText("OK");
    progressLab->setText("");
    actualFile->setText("");
    progressBar->setValue(0);
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

  actualFile->setText(startPath);
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
        if( getKeep() )
          relfile = relfile.left(relfile.length()-9);
        if( getCompress() )
          relfile = relfile.left(relfile.length()-1);
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

void backupExecuter::scanDirectory(QDate const &date, QString const &startPath, bool eraseAll)
{
  static int scanned;

  static unsigned totalcount;
  static unsigned long totaldirkbytes;

  static unsigned yearcount;
  static unsigned long yearkbytes;
  static unsigned halfcount;
  static unsigned long halfkbytes;
  static unsigned quartercount;
  static unsigned long quarterkbytes;
  static unsigned monthcount;
  static unsigned long monthkbytes;
  static unsigned daycount;
  static unsigned long daykbytes;

  static int level;

  if( startPath.isNull() )
  {
    progressLab->setText("Cleaning up Directories...");
    scanned = 0;
    totalcount = yearcount = halfcount = quartercount = monthcount = daycount = 0;
    totaldirkbytes = yearkbytes = halfkbytes = quarterkbytes = monthkbytes = daykbytes = 0;
    level = 0;
    progressBar->setMaximum(0);
    scanDirectory(date,destination);
    stream << "----> " << daycount << " files younger than a month with " << daykbytes << " Kbytes found\r\n";
    stream << "----> " << monthcount << " files between one and three months with " << monthkbytes << " Kbytes found\r\n";
    stream << "----> " << quartercount << " files between three months and half a year with " << quarterkbytes << " Kbytes found\r\n";
    stream << "----> " << halfcount << " files between half a year and a year with " << halfkbytes << " Kbytes found\r\n";
    stream << "----> " << yearcount << " files older than a year with " << yearkbytes << " Kbytes found\r\n";
    stream << "====> " << yearcount+halfcount+quartercount+monthcount+daycount << " total files with " << yearkbytes+halfkbytes+quarterkbytes+monthkbytes+daykbytes << " Kbytes found\r\n";
    if( verboseMaintenance->isChecked() )
      stream << "====> " << totalcount << " deletable files with " << totaldirkbytes << " Kbytes found\r\n";
    else
      stream << "====> " << totalcount << " files with " << totaldirkbytes << " Kbytes deleted\r\n";
    progressBar->setMaximum(1);
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

    actualFile->setText(actPath);
    progressBar->setValue(scanned++);
    qApp->processEvents();

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
      QString relPath = actPath.mid(destination.length());
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
            int cutting = 0;
            if( getCompress() ) cutting += 1;
            if( getKeep() )     cutting += 9;
            if( cutting>0 ) filename = filename.left(filename.length()-cutting);

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
              yearcount++;
              yearkbytes += (fi.size()/1024);
            }
            else if( filemodified.daysTo(today)>182 )
            {
              halfcount++;
              halfkbytes += (fi.size()/1024);
            }
            else if( filemodified.daysTo(today)>91 )
            {
              quartercount++;
              quarterkbytes += (fi.size()/1024);
            }
            else if( filemodified.daysTo(today)>30 )
            {
              monthcount++;
              monthkbytes += (fi.size()/1024);
            }
            else
            {
              daycount++;
              daykbytes += (fi.size()/1024);
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

  if( startPath.isNull() )
  {
    progressLab->setText("Finding duplicate files...");
    progressBar->setMaximum(0);
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
    progressBar->setMaximum(1);
    progressBar->setValue(0);
    qApp->processEvents();
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

    actualFile->setText(actPath);
    progressBar->setValue(scanned++);
    qApp->processEvents();

    QString lastFile = "";
    QString lastName = "";

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
          int cutting = 0;
          if( getCompress() ) cutting += 1;
          if( getKeep() )     cutting += 9;
          if( cutting>0 ) filename = filename.left(filename.length()-cutting);

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
                if( !QFile::exists(srcfile) && listDate>=fi.lastModified() )
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
            else if( !QFile::exists(srcfile) )
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

void backupExecuter::verifyBackup(QString const &startPath)
{
  if( startPath.isNull() )
  {
    if( verboseExecute->isChecked() )
      return;

    progressLab->setText("verifying files in backup...");
    progressBar->setMaximum(lastVerifiedK);
    verifiedK = 0;
    verifyBackup(destination);
    progressBar->setMaximum(1);
    progressBar->setValue(0);
    qApp->processEvents();
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

    actualFile->setText(actPath);
    //progressBar->setValue(scanned++);
    qApp->processEvents();
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
          int cutting = 0;
          if( getCompress() ) cutting += 1;
          if( getKeep() )     cutting += 9;
          if( cutting>0 ) filename = filename.left(filename.length()-cutting);

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
                progressBar->setValue(verifiedK);
              else
                progressBar->setMaximum(0);
              qApp->processEvents();
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
                    progressBar->setValue(verifiedK);
                  else
                    progressBar->setMaximum(0);
                  qApp->processEvents();
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
                    progressBar->setValue(verifiedK);
                  else
                    progressBar->setMaximum(0);
                  qApp->processEvents();
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
