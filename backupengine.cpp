#include "backupengine.h"

#include <qapplication.h>

backupEngine::backupEngine(IBackupOperationsInterface &backupHandler,bool verifyOnly)
  : m_backupHandler(backupHandler)
  , m_verifyOnly(verifyOnly)
{

}

backupEngine::~backupEngine()
{

}

void backupEngine::run()
{
  // do something!
  if( m_verifyOnly )
    m_backupHandler.threadedVerifyOperation();
  else
    m_backupHandler.threadedCopyOperation();

  QApplication::postEvent(this,new QEvent(QEvent::User));
}

bool backupEngine::event ( QEvent * event )
{
  if( event->type()!=QEvent::User )
    return false;

  // finish event
  wait();
  deleteLater();
  m_backupHandler.operationFinishedEvent();

  return true;
}
