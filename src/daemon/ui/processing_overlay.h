/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QWidget>
#include <QString>

// Forward declarations
class QLabel;
class QTimer;
class QGraphicsOpacityEffect;

namespace KRunner {
namespace YubiKey {

/**
 * @brief Semi-transparent overlay widget with animated processing status
 *
 * Displays a centered status message with animated dots (...) on a semi-transparent
 * background that covers the parent widget. The status message fades in/out smoothly
 * when updated.
 *
 * Features:
 * - Semi-transparent background (78% opacity)
 * - Animated dots (0-3 dots cycling every 500ms)
 * - Smooth fade in/out transitions (400ms)
 * - Auto-resizes to cover parent widget
 *
 * Usage:
 * @code
 * auto *overlay = new ProcessingOverlay(parentWidget);
 * overlay->show("Processing...");  // Show with message
 * overlay->updateStatus("Almost done..."); // Update message with fade
 * overlay->hide();  // Hide overlay
 * @endcode
 */
class ProcessingOverlay : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Constructs processing overlay
     * @param parent Parent widget to overlay (must not be nullptr)
     */
    explicit ProcessingOverlay(QWidget *parent);

    /**
     * @brief Shows overlay with status message
     * @param message Status message to display
     */
    void show(const QString &message);

    /**
     * @brief Updates status message with fade animation
     * @param message New status message
     */
    void updateStatus(const QString &message);

    /**
     * @brief Hides overlay and stops animations
     */
    void hide();

private Q_SLOTS:
    void onAnimationTick();

private:
    void setupUi();
    void updateGeometry();

    // UI components
    QLabel *m_statusLabel;      // Main status text (doesn't change during animation)
    QLabel *m_dotsLabel;        // Animated dots label
    QGraphicsOpacityEffect *m_opacityEffect;  // Fade effect for status
    QTimer *m_animationTimer;   // Timer for dot animation

    // Animation state
    QString m_baseStatusText;   // Base text without dots
    int m_animationDots;        // Current dot count (0-3)
};

} // namespace YubiKey
} // namespace KRunner
