#pragma once

#include <QString>
#include <QWidget>
#include <cstddef>

class QLabel;

enum class PanelCaption { Filename, Badge };

// Mesh identity above the viewport, without intercepting camera input.
class PanelLabel : public QWidget {
public:
  PanelLabel(size_t file, const QString& name, QWidget* parent);
  void setCaption(PanelCaption caption);
  void setStatus(const QString& status);

protected:
  void resizeEvent(QResizeEvent* event) override;

private:
  QLabel* badge_;
  QLabel* name_;
  QLabel* status_;
  QString filename_;
  PanelCaption caption_ = PanelCaption::Filename;
  void layoutCaption();
};
