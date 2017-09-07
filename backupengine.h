#ifndef BACKUPENGINE
#define BACKUPENGINE

#include <qobject.h>
#include <qmutex.h>

class backupWorker;
class QProgressBar;
class QLabel;

class IBackupOperationsInterface
{
public:
  virtual void threadedCopyOperation() = 0;
  virtual void threadedVerifyOperation() = 0;
  virtual void operationFinishedEvent() = 0;

  virtual void processProgressMaximum(int maximum) = 0;
  virtual void processProgressValue(int value) = 0;
  virtual void processProgressText(QString const &text) = 0;
  virtual void processFileNameText(QString const &text) = 0;
};

class backupEngine : public QObject
{
public:
  backupEngine(IBackupOperationsInterface &backupHandler);
  virtual ~backupEngine();

  void setProgressMaximum(int max);
  void setProgressValue(int value);
  void setProgressText(QString const &text);
  void setFileNameText(QString const &text);
  void setToolTipText(QString const &text);

  void start(bool verifyOnly);

protected:
  virtual void timerEvent(QTimerEvent */*event*/ );

  volatile bool m_changed;
  int m_progressMax;
  int m_progressValue;
  QString m_progressText;
  QString m_filenameText;
  QString m_tooltipText;
  QMutex m_locker;

private:
  IBackupOperationsInterface &m_backupHandler;

  backupWorker *m_worker;
};


#endif // BACKUPENGINE

