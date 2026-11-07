#include "filter_dialog.hpp"

#include "pages.hpp"
#include "pixcake_theme.hpp"
#include "pixcake_widgets.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QSpinBox>

#include <functional>

namespace pixcake::shell {
namespace {

struct Choice {
  std::string value;
  QString label;
  QCheckBox* box = nullptr;
};

class FilterDialog final : public QDialog {
 public:
  FilterDialog(QWidget* parent, PageActions* actions) : QDialog(parent), actions_(actions) {
    setObjectName(QStringLiteral("filter-dialog"));
    setWindowTitle(qtext("filter.title"));
    setAttribute(Qt::WA_DeleteOnClose);
    auto* layout = new QVBoxLayout(this);
    auto* rating = new QGroupBox(qtext("filter.rating"), this);
    auto* rating_row = new QHBoxLayout(rating);
    rating_min_ = spin(rating, "filter-rating-min");
    rating_max_ = spin(rating, "filter-rating-max");
    rating_max_->setValue(kRatingMax);
    rating_row->addWidget(text(qtext("filter.rating.from")));
    rating_row->addWidget(rating_min_);
    rating_row->addWidget(text(qtext("filter.rating.to")));
    rating_row->addWidget(rating_max_);
    rating_row->addStretch();
    layout->addWidget(rating);
    group(layout, "filter.flag", "filter-flag", flags_,
          {{kFlagPick, qs(kFlagPick)}, {kFlagExclude, qs(kFlagExclude)}, {"", qtext("filter.none.flag")}});
    group(layout, "filter.color", "filter-color", colors_,
          {{kColorRed, qs(kColorRed)}, {kColorYellow, qs(kColorYellow)}, {kColorGreen, qs(kColorGreen)},
           {kColorBlue, qs(kColorBlue)}, {kColorPurple, qs(kColorPurple)}, {"", qtext("filter.none.color")}});
    group(layout, "filter.edit", "filter-edit", edits_,
          {{kEditStatusEdited, qs(kEditStatusEdited)}, {"", qtext("filter.not.edited")}});
    group(layout, "filter.export", "filter-export", exports_,
          {{kExportStatusExported, qs(kExportStatusExported)}, {"", qtext("filter.not.exported")}});
    group(layout, "filter.format", "filter-format", formats_,
          {{"JPEG", QStringLiteral("JPEG")}, {"PNG", QStringLiteral("PNG")}, {"TIFF", QStringLiteral("TIFF")},
           {"BMP", QStringLiteral("BMP")}});
    auto* sort_group = new QGroupBox(qtext("filter.sort"), this);
    auto* sort_row = new QHBoxLayout(sort_group);
    sort_ = new QComboBox(sort_group);
    sort_->setObjectName(QStringLiteral("filter-sort"));
    const char* keys[] = {"filter.sort.import", "filter.sort.capture", "filter.sort.edit", "filter.sort.rating",
                          "filter.sort.type",   "filter.sort.size",    "filter.sort.name"};
    for (const char* key : keys) sort_->addItem(qtext(key));
    descending_ = new QCheckBox(qtext("filter.sort.descending"), sort_group);
    descending_->setObjectName(QStringLiteral("filter-descending"));
    sort_row->addWidget(sort_, 1);
    sort_row->addWidget(descending_);
    layout->addWidget(sort_group);
    auto* bottom = new QHBoxLayout;
    shown_ = text(QString(), true);
    shown_->setObjectName(QStringLiteral("filter-shown"));
    bottom->addWidget(shown_, 1);
    auto* reset = action(qtext("filter.reset"), this, true);
    reset->setObjectName(QStringLiteral("filter-reset"));
    bottom->addWidget(reset);
    layout->addLayout(bottom);
    connect(reset, &QPushButton::clicked, this, [this] { load(PhotoQuery{}); apply(); });
    connect(rating_min_, qOverload<int>(&QSpinBox::valueChanged), this, [this] { apply(); });
    connect(rating_max_, qOverload<int>(&QSpinBox::valueChanged), this, [this] { apply(); });
    connect(sort_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { apply(); });
    connect(descending_, &QCheckBox::toggled, this, [this] { apply(); });
    load(actions_->photo_filter());
    refreshShown();
  }

 private:
  QSpinBox* spin(QWidget* parent, const char* name) {
    auto* box = new QSpinBox(parent);
    box->setObjectName(QString::fromLatin1(name));
    box->setRange(kRatingMin, kRatingMax);
    return box;
  }

  void group(QVBoxLayout* layout, const char* title, const char* name, std::vector<Choice>& into,
             std::vector<std::pair<std::string, QString>> choices) {
    auto* box = new QGroupBox(qtext(title), this);
    auto* row = new QHBoxLayout(box);
    for (auto& [value, label] : choices) {
      Choice choice{value, label, new QCheckBox(label, box)};
      choice.box->setObjectName(QString::fromLatin1(name) + QLatin1Char('-') + QString::number(into.size()));
      connect(choice.box, &QCheckBox::toggled, this, [this] { apply(); });
      row->addWidget(choice.box);
      into.push_back(choice);
    }
    row->addStretch();
    layout->addWidget(box);
  }

  static void tick(std::vector<Choice>& choices, const std::vector<std::string>& values) {
    for (auto& choice : choices) {
      const QSignalBlocker block(choice.box);
      choice.box->setChecked(std::find(values.begin(), values.end(), choice.value) != values.end());
    }
  }

  static std::vector<std::string> ticked(const std::vector<Choice>& choices) {
    std::vector<std::string> values;
    for (const auto& choice : choices) {
      if (choice.box->isChecked()) values.push_back(choice.value);
    }
    return values;
  }

  void load(const PhotoQuery& query) {
    loading_ = true;
    {
      const QSignalBlocker a(rating_min_), b(rating_max_), c(sort_), d(descending_);
      rating_min_->setValue(query.rating_min);
      rating_max_->setValue(query.rating_max);
      sort_->setCurrentIndex(static_cast<int>(query.sort));
      descending_->setChecked(query.descending);
    }
    tick(flags_, query.flags);
    tick(colors_, query.colors);
    tick(edits_, query.edit_statuses);
    tick(exports_, query.export_statuses);
    tick(formats_, query.formats);
    loading_ = false;
  }

  void apply() {
    if (loading_) return;
    PhotoQuery query;
    query.rating_min = rating_min_->value();
    query.rating_max = rating_max_->value();
    if (query.rating_min > query.rating_max) query.rating_max = query.rating_min;  // 倒置的区间不合法：以下限为准
    query.flags = ticked(flags_);
    query.colors = ticked(colors_);
    query.edit_statuses = ticked(edits_);
    query.export_statuses = ticked(exports_);
    query.formats = ticked(formats_);
    query.sort = static_cast<PhotoSortKey>(sort_->currentIndex());
    query.descending = descending_->isChecked();
    actions_->request_photo_filter(query);
    refreshShown();
  }

  void refreshShown() {
    std::size_t listed = 0;
    for (const auto& photo : actions_->photos()) listed += photo.copy == 0 ? 1 : 0;
    shown_->setText(qtext("filter.shown").arg(listed).arg(actions_->photo_total()));
  }

  PageActions* actions_;
  bool loading_ = false;
  QSpinBox* rating_min_ = nullptr;
  QSpinBox* rating_max_ = nullptr;
  std::vector<Choice> flags_, colors_, edits_, exports_, formats_;
  QComboBox* sort_ = nullptr;
  QCheckBox* descending_ = nullptr;
  QLabel* shown_ = nullptr;
};

}  // namespace

void openFilterDialog(QWidget* parent, PageActions* actions) {
  QWidget* window = parent != nullptr ? parent->window() : nullptr;
  if (window != nullptr) {
    if (auto* open = window->findChild<QDialog*>(QStringLiteral("filter-dialog"))) {
      open->raise();
      open->activateWindow();
      return;
    }
  }
  (new FilterDialog(window, actions))->show();
}

QPushButton* filterAction(std::string_view list, const CopyItem& item, QWidget* parent, PageActions* actions) {
  if (item.id != "filter" && item.id != "sort") return itemAction(list, item, parent);
  auto* button = itemAction(list, item, parent, actions != nullptr && actions->photo_total() > 0);
  if (actions == nullptr) return button;
  button->setObjectName(QStringLiteral("photo-") + qs(item.id));
  button->setProperty("selected", actions->photo_filtered());
  QObject::connect(button, &QPushButton::clicked, button, [button, actions] { openFilterDialog(button, actions); });
  return button;
}

}  // namespace pixcake::shell
