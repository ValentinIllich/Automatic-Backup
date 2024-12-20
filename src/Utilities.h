#ifndef UTILITIES_H
#define UTILITIES_H

#include <qstring.h>
#include <qdir.h>
#include <qfile.h>
#include <qprocess.h>
#include <qplaintextedit.h>
#include <qmessagebox.h>
#include <qapplication.h>

struct discInfo
{
  int m_capacity;
  quint64 m_availableBytes;
  quint64 m_freeBytes;
};

discInfo getDiscInfo( QString const &filename );
extern void setTimestamps( QString const &filename, QDateTime const &modified, QTextStream &errors );

extern void showHelp(QObject *parent,QString const &ressourcePath,QString const &helpfile);

extern void error( QString const &text );
extern QString getErrorMessage();

extern void setDbgWindow(QPlainTextEdit *win,int level = 1);
extern bool dbgVisible(bool showWindow = false);
extern bool dbgout(QString const &text,int level,bool skipLineBreaks = false);

#endif // UTILITIES_H
