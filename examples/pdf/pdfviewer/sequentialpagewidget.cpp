/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtPDF module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL3$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or later as published by the Free
** Software Foundation and appearing in the file LICENSE.GPL included in
** the packaging of this file. Please review the following information to
** ensure the GNU General Public License version 2.0 requirements will be
** met: http://www.gnu.org/licenses/gpl-2.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "sequentialpagewidget.h"
#include "pagerenderer.h"
#include <QPaintEvent>
#include <QPainter>
#include <QPdfDocument>
#include <QGuiApplication>
#include <QScreen>
#include <QLoggingCategory>
#include <QElapsedTimer>

Q_DECLARE_LOGGING_CATEGORY(lcExample)

SequentialPageWidget::SequentialPageWidget(QWidget *parent)
    : QWidget(parent)
    , m_pageCacheLimit(20)
    , m_pageRenderer(new PageRenderer())
    , m_background(Qt::darkGray)
    , m_placeholderIcon(":icons/images/busy.png")
    , m_placeholderBackground(Qt::white)
    , m_pageSpacing(3)
    , m_topPageShowing(0)
    , m_zoom(1.)
    , m_screenResolution(QGuiApplication::primaryScreen()->logicalDotsPerInch() / 72.0)
    , m_document(nullptr)
{
    connect(m_pageRenderer, SIGNAL(pageReady(int, qreal, QImage)), this, SLOT(pageLoaded(int, qreal, QImage)), Qt::QueuedConnection);
    grabGesture(Qt::SwipeGesture);
}

SequentialPageWidget::~SequentialPageWidget()
{
    delete m_pageRenderer;
}

void SequentialPageWidget::setDocument(QPdfDocument *document)
{
    m_pageRenderer->setDocument(document);

    m_document = document;
    connect(m_document, &QPdfDocument::statusChanged, this, &SequentialPageWidget::documentStatusChanged);

    documentStatusChanged();
}

void SequentialPageWidget::setZoom(qreal factor)
{
    m_zoom = factor;
    emit zoomChanged(factor);
    invalidate();
}

QSizeF SequentialPageWidget::pageSize(int page)
{
//    if (!m_pageSizes.length() <= page)
//        return QSizeF();
    return m_pageSizes[page] * m_screenResolution * m_zoom;
}

void SequentialPageWidget::invalidate()
{
    QSizeF totalSize(0, m_pageSpacing);
    for (int page = 0; page < pageCount(); ++page) {
        QSizeF size = pageSize(page);
        totalSize.setHeight(totalSize.height() + size.height());
        if (size.width() > totalSize.width())
            totalSize.setWidth(size.width());
    }
    m_totalSize = totalSize.toSize();
    setMinimumSize(m_totalSize);
    emit zoomChanged(m_zoom);
    qCDebug(lcExample) << "total size" << m_totalSize;
    m_pageCache.clear();
    update();
}

void SequentialPageWidget::documentStatusChanged()
{
    m_pageSizes.clear();
    m_topPageShowing = 0;

    if (m_document->status() == QPdfDocument::Ready) {
        for (int page = 0; page < m_document->pageCount(); ++page)
            m_pageSizes.append(m_document->pageSize(page));
    }

    invalidate();
}

void SequentialPageWidget::pageLoaded(int page, qreal zoom, QImage image)
{
    Q_UNUSED(zoom)
    if (m_cachedPagesLRU.length() > m_pageCacheLimit)
        m_pageCache.remove(m_cachedPagesLRU.takeFirst());
    m_pageCache.insert(page, image);
    m_cachedPagesLRU.append(page);
    update();
}

int SequentialPageWidget::pageCount()
{
    return m_pageSizes.count();
}

void SequentialPageWidget::paintEvent(QPaintEvent * event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), m_background);

    if (m_pageSizes.isEmpty())
        return;

    // Find the first page that needs to be rendered
    int page = 0;
    int y = 0;
    while (page < pageCount()) {
        QSizeF size = pageSize(page);
        int height = size.toSize().height();
        if (y + height >= event->rect().top())
            break;
        y += height + m_pageSpacing;
        ++page;
    }
    y += m_pageSpacing;
    m_topPageShowing = page;

    // Actually render pages
    while (y < event->rect().bottom() && page < pageCount()) {
        QSizeF size = pageSize(page);
        if (m_pageCache.contains(page)) {
            const QImage &img = m_pageCache[page];
            painter.fillRect((width() - img.width()) / 2, y, size.width(), size.height(), Qt::white);
            painter.drawImage((width() - img.width()) / 2, y, img);
        } else {
            painter.fillRect((width() - size.width()) / 2, y, size.width(), size.height(), m_placeholderBackground);
            painter.drawPixmap((size.width() - m_placeholderIcon.width()) / 2,
                               (size.height() - m_placeholderIcon.height()) / 2, m_placeholderIcon);
            m_pageRenderer->requestPage(page, m_screenResolution * m_zoom);
        }
        y += size.height() + m_pageSpacing;
        ++page;
    }
    m_bottomPageShowing = page - 1;
    emit showingPageRange(m_topPageShowing, m_bottomPageShowing);
}

qreal SequentialPageWidget::yForPage(int endPage)
{
    // TODO maybe put this loop into a page iterator class
    int y = m_pageSpacing;
    for (int page = 0; page < pageCount() && page < endPage; ++page) {
        QSizeF size = pageSize(page);
        int height = size.toSize().height();
        y += height + m_pageSpacing;
    }
    return y;
}
