// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/thumb_fetch.cpp
 * @brief HTTP download of one version image. The plugin already links Qt.
 */

#include "model/thumb_fetch.hpp"

#include <QBuffer>
#include <QEventLoop>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace xstudio::mikros_shotbrowser::thumb {
namespace {

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

} // namespace

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

std::string toFileUrl(const std::filesystem::path &path) {
    if (path.empty()) {
        return {};
    }
    return QUrl::fromLocalFile(QString::fromStdString(path.string())).toString().toStdString();
}

} // namespace xstudio::mikros_shotbrowser::thumb
