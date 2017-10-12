#include <qapplication.h>
#include <qsettings.h>
#include <qlistwidget.h>
#include <qpushbutton.h>
#include <qpainter.h>
#include <qmessagebox.h>

#include <qfile.h>
#include <qfileinfo.h>
#include <qtextstream.h>
#include <qdatetime.h>
#include <qmenu.h>
#include <qinputdialog.h>
#include <qfiledialog.h>

#include "backupMain.h"
#include "backupExecuter.h"
#include "cleanupDialog.h"
#include "utilities.h"

class backupListItem : public QListWidgetItem
{
public:
  backupListItem( QListWidget *listbox, QString const &name,
                  QString const &src, QString const &dst, QString const &flt,
                  bool autoStart,int interval,bool keep,int versions,bool zlib,
                  bool suspend, int timeout) : QListWidgetItem(name,listbox)
  , m_executer(NULL)
  , m_sText(name)
  , m_sSrc(src)
  , m_sDst(dst)
  , m_sFlt(flt)
  , m_bAuto(autoStart)
  , m_iInterval(interval)
  , m_bKeep(keep)
  , m_iVersions(versions)
  , m_bzlib(zlib)
  , m_bsuspend(suspend)
  , m_iTimeout(timeout)
  , m_bexecuted(false)
  {
    setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled|Qt::ItemIsTristate);
    if( m_bAuto )
      setCheckState(Qt::Checked);
    else
      setCheckState(Qt::Unchecked);
  }
  virtual ~backupListItem()
  {
    delete m_executer;
  }

  void editBackupItem(bool closeAferExecute,bool *askForShutdown)
  {
    backupExecuter executer(m_sText,m_sSrc,m_sDst,m_sFlt,m_bAuto,m_iInterval,m_bKeep,m_iVersions,m_bzlib,m_bsuspend,m_iTimeout);
    executer.setCloseAfterExecute(closeAferExecute);
    executer.setAskForShutdown(askForShutdown);
    executer.exec();
    m_bexecuted = executer.result()==QDialog::Accepted;

    m_sText = executer.getTitle();
    m_sSrc = executer.getSrc();
    m_sDst = executer.getDst();
    m_sFlt = executer.getFlt();
    m_bAuto = executer.getAuto();
    m_iInterval = executer.getInterval();
    m_bKeep = executer.getKeep();
    m_iVersions = executer.getVersions();
    m_bzlib = executer.getCompress();
    m_bsuspend = executer.getSuspend();
    m_iTimeout = executer.getTimeout();

    setText(m_sText);
    if( m_bAuto )
      setCheckState(Qt::Checked);
    else
      setCheckState(Qt::Unchecked);

    listWidget()->repaint();
  }

  void startBatchExecution()
  {
    if( m_executer==NULL )
      m_executer = new backupExecuter(m_sText,m_sSrc,m_sDst,m_sFlt,m_bAuto,m_iInterval,m_bKeep,m_iVersions,m_bzlib,m_bsuspend,m_iTimeout);
    m_executer->startBatchProcessing();
  }

  void stopBatchExecution()
  {
    if( m_executer==NULL )
      m_executer = new backupExecuter(m_sText,m_sSrc,m_sDst,m_sFlt,m_bAuto,m_iInterval,m_bKeep,m_iVersions,m_bzlib,m_bsuspend,m_iTimeout);
    m_executer->stopBatchProcessing();
  }

  void executebackupItem(bool closeAferExecute,bool runningInBackground)
  {
    if( m_executer==NULL )
      m_executer = new backupExecuter(m_sText,m_sSrc,m_sDst,m_sFlt,m_bAuto,m_iInterval,m_bKeep,m_iVersions,m_bzlib,m_bsuspend,m_iTimeout);
    m_executer->setCloseAfterExecute(closeAferExecute);
//    m_executer->startBatchProcessing();
    m_executer->show();
    m_executer->doIt(runningInBackground);
    m_bexecuted = true;
  }

  void verifyBackupItem(bool closeAferExecute,bool runningInBackground)
  {
    if( m_executer==NULL )
      m_executer = new backupExecuter(m_sText,m_sSrc,m_sDst,m_sFlt,m_bAuto,m_iInterval,m_bKeep,m_iVersions,m_bzlib,m_bsuspend,m_iTimeout);
    m_executer->setCloseAfterExecute(closeAferExecute);
//    m_executer->startBatchProcessing();
    m_executer->show();
    m_executer->verifyIt(runningInBackground);
  }

  bool executionDone()
  {
    return m_bexecuted;
  }

  backupExecuter    *m_executer;

  QString			m_sText;
  QString			m_sSrc;
  QString			m_sDst;
  QString			m_sFlt;
  bool			m_bAuto;
  int				m_iInterval;
  bool			m_bKeep;
  int				m_iVersions;
  bool			m_bzlib;
  bool      m_bsuspend;
  int       m_iTimeout;
  bool			m_bexecuted;
};

void do_log( QString const &txt )
{
  QFile *log = backupExecuter::openFile(QString::null);
  QTextStream stream;

  if( log->isOpen() )
    stream.setDevice(log);

  stream << "**** " << QDateTime::currentDateTime().toString() << ": " << txt << "\r\n";

  log->close();
}

backupMain::backupMain(bool runsAsAdmin, QString const &configfile)
: m_selected(0)
, m_immediateShutdown(false)
{
  QSettings settings;
  QString path = settings.value("ConfigFile","").toString();
  if( path.isEmpty() ) settings.setValue("ConfigFile",path = "./backup.cfg");

  QFile config;
  QString line;

  setupUi(this);

  if( runsAsAdmin )
    setWindowTitle(windowTitle()+" with Administrator Rights "+BACKUP_VERSION);
  else
    setWindowTitle(windowTitle()+" "+BACKUP_VERSION);

  config.setFileName( configfile.isEmpty() ? path : configfile );

  do_log("starting backup tool");
  do_log("config file is '"+config.fileName()+"'");

  if( config.open(QIODevice::ReadOnly) )
  {
    QTextStream stream(&config);

    while ( !stream.atEnd() )
    {
      line = stream.readLine(); // line of text excluding '\n'
      QStringList list = line.split("\t");

      bool autos = list.at(4)=="1";
      int interval = (list.at(5)).toInt();
      bool keepc = list.count()>6 ? list.at(6)=="1" : false;
      int vrs = list.count()>7 ? (list.at(7)).toInt() : 2;
      bool zlib = list.count()>8 ? list.at(8)=="1" : false;
      bool suspend = list.count()>9 ? list.at(9)=="1" : false;
      int timeout = list.count()>10 ? list.at(10).toInt() : 3;
      new backupListItem(backupList,list.at(0),list.at(1),list.at(2),list.at(3),autos,interval,keepc,vrs,zlib,suspend,timeout);
    }
    config.close();
  }

  keepRun->setChecked(settings.value("KeepRunning").toInt()==1 ? true : false);
  shutDown->setChecked(settings.value("RunAndShtDwn").toInt()==1 ? true : false);

  checkSelection();
}

backupMain::~backupMain()
{
  do_log("ending backup tool");
  QSettings settings;
  settings.setValue("WindowTitlebarHeight",geometry().y()-frameGeometry().y());
  settings.setValue("WindowFramewidth",geometry().x()-frameGeometry().x());
  settings.setValue("KeepRunning",keepRun->isChecked() ? 1 : 0);
  settings.setValue("RunAndShtDwn",shutDown->isChecked() ? 1 : 0);
}

void backupMain::aboutButton()
{
  QMenu menu(this);
  QAction *about = menu.addAction("About...");
  menu.addSeparator();
  QAction *configfile = menu.addAction("Change config file...");
  QAction *logfile =    menu.addAction("Change log file...");
  QAction *iconpos =    menu.addAction("'Keep running' icon position");
  menu.addSeparator();
  QAction *help = menu.addAction("Help...");

  QAction *result = menu.exec(QCursor::pos());
  if( result==about )
  {
    QMessageBox::about(this,"about backup",windowTitle()+"\n(C) 2009-2010 Valentin Illich\n\nThis program is distributed under the GPL license.\nPlease refer to www.VISolutions.com");
  }
  if( result==help )
  {
    showHelp(this,":/helptext/AutomaticBackup.pdf","AutomaticBackup.pdf");
  }
  if( result==iconpos )
  {
    QStringList items;
    QSettings settings;
    QString position = settings.value("BackupWindowPosition","ll").toString();

    items << tr("lower left") << tr("upper right") << tr("upper left") << tr("lower right");
    int currentpos = 0;

    if( position=="ll" )
      currentpos = 0;
    else if( position=="ur" )
      currentpos = 1;
    else if( position=="ul" )
      currentpos = 2;
    else if( position=="lr" )
      currentpos = 3;

    bool ok;
    QString item = QInputDialog::getItem(this, tr("QInputDialog::getItem()"), tr("Icon position:"), items, currentpos, false, &ok);

    if( ok && !item.isEmpty() )
    {
      if( item=="lower left" )
        settings.setValue("BackupWindowPosition","ll");
      if( item=="upper right" )
        settings.setValue("BackupWindowPosition","ur");
      if( item=="upper left" )
        settings.setValue("BackupWindowPosition","ul");
      if( item=="lower right" )
        settings.setValue("BackupWindowPosition","lr");
    }
  }
  if( result==configfile )
  {
    QSettings settings;
    QString path = settings.value("ConfigFile","").toString();

    QString newFile = QFileDialog::getSaveFileName(this,"select config file",path,"Config files (*.cfg)");

    if( !newFile.isEmpty() )
    {
      settings.setValue("ConfigFile",newFile);
      saveConfig(newFile);
    }
  }
  if( result==logfile )
  {
    QSettings settings;
    QString path = settings.value("LogFile","").toString();

    QString newFile = QFileDialog::getSaveFileName(this,"select log file",path,"Log files (*.log)");

    if( !newFile.isEmpty() )
    {
      settings.setValue("LogFile",newFile);
    }
  }
}

void backupMain::saveConfig(QString const &configFile)
{
  QFile config;
  if( configFile.isEmpty() )
  {
    QSettings settings;
    QString path = settings.value("ConfigFile","./backup.cfg").toString();
    config.setFileName(path);
  }
  else
    config.setFileName(configFile);
  if( config.open(QIODevice::WriteOnly) )
  {
    QTextStream stream(&config);

    for( int i=0; i<backupList->count(); i++ )
    {
      backupListItem *item = static_cast<backupListItem*>(backupList->item(i));
      stream	<< item->m_sText << "\t"
      << item->m_sSrc << "\t"
      << item->m_sDst << "\t"
      << item->m_sFlt << "\t"
      << item->m_bAuto << "\t"
      << item->m_iInterval << "\t"
      << item->m_bKeep << "\t"
      << item->m_iVersions << "\t"
      << item->m_bzlib << "\t"
      << item->m_bsuspend << "\t"
      << item->m_iTimeout << "\n";
    }
    config.close();
  }
}

void backupMain::newBackup()
{
  new backupListItem(backupList,"unnamed","","","",false,0,false,3,false,false,3);
}

void backupMain::quit()
{
  //qApp->quit();
  accept();
}

void backupMain::checkSelection()
{
  m_selected = backupList->selectedItems().count();

  keepRun->setEnabled(true);
  if( shutDown->isChecked() )
  {
    exitButt->setText("Close");
    keepRun->setChecked(true);
    keepRun->setEnabled(false);
  }
  else if( keepRun->isChecked() )
    exitButt->setText("Close");
  else
    exitButt->setText("Exit");

  if( m_selected==0 )
  {
    deleteBackupButt->setEnabled(false);
    editBackupButt->setEnabled(false);
    editBackupButt->setText("Select...");
    cleanButt->setEnabled(true);
    cleanButt->setText("Analyze...");
  }
  else if( m_selected==1 )
  {
    deleteBackupButt->setEnabled(true);
    editBackupButt->setEnabled(true);
    editBackupButt->setText("Select...");
    cleanButt->setEnabled(true);
    cleanButt->setText("Cleanup...");
  }
  else
  {
    deleteBackupButt->setEnabled(true);
    editBackupButt->setEnabled(true);
    editBackupButt->setText("Execute...");
    cleanButt->setEnabled(false);
    //        cleanButt->setText("Cleanup...");
  }
}

void backupMain::editBackup()
{
  m_immediateShutdown = false;

  QList<QListWidgetItem*> const &list = backupList->selectedItems();
  for( QList<QListWidgetItem*>::const_iterator it=list.begin(); it!=list.end(); ++it )
  {
    QListWidgetItem *item = *it;

    if( m_selected==1 )
    {
      if( shutDown->isChecked() )
        static_cast<backupListItem*>(item)->editBackupItem(true,&m_immediateShutdown);
      else
        static_cast<backupListItem*>(item)->editBackupItem(false,NULL);
    }
    else
    {
      static_cast<backupListItem*>(item)->executebackupItem(true,false);
    }
  }

  if( m_immediateShutdown )
    accept();
  else
    activateWindow();
}

void backupMain::cleanupBackup()
{
  QList<QListWidgetItem*> const &list = backupList->selectedItems();
  for( QList<QListWidgetItem*>::const_iterator it=list.begin(); it!=list.end(); ++it )
  {
    QListWidgetItem *item = *it;

    if( m_selected==1 )
    {
      backupListItem* selected = static_cast<backupListItem*>(item);
      QString path = selected->m_sDst;

      cleanupDialog dlg(0);
      dlg.setPaths(selected->m_sSrc,selected->m_sDst);
      dlg.show();
      dlg.activateWindow();
      dlg.doAnalyze();
      dlg.exec();
    }
  }

  if( m_selected==0 )
  {
    cleanupDialog dlg(0);
    dlg.exec();
  }

  activateWindow();
}

void backupMain::deleteBackup()
{
  QList<QListWidgetItem*> const &list = backupList->selectedItems();
  for( QList<QListWidgetItem*>::const_iterator it=list.begin(); it!=list.end(); ++it )
  {
    QListWidgetItem *item = *it;

    if( QMessageBox::information(this,"confirmation","Do you really want to delete '"
    +static_cast<backupListItem*>(item)->m_sText+"' ?",
    QMessageBox::Yes,QMessageBox::No|QMessageBox::Default)==QMessageBox::Yes )
      delete item;
  }
}

void backupMain::autoExecute(bool runningInBackground)
{
  for( int i=0; i<backupList->count(); i++ )
  {
    backupListItem *item = static_cast<backupListItem*>(backupList->item(i));
    do_log("autoExecute found item '"+item->m_sText+"'");
    if( item->m_bAuto )
    {
      do_log("autoExecute testing automatic item '"+item->m_sText+"'");
      QFileInfo tstbackup(item->m_sSrc+"/"+item->m_sText.remove(" "));
      QFileInfo tstverify(item->m_sSrc+"/"+item->m_sText.remove(" ")+"_chk");
      bool doItBackup = true;
      bool doItVerify = true;
      int backupdays = 0;
      int verifydays = 0;
      switch( item->m_iInterval )
      {
        case 0://daily
          backupdays = 1;
          verifydays = 7;
          break;
        case 1://weekly
          backupdays = 7;
          verifydays = 30;
          break;
        case 2://14 days
          backupdays = 14;
          verifydays = 30;
          break;
        case 3://monthly
          backupdays = 30;
          verifydays = 90;
          break;
        case 4://3 months
          backupdays = 90;
          verifydays = 180;
          break;
        case 5://yearly
          backupdays = 365;
          verifydays = 365;
          break;
      }
      if( tstbackup.exists() )
      {
        int diff = tstbackup.lastModified().daysTo(QDateTime::currentDateTime());
        do_log("autoExecute item backup period is "+QString::number(backupdays)+" days, tested file info '"+tstbackup.absoluteFilePath()+"' was "+QString::number(diff)+" days ago.");
        if(  diff < backupdays )
          doItBackup = false;
      }
      else
        do_log("autoExecute item backup period is "+QString::number(backupdays)+" days, tested file not found.");

      if( tstverify.exists() )
      {
        int diff = tstverify.lastModified().daysTo(QDateTime::currentDateTime());
        do_log("autoExecute item verify period is "+QString::number(verifydays)+" days, tested file info '"+tstverify.absoluteFilePath()+"' was "+QString::number(diff)+" days ago.");
        if(  diff < verifydays )
          doItVerify = false;
      }
      else
        do_log("autoExecute item verify period is "+QString::number(verifydays)+" days, tested file not found.");

      if( doItBackup )
      {
        item->startBatchExecution();
        item->executebackupItem(true,runningInBackground);
      }

      if( doItVerify )
      {
        if( !doItBackup ) item->startBatchExecution();
        item->verifyBackupItem(true,runningInBackground);
      }

      if( doItBackup || doItVerify )
        item->stopBatchExecution();
    }
  }
  activateWindow();
}

bool backupMain::keepRunning() const
{
  return keepRun->isChecked();
}

bool backupMain::runAndShutdown() const
{
  return shutDown->isChecked();
}

bool backupMain::immediateShutdown() const
{
  return m_immediateShutdown;
}
