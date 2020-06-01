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
#include "cleanupdialog.h"
#include "Utilities.h"

class backupListItem : public QListWidgetItem
{
public:
  backupListItem( QListWidget *listbox, backupConfigData &confiData )
  : QListWidgetItem(confiData.m_sName,listbox)
  , m_config(confiData)
  , m_executer(NULL)
  {
    setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled|Qt::ItemIsTristate);
    if( m_config.m_bAuto )
      setCheckState(Qt::Checked);
    else
      setCheckState(Qt::Unchecked);
  }
  virtual ~backupListItem()
  {
    delete m_executer;
  }

  backupConfigData &getConfigData()
  {
    return m_config;
  }

  void editBackupItem(bool closeAferExecute,bool *askForShutdown)
  {
    backupExecuter executer(m_config);
    executer.setCloseAfterExecute(closeAferExecute);
    executer.setAskForShutdown(askForShutdown);
    executer.exec();
    //m_config.m_bexecuted = executer.result()==QDialog::Accepted;

    m_config = executer.getConfigData();

    setText(m_config.m_sName);
    if( m_config.m_bAuto )
      setCheckState(Qt::Checked);
    else
      setCheckState(Qt::Unchecked);

    listWidget()->repaint();
  }

  backupExecuter *getBackupExecuter()
  {
    if( m_executer==NULL )
      m_executer = new backupExecuter(m_config);
    return m_executer;
  }

  void startBatchExecution()
  {
    if( m_executer==NULL )
      m_executer = new backupExecuter(m_config);
    m_executer->startBatchProcessing();
  }

  void stopBatchExecution()
  {
    if( m_executer==NULL )
      m_executer = new backupExecuter(m_config);
    m_executer->stopBatchProcessing();
  }

  void executebackupItem(bool closeAferExecute,bool runningInBackground)
  {
    if( m_executer==NULL )
      m_executer = new backupExecuter(m_config);
    m_executer->setCloseAfterExecute(closeAferExecute);
//    m_executer->startBatchProcessing();
    m_executer->show();
    m_executer->doIt(runningInBackground);
    //m_config.m_bexecuted = true;
  }

  void verifyBackupItem(bool closeAferExecute,bool runningInBackground)
  {
    if( m_executer==NULL )
      m_executer = new backupExecuter(m_config);
    m_executer->setCloseAfterExecute(closeAferExecute);
//    m_executer->startBatchProcessing();
    m_executer->show();
    m_executer->verifyIt(runningInBackground);
  }

/*  bool executionDone()
  {
    return m_config.m_bexecuted;
  }*/

private:
  backupConfigData  m_config;
  backupExecuter    *m_executer;
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
    setWindowTitle(windowTitle()+" with Administrator Rights "+BACKUP_STR_VERSION);
  else
    setWindowTitle(windowTitle()+" "+BACKUP_STR_VERSION);

  config.setFileName( configfile.isEmpty() ? path : configfile );

  do_log("starting backup tool");
  do_log("config file is '"+config.fileName()+"'");

  if( config.open(QIODevice::ReadOnly) )
  {
    QTextStream stream(&config);

    while ( !stream.atEnd() )
    {
      line = stream.readLine(); // line of text excluding '\n'

      backupConfigData config;
      config.getFromString(line);

      new backupListItem(backupList,config);
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
    QMessageBox::about(this,"about backup",windowTitle()+"."+BACKUP_STR_BUILDNO+BACKUP_STR_ABOUTINFO
                       +"\n\nThis program is distributed under the GPL license.\nPlease refer to www.VISolutions.com");
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
      stream	<< item->getConfigData().putToString();
    }
    config.close();
  }
}

void backupMain::newBackup()
{
  backupConfigData config;
  new backupListItem(backupList,config);
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
    cleanDirButt->setEnabled(true);
    cleanDirButt->setText("Analyze Directory...");
  }
  else if( m_selected==1 )
  {
    deleteBackupButt->setEnabled(true);
    cleanDirButt->setEnabled(true);
    cleanDirButt->setText("Analyze Backup...");
  }
  else
  {
    deleteBackupButt->setEnabled(true);
    cleanDirButt->setEnabled(false);
    cleanDirButt->setText("Analyze Directory...");
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

void backupMain::cleanupDirectory()
{
  QList<QListWidgetItem*> const &list = backupList->selectedItems();
  for( QList<QListWidgetItem*>::const_iterator it=list.begin(); it!=list.end(); ++it )
  {
    QListWidgetItem *item = *it;

    if( m_selected==1 )
    {
      backupListItem* selected = static_cast<backupListItem*>(item);
      QString path = selected->getConfigData().m_sDst;

      cleanupDialog dlg(0);
      dlg.setPaths(selected->getConfigData().m_sSrc,selected->getConfigData().m_sDst);
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

void backupMain::cleanupBackup()
{
  QList<QListWidgetItem*> const &list = backupList->selectedItems();
  for( QList<QListWidgetItem*>::const_iterator it=list.begin(); it!=list.end(); ++it )
  {
    QListWidgetItem *item = *it;

    if( m_selected==1 )
    {
      backupListItem* selected = static_cast<backupListItem*>(item);
      QString path = selected->getConfigData().m_sDst;

      cleanupDialog dlg(0);
      dlg.setPaths(selected->getConfigData().m_sSrc,selected->getConfigData().m_sDst);
      dlg.show();
      dlg.activateWindow();
      dlg.doAnalyze();
      dlg.exec();
    }
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
    +static_cast<backupListItem*>(item)->getConfigData().m_sName+"' ?",
    QMessageBox::Yes,QMessageBox::No|QMessageBox::Default)==QMessageBox::Yes )
      delete item;
  }
}

void backupMain::autoExecute(bool runningInBackground)
{
  for( int i=0; i<backupList->count(); i++ )
  {
    backupListItem *item = static_cast<backupListItem*>(backupList->item(i));
    do_log("autoExecute found item '"+item->getConfigData().m_sName+"'");
    if( item->getConfigData().m_bAuto )
    {
      do_log("autoExecute testing automatic item '"+item->getConfigData().m_sName+"'");
      QFileInfo tstbackup(item->getBackupExecuter()->getAutobackupCheckFile(""));
      QFileInfo tstverify(item->getBackupExecuter()->getAutobackupCheckFile("_chk"));
      bool doItBackup = true;
      bool doItVerify = true;
      int backupdays = 0;
      int verifydays = 0;
      switch( item->getConfigData().m_iInterval )
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
        item->getBackupExecuter()->processEventsAndWait();
      }

      if( doItVerify )
      {
        if( !doItBackup ) item->startBatchExecution();
        item->verifyBackupItem(true,runningInBackground);
        item->getBackupExecuter()->processEventsAndWait();
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
