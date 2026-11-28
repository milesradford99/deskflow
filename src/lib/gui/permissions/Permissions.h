/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QMap>
#include <QObject>
#include <QString>

class QTimer;

/**
 * macOS privacy permissions (TCC): what Deskflow needs, whether it has it, and how to fix it.
 *
 * macOS keys each grant to the app's code signature (its designated requirement). An ad-hoc
 * signed build has a new signature every time, so after an update the old switch in System
 * Settings stays on but no longer counts, and asking again does nothing because an entry
 * already exists. The fix has two halves:
 * - CI signs with one stable certificate when one is configured, so grants carry over;
 * - the app remembers which signature last held each permission. When it is missing and that
 *   signature differs from ours, the entry is stale: Deskflow removes its own entry
 *   (`tccutil reset <service> <bundle id>`) and asks again, which adds a fresh one.
 */
namespace deskflow::gui::permissions {

enum class Kind
{
  Accessibility,   //!< send and read input (required: the core cannot work without it)
  InputMonitoring, //!< read the keyboard when this Mac is the server (some macOS versions want it)
  ScreenRecording  //!< screenshots of other apps' windows and remote viewing
};

inline const QList<Kind> kAllKinds{Kind::Accessibility, Kind::InputMonitoring, Kind::ScreenRecording};

enum class State
{
  Granted,
  NotGranted,
  //! Missing, but an earlier build held it: System Settings still shows a switch that no longer counts.
  Stale,
  //! Asked for in this run; macOS applies screen recording only after the app restarts.
  RestartNeeded
};

//! Thin wrappers over the system calls (PermissionsMac.mm, or a stub elsewhere).
namespace platform {
bool supported();
bool granted(Kind kind);
//! Show Apple's prompt (macOS shows it only when there is no entry yet) and add the app to the list.
void prompt(Kind kind);
//! Remove this app's entry for one permission. False if tccutil failed or there is no bundle id.
bool reset(Kind kind);
bool resetAll();
void openSettings(Kind kind);
//! Designated requirement of the running app: what TCC compares grants against.
QString signature();
//! True when signed with a certificate (stable across builds), false for ad-hoc signatures.
bool stableSignature();
//! Name of the signing certificate, empty when ad-hoc.
QString signer();
//! Open a new instance once this process (pid) has exited.
bool scheduleRelaunch(qint64 pid);
} // namespace platform

/**
 * @brief Watches the permissions while the app is in front, and does the fixing.
 *
 * `DESKFLOW_FAKE_PERMISSIONS="accessibility=granted,input=stale,screen=restart"` fakes the
 * states on any platform, for screenshots and tests.
 */
class PermissionMonitor : public QObject
{
  Q_OBJECT
public:
  explicit PermissionMonitor(QObject *parent = nullptr);

  //! False where there is nothing to ask for (not macOS).
  bool supported() const;
  State state(Kind kind) const;
  bool isRequired(Kind kind) const;
  //! The core may start (Accessibility is there, or the platform needs nothing).
  bool ready() const;
  //! Permissions that need the user now: required ones, stale ones, and screen recording when
  //! remote viewing is on. Shown as a badge in the sidebar.
  int attentionCount() const;

  bool stableSignature() const;
  QString signer() const;

  //! Remove a stale or refused entry, ask again and open the right pane of System Settings.
  void allow(Kind kind);
  void openSettings(Kind kind);
  //! Clear every Deskflow entry, then restart so the next prompts start from scratch.
  void resetAll();
  //! Quit and open again (screen recording only applies to a new process).
  void relaunch();

  //! Check now (also runs every second while the app is active).
  void refresh();

Q_SIGNALS:
  void changed();
  //! A permission turned on while the app was running.
  void granted(deskflow::gui::permissions::Kind kind);
  //! The app should quit now; a new instance opens once it has.
  void quitRequested();

private:
  State compute(Kind kind) const;
  bool rawGranted(Kind kind) const;
  void remember(Kind kind);

  QTimer *m_timer;
  QMap<Kind, State> m_states;
  QList<Kind> m_requested;
  QString m_signature;
  bool m_fake = false;
  QMap<Kind, State> m_fakeStates;
};

QString settingsKey(Kind kind);

} // namespace deskflow::gui::permissions
