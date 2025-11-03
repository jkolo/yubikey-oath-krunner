/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "processing_overlay.h"
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QPalette>
#include <QColor>

namespace YubiKeyOath {
namespace Daemon {

ProcessingOverlay::ProcessingOverlay(QWidget *parent)
    : QWidget(parent)
    , m_animationTimer(new QTimer(this))
{
    setupUi();

    // Configure animation timer
    m_animationTimer->setInterval(500); // 500ms per dot
    connect(m_animationTimer, &QTimer::timeout, this, &ProcessingOverlay::onAnimationTick);

    // Initially hidden
    QWidget::hide();
}

void ProcessingOverlay::setupUi()
{
    // Enable semi-transparent background
    setAutoFillBackground(true);

    // Set semi-transparent background using default palette color
    QPalette overlayPalette = palette();
    QColor bgColor = overlayPalette.color(QPalette::Window);
    bgColor.setAlpha(200); // Semi-transparent (0-255, 200 = ~78% opacity)
    overlayPalette.setColor(QPalette::Window, bgColor);
    setPalette(overlayPalette);

    // Main layout - center content
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setAlignment(Qt::AlignCenter);
    mainLayout->setContentsMargins(40, 40, 40, 40);
    mainLayout->setSpacing(20);

    // Container for status label with independent opacity
    auto *labelContainer = new QWidget(this);
    labelContainer->setStyleSheet(QStringLiteral("background-color: transparent;"));
    auto *labelLayout = new QVBoxLayout(labelContainer);
    labelLayout->setContentsMargins(0, 0, 0, 0);

    // Horizontal layout for text + dots (so text doesn't move)
    auto *textRow = new QWidget(labelContainer);
    textRow->setStyleSheet(QStringLiteral("background-color: transparent;"));
    auto *textLayout = new QHBoxLayout(textRow);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(0);
    textLayout->setAlignment(Qt::AlignCenter);

    // Status label (main text - doesn't change during animation)
    m_statusLabel = new QLabel(textRow);
    m_statusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_statusLabel->setStyleSheet(QStringLiteral(
        "QLabel { "
        "  font-size: 16pt; "
        "  font-weight: bold; "
        "  background-color: transparent; "
        "}"
    ));
    textLayout->addWidget(m_statusLabel);

    // Dots label (animated 0-3 dots with fixed width)
    m_dotsLabel = new QLabel(textRow);
    m_dotsLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_dotsLabel->setStyleSheet(QStringLiteral(
        "QLabel { "
        "  font-size: 16pt; "
        "  font-weight: bold; "
        "  background-color: transparent; "
        "}"
    ));
    m_dotsLabel->setFixedWidth(30); // Fixed width for 3 dots
    textLayout->addWidget(m_dotsLabel);

    labelLayout->addWidget(textRow);

    // Create opacity effect for label container
    m_opacityEffect = new QGraphicsOpacityEffect(this);
    labelContainer->setGraphicsEffect(m_opacityEffect);
    m_opacityEffect->setOpacity(0.0);

    mainLayout->addWidget(labelContainer);
}

void ProcessingOverlay::show(const QString &message)
{
    // Store base text and reset animation
    m_baseStatusText = message;
    m_animationDots = 0;

    // Update status message
    m_statusLabel->setText(message);
    m_dotsLabel->setText(QString());

    // Resize overlay to cover entire parent
    updateGeometry();

    // Show overlay immediately
    QWidget::show();
    raise(); // Bring to front

    // Start animation timer
    m_animationTimer->start();

    // Fade in animation for status label (400ms)
    auto *fadeIn = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    fadeIn->setDuration(400);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->setEasingCurve(QEasingCurve::InOutQuad);
    fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
}

void ProcessingOverlay::updateStatus(const QString &message)
{
    // Fade out animation (400ms)
    auto *fadeOut = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    fadeOut->setDuration(400);
    fadeOut->setStartValue(1.0);
    fadeOut->setEndValue(0.0);
    fadeOut->setEasingCurve(QEasingCurve::InOutQuad);

    // When fade out finishes, update text and fade in
    connect(fadeOut, &QPropertyAnimation::finished, this, [this, message]() {
        // Update text while invisible
        m_baseStatusText = message;
        m_animationDots = 0;
        m_statusLabel->setText(message);
        m_dotsLabel->setText(QString());

        // Fade in animation (400ms)
        auto *fadeIn = new QPropertyAnimation(m_opacityEffect, "opacity", this);
        fadeIn->setDuration(400);
        fadeIn->setStartValue(0.0);
        fadeIn->setEndValue(1.0);
        fadeIn->setEasingCurve(QEasingCurve::InOutQuad);
        fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
    });

    fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
}

void ProcessingOverlay::hide()
{
    // Stop animation timer
    m_animationTimer->stop();

    // Reset opacity for next time
    m_opacityEffect->setOpacity(0.0);

    // Hide overlay
    QWidget::hide();
}

void ProcessingOverlay::onAnimationTick()
{
    // Cycle through 0-3 dots
    m_animationDots = (m_animationDots + 1) % 4;

    // Update dots label only (main text stays unchanged)
    QString const dots = QStringLiteral(".").repeated(m_animationDots);
    m_dotsLabel->setText(dots);
}

void ProcessingOverlay::updateGeometry()
{
    if (parentWidget()) {
        setGeometry(0, 0, parentWidget()->width(), parentWidget()->height());
    }
}

} // namespace Daemon
} // namespace YubiKeyOath
