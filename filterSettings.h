#ifndef FILTERSETTINGS_H
#define FILTERSETTINGS_H

#include <QDialog>

namespace Ui {
class filterSettings;
}

class QCheckBox;
class QPlainTextEdit;

class filterSettings : public QDialog
{
  Q_OBJECT

public:
  explicit filterSettings(QWidget *parent = nullptr);
  ~filterSettings();

  void setFilters(QString const &filterSettings);
  QString getFilters();

public slots:
  virtual void checkChanged(int checked);

private:
  void setupControls(QCheckBox *box,QPlainTextEdit *text,QString const &settings);
  QString getFromControls(QCheckBox *box,QPlainTextEdit *text);

  Ui::filterSettings *ui;
};

#endif // FILTERSETTINGS_H
