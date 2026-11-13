// 横屏音乐页：左侧唱片，右侧曲目信息/播放控制，列表在本页内切换。
// 不使用顶层弹窗，避免 linuxfb 下弹窗焦点和触摸坐标问题。
#include "media_page.h"
#include "audio_player.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSlider>
#include <QStackedLayout>
#include <QVBoxLayout>
#include <QWidget>

namespace {
// 长歌名按实际宽度省略，防止把右侧布局和屏幕撑宽。
class ElidedLabel : public QLabel
{
public:
    explicit ElidedLabel(QWidget* parent = nullptr) : QLabel(parent)
    {
        setTextFormat(Qt::PlainText);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }
    void setFullText(const QString& text) { full_ = text; updateText(); }
protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        updateText();
    }
private:
    void updateText()
    {
        setText(fontMetrics().elidedText(full_, Qt::ElideRight, qMax(0, width())));
    }
    QString full_;
};

// 自绘图标不依赖系统符号字体，在板端与开发机上形状一致。
enum class Symbol { Previous, Play, Pause, Next };
class TransportButton : public QPushButton
{
public:
    explicit TransportButton(Symbol symbol, QWidget* parent = nullptr)
        : QPushButton(parent), symbol_(symbol)
    {
        setFixedSize(symbol == Symbol::Play ? 68 : 56,
                     symbol == Symbol::Play ? 68 : 56);
        setFocusPolicy(Qt::NoFocus);
    }
    void setSymbol(Symbol symbol) { symbol_ = symbol; update(); }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool center = symbol_ == Symbol::Play || symbol_ == Symbol::Pause;
        painter.setPen(Qt::NoPen);
        if (center) {
            painter.setBrush(isDown() ? QColor("#c7d7e2") : QColor("#f5f7f8"));
            painter.drawEllipse(rect().adjusted(1, 1, -1, -1));
        }
        painter.translate(width() / 2.0, height() / 2.0);
        painter.setBrush(!isEnabled() ? QColor("#555555") :
                         center ? QColor("#101214") : QColor("#f5f7f8"));
        if (symbol_ == Symbol::Pause) {
            painter.drawRoundedRect(QRectF(-9, -11, 6, 22), 1, 1);
            painter.drawRoundedRect(QRectF(3, -11, 6, 22), 1, 1);
        } else {
            if (symbol_ == Symbol::Previous) painter.scale(-1, 1);
            QPainterPath triangle;
            triangle.moveTo(-7, -11);
            triangle.lineTo(10, 0);
            triangle.lineTo(-7, 11);
            triangle.closeSubpath();
            painter.drawPath(triangle);
            if (!center) painter.drawRoundedRect(QRectF(11, -11, 3, 22), 1, 1);
        }
    }
private:
    Symbol symbol_;
};

// 圆形唱片裁剪同名封面；无封面时绘制默认山景，绝不假装是歌曲真实封面。
class RecordCover : public QWidget
{
public:
    explicit RecordCover(QWidget* parent = nullptr) : QWidget(parent)
    {
        // 720×480 横屏的左栏留出248px唱片，不被布局压成小图。
        setFixedSize(248, 248);
    }
    void setSong(const QString& path)
    {
        cover_ = QPixmap();
        const QFileInfo file(path);
        const QString base = file.absolutePath() + "/" + file.completeBaseName();
        const QStringList candidates = {
            base + ".jpg", base + ".png", base + ".jpeg",
            file.absolutePath() + "/cover.jpg",
            file.absolutePath() + "/cover.png",
            file.absolutePath() + "/cover.jpeg"
        };
        for (const QString& candidate : candidates) {
            if (cover_.load(candidate)) break;
        }
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const qreal size = qMin(width(), height()) - 4;
        const QRectF disc((width() - size) / 2, (height() - size) / 2, size, size);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#1e252b"));
        p.drawEllipse(disc);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor("#2b3338"), 1));
        for (int inset = 6; inset < size * 0.15; inset += 4)
            p.drawEllipse(disc.adjusted(inset, inset, -inset, -inset));

        const QRectF art = disc.adjusted(size * 0.16, size * 0.16,
                                        -size * 0.16, -size * 0.16);
        QPainterPath clip;
        clip.addEllipse(art);
        p.setClipPath(clip);
        if (!cover_.isNull()) {
            const QPixmap scaled = cover_.scaled(art.size().toSize(),
                Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            p.drawPixmap(art.topLeft() + QPointF((art.width() - scaled.width()) / 2,
                         (art.height() - scaled.height()) / 2), scaled);
        } else {
            QLinearGradient sky(art.topLeft(), art.bottomLeft());
            sky.setColorAt(0, QColor("#647f94"));
            sky.setColorAt(0.58, QColor("#dbb48e"));
            sky.setColorAt(1, QColor("#253d49"));
            p.fillRect(art, sky);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#f4db9e"));
            p.drawEllipse(QPointF(art.left() + art.width() * 0.7,
                                 art.top() + art.height() * 0.3),
                          art.width() * 0.07, art.width() * 0.07);
            QPainterPath mountain;
            mountain.moveTo(art.left(), art.bottom());
            mountain.lineTo(art.left(), art.top() + art.height() * 0.68);
            mountain.lineTo(art.left() + art.width() * 0.35, art.top() + art.height() * 0.5);
            mountain.lineTo(art.right(), art.top() + art.height() * 0.82);
            mountain.lineTo(art.right(), art.bottom());
            mountain.closeSubpath();
            p.fillPath(mountain, QColor("#243b48"));
        }
    }
private:
    QPixmap cover_;
};

QString timeText(qint64 ms)
{
    const qint64 seconds = qMax<qint64>(0, ms / 1000);
    return QStringLiteral("%1:%2").arg(seconds / 60, 2, 10, QLatin1Char('0'))
                                 .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

class MediaPage : public QWidget
{
public:
    explicit MediaPage(AudioPlayer* player, QWidget* parent)
        : QWidget(parent), player_(player)
    {
        setObjectName("musicPage");
        setStyleSheet(
            "#musicPage QLabel { background: transparent; }"
            "#musicPage QPushButton { border: none; background: transparent;"
            " color: #9aa4aa; font-size: 15px; }"
            "#musicPage QPushButton:pressed { color: #3e6ae1; }"
            "#musicPage QPushButton:disabled { color: #555555; }"
            "#musicPage QSlider::groove:horizontal { height: 4px; background: #303b43; }"
            "#musicPage QSlider::sub-page:horizontal { background: #3e6ae1; }"
            "#musicPage QSlider::handle:horizontal { background: #3e6ae1;"
            " width: 16px; margin: -6px 0; border-radius: 8px; }"
            "#musicPage QListWidget { background: #0f1316; color: #f5f7f8;"
            " border: none; font-size: 18px; }"
            "#musicPage QListWidget::item { padding: 12px; }"
            "#musicPage QListWidget::item:selected { background: #252e35; color: #3e6ae1; }");
        stack_ = new QStackedLayout(this);
        stack_->setContentsMargins(0, 0, 0, 0);
        auto* playerPage = new QWidget;
        auto* row = new QHBoxLayout(playerPage);
        row->setContentsMargins(24, 12, 24, 12);
        row->setSpacing(32);
        auto* left = new QVBoxLayout;
        left->addStretch();
        cover_ = new RecordCover;
        left->addWidget(cover_, 0, Qt::AlignHCenter);
        auto* caption = new QLabel(QStringLiteral("本 地 音 乐"));
        caption->setAlignment(Qt::AlignCenter);
        caption->setStyleSheet("font-size: 13px; color: #8e989e;");
        left->addSpacing(16);
        left->addWidget(caption);
        left->addStretch();
        row->addLayout(left, 43);

        auto* right = new QVBoxLayout;
        right->setSpacing(6);
        right->addStretch();
        status_ = new ElidedLabel;
        status_->setFixedHeight(22);
        status_->setStyleSheet("font-size: 13px; color: #9aa4aa;");
        right->addWidget(status_);
        title_ = new ElidedLabel;
        title_->setFixedHeight(44);
        title_->setStyleSheet("font-size: 28px; color: #f5f7f8; font-weight: normal;");
        right->addWidget(title_);
        artist_ = new ElidedLabel;
        artist_->setFixedHeight(26);
        artist_->setStyleSheet("font-size: 16px; color: #9aa4aa;");
        right->addWidget(artist_);
        right->addSpacing(10);

        auto* transport = new QHBoxLayout;
        previous_ = new TransportButton(Symbol::Previous);
        play_ = new TransportButton(Symbol::Play);
        next_ = new TransportButton(Symbol::Next);
        previous_->setAccessibleName(QStringLiteral("上一首"));
        play_->setAccessibleName(QStringLiteral("播放"));
        next_->setAccessibleName(QStringLiteral("下一首"));
        transport->addWidget(previous_);
        transport->addStretch();
        transport->addWidget(play_);
        transport->addStretch();
        transport->addWidget(next_);
        right->addLayout(transport);
        right->addSpacing(8);
        slider_ = new QSlider(Qt::Horizontal);
        slider_->setRange(0, 1000); // 固定比例，避免长音频毫秒数超出 int。
        slider_->setFixedHeight(28);
        slider_->setEnabled(false);
        slider_->setAccessibleName(QStringLiteral("播放进度"));
        right->addWidget(slider_);
        auto* times = new QHBoxLayout;
        elapsed_ = new QLabel("00:00");
        total_ = new QLabel("--:--");
        for (auto* label : {elapsed_, total_})
            label->setStyleSheet("font-size: 13px; color: #9aa4aa;");
        times->addWidget(elapsed_);
        times->addStretch();
        times->addWidget(total_);
        right->addLayout(times);
        auto* options = new QHBoxLayout;
        mode_ = new QPushButton(QStringLiteral("顺序播放"));
        auto* listButton = new QPushButton(QStringLiteral("播放列表"));
        for (auto* button : {mode_, listButton}) {
            button->setMinimumHeight(44);
            button->setFocusPolicy(Qt::NoFocus);
        }
        options->addWidget(mode_);
        options->addStretch();
        options->addWidget(listButton);
        right->addLayout(options);
        right->addStretch();
        row->addLayout(right, 57);
        stack_->addWidget(playerPage);

        auto* listPage = new QWidget;
        auto* listLayout = new QVBoxLayout(listPage);
        auto* listBar = new QHBoxLayout;
        auto* back = new QPushButton(QStringLiteral("返回播放"));
        auto* refresh = new QPushButton(QStringLiteral("刷新歌曲"));
        for (auto* button : {back, refresh}) {
            button->setMinimumHeight(48);
            button->setFocusPolicy(Qt::NoFocus);
        }
        listBar->addWidget(back);
        listBar->addStretch();
        listBar->addWidget(refresh);
        listLayout->addLayout(listBar);
        list_ = new QListWidget;
        list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list_->setTextElideMode(Qt::ElideRight);
        listLayout->addWidget(list_);
        stack_->addWidget(listPage);

        connect(player_, &AudioPlayer::statusChanged, this, [this](const QString& text) {
            statusText_ = text;
            updateStatus();
        });
        connect(player_, &AudioPlayer::playingChanged, this, [this](bool playing) {
            playing_ = playing;
            play_->setSymbol(playing ? Symbol::Pause : Symbol::Play);
            play_->setAccessibleName(playing ? QStringLiteral("暂停") : QStringLiteral("播放"));
        });
        connect(player_, &AudioPlayer::metadataChanged, this,
                [this](const QString& title, const QString& artist) {
            if (!title.isEmpty()) title_->setFullText(title);
            if (!artist.isEmpty()) artist_->setFullText(artist);
        });
        connect(player_, &AudioPlayer::progressChanged, this,
                [this](qint64 position, qint64 duration) {
            duration_ = duration;
            slider_->setEnabled(duration > 0);
            total_->setText(duration > 0 ? timeText(duration) : QStringLiteral("--:--"));
            if (!slider_->isSliderDown()) {
                slider_->setValue(duration > 0 ? static_cast<int>(position * 1000 / duration) : 0);
                elapsed_->setText(timeText(position));
            }
        });
        connect(slider_, &QSlider::sliderMoved, this, [this](int value) {
            elapsed_->setText(timeText(duration_ * value / 1000));
        });
        connect(slider_, &QSlider::sliderReleased, this, [this]() {
            player_->seek(duration_ * slider_->value() / 1000);
        });
        connect(play_, &QPushButton::clicked, this, [this]() {
            if (playing_) player_->pause();
            else if (index_ >= 0) player_->playFile(tracks_.at(index_));
        });
        connect(previous_, &QPushButton::clicked, this, [this]() { step(-1); });
        connect(next_, &QPushButton::clicked, this, [this]() { step(1); });
        connect(mode_, &QPushButton::clicked, this, [this]() {
            modeIndex_ = (modeIndex_ + 1) % 3;
            const QStringList modes = {QStringLiteral("顺序播放"), QStringLiteral("列表循环"),
                                       QStringLiteral("单曲循环")};
            mode_->setText(modes.at(modeIndex_));
        });
        connect(player_, &AudioPlayer::finished, this, [this]() {
            if (tracks_.isEmpty()) return;
            if (modeIndex_ == 2) selectTrack(index_, true);
            else if (index_ + 1 < tracks_.size()) selectTrack(index_ + 1, true);
            else if (modeIndex_ == 1) selectTrack(0, true);
            // 顺序播放到末首就停止，不自动回到第一首。
        });
        connect(listButton, &QPushButton::clicked, this, [this]() {
            stack_->setCurrentIndex(1);
        });
        connect(back, &QPushButton::clicked, this, [this]() { stack_->setCurrentIndex(0); });
        connect(refresh, &QPushButton::clicked, this, [this]() { scanTracks(); });
        connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            selectTrack(list_->row(item), true);
            stack_->setCurrentIndex(0);
        });
        scanTracks();
    }
private:
    void updateStatus()
    {
        status_->setFullText(index_ >= 0
            ? QStringLiteral("%1 · %2 / %3").arg(statusText_).arg(index_ + 1).arg(tracks_.size())
            : statusText_);
    }
    void scanTracks()
    {
        const QString previous = index_ >= 0 ? tracks_.at(index_) : QString();
        const QDir dir(QCoreApplication::applicationDirPath() + "/media");
        tracks_.clear();
        list_->clear();
        for (const QFileInfo& file : dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name)) {
            const QString ext = file.suffix().toLower();
            if (ext != "mp3" && ext != "wav" && ext != "flac" && ext != "ogg") continue;
            tracks_.append(file.absoluteFilePath());
            list_->addItem(file.completeBaseName());
        }
        index_ = tracks_.indexOf(previous);
        const bool hasTracks = !tracks_.isEmpty();
        play_->setEnabled(hasTracks);
        previous_->setEnabled(hasTracks);
        next_->setEnabled(hasTracks);
        if (index_ >= 0) {
            list_->setCurrentRow(index_);
            updateStatus();
            return; // 刷新列表不打断仍存在的当前歌曲。
        }
        player_->stop();
        if (hasTracks) selectTrack(0, false);
        else {
            title_->setFullText(QStringLiteral("暂无本地音乐"));
            artist_->setFullText(QStringLiteral("将歌曲放入 media 文件夹"));
            statusText_ = QStringLiteral("上传后点播放列表 → 刷新歌曲");
            cover_->setSong(QString());
            updateStatus();
        }
    }
    void selectTrack(int index, bool start)
    {
        if (index < 0 || index >= tracks_.size()) return;
        player_->stop(); // 手动选中同一首也从头开始，清空旧时间和总线消息。
        index_ = index;
        const QString path = tracks_.at(index_);
        title_->setFullText(QFileInfo(path).completeBaseName());
        artist_->setFullText(QStringLiteral("本地音频"));
        cover_->setSong(path);
        list_->setCurrentRow(index_);
        statusText_ = QStringLiteral("准备播放");
        updateStatus();
        if (start) player_->playFile(path);
    }
    void step(int delta)
    {
        if (!tracks_.isEmpty())
            selectTrack((index_ + delta + tracks_.size()) % tracks_.size(), true);
    }

    AudioPlayer* player_;
    QStackedLayout* stack_;
    RecordCover* cover_;
    ElidedLabel *title_, *artist_, *status_;
    TransportButton *previous_, *play_, *next_;
    QSlider* slider_;
    QLabel *elapsed_, *total_;
    QPushButton* mode_;
    QListWidget* list_;
    QStringList tracks_;
    int index_ = -1;
    int modeIndex_ = 0;
    bool playing_ = false;
    qint64 duration_ = 0;
    QString statusText_;
};
} // namespace

QWidget* createMediaPage(AudioPlayer* player, QWidget* parent)
{
    return new MediaPage(player, parent);
}
