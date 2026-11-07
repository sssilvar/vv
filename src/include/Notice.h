#pragma once

#include <QSize>
#include <QString>
#include <QWidget>
#include <chrono>

class QTimer;
class QVariantAnimation;

enum class NoticeKind {
  Info,  // what an action did, such as where it saved a file
  Error, // why an action did nothing; stays longer
};

/// A short message on a card over the view that fades in, stays a moment and fades out.
/// Every mouse event passes through it to what lies under it, and it moves no other
/// overlay; the owner places it, at sizeHint or narrower, and the text then shortens in
/// the middle.
class Notice : public QWidget {
  friend class UiTests; // measures the text in the font the notice paints it with

public:
  explicit Notice(QWidget* parent = nullptr);

  /// Shows text in place of any notice still showing, for kind's time.
  void post(const QString& text, NoticeKind kind);

  /// How long a notice of kind stays before it fades out: 2 s for Info, 5 s for Error.
  void setDuration(NoticeKind kind, std::chrono::milliseconds duration);

  QString text() const {
    return text_;
  }

  QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  QFont textFont() const;
  void fadeTo(double opacity, std::chrono::milliseconds duration);

  QString text_;
  NoticeKind kind_ = NoticeKind::Info;
  std::chrono::milliseconds infoDuration_{2000};
  std::chrono::milliseconds errorDuration_{5000};
  double opacity_ = 0.0;
  QVariantAnimation* fade_ = nullptr;
  QTimer* hold_ = nullptr;
};
