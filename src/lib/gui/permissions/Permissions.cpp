/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "Permissions.h"

#include "common/Settings.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QHash>
#include <QTimer>

namespace deskflow::gui::permissions {

namespace {

constexpr int kPollMs = 1000;

QString shortHash(const QString &text)
{
  if (text.isEmpty())
    return {};
  return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
}

} // namespace

QString settingsKey(Kind kind)
{
  switch (kind) {
  case Kind::Accessibility:
    return Settings::Permissions::AccessibilitySignature;
  case Kind::InputMonitoring:
    return Settings::Permissions::InputMonitoringSignature;
  case Kind::ScreenRecording:
    return Settings::Permissions::ScreenRecordingSignature;
  }
  return {};
}

PermissionMonitor::PermissionMonitor(QObject *parent) : QObject(parent), m_timer{new QTimer(this)}
{
  if (const auto fake = qEnvironmentVariable("DESKFLOW_FAKE_PERMISSIONS"); !fake.isEmpty()) {
    m_fake = true;
    const QHash<QString, Kind> kinds{
        {QStringLiteral("accessibility"), Kind::Accessibility},
        {QStringLiteral("input"), Kind::InputMonitoring},
        {QStringLiteral("screen"), Kind::ScreenRecording}
    };
    const QHash<QString, State> states{
        {QStringLiteral("granted"), State::Granted},
        {QStringLiteral("denied"), State::NotGranted},
        {QStringLiteral("stale"), State::Stale},
        {QStringLiteral("restart"), State::RestartNeeded}
    };
    for (const auto &pair : fake.split(QLatin1Char(','))) {
      const auto parts = pair.split(QLatin1Char('='));
      if (parts.size() == 2 && kinds.contains(parts[0]) && states.contains(parts[1]))
        m_fakeStates.insert(kinds.value(parts[0]), states.value(parts[1]));
    }
  }

  m_signature = m_fake ? QStringLiteral("fake") : shortHash(platform::signature());

  for (const auto kind : kAllKinds)
    m_states.insert(kind, compute(kind));

  if (!supported())
    return;

  connect(m_timer, &QTimer::timeout, this, &PermissionMonitor::refresh);
  m_timer->setInterval(kPollMs);
  m_timer->start();
  // Only poll while the app is in front: the user flips switches in System Settings and comes back.
  connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
    if (state == Qt::ApplicationActive) {
      refresh();
      m_timer->start(kPollMs);
    } else {
      // keep a slow check going so a grant still starts the core while System Settings is in front
      m_timer->setInterval(kPollMs * 2);
    }
  });
}

bool PermissionMonitor::supported() const
{
  return m_fake || platform::supported();
}

State PermissionMonitor::state(Kind kind) const
{
  return m_states.value(kind, State::Granted);
}

bool PermissionMonitor::isRequired(Kind kind) const
{
  return kind == Kind::Accessibility;
}

bool PermissionMonitor::ready() const
{
  return !supported() || state(Kind::Accessibility) == State::Granted;
}

int PermissionMonitor::attentionCount() const
{
  if (!supported())
    return 0;
  int count = 0;
  for (const auto kind : kAllKinds) {
    const auto s = state(kind);
    if (s == State::Granted)
      continue;
    const bool inUse = isRequired(kind) ||
                       (kind == Kind::ScreenRecording && Settings::value(Settings::RemoteView::AllowViewing).toBool());
    if (inUse || s == State::Stale)
      ++count;
  }
  return count;
}

bool PermissionMonitor::stableSignature() const
{
  return !m_fake && platform::stableSignature();
}

QString PermissionMonitor::signer() const
{
  return m_fake ? QString() : platform::signer();
}

bool PermissionMonitor::rawGranted(Kind kind) const
{
  if (m_fake)
    return m_fakeStates.value(kind, State::Granted) == State::Granted;
  return platform::granted(kind);
}

State PermissionMonitor::compute(Kind kind) const
{
  if (!supported())
    return State::Granted;
  if (m_fake && m_fakeStates.contains(kind) && !m_requested.contains(kind))
    return m_fakeStates.value(kind);
  if (rawGranted(kind))
    return State::Granted;
  // The process keeps its first answer for screen recording, so a switch turned on now only
  // shows up after a restart.
  if (kind == Kind::ScreenRecording && m_requested.contains(kind))
    return State::RestartNeeded;
  const auto held = Settings::value(settingsKey(kind)).toString();
  if (!held.isEmpty() && held != m_signature)
    return State::Stale;
  return State::NotGranted;
}

void PermissionMonitor::remember(Kind kind)
{
  if (m_fake || m_signature.isEmpty())
    return;
  if (Settings::value(settingsKey(kind)).toString() != m_signature)
    Settings::setValue(settingsKey(kind), m_signature);
}

void PermissionMonitor::refresh()
{
  bool any = false;
  for (const auto kind : kAllKinds) {
    const auto now = compute(kind);
    if (now == State::Granted)
      remember(kind);
    const auto before = m_states.value(kind);
    if (now == before)
      continue;
    m_states.insert(kind, now);
    any = true;
    qInfo().noquote() << "permission" << static_cast<int>(kind) << "changed to" << static_cast<int>(now);
    if (now == State::Granted)
      Q_EMIT granted(kind);
  }
  if (any)
    Q_EMIT changed();
}

void PermissionMonitor::allow(Kind kind)
{
  if (!supported())
    return;

  if (m_fake) {
    m_requested.append(kind);
    m_fakeStates.insert(kind, kind == Kind::ScreenRecording ? State::RestartNeeded : State::Granted);
    refresh();
    return;
  }

  if (!platform::granted(kind)) {
    // Whatever entry exists does not count for this build (stale) or was switched off.
    // Removing it lets the prompt below add a fresh one that the user only has to switch on.
    if (!platform::reset(kind))
      qWarning() << "unable to clear the permission entry" << static_cast<int>(kind);
    Settings::setValue(settingsKey(kind), QString());
  }
  m_requested.append(kind);
  platform::prompt(kind);
  // Apple's prompt adds the entry; bring the list up so the switch is right there.
  QTimer::singleShot(400, this, [kind] { platform::openSettings(kind); });
  refresh();
}

void PermissionMonitor::openSettings(Kind kind)
{
  if (!m_fake)
    platform::openSettings(kind);
}

void PermissionMonitor::resetAll()
{
  if (!m_fake && !platform::resetAll())
    qWarning("unable to clear the permission entries");
  for (const auto kind : kAllKinds)
    Settings::setValue(settingsKey(kind), QString());
  relaunch();
}

void PermissionMonitor::relaunch()
{
  if (m_fake)
    return;
  if (!platform::scheduleRelaunch(QCoreApplication::applicationPid())) {
    qWarning("unable to schedule a relaunch");
    return;
  }
  Q_EMIT quitRequested();
}

} // namespace deskflow::gui::permissions
