#include "backupdirstruct.h"

#include <QStringList>
#include <QDateTime>

QDataStream &operator<<(QDataStream &out, const struct fileTocEntry &src)
{
  out << src.m_prefix << src.m_tocId << src.m_size << src.m_modify << src.m_crc;
  return out;
}
QDataStream &operator>>(QDataStream &in, struct fileTocEntry &dst)
{
  in >> dst.m_prefix;
  in >> dst.m_tocId;
  in >> dst.m_size;
  in >> dst.m_modify;
  in >> dst.m_crc;
  return in;
}

QDataStream &operator<<(QDataStream &out, const std::list<fileTocEntry> &src)
{
  std::list<fileTocEntry>::const_iterator it = src.begin();
  out << (qint64)src.size();
  while( it!=src.end() )
  {
    out << *it;
    ++it;
  }
  return out;
}

QDataStream &operator>>(QDataStream &in, std::list<fileTocEntry> &dst)
{
  qint64 size;

  in >> size;
  for( qint64 i = 0; i<size; i++ )
  {
    struct fileTocEntry entry;
    in >> entry.m_prefix;
    in >> entry.m_tocId;
    in >> entry.m_size;
    in >> entry.m_modify;
    in >> entry.m_crc;
    dst.push_back(entry);
  }

  return in;
}

backupDirstruct::backupDirstruct()
  : m_nextTocId(0)

{
}

backupDirstruct::~backupDirstruct()
{
}

bool backupDirstruct::convertFromTocFile(QString const &tocSummaryFile, dirEntry *rootEntry, int &dirCount)
{
  backupDirstruct dirs;

  if( dirs.readFromFile(tocSummaryFile) )
  {
    tocDataContainerMap::iterator it1 = dirs.getFirstElement();
    while( it1 != dirs.getLastElement() )
    {
      QString fullPath = it1.key();
      //m_engine->setProgressText(fullPath);

      if( fullPath.startsWith("/") ) fullPath = fullPath.mid(1);
      QStringList paths = fullPath.split("/");

      QStringList::iterator it = paths.begin();
      dirEntry *currDir = rootEntry;
      while ( it!=paths.end() )
      {
        QString currentPath = *it;
        if( currentPath!="." )
        {
          if( currDir->m_dirs.contains(currentPath) )
            currDir = currDir->m_dirs[currentPath];
          else
          {
            dirEntry *newEntry = new dirEntry(currDir,currentPath);
            currDir->m_dirs[currentPath] = newEntry;
            currDir = newEntry;
          }
        }
        ++it;
      }

      qint64 lastModifiedFile = 0;
      qint64 fileSizes = 0;
      //if( !exclusions.contains(it1.key()) )
      {
        tocDataEntryMap::iterator it2 = it1.value().begin();
        while( it2!=it1.value().end() )
        {
          tocDataEntryList entries = it2.value();
          tocDataEntryList::iterator it3 = entries.begin();
          while( it3!=entries.end() )
          {
            fileTocEntry tocentry = (*it3);

            dirEntry *newEntry = new dirEntry(currDir,it2.key());
            newEntry->m_tocData = tocentry;

            currDir->m_files.append(newEntry);

            fileSizes += tocentry.m_size;
            if( tocentry.m_modify>lastModifiedFile ) lastModifiedFile = tocentry.m_modify;
            ++it3;
          }
          ++it2;
        }
      }

      currDir->updateDirInfos(fileSizes,lastModifiedFile);

      dirCount++;

      ++it1;
    }

    return true;
  }

  return false;
}

void iterateDirecories(dirEntry *entry,backupDirstruct &dirs)
{
  QMap<QString,dirEntry*>::iterator it1 = entry->m_dirs.begin();
  while( it1!=entry->m_dirs.end() )
  {
    iterateDirecories(it1.value(),dirs);
    ++it1;
  }

  QList<dirEntry*>::iterator it2 = entry->m_files.begin();
  while( it2!=entry->m_files.end() )
  {
    fileTocEntry toc = (*it2)->m_tocData;
    dirs.addFile(entry->relativeFilePath(),toc.m_prefix+
                 (*it2)->m_name,toc);
    ++it2;
  }
}

bool backupDirstruct::convertToTocFile(const QString &tocSummaryFile, dirEntry *rootEntry)
{
  backupDirstruct dirs;
  iterateDirecories(rootEntry,dirs);

  return dirs.writeToFile(tocSummaryFile);
}

QString backupDirstruct::createFileNamePrefix(bool keepCopies, bool compressFile)
{
  QString prefix;

  QDateTime dt = QDateTime::currentDateTime();
  QString appendixcpy = dt.toString("yyyyMMddhhmmss");

  if( keepCopies ) prefix = "_#" + appendixcpy;
  if( compressFile ) prefix = "_@" + prefix;

  if( !prefix.isEmpty() )
    prefix += ".";

  return prefix;
}

QString backupDirstruct::addFilenamePrefix(const QString &relPath, const QString &prefix)
{
  QString result = relPath;
  int pos = result.lastIndexOf("/");
  if( pos>=0 )
    result = result.insert(pos+1,prefix);
  else
    result = prefix + result;

  return result;
}

QString backupDirstruct::cutFilenamePrefix(const QString &relPath, QString *prefixFound /*= NULL*/)
{
  QString result = relPath;
  QString name = "";
  int prefixlen = 0;
  int pos = result.lastIndexOf("/");
  if( pos>=0 )
    name = result.mid(pos+1);
  else
    name = relPath;

  if( name.startsWith("_@") )
  {
    name = name.mid(2);
    prefixlen += 2;
  }
  if( name.startsWith("_#") )
  {
    name = name.mid(16);
    prefixlen += 16;
  }
  if( prefixlen>0 )
  {
    if( prefixFound )
      *prefixFound = result.mid(pos+1,prefixlen+1);
    result.remove(pos+1,prefixlen+1);
  }

  return result;
}

QString backupDirstruct::getTocSummaryFile(QString const &filePath)
{
  return filePath + "/tocsummary.crcs";
}

bool backupDirstruct::isTocSummaryFile(const QString &filePath)
{
  return filePath.contains("tocsummary.crcs");
}

QString backupDirstruct::getChecksumSummaryFile(QString const &filePath)
{
  return filePath + "/checksumsummary.crcs";
}

bool backupDirstruct::isChecksumSummaryFile(const QString &filePath)
{
  return filePath.contains("checksumsummary.crcs");
}
