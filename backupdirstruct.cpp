#include "backupdirstruct.h"

#include <QStringList>

QDataStream &operator<<(QDataStream &out, const struct fileTocEntry &src)
{
  out << src.m_tocId << src.m_size << src.m_modify << src.m_crc;
  return out;
}
QDataStream &operator>>(QDataStream &in, struct fileTocEntry &dst)
{
  in >> dst.m_tocId;
  in >> dst.m_size;
  in >> dst.m_modify;
  in >> dst.m_crc;
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
            newEntry->m_tocData.m_tocId = 0;
            newEntry->m_tocData.m_size = 0;
            newEntry->m_tocData.m_modify = 0;
            newEntry->m_tocData.m_crc = 0;

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
          fileTocEntry tocentry = dirs.getEntry(it2);

          dirEntry *newEntry = new dirEntry(currDir,it2.key());
          newEntry->m_tocData = tocentry;

          currDir->m_files.append(newEntry);

          fileSizes += tocentry.m_size;
          if( tocentry.m_modify>lastModifiedFile ) lastModifiedFile = tocentry.m_modify;
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
    dirs.addFile(entry->relativeFilePath(),(*it2)->m_name,toc);
    ++it2;
  }
}

bool backupDirstruct::convertToTocFile(const QString &tocSummaryFile, dirEntry *rootEntry)
{
  backupDirstruct dirs;
  iterateDirecories(rootEntry,dirs);

  return dirs.writeToFile(tocSummaryFile);
}
