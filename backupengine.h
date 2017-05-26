#ifndef BACKUPENGINE
#define BACKUPENGINE

#include <qthread.h>

class IBackupOperationsInterface
{
public:
  virtual void threadedCopyOperation() = 0;
  virtual void threadedVerifyOperation() = 0;
  virtual void operationFinishedEvent() = 0;
};

class backupEngine : public QThread
{
public:
  backupEngine(IBackupOperationsInterface &backupHandler,bool verifyOnly);
  virtual ~backupEngine();

protected:
  virtual void run();
  virtual bool event ( QEvent * event );

private:
  IBackupOperationsInterface &m_backupHandler;
  bool m_verifyOnly;
};


#endif // BACKUPENGINE

