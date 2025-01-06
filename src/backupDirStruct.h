#ifndef BACKUPDIRSTRUCT_H
#define BACKUPDIRSTRUCT_H

#include <stdlib.h>
#include <map>
//#include <string>

#include <QString>
#include <QMap>
#include <QList>
#include <QFile>
#include <QDataStream>
#include <QStringList>

struct fileTocEntry
{
  QString m_prefix;
  qint64 m_tocId;
  qint64 m_size;
  qint64 m_modify;
  qint64 m_crc;
};

class dirEntry
{
public:
  dirEntry(dirEntry *parent,QString const &name);

  virtual ~dirEntry();

  void updateDirInfos(qint64 sizeOfFiles,qint64 lastModified);
  QString absoluteFilePath();
  QString relativeFilePath();
  void deleteDir(dirEntry *entry);
  void deleteFile(dirEntry *entry);

  dirEntry *m_parent;
  QString m_name;
  fileTocEntry m_tocData;

  QMap<QString,dirEntry*> m_dirs;
  QList<dirEntry*> m_files;
};

QDataStream &operator<<(QDataStream &out, const struct fileTocEntry &src);
QDataStream &operator>>(QDataStream &in, struct fileTocEntry &dst);

QDataStream &operator<<(QDataStream &out, const std::list<fileTocEntry> &src);
QDataStream &operator>>(QDataStream &in, std::list<fileTocEntry> &dst);

typedef std::list<fileTocEntry> tocDataEntryList;
typedef QMap<QString,tocDataEntryList> tocDataEntryMap;
typedef QMap<QString,tocDataEntryMap> tocDataContainerMap;

class backupDirStruct
{
public:
  backupDirStruct();
  ~backupDirStruct();

  bool readFromFile(QString const &tocSummaryFile);
  bool writeToFile(QString const &tocSummaryFile);

  tocDataContainerMap::iterator getFirstElement();
  tocDataContainerMap::iterator getLastElement();

  bool tocHasChanged();

  int size();
  bool exists(QString const &path, QString const &file);
  qint64 lastModified(QString const &path, QString const &file);
  tocDataEntryList &getEntryList(QString const &path, QString const &file);

  fileTocEntry &getNewestEntry(tocDataEntryMap::iterator const &it);

  qint64 nextTocId();

  void addFile(QString const &path, QString const &file, fileTocEntry &entry);
  void expandFile(QString const &path, QString const &file, QStringList &toBeDeleted);
  void removeFile(QString const &path, QString const &file, QStringList &toBeDeleted);
  void keepFiles( QString const &path, QString const &file, size_t numberOfFiles, QStringList &toBeDeleted );

  static bool convertFromTocFile(QString const &tocSummaryFile, dirEntry *rootEntry, int &dirCount);
  static bool convertToTocFile(QString const &tocSummaryFile, dirEntry *rootEntry);

  static QString createFileNamePrefix(bool keepCopies, bool compressFile);

  static QString addFilenamePrefix(QString const &relPath,QString const &prefix);
  static QString cutFilenamePrefix(QString const &relPath,QString *prefixFound = NULL);

  static QString getTocSummaryFile(QString const &filePath);
  static bool isTocSummaryFile(QString const &filePath);

  static QString getChecksumSummaryFile(QString const &filePath);
  static bool isChecksumSummaryFile(const QString &filePath);

  static bool isSummaryFile(const QString &filePath);

private:
  tocDataContainerMap m_archiveContent;
  qint64 m_nextTocId;
  bool m_tocChanged;
};

#endif // BACKUPDIRSTRUCT_H
