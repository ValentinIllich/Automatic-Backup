#ifndef	__backupExecuter__
#define	__backupExecuter__

#include "ui_backupwindow.h"
#include "backupEngine.h"
#include "backupDirStruct.h"
#include "backupConfigData.h"

#include <qstring.h>
#include <qstringlist.h>
#include <qtextstream.h>
#include <qdatetime.h>
#include <qfile.h>
#include <qbuffer.h>
#include <qvector.h>
#include <qmap.h>
#include <qmutex.h>
#include <stdlib.h>
#include <map>
#include <string>
#include <qwaitcondition.h>

typedef QVector<quint16> crcSums;

struct crcInfo
{
  time_t lastScan;
  crcSums crc;
};

class backupExecuter : public QWidget, public Ui_backupwindow, public IBackupOperationsInterface
{
  Q_OBJECT
public:
  backupExecuter( backupConfigData &configData );
  virtual ~backupExecuter();

  void startBatchProcessing();
  void stopBatchProcessing();
  void findDirectories( QString const &start = "" );
  void verifyBackup(QString const &startPath = QString::null);

  QString getAutobackupCheckFile(QString const &suffix);
  bool isAutoBackupCreatedFile(QString const &file);

  backupConfigData getConfigData();
  void setConfigData(backupConfigData const &config);

  void	setUnattendedMode(bool doIt);

  static QFile *openFile( QString const &filename, bool readOnly = false );
  static void displayResult( QWidget *parent, QString const &text, QString const windowTitle = "" );
  static void setWindowOnScreen(QWidget *widget,int width,int height);

  static QDateTime getLimitDate(QWidget *parent,QDateTime const &startingDate);

  virtual void processProgressMaximum(int maximum);
  virtual void processProgressValue(int value);
  virtual void processProgressText(QString const &text);
  virtual void processFileNameText(QString const &text);

  virtual void threadedCopyOperation();
  virtual void threadedVerifyOperation();
  virtual void operationFinishedEvent();

public slots:
  virtual void doIt(bool runningInBackground = false);
  virtual void verifyIt(bool runningInBackground = false);
  virtual void processEventsAndWait();
  virtual void cancel();
  virtual void cleanup();
  virtual void screenResizedSlot( int screen );

protected:
  void contextMenuEvent(QContextMenuEvent */*event*/);
  void closeEvent ( QCloseEvent */*event*/ );
  void timerEvent(QTimerEvent */*event*/ );

private:
  struct backupStatistics
  {
    backupStatistics() : count(0), dirkbytes(0), yearcount(0), yearkbytes(0), halfcount(0), halfkbytes(0), quartercount(0),
    quarterkbytes(0), monthcount(0), monthkbytes(0), daycount(0), daykbytes(0) {}
    unsigned count;
    unsigned long dirkbytes;

    unsigned yearcount;
    unsigned long yearkbytes;
    unsigned halfcount;
    unsigned long halfkbytes;
    unsigned quartercount;
    unsigned long quarterkbytes;
    unsigned monthcount;
    unsigned long monthkbytes;
    unsigned daycount;
    unsigned long daykbytes;

    backupStatistics & operator += ( backupStatistics const &a1)
    {
      this->count += a1.count;
      this->dirkbytes += a1.dirkbytes;
      this->yearcount += a1.yearcount;
      this->yearkbytes += a1.yearkbytes;
      this->halfcount += a1.halfcount;
      this->halfkbytes += a1.halfkbytes;
      this->quartercount += a1.quartercount;
      this->quarterkbytes += a1.quarterkbytes;
      this->monthcount += a1.monthcount;
      this->monthkbytes += a1.monthkbytes;
      this->daycount += a1.daycount;
      this->daykbytes += a1.daykbytes;
      return *this;
    }
  };

  backupStatistics getStatistics(QDate const &date,QString const &srcfile,QDate const &filemodified,qint64 const filesize,bool &maybeErased);

  void loadData();
  void saveData();
  void setControlsFromConfigData(backupConfigData &config);
  backupConfigData getConfigDataFromControls();
  void changeVisibility();
  void startingAction();
  void stoppingAction();

  bool updateAutoBackupTime();
  bool updateAutoVerifyTime();

  void analyzeDirectories();
  bool ensureDirExists( QString const &fullPath, QString const &srcBase, QString const &dstBase );
  void copySelectedFiles();
  void restoreDirectory(QString const &startPath);
  void copyFile(QString const &srcFile, QString const &dstFile);

  void deletePath(QString const &absolutePath,QString const &indent = "");
  void scanDirectory(QDate const &date, QString const &startPath = QString::null, bool eraseAll = false);
  void findDuplicates(QString const &startPath = QString::null,bool operatingOnSource = false);

  void deleteFilesFromDestination(QList< QPair<QString,QString> > const &m_toBeRemovedFromToc, unsigned &totalcount, unsigned long &totaldirkbytes);

  QString const &defaultAt( QStringList const &list, int ix );

  void checkTimeout();

  backupConfigData m_config;

  QStringList directories;
  QStringList filelist;
  QStringList dirincludes;
  QStringList fileincludes;
  QStringList direxcludes;
  QStringList fileexcludes;

  QMap<QString,QString> filemap;
  QMap<qint64,QString> crcmap;

  QFile *log;
  QBuffer buff;
  QTextStream stream;
  QString errstream;

  /* is set when backup is executed automatically */
  bool m_bringToFront;
  bool m_isBatch;
  bool m_running;
  bool m_error;
  bool m_dirsCreated;
  bool m_closed;
  bool m_background;
  bool m_diskFull;
  bool m_runUnattended;

  bool collectingDeleted;
  QString collectingPath;

  int dircount;
  unsigned int files_to_copy;
  unsigned int kbytes_to_copy;

  static QString nullStr;
  static QFile fileObj;

  QMap<QString,struct crcInfo> crcSummary;

  backupDirStruct m_dirs;

  quint32 lastVerifiedK;
  quint32 verifiedK;
  bool checksumsChanged;

  QDateTime startTime;

  backupEngine *m_engine;

  QList< QPair<QString,QString> > m_toBeRemovedFromToc;
};

#endif
