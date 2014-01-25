#ifndef BACKUPSPLASH_H
#define BACKUPSPLASH_H

#include "backupMain.h"

class backupSplash : public QWidget
{
  Q_OBJECT
public:
  backupSplash(QObject */*parent = 0*/);
  virtual ~backupSplash();

  void updateSettings(backupMain const &main);

  bool startup();
  void executeBackups(bool runningInBackground);
  void shutDown();

  void updateTooltip();

  bool keepRunning();
  void checkQuit();

protected:
  void closeEvent(QCloseEvent *event);
  void paintEvent(QPaintEvent */*event*/);
  void contextMenuEvent(QContextMenuEvent */*event*/);
  void timerEvent(QTimerEvent */*event*/);

private slots:
  void screenResizedSlot(int screen);

private:
  bool active;            // no Help, no Quit in startup message
  bool interactive;       // show main dialog
  bool batch;             // running in background with little icon
  bool shutdown;          // running on Close / Quit, shutting down
  bool shtdwnwarning ;    // warn if shutdown is set and app is not closed via menu
  bool shtdwncontinue;    // warning has been showed; proceed with shutdown
};

#endif // BACKUPSPLASH_H
