#ifndef	__backupMain__
#define	__backupMain__

#include "ui_mainwindow.h"

#define	BACKUP_VERSION	 "1.21"

class backupMain : public QDialog, public Ui_mainwindow
{
  Q_OBJECT
public:
  backupMain(bool runsAsAdmin, QString const &configfile);
  virtual ~backupMain();

  void saveConfig(QString const &configFile = QString::null);
  void autoExecute(bool runningInBackground);
  bool keepRunning() const;
  bool runAndShutdown() const;
  bool immediateShutdown() const;

public slots:
  virtual void newBackup();
  virtual void quit();
  virtual void editBackup();
  virtual void deleteBackup();
  virtual void checkSelection();
  virtual void cleanupBackup();
  virtual void aboutButton();

private:
  int m_selected;
  bool m_immediateShutdown;
};

#endif
