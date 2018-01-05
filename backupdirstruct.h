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
      path = current->m_name + path;
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

      tocDataContainerMap::iterator it1 = getFirstElement();
      while( it1 != getLastElement() )
      {
        tocDataEntryMap::iterator it2 = it1.value().begin();

        while( it2!=it1.value().end() )
        {
          if( getEntry(it2).m_tocId>=m_nextTocId )
            m_nextTocId = getEntry(it2).m_tocId + 1;
          ++it2;
        }
        ++it1;
      }

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

  int size()
  {
    return m_archiveContent.size();
  }
  bool exists(QString const &path, QString const &file)
  {
    if( path.isEmpty() )
      return m_archiveContent.contains(".") && m_archiveContent["."].contains(file);
    else
      return m_archiveContent.contains(path) && m_archiveContent[path].contains(file);
  }
  qint64 lastModified(QString const &path, QString const &file)
  {
    if( path.isEmpty() )
      return m_archiveContent["."][file].front().m_modify;
    else
      return m_archiveContent[path][file].front().m_modify;
  }

  fileTocEntry &getEntry(tocDataEntryMap::iterator const &it)
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

    if( prefix.isEmpty() )
      m_archiveContent[basePath][baseFile].clear();
    m_archiveContent[basePath][baseFile].push_front(entry);
  }
  void removeFile(QString const &path, QString const &file)
  {
    if( path.isEmpty() )
      m_archiveContent["."].remove(file);
    else
      m_archiveContent[path].remove(file);
  }
  void keepFiles( size_t numberOfFiles, QStringList &toBeDeleted )
  {
    tocDataContainerMap::iterator it1 = m_archiveContent.begin();
    while( it1!=m_archiveContent.end() )
    {
      tocDataEntryMap::iterator it2 = it1.value().begin();
      while( it2!=it1.value().end() )
      {
        std::list<fileTocEntry> &entries = it2.value();

        while( entries.size()>numberOfFiles )
        {
          fileTocEntry entry = entries.back();
          entries.pop_back();

          QString relPath = it1.key();
          if( !relPath.isEmpty() ) relPath += "/";
          relPath += entry.m_prefix+it2.key();

          toBeDeleted.push_back(relPath);
        }
        ++it2;
      }
      ++it1;
    }

  }

  static bool convertFromTocFile(QString const &tocSummaryFile, dirEntry *rootEntry, int &dirCount);
  static bool convertToTocFile(QString const &tocSummaryFile, dirEntry *rootEntry);

  static QString createFileNamePrefix(bool keepCopies, bool compressFile);

  static QString addFilenamePrefix(QString const &relPath,QString const &prefix);
  static QString cutFilenamePrefix(QString const &relPath,QString *prefixFound = NULL);

private:
  tocDataContainerMap m_archiveContent;
  qint64 m_nextTocId;
};

#endif // BACKUPDIRSTRUCT_H
