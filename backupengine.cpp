#include "backupengine.h"

#include <qapplication.h>
#include <qthread.h>
#include <qmutex.h>
//#include <qprogressbar.h>
//#include <qlabel.h>

class backupWorker : public QThread
{
public:
  backupWorker(IBackupOperationsInterface &backupHandler,bool verifyOnly)
    : m_backupHandler(backupHandler)
    , m_verifyOnly(verifyOnly)
  {
  }

protected:
  virtual void run()
  {
    // do something!
    if( m_verifyOnly )
      m_backupHandler.threadedVerifyOperation();
    else
      m_backupHandler.threadedCopyOperation();

    QApplication::postEvent(this,new QEvent(QEvent::User));
  }

  virtual bool event ( QEvent* event )
  {
    if( event->type()!=QEvent::User )
      return false;

    // finish event
    wait();
    deleteLater();
    m_backupHandler.operationFinishedEvent();

    return true;
  }

private:
  IBackupOperationsInterface &m_backupHandler;
  bool m_verifyOnly;
};

backupEngine::backupEngine(IBackupOperationsInterface &backupHandler)
  : m_changed(false)
  , m_progressMax(0)
  , m_progressValue(0)
  , m_locker()
  , m_backupHandler(backupHandler)
  , m_worker(NULL)
{
  startTimer(100);
}

backupEngine::~backupEngine()
{
}

void backupEngine::timerEvent(QTimerEvent *)
{
  if (!m_changed)
    return;

  m_locker.lock();
  m_backupHandler.processProgressMaximum(m_progressMax);
  m_backupHandler.processProgressValue(m_progressValue);
  m_backupHandler.processProgressText(m_progressText);
  m_backupHandler.processFileNameText(m_filenameText);
  //setToolTip(m_tooltipText);
  m_locker.unlock();

  m_changed = false;
}

void backupEngine::start(bool verifyOnly)
{
  m_worker = new backupWorker(m_backupHandler,verifyOnly);
  m_worker->start();
}

void backupEngine::setProgressMaximum(int max)
{
  m_locker.lock();
  m_progressMax = max;
  m_changed = true;
  m_locker.unlock();
}

void backupEngine::setProgressValue(int value)
{
  m_locker.lock();
  m_progressValue = value;
  m_changed = true;
  m_locker.unlock();
}

void backupEngine::setProgressText(const QString &text)
{
  m_locker.lock();
  m_progressText = text;
  m_changed = true;
  m_locker.unlock();
}

void backupEngine::setFileNameText(const QString &text)
{
  m_locker.lock();
  m_filenameText = text;
  m_changed = true;
  m_locker.unlock();
}

void backupEngine::setToolTipText(const QString &text)
{
  m_locker.lock();
  m_tooltipText = text;
  m_changed = true;
  m_locker.unlock();
}
