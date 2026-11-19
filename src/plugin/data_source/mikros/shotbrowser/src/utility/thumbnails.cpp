// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/thumbnails.cpp
 * @brief Disk cache and JPEG download. Cards read the stored URL and do not download.
 */

#include "utility/thumbnails.hpp"

#include "model/dal/dal.hpp"
#include "model/log.hpp"
#include "utility/catalog.hpp"

#include <QBuffer>
#include <QEventLoop>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <thread>


namespace trace = xstudio::mikros_shotbrowser::log;

using namespace xstudio;
using namespace xstudio::mikros_shotbrowser;

namespace xstudio::mikros_shotbrowser {

namespace thumb {

/**
 * @brief Directory that holds every version thumbnail.
 * @return `${HOME}/xStudio/mikros_shotbrowser/thumbs`, or empty when home is missing.
 */
std::filesystem::path thumbRoot() {
    const std::filesystem::path home = homeDirectory();
    if (home.empty()) {
        return {};
    }
    return home / "xStudio" / "mikros_shotbrowser" / "thumbs";
}

/**
 * @brief Cache file of one version.
 * @param root Directory from `thumbRoot`.
 * @param projectId Numeric project id.
 * @param versionId Numeric version id.
 * @return `<root>/<projectId>/<versionId>/thumbnail.jpg`.
 */
std::filesystem::path cacheFile(
    const std::filesystem::path &root, std::int64_t projectId, std::int64_t versionId) {
    return root / std::to_string(projectId) / std::to_string(versionId) / "thumbnail.jpg";
}

/**
 * @brief True when `path` is a non-empty regular file.
 * @param path File to test.
 * @return True when the file exists and its size is greater than zero.
 */
bool fileHasBytes(const std::filesystem::path &path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return false;
    }
    return std::filesystem::file_size(path, error) > 0 && !error;
}

/**
 * @brief What the page worker does with one version image.
 */
enum class ThumbKind {
    None,  ///< Show the placeholder. Nothing is written.
    Ready, ///< `path` is a file the card can show.
    Fetch  ///< Download the URL and write `path`.
};

/**
 * @brief Decision for one version image.
 */
struct ThumbPlan {
    ThumbKind kind = ThumbKind::None; ///< Next step for this version.
    std::filesystem::path path;       ///< File to show, or the cache file to write.
};

/**
 * @brief Plan for an image that is already a local file.
 * @param image Filesystem path from the version.
 * @return `Ready` when the file has bytes, otherwise `None`.
 */
ThumbPlan planFile(const std::string &image) {
    const std::filesystem::path path(image);
    if (!fileHasBytes(path)) {
        return {};
    }
    ThumbPlan plan;
    plan.kind = ThumbKind::Ready;
    plan.path = path;
    return plan;
}

/**
 * @brief Plan for an image that must be downloaded once.
 * @param root Cache directory. Empty means the image cannot be stored.
 * @param projectId Numeric project id.
 * @param versionId Numeric version id.
 * @return `Ready` when `thumbnail.jpg` is already there, otherwise `Fetch`.
 */
ThumbPlan planHttp(
    const std::filesystem::path &root, std::int64_t projectId, std::int64_t versionId) {
    if (root.empty()) {
        return {};
    }
    ThumbPlan plan;
    plan.path = cacheFile(root, projectId, versionId);
    plan.kind = fileHasBytes(plan.path) ? ThumbKind::Ready : ThumbKind::Fetch;
    return plan;
}

/**
 * @brief Chooses placeholder, direct file, cache hit, or download.
 * @param root Cache directory. Empty means an `http` image cannot be stored.
 * @param image URL or filesystem path from the version. Empty means no picture.
 * @param provider `http`, `file`, or anything else.
 * @param projectId Numeric project id.
 * @param versionId Numeric version id.
 * @return The step for this version. A present cache file is `Ready`, not `Fetch`.
 */
ThumbPlan planThumb(
    const std::filesystem::path &root,
    const std::string &image,
    const std::string &provider,
    std::int64_t projectId,
    std::int64_t versionId) {
    if (image.empty()) {
        return {};
    }
    if (provider == "file") {
        return planFile(image);
    }
    if (provider == "http") {
        return planHttp(root, projectId, versionId);
    }
    return {};
}

/**
 * @brief Writes `bytes` to `path` when the file is absent or empty.
 * @param path Destination, including `thumbnail.jpg`.
 * @param bytes Image bytes already checked by the caller.
 * @return True when the file is non-empty afterwards. False when `bytes` is empty.
 */
bool writeBytesIfMissing(const std::filesystem::path &path, const std::string &bytes) {
    if (fileHasBytes(path)) {
        return true; ///< Another page already stored this version.
    }
    if (bytes.empty()) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    return fileHasBytes(path);
}

/**
 * @brief Reads the reply body once the request has finished.
 * @param reply Finished reply. The caller deletes it.
 * @return Body bytes, or empty when the request failed.
 */
QByteArray readBody(QNetworkReply *reply) {
    if (reply->error() != QNetworkReply::NoError) {
        return {};
    }
    return reply->readAll();
}

/**
 * @brief Encodes a decoded image as JPEG.
 * @param body Bytes returned by the server.
 * @return JPEG bytes, or empty when `body` is not an image.
 */
std::string encodeJpeg(const QByteArray &body) {
    QImage image;
    if (!image.loadFromData(body)) {
        return {};
    }
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "JPEG")) {
        return {};
    }
    return std::string(jpeg.constData(), static_cast<std::size_t>(jpeg.size()));
}

/**
 * @brief Downloads `url` and returns JPEG bytes.
 * @param url Address from the version `image` field.
 * @return JPEG bytes, or empty when the request fails or the body is not an image.
 */
std::string fetchJpeg(const std::string &url) {
    QEventLoop loop;
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(QString::fromStdString(url))};
    request.setTransferTimeout(15000); ///< One slow host must not stall the page worker.
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = manager.get(request);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec(); ///< Wait in this worker thread. The actor stays free.
    const QByteArray body = readBody(reply);
    delete reply;
    return encodeJpeg(body);
}

/**
 * @brief Builds the URL a QML `Image` can load.
 * @param path Local file, either the cache or a `file` provider path.
 * @return `file://` URL. Empty when `path` is empty.
 */
std::string toFileUrl(const std::filesystem::path &path) {
    if (path.empty()) {
        return {};
    }
    return QUrl::fromLocalFile(QString::fromStdString(path.string())).toString().toStdString();
}

} // namespace thumb

} // namespace xstudio::mikros_shotbrowser

namespace xstudio::mikros_shotbrowser {

void scheduleOneThumb(
    std::int64_t projectId,
    std::int64_t versionId,
    std::function<void(std::string fileUrl)> onReady) {
    if (!onReady) {
        return;
    }
    const std::filesystem::path root = thumb::thumbRoot();
    const thumb::ThumbPlan cached =
        thumb::planHttp(root, projectId, versionId); ///< Skip the find when the cache already has it.
    if (cached.kind == thumb::ThumbKind::Ready) {
        trace::info(
            "thumb projectId=" + std::to_string(projectId) + " versionId=" +
            std::to_string(versionId) + " origin=cache");
        onReady(thumb::toFileUrl(cached.path));
        return;
    }
    std::thread([projectId, versionId, root, onReady = std::move(onReady)]() {
        const poco::VersionThumb row = dal::findVersionThumb(versionId);
        if (row.image.empty()) {
            trace::info(
                "thumb projectId=" + std::to_string(projectId) + " versionId=" +
                std::to_string(versionId) + " origin=none");
            return;
        }
        const thumb::ThumbPlan plan =
            thumb::planThumb(root, row.image, row.image_provider, projectId, versionId);
        if (plan.kind == thumb::ThumbKind::Ready) {
            const char *origin = row.image_provider == "file" ? "file" : "cache";
            trace::info(
                "thumb projectId=" + std::to_string(projectId) + " versionId=" +
                std::to_string(versionId) + " origin=" + origin);
            onReady(thumb::toFileUrl(plan.path));
            return;
        }
        if (plan.kind != thumb::ThumbKind::Fetch) {
            trace::info(
                "thumb projectId=" + std::to_string(projectId) + " versionId=" +
                std::to_string(versionId) + " origin=none");
            return;
        }
        const bool stored = thumb::writeBytesIfMissing(plan.path, thumb::fetchJpeg(row.image));
        trace::info(
            "thumb projectId=" + std::to_string(projectId) + " versionId=" +
            std::to_string(versionId) + " origin=" + (stored ? "download" : "none"));
        if (!stored) {
            return;
        }
        onReady(thumb::toFileUrl(plan.path));
    }).detach();
}

} // namespace xstudio::mikros_shotbrowser

namespace xstudio::mikros_shotbrowser {

namespace {

/**
 * @brief File URLs already resolved for one process.
 *
 * The files live in one thumbnail directory. The key is the project and the version.
 * @return The map pages read and the thumbnail mail writes.
 */
std::map<std::pair<std::int64_t, std::int64_t>, std::string> &storedThumbPaths() {
    static std::map<std::pair<std::int64_t, std::int64_t>, std::string> paths;
    return paths; ///< Shared by the version cards and the playlist rows.
}

} // namespace

std::string thumbFor(std::int64_t projectId, std::int64_t versionId) {
    const auto found = storedThumbPaths().find({projectId, versionId});
    if (found == storedThumbPaths().end()) {
        return {};
    }
    return found->second;
}

void rememberThumbs(std::int64_t projectId, const utility::JsonStore &payload) {
    if (!payload.is_array()) {
        return;
    }
    for (const auto &entry : payload) {
        if (!entry.contains("versionId") || !entry.contains("image")) {
            continue;
        }
        const std::string image = entry["image"].get<std::string>();
        if (image.empty()) {
            continue;
        }
        storedThumbPaths()[{projectId, entry["versionId"].get<std::int64_t>()}] = image;
    }
}


} // namespace xstudio::mikros_shotbrowser
