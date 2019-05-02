#include <qdesktopwidget.h>
#include <qsettings.h>
#include <qpainter.h>
#include <qmenu.h>
#include <QString>
#include <QInputDialog>

#include "backupSplash.h"
#include "backupMain.h"
#include "backupExecuter.h"
#include "utilities.h"

extern "C" int getAdminRights(int argc, char* argv[],char *password);

class startMessage : public QMessageBox
{
public:
  startMessage(bool allowAdmin)
  : QMessageBox(QMessageBox::NoIcon,"","")
  , m_allowAdmin(allowAdmin)
  , m_seconds(allowAdmin ? 5 : 30)
  , m_config(0)
  , m_cancel(0)
  {
    QFontMetrics buttons(font());

    m_ok = addButton("Ok",QMessageBox::ActionRole);
    m_ok->setMinimumWidth(buttons.width("Config...")+50);
    m_config = addButton("Config...",QMessageBox::ActionRole);
    m_config->setMinimumWidth(buttons.width("Config...")+50);
    m_help =  addButton("Help",QMessageBox::ActionRole);
    m_help->setMinimumWidth(buttons.width("Config...")+50);
    if( allowAdmin )
    {
      m_admin = addButton("Root",QMessageBox::ActionRole);
      m_admin->setMinimumWidth(buttons.width("Config...")+50);
    }
    m_cancel = addButton("Quit",QMessageBox::ActionRole);
    m_cancel->setMinimumWidth(buttons.width("Config...")+50);
    setIconPixmap(QApplication::style()->standardPixmap(QStyle::SP_DesktopIcon));
    /*QFontMetrics metrics(qApp->font());
    int w = metrics.width("Automatic Backup will start in 5 seconds.")/2;
    m_ok->setFixedWidth(w);
    m_config->setFixedWidth(w);
    m_cancel->setFixedWidth(w);*/
    updateText();
    startTimer(1000);
  }
  bool isConfig()
  {
    return clickedButton()==m_config;
  }
  bool isHelp()
  {
    return clickedButton()==m_help;
  }
  bool isAdmin()
  {
    return clickedButton()==m_admin;
  }
  bool isQuit()
  {
    return clickedButton()==m_cancel;
  }
protected:
  virtual void timerEvent ( QTimerEvent */* event*/ )
  {
    m_seconds--;
    if( m_seconds==0 )
      accept();
    else
      updateText();
  }
private:
  void updateText()
  {
    if( m_allowAdmin )
      setText("Automatic Backup will start in "+QString::number(m_seconds)+" seconds.");
    else
      setText("Backup with Administrator Rights starts in "+QString::number(m_seconds)+" seconds.");
  }

  bool m_allowAdmin;
  int	m_seconds;
  QPushButton *m_ok;
  QPushButton *m_config;
  QPushButton *m_help;
  QPushButton *m_admin;
  QPushButton *m_cancel;
};

class shutDownMessage : public QMessageBox
{
public:
  shutDownMessage()
  : QMessageBox(QMessageBox::NoIcon,"","")
  , m_seconds(10)
  , m_ok(0)
  , m_cancel(0)
  {
    QFontMetrics buttons(font());

    m_ok = addButton("Continue...",QMessageBox::ActionRole);
    m_ok->setMinimumWidth(buttons.width("Continue...")+50);
    m_cancel = addButton("Quit",QMessageBox::ActionRole);
    m_cancel->setMinimumWidth(buttons.width("Continue...")+50);
    setIconPixmap(QApplication::style()->standardPixmap(QStyle::SP_ComputerIcon));
    updateText();
    startTimer(1000);
  }
  bool cancelled()
  {
    return clickedButton()==m_cancel;
  }
protected:
  virtual void timerEvent ( QTimerEvent */* event*/ )
  {
    m_seconds--;
    if( m_seconds==0 )
      accept();
    else
      updateText();
  }
private:
  void updateText()
  {
    setText("Computer will shut\ndown in "+QString::number(m_seconds)+" seconds.");
  }

  int	m_seconds;
  QPushButton *m_ok;
  QPushButton *m_cancel;
};

class quitMessage : public QMessageBox
{
public:
  quitMessage(QString const &message)
  : QMessageBox(QMessageBox::NoIcon,"","")
  , m_seconds(20)
  , m_ok(0)
  , m_template(message)
  {
    QFontMetrics buttons(font());

    m_ok = addButton("OK",QMessageBox::ActionRole);
    setIconPixmap(QApplication::style()->standardPixmap(QStyle::SP_ComputerIcon));
    updateText();
    startTimer(1000);
  }

protected:
  virtual void timerEvent ( QTimerEvent */* event*/ )
  {
    m_seconds--;
    if( m_seconds==0 )
      accept();
    else
      updateText();
  }
private:
  void updateText()
  {
    setText(m_template+"Automatic Backup will quit in "+QString::number(m_seconds)+" seconds.");
  }

  int	m_seconds;
  QPushButton *m_ok;
  QString m_template;
};

backupSplash::backupSplash(QObject */*parent = 0*/)
: QWidget(0,Qt::SplashScreen|Qt::WindowStaysOnTopHint)
, active(true)
, interactive(false)
, batch(false)
, shutdown(false)
, shtdwnwarning(true)
, shtdwncontinue(false)
{
  connect(qApp->desktop(),SIGNAL(resized(int)),this,SLOT(screenResizedSlot(int)));
}

backupSplash::~backupSplash()
{
}

void backupSplash::updateSettings(backupMain const &main)
{
  batch = main.keepRunning();
  shutdown = main.runAndShutdown();
  if( shutdown )
    shtdwnwarning = true; // warn only if configured for shutdown execution
  else
    shtdwnwarning = false;
}

bool backupSplash::startup(int argc,char **argv)
{
  QSettings settings;
  bool startDialog = (settings.value("KeepRunning").toInt()==1 ? false : true);

  QFile *fp = backupExecuter::openFile("errors",true);
  if( fp->fileName()!=QString::null )
  {
    QApplication::alert(this);
    QMessageBox::warning(this,"backup problems","at least one of the last executed backups had errors.\nPlease check the following details.");
    backupExecuter::displayResult(this,fp->readAll().data(),"backup errors");
    fp->close();
    fp->remove();
  }

  bool enableAdminButton = ( (argc<2) || (strcmp(argv[1],"-admin")!=0) );

  if( !interactive && startDialog )
  {
    startMessage msg(enableAdminButton);
    //interactive = false;
    msg.exec();
    if( msg.isConfig() )
      interactive = true;
    if( msg.isHelp() )
    {
      active = false;
      showHelp(this,":/helptext/AutomaticBackup.pdf","AutomaticBackup.pdf");
    }
    if( msg.isAdmin() )
    {
      bool ok = true;
      QString text = "";

#if defined(Q_OS_MAC)
      text = QInputDialog::getText(0,tr("Authorization"),
                                     tr("Enter Administrator Password:"), QLineEdit::Password,
                                     "", &ok);
      if (text.isEmpty())
        ok = false;
#endif
      if( ok )
        getAdminRights(argc,argv,text.toLatin1().data());

      active = false;
      qApp->quit();
    }
    if( msg.isQuit() )
    {
      active = false;
      qApp->quit();
    }
  }

  if( active )
  {
    backupMain main(!enableAdminButton,"");

    updateSettings(main);

    //###qApp->setMainWidget(&main);
    if( interactive )
    {
      main.show();
      main.exec();

      updateSettings(main);
      main.saveConfig("");

      interactive = false;

      if( main.immediateShutdown() )
      {
        // run & shutdown checked and backup(s) executed manually from edit pane
        shutDown();
        active = false;
      }
    }
    else if( !batch )
    {
      main.autoExecute(false);
    }
  }

  updateTooltip();
  startTimer(3600000);

  return active;
}

void backupSplash::executeBackups(bool runningInBackground)
{
  backupMain main(false,"");
  main.autoExecute(runningInBackground);
}

void backupSplash::shutDown()
{
  QString cmd;
  QStringList arguments;

#if defined(Q_OS_WIN32)
  char *systemdir = getenv("SystemRoot");
  cmd = QString(systemdir)+"\\system32\\shutdown.exe";
  arguments << "/s" << "/t" << "0";
#endif
#if defined(Q_OS_MAC)
  cmd = "/bin/sh";
  arguments << "-c" << "osascript -e 'tell application \"Finder\"' -e 'shut down' -e 'end tell'";
#endif

  shutDownMessage msg;
  msg.exec();
  if( !msg.cancelled() )
    QProcess::execute(cmd,arguments);
}

void backupSplash::updateTooltip()
{
  QString nextTime = QDateTime::currentDateTime().addSecs(3600).toString();
  setToolTip(QString("Automatic Backup V")+BACKUP_VERSION+" running.\nNext schedule on "+nextTime);
}

bool backupSplash::keepRunning()
{
  return batch;
}
void backupSplash::closeEvent(QCloseEvent */*event*/)
{
  checkQuit();
  qApp->quit();
}
void backupSplash::checkQuit()
{
  if( shtdwnwarning && !shtdwncontinue )
  {
    quitMessage box("Pending backups are executed only\nif the shutdown is initiated by the\n\"Run & Shutdown\" menu!\n\n");
    box.exec();
    shtdwncontinue = true;
  }
}

void backupSplash::paintEvent(QPaintEvent */*event*/)
{
  QPainter p(this);
  QPixmap pixmap(":/images/save.png");
  p.drawPixmap(0,0,pixmap);
}
void backupSplash::contextMenuEvent(QContextMenuEvent */*event*/)
{
  QMenu menu(this);
  QAction *config = menu.addAction("Config...");
  QAction *quit = menu.addAction("Quit");
  QAction *runNshtdwn = NULL;
  QAction *triggerSchedule = NULL;
  menu.addSeparator();
  if( shutdown )
    runNshtdwn = menu.addAction("Run && Shutdown");
  else if( keepRunning() )
    triggerSchedule = menu.addAction("Run now...");;
  QAction *help = menu.addAction("Help...");

  QAction *result = menu.exec(QCursor::pos());
  if( result==config )
  {
    bool shutDown = false;

    interactive = true;
    hide();
    char *argv[] = { (char*)"",NULL };
    shutDown = !startup(1,argv);
    show();
    backupExecuter::setWindowOnScreen(this,32,32);
    interactive = false;

    // if shutDown is set, startup() has already scheduled the request
    if( shutDown )
    {
      shtdwnwarning = false;
      qApp->quit();
    }

    if( !keepRunning() )
      qApp->quit();
  }
  else if( result==quit )
    qApp->quit();
  else if( result==runNshtdwn )
  {
    shtdwnwarning = false;
    executeBackups(false);
    shutDown();
    qApp->quit();
  }
  else if( result==triggerSchedule )
  {
    timerEvent(NULL);
  }
  else if( result==help )
  {
    showHelp(this,":/helptext/AutomaticBackup.pdf","AutomaticBackup.pdf");
  }
}

void backupSplash::timerEvent(QTimerEvent */*event*/)
{
  if( active && !interactive && !shutdown )
  {
    updateTooltip();
    hide();
    executeBackups(true);
    show();
  }
}

void backupSplash::screenResizedSlot(int /*screen*/)
{
  backupExecuter::setWindowOnScreen(this,32,32);
}

