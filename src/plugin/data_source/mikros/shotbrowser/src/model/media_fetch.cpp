// SPDX-License-Identifier: Apache-2.0
/**
 * @file model/media_fetch.cpp
 * @brief HTTP download of one version movie. Runs off the CAF scheduler.
 */

#include "model/media_fetch.hpp"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace xstudio::mikros_shotbrowser {

std::string fetchMovie(const std::string &url) {
    QEventLoop loop;
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(QString::fromStdString(url))};
    request.setTransferTimeout(120000); ///< A movie is larger than a thumbnail.
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = manager.get(request);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec(); ///< Wait in the open thread. The panel actor stays free.
    std::string body;
    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray bytes = reply->readAll();
        body.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    }
    delete reply;
    return body;
}

} // namespace xstudio::mikros_shotbrowser
