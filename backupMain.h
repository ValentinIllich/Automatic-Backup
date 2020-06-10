#ifndef	__backupMain__
#define	__backupMain__

#include "ui_mainwindow.h"
#include "ressources/version.inc"
#include "backupConfigData.h"

class backupMain : public QDialog, public Ui_mainwindow
{
  Q_OBJECT
public:
  backupMain(bool runsAsAdmin, QString const &configfile);
  virtual ~backupMain();

  void connectSignalSlots();

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
  virtual void cleanupDirectory();
  virtual void cleanupBackup();
  virtual void aboutButton();
  virtual void selSource();
  virtual void selDest();
  virtual void getChangedConfigData();

  void enableControls(bool enabled);
  void setControlsFromConfigData(backupConfigData &config);
  backupConfigData getConfigDataFromControls();

private:
  int m_selected;
  bool m_immediateShutdown;
  bool m_isUpdatingConfig;
};

#endif
