#ifndef DIALOG_H
#define DIALOG_H

#include <QtGui/QDialog>
#include <qtreewidgetitem>

namespace Ui
{
    class cleanupDialog;
}

class cleanupDialog : public QDialog
{
    Q_OBJECT

public:
    cleanupDialog(QWidget *parent = 0);
    virtual ~cleanupDialog();

    void setPaths(QString const &srcpath,QString const &dstpath);

public slots:
    void doAnalyze();
    void doBreak();
    void analyzePath(QString const &path);

protected:
    virtual void contextMenuEvent ( QContextMenuEvent * e );

private:
    void scanRelativePath( QString const &path, double &Bytes, QTreeWidgetItem *item, bool &isEmpty );
    bool traverseItems(QTreeWidgetItem *startingItem,double &dirSize);
    void openFile( QString const &fn );

    Ui::cleanupDialog *ui;

    QString m_analyzingPath;
    QString m_sourcePath;

    bool m_analyze;
    bool m_run;
    bool m_ignore;
    bool m_cancel;

    int m_depth;
    int m_chars;
    QFontMetrics *m_metrics;
};

#endif // DIALOG_H
