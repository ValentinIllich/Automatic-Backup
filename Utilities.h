#ifndef UTILITIES_H
#define UTILITIES_H

#include <qstring.h>
#include <QDir.h>
#include <qfile.h>
#include <qprocess.h>
#include <qplaintextedit.h>
#include <QMessageBox.h>
#include <qapplication.h>

extern void setTimestamps( QString const &filename, QDateTime const &modified );

extern void showHelp(QObject *parent,QString const &ressourcePath,QString const &helpfile);

extern void error( QString const &text );
extern QString getErrorMessage();

extern void setDbgWindow(QPlainTextEdit *win,int level = 1);
extern bool dbgVisible(bool showWindow = false);
extern bool dbgout(QString const &text,int level,bool skipLineBreaks = false);

#endif // UTILITIES_H
