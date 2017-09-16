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

bool backupDirstruct::convertFromTocFile(QString const &tocSummaryFile, dirEntry *rootEntry)
{
  backupDirstruct dirs;

  if( dirs.readFromFile(tocSummaryFile) )
  {
    QMap<QString,QMap<QString,fileTocEntry> >::iterator it1 = dirs.getFirstElement();
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
        ++it;
      }

      qint64 lastModifiedFile = 0;
      qint64 fileSizes = 0;
      //if( !exclusions.contains(it1.key()) )
      {
        QMap<QString,fileTocEntry>::iterator it2 = it1.value().begin();
        while( it2!=it1.value().end() )
        {
          fileTocEntry tocentry;
          tocentry.m_tocId = it2.value().m_tocId;
          tocentry.m_size = it2.value().m_size;
          tocentry.m_modify = it2.value().m_modify;
          tocentry.m_crc = it2.value().m_crc;

          dirEntry *newEntry = new dirEntry(currDir,it2.key());
          newEntry->m_tocData = tocentry;

          currDir->m_files.append(newEntry);

          fileSizes += tocentry.m_size;
          if( tocentry.m_modify>lastModifiedFile ) lastModifiedFile = tocentry.m_modify;
          ++it2;
        }
      }

      currDir->updateDirInfos(fileSizes,lastModifiedFile);

      ++it1;
    }

    return true;
  }

  return false;
}
