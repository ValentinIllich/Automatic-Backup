#ifndef BACKUPCONFIGDATA_H
#define BACKUPCONFIGDATA_H

struct backupConfigData
{
  backupConfigData()
  {
    m_sName = "";
    m_sSrc = "";
    m_sDst = "";
    m_sFlt = "";
    m_bAuto = false;
    m_iInterval = 0;
    m_bKeep = false;
    m_iVersions = 0;
    m_bVerify = false;
    m_bsuspend = false;
    m_iTimeout = 0;
    m_bBackground = false;
    m_iLastModify = 0;
    m_bVerbose = false;
    m_bVerboseMaint = false;
    m_bCollectDeleted = false;
    m_bFindSrcDupl = false;
    m_bScanDestPath = false;
  }
  void getFromString(QString const &string)
  {
    QStringList list = string.split("\t");

    m_sName = list.at(0);
    m_sSrc = list.at(1);
    m_sDst = list.at(2);
    m_sFlt = list.at(3);
    m_bAuto = list.at(4)=="1";
    m_iInterval = (list.at(5)).toInt();
    m_bKeep = list.count()>6 ? list.at(6)=="1" : false;
    m_iVersions = list.count()>7 ? (list.at(7)).toInt() : 2;
    m_bVerify = list.count()>8 ? list.at(8)=="1" : true;
    m_bsuspend = list.count()>9 ? list.at(9)=="1" : false;
    m_iTimeout = list.count()>10 ? list.at(10).toInt() : 3;
    m_bBackground = list.count()>11 ? list.at(11)=="1" : false;
    m_iLastModify = list.count()>12 ? list.at(12).toInt() : 0;
  }
  QString putToString()
  {
    QString result = m_sName + "\t"
    + m_sSrc + "\t"
    + m_sDst + "\t"
    + m_sFlt + "\t"
    + (m_bAuto ? "1":"0") + "\t"
    + QString::number(m_iInterval) + "\t"
    + (m_bKeep  ? "1":"0") + "\t"
    + QString::number(m_iVersions) + "\t"
    + (m_bVerify  ? "1":"0") + "\t"
    + (m_bsuspend  ? "1":"0") + "\t"
    + QString::number(m_iTimeout) + "\t"
    + (m_bBackground ? "1" : "0") + "\t"
    + QString::number(m_iLastModify) + "\n";

    return result;
  }

  QString   m_sName;
  QString		m_sSrc;
  QString		m_sDst;
  QString		m_sFlt;
  bool			m_bAuto;
  int				m_iInterval;
  bool			m_bKeep;
  int				m_iVersions;
  bool			m_bVerify;
  bool      m_bsuspend;
  int       m_iTimeout;
  bool			m_bBackground;
  int       m_iLastModify;
  // non permanent settings
  bool      m_bVerbose;
  bool      m_bVerboseMaint;
  bool      m_bCollectDeleted;
  bool      m_bFindSrcDupl;
  bool      m_bScanDestPath;
};


#endif // BACKUPCONFIGDATA_H
