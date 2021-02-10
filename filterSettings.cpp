#include "filterSettings.h"
#include "ui_filterSettings.h"

filterSettings::filterSettings(QWidget *parent) :
  QDialog(parent),
  ui(new Ui::filterSettings)
{
  ui->setupUi(this);
}

filterSettings::~filterSettings()
{
  delete ui;
}

void filterSettings::setFilters(const QString &filterSettings)
{
  QString dirPatterns;
  QString filePatterns;

  QStringList inclexclPatterns = filterSettings.split("-");
  if( inclexclPatterns.count()==1 )
    // no excludes given
    inclexclPatterns.append("");
  else if( inclexclPatterns.count()!=2 )
    ;//error!

  QString includeTag = inclexclPatterns.at(0);
  QStringList includePatterns = includeTag.replace(")","").split("(");
  if( includePatterns.count()==1 )
    includePatterns.append("");

  QString excludeTag = inclexclPatterns.at(1);
  QStringList excludePatterns = excludeTag.replace(")","").split("(");
  if( excludePatterns.count()==1 )
    excludePatterns.append("");

  QString dirIncludePatterns = includePatterns.at(0);
  QString fileIncludePatterns = includePatterns.at(1);
  QString dirExcludePatterns = excludePatterns.at(0);
  QString fileExcludePatterns = excludePatterns.at(1);

  setupControls(ui->dirIncActive,ui->dirIncludes,dirIncludePatterns);
  setupControls(ui->dirExclActive,ui->dirExcludes,dirExcludePatterns);
  setupControls(ui->fileInclActive,ui->fileIncludes,fileIncludePatterns);
  setupControls(ui->fileExclActive,ui->fileExludes,fileExcludePatterns);
}

QString filterSettings::getFilters()
{
  QString dirIncludePatterns = getFromControls(ui->dirIncActive,ui->dirIncludes);
  QString dirExcludePatterns = getFromControls(ui->dirExclActive,ui->dirExcludes);
  QString fileIncludePatterns = getFromControls(ui->fileInclActive,ui->fileIncludes);
  QString fileExcludePatterns = getFromControls(ui->fileExclActive,ui->fileExludes);

  QString includePatterns = dirIncludePatterns;
  if( !fileIncludePatterns.isEmpty() ) includePatterns += ("(" + fileIncludePatterns + ")");
  QString excluePatterns = dirExcludePatterns;
  if( !fileExcludePatterns.isEmpty() ) excluePatterns += ("(" + fileExcludePatterns + ")");

  QString inclExclPatterns = includePatterns;
  if( !excluePatterns.isEmpty() ) inclExclPatterns += "-" + excluePatterns;

  return inclExclPatterns;
}

void filterSettings::checkChanged(int /*checked*/)
{
  ui->dirIncludes->setEnabled(ui->dirIncActive->isChecked());
  if( ui->dirIncActive->isChecked() && ui->dirIncludes->toPlainText()=="all" )
    ui->dirIncludes->setPlainText("");

  ui->dirExcludes->setEnabled(ui->dirExclActive->isChecked());
  if( ui->dirExclActive->isChecked() && ui->dirExcludes->toPlainText()=="all" )
    ui->dirExcludes->setPlainText("");

  ui->fileIncludes->setEnabled(ui->fileInclActive->isChecked());
  if( ui->fileInclActive->isChecked() && ui->fileIncludes->toPlainText()=="all" )
    ui->fileIncludes->setPlainText("");

  ui->fileExludes->setEnabled(ui->fileExclActive->isChecked());
  if( ui->fileExclActive->isChecked() && ui->fileExludes->toPlainText()=="all" )
    ui->fileExludes->setPlainText("");
}

void filterSettings::setupControls(QCheckBox *box, QPlainTextEdit *text, const QString &settings)
{
  if( !settings.isEmpty() )
  {
    QString converted = settings;
    converted = converted.replace(";","\n");
    box->setChecked(true);
    text->setEnabled(true);
    text->setPlainText(converted);
  }
  else
  {
    box->setChecked(false);
    text->setEnabled(false);
    text->setPlainText("all");
  }
}

QString filterSettings::getFromControls(QCheckBox *box, QPlainTextEdit *text)
{
  if( box->isChecked() )
  {
    QString converted = text->toPlainText();
    converted = converted.replace("\n",";");
    return converted;
  }
  else
  {
    return "";
  }
}
