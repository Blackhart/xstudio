// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/shot_tree.cpp
 * @brief Sequence and shot tree: item model, filter, page load, and selection.
 */

#include "viewModel/shot_tree.hpp"

#include <caf/const_typed_message_view.hpp>
#include <caf/detail/behavior_impl.hpp>
#include <caf/make_counted.hpp>
#include <caf/unit.hpp>

#include <QCollator>
#include <QQmlEngine>
#include <QString>
#include <QtQml>

#include <algorithm>
#include <string>
#include <utility>

namespace trace = xstudio::mikros_shotbrowser::log;

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace xstudio::mikros_shotbrowser {

namespace {

ShotTree *qml_shot_tree = nullptr; ///< Instance exposed to QML as ShotTreeBridge.

using TreePage = std::function<
    void(const std::string &, std::uint64_t, bool, const utility::JsonStore &)>;

/**
 * @brief Applies a shot-tree page or count and declines every other catalog.
 *
 * `or_else` tries the next handler only when `invoke` returns false. Returning
 * `caf::skip` from a matched functor still consumes the message, so a page for
 * another catalog is declined by returning false.
 */
class ShotTreePageHandler : public caf::detail::behavior_impl {
  public:
    /**
     * @brief Stores the lambda that applies one shot-tree page or count.
     * @param apply Called on the panel actor for a current shot-tree message.
     */
    explicit ShotTreePageHandler(TreePage apply) : apply_(std::move(apply)) {}

    /**
     * @brief Runs the tree lambda, or declines the message.
     * @param visitor Receives an empty result when the page was ours.
     * @param message Worker page.
     * @return False when this handler does not take the message.
     */
    bool invoke(caf::detail::invoke_result_visitor &visitor, caf::message &message) override {
        using view_type = caf::const_typed_message_view<
            data_source::put_data_atom,
            std::uint64_t,
            std::string,
            std::int64_t,
            bool,
            std::int64_t,
            bool,
            std::int64_t,
            bool,
            std::int64_t,
            bool,
            std::int64_t,
            bool,
            utility::JsonStore>;
        view_type view{message};
        if (!view) {
            return false;
        }
        const std::string catalog = caf::get<2>(view);
        if (catalog != "shot_tree" && catalog != "shot_tree_count") {
            return false; ///< Another catalog: the shell handler continues.
        }
        apply_(catalog, caf::get<1>(view), caf::get<12>(view), caf::get<13>(view));
        visitor(caf::unit);
        return true;
    }

  private:
    TreePage apply_; ///< Lambda stored by `message_handler_extensions`.
};

/**
 * @brief Orders one sequence before another by name.
 * @param left First sequence.
 * @param right Second sequence.
 * @return True when `left` belongs before `right`.
 */
bool sequenceNameAlphabeticOrderAlgorithm(
    const poco::SequenceShots &left, const poco::SequenceShots &right) {
    static const QCollator order = [] {
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive); ///< `a` comes before `B`.
        return collator;
    }();
    const int nameOrder = order.compare(
        QString::fromStdString(left.sequence.name), QString::fromStdString(right.sequence.name));
    if (nameOrder != 0) {
        return nameOrder < 0; ///< Different names: the earlier name comes first.
    }
    return left.sequence.id < right.sequence.id;
}

/**
 * @brief Orders one shot before another by name.
 * @param left First shot.
 * @param right Second shot.
 * @return True when `left` belongs before `right`.
 */
bool shotNameAlphabeticOrderAlgorithm(const poco::Shot &left, const poco::Shot &right) {
    static const QCollator order = [] {
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive); ///< `a` comes before `B`.
        return collator;
    }();
    const int nameOrder =
        order.compare(QString::fromStdString(left.name), QString::fromStdString(right.name));
    if (nameOrder != 0) {
        return nameOrder < 0; ///< Different names: the earlier name comes first.
    }
    return left.id < right.id; ///< Same name: the smaller id stays put.
}

/**
 * @brief Packs a sequence row into a model internal id.
 * @param sequenceIndex Index in `sequences_`.
 */
quintptr packSequenceId(int sequenceIndex) {
    return (quintptr(sequenceIndex) << 1) | 0u;
}

/**
 * @brief Packs a shot row into a model internal id.
 * @param sequenceIndex Parent index in `sequences_`.
 * @param shotIndex Index in that sequence's shots.
 */
quintptr packShotId(int sequenceIndex, int shotIndex) {
    return (quintptr(sequenceIndex) << 1) | 1u | (quintptr(shotIndex) << 16);
}

/**
 * @brief True when the packed id names a shot.
 */
bool packedIsShot(quintptr id) { return (id & 1u) != 0u; }

/**
 * @brief Sequence index stored in a packed id.
 */
int packedSequenceIndex(quintptr id) { return int((id >> 1) & 0x7FFFu); }

/**
 * @brief Shot index stored in a packed shot id.
 */
int packedShotIndex(quintptr id) { return int((id >> 16) & 0xFFFFu); }

/**
 * @brief True when a row's name contains the filter text, or its id equals it.
 * @param index Source model index for the sequence or shot.
 * @param text Case-insensitive needle for the name; exact match for the id.
 */
bool rowMatchesFilter(const QModelIndex &index, const QString &text) {
    const QString name = index.data(ShotTreeItemModel::NameRole).toString();
    if (name.contains(text, Qt::CaseInsensitive)) {
        return true;
    }
    const QString id = index.data(ShotTreeItemModel::IdRole).toString();
    return id.compare(text, Qt::CaseInsensitive) == 0; ///< Id match is exact, not a substring.
}

} // namespace

// ---------------------------------------------------------------------------
// ShotTreeItemModel
// ---------------------------------------------------------------------------

ShotTreeItemModel::ShotTreeItemModel(ShotTree *owner)
    : QAbstractItemModel(owner), owner_(owner) {}

void ShotTreeItemModel::resetFromSequences() {
    beginResetModel();
    endResetModel();
}

QModelIndex
ShotTreeItemModel::index(int row, int column, const QModelIndex &parent) const {
    if (!owner_ || column != 0 || row < 0) {
        return {};
    }
    if (!parent.isValid()) {
        if (row >= static_cast<int>(owner_->sequences_.size())) {
            return {};
        }
        return createIndex(row, 0, packSequenceId(row));
    }
    if (packedIsShot(parent.internalId())) {
        return {};
    }
    const int sequenceIndex = packedSequenceIndex(parent.internalId());
    if (sequenceIndex < 0 || sequenceIndex >= static_cast<int>(owner_->sequences_.size())) {
        return {};
    }
    if (row >= static_cast<int>(owner_->sequences_[sequenceIndex].shots.size())) {
        return {};
    }
    return createIndex(row, 0, packShotId(sequenceIndex, row));
}

QModelIndex ShotTreeItemModel::parent(const QModelIndex &child) const {
    if (!child.isValid() || !packedIsShot(child.internalId())) {
        return {};
    }
    return createIndex(packedSequenceIndex(child.internalId()), 0,
                       packSequenceId(packedSequenceIndex(child.internalId())));
}

int ShotTreeItemModel::rowCount(const QModelIndex &parent) const {
    if (!owner_) {
        return 0;
    }
    if (!parent.isValid()) {
        return static_cast<int>(owner_->sequences_.size());
    }
    if (packedIsShot(parent.internalId())) {
        return 0;
    }
    const int sequenceIndex = packedSequenceIndex(parent.internalId());
    if (sequenceIndex < 0 || sequenceIndex >= static_cast<int>(owner_->sequences_.size())) {
        return 0;
    }
    return static_cast<int>(owner_->sequences_[sequenceIndex].shots.size());
}

int ShotTreeItemModel::columnCount(const QModelIndex &) const { return 1; }

QVariant ShotTreeItemModel::data(const QModelIndex &index, int role) const {
    if (!owner_ || !index.isValid()) {
        return {};
    }
    const quintptr id = index.internalId();
    if (packedIsShot(id)) {
        const int sequenceIndex = packedSequenceIndex(id);
        const int shotIndex = packedShotIndex(id);
        if (sequenceIndex < 0 || sequenceIndex >= static_cast<int>(owner_->sequences_.size())) {
            return {};
        }
        const poco::SequenceShots &sequence = owner_->sequences_[sequenceIndex];
        if (shotIndex < 0 || shotIndex >= static_cast<int>(sequence.shots.size())) {
            return {};
        }
        const poco::Shot &shot = sequence.shots[shotIndex];
        switch (role) {
        case Qt::DisplayRole:
        case NameRole:
            return QString::fromStdString(shot.name);
        case IdRole:
            return QVariant::fromValue(static_cast<qint64>(shot.id));
        case IsSequenceRole:
            return false;
        default:
            return {};
        }
    }
    const int sequenceIndex = packedSequenceIndex(id);
    if (sequenceIndex < 0 || sequenceIndex >= static_cast<int>(owner_->sequences_.size())) {
        return {};
    }
    const poco::SequenceShots &sequence = owner_->sequences_[sequenceIndex];
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return QString::fromStdString(sequence.sequence.name);
    case IdRole:
        return QVariant::fromValue(static_cast<qint64>(sequence.sequence.id));
    case IsSequenceRole:
        return true;
    default:
        return {};
    }
}

QHash<int, QByteArray> ShotTreeItemModel::roleNames() const {
    return {
        {Qt::DisplayRole, "display"},
        {IdRole, "nodeId"},
        {NameRole, "name"},
        {IsSequenceRole, "isSequence"}};
}

bool ShotTreeItemModel::hasChildren(const QModelIndex &parent) const {
    if (!owner_) {
        return false;
    }
    if (!parent.isValid()) {
        return !owner_->sequences_.empty();
    }
    if (packedIsShot(parent.internalId())) {
        return false;
    }
    const int sequenceIndex = packedSequenceIndex(parent.internalId());
    if (sequenceIndex < 0 || sequenceIndex >= static_cast<int>(owner_->sequences_.size())) {
        return false;
    }
    const poco::SequenceShots &sequence = owner_->sequences_[sequenceIndex];
    return !sequence.shots.empty();
}

// ---------------------------------------------------------------------------
// ShotTreeFilterModel
// ---------------------------------------------------------------------------

ShotTreeFilterModel::ShotTreeFilterModel(QObject *parent) : QSortFilterProxyModel(parent) {
    setRecursiveFilteringEnabled(true); ///< A matching shot keeps its sequence visible.
    setFilterCaseSensitivity(Qt::CaseInsensitive);
}

void ShotTreeFilterModel::setFilterText(const QString &text) {
    if (filter_text_ == text) {
        return;
    }
    filter_text_ = text;
    invalidateFilter();
    emit filterTextChanged();
}

bool ShotTreeFilterModel::filterAcceptsRow(
    int source_row, const QModelIndex &source_parent) const {
    if (filter_text_.isEmpty()) {
        return true;
    }
    const QAbstractItemModel *source = sourceModel();
    if (!source) {
        return true;
    }
    const QModelIndex index = source->index(source_row, 0, source_parent);
    const bool selfHit = rowMatchesFilter(index, filter_text_);
    const bool isSequence = index.data(ShotTreeItemModel::IsSequenceRole).toBool();
    if (isSequence) {
        if (selfHit) {
            return true;
        }
        // Accept the sequence when a loaded shot matches so TreeView can expand it.
        const int childCount = source->rowCount(index);
        for (int child = 0; child < childCount; ++child) {
            if (rowMatchesFilter(source->index(child, 0, index), filter_text_)) {
                return true;
            }
        }
        return false;
    }
    if (selfHit) {
        return true;
    }
    // A matching sequence (name or id) keeps every shot visible.
    return rowMatchesFilter(source_parent, filter_text_);
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ShotTree::ShotTree(plugin::StandardPlugin &panel)
    : QObject(nullptr),
      panel_(panel),
      item_model_(this),
      filter_model_(this) {
    filter_model_.setSourceModel(&item_model_);
    initQmlAttributes(panel);
    registerQmlSingleton();
}

void ShotTree::initQmlAttributes(plugin::StandardPlugin &panel) {
    sequence_ = panel.add_string_attribute("Sequence", "Sequence", "-1");
    sequence_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
    shot_ = panel.add_string_attribute("Shot", "Shot", "-1");
    shot_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
    footer_ = panel.add_string_attribute("ShotTreeFooter", "ShotTreeFooter", "");
    footer_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
    shot_tree_loaded_ = panel.add_boolean_attribute("ShotTreeLoaded", "ShotTreeLoaded", false);
    shot_tree_loaded_->expose_in_ui_attrs_group("mikros_shotbrowser_attributes");
}

void ShotTree::registerQmlSingleton() {
    qml_shot_tree = this;
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    // Separate URI so ShotTree.qml (in MikrosShotBrowser) does not import its own module.
    qmlRegisterSingletonType<ShotTree>(
        "mikros.shotbrowser.tree",
        1,
        0,
        "ShotTreeBridge",
        [](QQmlEngine *engine, QJSEngine *) -> QObject * {
            Q_ASSERT(qml_shot_tree != nullptr);
            engine->setObjectOwnership(qml_shot_tree, QQmlEngine::CppOwnership);
            return qml_shot_tree;
        });
}

QAbstractItemModel *ShotTree::treeModel() { return &filter_model_; }

QString ShotTree::filterText() const { return filter_model_.filterText(); }

void ShotTree::setFilterText(const QString &text) {
    if (filter_model_.filterText() == text) {
        return;
    }
    filter_model_.setFilterText(text);
    emit filterTextChanged();
}

// ---------------------------------------------------------------------------
// Selection. The tree writes an id. These functions store it and signal it.
// ---------------------------------------------------------------------------

void ShotTree::attribute_changed(const utility::Uuid &attr_uuid, int role) {
    if (role != module::Attribute::Value) {
        return;
    }
    if (sequence_ && attr_uuid == sequence_->uuid()) {
        const std::int64_t sequenceId = readSelectedSequenceIdFromQml();
        if (sequenceId == dal::selectedSequence() && dal::selectedShot() == -1) {
            return; ///< The QML write repeats a pair already stored.
        }
        setSelectedSequenceAndShot(sequenceId, -1); ///< A sequence click drops the shot.
        return;
    }
    if (shot_ && attr_uuid == shot_->uuid()) {
        const std::int64_t shotId = readSelectedShotIdFromQml();
        std::int64_t sequenceId = -1;
        if (shotId != -1) {
            const std::optional<poco::SequenceShots> parent = findSequenceOwningShot(shotId);
            sequenceId = parent ? parent->sequence.id : -1; ///< Parent follows the shot; missing means none.
        }
        if (sequenceId == dal::selectedSequence() && shotId == dal::selectedShot()) {
            return; ///< The QML write repeats a pair already stored.
        }
        setSelectedSequenceAndShot(sequenceId, shotId);
    }
}

std::int64_t ShotTree::convertStringToId(const std::string &text) const {
    if (text.empty() || text == "-1") {
        return -1;
    }
    try {
        return std::stoll(text);
    } catch (const std::exception &) {
        return -1; ///< A value the tree did not write is None.
    }
}

std::int64_t ShotTree::readSelectedSequenceIdFromQml() const {
    if (!sequence_) {
        return -1;
    }
    return convertStringToId(sequence_->value());
}

std::int64_t ShotTree::readSelectedShotIdFromQml() const {
    if (!shot_) {
        return -1;
    }
    return convertStringToId(shot_->value());
}

void ShotTree::setSelectedSequenceAndShot(std::int64_t sequenceId, std::int64_t shotId) {
    if (sequenceId == dal::selectedSequence() && shotId == dal::selectedShot()) {
        return; ///< The pair is already stored.
    }
    dal::setSelectedSequence(sequenceId); ///< Other screens read this id from the DAL.
    dal::setSelectedShot(shotId);
    trace::select(
        "sequence",
        sequenceId != -1,
        sequenceId == -1 ? 0 : sequenceId,
        getSequenceNameById(sequenceId));
    trace::select("shot", shotId != -1, shotId == -1 ? 0 : shotId, getShotNameById(shotId));
    sendSelectedSequenceIdAndSelectedShotIdToQml(); ///< Sync both attributes without re-entering.
    emit onSequenceAndShotSelected(sequenceId, shotId);
}

void ShotTree::sendSelectedSequenceIdAndSelectedShotIdToQml() {
    const std::string sequenceText = std::to_string(dal::selectedSequence());
    const std::string shotText = std::to_string(dal::selectedShot());
    if (sequence_ && sequence_->value() != sequenceText) {
        sequence_->set_value(sequenceText, false); ///< Do not re-enter attribute_changed.
    }
    if (shot_ && shot_->value() != shotText) {
        shot_->set_value(shotText, false);
    }
}

std::string ShotTree::getSequenceNameById(std::int64_t sequenceId) const {
    for (const poco::SequenceShots &sequence : sequences_) {
        if (sequence.sequence.id == sequenceId) {
            return sequence.sequence.name;
        }
    }
    return {};
}

std::string ShotTree::getShotNameById(std::int64_t shotId) const {
    for (const poco::SequenceShots &sequence : sequences_) {
        for (const poco::Shot &shot : sequence.shots) {
            if (shot.id == shotId) {
                return shot.name;
            }
        }
    }
    return {};
}

std::optional<poco::SequenceShots> ShotTree::findSequenceOwningShot(std::int64_t shotId) const {
    for (const poco::SequenceShots &sequence : sequences_) {
        for (const poco::Shot &shot : sequence.shots) {
            if (shot.id == shotId) {
                return sequence;
            }
        }
    }
    return std::nullopt;
}

std::optional<poco::SequenceShots> ShotTree::findSequenceById(std::int64_t sequenceId) const {
    for (const poco::SequenceShots &sequence : sequences_) {
        if (sequence.sequence.id == sequenceId) {
            return sequence;
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Sequence list. Non-empty sequences_ is reused. A new project clears it first.
// ---------------------------------------------------------------------------

bool ShotTree::checkProjectSelected() const {
    return dal::selectedProject() != -1;
}

bool ShotTree::checkBrowserModeIsShot() const {
    return dal::browserMode() == dal::BrowserMode::Shot;
}

void ShotTree::loadSequenceShotsAsync() {
    if (!sequences_.empty() || !checkProjectSelected() || !checkBrowserModeIsShot()) {
        return; ///< Tree already loaded, no project, or the asset tab is showing.
    }
    const std::uint64_t requestId = ++(*sequence_request_id_); ///< Stop the sequence fetch already running.
    const std::int64_t projectId = dal::selectedProject();
    CatalogLoadRequest pages =
        makeCatalogRequest(&panel_, CatalogKind::ShotTree, requestId, projectId);
    pages.generation = sequence_request_id_; ///< The worker stops before the next findSequenceShots.
    launchCatalogLoad(panel_.system(), std::move(pages));
    CatalogLoadRequest counts =
        makeCatalogRequest(&panel_, CatalogKind::ShotTreeCount, requestId, projectId);
    counts.generation = sequence_request_id_; ///< Drop a stale footer with the pages.
    launchCatalogLoad(panel_.system(), std::move(counts));
}

void ShotTree::clearSequenceShotsTree() {
    ++(*sequence_request_id_); ///< Drop pages that still belong to the previous project.
    clearShotTree();
}

void ShotTree::clearSelectedSequenceAndShot() {
    setSelectedSequenceAndShot(-1, -1);
}

void ShotTree::clearShotTree() {
    sequences_.clear();
    item_model_.resetFromSequences();
    clearFooterValue(); ///< Clear until the next count arrives.
    clearShotTreeLoadedFlag();
}

namespace {

/**
 * @brief Formats one count for the shot-tree footer.
 * @param total Match count.
 * @param singular Noun without a trailing `s`.
 * @return `No …s`, `1 …`, or `N …s`.
 */
std::string formatCountLabel(int total, const char *singular) {
    const std::string plural = std::string(singular) + "s";
    if (total <= 0) {
        return "No " + plural;
    }
    if (total == 1) {
        return "1 " + std::string(singular);
    }
    return std::to_string(total) + " " + plural;
}

} // namespace

void ShotTree::updateShotTreeFooter(const utility::JsonStore &payload) {
    if (!footer_) {
        return;
    }
    const int sequences = payload.value("sequences", 0);
    const int shots = payload.value("shots", 0);
    footer_->set_value(
        formatCountLabel(sequences, "sequence") + " · " + formatCountLabel(shots, "shot"));
}

void ShotTree::clearFooterValue() {
    if (footer_) {
        footer_->set_value("");
    }
}

void ShotTree::clearShotTreeLoadedFlag() {
    if (shot_tree_loaded_) {
        shot_tree_loaded_->set_value(false); ///< An empty tree is a load in progress, not a result.
    }
}

void ShotTree::markShotTreeLoaded() {
    if (shot_tree_loaded_) {
        shot_tree_loaded_->set_value(true); ///< Zero rows is still a finished load.
    }
}

void ShotTree::updateSequenceShots(bool append, const utility::JsonStore &payload) {
    const std::vector<poco::SequenceShots> rows = rowsFromJson<poco::SequenceShots>(payload);
    if (!append) {
        sequences_.clear();
    }
    for (const poco::SequenceShots &row : rows) {
        if (findSequenceById(row.sequence.id)) {
            continue; ///< This page repeats a sequence already in the tree.
        }
        sequences_.push_back(row);
    }
    sortShotsByName();
    sortSequencesByName();
    item_model_.resetFromSequences(); ///< Sort reorders roots; a full reset is the simple path.
    markShotTreeLoaded();
}

void ShotTree::sortShotsByName() {
    for (poco::SequenceShots &sequence : sequences_) {
        std::sort(sequence.shots.begin(), sequence.shots.end(), shotNameAlphabeticOrderAlgorithm);
    }
}

void ShotTree::sortSequencesByName() {
    std::sort(sequences_.begin(), sequences_.end(), sequenceNameAlphabeticOrderAlgorithm);
}

// ---------------------------------------------------------------------------
// Pages. The worker mail is accepted here, then handed to the tree.
// ---------------------------------------------------------------------------

caf::message_handler ShotTree::message_handler_extensions() {
    auto apply = [this](
                     const std::string &catalog,
                     std::uint64_t requestId,
                     bool append,
                     const utility::JsonStore &payload) {
        if (requestId != sequence_request_id_->load()) {
            trace::drop("shot_tree", requestId);
            return;
        }
        if (catalog == "shot_tree_count") {
            updateShotTreeFooter(payload);
            return;
        }
        updateSequenceShots(append, payload);
    };
    caf::detail::behavior_impl::pointer handler =
        caf::make_counted<ShotTreePageHandler>(std::move(apply));
    return caf::message_handler{std::move(handler)};
}

} // namespace xstudio::mikros_shotbrowser
