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
#include <utility>

namespace trace = xstudio::mikros_shotbrowser::log;

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace xstudio::mikros_shotbrowser {

namespace {

ShotTree *qml_shot_tree = nullptr; ///< Instance exposed to QML as ShotTreeBridge.

using TreePage = std::function<void(
    std::uint64_t,
    const std::string &,
    std::int64_t,
    bool,
    std::int64_t,
    const utility::JsonStore &)>;

/**
 * @brief Applies a shot-tree page and declines every other catalog.
 *
 * `or_else` tries the next handler only when `invoke` returns false. Returning
 * `caf::skip` from a matched functor still consumes the message, so a page for
 * another catalog is declined by returning false.
 */
class ShotTreePageHandler : public caf::detail::behavior_impl {
  public:
    /**
     * @brief Stores the lambda that applies one shot-tree page.
     * @param apply Called on the panel actor for a current shot-tree page.
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
        if (catalog != "shot_tree" && catalog != "sequence_shots") {
            return false; ///< Another catalog: the shell handler continues.
        }
        apply_(
            caf::get<1>(view),
            catalog,
            caf::get<3>(view),
            caf::get<12>(view),
            caf::get<5>(view),
            caf::get<13>(view));
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
bool sequenceNameAlphabeticOrderAlgorithm(const SequenceNode &left, const SequenceNode &right) {
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
    return left.id < right.id;
}

/**
 * @brief Orders one shot before another by name.
 * @param left First shot.
 * @param right Second shot.
 * @return True when `left` belongs before `right`.
 */
bool shotNameAlphabeticOrderAlgorithm(const ShotNode &left, const ShotNode &right) {
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

void ShotTreeItemModel::replaceShots(
    int sequenceIndex, std::vector<ShotNode> shots, bool shotsLoaded) {
    if (!owner_ || sequenceIndex < 0 ||
        sequenceIndex >= static_cast<int>(owner_->sequences_.size())) {
        return;
    }
    SequenceNode &sequence = owner_->sequences_[sequenceIndex];
    const QModelIndex parent = index(sequenceIndex, 0, QModelIndex());
    const int previousShotCount = static_cast<int>(sequence.shots.size());
    if (previousShotCount > 0) {
        beginRemoveRows(parent, 0, previousShotCount - 1);
        sequence.shots.clear();
        endRemoveRows();
    }
    const int newCount = static_cast<int>(shots.size());
    if (newCount > 0) {
        beginInsertRows(parent, 0, newCount - 1);
        sequence.shots = std::move(shots);
        endInsertRows();
    } else {
        sequence.shots.clear();
    }
    sequence.shots_loaded = shotsLoaded;
    sequence.shots_loading = false;
    emit dataChanged(parent, parent, {ShotsLoadedRole});
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
        const SequenceNode &sequence = owner_->sequences_[sequenceIndex];
        if (shotIndex < 0 || shotIndex >= static_cast<int>(sequence.shots.size())) {
            return {};
        }
        const ShotNode &shot = sequence.shots[shotIndex];
        switch (role) {
        case Qt::DisplayRole:
        case NameRole:
            return QString::fromStdString(shot.name);
        case IdRole:
            return QVariant::fromValue(static_cast<qint64>(shot.id));
        case IsSequenceRole:
            return false;
        case ShotsLoadedRole:
            return true;
        default:
            return {};
        }
    }
    const int sequenceIndex = packedSequenceIndex(id);
    if (sequenceIndex < 0 || sequenceIndex >= static_cast<int>(owner_->sequences_.size())) {
        return {};
    }
    const SequenceNode &sequence = owner_->sequences_[sequenceIndex];
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return QString::fromStdString(sequence.name);
    case IdRole:
        return QVariant::fromValue(static_cast<qint64>(sequence.id));
    case IsSequenceRole:
        return true;
    case ShotsLoadedRole:
        return sequence.shots_loaded;
    default:
        return {};
    }
}

QHash<int, QByteArray> ShotTreeItemModel::roleNames() const {
    return {
        {Qt::DisplayRole, "display"},
        {IdRole, "nodeId"},
        {NameRole, "name"},
        {IsSequenceRole, "isSequence"},
        {ShotsLoadedRole, "shotsLoaded"}};
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
    const SequenceNode &sequence = owner_->sequences_[sequenceIndex];
    return !sequence.shots_loaded || !sequence.shots.empty();
}

bool ShotTreeItemModel::canFetchMore(const QModelIndex &parent) const {
    if (!owner_ || !parent.isValid() || packedIsShot(parent.internalId())) {
        return false;
    }
    const int sequenceIndex = packedSequenceIndex(parent.internalId());
    if (sequenceIndex < 0 || sequenceIndex >= static_cast<int>(owner_->sequences_.size())) {
        return false;
    }
    const SequenceNode &sequence = owner_->sequences_[sequenceIndex];
    return !sequence.shots_loaded && !sequence.shots_loading;
}

void ShotTreeItemModel::fetchMore(const QModelIndex &parent) {
    if (!canFetchMore(parent) || !owner_) {
        return;
    }
    const int sequenceIndex = packedSequenceIndex(parent.internalId());
    SequenceNode &sequence = owner_->sequences_[sequenceIndex];
    sequence.shots_loading = true; ///< Stop TreeView from calling fetchMore again.
    owner_->loadShotForSequenceAsync(sequence.id);
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
            const std::optional<SequenceNode> parent = findSequenceOwningShot(shotId);
            sequenceId = parent ? parent->id : -1; ///< Parent follows the shot; missing means none.
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
    for (const SequenceNode &sequence : sequences_) {
        if (sequence.id == sequenceId) {
            return sequence.name;
        }
    }
    return {};
}

std::string ShotTree::getShotNameById(std::int64_t shotId) const {
    for (const SequenceNode &sequence : sequences_) {
        for (const ShotNode &shot : sequence.shots) {
            if (shot.id == shotId) {
                return shot.name;
            }
        }
    }
    return {};
}

std::optional<SequenceNode> ShotTree::findSequenceOwningShot(std::int64_t shotId) const {
    for (const SequenceNode &sequence : sequences_) {
        for (const ShotNode &shot : sequence.shots) {
            if (shot.id == shotId) {
                return sequence;
            }
        }
    }
    return std::nullopt;
}

std::optional<SequenceNode> ShotTree::findSequenceNodeById(std::int64_t sequenceId) const {
    for (const SequenceNode &sequence : sequences_) {
        if (sequence.id == sequenceId) {
            return sequence;
        }
    }
    return std::nullopt;
}

int ShotTree::getIndexOfSequenceNodeById(std::int64_t sequenceId) const {
    for (int index = 0; index < static_cast<int>(sequences_.size()); ++index) {
        if (sequences_[index].id == sequenceId) {
            return index;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Sequence list. A load clears the tree, then each page extends it.
// ---------------------------------------------------------------------------

bool ShotTree::checkProjectSelected() const {
    return dal::selectedProject() != -1;
}

bool ShotTree::checkBrowserModeIsShot() const {
    return dal::browserMode() == dal::BrowserMode::Shot;
}

void ShotTree::loadSequenceListAsync() {
    const std::uint64_t requestId = ++(*sequence_request_id_); ///< Stop the sequence fetch already running.
    clearShotTree(); ///< The previous project's sequences and shots leave the screen.
    if (!checkProjectSelected() || !checkBrowserModeIsShot()) {
        return; ///< No project, or the asset tab is showing.
    }
    sequence_page_ = [this](std::uint64_t id, bool append, const utility::JsonStore &payload) {
        if (id != sequence_request_id_->load()) {
            trace::drop("shot_tree", id);
            return;
        }
        updateSequenceList(append, payload);
    };
    CatalogLoadRequest request =
        makeCatalogRequest(&panel_, CatalogKind::ShotTree, requestId, dal::selectedProject());
    request.generation = sequence_request_id_; ///< The worker stops before the next findSequences.
    launchCatalogLoad(panel_.system(), std::move(request));
}

void ShotTree::clearShotTree() {
    sequences_.clear();
    item_model_.resetFromSequences();
    setSelectedSequenceAndShot(-1, -1); ///< Drop the pair with the previous project's tree.
}

void ShotTree::loadShotForSequenceAsync(std::int64_t sequenceId) {
    shot_page_ = [this](std::int64_t id, bool append, const utility::JsonStore &payload) {
        updateShotListForSequence(id, append, payload); ///< A shot fetch is not stopped by a newer one.
    };
    CatalogLoadRequest request = makeCatalogRequest(
        &panel_,
        CatalogKind::SequenceShots,
        sequence_request_id_->load(),
        dal::selectedProject());
    request.sequence_id = sequenceId;
    launchCatalogLoad(panel_.system(), std::move(request));
}

void ShotTree::updateSequenceList(bool append, const utility::JsonStore &payload) {
    const std::vector<poco::Sequence> rows = rowsFromJson<poco::Sequence>(payload);
    const std::vector<SequenceNode> previous = sequences_;
    if (!append) {
        sequences_.clear();
    }
    for (const poco::Sequence &row : rows) {
        if (findSequenceNodeById(row.id)) {
            continue; ///< This page repeats a sequence already in the tree.
        }
        SequenceNode node;
        node.id = row.id;
        node.name = row.name;
        for (const SequenceNode &kept : previous) {
            if (kept.id != row.id) {
                continue;
            }
            node = kept; ///< Keep shots already unfolded on this sequence.
            break;
        }
        sequences_.push_back(std::move(node));
    }
    sortSequencesByName();
    item_model_.resetFromSequences(); ///< Sort reorders roots; a full reset is the simple path.
}

void ShotTree::updateShotListForSequence(
    std::int64_t sequenceId, bool append, const utility::JsonStore &payload) {
    const std::vector<poco::Shot> rows = rowsFromJson<poco::Shot>(payload);
    const int sequenceIndex = getIndexOfSequenceNodeById(sequenceId);
    if (sequenceIndex < 0) {
        return;
    }
    SequenceNode &sequence = sequences_[sequenceIndex];
    std::vector<ShotNode> shots = append ? sequence.shots : std::vector<ShotNode>{};
    for (const poco::Shot &row : rows) {
        shots.push_back(ShotNode{row.id, row.name});
    }
    sortShotsByName(shots); ///< Only the shots of this sequence move.
    const bool shotsLoaded =
        sequence.shots_loaded || rows.size() < dal::readPageLimit(); ///< A short page is the last.
    item_model_.replaceShots(sequenceIndex, std::move(shots), shotsLoaded);
}

void ShotTree::sortSequencesByName() {
    std::sort(sequences_.begin(), sequences_.end(), sequenceNameAlphabeticOrderAlgorithm);
}

void ShotTree::sortShotsByName(std::vector<ShotNode> &shots) const {
    std::sort(shots.begin(), shots.end(), shotNameAlphabeticOrderAlgorithm);
}

// ---------------------------------------------------------------------------
// Pages. The worker mail is accepted here, then handed to the tree.
// ---------------------------------------------------------------------------

caf::message_handler ShotTree::message_handler_extensions() {
    auto apply = [this](
                     std::uint64_t requestId,
                     const std::string &catalog,
                     std::int64_t projectId,
                     bool append,
                     std::int64_t sequenceId,
                     const utility::JsonStore &payload) {
        if (dal::selectedProject() != projectId) {
            trace::drop(catalog.c_str(), requestId); ///< The user picked another project.
            return;
        }
        if (catalog == "shot_tree") {
            if (sequence_page_) {
                sequence_page_(requestId, append, payload);
            }
            return;
        }
        if (shot_page_) {
            shot_page_(sequenceId, append, payload); ///< Shot pages are not dropped when the sequence id moves.
        }
    };
    caf::detail::behavior_impl::pointer handler =
        caf::make_counted<ShotTreePageHandler>(std::move(apply));
    return caf::message_handler{std::move(handler)};
}

} // namespace xstudio::mikros_shotbrowser
