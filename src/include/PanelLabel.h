#pragma once

#include <QString>
#include <QWidget>
#include <cstddef>

class QLabel;

// A file's letter badge and name over its pane, without intercepting camera input.
class PanelLabel : public QWidget {
public:
  PanelLabel(size_t file, const QString& name, QWidget* parent);

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  QLabel* badge_;
  QLabel* name_;
  QString filename_;
};
