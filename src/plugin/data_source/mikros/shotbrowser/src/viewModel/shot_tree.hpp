// SPDX-License-Identifier: Apache-2.0
/**
 * @file viewModel/shot_tree.hpp
 * @brief Sequence and shot tree: the view model of that screen.
 */
#pragma once

#include "utility/catalog.hpp"

using namespace xstudio;

#include <QAbstractItemModel>
#include <QObject>
#include <QSortFilterProxyModel>
#include <QString>

#include <caf/message_handler.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace xstudio::mikros_shotbrowser {

class ShotTree;

/**
 * @brief One shot under a sequence.
 */
struct ShotNode {
    std::int64_t id = -1; ///< Shot id.
    std::string name;     ///< Label shown in the tree.
};

/**
 * @brief One sequence and the shots already loaded under it.
 */
struct SequenceNode {
    std::int64_t id = -1;        ///< Sequence id.
    std::string name;            ///< Label shown in the tree.
    std::vector<ShotNode> shots; ///< Children. Empty until the sequence is unfolded.
    bool shots_loaded = false;   ///< True after the last shot page.
    bool shots_loading = false;  ///< True while a fetchMore load is in flight.
};

/**
 * @brief Item model indexed on ShotTree::sequences_.
 *
 * Sequences are root rows. Shots are children. Unloaded sequences report
 * hasChildren and canFetchMore so TreeView can expand and load.
 */
class ShotTreeItemModel : public QAbstractItemModel {
    Q_OBJECT

  public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        IsSequenceRole,
        ShotsLoadedRole
    };

    /**
     * @brief Binds the model to the tree that owns the sequence vector.
     * @param owner View model that holds `sequences_` and starts shot loads.
     */
    explicit ShotTreeItemModel(ShotTree *owner);

    /**
     * @brief Rebuilds every root row from `sequences_`.
     */
    void resetFromSequences();

    /**
     * @brief Replaces the visible shots under one sequence.
     *
     * Removes the current children, writes `shots` into `sequences_`, then inserts.
     * @param sequenceIndex Index in `sequences_`.
     * @param shots Sorted shots that become the new children.
     * @param shotsLoaded True when the last page has arrived.
     */
    void replaceShots(
        int sequenceIndex, std::vector<ShotNode> shots, bool shotsLoaded);

    [[nodiscard]] QModelIndex
    index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex &child) const override;
    [[nodiscard]] int
    rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] int
    columnCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant
    data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] bool
    hasChildren(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

  private:
    ShotTree *owner_ = nullptr; ///< Owns `sequences_` and shot loads.
};

/**
 * @brief Recursive name filter over the sequence and shot tree.
 */
class ShotTreeFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)

  public:
    /**
     * @brief Enables recursive filtering on the shot-tree item model.
     * @param parent QObject parent.
     */
    explicit ShotTreeFilterModel(QObject *parent = nullptr);

    /**
     * @brief Current substring filter.
     */
    [[nodiscard]] QString filterText() const { return filter_text_; }

    /**
     * @brief Sets the substring filter and invalidates the proxy.
     * @param text Case-insensitive needle. Empty shows every row.
     */
    void setFilterText(const QString &text);

  signals:
    /**
     * @brief Emitted when `filterText` changes.
     */
    void filterTextChanged();

  protected:
    /**
     * @brief Accepts a sequence or shot by name or id, including parent match for shots.
     */
    [[nodiscard]] bool
    filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override;

  private:
    QString filter_text_; ///< Substring matched against `name`.
};

/**
 * @brief View model of the sequence and shot tree.
 *
 * The tree stores ids. The QML TreeView shows names and writes ids back. This
 * class stores those ids through the DAL and emits the signals other screens subscribe to.
 */
class ShotTree : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *treeModel READ treeModel CONSTANT)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)

  public:
    /**
     * @brief Publishes the empty selection attributes and registers the tree model for QML.
     * @param panel Panel actor. Attributes are created on it, and loads are spawned through it.
     */
    explicit ShotTree(plugin::StandardPlugin &panel);

    /**
     * @brief Filtered tree model for the QML TreeView.
     */
    [[nodiscard]] QAbstractItemModel *treeModel();

    /**
     * @brief Substring filter forwarded to the proxy.
     */
    [[nodiscard]] QString filterText() const;

    /**
     * @brief Sets the substring filter shown in the tree.
     * @param text Case-insensitive needle.
     */
    void setFilterText(const QString &text);

    /**
     * @brief Stores a sequence or a shot.
     *
     * Other attributes are ignored. A value already stored does not emit.
     * @param attr_uuid Attribute the panel changed.
     * @param role Role that changed. Only Value is a choice.
     */
    void attribute_changed(const utility::Uuid &attr_uuid, int role);

    /**
     * @brief Stops the previous sequence fetch, clears the tree, then loads when the tab is Shot.
     *
     * The request id always moves. The catalog starts only when a project is selected
     * and the left tab is Shot. Returns before the pages arrive.
     */
    void loadSequenceListAsync();

    /**
     * @brief Handles a shot-tree page and declines every other catalog.
     *
     * The shell chains this with `or_else`. A declined page is left for the next handler.
     * @return The shot-tree page handler.
     */
    caf::message_handler message_handler_extensions();

  signals:
    /**
     * @brief Emitted after a sequence and shot pair is stored from the tree.
     * @param sequenceId Sequence id, or `-1` when none is selected.
     * @param shotId Shot id, or `-1` when none is selected.
     */
    void onSequenceAndShotSelected(std::int64_t sequenceId, std::int64_t shotId);

    /**
     * @brief Emitted when the tree filter text changes.
     */
    void filterTextChanged();

  private:
    friend class ShotTreeItemModel;

    /**
     * @brief Publishes the selected sequence and shot ids.
     * @param panel Panel actor that owns the attributes.
     */
    void initQmlAttributes(plugin::StandardPlugin &panel);

    /**
     * @brief Registers this instance so QML can read `treeModel`.
     */
    void registerQmlSingleton();

    /**
     * @brief Converts an attribute string to an id.
     * @param text Attribute value. Empty or `-1` means none.
     * @return The id, or `-1` when the text is not an integer.
     */
    std::int64_t convertStringToId(const std::string &text) const;

    /**
     * @brief Id of the sequence written by the tree.
     * @return The id, or `-1` when none is selected.
     */
    std::int64_t readSelectedSequenceIdFromQml() const;

    /**
     * @brief Id of the shot written by the tree.
     * @return The id, or `-1` when none is selected.
     */
    std::int64_t readSelectedShotIdFromQml() const;

    /**
     * @brief Stores the sequence and shot pair and emits `onSequenceAndShotSelected`.
     *
     * Writes both ids through the DAL, then syncs the QML attributes without re-entry.
     * A pair already stored does not emit.
     * @param sequenceId Sequence id, or `-1` when none is selected.
     * @param shotId Shot id, or `-1` when none is selected.
     */
    void setSelectedSequenceAndShot(std::int64_t sequenceId, std::int64_t shotId);

    /**
     * @brief True when a project id is stored.
     * @return True when `dal::selectedProject` is not `-1`.
     */
    bool checkProjectSelected() const;

    /**
     * @brief True when the left tab is the sequence tree.
     * @return True when `dal::browserMode` is `Shot`.
     */
    bool checkBrowserModeIsShot() const;

    /**
     * @brief Empties the tree and clears the selection.
     */
    void clearShotTree();

    /**
     * @brief Loads the shots of one unfolded sequence.
     *
     * The fetch runs to the last page. A later unfold does not stop it.
     * @param sequenceId Sequence the user unfolded.
     */
    void loadShotForSequenceAsync(std::int64_t sequenceId);

    /**
     * @brief Writes the selected sequence and shot ids without notifying.
     */
    void sendSelectedSequenceIdAndSelectedShotIdToQml();

    /**
     * @brief Name of one sequence in the current tree.
     * @param sequenceId Id to look up.
     * @return The name, or empty when the id is not in the tree.
     */
    std::string getSequenceNameById(std::int64_t sequenceId) const;

    /**
     * @brief Name of one shot in the current tree.
     * @param shotId Id to look up.
     * @return The name, or empty when the id is not in the tree.
     */
    std::string getShotNameById(std::int64_t shotId) const;

    /**
     * @brief Sequence that contains a shot id.
     * @param shotId Shot the user selected.
     * @return The parent sequence, or empty when the shot is not in the tree.
     */
    std::optional<SequenceNode> findSequenceOwningShot(std::int64_t shotId) const;

    /**
     * @brief Sequence already stored in `sequences_`.
     * @param sequenceId Sequence id.
     * @return The node, or empty when it is new.
     */
    std::optional<SequenceNode> findSequenceNodeById(std::int64_t sequenceId) const;

    /**
     * @brief Index of a sequence in `sequences_`.
     * @param sequenceId Sequence id.
     * @return The index, or `-1` when missing.
     */
    int getIndexOfSequenceNodeById(std::int64_t sequenceId) const;

    /**
     * @brief Replaces or extends the sequence nodes from one page, then sorts them.
     * @param append False for the first page, which replaces the sequence list.
     * @param payload `{id, name}` rows from the worker.
     */
    void updateSequenceList(bool append, const utility::JsonStore &payload);

    /**
     * @brief Adds one page of shots into that sequence, then sorts those shots.
     * @param sequenceId Sequence the page belongs to.
     * @param append False for the first page, which replaces that sequence's shots.
     * @param payload `{id, name}` rows from the worker.
     */
    void updateShotListForSequence(
        std::int64_t sequenceId, bool append, const utility::JsonStore &payload);

    /**
     * @brief Sorts every sequence by name.
     */
    void sortSequencesByName();

    /**
     * @brief Sorts shots by name.
     * @param shots Shot list to reorder.
     */
    void sortShotsByName(std::vector<ShotNode> &shots) const;

    plugin::StandardPlugin &panel_; ///< Panel actor. Attributes and loads go through it.
    std::vector<SequenceNode> sequences_; ///< Source of truth for the tree.
    ShotTreeItemModel item_model_;        ///< Indexes `sequences_` for the TreeView.
    ShotTreeFilterModel filter_model_;    ///< Recursive name filter over `item_model_`.
    RequestGeneration sequence_request_id_ =
        std::make_shared<std::atomic<std::uint64_t>>(0); ///< Stops the next sequence fetch when it moves.
    std::function<void(std::uint64_t, bool, const utility::JsonStore &)> sequence_page_; ///< Applies one sequence page.
    std::function<void(std::int64_t, bool, const utility::JsonStore &)> shot_page_; ///< Applies one shot page. It does not stop.
    module::StringAttribute *sequence_ = nullptr; ///< Selected sequence id, or `-1`.
    module::StringAttribute *shot_ = nullptr;     ///< Selected shot id, or `-1`.
};

} // namespace xstudio::mikros_shotbrowser
