#ifndef	__backupExecuter__
#define	__backupExecuter__

#include "ui_backupwindow.h"

#include <qstring.h>
#include <qstringlist.h>
#include <qtextstream.h>
#include <qfile.h>
#include <qbuffer.h>
#include <qvector.h>
#include <qmap.h>

#include <stdlib.h>

typedef QVector<quint16> crcSums;

struct crcInfo
{
  time_t lastScan;
  crcSums crc;
};

class backupExecuter : public QDialog, public Ui_backupwindow
{
  Q_OBJECT
public:
  backupExecuter( QString const &name, QString const &src, QString const &dst,
                  QString const &flt, bool automatic, int repeat,
                  bool keepVers, int versions, bool zlib,
                  bool suspend, int breakafter);
  virtual ~backupExecuter();

  void startBatchProcessing();
  void stopBatchProcessing();
  void findDirectories( QString const &start = "" );
  void verifyBackup(QString const &startPath = QString::null);

  QString getTitle();
  QString getSrc();
  QString getDst();
  QString getFlt();
  bool	getAuto();
  int		getInterval();
  bool  getBackground();
  bool	getCompress();
  bool	getKeep();
  int		getVersions();
  bool	getSuspend();
  int		getTimeout();

  void	setCloseAfterExecute(bool doIt);

  static QFile *openFile( QString const &filename, bool readOnly = false );
  static void displayResult( QWidget *parent, QString const &text, QString const windowTitle = "" );
  static void setWindowOnScreen(QWidget *widget,int width,int height);

public slots:
  virtual void selSource();
  virtual void selDest();
  virtual void selAuto();
  virtual void doIt(bool runningInBackground = false);
  virtual void verifyIt(bool runningInBackground = false);
  virtual void restoreDir();
  virtual void restoreFile();
  virtual void cancel();
  virtual void cleanup();
  virtual void help();
  virtual void screenResizedSlot( int screen );

protected:
  void contextMenuEvent(QContextMenuEvent */*event*/);
  void closeEvent ( QCloseEvent */*event*/ );

private:
  void changeVisibility();
  void startingAction();
  void stoppingAction();
  void analyzeDirectories();
  QString ensureDirExists( QString const &fullPath, QString const &srcBase, QString const &dstBase );
  void copySelectedFiles();
  void restoreDirectory(QString const &startPath);
  void copyFile(QString const &srcFile, QString const &dstFile);

  void deletePath(QString const &absolutePath,QString const &indent = "");
  void scanDirectory(QDate const &date, QString const &startPath = QString::null, bool eraseAll = false);
  void findDuplicates(QString const &startPath = QString::null,bool operatingOnSource = false);

  QString const &defaultAt( QStringList const &list, int ix );

  void checkTimeout();

  QString source;
  QString destination;
  QString filter;
  QString appendixcpy;

  QStringList directories;
  QStringList filelist;
  QStringList dirincludes;
  QStringList fileincludes;
  QStringList direxcludes;
  QStringList fileexcludes;

  QMap<QString,QString> filemap;

  QFile *log;
  QBuffer buff;
  QTextStream stream;
  QString errstream;

  /* is set when backup is executed automatically */
  bool m_autoStart;
  bool m_isBatch;
  bool m_running;
  bool m_error;
  bool m_dirsCreated;
  bool m_closed;
  bool m_background;
  bool m_closeAfterExecute;

  bool collectingDeleted;
  QString collectingPath;

  int dircount;
  unsigned int kbytes_to_copy;

  static QString nullStr;
  static QFile fileObj;

  QMap<QString,struct crcInfo> crcSummary;
  quint32 lastVerifiedK;
  quint32 verifiedK;
  bool checksumsChanged;

  QDateTime startTime;
};

#endif
