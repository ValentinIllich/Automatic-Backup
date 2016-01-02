#include "utilities.h"

#include <qfileinfo.h>
#include <qdatetime.h>
#include <qprocess.h>

#if defined(Q_OS_WIN32)
#include <windows.h>
#include <time.h>

void TimetToFileTime( QDateTime const &timestamp, LPFILETIME pft )
{
    QDateTime modified = timestamp.toUTC();
    SYSTEMTIME st;
    st.wYear = modified.date().year();
    st.wMonth = modified.date().month();
    st.wDay = modified.date().day();
    st.wDayOfWeek = modified.date().dayOfWeek();
    st.wHour = modified.time().hour();
    st.wMinute = modified.time().minute();
    st.wSecond = modified.time().second();
    st.wMilliseconds = modified.time().msec();
    SystemTimeToFileTime(&st,pft);
}
/*BOOL SetFileToCurrentTime(HANDLE hFile)
{
    FILETIME ft;
    SYSTEMTIME st;
    BOOL f;

    GetSystemTime(&st);              // Gets the current system time
    SystemTimeToFileTime(&st, &ft);  // Converts the current system time to file time format
    f = SetFileTime(hFile,           // Sets last-write time of the file
        (LPFILETIME) NULL,           // to the converted current system time
        (LPFILETIME) NULL,
        &ft);

    return f;
}*/

#endif

void setTimestamps( QString const &filename, QDateTime const &modified )
{
#if defined(Q_OS_WIN32)
  // taken from MSDN example

  FILETIME thefiletime = {0,0};
  TimetToFileTime(modified,&thefiletime);
  //get the handle to the file
  WCHAR *_wfilename = new WCHAR[1024];
  filename.toWCharArray(_wfilename);
  _wfilename[filename.length()]=0x0;
  HANDLE filehndl = CreateFile(_wfilename,
                    FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
  //set the filetime on the file
  if( SetFileTime(filehndl,&thefiletime,&thefiletime,&thefiletime)==0 )
      QMessageBox::critical(0,"error","could not set creation time of "+filename+" "+QString::number(GetLastError(),16));
  //close our handle.
  CloseHandle(filehndl);
  delete[] _wfilename;
#elif defined(Q_OS_MAC) || defined(Q_WS_X11)
  QString command = "touch -t " + modified.toString("yyyyMMddhhmm.ss") + " \"" + filename +"\"";
  QProcess::execute(command);
#endif
}

/** this method shows the help */
void showHelp(QObject *parent,QString const &ressourcePath,QString const &helpfile)
{
    dbgout("help...",2);
    QString destfile = QDir::tempPath()+"/"+helpfile;
    QString name;

    QFileInfo fi1(ressourcePath);
    QFileInfo fi2(destfile);
    if( !QFile::exists(destfile) || (fi1.lastModified()>fi2.lastModified()) )
    {
      if( QFile::exists(destfile) && !QFile::remove(destfile) )
          error("could not delete help file. Probably you will see an older version.");
      QFile::copy(ressourcePath,destfile);
    }

    QString webbrowser;
#if defined(Q_OS_WIN32)
    name = QString("file://")+QDir::tempPath()+"/" + helpfile;
    ShellExecuteA(0, 0, name.toLocal8Bit(), 0, 0, SW_SHOWNORMAL);
#endif
#if defined(Q_OS_MAC)
    name = QDir::tempPath()+"/" + helpfile;
    webbrowser = "open";
#endif
#if defined(Q_WS_X11)
    name = QString("file://")+QDir::tempPath()+"/" + helpfile;
    if (isKDERunning())
    {
        webbrowser = "kfmclient";
    }
#endif

    if( !webbrowser.isEmpty() )
    {
        QProcess *proc = new QProcess(parent);
        QObject::connect(proc, SIGNAL(finished(int)), proc, SLOT(deleteLater()));

        QStringList args;
        if (webbrowser == QLatin1String("kfmclient"))
            args.append(QLatin1String("exec"));
        args.append(name);
        proc->start(webbrowser, args);
        proc->waitForFinished(10000);
    }
}

static QPlainTextEdit *window = NULL;
static bool visible = false;
static QString errorMessage;
static int errorLevel = 0;

void error( QString const &text )
{
    QMessageBox::critical(0,"error",text);
    dbgout(text,0);
}

QString getErrorMessage()
{
    return errorMessage;
}

bool dbgVisible(bool showWindow)
{
    if( showWindow )
        visible = true;

    if( visible && window )
    {
        window->show();
        window->activateWindow();
    }

    return visible && (window!=NULL);
}

extern void setDbgWindow(QPlainTextEdit *win,int level)
{
	window = win;
    errorLevel = level;
}

bool dbgout( QString const &text, int level, bool skipLineBreaks )
{
    if( level>errorLevel )
        return true;

    QString message = text;

	if( skipLineBreaks )
		message.remove("\n").remove("\r");
    if( message.startsWith("###") )
        errorMessage += (message+"\n");

    if( window==NULL )
    {
        window = new QPlainTextEdit(0);
        window->setFont(QFont("Courier",12));
        window->setGeometry(50,50,800,300);
        window->setLineWrapMode(QPlainTextEdit::NoWrap);

        if( visible )
        {
            window->show();
            window->activateWindow();
        }
    }

	window->appendPlainText(message);
    QTextCursor newCursor(window->document());
    newCursor.movePosition(QTextCursor::End);
    window->setTextCursor(newCursor);
    newCursor.movePosition(QTextCursor::StartOfLine);
    window->setTextCursor(newCursor);
    qApp->processEvents();

    return true;
}
