#pragma once

#include <QWidget>
#include <QString>

class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;

namespace charging::client {

// Local demo album. Selecting a picture never starts a business order.
class PhotoAlbumPage final : public QWidget {
    Q_OBJECT
public:
    explicit PhotoAlbumPage(QWidget *parent = nullptr, const QString &directory = {});
    void reload();
signals:
    void imageSelected(const QString &path);
    void backRequested();
protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void layoutGrid();
    void updateSelection();
    void showPreview();
    void updatePreview();
    QString directory_;
    QStackedWidget *pages_;
    QListWidget *photos_;
    QLabel *count_, *selection_, *empty_, *preview_;
    QPushButton *confirm_, *previewButton_, *back_;
};

} // namespace charging::client
