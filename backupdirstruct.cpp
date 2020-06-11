#include "backupdirstruct.h"

#include <QStringList>
#include <QDateTime>


dirEntry::dirEntry(dirEntry *parent, const QString &name)
  : m_parent(parent)
  ,m_name(name)
{
  m_tocData.m_prefix.clear();
  m_tocData.m_tocId = 0;
  m_tocData.m_size = 0;
  m_tocData.m_modify = 0;
  m_tocData.m_crc = 0;
}

dirEntry::~dirEntry()
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

void dirEntry::updateDirInfos(qint64 sizeOfFiles, qint64 lastModified)
{
  dirEntry *parent = this;
  while( parent )
  {
    parent->m_tocData.m_size += sizeOfFiles;
    if( lastModified>parent->m_tocData.m_modify ) parent->m_tocData.m_modify = lastModified;
    parent = parent->m_parent;
  }
}

QString dirEntry::absoluteFilePath()
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

QString dirEntry::relativeFilePath()
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

void dirEntry::deleteDir(dirEntry *entry)
{
  m_dirs.remove(entry->m_name);
  dirEntry *current = this;
  while( current )
  {
    //current->m_tocData.m_size -= entry->m_tocData.m_size;
    current = current->m_parent;
  }
}

void dirEntry::deleteFile(dirEntry *entry)
{
  m_files.removeAll(entry);
  dirEntry *current = this;
  while( current )
  {
    current->m_tocData.m_size -= entry->m_tocData.m_size;
    current = current->m_parent;
  }
}

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

backupDirStruct::backupDirStruct()
  : m_nextTocId(0)
  , m_tocChanged(false)
{
}

backupDirStruct::~backupDirStruct()
{
}

bool backupDirStruct::readFromFile(const QString &tocSummaryFile)
{
  m_archiveContent.clear();

  if( QFile::exists(tocSummaryFile) )
  {
    QFile tocFile(tocSummaryFile);
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
        if( fp ) fprintf(fp,"%s/%s: ",it1.key().toLatin1().data(),it2.key().toLatin1().data());
        tocDataEntryList entries = it2.value();
        tocDataEntryList::iterator it3 = entries.begin();
        while( it3!=entries.end() )
        {
          if( fp ) fprintf(fp,"%s, ",it3->m_prefix.toLatin1().data());
          if( (*it3).m_tocId>=m_nextTocId )
            m_nextTocId = (*it3).m_tocId + 1;
          ++it3;
        }
        if( fp ) fprintf(fp,"\n");
        ++it2;
      }
      ++it1;
    }

    if( fp ) fclose(fp);
    m_tocChanged = false;
    return true;
  }
  return false;
}

bool backupDirStruct::writeToFile(const QString &tocSummaryFile)
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

tocDataContainerMap::iterator backupDirStruct::getFirstElement()
{
  return m_archiveContent.begin();
}

tocDataContainerMap::iterator backupDirStruct::getLastElement()
{
  return m_archiveContent.end();
}

bool backupDirStruct::tocHasChanged()
{
  return m_tocChanged;
}

int backupDirStruct::size()
{
  return m_archiveContent.size();
}

bool backupDirStruct::exists(const QString &path, const QString &file)
{
  QString prefix = "";
  QString baseFile = backupDirStruct::cutFilenamePrefix(file,&prefix);

  if( path.isEmpty() )
    return m_archiveContent.contains(".") && m_archiveContent["."].contains(baseFile);
  else
    return m_archiveContent.contains(path) && m_archiveContent[path].contains(baseFile);
}

qint64 backupDirStruct::lastModified(const QString &path, const QString &file)
{
  QString prefix = "";
  QString baseFile = backupDirStruct::cutFilenamePrefix(file,&prefix);

  if( path.isEmpty() )
    return m_archiveContent["."][baseFile].front().m_modify;
  else
    return m_archiveContent[path][baseFile].front().m_modify;
}

tocDataEntryList &backupDirStruct::getEntryList(const QString &path, const QString &file)
{
  QString prefix = "";
  QString baseFile = backupDirStruct::cutFilenamePrefix(file,&prefix);

  if( path.isEmpty() )
    return m_archiveContent["."][baseFile];
  else
    return m_archiveContent[path][baseFile];
}

fileTocEntry &backupDirStruct::getNewestEntry(const tocDataEntryMap::iterator &it)
{
  return it.value().front();
}

qint64 backupDirStruct::nextTocId()
{
  return m_nextTocId++;
}

void backupDirStruct::addFile(const QString &path, const QString &file, fileTocEntry &entry)
{
  QString basePath = "";
  QString prefix = "";
  QString baseFile = backupDirStruct::cutFilenamePrefix(file,&prefix);

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

void backupDirStruct::removeFile(const QString &path, const QString &file, QStringList &toBeDeleted)
{
  QString basePath = "";
  QString prefix = "";
  QString baseFile = backupDirStruct::cutFilenamePrefix(file,&prefix);

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

void backupDirStruct::keepFiles(const QString &path, const QString &file, size_t numberOfFiles, QStringList &toBeDeleted)
{
  QString basePath = "";
  QString prefix = "";
  QString baseFile = backupDirStruct::cutFilenamePrefix(file,&prefix);

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

bool backupDirStruct::convertFromTocFile(QString const &tocSummaryFile, dirEntry *rootEntry, int &dirCount)
{
  backupDirStruct dirs;

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

void iterateDirecories(dirEntry *entry,backupDirStruct &dirs)
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

bool backupDirStruct::convertToTocFile(const QString &tocSummaryFile, dirEntry *rootEntry)
{
  backupDirStruct dirs;
  iterateDirecories(rootEntry,dirs);

  return dirs.writeToFile(tocSummaryFile);
}

QString backupDirStruct::createFileNamePrefix(bool keepCopies, bool compressFile)
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

QString backupDirStruct::addFilenamePrefix(const QString &relPath, const QString &prefix)
{
  QString result = relPath;
  int pos = result.lastIndexOf("/");
  if( pos>=0 )
    result = result.insert(pos+1,prefix);
  else
    result = prefix + result;

  return result;
}

QString backupDirStruct::cutFilenamePrefix(const QString &relPath, QString *prefixFound /*= NULL*/)
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

QString backupDirStruct::getTocSummaryFile(QString const &filePath)
{
  return filePath + "/tocsummary.crcs";
}

bool backupDirStruct::isTocSummaryFile(const QString &filePath)
{
  return filePath.contains("tocsummary.crcs");
}

QString backupDirStruct::getChecksumSummaryFile(QString const &filePath)
{
  return filePath + "/checksumsummary.crcs";
}

bool backupDirStruct::isChecksumSummaryFile(const QString &filePath)
{
  return filePath.contains("checksumsummary.crcs");
}

bool backupDirStruct::isSummaryFile(const QString &filePath)
{
  return filePath.endsWith(".crcs");
}
