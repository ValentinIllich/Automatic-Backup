#include <qapplication.h>
#include <qmessagebox.h>
#include <qdialog.h>
#include <qdatetime.h>
#include <qdesktopwidget.h>
#include <qsettings.h>
#include <qinputdialog.h>

#include "backupSplash.h"
#include "backupMain.h"
#include "backupExecuter.h"
#include "utilities.h"

int main( int argc, char **argv )
{
  QCoreApplication::setOrganizationName("VISolutions.de");
  QCoreApplication::setApplicationName("Automatic Backup");

  QApplication app(argc,argv);
  backupSplash object(0);

  //object.setWindowTitle("backup");
  //object.move(qApp->desktop()->width(),qApp->desktop()->height());
  if( object.startup(argc,argv) )
  {
    if( object.keepRunning() )
    {
      backupExecuter::setWindowOnScreen(&object,32,32);
      object.show();
      app.setQuitOnLastWindowClosed(false);
      app.exec();

      object.checkQuit();
    }
  }

  return 0;
}
