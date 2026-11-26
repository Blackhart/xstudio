// SPDX-License-Identifier: Apache-2.0
/**
 * @file utility/thumbnails.cpp
 * @brief Disk cache and JPEG download sharing Shot Browser's thumb root.
 */

#include "utility/thumbnails.hpp"

#include "model/log.hpp"

#include <QBuffer>
#include <QEventLoop>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <filesystem>
#include <fstream>
#include <system_error>
#include <thread>
#include <utility>

namespace xstudio::mikros_media_details {
namespace {

namespace trace = log;

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
 * @brief What the resolver does with one version image.
 */
enum class ThumbKind {
    None,  ///< Show the placeholder.
    Ready, ///< `path` is a file QML can show.
    Fetch  ///< Download the URL and write `path`.
};

/**
 * @brief Decision for one version image.
 */
struct ThumbPlan {
    ThumbKind kind = ThumbKind::None; ///< Next step.
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
 * @param root Cache directory.
 * @param image URL or filesystem path. Empty means no picture.
 * @param provider `http`, `file`, or anything else.
 * @param projectId Numeric project id.
 * @param versionId Numeric version id.
 * @return The step for this version.
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
    // Unknown provider: still prefer a shared-cache hit when present.
    const ThumbPlan cached = planHttp(root, projectId, versionId);
    if (cached.kind == ThumbKind::Ready) {
        return cached;
    }
    if (image.rfind("http://", 0) == 0 || image.rfind("https://", 0) == 0) {
        ThumbPlan plan = cached;
        if (plan.path.empty()) {
            return {};
        }
        plan.kind = ThumbKind::Fetch;
        return plan;
    }
    return planFile(image);
}

/**
 * @brief Writes `bytes` to `path` when the file is absent or empty.
 * @param path Destination, including `thumbnail.jpg`.
 * @param bytes Image bytes already checked by the caller.
 * @return True when the file is non-empty afterwards.
 */
bool writeBytesIfMissing(const std::filesystem::path &path, const std::string &bytes) {
    if (fileHasBytes(path)) {
        return true;
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
 * @return JPEG bytes, or empty when the request fails.
 */
std::string fetchJpeg(const std::string &url) {
    QEventLoop loop;
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(QString::fromStdString(url))};
    request.setTransferTimeout(15000);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = manager.get(request);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    QByteArray body;
    if (reply->error() == QNetworkReply::NoError) {
        body = reply->readAll();
    }
    delete reply;
    return encodeJpeg(body);
}

/**
 * @brief Builds the URL a QML `Image` can load.
 * @param path Local file.
 * @return `file://` URL. Empty when `path` is empty.
 */
std::string toFileUrl(const std::filesystem::path &path) {
    if (path.empty()) {
        return {};
    }
    return QUrl::fromLocalFile(QString::fromStdString(path.string())).toString().toStdString();
}

} // namespace

namespace thumb {

std::filesystem::path thumbRoot() {
    const std::filesystem::path home = homeDirectory();
    if (home.empty()) {
        return {};
    }
    // Same directory as Shot Browser so both plugins share disk cache.
    return home / "xStudio" / "mikros_shotbrowser" / "thumbs";
}

} // namespace thumb

void scheduleOneThumb(
    std::int64_t projectId,
    std::int64_t versionId,
    std::string image,
    std::string imageProvider,
    std::function<void(std::string fileUrl)> onReady) {
    if (!onReady || projectId <= 0 || versionId <= 0) {
        return;
    }
    const std::filesystem::path root = thumb::thumbRoot();
    const ThumbPlan cached = planHttp(root, projectId, versionId);
    if (cached.kind == ThumbKind::Ready) {
        trace::info(
            "thumb projectId=" + std::to_string(projectId) + " versionId=" +
            std::to_string(versionId) + " origin=cache");
        onReady(toFileUrl(cached.path));
        return;
    }
    std::thread([projectId,
                 versionId,
                 root,
                 image = std::move(image),
                 imageProvider = std::move(imageProvider),
                 onReady = std::move(onReady)]() mutable {
        const ThumbPlan plan = planThumb(root, image, imageProvider, projectId, versionId);
        if (plan.kind == ThumbKind::Ready) {
            const char *origin = imageProvider == "file" ? "file" : "cache";
            trace::info(
                "thumb projectId=" + std::to_string(projectId) + " versionId=" +
                std::to_string(versionId) + " origin=" + origin);
            onReady(toFileUrl(plan.path));
            return;
        }
        if (plan.kind != ThumbKind::Fetch) {
            trace::info(
                "thumb projectId=" + std::to_string(projectId) + " versionId=" +
                std::to_string(versionId) + " origin=none");
            return;
        }
        const bool stored = writeBytesIfMissing(plan.path, fetchJpeg(image));
        trace::info(
            "thumb projectId=" + std::to_string(projectId) + " versionId=" +
            std::to_string(versionId) + " origin=" + (stored ? "download" : "none"));
        if (stored) {
            onReady(toFileUrl(plan.path));
        }
    }).detach();
}

} // namespace xstudio::mikros_media_details
