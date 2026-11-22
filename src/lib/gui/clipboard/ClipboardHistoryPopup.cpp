/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardHistoryPopup.h"

#include "ClipboardHistoryDelegate.h"
#include "ClipboardHistoryItem.h"
#include "ClipboardHistoryModel.h"

#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCursor>
#include <QDesktopServices>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#if defined(Q_OS_MACOS)
#include "gui/OSXHelpers.h"
#endif

namespace deskflow::gui::clipboard {

namespace {

constexpr int kShadowMargin = 18;
constexpr QSize kCardSize(780, 500);

QString rgba(QColor color, qreal alpha)
{
  return QStringLiteral("rgba(%1, %2, %3, %4)")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(QString::number(alpha, 'f', 3));
}

QString shortcutText(const QString &portable)
{
  return QKeySequence(portable).toString(QKeySequence::NativeText);
}

QString kbd(const QString &key, const QString &label)
{
  return QStringLiteral("<b>%1</b>&nbsp;%2").arg(key.toHtmlEscaped(), label.toHtmlEscaped());
}

} // namespace

ClipboardHistoryPopup::ClipboardHistoryPopup(ClipboardHistoryModel *model, QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      m_model(model),
      m_filter(new ClipboardHistoryFilterModel(this))
{
  setAttribute(Qt::WA_TranslucentBackground);
  setWindowTitle(tr("Clipboard History"));
  setObjectName(QStringLiteral("ClipboardHistoryPopup"));

  m_filter->setSourceModel(m_model);

  buildUi();
  applyStyle();
  updateCounts();
  updatePreview();

  connect(m_filter, &QAbstractItemModel::rowsInserted, this, &ClipboardHistoryPopup::updateCounts);
  connect(m_filter, &QAbstractItemModel::rowsRemoved, this, &ClipboardHistoryPopup::updateCounts);
  connect(m_filter, &QAbstractItemModel::modelReset, this, &ClipboardHistoryPopup::updateCounts);
  connect(m_filter, &QAbstractItemModel::layoutChanged, this, &ClipboardHistoryPopup::updateCounts);
  connect(m_filter, &QAbstractItemModel::dataChanged, this, &ClipboardHistoryPopup::updatePreview);
}

void ClipboardHistoryPopup::buildUi()
{
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(kShadowMargin, kShadowMargin, kShadowMargin, kShadowMargin);

  m_card = new QFrame(this);
  m_card->setObjectName(QStringLiteral("card"));
  outer->addWidget(m_card);

  auto *shadow = new QGraphicsDropShadowEffect(m_card);
  shadow->setBlurRadius(36);
  shadow->setOffset(0, 8);
  shadow->setColor(QColor(0, 0, 0, 90));
  m_card->setGraphicsEffect(shadow);

  auto *cardLayout = new QVBoxLayout(m_card);
  cardLayout->setContentsMargins(0, 0, 0, 0);
  cardLayout->setSpacing(0);

  // Search row.
  auto *header = new QHBoxLayout;
  header->setContentsMargins(18, 14, 12, 8);
  header->setSpacing(8);

  auto *searchIcon = new QLabel(m_card);
  searchIcon->setObjectName(QStringLiteral("searchIcon"));
  searchIcon->setFixedSize(20, 20);
  header->addWidget(searchIcon);

  m_search = new QLineEdit(m_card);
  m_search->setObjectName(QStringLiteral("search"));
  m_search->setPlaceholderText(tr("Search clipboard history…"));
  m_search->setClearButtonEnabled(true);
  m_search->setFrame(false);
  m_search->installEventFilter(this);
  header->addWidget(m_search, 1);

  m_sortMenu = new QMenu(this);
  auto *sortGroup = new QActionGroup(m_sortMenu);
  const QList<QPair<ClipboardHistoryFilterModel::SortMode, QString>> sorts = {
      {ClipboardHistoryFilterModel::SortMode::Recent, tr("Recently copied")},
      {ClipboardHistoryFilterModel::SortMode::MostUsed, tr("Most copied")},
      {ClipboardHistoryFilterModel::SortMode::FirstCopied, tr("First copied")},
  };
  for (const auto &[mode, label] : sorts) {
    auto *action = m_sortMenu->addAction(label);
    action->setCheckable(true);
    action->setData(int(mode));
    action->setChecked(mode == m_filter->sortMode());
    sortGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, mode = mode] {
      setSortMode(mode);
      Q_EMIT sortModeChanged(mode);
    });
  }

  m_sortButton = new QToolButton(m_card);
  m_sortButton->setObjectName(QStringLiteral("pillButton"));
  m_sortButton->setPopupMode(QToolButton::InstantPopup);
  m_sortButton->setMenu(m_sortMenu);
  m_sortButton->setToolTip(tr("Sort order"));
  m_sortButton->setFocusPolicy(Qt::NoFocus);
  header->addWidget(m_sortButton);

  m_moreButton = new QToolButton(m_card);
  m_moreButton->setObjectName(QStringLiteral("pillButton"));
  m_moreButton->setText(QStringLiteral("⋯"));
  m_moreButton->setPopupMode(QToolButton::InstantPopup);
  m_moreButton->setToolTip(tr("Clipboard history settings"));
  m_moreButton->setFocusPolicy(Qt::NoFocus);
  header->addWidget(m_moreButton);
  cardLayout->addLayout(header);

  // Category chips.
  auto *chips = new QHBoxLayout;
  chips->setContentsMargins(16, 0, 16, 10);
  chips->setSpacing(6);
  m_categories = new QButtonGroup(this);
  m_categories->setExclusive(true);
  const QStringList categoryNames = {tr("All"), tr("Pinned"), tr("Text"), tr("Links"), tr("Images"), tr("Files")};
  for (int i = 0; i < categoryNames.size(); ++i) {
    auto *chip = new QToolButton(m_card);
    chip->setObjectName(QStringLiteral("chip"));
    chip->setText(categoryNames.at(i));
    chip->setCheckable(true);
    chip->setChecked(i == 0);
    chip->setFocusPolicy(Qt::NoFocus);
    chip->setCursor(Qt::PointingHandCursor);
    m_categories->addButton(chip, i);
    chips->addWidget(chip);
  }
  chips->addStretch(1);
  m_countLabel = new QLabel(m_card);
  m_countLabel->setObjectName(QStringLiteral("muted"));
  chips->addWidget(m_countLabel);
  cardLayout->addLayout(chips);
  connect(m_categories, &QButtonGroup::idClicked, this, &ClipboardHistoryPopup::setCategory);

  auto *topLine = new QFrame(m_card);
  topLine->setObjectName(QStringLiteral("divider"));
  topLine->setFixedHeight(1);
  cardLayout->addWidget(topLine);

  // List and preview.
  auto *body = new QHBoxLayout;
  body->setContentsMargins(0, 0, 0, 0);
  body->setSpacing(0);

  m_listStack = new QStackedWidget(m_card);
  m_list = new QListView(m_listStack);
  m_list->setObjectName(QStringLiteral("list"));
  m_list->setModel(m_filter);
  m_list->setItemDelegate(new ClipboardHistoryDelegate(m_list));
  m_list->setFrameShape(QFrame::NoFrame);
  m_list->setUniformItemSizes(true);
  m_list->setSelectionMode(QAbstractItemView::SingleSelection);
  m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_list->setMouseTracking(true);
  m_list->setAttribute(Qt::WA_Hover);
  m_list->setContextMenuPolicy(Qt::CustomContextMenu);
  m_list->setFocusPolicy(Qt::NoFocus);
  m_list->viewport()->setAutoFillBackground(false);
  m_list->setSpacing(0);
  m_listStack->addWidget(m_list);

  m_emptyLabel = new QLabel(m_listStack);
  m_emptyLabel->setObjectName(QStringLiteral("empty"));
  m_emptyLabel->setAlignment(Qt::AlignCenter);
  m_emptyLabel->setWordWrap(true);
  m_listStack->addWidget(m_emptyLabel);
  body->addWidget(m_listStack, 11);

  connect(m_list, &QListView::doubleClicked, this, [this](const QModelIndex &index) {
    activateRow(index.row(), false);
  });
  connect(m_list, &QListView::customContextMenuRequested, this, &ClipboardHistoryPopup::showContextMenu);
  connect(m_list->selectionModel(), &QItemSelectionModel::currentChanged, this, &ClipboardHistoryPopup::updatePreview);

  auto *bodyLine = new QFrame(m_card);
  bodyLine->setObjectName(QStringLiteral("divider"));
  bodyLine->setFixedWidth(1);
  body->addWidget(bodyLine);

  auto *preview = new QWidget(m_card);
  preview->setObjectName(QStringLiteral("preview"));
  auto *previewLayout = new QVBoxLayout(preview);
  previewLayout->setContentsMargins(16, 14, 16, 14);
  previewLayout->setSpacing(10);

  m_previewStack = new QStackedWidget(preview);
  m_previewText = new QPlainTextEdit(m_previewStack);
  m_previewText->setObjectName(QStringLiteral("previewText"));
  m_previewText->setReadOnly(true);
  m_previewText->setFrameShape(QFrame::NoFrame);
  m_previewText->setFocusPolicy(Qt::NoFocus);
  m_previewText->setLineWrapMode(QPlainTextEdit::WidgetWidth);
  m_previewStack->addWidget(m_previewText);

  m_previewImage = new QLabel(m_previewStack);
  m_previewImage->setAlignment(Qt::AlignCenter);
  m_previewImage->setMinimumSize(1, 1);
  m_previewStack->addWidget(m_previewImage);

  m_previewColor = new QLabel(m_previewStack);
  m_previewColor->setAlignment(Qt::AlignCenter);
  m_previewStack->addWidget(m_previewColor);
  previewLayout->addWidget(m_previewStack, 1);

  m_openLinkButton = new QPushButton(tr("Open link"), preview);
  m_openLinkButton->setObjectName(QStringLiteral("pillButton"));
  m_openLinkButton->setFocusPolicy(Qt::NoFocus);
  m_openLinkButton->setCursor(Qt::PointingHandCursor);
  connect(m_openLinkButton, &QPushButton::clicked, this, [this] {
    if (const auto *item = m_model->item(currentId())) {
      auto url = QUrl::fromUserInput(item->text.trimmed());
      if (url.isValid())
        QDesktopServices::openUrl(url);
    }
  });
  previewLayout->addWidget(m_openLinkButton, 0, Qt::AlignLeft);

  m_metaLabel = new QLabel(preview);
  m_metaLabel->setObjectName(QStringLiteral("meta"));
  m_metaLabel->setTextFormat(Qt::RichText);
  m_metaLabel->setWordWrap(true);
  previewLayout->addWidget(m_metaLabel);

  body->addWidget(preview, 9);
  cardLayout->addLayout(body, 1);

  auto *bottomLine = new QFrame(m_card);
  bottomLine->setObjectName(QStringLiteral("divider"));
  bottomLine->setFixedHeight(1);
  cardLayout->addWidget(bottomLine);

  // Footer: keyboard hints and recording state.
  auto *footer = new QHBoxLayout;
  footer->setContentsMargins(18, 8, 18, 9);
  m_hintsLabel = new QLabel(m_card);
  m_hintsLabel->setObjectName(QStringLiteral("muted"));
  m_hintsLabel->setTextFormat(Qt::RichText);
  m_hintsLabel->setText(
      QStringList{
          kbd(QStringLiteral("↵"), tr("Copy")),
          kbd(shortcutText(QStringLiteral("Shift+Return")), tr("Plain text")),
          kbd(shortcutText(QStringLiteral("Ctrl+P")), tr("Pin")),
          kbd(shortcutText(QStringLiteral("Shift+Del")), tr("Delete")),
          kbd(QStringLiteral("Tab"), tr("Filter")),
          kbd(QStringLiteral("Esc"), tr("Close")),
      }
          .join(QStringLiteral("&nbsp;&nbsp;&nbsp;"))
  );
  footer->addWidget(m_hintsLabel, 1);
  m_statusLabel = new QLabel(m_card);
  m_statusLabel->setObjectName(QStringLiteral("muted"));
  m_statusLabel->setTextFormat(Qt::RichText);
  footer->addWidget(m_statusLabel);
  cardLayout->addLayout(footer);

  connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
    m_filter->setSearchText(text);
    selectRow(0);
    updateCounts();
  });

  resize(kCardSize + QSize(2 * kShadowMargin, 2 * kShadowMargin));
  updateSortButton();
  setRecordingState(false, false);
}

void ClipboardHistoryPopup::applyStyle()
{
  const auto pal = QApplication::palette();
  const bool dark = pal.color(QPalette::Window).lightness() < 128;
  const auto text = pal.color(QPalette::Text);
  const auto accent = pal.color(QPalette::Highlight);

  const auto cardBg = dark ? QColor(32, 32, 36) : QColor(250, 250, 252);
  const auto border = dark ? rgba(Qt::white, 0.10) : rgba(Qt::black, 0.10);
  const auto divider = dark ? rgba(Qt::white, 0.07) : rgba(Qt::black, 0.07);

  QPalette listPalette = pal;
  listPalette.setColor(QPalette::Base, Qt::transparent);
  m_list->setPalette(listPalette);

  setStyleSheet(QStringLiteral(R"(
    QFrame#card { background: %1; border: 1px solid %2; border-radius: 16px; }
    QFrame#divider { background: %3; border: none; }
    QLineEdit#search { background: transparent; border: none; font-size: 17px; padding: 4px 2px; color: %4; }
    QToolButton#chip {
      background: transparent; border: 1px solid %2; border-radius: 13px;
      padding: 3px 12px; color: %5;
    }
    QToolButton#chip:hover { background: %6; }
    QToolButton#chip:checked { background: %7; border-color: %7; color: %8; }
    QToolButton#pillButton, QPushButton#pillButton {
      background: %6; border: none; border-radius: 8px; padding: 5px 10px; color: %4;
    }
    QToolButton#pillButton:hover, QPushButton#pillButton:hover { background: %9; }
    QToolButton#pillButton::menu-indicator { image: none; width: 0; }
    QListView#list { background: transparent; border: none; outline: none; padding: 6px 0; }
    QPlainTextEdit#previewText { background: transparent; color: %4; }
    QLabel#muted, QLabel#meta { color: %5; }
    QLabel#empty { color: %5; font-size: 14px; }
    QScrollBar:vertical { background: transparent; width: 8px; margin: 4px 2px; }
    QScrollBar::handle:vertical { background: %9; border-radius: 3px; min-height: 30px; }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
  )")
                    .arg(
                        cardBg.name(), border, divider, text.name(), rgba(text, 0.62), rgba(text, 0.07), accent.name(),
                        pal.color(QPalette::HighlightedText).name(), rgba(text, 0.14)
                    ));

  // Magnifier glyph drawn in the text colour so it matches either theme.
  if (auto *icon = findChild<QLabel *>(QStringLiteral("searchIcon"))) {
    const qreal dpr = devicePixelRatioF();
    QPixmap glyph(QSize(20, 20) * dpr);
    glyph.setDevicePixelRatio(dpr);
    glyph.fill(Qt::transparent);
    QPainter p(&glyph);
    p.setRenderHint(QPainter::Antialiasing);
    auto colour = text;
    colour.setAlphaF(0.55f);
    p.setPen(QPen(colour, 2, Qt::SolidLine, Qt::RoundCap));
    p.drawEllipse(QRectF(3, 3, 11, 11));
    p.drawLine(QPointF(12.5, 12.5), QPointF(17, 17));
    icon->setPixmap(glyph);
  }

  auto font = m_previewText->font();
  font.setStyleHint(QFont::Monospace);
  font.setFamilies(
      {QStringLiteral("SF Mono"), QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"),
       QStringLiteral("JetBrains Mono"), QStringLiteral("DejaVu Sans Mono"), QStringLiteral("monospace")}
  );
  m_previewText->setFont(font);
}

void ClipboardHistoryPopup::present()
{
  const auto *screen = QGuiApplication::screenAt(QCursor::pos());
  if (!screen)
    screen = QGuiApplication::primaryScreen();
  const auto area = screen->availableGeometry();

  auto size = kCardSize + QSize(2 * kShadowMargin, 2 * kShadowMargin);
  size = size.boundedTo(area.size() - QSize(40, 40));
  resize(size);
  // Spotlight style: horizontally centred, a little above the middle.
  move(area.left() + (area.width() - width()) / 2, area.top() + qMax(20, (area.height() - height()) / 3));

  m_search->clear();
  m_categories->button(0)->setChecked(true);
  setCategory(0);
  selectRow(0);

#if defined(Q_OS_MACOS)
  forceAppActive();
#endif
  show();
  raise();
  activateWindow();
  m_search->setFocus(Qt::PopupFocusReason);
  // The preview depends on the final layout size.
  QTimer::singleShot(0, this, &ClipboardHistoryPopup::updatePreview);
}

void ClipboardHistoryPopup::toggle()
{
  if (isVisible())
    hide();
  else
    present();
}

void ClipboardHistoryPopup::setSettingsMenu(QMenu *menu)
{
  m_moreButton->setMenu(menu);
  connect(menu, &QMenu::aboutToShow, this, [this] { m_menuOpen = true; });
  connect(menu, &QMenu::aboutToHide, this, [this] {
    m_menuOpen = false;
    QTimer::singleShot(0, this, [this] { m_search->setFocus(); });
  });
}

void ClipboardHistoryPopup::setSortMode(ClipboardHistoryFilterModel::SortMode mode)
{
  m_filter->setSortMode(mode);
  for (auto *action : m_sortMenu->actions())
    action->setChecked(action->data().toInt() == int(mode));
  updateSortButton();
  selectRow(0);
}

void ClipboardHistoryPopup::setRecordingState(bool paused, bool ignoringNext)
{
  m_paused = paused;
  m_ignoringNext = ignoringNext;
  QString dot;
  QString label;
  if (paused) {
    dot = QStringLiteral("#f59e0b");
    label = tr("Paused");
  } else if (ignoringNext) {
    dot = QStringLiteral("#3b82f6");
    label = tr("Ignoring next copy");
  } else {
    dot = QStringLiteral("#22c55e");
    label = tr("Recording");
  }
  m_statusLabel->setText(QStringLiteral("<span style=\"color:%1\">●</span>&nbsp;%2").arg(dot, label.toHtmlEscaped()));
  updateCounts();
}

bool ClipboardHistoryPopup::eventFilter(QObject *watched, QEvent *event)
{
  if (watched != m_search || event->type() != QEvent::KeyPress)
    return QWidget::eventFilter(watched, event);

  auto *key = static_cast<QKeyEvent *>(event);
  const auto mods = key->modifiers();
  const bool ctrl = mods & Qt::ControlModifier; // Command on macOS

  switch (key->key()) {
  case Qt::Key_Down:
    moveSelection(1);
    return true;
  case Qt::Key_Up:
    moveSelection(-1);
    return true;
  case Qt::Key_PageDown:
    moveSelection(qMax(1, m_list->viewport()->height() / ClipboardHistoryDelegate::kRowHeight - 1));
    return true;
  case Qt::Key_PageUp:
    moveSelection(-qMax(1, m_list->viewport()->height() / ClipboardHistoryDelegate::kRowHeight - 1));
    return true;
  case Qt::Key_Return:
  case Qt::Key_Enter:
    activate(mods & Qt::ShiftModifier);
    return true;
  case Qt::Key_Escape:
    if (!m_search->text().isEmpty())
      m_search->clear();
    else
      hide();
    return true;
  case Qt::Key_Tab:
    setCategory((m_categories->checkedId() + 1) % m_categories->buttons().size());
    return true;
  case Qt::Key_Backtab:
    setCategory(
        (m_categories->checkedId() + int(m_categories->buttons().size()) - 1) % int(m_categories->buttons().size())
    );
    return true;
  case Qt::Key_Delete:
    if (mods & (Qt::ShiftModifier | Qt::ControlModifier)) {
      removeCurrent();
      return true;
    }
    break;
  case Qt::Key_Backspace:
    if (ctrl && (mods & Qt::ShiftModifier)) {
      removeCurrent();
      return true;
    }
    break;
  case Qt::Key_P:
    if (ctrl) {
      togglePinCurrent();
      return true;
    }
    break;
  case Qt::Key_W:
    if (ctrl) {
      hide();
      return true;
    }
    break;
  default:
    if (ctrl && key->key() >= Qt::Key_1 && key->key() <= Qt::Key_9) {
      activateRow(key->key() - Qt::Key_1, mods & Qt::ShiftModifier);
      return true;
    }
    break;
  }
  return QWidget::eventFilter(watched, event);
}

void ClipboardHistoryPopup::changeEvent(QEvent *event)
{
  QWidget::changeEvent(event);
  if (event->type() == QEvent::ActivationChange) {
    // Behave like a launcher: clicking anywhere else dismisses the panel.
    QTimer::singleShot(0, this, [this] {
      if (isVisible() && !isActiveWindow() && !m_menuOpen && !QApplication::activePopupWidget())
        hide();
    });
  } else if (event->type() == QEvent::PaletteChange) {
    applyStyle();
  }
}

void ClipboardHistoryPopup::updatePreview()
{
  const auto *item = m_model->item(currentId());
  m_openLinkButton->setVisible(item && item->kind == ItemKind::Link);

  if (!item) {
    m_previewText->clear();
    m_previewStack->setCurrentWidget(m_previewText);
    m_metaLabel->clear();
    return;
  }

  QString content;
  switch (item->kind) {
  case ItemKind::Image: {
    const auto image = m_model->image(item->id);
    const auto available = m_previewStack->size() * devicePixelRatioF();
    auto pixmap = QPixmap::fromImage(
        image.size().boundedTo(available) == image.size()
            ? image
            : image.scaled(available, Qt::KeepAspectRatio, Qt::SmoothTransformation)
    );
    pixmap.setDevicePixelRatio(devicePixelRatioF());
    m_previewImage->setPixmap(pixmap);
    m_previewStack->setCurrentWidget(m_previewImage);
    content = tr("%1 × %2 pixels").arg(item->imageSize.width()).arg(item->imageSize.height());
    break;
  }
  case ItemKind::Color: {
    const auto color = QColor::fromString(item->text.trimmed());
    m_previewColor->setStyleSheet(
        QStringLiteral("background: %1; border-radius: 12px; color: %2; font-size: 18px;")
            .arg(color.name(QColor::HexArgb), color.lightness() > 140 ? QStringLiteral("#000") : QStringLiteral("#fff"))
    );
    m_previewColor->setText(item->text.trimmed());
    m_previewStack->setCurrentWidget(m_previewColor);
    content = QStringLiteral("rgb(%1, %2, %3)").arg(color.red()).arg(color.green()).arg(color.blue());
    break;
  }
  case ItemKind::Files:
    m_previewText->setPlainText(item->text);
    m_previewStack->setCurrentWidget(m_previewText);
    content = item->urls.size() == 1 ? tr("1 file") : tr("%1 files").arg(item->urls.size());
    break;
  default: {
    // Large texts are cut for the preview only; the copy keeps everything.
    constexpr int kPreviewLimit = 20000;
    m_previewText->setPlainText(
        item->text.size() > kPreviewLimit ? item->text.left(kPreviewLimit) + QStringLiteral("…") : item->text
    );
    m_previewStack->setCurrentWidget(m_previewText);
    const auto lines = item->text.count(QLatin1Char('\n')) + 1;
    content =
        item->text.size() == 1 ? tr("1 character") : tr("%1 characters").arg(QLocale().toString(item->text.size()));
    if (lines > 1)
      content += QStringLiteral(" · ") + tr("%1 lines").arg(lines);
    if (!item->html.isEmpty())
      content += QStringLiteral(" · ") + tr("rich text");
    break;
  }
  }

  const auto row = [](const QString &label, const QString &value) {
    return QStringLiteral("<tr><td style=\"padding-right:14px\">%1</td><td>%2</td></tr>")
        .arg(label.toHtmlEscaped(), value.toHtmlEscaped());
  };
  QString meta = QStringLiteral("<table cellspacing=\"0\" cellpadding=\"1\">");
  meta += row(tr("Source"), item->source.isEmpty() ? tr("This computer") : item->source);
  meta += row(tr("Content"), content);
  meta += row(tr("Last copied"), QLocale().toString(item->lastCopied, QLocale::ShortFormat));
  if (item->copyCount > 1) {
    meta += row(tr("First copied"), QLocale().toString(item->firstCopied, QLocale::ShortFormat));
    meta += row(tr("Times copied"), QString::number(item->copyCount));
  }
  meta += QStringLiteral("</table>");
  m_metaLabel->setText(meta);
}

void ClipboardHistoryPopup::updateCounts()
{
  const int shown = m_filter->rowCount();
  const int total = m_model->rowCount();
  m_countLabel->setText(
      shown == total ? (total == 1 ? tr("1 item") : tr("%1 items").arg(total)) : tr("%1 of %2").arg(shown).arg(total)
  );

  if (shown > 0) {
    m_listStack->setCurrentWidget(m_list);
    if (!m_list->currentIndex().isValid())
      selectRow(0);
  } else {
    QString message;
    if (total == 0)
      message = m_paused ? tr("Recording is paused.\nResume it from the ⋯ menu.")
                         : tr("Nothing here yet.\nCopy something on any of your computers and it will show up here.");
    else if (!m_search->text().trimmed().isEmpty())
      message = tr("No matches for “%1”").arg(m_search->text().trimmed());
    else
      message = tr("Nothing in this category yet.");
    m_emptyLabel->setText(message);
    m_listStack->setCurrentWidget(m_emptyLabel);
  }
  updatePreview();
}

void ClipboardHistoryPopup::selectRow(int row)
{
  if (m_filter->rowCount() == 0) {
    m_list->setCurrentIndex({});
    updatePreview();
    return;
  }
  row = qBound(0, row, m_filter->rowCount() - 1);
  const auto index = m_filter->index(row, 0);
  m_list->setCurrentIndex(index);
  m_list->scrollTo(index);
}

void ClipboardHistoryPopup::moveSelection(int delta)
{
  const int current = m_list->currentIndex().isValid() ? m_list->currentIndex().row() : -1;
  selectRow(current + delta);
}

void ClipboardHistoryPopup::setCategory(int category)
{
  if (auto *button = m_categories->button(category))
    button->setChecked(true);
  m_filter->setCategory(ClipboardHistoryFilterModel::Category(category));
  selectRow(0);
  updateCounts();
}

void ClipboardHistoryPopup::activate(bool plainTextOnly)
{
  if (m_list->currentIndex().isValid())
    activateRow(m_list->currentIndex().row(), plainTextOnly);
}

void ClipboardHistoryPopup::activateRow(int row, bool plainTextOnly)
{
  if (row < 0 || row >= m_filter->rowCount())
    return;
  const auto id = m_filter->index(row, 0).data(ClipboardHistoryModel::IdRole).toString();
  hide();
  Q_EMIT copyRequested(id, plainTextOnly);
}

void ClipboardHistoryPopup::togglePinCurrent()
{
  const auto id = currentId();
  if (id.isEmpty())
    return;
  Q_EMIT pinToggled(id);
  // Keep the same item selected after it moves.
  for (int row = 0; row < m_filter->rowCount(); ++row) {
    if (m_filter->index(row, 0).data(ClipboardHistoryModel::IdRole).toString() == id) {
      selectRow(row);
      break;
    }
  }
}

void ClipboardHistoryPopup::removeCurrent()
{
  if (!m_list->currentIndex().isValid())
    return;
  const int row = m_list->currentIndex().row();
  Q_EMIT removeRequested(currentId());
  selectRow(row);
}

void ClipboardHistoryPopup::showContextMenu(const QPoint &pos)
{
  const auto index = m_list->indexAt(pos);
  if (!index.isValid())
    return;
  m_list->setCurrentIndex(index);
  const auto id = currentId();
  const auto *item = m_model->item(id);
  if (!item)
    return;

  QMenu menu(this);
  menu.addAction(tr("Copy"), this, [this, row = index.row()] { activateRow(row, false); });
  if (item->kind != ItemKind::Image)
    menu.addAction(tr("Copy as plain text"), this, [this, row = index.row()] { activateRow(row, true); });
  menu.addAction(item->pinned ? tr("Unpin") : tr("Pin"), this, [this] { togglePinCurrent(); });
  menu.addSeparator();
  menu.addAction(tr("Delete"), this, [this] { removeCurrent(); });

  m_menuOpen = true;
  menu.exec(m_list->viewport()->mapToGlobal(pos));
  m_menuOpen = false;
}

void ClipboardHistoryPopup::updateSortButton()
{
  for (auto *action : m_sortMenu->actions()) {
    if (action->isChecked()) {
      m_sortButton->setText(action->text() + QStringLiteral("  ▾"));
      return;
    }
  }
}

QString ClipboardHistoryPopup::currentId() const
{
  const auto index = m_list->currentIndex();
  return index.isValid() ? index.data(ClipboardHistoryModel::IdRole).toString() : QString();
}

} // namespace deskflow::gui::clipboard
