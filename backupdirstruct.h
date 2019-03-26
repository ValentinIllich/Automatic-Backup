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
  dirEntry(dirEntry *parent,QString const &name)
    : m_parent(parent)
    ,m_name(name)
  {
    m_tocData.m_prefix.clear();
    m_tocData.m_tocId = 0;
    m_tocData.m_size = 0;
    m_tocData.m_modify = 0;
    m_tocData.m_crc = 0;
  }

  virtual ~dirEntry()
  {
    QMap<QString, dirEntry*>::const_iterator it1 = m_dirs.constBegin();
    while (it1 != m_dirs.constEnd())
    {
      delete it1.value();
      ++it1;
    }
    QList<dirEntry*>::const_iterator it2 = m_files.constBegin();
    while (it2 != m_files.constEnd())
    {
      delete *it2;
      ++it2;
    }
  }

  void updateDirInfos(qint64 sizeOfFiles,qint64 lastModified)
  {
    dirEntry *parent = this;
    while( parent )
    {
      parent->m_tocData.m_size += sizeOfFiles;
      if( lastModified>parent->m_tocData.m_modify ) parent->m_tocData.m_modify = lastModified;
      parent = parent->m_parent;
    }
  }
  QString absoluteFilePath()
  {
    QString path = "";

    dirEntry *current = this;
    while( current )
    {
      if( !path.isEmpty() ) path.prepend("/");
      path = current->m_tocData.m_prefix+current->m_name + path;
      current = current->m_parent;
    }

    return path;
  }
  QString relativeFilePath()
  {
    QString path = "";

    dirEntry *current = this;
    while( current->m_parent )
    {
      if( !path.isEmpty() ) path.prepend("/");
      path = current->m_name + path;
      current = current->m_parent;
    }

    return path;
  }
  void deleteDir(dirEntry *entry)
  {
    m_dirs.remove(entry->m_name);
    dirEntry *current = this;
    while( current )
    {
      //current->m_tocData.m_size -= entry->m_tocData.m_size;
      current = current->m_parent;
    }
  }
  void deleteFile(dirEntry *entry)
  {
    m_files.removeAll(entry);
    dirEntry *current = this;
    while( current )
    {
      current->m_tocData.m_size -= entry->m_tocData.m_size;
      current = current->m_parent;
    }
  }

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

class backupDirstruct
{
public:
  backupDirstruct();
  ~backupDirstruct();

  bool readFromFile(QString const &tocSummaryFile)
  {
    QFile tocFile(tocSummaryFile);
    if( QFile::exists(tocSummaryFile) )
    {
      m_nextTocId = 0;

      tocFile.open(QIODevice::ReadOnly);
      QDataStream str(&tocFile);
      str >> m_archiveContent;
      tocFile.close();

      FILE *fp=fopen("/Users/valentinillich/toc.txt","w");

      tocDataContainerMap::iterator it1 = getFirstElement();
      while( it1 != getLastElement() )
      {
        tocDataEntryMap::iterator it2 = it1.value().begin();

        while( it2!=it1.value().end() )
        {
          fprintf(fp,"%s/%s: ",it1.key().toLatin1().data(),it2.key().toLatin1().data());
          tocDataEntryList entries = it2.value();
          tocDataEntryList::iterator it3 = entries.begin();
          while( it3!=entries.end() )
          {
            fprintf(fp,"%s, ",it3->m_prefix.toLatin1().data());
            if( (*it3).m_tocId>=m_nextTocId )
              m_nextTocId = (*it3).m_tocId + 1;
            ++it3;
          }
          fprintf(fp,"\n");
          ++it2;
        }
        ++it1;
      }

      fclose(fp);
      m_tocChanged = false;
      return true;
    }
    return false;
  }
  bool writeToFile(QString const &tocSummaryFile)
  {
    QFile tocFile(tocSummaryFile);
    if( tocFile.open(QIODevice::WriteOnly | QIODevice::Truncate) )
    {
      QDataStream str(&tocFile);
      str << m_archiveContent;
      tocFile.close();
      m_tocChanged = false;
      return true;
    }
    return false;
  }

  tocDataContainerMap::iterator getFirstElement()
  {
    return m_archiveContent.begin();
  }
  tocDataContainerMap::iterator getLastElement()
  {
    return m_archiveContent.end();
  }

  bool tocHasChanged()
  {
      return m_tocChanged;
  }

  int size()
  {
    return m_archiveContent.size();
  }
  bool exists(QString const &path, QString const &file)
  {
    QString prefix = "";
    QString baseFile = backupDirstruct::cutFilenamePrefix(file,&prefix);

    if( path.isEmpty() )
      return m_archiveContent.contains(".") && m_archiveContent["."].contains(baseFile);
    else
      return m_archiveContent.contains(path) && m_archiveContent[path].contains(baseFile);
  }
  qint64 lastModified(QString const &path, QString const &file)
  {
    QString prefix = "";
    QString baseFile = backupDirstruct::cutFilenamePrefix(file,&prefix);

    if( path.isEmpty() )
      return m_archiveContent["."][baseFile].front().m_modify;
    else
      return m_archiveContent[path][baseFile].front().m_modify;
  }
  tocDataEntryList &getEntryList(QString const &path, QString const &file)
  {
    QString prefix = "";
    QString baseFile = backupDirstruct::cutFilenamePrefix(file,&prefix);

    if( path.isEmpty() )
      return m_archiveContent["."][baseFile];
    else
      return m_archiveContent[path][baseFile];
  }

  fileTocEntry &getNewestEntry(tocDataEntryMap::iterator const &it)
  {
    return it.value().front();
  }

  qint64 nextTocId()
  {
    return m_nextTocId++;
  }

  void addFile(QString const &path, QString const &file, fileTocEntry &entry)
  {
    QString basePath = "";
    QString prefix = "";
    QString baseFile = backupDirstruct::cutFilenamePrefix(file,&prefix);

    if( path.isEmpty() )
      basePath = ".";
    else
      basePath = path;

    if( prefix.isEmpty() && entry.m_prefix.isEmpty() )
      m_archiveContent[basePath][baseFile].clear();
    else if( entry.m_prefix.isEmpty() )
      entry.m_prefix = prefix;
    m_archiveContent[basePath][baseFile].push_front(entry);
    m_tocChanged = true;
  }
  void removeFile(QString const &path, QString const &file, QStringList &toBeDeleted)
  {
    QString basePath = "";
    QString prefix = "";
    QString baseFile = backupDirstruct::cutFilenamePrefix(file,&prefix);

    if( path.isEmpty() )
      basePath = ".";
    else
      basePath = path;

    std::list<fileTocEntry> &entries = m_archiveContent[basePath][baseFile];
    std::list<fileTocEntry>::iterator it = entries.begin();
    while( it!=entries.end() )
    {
      QString relPath = path;
      if( !relPath.isEmpty() ) relPath += "/";
      relPath += (*it).m_prefix+file;

      toBeDeleted.push_back(relPath);
      ++it;
    }

    m_archiveContent[basePath].remove(baseFile);
    m_tocChanged = true;
  }
  void keepFiles( QString const &path, QString const &file, size_t numberOfFiles, QStringList &toBeDeleted )
  {
    QString basePath = "";
    QString prefix = "";
    QString baseFile = backupDirstruct::cutFilenamePrefix(file,&prefix);

    if( path.isEmpty() )
      basePath = ".";
    else
      basePath = path;

    std::list<fileTocEntry> &entries = m_archiveContent[basePath][baseFile];

    while( entries.size()>numberOfFiles )
    {
      fileTocEntry entry = entries.back();
      entries.pop_back();

      QString relPath = path;
      if( !relPath.isEmpty() ) relPath += "/";
      relPath += entry.m_prefix+file;

      toBeDeleted.push_back(relPath);
    }
  }

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
